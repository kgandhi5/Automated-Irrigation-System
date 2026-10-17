// Settings validation and the CRC-protected EEPROM image.
#include <irrigation_core.h>
#include <string.h>
#include <unity.h>

using namespace irr;

void setUp() {}
void tearDown() {}

void test_crc16_matches_reference_vector() {
  const char* check = "123456789";  // CRC-16/CCITT-FALSE check value is 0x29B1
  TEST_ASSERT_EQUAL_HEX16(0x29B1, crc16(reinterpret_cast<const uint8_t*>(check), 9));
}

void test_defaults_are_already_sane() {
  Settings s;
  loadDefaults(s);
  TEST_ASSERT_FALSE(sanitize(s));
  TEST_ASSERT_EQUAL_UINT8(70, s.zone[0].dryPct);
}

void test_sanitize_clamps_and_repairs() {
  Settings s;
  loadDefaults(s);
  s.zone[0].dryPct = 99;
  s.zone[1].targetPct = 50;  // below dry (70)
  s.zone[2].pulseMs = 50000;
  s.zone[3].rawDry = 500;
  s.zone[3].rawWet = 480;    // span too small to be a real calibration
  s.zone[0].enabled = 7;
  s.soakSec = 0;
  s.telemetryMs = 5;
  TEST_ASSERT_TRUE(sanitize(s));
  TEST_ASSERT_EQUAL_UINT8(limits::kDryPctMax, s.zone[0].dryPct);
  TEST_ASSERT_TRUE(s.zone[0].targetPct > s.zone[0].dryPct);
  TEST_ASSERT_EQUAL_UINT8(71, s.zone[1].targetPct);
  TEST_ASSERT_EQUAL_UINT16(limits::kPulseMaxMs, s.zone[2].pulseMs);
  TEST_ASSERT_EQUAL_UINT16(defaults::kRawDry, s.zone[3].rawDry);
  TEST_ASSERT_EQUAL_UINT16(defaults::kRawWet, s.zone[3].rawWet);
  TEST_ASSERT_EQUAL_UINT8(1, s.zone[0].enabled);
  TEST_ASSERT_EQUAL_UINT16(limits::kSoakSecMin, s.soakSec);
  TEST_ASSERT_EQUAL_UINT16(limits::kTelemetryMinMs, s.telemetryMs);
  TEST_ASSERT_FALSE(sanitize(s));  // idempotent
}

void test_pulse_limit_stays_below_hard_guard() {
  TEST_ASSERT_TRUE(limits::kPulseMaxMs < kPumpHardMaxMs);
}

void test_image_round_trip() {
  Settings in;
  loadDefaults(in);
  in.boxId = 2;
  in.zone[1].dryPct = 55;
  ConfigImage img;
  sealImage(img, in);
  Settings out;
  loadDefaults(out);
  TEST_ASSERT_TRUE(openImage(img, out));
  TEST_ASSERT_EQUAL_MEMORY(&in, &out, sizeof(Settings));
}

void test_corrupted_image_is_rejected() {
  Settings in;
  loadDefaults(in);
  ConfigImage img;
  sealImage(img, in);
  Settings out;
  memset(&out, 0xAB, sizeof out);
  Settings untouched = out;

  ConfigImage bad = img;
  reinterpret_cast<uint8_t*>(&bad.settings)[5] ^= 0x01;  // single bit flip
  TEST_ASSERT_FALSE(openImage(bad, out));
  TEST_ASSERT_EQUAL_MEMORY(&untouched, &out, sizeof(Settings));

  bad = img;
  bad.version = kConfigVersion + 1;
  TEST_ASSERT_FALSE(openImage(bad, out));

  ConfigImage blank;
  memset(&blank, 0xFF, sizeof blank);  // factory-fresh EEPROM
  TEST_ASSERT_FALSE(openImage(blank, out));
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_crc16_matches_reference_vector);
  RUN_TEST(test_defaults_are_already_sane);
  RUN_TEST(test_sanitize_clamps_and_repairs);
  RUN_TEST(test_pulse_limit_stays_below_hard_guard);
  RUN_TEST(test_image_round_trip);
  RUN_TEST(test_corrupted_image_is_rejected);
  return UNITY_END();
}
