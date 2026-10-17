#include "settings.h"

#include <string.h>

#include "crc16.h"

namespace irr {

namespace {

template <typename T>
bool clampField(T& v, T lo, T hi) {
  if (v < lo) {
    v = lo;
    return true;
  }
  if (v > hi) {
    v = hi;
    return true;
  }
  return false;
}

uint16_t span(uint16_t a, uint16_t b) { return a > b ? a - b : b - a; }

}  // namespace

void loadDefaults(Settings& s) {
  memset(&s, 0, sizeof s);
  s.boxId = 1;
  s.maxPulses = 4;
  s.minRisePct = 3;
  s.soakSec = 60;
  s.cooldownMin = 30;
  s.budgetSec = 60;
  s.flowMlPerMin = 1500;
  s.telemetryMs = 0;
  for (uint8_t z = 0; z < kZones; ++z) {
    ZoneSettings& zs = s.zone[z];
    zs.enabled = 1;
    zs.dryPct = defaults::kDryPct;
    zs.targetPct = defaults::kTargetPct;
    zs.pulseMs = defaults::kPulseMs;
    zs.rawDry = defaults::kRawDry;
    zs.rawWet = defaults::kRawWet;
  }
}

bool sanitize(Settings& s) {
  bool changed = false;
  changed |= clampField<uint8_t>(s.boxId, 0, limits::kBoxIdMax);
  changed |= clampField<uint8_t>(s.maxPulses, 1, limits::kMaxPulsesMax);
  changed |= clampField<uint8_t>(s.minRisePct, 0, limits::kMinRisePctMax);
  changed |= clampField<uint16_t>(s.soakSec, limits::kSoakSecMin, limits::kSoakSecMax);
  changed |= clampField<uint16_t>(s.cooldownMin, 0, limits::kCooldownMinMax);
  changed |= clampField<uint16_t>(s.budgetSec, limits::kBudgetSecMin, limits::kBudgetSecMax);
  changed |= clampField<uint16_t>(s.flowMlPerMin, 1, limits::kFlowMax);
  if (s.telemetryMs != 0) {
    changed |= clampField<uint16_t>(s.telemetryMs, limits::kTelemetryMinMs, limits::kTelemetryMaxMs);
  }

  for (uint8_t z = 0; z < kZones; ++z) {
    ZoneSettings& zs = s.zone[z];
    if (zs.enabled > 1) {
      zs.enabled = 1;
      changed = true;
    }
    changed |= clampField<uint8_t>(zs.dryPct, limits::kDryPctMin, limits::kDryPctMax);
    // Target must sit above the start threshold or the zone would oscillate.
    changed |= clampField<uint8_t>(zs.targetPct, static_cast<uint8_t>(zs.dryPct + 1),
                                   limits::kTargetPctMax);
    changed |= clampField<uint16_t>(zs.pulseMs, limits::kPulseMinMs, limits::kPulseMaxMs);
    changed |= clampField<uint16_t>(zs.rawDry, 0, limits::kAdcMax);
    changed |= clampField<uint16_t>(zs.rawWet, 0, limits::kAdcMax);
    if (span(zs.rawDry, zs.rawWet) < limits::kMinCalSpan) {
      zs.rawDry = defaults::kRawDry;
      zs.rawWet = defaults::kRawWet;
      changed = true;
    }
  }
  return changed;
}

void sealImage(ConfigImage& img, const Settings& s) {
  memset(&img, 0, sizeof img);
  img.magic = kConfigMagic;
  img.version = kConfigVersion;
  img.length = static_cast<uint8_t>(sizeof(Settings));
  img.settings = s;
  img.crc = crc16(reinterpret_cast<const uint8_t*>(&img), offsetof(ConfigImage, crc));
}

bool openImage(const ConfigImage& img, Settings& out) {
  if (img.magic != kConfigMagic || img.version != kConfigVersion ||
      img.length != sizeof(Settings)) {
    return false;
  }
  const uint16_t crc = crc16(reinterpret_cast<const uint8_t*>(&img), offsetof(ConfigImage, crc));
  if (crc != img.crc) {
    return false;
  }
  out = img.settings;
  sanitize(out);
  return true;
}

}  // namespace irr
