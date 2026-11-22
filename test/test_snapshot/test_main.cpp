// Host unit tests: saving the timer to NVS and continuing after a reboot.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <vector>

#include "timer/snapshot.h"

using faces::Face;
using timer::EventType;
using timer::Mode;
using timer::Status;

namespace {

int64_t mono = 0;
int64_t epoch = timer::NONE;  // absolute epoch ms, NONE = clock not synced
int uuids = 0;
std::vector<EventType> events;

timer::TimerCore make() {
    timer::Env env;
    env.monotonicMs = [] { return mono; };
    env.epochMs = [] { return epoch; };
    env.newUuid = [](char out[37]) { snprintf(out, 37, "00000000-0000-4000-8000-%012d", ++uuids); };
    env.onEvent = [](EventType t, Face) { events.push_back(t); };
    timer::TimerCore core(env);
    timer::Durations d;
    d.workSeconds = 1500;
    d.shortBreakSeconds = 300;
    d.longBreakSeconds = 900;
    core.configure(d);
    return core;
}

}  // namespace

void setUp() {
    mono = 5000;
    epoch = timer::NONE;
    uuids = 0;
    events.clear();
}

void tearDown() {}

void test_capture_running_without_clock_has_no_end() {
    auto t = make();
    t.start(Mode::Work);
    mono += 100000;
    const timer::Saved s = timer::capture(t);
    TEST_ASSERT_TRUE(timer::isValid(s));
    TEST_ASSERT_EQUAL(Status::Running, s.status);
    TEST_ASSERT_EQUAL_UINT32(1400, s.remainingSeconds);
    TEST_ASSERT_EQUAL(timer::NONE, s.endsAtEpoch);
    TEST_ASSERT_EQUAL_STRING("00000000-0000-4000-8000-000000000001", s.sessionId);
}

void test_capture_running_with_clock_keeps_absolute_end() {
    epoch = 1761150000000LL;
    auto t = make();
    t.start(Mode::Work);
    mono += 100000;
    epoch += 100000;
    const timer::Saved s = timer::capture(t);
    TEST_ASSERT_EQUAL(1761150000000LL + 1500000, s.endsAtEpoch);
}

void test_equal_states_give_equal_bytes() {
    auto t = make();
    t.start(Mode::ShortBreak);
    const timer::Saved a = timer::capture(t);
    const timer::Saved b = timer::capture(t);
    TEST_ASSERT_EQUAL_MEMORY(&a, &b, sizeof(a));
}

void test_running_comes_back_paused_without_clock() {
    auto t = make();
    t.start(Mode::Work);
    mono += 300000;
    const timer::Saved s = timer::capture(t);

    events.clear();
    auto after = make();  // reboot
    after.restoreAndSync(timer::restoredState(s, timer::NONE));
    TEST_ASSERT_EQUAL(Status::Paused, after.state().status);
    TEST_ASSERT_EQUAL_UINT32(1200, after.remainingSeconds());
    TEST_ASSERT_EQUAL_STRING("00000000-0000-4000-8000-000000000001", after.state().sessionId);
    TEST_ASSERT_EQUAL(1, events.size());
    TEST_ASSERT_EQUAL(EventType::StateSync, events[0]);
}

void test_continue_until_epoch_resumes_to_the_same_end() {
    epoch = 1761150000000LL;
    auto t = make();
    t.start(Mode::Work);
    const timer::Saved s = timer::capture(t);  // ends at epoch + 1 500 000

    mono = 2000;          // reboot: monotonic starts again
    epoch += 600000;      // the cube was off for 10 minutes
    auto after = make();
    after.restoreAndSync(timer::restoredState(s, epoch));
    TEST_ASSERT_TRUE(after.continueUntilEpoch(s.endsAtEpoch));
    TEST_ASSERT_EQUAL(Status::Running, after.state().status);
    TEST_ASSERT_EQUAL_UINT32(900, after.remainingSeconds());
    TEST_ASSERT_EQUAL_STRING("00000000-0000-4000-8000-000000000001", after.state().sessionId);
    TEST_ASSERT_EQUAL(s.endsAtEpoch, after.epochEndsAt());
    TEST_ASSERT_EQUAL(EventType::Resumed, events.back());
}

void test_continue_until_epoch_finishes_when_end_passed() {
    epoch = 1761150000000LL;
    auto t = make();
    t.start(Mode::ShortBreak);
    const timer::Saved s = timer::capture(t);

    mono = 2000;
    epoch += 3600000;  // off for an hour
    auto after = make();
    after.restoreAndSync(timer::restoredState(s, epoch));
    TEST_ASSERT_TRUE(after.continueUntilEpoch(s.endsAtEpoch));
    TEST_ASSERT_EQUAL(Status::Finished, after.state().status);
    TEST_ASSERT_EQUAL_UINT32(0, after.remainingSeconds());
    TEST_ASSERT_EQUAL(EventType::Finished, events.back());
}

void test_continue_needs_clock_and_paused_state() {
    auto t = make();
    TEST_ASSERT_FALSE(t.continueUntilEpoch(1761150000000LL));  // STOPPED, no clock
    epoch = 1761150000000LL;
    TEST_ASSERT_FALSE(t.continueUntilEpoch(timer::NONE));
}

void test_paused_stopped_finished_come_back_as_they_were() {
    auto t = make();
    t.start(Mode::LongBreak);
    mono += 60000;
    t.applyCommand(timer::CommandType::Pause, false, Mode::Work, nullptr);
    timer::State st = timer::restoredState(timer::capture(t), timer::NONE);
    TEST_ASSERT_EQUAL(Status::Paused, st.status);
    TEST_ASSERT_EQUAL_UINT32(840, st.remainingSeconds);

    t.applyCommand(timer::CommandType::Stop, false, Mode::Work, nullptr);
    st = timer::restoredState(timer::capture(t), timer::NONE);
    TEST_ASSERT_EQUAL(Status::Stopped, st.status);
    TEST_ASSERT_EQUAL_STRING("", st.sessionId);
}

void test_invalid_saved_data_is_rejected() {
    auto t = make();
    timer::Saved s = timer::capture(t);
    TEST_ASSERT_TRUE(timer::isValid(s));
    timer::Saved bad = s;
    bad.version = 99;
    TEST_ASSERT_FALSE(timer::isValid(bad));
    bad = s;
    bad.remainingSeconds = bad.durationSeconds + 1;
    TEST_ASSERT_FALSE(timer::isValid(bad));
    bad = s;
    bad.durationSeconds = 0;
    TEST_ASSERT_FALSE(timer::isValid(bad));
    bad = s;
    memset(bad.sessionId, 'x', sizeof(bad.sessionId));
    TEST_ASSERT_FALSE(timer::isValid(bad));
    bad = s;
    memset(&bad.status, 7, 1);
    TEST_ASSERT_FALSE(timer::isValid(bad));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_capture_running_without_clock_has_no_end);
    RUN_TEST(test_capture_running_with_clock_keeps_absolute_end);
    RUN_TEST(test_equal_states_give_equal_bytes);
    RUN_TEST(test_running_comes_back_paused_without_clock);
    RUN_TEST(test_continue_until_epoch_resumes_to_the_same_end);
    RUN_TEST(test_continue_until_epoch_finishes_when_end_passed);
    RUN_TEST(test_continue_needs_clock_and_paused_state);
    RUN_TEST(test_paused_stopped_finished_come_back_as_they_were);
    RUN_TEST(test_invalid_saved_data_is_rejected);
    return UNITY_END();
}
