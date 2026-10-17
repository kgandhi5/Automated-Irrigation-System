// 16x2 character LCD frame buffer with dirty-cell flushing.
//
// Pages are rendered into a Frame in RAM; LcdMirror remembers what the panel
// currently shows and only sends the cells that changed. That avoids the
// visible flicker of lcd.clear() (a 1.5 ms blanking command on the HD44780)
// and keeps the UI task short, which matters most on a slow I2C backpack.
#pragma once

#include <stdint.h>

namespace irr {

constexpr uint8_t kLcdCols = 16;
constexpr uint8_t kLcdRows = 2;

/// Minimal output interface so the core never touches an LCD library.
class CharDisplay {
 public:
  virtual void setCursor(uint8_t col, uint8_t row) = 0;
  virtual void putChar(uint8_t c) = 0;

 protected:
  ~CharDisplay() {}
};

struct Frame {
  char cell[kLcdRows][kLcdCols];

  void clear();
  /// Writes `s` starting at (row, col), clipped to the row.
  void text(uint8_t row, uint8_t col, const char* s);
  void put(uint8_t row, uint8_t col, char c);
  /// Writes `value` right-aligned so its last digit lands on `lastCol`.
  void number(uint8_t row, uint8_t lastCol, uint32_t value);
  /// Zero-padded two digits at (row, col).
  void twoDigits(uint8_t row, uint8_t col, uint8_t value);
};

class LcdMirror {
 public:
  /// Forces a full repaint on the next flush (after lcd.clear() / createChar()).
  void invalidate() { valid_ = false; }
  /// Sends changed cells to `out`. Returns the number of characters written.
  uint8_t flush(const Frame& f, CharDisplay& out);

 private:
  char shown_[kLcdRows][kLcdCols] = {};
  bool valid_ = false;
};

}  // namespace irr
