// Cooperative scheduler: due-time logic, drift, overrun resync, profiling
// and the watchdog breadcrumb.
#include <irrigation_core.h>
#include <unity.h>

using namespace irr;

namespace {

int runsSlow = 0;
int runsEvery = 0;
uint32_t fakeUs = 0;
volatile uint8_t crumb = 0xAA;
uint8_t crumbSeenBySlow = 0;

void slowTask(uint32_t) {
  ++runsSlow;
  crumbSeenBySlow = crumb;
  fakeUs += 400;  // pretend this task takes 400 us
}
void everyTask(uint32_t) { ++runsEvery; }
uint32_t fakeMicros() { return fakeUs; }

Task tasks[] = {
    {"slow", slowTask, 100, 0, 0, 0, 0},
    {"every", everyTask, 0, 0, 0, 0, 0},
};
Scheduler sched(tasks, 2);

}  // namespace

void setUp() {
  runsSlow = 0;
  runsEvery = 0;
  fakeUs = 0;
  sched.begin(0, fakeMicros, &crumb);
}
void tearDown() {}

void test_periodic_task_runs_only_when_due() {
  sched.runDue(50);
  TEST_ASSERT_EQUAL_INT(0, runsSlow);
  TEST_ASSERT_EQUAL_INT(1, runsEvery);
  sched.runDue(100);
  TEST_ASSERT_EQUAL_INT(1, runsSlow);
  TEST_ASSERT_EQUAL_INT(2, runsEvery);
}

void test_jitter_does_not_accumulate() {
  sched.runDue(105);  // late by 5 ms
  sched.runDue(199);
  TEST_ASSERT_EQUAL_INT(1, runsSlow);
  sched.runDue(200);  // still on the 100 ms grid
  TEST_ASSERT_EQUAL_INT(2, runsSlow);
}

void test_overrun_resyncs_instead_of_bursting() {
  sched.runDue(350);  // missed 3 periods
  TEST_ASSERT_EQUAL_INT(1, runsSlow);
  sched.runDue(351);
  sched.runDue(400);
  TEST_ASSERT_EQUAL_INT(1, runsSlow);
  sched.runDue(450);
  TEST_ASSERT_EQUAL_INT(2, runsSlow);
}

void test_profiles_execution_time() {
  sched.runDue(100);
  TEST_ASSERT_EQUAL_UINT16(400, sched.task(0).lastUs);
  TEST_ASSERT_EQUAL_UINT16(400, sched.task(0).maxUs);
  TEST_ASSERT_EQUAL_UINT32(1, sched.task(0).runs);
}

void test_breadcrumb_marks_running_task() {
  sched.runDue(100);
  TEST_ASSERT_EQUAL_UINT8(0, crumbSeenBySlow);
  TEST_ASSERT_EQUAL_UINT8(kNoTask, crumb);
}

void test_handles_millis_rollover() {
  sched.begin(0xFFFFFFC0UL, fakeMicros, &crumb);
  sched.runDue(0xFFFFFFFFUL);
  TEST_ASSERT_EQUAL_INT(0, runsSlow);
  sched.runDue(0x00000024UL);  // 100 ms after begin, across the wrap
  TEST_ASSERT_EQUAL_INT(1, runsSlow);
}

int main(int, char**) {
  UNITY_BEGIN();
  RUN_TEST(test_periodic_task_runs_only_when_due);
  RUN_TEST(test_jitter_does_not_accumulate);
  RUN_TEST(test_overrun_resyncs_instead_of_bursting);
  RUN_TEST(test_profiles_execution_time);
  RUN_TEST(test_breadcrumb_marks_running_task);
  RUN_TEST(test_handles_millis_rollover);
  return UNITY_END();
}
