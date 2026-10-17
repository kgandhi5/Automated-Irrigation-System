// Cooperative, non-blocking periodic task scheduler with per-task execution
// profiling and a crash breadcrumb for watchdog post-mortems.
#pragma once

#include <stdint.h>

namespace irr {

typedef void (*TaskFn)(uint32_t now);
typedef uint32_t (*MicrosFn)();

struct Task {
  const char* name;   ///< opaque to the scheduler (lives in flash on AVR)
  TaskFn fn;
  uint16_t periodMs;  ///< 0 = run on every pass
  uint32_t lastRun;
  uint32_t runs;
  uint16_t lastUs;
  uint16_t maxUs;     ///< worst-case execution time seen
};

constexpr uint8_t kNoTask = 0xFF;

class Scheduler {
 public:
  Scheduler(Task* tasks, uint8_t count) : tasks_(tasks), count_(count) {}

  /// `micros` times each task; `breadcrumb` (may be null) is set to the index
  /// of the running task and back to kNoTask afterwards. Point it at RAM the
  /// C runtime does not clear and a watchdog reset reveals which task hung.
  void begin(uint32_t now, MicrosFn micros, volatile uint8_t* breadcrumb);

  /// Runs every task that is due, in table order. Returns how many ran.
  uint8_t runDue(uint32_t now);

  uint8_t count() const { return count_; }
  const Task& task(uint8_t i) const { return tasks_[i]; }
  void resetStats();

 private:
  Task* tasks_;
  uint8_t count_;
  MicrosFn micros_ = nullptr;
  volatile uint8_t* breadcrumb_ = nullptr;
};

}  // namespace irr
