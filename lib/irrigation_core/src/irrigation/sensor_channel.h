// Signal conditioning for one soil-moisture probe:
//   burst of raw ADC samples -> median (spike rejection) -> plausibility check
//   -> fixed-point EMA low-pass -> two-point calibration -> moisture in 0.1 %
#pragma once

#include <stdint.h>

#include "types.h"

namespace irr {

/// Sorts `v` in place (insertion sort, n is tiny) and returns the median.
uint16_t medianInPlace(uint16_t* v, uint8_t n);

/// Maps a raw ADC count to moisture (0.1 % units) using the dry/wet points.
/// Works whichever way the sensor is wired (capacitive and resistive probes
/// both read *lower* when wet, but nothing here assumes that).
uint16_t rawToDpct(uint16_t raw, uint16_t rawDry, uint16_t rawWet);

/// True if `raw` lies inside the calibrated range widened by 25 % on each
/// side. A reading far outside it means an open or shorted sensor lead.
bool rawPlausible(uint16_t raw, uint16_t rawDry, uint16_t rawWet);

class SensorChannel {
 public:
  static constexpr uint8_t kMaxBurst = 9;
  /// Inter-quartile spread above this means the line is picking up noise,
  /// typical of a floating (disconnected) analog input.
  static constexpr uint16_t kMaxSpread = 40;
  /// EMA weight = 1 / 2^kEmaShift. At one burst per second this gives a
  /// time constant of ~4 s: smooth, but fast next to a 60 s soak.
  static constexpr uint8_t kEmaShift = 2;

  void reset();

  /// Feeds one burst of back-to-back samples. Sorts `samples` in place.
  void update(uint16_t* samples, uint8_t n, uint16_t rawDry, uint16_t rawWet);

  Reading reading() const { return Reading{dpct_, valid_ && primed_}; }
  bool sampled() const { return sampled_; }  ///< at least one burst seen, valid or not
  bool primed() const { return primed_; }    ///< at least one valid burst fed the filter
  uint16_t lastRaw() const { return lastRaw_; }        ///< median of the last burst
  uint16_t lastSpread() const { return spread_; }      ///< IQR of the last burst
  uint16_t filteredRaw() const { return static_cast<uint16_t>((emaQ8_ + 128) >> 8); }

 private:
  uint32_t emaQ8_ = 0;  ///< filter state, raw counts in Q24.8 fixed point
  uint16_t lastRaw_ = 0;
  uint16_t spread_ = 0;
  uint16_t dpct_ = 0;
  bool sampled_ = false;
  bool primed_ = false;
  bool valid_ = false;
};

}  // namespace irr
