#include "ui.h"

namespace irr {

namespace {

void renderSplash(const UiModel& m, Frame& f) {
  f.text(0, 0, "Smart Irrigation");
  f.text(1, 0, "fw ");
  f.text(1, 3, m.version);
  f.text(1, 11, "Box ");
  f.number(1, 15, m.boxId);
}

void renderMoisture(const UiModel& m, Frame& f) {
  for (uint8_t z = 0; z < kZones; ++z) {
    const uint8_t col = static_cast<uint8_t>(z * 4);
    const ZoneState st = m.controller->state(z);
    f.put(0, static_cast<uint8_t>(col + 1), 'P');
    f.put(0, static_cast<uint8_t>(col + 2), static_cast<char>('1' + z));
    f.put(0, static_cast<uint8_t>(col + 3), zoneGlyph(st));

    if (st == ZoneState::Disabled) {
      f.text(1, static_cast<uint8_t>(col + 1), "--");
    } else if (st == ZoneState::FaultSensor) {
      f.text(1, static_cast<uint8_t>(col + 1), "Err");
    } else {
      const uint16_t pct = static_cast<uint16_t>((m.readings[z].dpct + 5) / 10);
      f.number(1, static_cast<uint8_t>(col + 2), pct);
      f.put(1, static_cast<uint8_t>(col + 3), '%');
    }
  }
}

void renderStatus(const UiModel& m, Frame& f) {
  for (uint8_t z = 0; z < kZones; ++z) {
    const uint8_t row = z / 2;
    const uint8_t col = static_cast<uint8_t>((z % 2) * 8);
    f.put(row, col, static_cast<char>('1' + z));
    f.put(row, static_cast<uint8_t>(col + 1), ':');
    f.text(row, static_cast<uint8_t>(col + 2), stateName(m.controller->state(z)));
  }
}

void renderSystem(const UiModel& m, Frame& f) {
  f.text(0, 0, "Box ");
  f.number(0, m.boxId < 10 ? 4 : 5, m.boxId);
  const uint32_t up = m.uptimeSec;
  f.number(0, 8, up / 86400UL);
  f.put(0, 9, 'd');
  f.twoDigits(0, 11, static_cast<uint8_t>((up % 86400UL) / 3600UL));
  f.put(0, 13, ':');
  f.twoDigits(0, 14, static_cast<uint8_t>((up % 3600UL) / 60UL));

  f.text(1, 0, "Today");
  f.number(1, 13, m.waterTodayMl);
  f.text(1, 14, "mL");
}

}  // namespace

Page pageFor(uint32_t uptimeSec) {
  if (uptimeSec < kSplashSec) {
    return Page::Splash;
  }
  const uint8_t phase = static_cast<uint8_t>(uptimeSec % kRotationSec);
  if (phase < 7) {
    return Page::Moisture;
  }
  return phase < 10 ? Page::Status : Page::System;
}

char zoneGlyph(ZoneState s) {
  switch (s) {
    case ZoneState::Watering:    return kGlyphDrop;
    case ZoneState::Queued:
    case ZoneState::Soaking:     return kGlyphWait;
    case ZoneState::Hold:        return '#';
    case ZoneState::FaultSensor:
    case ZoneState::FaultNoRise: return '!';
    default:                     return ' ';
  }
}

void render(Page p, const UiModel& m, Frame& f) {
  f.clear();
  switch (p) {
    case Page::Splash:   renderSplash(m, f); break;
    case Page::Moisture: renderMoisture(m, f); break;
    case Page::Status:   renderStatus(m, f); break;
    case Page::System:   renderSystem(m, f); break;
  }
}

}  // namespace irr
