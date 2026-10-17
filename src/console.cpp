#include "console.h"

#include <Arduino.h>
#include <avr/pgmspace.h>
#include <string.h>

#include "app.h"
#include "board.h"
#include "board_config.h"
#include "storage.h"

using irr::kAllZones;
using irr::kZones;
using irr::streq;
namespace limits = irr::limits;

namespace {

typedef const __FlashStringHelper* FStr;

constexpr uint8_t kLineMax = 48;
constexpr uint8_t kMaxArgs = 5;

char g_line[kLineMax];
uint8_t g_len = 0;
bool g_overflow = false;

// ---- formatting helpers ----------------------------------------------------

void printPadded(uint32_t v, uint8_t width) {
  uint8_t digits = 1;
  for (uint32_t t = v; t >= 10; t /= 10) {
    ++digits;
  }
  while (digits++ < width) {
    Serial.print(' ');
  }
  Serial.print(v);
}

void printDpct(uint16_t dpct) {
  Serial.print(dpct / 10);
  Serial.print('.');
  Serial.print(dpct % 10);
}

void printTimestamp(uint32_t ms) {
  Serial.print('[');
  printPadded(ms / 1000UL, 7);
  Serial.print('.');
  const uint16_t frac = ms % 1000UL;
  if (frac < 100) Serial.print('0');
  if (frac < 10) Serial.print('0');
  Serial.print(frac);
  Serial.print(F("] "));
}

void printZone(uint8_t z) {
  Serial.print('P');
  Serial.print(z + 1);
}

void printHex8(uint8_t v) {
  Serial.print(F("0x"));
  if (v < 0x10) Serial.print('0');
  Serial.print(v, HEX);
}

void error(FStr msg) {
  Serial.print(F("error: "));
  Serial.println(msg);
}

void okUnsaved() { Serial.println(F("ok (run 'save' to persist)")); }

template <typename T>
bool assign(T& field, const char* s, uint32_t lo, uint32_t hi) {
  uint32_t v = 0;
  if (!irr::parseUint(s, lo, hi, &v)) {
    return false;
  }
  field = static_cast<T>(v);
  return true;
}

/// Expands a parsed zone (0..3 or kAllZones) into a [first, last] range.
void zoneRange(uint8_t zone, uint8_t& first, uint8_t& last) {
  first = zone == kAllZones ? 0 : zone;
  last = zone == kAllZones ? kZones - 1 : zone;
}

// ---- commands --------------------------------------------------------------

void cmdHelp() {
  Serial.println(F(
      "Commands (zone = 1-4, * = all zones):\n"
      "  status                       live zone table\n"
      "  config                       settings as a replayable command script\n"
      "  set <z> dry <pct>            start watering below this moisture\n"
      "  set <z> target <pct>         end the cycle at this moisture\n"
      "  set <z> pulse <ms>           pump on-time per pulse\n"
      "  set <z> enable <0|1>         enable / disable a zone\n"
      "  set soak <s>  cooldown <min>  pulses <n>  budget <s>\n"
      "  set rise <pct>  flow <ml/min>  id <n>  tlm <ms|0>\n"
      "  cal <z> dry|wet              use the current raw reading as a calibration point\n"
      "  cal <z> <raw_dry> <raw_wet>  set calibration explicitly\n"
      "  water <z> <ms>               manual pulse\n"
      "  stop                         stop all pumps and cancel cycles\n"
      "  clear [z]                    clear latched faults\n"
      "  save | load | defaults       EEPROM persistence\n"
      "  stats                        uptime, reset cause, RAM, task timing\n"
      "  reboot                       restart through the watchdog"));
}

void cmdStatus() {
  Serial.println(F("zone state  moist%   raw  iqr  pulses  today_s  cycles  faults"));
  for (uint8_t z = 0; z < kZones; ++z) {
    const irr::Reading r = g_readings[z];
    const irr::ZoneStats& st = g_controller.stats(z);
    Serial.print(' ');
    printZone(z);
    Serial.print(F("   "));
    const char* name = irr::stateName(g_controller.state(z));
    Serial.print(name);
    for (uint8_t i = static_cast<uint8_t>(strlen(name)); i < 6; ++i) {
      Serial.print(' ');
    }
    if (g_sensors[z].primed()) {
      printPadded(r.dpct / 10, 4);
      Serial.print('.');
      Serial.print(r.dpct % 10);
    } else {
      Serial.print(F("    --"));
    }
    Serial.print(r.valid ? ' ' : '?');
    printPadded(g_sensors[z].lastRaw(), 5);
    printPadded(g_sensors[z].lastSpread(), 5);
    printPadded(g_controller.pulsesThisCycle(z), 6);
    Serial.print('/');
    Serial.print(g_settings.maxPulses);
    printPadded(g_controller.waterTodayMs(z) / 1000UL, 8);
    printPadded(st.cycles, 8);
    printPadded(st.faults, 8);
    Serial.println();
  }
  Serial.print(F("water today: "));
  Serial.print(irr::waterMl(g_controller.waterTodayMsTotal(), g_settings.flowMlPerMin));
  Serial.print(F(" mL (est.)  pumps: "));
  printHex8(g_pumps.mask());
  Serial.println();
}

void printGlobal(FStr key, uint32_t v) {
  Serial.print(F("set "));
  Serial.print(key);
  Serial.print(' ');
  Serial.println(v);
}

void printZoneSetting(uint8_t z, FStr key, uint32_t v) {
  Serial.print(F("set "));
  Serial.print(z + 1);
  Serial.print(' ');
  Serial.print(key);
  Serial.print(' ');
  Serial.println(v);
}

void cmdConfig() {
  const irr::Settings& s = g_settings;
  printGlobal(F("id"), s.boxId);
  printGlobal(F("soak"), s.soakSec);
  printGlobal(F("cooldown"), s.cooldownMin);
  printGlobal(F("pulses"), s.maxPulses);
  printGlobal(F("budget"), s.budgetSec);
  printGlobal(F("rise"), s.minRisePct);
  printGlobal(F("flow"), s.flowMlPerMin);
  printGlobal(F("tlm"), s.telemetryMs);
  for (uint8_t z = 0; z < kZones; ++z) {
    const irr::ZoneSettings& zs = s.zone[z];
    printZoneSetting(z, F("enable"), zs.enabled);
    printZoneSetting(z, F("dry"), zs.dryPct);
    printZoneSetting(z, F("target"), zs.targetPct);
    printZoneSetting(z, F("pulse"), zs.pulseMs);
    Serial.print(F("cal "));
    Serial.print(z + 1);
    Serial.print(' ');
    Serial.print(zs.rawDry);
    Serial.print(' ');
    Serial.println(zs.rawWet);
  }
}

bool setZoneField(uint8_t zone, const char* key, const char* val) {
  uint8_t first, last;
  zoneRange(zone, first, last);
  for (uint8_t z = first; z <= last; ++z) {
    irr::ZoneSettings& zs = g_settings.zone[z];
    bool ok;
    if (streq(key, "dry")) {
      ok = assign(zs.dryPct, val, limits::kDryPctMin, limits::kDryPctMax);
    } else if (streq(key, "target")) {
      ok = assign(zs.targetPct, val, limits::kDryPctMin + 1, limits::kTargetPctMax);
    } else if (streq(key, "pulse")) {
      ok = assign(zs.pulseMs, val, limits::kPulseMinMs, limits::kPulseMaxMs);
    } else if (streq(key, "enable")) {
      ok = assign(zs.enabled, val, 0, 1);
    } else {
      error(F("unknown zone setting (dry, target, pulse, enable)"));
      return false;
    }
    if (!ok) {
      error(F("value out of range"));
      return false;
    }
  }
  return true;
}

bool setGlobalField(const char* key, const char* val) {
  irr::Settings& s = g_settings;
  bool ok;
  if (streq(key, "soak")) {
    ok = assign(s.soakSec, val, limits::kSoakSecMin, limits::kSoakSecMax);
  } else if (streq(key, "cooldown")) {
    ok = assign(s.cooldownMin, val, 0, limits::kCooldownMinMax);
  } else if (streq(key, "pulses")) {
    ok = assign(s.maxPulses, val, 1, limits::kMaxPulsesMax);
  } else if (streq(key, "budget")) {
    ok = assign(s.budgetSec, val, limits::kBudgetSecMin, limits::kBudgetSecMax);
  } else if (streq(key, "rise")) {
    ok = assign(s.minRisePct, val, 0, limits::kMinRisePctMax);
  } else if (streq(key, "flow")) {
    ok = assign(s.flowMlPerMin, val, 1, limits::kFlowMax);
  } else if (streq(key, "id")) {
    ok = assign(s.boxId, val, 0, limits::kBoxIdMax);
  } else if (streq(key, "tlm")) {
    uint16_t ms = 0;
    ok = assign(ms, val, 0, limits::kTelemetryMaxMs) && (ms == 0 || ms >= limits::kTelemetryMinMs);
    if (ok) {
      s.telemetryMs = ms;
    }
  } else {
    error(F("unknown setting (soak, cooldown, pulses, budget, rise, flow, id, tlm)"));
    return false;
  }
  if (!ok) {
    error(F("value out of range"));
  }
  return ok;
}

void cmdSet(uint8_t argc, char** argv) {
  uint8_t zone = 0;
  bool ok;
  if (argc == 4 && irr::parseZone(argv[1], &zone)) {
    ok = setZoneField(zone, argv[2], argv[3]);
  } else if (argc == 3) {
    ok = setGlobalField(argv[1], argv[2]);
  } else {
    error(F("usage: set <z> <key> <value>  |  set <key> <value>"));
    return;
  }
  if (!ok) {
    return;
  }
  if (irr::sanitize(g_settings)) {
    Serial.println(F("note: related values were adjusted to stay consistent (see 'config')"));
  }
  okUnsaved();
}

void cmdCal(uint8_t argc, char** argv) {
  uint8_t zone = 0;
  if (argc < 3 || argc > 4 || !irr::parseZone(argv[1], &zone) || zone == kAllZones) {
    error(F("usage: cal <1-4> dry|wet  |  cal <1-4> <raw_dry> <raw_wet>"));
    return;
  }
  irr::ZoneSettings& zs = g_settings.zone[zone];
  uint16_t dry = zs.rawDry;
  uint16_t wet = zs.rawWet;
  if (argc == 3) {
    // Use the raw median, not the filtered value: the filter only accepts
    // samples that were plausible under the *old* calibration.
    if (!g_sensors[zone].sampled()) {
      error(F("no reading yet"));
      return;
    }
    if (streq(argv[2], "dry")) {
      dry = g_sensors[zone].lastRaw();
    } else if (streq(argv[2], "wet")) {
      wet = g_sensors[zone].lastRaw();
    } else {
      error(F("expected 'dry' or 'wet'"));
      return;
    }
  } else if (!assign(dry, argv[2], 0, limits::kAdcMax) || !assign(wet, argv[3], 0, limits::kAdcMax)) {
    error(F("raw values must be 0-1023"));
    return;
  }
  const uint16_t span = dry > wet ? dry - wet : wet - dry;
  if (span < limits::kMinCalSpan) {
    error(F("dry and wet points are too close together"));
    return;
  }
  zs.rawDry = dry;
  zs.rawWet = wet;
  printZone(zone);
  Serial.print(F(" calibration dry="));
  Serial.print(dry);
  Serial.print(F(" wet="));
  Serial.println(wet);
  okUnsaved();
}

void cmdWater(uint8_t argc, char** argv, uint32_t now) {
  uint8_t zone = 0;
  uint32_t ms = 0;
  if (argc != 3 || !irr::parseZone(argv[1], &zone) || zone == kAllZones ||
      !irr::parseUint(argv[2], limits::kPulseMinMs, limits::kPulseMaxMs, &ms)) {
    error(F("usage: water <1-4> <200-10000 ms>"));
    return;
  }
  if (!g_controller.manualPulse(zone, static_cast<uint16_t>(ms), now)) {
    error(F("zone is pumping, disabled or has a sensor fault"));
  }
}

void cmdClear(uint8_t argc, char** argv, uint32_t now) {
  uint8_t zone = kAllZones;
  if (argc == 2 && !irr::parseZone(argv[1], &zone)) {
    error(F("usage: clear [1-4]"));
    return;
  }
  uint8_t first, last;
  zoneRange(zone, first, last);
  uint8_t cleared = 0;
  for (uint8_t z = first; z <= last; ++z) {
    cleared += g_controller.clearFault(z, now) ? 1 : 0;
  }
  Serial.print(cleared);
  Serial.println(F(" fault(s) cleared"));
}

void printResetCause() {
  const board::ResetInfo& ri = board::resetInfo();
  Serial.print(F("reset: "));
  if (ri.mcusr & _BV(WDRF)) {
    Serial.print(F("watchdog"));
  } else if (ri.mcusr & _BV(BORF)) {
    Serial.print(F("brown-out"));
  } else if (ri.mcusr & _BV(EXTRF)) {
    Serial.print(F("reset pin"));
  } else if (ri.mcusr & _BV(PORF)) {
    Serial.print(F("power-on"));
  } else {
    // Optiboot clears MCUSR before the sketch starts; fall back to whether RAM survived.
    Serial.print(ri.warm ? F("warm (watchdog or reset button)") : F("cold start"));
  }
  Serial.print(F(", mcusr="));
  printHex8(ri.mcusr);
  if (ri.warm) {
    Serial.print(F(", warm resets since power-up="));
    Serial.print(ri.warmResets);
  }
  if (ri.lastTask < g_scheduler.count()) {
    Serial.print(F(", interrupted task '"));
    Serial.print(reinterpret_cast<FStr>(g_scheduler.task(ri.lastTask).name));
    Serial.print('\'');
  }
  Serial.println();
}

void cmdStats() {
  Serial.print(F("fw " FW_VERSION "  built " __DATE__ " " __TIME__ "  box "));
  Serial.print(g_settings.boxId);
  Serial.print(F("  uptime "));
  Serial.print(g_uptime.seconds());
  Serial.println(F(" s"));
  printResetCause();
  Serial.print(F("free RAM "));
  Serial.print(board::freeRam());
  Serial.print(F(" B  loop max "));
  Serial.print(g_loopMaxUs);
  Serial.print(F(" us  pump guard trips "));
  Serial.print(g_pumps.guardTrips());
  Serial.print(F("  config "));
  Serial.println(g_settingsFromEeprom ? F("from EEPROM") : F("defaults"));

  Serial.println(F("task       period_ms       runs  last_us  max_us"));
  for (uint8_t i = 0; i < g_scheduler.count(); ++i) {
    const irr::Task& t = g_scheduler.task(i);
    Serial.print(reinterpret_cast<FStr>(t.name));
    for (size_t n = strlen_P(t.name); n < 10; ++n) {
      Serial.print(' ');
    }
    printPadded(t.periodMs, 10);
    printPadded(t.runs, 11);
    printPadded(t.lastUs, 9);
    printPadded(t.maxUs, 8);
    Serial.println();
  }
}

void execute(char* line, uint32_t now) {
  char* argv[kMaxArgs];
  const uint8_t argc = irr::tokenize(line, argv, kMaxArgs);
  if (argc == 0) {
    return;
  }
  const char* cmd = argv[0];
  if (streq(cmd, "help") || streq(cmd, "?")) {
    cmdHelp();
  } else if (streq(cmd, "status") || streq(cmd, "s")) {
    cmdStatus();
  } else if (streq(cmd, "config")) {
    cmdConfig();
  } else if (streq(cmd, "set")) {
    cmdSet(argc, argv);
  } else if (streq(cmd, "cal")) {
    cmdCal(argc, argv);
  } else if (streq(cmd, "water")) {
    cmdWater(argc, argv, now);
  } else if (streq(cmd, "stop")) {
    g_controller.emergencyStop(now);
  } else if (streq(cmd, "clear")) {
    cmdClear(argc, argv, now);
  } else if (streq(cmd, "save")) {
    storage::save(g_settings);
    g_settingsFromEeprom = true;
    Serial.println(F("saved"));
  } else if (streq(cmd, "load")) {
    if (storage::load(g_settings)) {
      g_settingsFromEeprom = true;
      Serial.println(F("loaded"));
    } else {
      error(F("no valid config in EEPROM"));
    }
  } else if (streq(cmd, "defaults")) {
    irr::loadDefaults(g_settings);
    okUnsaved();
  } else if (streq(cmd, "stats")) {
    cmdStats();
  } else if (streq(cmd, "reboot")) {
    Serial.println(F("rebooting..."));
    Serial.flush();
    board::reboot();
  } else {
    Serial.print(F("unknown command '"));
    Serial.print(cmd);
    Serial.println(F("', try 'help'"));
  }
}

}  // namespace

namespace console {

void begin() {
  Serial.println();
  Serial.print(F("Smart Irrigation fw " FW_VERSION "  box "));
  Serial.println(g_settings.boxId);
  printResetCause();
  Serial.println(g_settingsFromEeprom ? F("config: loaded from EEPROM")
                                      : F("config: EEPROM empty or corrupt, defaults written"));
  Serial.println(F("type 'help' for commands (line ending: newline)"));
}

void poll(uint32_t now) {
  while (Serial.available() > 0) {
    const char c = static_cast<char>(Serial.read());
    if (c == '\r' || c == '\n') {
      if (g_overflow) {
        error(F("line too long"));
      } else if (g_len > 0) {
        g_line[g_len] = '\0';
        execute(g_line, now);
      }
      g_len = 0;
      g_overflow = false;
    } else if (g_len < kLineMax - 1) {
      g_line[g_len++] = c;
    } else {
      g_overflow = true;
    }
  }
}

void printEvent(const irr::Event& e, void* /*ctx*/) {
  using irr::EventType;
  printTimestamp(e.t);
  if (e.zone != kAllZones) {
    printZone(e.zone);
    Serial.print(' ');
  }
  switch (e.type) {
    case EventType::CycleStart:
      Serial.print(F("dry at "));
      printDpct(e.value);
      Serial.print(F("%, cycle start"));
      break;
    case EventType::PumpOn:
      Serial.print(F("pump on for "));
      Serial.print(e.value);
      Serial.print(F(" ms"));
      break;
    case EventType::PumpOff:
      Serial.print(F("pump off after "));
      Serial.print(e.value);
      Serial.print(F(" ms, soaking"));
      break;
    case EventType::CycleDone:
      Serial.print(F("target reached at "));
      printDpct(e.value);
      Serial.print('%');
      break;
    case EventType::CyclePartial:
      Serial.print(F("out of pulses at "));
      printDpct(e.value);
      Serial.print(F("%, below target; cooling down"));
      break;
    case EventType::BudgetHold:
      Serial.print(F("daily water budget used ("));
      Serial.print(e.value);
      Serial.print(F(" s), holding"));
      break;
    case EventType::BudgetReset:
      Serial.print(F("new 24 h water budget window"));
      break;
    case EventType::FaultSensor:
      Serial.print(F("FAULT: sensor reading implausible, check wiring"));
      break;
    case EventType::SensorRecovered:
      Serial.print(F("sensor recovered at "));
      printDpct(e.value);
      Serial.print('%');
      break;
    case EventType::FaultNoRise:
      Serial.print(F("FAULT: moisture rose only "));
      printDpct(e.value);
      Serial.print(F("% after max pulses, check reservoir / pump / tubing"));
      break;
    case EventType::FaultCleared:
      Serial.print(F("fault cleared"));
      break;
    case EventType::GuardTrip:
      Serial.print(F("SAFETY: pump hard limit tripped after "));
      Serial.print(e.value);
      Serial.print(F(" ms"));
      break;
    case EventType::ManualPulse:
      Serial.print(F("manual pulse queued, "));
      Serial.print(e.value);
      Serial.print(F(" ms"));
      break;
    case EventType::EmergencyStop:
      Serial.print(F("EMERGENCY STOP: all pumps off, cycles cancelled"));
      break;
  }
  Serial.println();
}

void printTelemetry() {
  Serial.print(F("$TLM,"));
  Serial.print(g_settings.boxId);
  Serial.print(',');
  Serial.print(g_uptime.seconds());
  for (uint8_t z = 0; z < kZones; ++z) {
    Serial.print(',');
    Serial.print(g_readings[z].dpct);
  }
  for (uint8_t z = 0; z < kZones; ++z) {
    Serial.print(',');
    Serial.print(g_sensors[z].lastRaw());
  }
  Serial.print(',');
  Serial.print(g_pumps.mask());
  Serial.print(',');
  for (uint8_t z = 0; z < kZones; ++z) {
    Serial.print(irr::stateLetter(g_controller.state(z)));
  }
  Serial.println();
}

}  // namespace console
