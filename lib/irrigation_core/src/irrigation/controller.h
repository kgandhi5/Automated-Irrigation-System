// Per-zone watering state machine and the pump arbiter.
//
//   Idle --dry for kDryConfirmMs--> Queued --granted--> Watering --pulseMs--> Soaking
//     ^                               ^                                          |
//     |                               +------- below target, pulses left -------+
//     |                                                                          |
//     +--- Cooldown <--- target reached, or max pulses with a real rise ---------+
//                                                                                |
//          FaultNoRise <--- max pulses and moisture rose < minRisePct -----------+
//
// Any state -> FaultSensor when readings stay implausible; it recovers on its own.
// FaultNoRise latches until clearFault() or a power cycle: it usually means an
// empty reservoir, and pumping on would just run the pump dry.
#pragma once

#include <stdint.h>

#include "pump_bank.h"
#include "settings.h"
#include "types.h"

namespace irr {

namespace timing {
constexpr uint32_t kWarmupMs = 5000;         ///< let sensors and filters settle after boot
constexpr uint32_t kDryConfirmMs = 5000;     ///< dryness must persist this long (debounce)
constexpr uint32_t kSensorFaultMs = 5000;    ///< invalid this long -> FaultSensor
constexpr uint32_t kSensorRecoverMs = 15000; ///< valid this long -> recover
constexpr uint32_t kInterPumpGapMs = 750;    ///< dead time between pumps (inrush, relay bounce)
constexpr uint32_t kBudgetWindowMs = 24UL * 60UL * 60UL * 1000UL;
}  // namespace timing

enum class EventType : uint8_t {
  CycleStart,       ///< value = moisture (0.1 %)
  PumpOn,           ///< value = planned pulse ms
  PumpOff,          ///< value = actual ms
  CycleDone,        ///< target reached, value = moisture
  CyclePartial,     ///< max pulses, moisture rose but missed target, value = moisture
  BudgetHold,       ///< daily budget used up, value = seconds used today
  BudgetReset,      ///< new 24 h window (zone = kAllZones)
  FaultSensor,
  SensorRecovered,
  FaultNoRise,      ///< value = rise achieved (0.1 %)
  FaultCleared,
  GuardTrip,        ///< hard on-time limit had to stop the pump, value = ms
  ManualPulse,      ///< value = ms
  EmergencyStop,    ///< zone = kAllZones
};

constexpr uint8_t kAllZones = 0xFF;

struct Event {
  uint32_t t;
  EventType type;
  uint8_t zone;
  uint16_t value;
};

typedef void (*EventSink)(const Event& e, void* ctx);

struct ZoneStats {
  uint16_t cycles;
  uint16_t pulses;
  uint16_t faults;
  uint32_t pumpMs;
};

class Controller {
 public:
  explicit Controller(PumpBank& pumps) : pumps_(pumps) {}

  void begin(uint32_t now);
  void setEventSink(EventSink sink, void* ctx = nullptr) {
    sink_ = sink;
    sinkCtx_ = ctx;
  }

  /// Advances every zone. `readings` must hold kZones entries.
  void tick(uint32_t now, const Settings& s, const Reading* readings);

  /// Queues a one-off pulse (console `water` command). Bypasses the budget
  /// but not the pump interlocks. Refused while watering or disabled.
  bool manualPulse(uint8_t zone, uint16_t ms, uint32_t now);
  bool clearFault(uint8_t zone, uint32_t now);
  /// Stops all pumps and puts every active zone into cooldown.
  void emergencyStop(uint32_t now);

  ZoneState state(uint8_t zone) const { return zones_[zone].state; }
  uint8_t pulsesThisCycle(uint8_t zone) const { return zones_[zone].pulses; }
  const ZoneStats& stats(uint8_t zone) const { return zones_[zone].stats; }
  uint32_t waterTodayMs(uint8_t zone) const { return zones_[zone].waterTodayMs; }
  uint32_t waterTodayMsTotal() const;
  bool warm() const { return warm_; }
  bool anyFault() const;
  bool anyBusy() const;  ///< a cycle is in progress (queued / soaking / hold)

 private:
  struct Zone {
    ZoneState state;
    bool manual;
    bool dryPending;
    bool lastValid;
    uint8_t pulses;
    uint16_t pulseMs;
    uint16_t cycleStartDpct;
    uint32_t since;        ///< entry time of the current state
    uint32_t drySince;
    uint32_t validSince;   ///< time `lastValid` last changed
    uint32_t waterTodayMs;
    ZoneStats stats;
  };

  void stepZone(uint8_t z, uint32_t now, const Settings& s, const Reading& r);
  void evaluateSoak(uint8_t z, uint32_t now, const Settings& s, const Reading& r);
  void arbitrate(uint32_t now, const Reading* readings);
  void finishPulse(uint8_t z, uint32_t now, uint32_t ranMs);
  bool budgetAllows(uint8_t z, const Settings& s, uint16_t pulseMs) const;
  void enter(uint8_t z, ZoneState st, uint32_t now);
  void emit(uint32_t now, EventType type, uint8_t zone, uint32_t value);

  PumpBank& pumps_;
  Zone zones_[kZones] = {};
  EventSink sink_ = nullptr;
  void* sinkCtx_ = nullptr;
  uint32_t bootAt_ = 0;
  uint32_t windowStart_ = 0;
  bool warm_ = false;
};

}  // namespace irr
