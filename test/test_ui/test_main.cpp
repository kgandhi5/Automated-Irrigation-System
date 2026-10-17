// LCD pages (character-exact), dirty-cell flushing, LED patterns and the
// uptime/volume helpers.
#include <irrigation_core.h>
#include <string.h>
#include <unity.h>

using namespace irr;

namespace {

/// Records what a real HD44780 would end up showing.
class FakeLcd : public CharDisplay {
 public:
  char screen[kLcdRows][kLcdCols + 1];
  int cursorMoves = 0;
  int chars = 0;

  FakeLcd() { memset(screen, 0, sizeof screen); }
  void setCursor(uint8_t col, uint8_t row) override {
    col_ = col;
    row_ = row;
    ++cursorMoves;
  }
  void putChar(uint8_t c) override {
    screen[row_][col_++] = static_cast<char>(c);
    ++chars;
  }

 private:
  uint8_t col_ = 0;
  uint8_t row_ = 0;
};

PumpBank pumps;
Controller ctl(pumps);
Settings s;
Reading r[kZones];
Frame frame;

void rowEquals(const char* expected, uint8_t row) {
  char got[kLcdCols + 1];
  memcpy(got, frame.cell[row], kLcdCols);
  got[kLcdCols] = '\0';
  TEST_ASSERT_EQUAL_STRING(expected, got);
}

UiModel model(uint32_t uptimeSec = 100) {
  UiModel m;
  m.readings = r;
  m.controller = &ctl;
  m.boxId = 1;
  m.uptimeSec = uptimeSec;
  m.waterTodayMl = 450;
  m.version = "1.0.0";
  return m;
}

}  // namespace

void setUp() {
  pumps.reset();
  loadDefaults(s);
  r[0] = Reading{594, true};
  r[1] = Reading{970, true};
  r[2] = Reading{730, true};
  r[3] = Reading{1000, true};
  ctl.begin(0);
}
void tearDown() {}

void test_moisture_page_layout() {
  render(Page::Moisture, model(), frame);
  rowEquals(" P1  P2  P3  P4 ", 0);
  rowEquals(" 59% 97% 73%100%", 1);
}

void test_moisture_page_shows_faults_and_disabled_zones() {
  s.zone[2].enabled = 0;
  r[0].dpct = 750;        // wet enough that P1 stays idle
  r[1].valid = false;
  ctl.tick(6000, s, r);   // warm-up done
  ctl.tick(11100, s, r);  // P2 invalid for > kSensorFaultMs
  r[3].dpct = 620;
  render(Page::Moisture, model(), frame);
  rowEquals(" P1  P2! P3  P4 ", 0);
  rowEquals(" 75% Err --  62%", 1);

  render(Page::Status, model(), frame);
  rowEquals("1:OK    2:SENS  ", 0);
  rowEquals("3:OFF   4:OK    ", 1);
}

void test_glyphs_follow_zone_state() {
  TEST_ASSERT_EQUAL_INT(kGlyphDrop, zoneGlyph(ZoneState::Watering));
  TEST_ASSERT_EQUAL_INT(kGlyphWait, zoneGlyph(ZoneState::Soaking));
  TEST_ASSERT_EQUAL_INT(kGlyphWait, zoneGlyph(ZoneState::Queued));
  TEST_ASSERT_EQUAL_INT('!', zoneGlyph(ZoneState::FaultNoRise));
  TEST_ASSERT_EQUAL_INT(' ', zoneGlyph(ZoneState::Idle));
}

void test_system_and_splash_pages() {
  render(Page::System, model(2 * 86400UL + 3 * 3600UL + 15 * 60UL), frame);
  rowEquals("Box 1   2d 03:15", 0);
  rowEquals("Today      450mL", 1);

  render(Page::Splash, model(), frame);
  rowEquals("Smart Irrigation", 0);
  rowEquals("fw 1.0.0   Box 1", 1);
}

void test_page_rotation() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Page::Splash), static_cast<int>(pageFor(0)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Page::Splash), static_cast<int>(pageFor(2)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Page::Moisture), static_cast<int>(pageFor(3)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Page::Status), static_cast<int>(pageFor(7)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Page::System), static_cast<int>(pageFor(11)));
  TEST_ASSERT_EQUAL_INT(static_cast<int>(Page::Moisture), static_cast<int>(pageFor(12)));
}

void test_mirror_sends_only_changed_cells() {
  FakeLcd lcd;
  LcdMirror mirror;
  render(Page::Moisture, model(), frame);
  TEST_ASSERT_EQUAL_UINT8(32, mirror.flush(frame, lcd));
  TEST_ASSERT_EQUAL_INT(2, lcd.cursorMoves);  // one per row
  TEST_ASSERT_EQUAL_UINT8(0, mirror.flush(frame, lcd));

  r[1].dpct = 950;  // 97% -> 95%
  render(Page::Moisture, model(), frame);
  lcd.cursorMoves = 0;
  TEST_ASSERT_EQUAL_UINT8(1, mirror.flush(frame, lcd));
  TEST_ASSERT_EQUAL_INT(1, lcd.cursorMoves);
  TEST_ASSERT_EQUAL_STRING(" 59% 95% 73%100%", lcd.screen[1]);

  mirror.invalidate();
  TEST_ASSERT_EQUAL_UINT8(32, mirror.flush(frame, lcd));
}

void test_led_patterns() {
  TEST_ASSERT_TRUE(ledLevel(LedMode::Pumping, 0));
  TEST_ASSERT_TRUE(ledLevel(LedMode::Pumping, 3999));
  TEST_ASSERT_TRUE(ledLevel(LedMode::Heartbeat, 0));
  TEST_ASSERT_FALSE(ledLevel(LedMode::Heartbeat, 125));
  TEST_ASSERT_TRUE(ledLevel(LedMode::Heartbeat, 4000));
  TEST_ASSERT_TRUE(ledLevel(LedMode::Fault, 0));
  TEST_ASSERT_FALSE(ledLevel(LedMode::Fault, 125));
  TEST_ASSERT_TRUE(ledLevel(LedMode::Fault, 250));
  TEST_ASSERT_FALSE(ledLevel(LedMode::Fault, 375));
}

void test_led_mode_priority() {
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LedMode::Heartbeat),
                        static_cast<int>(selectLedMode(ctl, pumps)));
  pumps.start(0, 0);
  TEST_ASSERT_EQUAL_INT(static_cast<int>(LedMode::Pumping),
                        static_cast<int>(selectLedMode(ctl, pumps)));
}

void test_water_volume_and_uptime_helpers() {
  TEST_ASSERT_EQUAL_UINT32(1500, waterMl(60000, 1500));
  TEST_ASSERT_EQUAL_UINT32(75, waterMl(3000, 1500));
  // Worst case the settings allow: 4 zones x 3600 s at 10 L/min, no overflow.
  TEST_ASSERT_EQUAL_UINT32(2400000UL, waterMl(4UL * 3600UL * 1000UL, 10000));

  UptimeClock clock;
  clock.begin(0xFFFFFC18UL);   // 1 s before millis() wraps
  clock.update(0x000003E8UL);  // 1 s after
  TEST_ASSERT_EQUAL_UINT32(2, clock.seconds());
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_moisture_page_layout);
  RUN_TEST(test_moisture_page_shows_faults_and_disabled_zones);
  RUN_TEST(test_glyphs_follow_zone_state);
  RUN_TEST(test_system_and_splash_pages);
  RUN_TEST(test_page_rotation);
  RUN_TEST(test_mirror_sends_only_changed_cells);
  RUN_TEST(test_led_patterns);
  RUN_TEST(test_led_mode_priority);
  RUN_TEST(test_water_volume_and_uptime_helpers);
  return UNITY_END();
}
