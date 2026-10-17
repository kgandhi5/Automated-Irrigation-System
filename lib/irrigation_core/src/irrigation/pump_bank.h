// Commanded state of the pump relays plus the safety interlocks that sit
// underneath the control logic:
//   * at most kMaxPumpsOn pumps at once (the boxes share one 5 V supply)
//   * a hard on-time limit that trips even if the controller misbehaves
#pragma once

#include <stdint.h>

#include "types.h"

namespace irr {

constexpr uint8_t kMaxPumpsOn = 1;
constexpr uint32_t kPumpHardMaxMs = 15000;

class PumpBank {
 public:
  void reset();

  /// Returns false if the zone is already on or the concurrency limit is hit.
  bool start(uint8_t zone, uint32_t now);
  /// Returns how long the pump ran, 0 if it was already off.
  uint32_t stop(uint8_t zone, uint32_t now);
  void stopAll(uint32_t now);

  /// Independent runtime guard. Call frequently; returns a bitmask of the
  /// pumps it had to switch off.
  uint8_t enforce(uint32_t now);

  bool isOn(uint8_t zone) const { return (mask_ >> zone) & 1U; }
  bool anyOn() const { return mask_ != 0; }
  uint8_t mask() const { return mask_; }
  uint8_t countOn() const;
  uint32_t lastRunMs(uint8_t zone) const { return lastRunMs_[zone]; }
  uint32_t lastOffAt() const { return lastOffAt_; }
  uint16_t guardTrips() const { return guardTrips_; }

 private:
  uint8_t mask_ = 0;
  uint16_t guardTrips_ = 0;
  uint32_t onAt_[kZones] = {};
  uint32_t lastRunMs_[kZones] = {};
  uint32_t lastOffAt_ = 0;
};

}  // namespace irr
