// Pump interlocks: concurrency limit and the independent hard on-time guard.
#include <irrigation_core.h>
#include <unity.h>

using namespace irr;

namespace {
PumpBank pumps;
}

void setUp() { pumps.reset(); }
void tearDown() {}

void test_only_one_pump_at_a_time() {
  TEST_ASSERT_TRUE(pumps.start(0, 100));
  TEST_ASSERT_FALSE(pumps.start(1, 100));
  TEST_ASSERT_FALSE(pumps.start(0, 100));  // already on
  TEST_ASSERT_EQUAL_HEX8(0x01, pumps.mask());
  TEST_ASSERT_FALSE(pumps.start(kZones, 100));  // out of range
}

void test_stop_reports_runtime() {
  pumps.start(2, 1000);
  TEST_ASSERT_EQUAL_UINT32(2500, pumps.stop(2, 3500));
  TEST_ASSERT_EQUAL_UINT32(3500, pumps.lastOffAt());
  TEST_ASSERT_EQUAL_UINT32(0, pumps.stop(2, 4000));  // already off
  TEST_ASSERT_TRUE(pumps.start(3, 4000));
}

void test_hard_guard_trips_at_limit() {
  pumps.start(1, 0);
  TEST_ASSERT_EQUAL_HEX8(0x00, pumps.enforce(kPumpHardMaxMs - 1));
  TEST_ASSERT_TRUE(pumps.isOn(1));
  TEST_ASSERT_EQUAL_HEX8(0x02, pumps.enforce(kPumpHardMaxMs));
  TEST_ASSERT_FALSE(pumps.anyOn());
  TEST_ASSERT_EQUAL_UINT32(kPumpHardMaxMs, pumps.lastRunMs(1));
  TEST_ASSERT_EQUAL_UINT16(1, pumps.guardTrips());
}

void test_hard_guard_across_millis_rollover() {
  const uint32_t start = 0xFFFFF000UL;
  pumps.start(0, start);
  TEST_ASSERT_EQUAL_HEX8(0x00, pumps.enforce(start + 5000));  // wrapped, only 5 s in
  TEST_ASSERT_EQUAL_HEX8(0x01, pumps.enforce(start + kPumpHardMaxMs));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_only_one_pump_at_a_time);
  RUN_TEST(test_stop_reports_runtime);
  RUN_TEST(test_hard_guard_trips_at_limit);
  RUN_TEST(test_hard_guard_across_millis_rollover);
  return UNITY_END();
}
