#include "led.h"

namespace irr {

uint32_t ledMask(LedMode m) {
  switch (m) {
    case LedMode::Heartbeat: return 0x00000001UL;  // 125 ms on, every 4 s
    case LedMode::Busy:      return 0x03030303UL;  // 250 ms on, every 1 s
    case LedMode::Fault:     return 0x00050005UL;  // blink-blink, every 2 s
    case LedMode::Pumping:   return 0xFFFFFFFFUL;  // solid
  }
  return 0;
}

bool ledLevel(LedMode m, uint32_t now) {
  const uint8_t slot = static_cast<uint8_t>((now / kLedSlotMs) & 31U);
  return (ledMask(m) >> slot) & 1UL;
}

LedMode selectLedMode(const Controller& c, const PumpBank& p) {
  if (p.anyOn()) {
    return LedMode::Pumping;
  }
  if (c.anyFault()) {
    return LedMode::Fault;
  }
  if (c.anyBusy()) {
    return LedMode::Busy;
  }
  return LedMode::Heartbeat;
}

}  // namespace irr
