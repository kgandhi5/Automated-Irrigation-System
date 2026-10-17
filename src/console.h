// Serial console (115200 baud, newline-terminated): configuration commands,
// human-readable event log and machine-readable telemetry.
#pragma once

#include <stdint.h>

#include <irrigation_core.h>

namespace console {

void begin();
/// Non-blocking: consumes whatever bytes have arrived and runs complete lines.
void poll(uint32_t now);
/// Matches irr::EventSink so the controller can log straight to the console.
void printEvent(const irr::Event& e, void* ctx);
/// `$TLM,box,uptime_s,m1..m4,raw1..raw4,pump_mask,states` (moisture in 0.1 %).
void printTelemetry();

}  // namespace console
