// State-machine tests for irr::Controller: the watering cycle, pump
// arbitration, fault handling, budgets and millis() rollover.
#include <irrigation_core.h>
#include <unity.h>

using namespace irr;

namespace {

PumpBank pumps;
Controller ctl(pumps);
Settings s;
Reading r[kZones];
uint32_t now = 0;

Event events[128];
int eventCount = 0;

void sink(const Event& e, void*) {
  if (eventCount < 128) {
    events[eventCount++] = e;
  }
}

int countEvents(EventType type, uint8_t zone) {
  int n = 0;
  for (int i = 0; i < eventCount; ++i) {
    if (events[i].type == type && events[i].zone == zone) {
      ++n;
    }
  }
  return n;
}

const Event* lastEvent(EventType type) {
  for (int i = eventCount - 1; i >= 0; --i) {
    if (events[i].type == type) {
      return &events[i];
    }
  }
  return nullptr;
}

void setAll(uint16_t dpct) {
  for (uint8_t z = 0; z < kZones; ++z) {
    r[z] = Reading{dpct, true};
  }
}

/// Mirrors the firmware loop: the pump guard runs alongside the controller.
void step(uint32_t dt = 50) {
  now += dt;
  pumps.enforce(now);
  ctl.tick(now, s, r);
}

void advance(uint32_t ms, uint32_t dt = 50) {
  for (uint32_t t = 0; t < ms; t += dt) {
    step(dt);
  }
}

/// Steps until `cond` holds; returns the time it took, or UINT32_MAX on timeout.
template <typename Cond>
uint32_t advanceUntil(Cond cond, uint32_t limitMs, uint32_t dt = 50) {
  for (uint32_t t = 0; t <= limitMs; t += dt) {
    if (cond()) {
      return t;
    }
    step(dt);
  }
  return UINT32_MAX;
}

void boot(uint32_t start) {
  pumps.reset();
  loadDefaults(s);
  s.cooldownMin = 1;
  now = start;
  eventCount = 0;
  setAll(900);
  ctl.begin(now);
  ctl.setEventSink(sink);
  advance(timing::kWarmupMs + 100);
}

#define ASSERT_STATE(z, st) TEST_ASSERT_EQUAL_INT(static_cast<int>(st), static_cast<int>(ctl.state(z)))

}  // namespace

void setUp() { boot(1000); }
void tearDown() {}

void test_nothing_happens_during_warmup() {
  pumps.reset();
  loadDefaults(s);
  now = 0;
  setAll(100);  // bone dry
  ctl.begin(now);
  advance(timing::kWarmupMs - 100);
  TEST_ASSERT_FALSE(ctl.warm());
  TEST_ASSERT_FALSE(pumps.anyOn());
  ASSERT_STATE(0, ZoneState::Idle);
}

void test_dryness_must_persist_before_cycle_starts() {
  r[0].dpct = 650;
  advance(timing::kDryConfirmMs - 200);
  ASSERT_STATE(0, ZoneState::Idle);
  r[0].dpct = 750;  // one wet reading resets the debounce
  step();
  r[0].dpct = 650;
  advance(timing::kDryConfirmMs - 200);
  ASSERT_STATE(0, ZoneState::Idle);
  TEST_ASSERT_NOT_EQUAL(UINT32_MAX, advanceUntil([] { return pumps.isOn(0); }, 500));
}

void test_full_cycle_reaches_target_then_cools_down() {
  r[0].dpct = 650;
  TEST_ASSERT_NOT_EQUAL(UINT32_MAX, advanceUntil([] { return pumps.isOn(0); }, 6000));
  ASSERT_STATE(0, ZoneState::Watering);
  TEST_ASSERT_EQUAL_INT(1, countEvents(EventType::CycleStart, 0));

  const uint32_t onFor = advanceUntil([] { return !pumps.isOn(0); }, 5000);
  TEST_ASSERT_UINT32_WITHIN(60, defaults::kPulseMs, onFor);
  ASSERT_STATE(0, ZoneState::Soaking);
  TEST_ASSERT_UINT32_WITHIN(60, defaults::kPulseMs, lastEvent(EventType::PumpOff)->value);

  r[0].dpct = 850;  // water reached the sensor
  advance(s.soakSec * 1000UL);
  ASSERT_STATE(0, ZoneState::Cooldown);
  TEST_ASSERT_EQUAL_INT(1, countEvents(EventType::CycleDone, 0));
  TEST_ASSERT_EQUAL_UINT16(1, ctl.stats(0).cycles);

  advance(s.cooldownMin * 60000UL);
  ASSERT_STATE(0, ZoneState::Idle);
  TEST_ASSERT_UINT32_WITHIN(60, defaults::kPulseMs, ctl.waterTodayMs(0));
}

void test_pulses_repeat_until_target_or_limit() {
  r[0].dpct = 650;
  for (uint8_t p = 1; p <= s.maxPulses; ++p) {
    TEST_ASSERT_NOT_EQUAL(UINT32_MAX, advanceUntil([] { return pumps.isOn(0); }, 10000));
    TEST_ASSERT_EQUAL_UINT8(p, ctl.pulsesThisCycle(0));
    advanceUntil([] { return ctl.state(0) != ZoneState::Watering; }, 5000);
    r[0].dpct = static_cast<uint16_t>(r[0].dpct + 20);  // +2 % per pulse, never reaches 80 %
    advance(s.soakSec * 1000UL);
  }
  // Rose 8 % in total: the plumbing works, the plant is just thirsty.
  ASSERT_STATE(0, ZoneState::Cooldown);
  TEST_ASSERT_EQUAL_INT(1, countEvents(EventType::CyclePartial, 0));
  TEST_ASSERT_EQUAL_UINT16(s.maxPulses, ctl.stats(0).pulses);
}

void test_no_rise_latches_fault_until_cleared() {
  r[0].dpct = 650;  // the pump "runs" but moisture never moves: empty reservoir
  advance(s.maxPulses * (defaults::kPulseMs + s.soakSec * 1000UL + 2000UL) + 6000UL);
  ASSERT_STATE(0, ZoneState::FaultNoRise);
  TEST_ASSERT_EQUAL_INT(1, countEvents(EventType::FaultNoRise, 0));
  TEST_ASSERT_TRUE(ctl.anyFault());

  const uint16_t pulses = ctl.stats(0).pulses;
  advance(30UL * 60UL * 1000UL, 1000);  // half an hour later: still latched, no pumping
  ASSERT_STATE(0, ZoneState::FaultNoRise);
  TEST_ASSERT_EQUAL_UINT16(pulses, ctl.stats(0).pulses);

  TEST_ASSERT_TRUE(ctl.clearFault(0, now));
  ASSERT_STATE(0, ZoneState::Idle);
  TEST_ASSERT_NOT_EQUAL(UINT32_MAX, advanceUntil([] { return pumps.isOn(0); }, 6000));
}

void test_no_rise_latch_survives_sensor_glitch() {
  r[0].dpct = 650;
  advance(s.maxPulses * (defaults::kPulseMs + s.soakSec * 1000UL + 2000UL) + 6000UL);
  ASSERT_STATE(0, ZoneState::FaultNoRise);
  r[0].valid = false;  // loose connector while the tank is still empty
  advance(timing::kSensorFaultMs * 2);
  r[0].valid = true;
  advance(timing::kSensorRecoverMs * 2);
  ASSERT_STATE(0, ZoneState::FaultNoRise);
}

void test_one_pump_at_a_time_driest_first_with_dead_time() {
  r[0].dpct = 680;
  r[1].dpct = 600;  // driest
  r[2].dpct = 690;
  r[3].dpct = 650;
  TEST_ASSERT_NOT_EQUAL(UINT32_MAX, advanceUntil([] { return pumps.anyOn(); }, 6000));
  TEST_ASSERT_TRUE(pumps.isOn(1));
  TEST_ASSERT_EQUAL_UINT8(1, pumps.countOn());
  ASSERT_STATE(0, ZoneState::Queued);
  ASSERT_STATE(3, ZoneState::Queued);

  advanceUntil([] { return !pumps.isOn(1); }, 5000);
  const uint32_t gap = advanceUntil([] { return pumps.anyOn(); }, 2000);
  TEST_ASSERT_TRUE(gap >= timing::kInterPumpGapMs - 50);
  TEST_ASSERT_TRUE(pumps.isOn(3));  // next driest
  TEST_ASSERT_EQUAL_UINT8(1, pumps.countOn());
}

void test_queued_zone_is_released_if_watered_by_hand() {
  r[0].dpct = 600;
  r[1].dpct = 650;
  advanceUntil([] { return pumps.isOn(0); }, 6000);
  ASSERT_STATE(1, ZoneState::Queued);
  r[1].dpct = 850;
  step();
  ASSERT_STATE(1, ZoneState::Idle);
}

void test_sensor_fault_stops_pump_and_recovers() {
  s.zone[0].pulseMs = 9000;  // long enough for the fault to land mid-pulse
  r[0].dpct = 650;
  advanceUntil([] { return pumps.isOn(0); }, 6000);
  r[0].valid = false;
  advance(timing::kSensorFaultMs - 100);
  TEST_ASSERT_TRUE(pumps.isOn(0));
  advance(200);
  ASSERT_STATE(0, ZoneState::FaultSensor);
  TEST_ASSERT_FALSE(pumps.isOn(0));
  TEST_ASSERT_EQUAL_INT(1, countEvents(EventType::FaultSensor, 0));

  r[0] = Reading{850, true};
  advance(timing::kSensorRecoverMs - 1000);
  ASSERT_STATE(0, ZoneState::FaultSensor);  // must stay good for the full window
  advance(1100);
  ASSERT_STATE(0, ZoneState::Idle);
  TEST_ASSERT_EQUAL_INT(1, countEvents(EventType::SensorRecovered, 0));
}

void test_brief_glitch_does_not_trip_sensor_fault() {
  r[0].valid = false;
  advance(timing::kSensorFaultMs - 500);
  r[0].valid = true;
  advance(timing::kSensorFaultMs);
  ASSERT_STATE(0, ZoneState::Idle);
}

void test_daily_budget_holds_then_resets() {
  s.budgetSec = 10;  // room for three 3 s pulses
  s.maxPulses = 10;
  r[0].dpct = 650;
  advance(5UL * 60UL * 1000UL);  // +0 rise each soak; budget runs out first
  ASSERT_STATE(0, ZoneState::Hold);
  TEST_ASSERT_EQUAL_UINT16(3, ctl.stats(0).pulses);
  TEST_ASSERT_TRUE(ctl.waterTodayMs(0) <= 10000UL);

  advance(timing::kBudgetWindowMs, 1000);  // next day
  TEST_ASSERT_TRUE(countEvents(EventType::BudgetReset, kAllZones) >= 1);
  TEST_ASSERT_TRUE(ctl.stats(0).pulses > 3);
}

void test_disabled_zone_never_waters() {
  s.zone[2].enabled = 0;
  r[2].dpct = 100;
  advance(60000);
  ASSERT_STATE(2, ZoneState::Disabled);
  TEST_ASSERT_EQUAL_UINT16(0, ctl.stats(2).pulses);
  s.zone[2].enabled = 1;
  step();
  ASSERT_STATE(2, ZoneState::Idle);
}

void test_hard_guard_trip_is_detected_and_accounted() {
  r[0].dpct = 650;
  advanceUntil([] { return pumps.isOn(0); }, 6000);
  // Controller stalls; only the independent guard keeps running.
  now += kPumpHardMaxMs;
  TEST_ASSERT_EQUAL_HEX8(0x01, pumps.enforce(now));
  step();
  ASSERT_STATE(0, ZoneState::Soaking);
  TEST_ASSERT_EQUAL_INT(1, countEvents(EventType::GuardTrip, 0));
  TEST_ASSERT_TRUE(ctl.waterTodayMs(0) >= kPumpHardMaxMs);
}

void test_cycle_survives_millis_rollover() {
  boot(0xFFFFFFFFUL - 12000UL);  // wraps during the pulse below
  r[0].dpct = 650;
  advanceUntil([] { return pumps.isOn(0); }, 6000);
  const uint32_t onFor = advanceUntil([] { return !pumps.isOn(0); }, 5000);
  TEST_ASSERT_UINT32_WITHIN(60, defaults::kPulseMs, onFor);
  r[0].dpct = 850;
  advance(s.soakSec * 1000UL);
  ASSERT_STATE(0, ZoneState::Cooldown);
}

void test_manual_pulse_runs_once_and_returns_idle() {
  TEST_ASSERT_TRUE(ctl.manualPulse(1, 1000, now));
  step();
  TEST_ASSERT_TRUE(pumps.isOn(1));
  const uint32_t onFor = advanceUntil([] { return !pumps.isOn(1); }, 3000);
  TEST_ASSERT_UINT32_WITHIN(60, 1000, onFor);
  advance(s.soakSec * 1000UL);
  ASSERT_STATE(1, ZoneState::Idle);
  TEST_ASSERT_EQUAL_UINT16(0, ctl.stats(1).cycles);
  TEST_ASSERT_FALSE(ctl.manualPulse(9, 1000, now));
}

void test_emergency_stop_halts_everything() {
  r[0].dpct = 600;
  r[1].dpct = 650;
  advanceUntil([] { return pumps.isOn(0); }, 6000);
  ctl.emergencyStop(now);
  TEST_ASSERT_FALSE(pumps.anyOn());
  ASSERT_STATE(0, ZoneState::Cooldown);
  ASSERT_STATE(1, ZoneState::Cooldown);
  advance(5000);
  TEST_ASSERT_FALSE(pumps.anyOn());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_nothing_happens_during_warmup);
  RUN_TEST(test_dryness_must_persist_before_cycle_starts);
  RUN_TEST(test_full_cycle_reaches_target_then_cools_down);
  RUN_TEST(test_pulses_repeat_until_target_or_limit);
  RUN_TEST(test_no_rise_latches_fault_until_cleared);
  RUN_TEST(test_no_rise_latch_survives_sensor_glitch);
  RUN_TEST(test_one_pump_at_a_time_driest_first_with_dead_time);
  RUN_TEST(test_queued_zone_is_released_if_watered_by_hand);
  RUN_TEST(test_sensor_fault_stops_pump_and_recovers);
  RUN_TEST(test_brief_glitch_does_not_trip_sensor_fault);
  RUN_TEST(test_daily_budget_holds_then_resets);
  RUN_TEST(test_disabled_zone_never_waters);
  RUN_TEST(test_hard_guard_trip_is_detected_and_accounted);
  RUN_TEST(test_cycle_survives_millis_rollover);
  RUN_TEST(test_manual_pulse_runs_once_and_returns_idle);
  RUN_TEST(test_emergency_stop_halts_everything);
  return UNITY_END();
}
