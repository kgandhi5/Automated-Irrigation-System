#include "board.h"

#include <Arduino.h>
#include <avr/wdt.h>

#include <irrigation_core.h>

#include "board_config.h"

// ---- Reset-cause capture ---------------------------------------------------
// Runs from .init3, before main() and before the C runtime clears .bss. It
// saves MCUSR and disables the watchdog, which otherwise stays armed after a
// watchdog reset and would reboot the board again mid-setup().
uint8_t g_mcusrMirror __attribute__((section(".noinit")));

extern "C" void captureResetCause() __attribute__((naked, used, section(".init3")));
extern "C" void captureResetCause() {
  g_mcusrMirror = MCUSR;
  MCUSR = 0;
  wdt_disable();
}

namespace {

const uint8_t kPumpPins[irr::kZones] = {PIN_PUMP_1, PIN_PUMP_2, PIN_PUMP_3, PIN_PUMP_4};
const uint8_t kSensorPins[irr::kZones] = {PIN_MOISTURE_1, PIN_MOISTURE_2, PIN_MOISTURE_3,
                                          PIN_MOISTURE_4};
constexpr uint8_t kRelayOn = RELAY_ACTIVE_LOW ? LOW : HIGH;
constexpr uint8_t kRelayOff = RELAY_ACTIVE_LOW ? HIGH : LOW;

// Survives watchdog and button resets (but not power loss), so after a
// watchdog reset we can report which task was running when the system hung.
struct NoInitState {
  uint16_t magic;
  volatile uint8_t lastTask;
  uint16_t warmResets;
};
constexpr uint16_t kNoInitMagic = 0xB007;
NoInitState g_noinit __attribute__((section(".noinit")));

board::ResetInfo g_reset;

}  // namespace

// Heap bookkeeping symbols from avr-libc, used to measure free RAM.
extern "C" char __heap_start;
extern "C" char* __brkval;

namespace board {

void earlyInit() {
  // Write the OFF level *before* switching to OUTPUT: the pin goes straight
  // from high-impedance to OFF with no glitch that could click a relay.
  for (uint8_t z = 0; z < irr::kZones; ++z) {
    digitalWrite(kPumpPins[z], kRelayOff);
    pinMode(kPumpPins[z], OUTPUT);
  }
  pinMode(PIN_STATUS_LED, OUTPUT);
  digitalWrite(PIN_STATUS_LED, LOW);
#if SENSOR_POWER_GATING
  digitalWrite(PIN_SENSOR_POWER, LOW);
  pinMode(PIN_SENSOR_POWER, OUTPUT);
#endif

  g_reset.mcusr = g_mcusrMirror;
  const bool powerUp = (g_mcusrMirror & (_BV(PORF) | _BV(BORF))) != 0;
  if (!powerUp && g_noinit.magic == kNoInitMagic) {
    g_reset.warm = true;
    g_reset.lastTask = g_noinit.lastTask;
    ++g_noinit.warmResets;
  } else {
    g_reset.warm = false;
    g_reset.lastTask = irr::kNoTask;
    g_noinit.magic = kNoInitMagic;
    g_noinit.warmResets = 0;
  }
  g_reset.warmResets = g_noinit.warmResets;
  g_noinit.lastTask = irr::kNoTask;
}

void writePumps(uint8_t mask) {
  for (uint8_t z = 0; z < irr::kZones; ++z) {
    digitalWrite(kPumpPins[z], (mask >> z) & 1U ? kRelayOn : kRelayOff);
  }
}

void writeLed(bool on) { digitalWrite(PIN_STATUS_LED, on ? HIGH : LOW); }

void sensorPower(bool on) {
#if SENSOR_POWER_GATING
  digitalWrite(PIN_SENSOR_POWER, on ? HIGH : LOW);
#else
  (void)on;
#endif
}

void readBurst(uint8_t zone, uint16_t* out, uint8_t n) {
  const uint8_t pin = kSensorPins[zone];
  // The ADC's sample-and-hold capacitor still carries the previous channel's
  // voltage after a mux switch; with high-impedance sensors the first
  // conversion is skewed toward it ("ghosting"), so throw it away.
  (void)analogRead(pin);
  for (uint8_t i = 0; i < n; ++i) {
    out[i] = static_cast<uint16_t>(analogRead(pin));
  }
}

const ResetInfo& resetInfo() { return g_reset; }

volatile uint8_t* taskBreadcrumb() { return &g_noinit.lastTask; }

int freeRam() {
  char top;
  return static_cast<int>(&top - (__brkval != nullptr ? __brkval : &__heap_start));
}

void watchdogStart() { wdt_enable(WDTO_2S); }

void watchdogKick() { wdt_reset(); }

void reboot() {
  writePumps(0);
  wdt_enable(WDTO_15MS);
  for (;;) {
  }
}

}  // namespace board
