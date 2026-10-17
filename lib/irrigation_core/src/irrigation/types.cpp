#include "types.h"

namespace irr {

const char* stateName(ZoneState s) {
  switch (s) {
    case ZoneState::Disabled:    return "OFF";
    case ZoneState::Idle:        return "OK";
    case ZoneState::Queued:      return "WAIT";
    case ZoneState::Watering:    return "PUMP";
    case ZoneState::Soaking:     return "SOAK";
    case ZoneState::Cooldown:    return "COOL";
    case ZoneState::Hold:        return "HOLD";
    case ZoneState::FaultSensor: return "SENS";
    case ZoneState::FaultNoRise: return "FAIL";
  }
  return "?";
}

char stateLetter(ZoneState s) {
  switch (s) {
    case ZoneState::Disabled:    return 'D';
    case ZoneState::Idle:        return 'I';
    case ZoneState::Queued:      return 'Q';
    case ZoneState::Watering:    return 'W';
    case ZoneState::Soaking:     return 'S';
    case ZoneState::Cooldown:    return 'C';
    case ZoneState::Hold:        return 'H';
    case ZoneState::FaultSensor: return 'E';
    case ZoneState::FaultNoRise: return 'F';
  }
  return '?';
}

bool isFault(ZoneState s) {
  return s == ZoneState::FaultSensor || s == ZoneState::FaultNoRise;
}

}  // namespace irr
