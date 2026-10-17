#include "sensor_channel.h"

namespace irr {

uint16_t medianInPlace(uint16_t* v, uint8_t n) {
  if (n == 0) {
    return 0;
  }
  for (uint8_t i = 1; i < n; ++i) {
    const uint16_t key = v[i];
    uint8_t j = i;
    while (j > 0 && v[j - 1] > key) {
      v[j] = v[j - 1];
      --j;
    }
    v[j] = key;
  }
  return v[n / 2];
}

uint16_t rawToDpct(uint16_t raw, uint16_t rawDry, uint16_t rawWet) {
  const int32_t num = static_cast<int32_t>(raw) - rawDry;
  const int32_t den = static_cast<int32_t>(rawWet) - rawDry;
  if (den == 0) {
    return 0;
  }
  // Round to nearest; num and den share a sign whenever the result is >= 0.
  const int32_t d = (num * kMoistureFull + den / 2) / den;
  if (d < 0) {
    return 0;
  }
  if (d > kMoistureFull) {
    return kMoistureFull;
  }
  return static_cast<uint16_t>(d);
}

bool rawPlausible(uint16_t raw, uint16_t rawDry, uint16_t rawWet) {
  const uint16_t lo = rawDry < rawWet ? rawDry : rawWet;
  const uint16_t hi = rawDry < rawWet ? rawWet : rawDry;
  const uint16_t margin = (hi - lo) / 4;
  return static_cast<uint32_t>(raw) + margin >= lo && raw <= static_cast<uint32_t>(hi) + margin;
}

void SensorChannel::reset() {
  emaQ8_ = 0;
  lastRaw_ = 0;
  spread_ = 0;
  dpct_ = 0;
  sampled_ = false;
  primed_ = false;
  valid_ = false;
}

void SensorChannel::update(uint16_t* samples, uint8_t n, uint16_t rawDry, uint16_t rawWet) {
  if (n == 0) {
    return;
  }
  if (n > kMaxBurst) {
    n = kMaxBurst;
  }
  lastRaw_ = medianInPlace(samples, n);
  sampled_ = true;
  // Inter-quartile spread: one spike from a relay or pump motor lands in the
  // excluded tails, but a floating input that wanders everywhere does not.
  const uint8_t q = n / 4;
  spread_ = samples[n - 1 - q] - samples[q];

  valid_ = rawPlausible(lastRaw_, rawDry, rawWet) && spread_ <= kMaxSpread;
  if (!valid_) {
    return;  // keep the filter state; bad samples never pollute it
  }

  const uint32_t x = static_cast<uint32_t>(lastRaw_) << 8;
  if (!primed_) {
    emaQ8_ = x;  // start at the first good sample instead of ramping up from 0
    primed_ = true;
  } else {
    // ema += (x - ema) / 2^k, rearranged so every term stays non-negative.
    emaQ8_ = emaQ8_ - (emaQ8_ >> kEmaShift) + (x >> kEmaShift);
  }
  dpct_ = rawToDpct(filteredRaw(), rawDry, rawWet);
}

}  // namespace irr
