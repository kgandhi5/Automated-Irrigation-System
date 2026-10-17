// Signal-conditioning tests: median, calibration mapping, plausibility,
// fixed-point EMA and fault rejection.
#include <irrigation_core.h>
#include <unity.h>

using namespace irr;

namespace {
constexpr uint16_t kDry = 600;
constexpr uint16_t kWet = 280;

void feed(SensorChannel& ch, uint16_t value, uint8_t n = 7) {
  uint16_t burst[SensorChannel::kMaxBurst];
  for (uint8_t i = 0; i < n; ++i) {
    burst[i] = value;
  }
  ch.update(burst, n, kDry, kWet);
}
}  // namespace

void setUp() {}
void tearDown() {}

void test_median_sorts_and_picks_middle() {
  uint16_t v[] = {5, 1, 4, 2, 3};
  TEST_ASSERT_EQUAL_UINT16(3, medianInPlace(v, 5));
  const uint16_t sorted[] = {1, 2, 3, 4, 5};
  TEST_ASSERT_EQUAL_UINT16_ARRAY(sorted, v, 5);
  TEST_ASSERT_EQUAL_UINT16(0, medianInPlace(v, 0));
}

void test_calibration_maps_both_directions() {
  TEST_ASSERT_EQUAL_UINT16(0, rawToDpct(600, kDry, kWet));
  TEST_ASSERT_EQUAL_UINT16(1000, rawToDpct(280, kDry, kWet));
  TEST_ASSERT_EQUAL_UINT16(500, rawToDpct(440, kDry, kWet));
  TEST_ASSERT_EQUAL_UINT16(0, rawToDpct(700, kDry, kWet));    // drier than dry: clamp
  TEST_ASSERT_EQUAL_UINT16(1000, rawToDpct(100, kDry, kWet)); // wetter than wet: clamp
  // Resistive probe: 1023 in air, ~350 in water.
  TEST_ASSERT_EQUAL_UINT16(0, rawToDpct(1023, 1023, 350));
  TEST_ASSERT_EQUAL_UINT16(1000, rawToDpct(350, 1023, 350));
  // A sensor wired the other way round (reads higher when wet).
  TEST_ASSERT_EQUAL_UINT16(500, rawToDpct(500, 200, 800));
  TEST_ASSERT_EQUAL_UINT16(0, rawToDpct(500, 500, 500));      // degenerate calibration
}

void test_plausibility_window_is_calibration_relative() {
  // span 320 -> 80 counts of margin -> valid window [200, 680]
  TEST_ASSERT_FALSE(rawPlausible(199, kDry, kWet));
  TEST_ASSERT_TRUE(rawPlausible(200, kDry, kWet));
  TEST_ASSERT_TRUE(rawPlausible(680, kDry, kWet));
  TEST_ASSERT_FALSE(rawPlausible(681, kDry, kWet));
  TEST_ASSERT_FALSE(rawPlausible(0, kDry, kWet));     // signal shorted to GND
  TEST_ASSERT_FALSE(rawPlausible(1023, kDry, kWet));  // signal shorted to VCC
  // For a resistive probe 1023 is simply "dry", not a fault.
  TEST_ASSERT_TRUE(rawPlausible(1023, 1023, 350));
}

void test_not_valid_until_first_good_burst() {
  SensorChannel ch;
  TEST_ASSERT_FALSE(ch.reading().valid);
  TEST_ASSERT_FALSE(ch.sampled());
  feed(ch, 440);
  TEST_ASSERT_TRUE(ch.reading().valid);
  TEST_ASSERT_EQUAL_UINT16(500, ch.reading().dpct);  // primed directly, no ramp from zero
}

void test_single_spike_is_rejected_by_median() {
  SensorChannel ch;
  uint16_t burst[] = {450, 451, 449, 450, 1023, 452, 450};
  ch.update(burst, 7, kDry, kWet);
  TEST_ASSERT_TRUE(ch.reading().valid);
  TEST_ASSERT_EQUAL_UINT16(450, ch.lastRaw());
  TEST_ASSERT_EQUAL_UINT16(469, ch.reading().dpct);  // (600-450)/(600-280) = 46.9 %
}

void test_noisy_floating_input_is_invalid() {
  SensorChannel ch;
  uint16_t burst[] = {250, 640, 300, 600, 380, 520, 450};  // all in range, but wandering
  ch.update(burst, 7, kDry, kWet);
  TEST_ASSERT_FALSE(ch.reading().valid);
  TEST_ASSERT_TRUE(ch.lastSpread() > SensorChannel::kMaxSpread);
}

void test_ema_converges_without_drift() {
  SensorChannel ch;
  feed(ch, 450);
  feed(ch, 290);
  TEST_ASSERT_EQUAL_UINT16(410, ch.filteredRaw());  // 450 + (290 - 450) / 4
  for (int i = 0; i < 40; ++i) {
    feed(ch, 290);
  }
  TEST_ASSERT_EQUAL_UINT16(290, ch.filteredRaw());
}

void test_invalid_bursts_do_not_pollute_filter() {
  SensorChannel ch;
  feed(ch, 450);
  for (int i = 0; i < 10; ++i) {
    feed(ch, 1010);  // open circuit
  }
  TEST_ASSERT_FALSE(ch.reading().valid);
  TEST_ASSERT_EQUAL_UINT16(1010, ch.lastRaw());      // still reported for diagnostics
  TEST_ASSERT_EQUAL_UINT16(450, ch.filteredRaw());   // filter untouched
  feed(ch, 450);
  TEST_ASSERT_TRUE(ch.reading().valid);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_median_sorts_and_picks_middle);
  RUN_TEST(test_calibration_maps_both_directions);
  RUN_TEST(test_plausibility_window_is_calibration_relative);
  RUN_TEST(test_not_valid_until_first_good_burst);
  RUN_TEST(test_single_spike_is_rejected_by_median);
  RUN_TEST(test_noisy_floating_input_is_invalid);
  RUN_TEST(test_ema_converges_without_drift);
  RUN_TEST(test_invalid_bursts_do_not_pollute_filter);
  return UNITY_END();
}
