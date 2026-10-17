#include "controller.h"

namespace irr {

void Controller::begin(uint32_t now) {
  bootAt_ = now;
  windowStart_ = now;
  warm_ = false;
  for (uint8_t z = 0; z < kZones; ++z) {
    zones_[z] = Zone();
    zones_[z].state = ZoneState::Idle;
    zones_[z].since = now;
    zones_[z].validSince = now;
  }
  pumps_.stopAll(now);
}

void Controller::tick(uint32_t now, const Settings& s, const Reading* readings) {
  if (elapsed(now, windowStart_, timing::kBudgetWindowMs)) {
    windowStart_ += timing::kBudgetWindowMs;
    for (uint8_t z = 0; z < kZones; ++z) {
      zones_[z].waterTodayMs = 0;
    }
    emit(now, EventType::BudgetReset, kAllZones, 0);
  }

  // A pump that is off while its zone is still Watering was stopped by the
  // PumpBank hard limit. Account for the water and carry on as if the pulse ended.
  for (uint8_t z = 0; z < kZones; ++z) {
    if (zones_[z].state == ZoneState::Watering && !pumps_.isOn(z)) {
      emit(now, EventType::GuardTrip, z, pumps_.lastRunMs(z));
      finishPulse(z, now, pumps_.lastRunMs(z));
    }
  }

  if (!warm_) {
    if (!elapsed(now, bootAt_, timing::kWarmupMs)) {
      return;
    }
    warm_ = true;
    for (uint8_t z = 0; z < kZones; ++z) {
      zones_[z].lastValid = readings[z].valid;
      zones_[z].validSince = now;
    }
  }

  for (uint8_t z = 0; z < kZones; ++z) {
    stepZone(z, now, s, readings[z]);
  }
  arbitrate(now, readings);
}

void Controller::stepZone(uint8_t z, uint32_t now, const Settings& s, const Reading& r) {
  Zone& zn = zones_[z];
  const ZoneSettings& zs = s.zone[z];

  if (!zs.enabled) {
    if (zn.state != ZoneState::Disabled) {
      if (zn.state == ZoneState::Watering) {
        finishPulse(z, now, pumps_.stop(z, now));
      }
      enter(z, ZoneState::Disabled, now);
    }
    return;
  }
  if (zn.state == ZoneState::Disabled) {
    enter(z, ZoneState::Idle, now);
    zn.lastValid = r.valid;
    zn.validSince = now;
  }

  // Sensor health is time-qualified so one bad burst never trips a fault.
  // A latched FaultNoRise stays put: FaultSensor recovers on its own, and
  // passing through it would silently clear the empty-reservoir latch.
  if (r.valid != zn.lastValid) {
    zn.lastValid = r.valid;
    zn.validSince = now;
  }
  if (!r.valid && !isFault(zn.state) && elapsed(now, zn.validSince, timing::kSensorFaultMs)) {
    if (zn.state == ZoneState::Watering) {
      finishPulse(z, now, pumps_.stop(z, now));
    }
    ++zn.stats.faults;
    enter(z, ZoneState::FaultSensor, now);
    emit(now, EventType::FaultSensor, z, 0);
    return;
  }

  switch (zn.state) {
    case ZoneState::Idle: {
      const bool dry = r.valid && r.dpct < zs.dryPct * 10U;
      if (!dry) {
        zn.dryPending = false;
        break;
      }
      if (!zn.dryPending) {
        zn.dryPending = true;
        zn.drySince = now;
        break;
      }
      if (!elapsed(now, zn.drySince, timing::kDryConfirmMs)) {
        break;
      }
      if (!budgetAllows(z, s, zs.pulseMs)) {
        enter(z, ZoneState::Hold, now);
        emit(now, EventType::BudgetHold, z, zn.waterTodayMs / 1000UL);
        break;
      }
      zn.manual = false;
      zn.pulses = 0;
      zn.pulseMs = zs.pulseMs;
      zn.cycleStartDpct = r.dpct;
      enter(z, ZoneState::Queued, now);
      emit(now, EventType::CycleStart, z, r.dpct);
      break;
    }

    case ZoneState::Queued:
      // Watered by hand while waiting for the pump: nothing left to do.
      if (!zn.manual && r.valid && r.dpct >= zs.targetPct * 10U) {
        enter(z, ZoneState::Idle, now);
      }
      break;

    case ZoneState::Watering:
      if (elapsed(now, zn.since, zn.pulseMs)) {
        finishPulse(z, now, pumps_.stop(z, now));
      }
      break;

    case ZoneState::Soaking:
      if (elapsed(now, zn.since, s.soakSec * 1000UL)) {
        evaluateSoak(z, now, s, r);
      }
      break;

    case ZoneState::Cooldown:
      if (elapsed(now, zn.since, s.cooldownMin * 60000UL)) {
        enter(z, ZoneState::Idle, now);
      }
      break;

    case ZoneState::Hold:
      if (budgetAllows(z, s, zs.pulseMs)) {
        enter(z, ZoneState::Idle, now);
      }
      break;

    case ZoneState::FaultSensor:
      if (r.valid && elapsed(now, zn.validSince, timing::kSensorRecoverMs)) {
        enter(z, ZoneState::Idle, now);
        emit(now, EventType::SensorRecovered, z, r.dpct);
      }
      break;

    case ZoneState::FaultNoRise:  // latched
    case ZoneState::Disabled:
      break;
  }
}

void Controller::evaluateSoak(uint8_t z, uint32_t now, const Settings& s, const Reading& r) {
  Zone& zn = zones_[z];
  if (zn.manual) {
    zn.manual = false;
    enter(z, ZoneState::Idle, now);
    return;
  }
  if (!r.valid) {
    return;  // wait for a good reading; a persistent fault is caught in stepZone
  }

  if (r.dpct >= s.zone[z].targetPct * 10U) {
    ++zn.stats.cycles;
    enter(z, ZoneState::Cooldown, now);
    emit(now, EventType::CycleDone, z, r.dpct);
    return;
  }

  if (zn.pulses < s.maxPulses) {
    if (budgetAllows(z, s, zn.pulseMs)) {
      enter(z, ZoneState::Queued, now);
    } else {
      enter(z, ZoneState::Hold, now);
      emit(now, EventType::BudgetHold, z, zn.waterTodayMs / 1000UL);
    }
    return;
  }

  // Out of pulses. If the water made no measurable difference, something
  // between the reservoir and the soil is broken: stop instead of retrying.
  const uint16_t rise = r.dpct > zn.cycleStartDpct ? r.dpct - zn.cycleStartDpct : 0;
  if (rise < s.minRisePct * 10U) {
    ++zn.stats.faults;
    enter(z, ZoneState::FaultNoRise, now);
    emit(now, EventType::FaultNoRise, z, rise);
    return;
  }
  ++zn.stats.cycles;
  enter(z, ZoneState::Cooldown, now);
  emit(now, EventType::CyclePartial, z, r.dpct);
}

void Controller::arbitrate(uint32_t now, const Reading* readings) {
  if (pumps_.countOn() >= kMaxPumpsOn ||
      !elapsed(now, pumps_.lastOffAt(), timing::kInterPumpGapMs)) {
    return;
  }
  // Driest zone first; manual pulses jump the queue.
  uint8_t best = kZones;
  uint16_t bestKey = 0xFFFF;
  for (uint8_t z = 0; z < kZones; ++z) {
    if (zones_[z].state != ZoneState::Queued) {
      continue;
    }
    const uint16_t key = zones_[z].manual ? 0 : static_cast<uint16_t>(readings[z].dpct + 1);
    if (key < bestKey) {
      bestKey = key;
      best = z;
    }
  }
  if (best == kZones || !pumps_.start(best, now)) {
    return;
  }
  Zone& zn = zones_[best];
  ++zn.pulses;
  ++zn.stats.pulses;
  enter(best, ZoneState::Watering, now);
  emit(now, EventType::PumpOn, best, zn.pulseMs);
}

bool Controller::manualPulse(uint8_t zone, uint16_t ms, uint32_t now) {
  if (zone >= kZones) {
    return false;
  }
  Zone& zn = zones_[zone];
  if (zn.state == ZoneState::Watering || zn.state == ZoneState::Disabled ||
      zn.state == ZoneState::FaultSensor) {
    return false;
  }
  if (ms < limits::kPulseMinMs) {
    ms = limits::kPulseMinMs;
  }
  if (ms > limits::kPulseMaxMs) {
    ms = limits::kPulseMaxMs;
  }
  zn.manual = true;
  zn.pulses = 0;
  zn.pulseMs = ms;
  enter(zone, ZoneState::Queued, now);
  emit(now, EventType::ManualPulse, zone, ms);
  return true;
}

bool Controller::clearFault(uint8_t zone, uint32_t now) {
  if (zone >= kZones || !isFault(zones_[zone].state)) {
    return false;
  }
  enter(zone, ZoneState::Idle, now);
  zones_[zone].validSince = now;
  emit(now, EventType::FaultCleared, zone, 0);
  return true;
}

void Controller::emergencyStop(uint32_t now) {
  for (uint8_t z = 0; z < kZones; ++z) {
    Zone& zn = zones_[z];
    if (zn.state == ZoneState::Watering) {
      finishPulse(z, now, pumps_.stop(z, now));
    }
    if (zn.state == ZoneState::Queued || zn.state == ZoneState::Soaking) {
      zn.manual = false;
      enter(z, ZoneState::Cooldown, now);
    }
  }
  pumps_.stopAll(now);
  emit(now, EventType::EmergencyStop, kAllZones, 0);
}

uint32_t Controller::waterTodayMsTotal() const {
  uint32_t total = 0;
  for (uint8_t z = 0; z < kZones; ++z) {
    total += zones_[z].waterTodayMs;
  }
  return total;
}

bool Controller::anyFault() const {
  for (uint8_t z = 0; z < kZones; ++z) {
    if (isFault(zones_[z].state)) {
      return true;
    }
  }
  return false;
}

bool Controller::anyBusy() const {
  for (uint8_t z = 0; z < kZones; ++z) {
    const ZoneState st = zones_[z].state;
    if (st == ZoneState::Queued || st == ZoneState::Soaking || st == ZoneState::Hold) {
      return true;
    }
  }
  return false;
}

void Controller::finishPulse(uint8_t z, uint32_t now, uint32_t ranMs) {
  Zone& zn = zones_[z];
  zn.waterTodayMs += ranMs;
  zn.stats.pumpMs += ranMs;
  enter(z, ZoneState::Soaking, now);
  emit(now, EventType::PumpOff, z, ranMs);
}

bool Controller::budgetAllows(uint8_t z, const Settings& s, uint16_t pulseMs) const {
  return zones_[z].waterTodayMs + pulseMs <= s.budgetSec * 1000UL;
}

void Controller::enter(uint8_t z, ZoneState st, uint32_t now) {
  zones_[z].state = st;
  zones_[z].since = now;
  zones_[z].dryPending = false;
}

void Controller::emit(uint32_t now, EventType type, uint8_t zone, uint32_t value) {
  if (sink_ == nullptr) {
    return;
  }
  const Event e{now, type, zone, static_cast<uint16_t>(value > 0xFFFFUL ? 0xFFFFUL : value)};
  sink_(e, sinkCtx_);
}

}  // namespace irr
