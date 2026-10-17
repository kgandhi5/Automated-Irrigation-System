#include "scheduler.h"

#include "types.h"

namespace irr {

void Scheduler::begin(uint32_t now, MicrosFn micros, volatile uint8_t* breadcrumb) {
  micros_ = micros;
  breadcrumb_ = breadcrumb;
  for (uint8_t i = 0; i < count_; ++i) {
    tasks_[i].lastRun = now;
  }
  resetStats();
}

void Scheduler::resetStats() {
  for (uint8_t i = 0; i < count_; ++i) {
    tasks_[i].runs = 0;
    tasks_[i].lastUs = 0;
    tasks_[i].maxUs = 0;
  }
}

uint8_t Scheduler::runDue(uint32_t now) {
  uint8_t ran = 0;
  for (uint8_t i = 0; i < count_; ++i) {
    Task& t = tasks_[i];
    if (t.periodMs != 0) {
      if (!elapsed(now, t.lastRun, t.periodMs)) {
        continue;
      }
      // Advance by exactly one period so the schedule does not drift with
      // loop jitter, but if we fell a whole period behind (a slow task, a
      // long serial print) resync instead of firing a burst of catch-up runs.
      t.lastRun += t.periodMs;
      if (elapsed(now, t.lastRun, t.periodMs)) {
        t.lastRun = now;
      }
    }

    if (breadcrumb_ != nullptr) {
      *breadcrumb_ = i;
    }
    const uint32_t t0 = micros_ != nullptr ? micros_() : 0;
    t.fn(now);
    const uint32_t dt = micros_ != nullptr ? micros_() - t0 : 0;
    if (breadcrumb_ != nullptr) {
      *breadcrumb_ = kNoTask;
    }

    t.lastUs = static_cast<uint16_t>(dt > 0xFFFFUL ? 0xFFFFUL : dt);
    if (t.lastUs > t.maxUs) {
      t.maxUs = t.lastUs;
    }
    ++t.runs;
    ++ran;
  }
  return ran;
}

}  // namespace irr
