// Board support: the only place that touches pins, the ADC, the watchdog and
// reset-cause registers.
#pragma once

#include <stdint.h>

namespace board {

struct ResetInfo {
  uint8_t mcusr;        ///< MCU status register at reset (0 if the bootloader cleared it)
  bool warm;            ///< RAM survived the reset (watchdog / reset button), not a power-up
  uint8_t lastTask;     ///< scheduler task running when the reset hit, or irr::kNoTask
  uint16_t warmResets;  ///< warm resets since the last power-up
};

/// First call in setup(): drives every relay to OFF before anything else can
/// fail, sets up the LED and sensor power pins, and captures the reset cause.
void earlyInit();

void writePumps(uint8_t mask);
void writeLed(bool on);
void sensorPower(bool on);

/// Discards one conversion after switching the ADC mux, then fills `out`
/// with `n` back-to-back samples of the zone's sensor.
void readBurst(uint8_t zone, uint16_t* out, uint8_t n);

const ResetInfo& resetInfo();
volatile uint8_t* taskBreadcrumb();
int freeRam();

void watchdogStart();
void watchdogKick();
[[noreturn]] void reboot();

}  // namespace board
