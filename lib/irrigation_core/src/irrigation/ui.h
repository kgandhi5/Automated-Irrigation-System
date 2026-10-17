// LCD page layouts. Rendering is a pure function of the model, so every page
// is unit-tested on the host character for character.
//
//   Moisture (main)      Status               System
//   +----------------+   +----------------+   +----------------+
//   | P1  P2* P3  P4 |   |1:OK    2:PUMP  |   |Box 1   2d 03:15|
//   | 59% 97% 73% 62%|   |3:SOAK  4:FAIL  |   |Today      450mL|
//   +----------------+   +----------------+   +----------------+
//   (* = custom glyph: water drop while pumping, hourglass while queued/soaking)
#pragma once

#include <stdint.h>

#include "controller.h"
#include "lcd.h"
#include "types.h"

namespace irr {

enum class Page : uint8_t { Splash, Moisture, Status, System };

/// CGRAM slots for the custom glyphs (0 is avoided so frames stay C-string safe).
constexpr char kGlyphDrop = 1;
constexpr char kGlyphWait = 2;

constexpr uint8_t kSplashSec = 3;
constexpr uint8_t kRotationSec = 12;  ///< Moisture 7 s, Status 3 s, System 2 s

struct UiModel {
  const Reading* readings;
  const Controller* controller;
  uint8_t boxId;
  uint32_t uptimeSec;
  uint32_t waterTodayMl;
  const char* version;
};

Page pageFor(uint32_t uptimeSec);
char zoneGlyph(ZoneState s);
void render(Page p, const UiModel& m, Frame& f);

}  // namespace irr
