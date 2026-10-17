// Automated Irrigation System firmware
//
// One ATmega328P (Arduino Uno) per box: 4 soil-moisture sensors, 4 pumps on a
// relay board, a 16x2 LCD and a status LED. The loop never blocks: every job
// is a task in a cooperative scheduler, guarded by the hardware watchdog.
//
//   sensors  20 ms   sample all probes once a second (skipped while a pump runs)
//   control  50 ms   per-zone watering state machine + pump arbiter
//   pumps    10 ms   hard on-time guard, re-assert relay outputs
//   led      25 ms   status LED pattern
//   ui      250 ms   render LCD page, push changed cells only
//   tlm     100 ms   optional CSV telemetry
//   console  every   serial command line

#include <Arduino.h>
#include <avr/pgmspace.h>

#include <irrigation_core.h>

#include "app.h"
#include "board.h"
#include "board_config.h"
#include "console.h"
#include "display.h"
#include "storage.h"

// ---- application state (see app.h) -----------------------------------------
irr::Settings g_settings;
irr::PumpBank g_pumps;
irr::Controller g_controller(g_pumps);
irr::SensorChannel g_sensors[irr::kZones];
irr::Reading g_readings[irr::kZones];
irr::UptimeClock g_uptime;
uint16_t g_loopMaxUs = 0;
bool g_settingsFromEeprom = false;

namespace {

constexpr uint32_t kSamplePeriodMs = 1000;
constexpr uint32_t kPumpBlankingMs = 300;  ///< let the supply settle after a pump stops
constexpr uint8_t kBurstSamples = 7;

#if SENSOR_POWER_GATING
constexpr uint32_t kSettleMs = SENSOR_SETTLE_MS;
#else
constexpr uint32_t kSettleMs = 0;
#endif

// ---- tasks -------------------------------------------------------------------

/// Two-phase sampler so optional sensor power-up never blocks the loop:
/// Idle --period elapsed--> Settling (probes powered) --settle time--> sample.
void taskSensors(uint32_t now) {
  static bool settling = false;
  static uint32_t phaseAt = 0;

  if (!settling) {
    if (!irr::elapsed(now, phaseAt, kSamplePeriodMs)) {
      return;
    }
    // Pump motors sag the 5 V rail and inject noise into the ADC reference;
    // the zone being watered doesn't need fresh data until it soaks anyway.
    if (g_pumps.anyOn() || !irr::elapsed(now, g_pumps.lastOffAt(), kPumpBlankingMs)) {
      return;
    }
    board::sensorPower(true);
    settling = true;
    phaseAt = now;
  }
  if (!irr::elapsed(now, phaseAt, kSettleMs)) {
    return;
  }

  uint16_t burst[kBurstSamples];
  for (uint8_t z = 0; z < irr::kZones; ++z) {
    board::readBurst(z, burst, kBurstSamples);
    const irr::ZoneSettings& zs = g_settings.zone[z];
    g_sensors[z].update(burst, kBurstSamples, zs.rawDry, zs.rawWet);
    g_readings[z] = g_sensors[z].reading();
  }
  board::sensorPower(false);
  settling = false;
  phaseAt = now;
}

void taskControl(uint32_t now) { g_controller.tick(now, g_settings, g_readings); }

void taskPumps(uint32_t now) {
  g_pumps.enforce(now);
  // Written every pass, not just on change, so a transient upset (ESD, a
  // brown-out glitch on the port register) can never leave a relay latched.
  board::writePumps(g_pumps.mask());
}

void taskLed(uint32_t now) {
  board::writeLed(irr::ledLevel(irr::selectLedMode(g_controller, g_pumps), now));
}

void taskUi(uint32_t /*now*/) {
  static irr::Frame frame;
  irr::UiModel model;
  model.readings = g_readings;
  model.controller = &g_controller;
  model.boxId = g_settings.boxId;
  model.uptimeSec = g_uptime.seconds();
  model.waterTodayMl = irr::waterMl(g_controller.waterTodayMsTotal(), g_settings.flowMlPerMin);
  model.version = FW_VERSION;
  irr::render(irr::pageFor(model.uptimeSec), model, frame);
  display::show(frame);
}

void taskTelemetry(uint32_t now) {
  static uint32_t last = 0;
  if (g_settings.telemetryMs != 0 && irr::elapsed(now, last, g_settings.telemetryMs)) {
    last = now;
    console::printTelemetry();
  }
}

void taskConsole(uint32_t now) { console::poll(now); }

const char kNameSensors[] PROGMEM = "sensors";
const char kNameControl[] PROGMEM = "control";
const char kNamePumps[] PROGMEM = "pumps";
const char kNameLed[] PROGMEM = "led";
const char kNameUi[] PROGMEM = "ui";
const char kNameTlm[] PROGMEM = "tlm";
const char kNameConsole[] PROGMEM = "console";

// Order matters: control runs right after fresh samples, and the relay
// outputs are written in the same pass as the control decision.
irr::Task g_tasks[] = {
    {kNameSensors, taskSensors, 20, 0, 0, 0, 0},
    {kNameControl, taskControl, 50, 0, 0, 0, 0},
    {kNamePumps, taskPumps, 10, 0, 0, 0, 0},
    {kNameLed, taskLed, 25, 0, 0, 0, 0},
    {kNameUi, taskUi, 250, 0, 0, 0, 0},
    {kNameTlm, taskTelemetry, 100, 0, 0, 0, 0},
    {kNameConsole, taskConsole, 0, 0, 0, 0, 0},
};

uint32_t microsNow() { return micros(); }

}  // namespace

irr::Scheduler g_scheduler(g_tasks, sizeof g_tasks / sizeof g_tasks[0]);

void setup() {
  board::earlyInit();  // relays OFF before anything else
  board::writeLed(true);  // lamp test while booting

  Serial.begin(SERIAL_BAUD);

  g_settingsFromEeprom = storage::load(g_settings);
  if (!g_settingsFromEeprom) {
    irr::loadDefaults(g_settings);
    storage::save(g_settings);
  }

  display::begin();
  for (uint8_t z = 0; z < irr::kZones; ++z) {
    g_sensors[z].reset();
    g_readings[z] = g_sensors[z].reading();
  }

  const uint32_t now = millis();
  g_uptime.begin(now);
  g_pumps.reset();
  g_controller.begin(now);
  g_controller.setEventSink(console::printEvent);
  g_scheduler.begin(now, microsNow, board::taskBreadcrumb());

  console::begin();
  board::watchdogStart();
}

void loop() {
  const uint32_t t0 = micros();
  const uint32_t now = millis();
  g_uptime.update(now);
  g_scheduler.runDue(now);
  board::watchdogKick();

  const uint32_t dt = micros() - t0;
  if (dt > g_loopMaxUs) {
    g_loopMaxUs = static_cast<uint16_t>(dt > 0xFFFFUL ? 0xFFFFUL : dt);
  }
}
