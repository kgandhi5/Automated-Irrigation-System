#include "lcd.h"

namespace irr {

void Frame::clear() {
  for (uint8_t r = 0; r < kLcdRows; ++r) {
    for (uint8_t c = 0; c < kLcdCols; ++c) {
      cell[r][c] = ' ';
    }
  }
}

void Frame::put(uint8_t row, uint8_t col, char c) {
  if (row < kLcdRows && col < kLcdCols) {
    cell[row][col] = c;
  }
}

void Frame::text(uint8_t row, uint8_t col, const char* s) {
  while (*s != '\0' && col < kLcdCols) {
    put(row, col++, *s++);
  }
}

void Frame::number(uint8_t row, uint8_t lastCol, uint32_t value) {
  int16_t col = lastCol;
  do {
    if (col >= 0) {
      put(row, static_cast<uint8_t>(col), static_cast<char>('0' + value % 10));
    }
    value /= 10;
    --col;
  } while (value != 0);
}

void Frame::twoDigits(uint8_t row, uint8_t col, uint8_t value) {
  put(row, col, static_cast<char>('0' + (value / 10) % 10));
  put(row, static_cast<uint8_t>(col + 1), static_cast<char>('0' + value % 10));
}

uint8_t LcdMirror::flush(const Frame& f, CharDisplay& out) {
  uint8_t written = 0;
  for (uint8_t r = 0; r < kLcdRows; ++r) {
    // The HD44780 auto-increments its cursor, so a run of changed cells
    // needs only one setCursor().
    bool cursorHere = false;
    for (uint8_t c = 0; c < kLcdCols; ++c) {
      const char want = f.cell[r][c];
      if (valid_ && shown_[r][c] == want) {
        cursorHere = false;
        continue;
      }
      if (!cursorHere) {
        out.setCursor(c, r);
        cursorHere = true;
      }
      out.putChar(static_cast<uint8_t>(want));
      shown_[r][c] = want;
      ++written;
    }
  }
  valid_ = true;
  return written;
}

}  // namespace irr
