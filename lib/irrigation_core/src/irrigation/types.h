// Core types shared by every module of the irrigation controller.
//
// Everything under lib/irrigation_core is plain C++11 with no Arduino
// dependency, so the same control logic runs on the ATmega328P and in the
// host-side unit tests / closed-loop simulator.
#pragma once

#include <stdint.h>

namespace irr {

/// Plant zones (sensor + pump pairs) per box.
constexpr uint8_t kZones = 4;

/// Moisture is carried through the system in tenths of a percent (0..1000) so
/// control decisions keep sub-percent resolution without floating point.
constexpr uint16_t kMoistureFull = 1000;

enum class ZoneState : uint8_t {
  Disabled,     ///< zone switched off in settings
  Idle,         ///< monitoring, soil is wet enough
  Queued,       ///< needs water, waiting for the pump arbiter
  Watering,     ///< pump running
  Soaking,      ///< pump off, waiting for water to reach the sensor
  Cooldown,     ///< cycle finished, minimum interval before the next one
  Hold,         ///< daily water budget used up
  FaultSensor,  ///< sensor reading implausible (open / shorted / noisy wiring)
  FaultNoRise,  ///< watered but moisture did not respond (empty tank, pump or tube failure)
};

struct Reading {
  uint16_t dpct;  ///< moisture in 0.1 % units
  bool valid;     ///< false if the latest sample failed plausibility checks
};

/// Wrap-safe "has `duration` ms passed since `since`". Correct across the
/// 49.7-day millis() rollover because unsigned subtraction is modulo 2^32.
inline bool elapsed(uint32_t now, uint32_t since, uint32_t duration) {
  return static_cast<uint32_t>(now - since) >= duration;
}

const char* stateName(ZoneState s);  ///< 4-char code for the LCD / console
char stateLetter(ZoneState s);       ///< 1-char code for telemetry
bool isFault(ZoneState s);

/// Converts pump run time to an estimated water volume.
/// Ordered to avoid 32-bit overflow for any value the settings allow.
inline uint32_t waterMl(uint32_t pumpMs, uint16_t flowMlPerMin) {
  return (pumpMs / 100UL) * flowMlPerMin / 600UL;
}

/// Seconds since boot that keep counting past the millis() rollover.
class UptimeClock {
 public:
  void begin(uint32_t now) {
    last_ = now;
    accMs_ = 0;
    secs_ = 0;
  }
  void update(uint32_t now) {
    accMs_ += static_cast<uint32_t>(now - last_);
    last_ = now;
    while (accMs_ >= 1000UL) {
      accMs_ -= 1000UL;
      ++secs_;
    }
  }
  uint32_t seconds() const { return secs_; }

 private:
  uint32_t last_ = 0;
  uint32_t accMs_ = 0;
  uint32_t secs_ = 0;
};

}  // namespace irr
