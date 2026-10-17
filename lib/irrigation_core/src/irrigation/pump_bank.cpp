#include "pump_bank.h"

namespace irr {

void PumpBank::reset() {
  mask_ = 0;
  guardTrips_ = 0;
  lastOffAt_ = 0;
  for (uint8_t z = 0; z < kZones; ++z) {
    onAt_[z] = 0;
    lastRunMs_[z] = 0;
  }
}

uint8_t PumpBank::countOn() const {
  uint8_t n = 0;
  for (uint8_t m = mask_; m; m &= static_cast<uint8_t>(m - 1)) {
    ++n;
  }
  return n;
}

bool PumpBank::start(uint8_t zone, uint32_t now) {
  if (zone >= kZones || isOn(zone) || countOn() >= kMaxPumpsOn) {
    return false;
  }
  mask_ |= static_cast<uint8_t>(1U << zone);
  onAt_[zone] = now;
  return true;
}

uint32_t PumpBank::stop(uint8_t zone, uint32_t now) {
  if (zone >= kZones || !isOn(zone)) {
    return 0;
  }
  mask_ &= static_cast<uint8_t>(~(1U << zone));
  lastRunMs_[zone] = now - onAt_[zone];
  lastOffAt_ = now;
  return lastRunMs_[zone];
}

void PumpBank::stopAll(uint32_t now) {
  for (uint8_t z = 0; z < kZones; ++z) {
    stop(z, now);
  }
}

uint8_t PumpBank::enforce(uint32_t now) {
  uint8_t tripped = 0;
  for (uint8_t z = 0; z < kZones; ++z) {
    if (isOn(z) && elapsed(now, onAt_[z], kPumpHardMaxMs)) {
      stop(z, now);
      tripped |= static_cast<uint8_t>(1U << z);
      ++guardTrips_;
    }
  }
  return tripped;
}

}  // namespace irr
