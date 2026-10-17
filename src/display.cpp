#include "display.h"

#include <Arduino.h>
#include <avr/pgmspace.h>

#include "board_config.h"

#if LCD_I2C
#include <LiquidCrystal_I2C.h>
#include <Wire.h>
#else
#include <LiquidCrystal.h>
#endif

namespace {

#if LCD_I2C
LiquidCrystal_I2C lcd(LCD_I2C_ADDR, irr::kLcdCols, irr::kLcdRows);
#else
LiquidCrystal lcd(LCD_PIN_RS, LCD_PIN_EN, LCD_PIN_D4, LCD_PIN_D5, LCD_PIN_D6, LCD_PIN_D7);
#endif

const uint8_t kDrop[8] PROGMEM = {
    0b00100, 0b00100, 0b01010, 0b01010, 0b10001, 0b10001, 0b01110, 0b00000,
};
const uint8_t kHourglass[8] PROGMEM = {
    0b11111, 0b10001, 0b01010, 0b00100, 0b01010, 0b10101, 0b11111, 0b00000,
};

class LcdAdapter : public irr::CharDisplay {
 public:
  void setCursor(uint8_t col, uint8_t row) override { lcd.setCursor(col, row); }
  void putChar(uint8_t c) override { lcd.write(c); }
};

LcdAdapter adapter;
irr::LcdMirror mirror;

void loadGlyph(uint8_t slot, const uint8_t* bitmapP) {
  uint8_t buf[8];
  memcpy_P(buf, bitmapP, sizeof buf);
  lcd.createChar(slot, buf);
}

}  // namespace

namespace display {

void begin() {
#if LCD_I2C
  lcd.init();
  lcd.backlight();
#else
  lcd.begin(irr::kLcdCols, irr::kLcdRows);
#endif
  loadGlyph(static_cast<uint8_t>(irr::kGlyphDrop), kDrop);
  loadGlyph(static_cast<uint8_t>(irr::kGlyphWait), kHourglass);
  lcd.clear();
  mirror.invalidate();
}

void show(const irr::Frame& frame) { mirror.flush(frame, adapter); }

}  // namespace display
