// Closed-loop software-in-the-loop simulation.
//
// Runs the *same* irrigation_core library that ships on the Uno against a
// simple physical model of four potted plants, using the firmware's task
// timing (1 s sampling with pump blanking, 50 ms control, hard pump guard).
// Two faults are injected to prove the safety logic end to end:
//   * P3's reservoir runs dry at hour 24      -> expect FaultNoRise, pump stops
//   * P4's sensor is unplugged hours 20-21    -> expect FaultSensor, then recovery
//
// Build:  g++ -std=c++11 -O2 -Ilib/irrigation_core/src sim/plant_sim.cpp lib/irrigation_core/src/irrigation/*.cpp -o plant_sim
// Run:    ./plant_sim [--hours 48] [--csv sim.csv] [--events events.log]
// Exit code is non-zero if any invariant is violated, so CI can run it.

#include <irrigation_core.h>

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

using namespace irr;

namespace {

constexpr uint32_t kTickMs = 50;
constexpr uint32_t kSampleMs = 1000;
constexpr uint32_t kBlankingMs = 300;
constexpr uint32_t kCsvEveryMs = 60000;
constexpr double kStartHourOfDay = 8.0;  // simulation starts at 08:00
constexpr double kPi = 3.14159265358979;

// Soil model. 1 mL of water raises a ~1.2 L pot by ~0.08 % volumetric
// moisture; water takes ~20 s to soak down to the probe.
constexpr double kPctPerMl = 0.08;
constexpr double kInfiltrationTauSec = 20.0;
constexpr double kFieldCapacity = 95.0;

struct Pot {
  const char* label;
  double moisture;     // true moisture, %
  double peakDryRate;  // evapotranspiration at midday, %/h
  double inTransit;    // water applied but not yet at the probe, %
  double reservoirEmptyAtH;
  double unplugFromH;
  double unplugToH;
};

Pot pots[kZones] = {
    {"P1 fern (thirsty)", 66.0, 1.6, 0, 1e9, 1e9, 1e9},
    {"P2 succulent", 88.0, 0.7, 0, 1e9, 1e9, 1e9},
    {"P3 basil (tank runs dry)", 79.0, 1.2, 0, 24.0, 1e9, 1e9},
    {"P4 pothos (cable pulled)", 84.0, 1.0, 0, 1e9, 20.0, 21.0},
};

std::mt19937 rng(42);
FILE* eventsOut = nullptr;

double hoursAt(uint32_t ms) { return ms / 3600000.0; }

/// Evapotranspiration follows the sun: low at night, peaking at noon.
double diurnal(double simHours) {
  const double hourOfDay = std::fmod(kStartHourOfDay + simHours, 24.0);
  const double sun = std::sin(2.0 * kPi * (hourOfDay - 6.0) / 24.0);
  return 0.3 + 0.7 * (sun > 0 ? sun : 0);
}

uint16_t sensorSample(const Pot& p, const ZoneSettings& zs, double h) {
  std::normal_distribution<double> noise(0.0, 2.0);
  std::uniform_real_distribution<double> uni(0.0, 1.0);
  if (h >= p.unplugFromH && h < p.unplugToH) {
    return static_cast<uint16_t>(150 + uni(rng) * 750);  // floating input
  }
  double raw = zs.rawDry + (static_cast<double>(zs.rawWet) - zs.rawDry) * p.moisture / 100.0;
  raw += noise(rng);
  if (uni(rng) < 0.005) {
    raw += (uni(rng) < 0.5 ? -300 : 300);  // relay / motor spike
  }
  if (raw < 0) raw = 0;
  if (raw > 1023) raw = 1023;
  return static_cast<uint16_t>(raw + 0.5);
}

const char* eventName(EventType t) {
  switch (t) {
    case EventType::CycleStart:      return "cycle_start";
    case EventType::PumpOn:          return "pump_on";
    case EventType::PumpOff:         return "pump_off";
    case EventType::CycleDone:       return "cycle_done";
    case EventType::CyclePartial:    return "cycle_partial";
    case EventType::BudgetHold:      return "budget_hold";
    case EventType::BudgetReset:     return "budget_reset";
    case EventType::FaultSensor:     return "fault_sensor";
    case EventType::SensorRecovered: return "sensor_recovered";
    case EventType::FaultNoRise:     return "fault_no_rise";
    case EventType::FaultCleared:    return "fault_cleared";
    case EventType::GuardTrip:       return "guard_trip";
    case EventType::ManualPulse:     return "manual_pulse";
    case EventType::EmergencyStop:   return "emergency_stop";
  }
  return "?";
}

struct Tally {
  int faultSensor[kZones];
  int sensorRecovered[kZones];
  int faultNoRise[kZones];
  int guardTrips;
  double firstNoRiseH[kZones];
};
Tally tally;

void onEvent(const Event& e, void*) {
  if (e.zone < kZones) {
    if (e.type == EventType::FaultSensor) ++tally.faultSensor[e.zone];
    if (e.type == EventType::SensorRecovered) ++tally.sensorRecovered[e.zone];
    if (e.type == EventType::FaultNoRise) {
      if (tally.faultNoRise[e.zone]++ == 0) tally.firstNoRiseH[e.zone] = hoursAt(e.t);
    }
  }
  if (e.type == EventType::GuardTrip) ++tally.guardTrips;
  if (eventsOut != nullptr) {
    std::fprintf(eventsOut, "%.5f,%s,%s,%u\n", hoursAt(e.t),
                 e.zone < kZones ? pots[e.zone].label : "all", eventName(e.type), e.value);
  }
}

int failures = 0;
void check(bool ok, const char* what) {
  std::printf("  [%s] %s\n", ok ? "PASS" : "FAIL", what);
  if (!ok) ++failures;
}

}  // namespace

int main(int argc, char** argv) {
  double hours = 48;
  const char* csvPath = "sim.csv";
  const char* eventsPath = "events.csv";
  for (int i = 1; i + 1 < argc; i += 2) {
    if (!std::strcmp(argv[i], "--hours")) hours = std::atof(argv[i + 1]);
    else if (!std::strcmp(argv[i], "--csv")) csvPath = argv[i + 1];
    else if (!std::strcmp(argv[i], "--events")) eventsPath = argv[i + 1];
  }

  FILE* csv = std::fopen(csvPath, "w");
  eventsOut = std::fopen(eventsPath, "w");
  if (csv == nullptr || eventsOut == nullptr) {
    std::perror("open output");
    return 2;
  }
  std::fprintf(csv, "hours");
  for (uint8_t z = 0; z < kZones; ++z) {
    std::fprintf(csv, ",true%u,measured%u,state%u", z + 1, z + 1, z + 1);
  }
  std::fprintf(csv, "\n");
  std::fprintf(eventsOut, "hours,zone,event,value\n");

  Settings s;
  loadDefaults(s);
  PumpBank pumps;
  Controller ctl(pumps);
  SensorChannel channels[kZones];
  Reading readings[kZones] = {};
  ctl.begin(0);
  ctl.setEventSink(onEvent);

  const uint32_t endMs = static_cast<uint32_t>(hours * 3600000.0);
  const double dt = kTickMs / 1000.0;
  const double mlPerSec = s.flowMlPerMin / 60.0;
  uint32_t lastSample = 0;
  int maxConcurrent = 0;
  double minAfterSettle[kZones];
  uint32_t maxWaterTodayMs[kZones] = {};
  uint16_t pulsesAtFault[kZones] = {};
  for (uint8_t z = 0; z < kZones; ++z) minAfterSettle[z] = 100;

  for (uint32_t now = 0; now <= endMs; now += kTickMs) {
    const double h = hoursAt(now);

    // ---- physics
    for (uint8_t z = 0; z < kZones; ++z) {
      Pot& p = pots[z];
      p.moisture -= p.peakDryRate * diurnal(h) * dt / 3600.0;
      if (pumps.isOn(z) && h < p.reservoirEmptyAtH) {
        p.inTransit += mlPerSec * dt * kPctPerMl;
      }
      const double arriving = p.inTransit * (dt / kInfiltrationTauSec);
      p.inTransit -= arriving;
      p.moisture += arriving;
      if (p.moisture > kFieldCapacity) p.moisture = kFieldCapacity;  // excess drains
      if (p.moisture < 0) p.moisture = 0;
    }

    // ---- firmware: sensor task (1 s, blanked while pumping)
    if (elapsed(now, lastSample, kSampleMs) && !pumps.anyOn() &&
        elapsed(now, pumps.lastOffAt(), kBlankingMs)) {
      lastSample = now;
      for (uint8_t z = 0; z < kZones; ++z) {
        uint16_t burst[7];
        for (uint16_t& b : burst) b = sensorSample(pots[z], s.zone[z], h);
        channels[z].update(burst, 7, s.zone[z].rawDry, s.zone[z].rawWet);
        readings[z] = channels[z].reading();
      }
    }

    // ---- firmware: control + pump guard
    ctl.tick(now, s, readings);
    pumps.enforce(now);

    // ---- invariants and bookkeeping
    if (pumps.countOn() > maxConcurrent) maxConcurrent = pumps.countOn();
    for (uint8_t z = 0; z < kZones; ++z) {
      if (h > 2.0 && pots[z].moisture < minAfterSettle[z]) minAfterSettle[z] = pots[z].moisture;
      if (ctl.waterTodayMs(z) > maxWaterTodayMs[z]) maxWaterTodayMs[z] = ctl.waterTodayMs(z);
      if (ctl.state(z) == ZoneState::FaultNoRise && pulsesAtFault[z] == 0) {
        pulsesAtFault[z] = ctl.stats(z).pulses;
      }
    }

    if (now % kCsvEveryMs == 0) {
      std::fprintf(csv, "%.4f", h);
      for (uint8_t z = 0; z < kZones; ++z) {
        std::fprintf(csv, ",%.2f,%.1f,%c", pots[z].moisture, readings[z].dpct / 10.0,
                     stateLetter(ctl.state(z)));
      }
      std::fprintf(csv, "\n");
    }
  }
  std::fclose(csv);
  std::fclose(eventsOut);

  std::printf("Simulated %.0f h, %u zones, firmware control library\n\n", hours, kZones);
  std::printf("%-26s %7s %7s %9s %10s %8s\n", "zone", "cycles", "pulses", "water_mL", "min_moist%",
              "state");
  for (uint8_t z = 0; z < kZones; ++z) {
    const ZoneStats& st = ctl.stats(z);
    std::printf("%-26s %7u %7u %9lu %10.1f %8s\n", pots[z].label, st.cycles, st.pulses,
                static_cast<unsigned long>(waterMl(st.pumpMs, s.flowMlPerMin)), minAfterSettle[z],
                stateName(ctl.state(z)));
  }

  std::printf("\nInvariants:\n");
  check(maxConcurrent <= kMaxPumpsOn, "never more than one pump running");
  check(tally.guardTrips == 0, "hard pump guard never needed (controller timing correct)");
  bool budgetOk = true;
  for (uint8_t z = 0; z < kZones; ++z) budgetOk &= maxWaterTodayMs[z] <= s.budgetSec * 1000UL;
  check(budgetOk, "daily water budget respected on every zone");
  check(minAfterSettle[0] > 60.0 && minAfterSettle[1] > 60.0 && minAfterSettle[3] > 60.0,
        "healthy zones stay above 60 % moisture");
  check(tally.faultNoRise[2] == 1 && tally.firstNoRiseH[2] > 24.0,
        "P3: empty reservoir detected as FaultNoRise (after hour 24)");
  check(ctl.state(2) == ZoneState::FaultNoRise && ctl.stats(2).pulses == pulsesAtFault[2],
        "P3: fault latched, pump never ran dry again");
  check(tally.faultSensor[3] == 1 && tally.sensorRecovered[3] == 1,
        "P4: unplugged sensor detected and recovered automatically");
  check(tally.faultNoRise[0] + tally.faultNoRise[1] + tally.faultNoRise[3] == 0 &&
            tally.faultSensor[0] + tally.faultSensor[1] + tally.faultSensor[2] == 0,
        "no false-positive faults on healthy zones (despite injected noise spikes)");

  std::printf("\n%s (%d failure%s)\n", failures ? "FAILED" : "ALL CHECKS PASSED", failures,
              failures == 1 ? "" : "s");
  return failures ? 1 : 0;
}
