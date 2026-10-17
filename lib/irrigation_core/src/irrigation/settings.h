// Runtime-tunable settings, their limits, and the CRC-protected EEPROM image.
#pragma once

#include <stddef.h>
#include <stdint.h>

#include "types.h"

namespace irr {

/// Per-plant settings. Field order and explicit padding keep the layout
/// identical on AVR (1-byte alignment) and on x86 (natural alignment), so the
/// host tests exercise exactly the bytes that land in EEPROM.
struct ZoneSettings {
  uint8_t enabled;    ///< 0 = zone ignored (e.g. box has fewer than 4 plants)
  uint8_t dryPct;     ///< start a watering cycle below this moisture
  uint8_t targetPct;  ///< stop the cycle once this is reached (hysteresis band)
  uint8_t reserved;
  uint16_t pulseMs;   ///< pump on-time per pulse
  uint16_t rawDry;    ///< ADC reading for bone-dry soil (two-point calibration)
  uint16_t rawWet;    ///< ADC reading for saturated soil / water
};

struct Settings {
  uint8_t boxId;           ///< identifies the box in telemetry (two boxes share firmware)
  uint8_t maxPulses;       ///< pulses per cycle before giving up
  uint8_t minRisePct;      ///< minimum moisture rise per cycle, else FaultNoRise
  uint8_t reserved;
  uint16_t soakSec;        ///< wait after each pulse for water to reach the sensor
  uint16_t cooldownMin;    ///< minimum gap between cycles on one zone
  uint16_t budgetSec;      ///< max pump seconds per zone per 24 h window
  uint16_t flowMlPerMin;   ///< pump flow rate, used for volume estimates only
  uint16_t telemetryMs;    ///< CSV telemetry period on the serial port, 0 = off
  ZoneSettings zone[kZones];
};

namespace limits {
constexpr uint8_t kDryPctMin = 5;
constexpr uint8_t kDryPctMax = 95;
constexpr uint8_t kTargetPctMax = 100;
constexpr uint16_t kPulseMinMs = 200;
constexpr uint16_t kPulseMaxMs = 10000;  // must stay below kPumpHardMaxMs
constexpr uint16_t kAdcMax = 1023;
constexpr uint16_t kMinCalSpan = 50;     // dry/wet points closer than this are unusable
constexpr uint8_t kMaxPulsesMax = 10;
constexpr uint8_t kMinRisePctMax = 50;
constexpr uint16_t kSoakSecMin = 5;
constexpr uint16_t kSoakSecMax = 3600;
constexpr uint16_t kCooldownMinMax = 1440;
constexpr uint16_t kBudgetSecMin = 10;
constexpr uint16_t kBudgetSecMax = 3600;
constexpr uint16_t kFlowMax = 10000;
constexpr uint16_t kTelemetryMinMs = 100;
constexpr uint16_t kTelemetryMaxMs = 60000;
constexpr uint8_t kBoxIdMax = 99;
}  // namespace limits

namespace defaults {
// Capacitive soil sensor v1.2 on 5 V reads roughly 600 in dry soil and 280 in
// water. A resistive FC-28/YL-69 probe reads ~1023 dry and ~350 wet; run
// `cal <z> dry` / `cal <z> wet` on the console to measure your own.
constexpr uint16_t kRawDry = 600;
constexpr uint16_t kRawWet = 280;
constexpr uint8_t kDryPct = 70;
constexpr uint8_t kTargetPct = 80;
constexpr uint16_t kPulseMs = 3000;
}  // namespace defaults

void loadDefaults(Settings& s);

/// Clamps every field into its legal range. Returns true if anything changed.
/// Called after loading from EEPROM and after every console edit, so the
/// controller never sees an out-of-range value.
bool sanitize(Settings& s);

// ---- persistence -----------------------------------------------------------

constexpr uint16_t kConfigMagic = 0x1A5C;
constexpr uint8_t kConfigVersion = 1;

struct ConfigImage {
  uint16_t magic;
  uint8_t version;
  uint8_t length;  ///< sizeof(Settings), catches layout changes without a version bump
  Settings settings;
  uint16_t crc;    ///< CRC-16 over every byte before this field
};

static_assert(sizeof(Settings) < 256, "Settings length must fit ConfigImage::length");

void sealImage(ConfigImage& img, const Settings& s);

/// Validates magic, version, length and CRC. On success copies (and
/// sanitizes) the settings into `out`; on failure leaves `out` untouched.
bool openImage(const ConfigImage& img, Settings& out);

}  // namespace irr
