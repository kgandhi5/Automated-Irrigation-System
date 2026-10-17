// Status LED patterns. Each pattern is a 32-slot bitmask played over a 4 s
// window (125 ms per slot), so the LED level is a pure function of time and
// needs no state or timers of its own.
#pragma once

#include <stdint.h>

#include "controller.h"
#include "pump_bank.h"

namespace irr {

enum class LedMode : uint8_t {
  Heartbeat,  ///< everything nominal: short blip every 4 s proves the firmware is alive
  Busy,       ///< a watering cycle is in progress (queued / soaking / budget hold)
  Fault,      ///< at least one zone faulted: double blink every 2 s
  Pumping,    ///< a pump is running: solid on
};

constexpr uint8_t kLedSlotMs = 125;

uint32_t ledMask(LedMode m);
bool ledLevel(LedMode m, uint32_t now);
LedMode selectLedMode(const Controller& c, const PumpBank& p);

}  // namespace irr
