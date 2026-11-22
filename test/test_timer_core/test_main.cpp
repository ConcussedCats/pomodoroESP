// Host unit tests of the cube timer (pio test -e native).
// Scenarios follow tools/esp32-emulator/cube-state.mjs and PROTOCOL.md sections 1, 3.5, 5.
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include <string>
#include <vector>

#include "timer/timer_core.h"

using faces::Face;
using timer::CommandType;
using timer::EventType;
using timer::Mode;
using timer::Status;

namespace {

int64_t nowMs = 0;
int64_t epoch = timer::NONE;
int uuidCounter = 0;
std::vector<std::string> events;

timer::TimerCore make(timer::Durations d = {}) {
    timer::Env env;
    env.monotonicMs = [] { return nowMs; };
    env.epochMs = [] { return epoch == timer::NONE ? timer::NONE : epoch + nowMs; };
    env.newUuid = [](char out[37]) { snprintf(out, 37, "00000000-0000-4000-8000-%012d", ++uuidCounter); };
    env.onEvent = [](EventType t, Face f) {
        std::string e = timer::name(t);
        if (t == EventType::FaceChanged) e += std::string(":") + faces::name(f);
        events.push_back(e);
    };
    timer::TimerCore core(env);
    core.configure(d);
    return core;
}

timer::Durations shortDurations() {
    timer::Durations d;
    d.workSeconds = 20;
    d.shortBreakSeconds = 10;
    d.longBreakSeconds = 15;
    return d;
}

void lastEvent(const char* expected) {
    TEST_ASSERT_FALSE(events.empty());
    TEST_ASSERT_EQUAL_STRING(expected, events.back().c_str());
}

}  // namespace

void setUp() {
    nowMs = 1000;
    epoch = timer::NONE;
    uuidCounter = 0;
    events.clear();
}

void tearDown() {}

// --- faces -------------------------------------------------------------------

void test_first_face_after_boot_is_baseline_only() {
    auto t = make();
    t.applyFace(Face::Work);
    TEST_ASSERT_EQUAL(Status::Stopped, t.state().status);
    TEST_ASSERT_EQUAL(1, events.size());
    lastEvent("FACE_CHANGED:WORK");
}

void test_flip_to_mode_starts_full_duration_with_new_session() {
    auto t = make(shortDurations());
    t.applyFace(Face::Display);
    t.applyFace(Face::Work);
    TEST_ASSERT_EQUAL(Status::Running, t.state().status);
    TEST_ASSERT_EQUAL(Mode::Work, t.state().mode);
    TEST_ASSERT_EQUAL_UINT32(20, t.state().durationSeconds);
    TEST_ASSERT_EQUAL_UINT32(20, t.state().remainingSeconds);
    TEST_ASSERT_EQUAL_STRING("00000000-0000-4000-8000-000000000001", t.state().sessionId);
    TEST_ASSERT_EQUAL(3, events.size());
    TEST_ASSERT_EQUAL_STRING("FACE_CHANGED:WORK", events[1].c_str());
    lastEvent("TIMER_STARTED");
}

void test_pause_face_pauses_and_same_mode_resumes_keeping_session() {
    auto t = make(shortDurations());
    t.applyFace(Face::Display);
    t.applyFace(Face::Work);
    nowMs += 7400;
    t.applyFace(Face::Pause);
    TEST_ASSERT_EQUAL(Status::Paused, t.state().status);
    TEST_ASSERT_EQUAL_UINT32(13, t.state().remainingSeconds);  // ceil(12.6)
    TEST_ASSERT_EQUAL(timer::NONE, t.state().startedAt);
    lastEvent("TIMER_PAUSED");

    nowMs += 60000;  // paused time does not count
    t.applyFace(Face::Work);
    TEST_ASSERT_EQUAL(Status::Running, t.state().status);
    TEST_ASSERT_EQUAL_STRING("00000000-0000-4000-8000-000000000001", t.state().sessionId);
    TEST_ASSERT_EQUAL_UINT32(13, t.remainingSeconds());
    lastEvent("TIMER_RESUMED");
}

void test_other_mode_face_starts_new_session() {
    auto t = make(shortDurations());
    t.applyFace(Face::Display);
    t.applyFace(Face::Work);
    t.applyFace(Face::ShortBreak);
    TEST_ASSERT_EQUAL(Mode::ShortBreak, t.state().mode);
    TEST_ASSERT_EQUAL_UINT32(10, t.state().durationSeconds);
    TEST_ASSERT_EQUAL_STRING("00000000-0000-4000-8000-000000000002", t.state().sessionId);
}

void test_pause_face_when_not_running_does_nothing() {
    auto t = make();
    t.applyFace(Face::Display);
    t.applyFace(Face::Pause);
    TEST_ASSERT_EQUAL(Status::Stopped, t.state().status);
    lastEvent("FACE_CHANGED:PAUSE");
}

void test_display_and_tech_change_nothing() {
    auto t = make(shortDurations());
    t.applyFace(Face::Pause);
    t.applyFace(Face::Work);
    t.applyFace(Face::Display);
    t.applyFace(Face::Tech);
    TEST_ASSERT_EQUAL(Status::Running, t.state().status);
    TEST_ASSERT_EQUAL_STRING("00000000-0000-4000-8000-000000000001", t.state().sessionId);
    lastEvent("FACE_CHANGED:TECH");
}

void test_same_face_again_is_ignored() {
    auto t = make();
    t.applyFace(Face::Display);
    t.applyFace(Face::Work);
    const size_t before = events.size();
    t.applyFace(Face::Work);
    TEST_ASSERT_EQUAL(before, events.size());
}

void test_configured_face_action_can_make_mode_face_display_only() {
    auto t = make(shortDurations());
    t.configureFaceAction(Face::Work, timer::FaceAction::None);
    t.applyFace(Face::Display);
    t.applyFace(Face::Work);
    TEST_ASSERT_EQUAL(Status::Stopped, t.state().status);
    lastEvent("FACE_CHANGED:WORK");
}

void test_configured_face_action_is_not_tied_to_face_name() {
    auto t = make(shortDurations());
    t.configureFaceAction(Face::Display, timer::FaceAction::StartLongBreak);
    t.applyFace(Face::Work);
    t.applyFace(Face::Display);
    TEST_ASSERT_EQUAL(Status::Running, t.state().status);
    TEST_ASSERT_EQUAL(Mode::LongBreak, t.state().mode);
}

// --- countdown ---------------------------------------------------------------

void test_finishes_at_zero_without_auto_start() {
    auto t = make(shortDurations());
    t.applyFace(Face::Display);
    t.applyFace(Face::Work);
    nowMs += 19999;
    TEST_ASSERT_FALSE(t.tick());
    TEST_ASSERT_EQUAL_UINT32(1, t.remainingSeconds());
    nowMs += 1;
    TEST_ASSERT_TRUE(t.tick());
    TEST_ASSERT_EQUAL(Status::Finished, t.state().status);
    TEST_ASSERT_EQUAL_UINT32(0, t.state().remainingSeconds);
    lastEvent("TIMER_FINISHED");
    nowMs += 60000;
    TEST_ASSERT_FALSE(t.tick());
    TEST_ASSERT_EQUAL(Status::Finished, t.state().status);
}

void test_full_cycle_work_finished_short_break() {
    auto t = make(shortDurations());
    t.applyFace(Face::Display);
    t.applyFace(Face::Work);
    nowMs += 20000;
    t.tick();
    TEST_ASSERT_EQUAL(Status::Finished, t.state().status);
    TEST_ASSERT_EQUAL(Mode::ShortBreak, timer::localNextMode(t.state().mode));
    t.applyFace(Face::ShortBreak);
    TEST_ASSERT_EQUAL(Status::Running, t.state().status);
    TEST_ASSERT_EQUAL(Mode::ShortBreak, t.state().mode);
    nowMs += 10000;
    TEST_ASSERT_TRUE(t.tick());
    TEST_ASSERT_EQUAL(Mode::Work, timer::localNextMode(t.state().mode));
}

void test_flip_to_same_mode_after_finish_starts_again() {
    auto t = make(shortDurations());
    t.applyFace(Face::Display);
    t.applyFace(Face::Work);
    nowMs += 20000;
    t.tick();
    t.applyFace(Face::Display);
    t.applyFace(Face::Work);
    TEST_ASSERT_EQUAL(Status::Running, t.state().status);
    TEST_ASSERT_EQUAL_STRING("00000000-0000-4000-8000-000000000002", t.state().sessionId);
}

void test_started_at_is_null_until_clock_synced() {
    auto t = make();
    t.applyFace(Face::Display);
    t.applyFace(Face::Work);
    TEST_ASSERT_EQUAL(timer::NONE, t.state().startedAt);
    epoch = 1761150000000LL;
    t.applyFace(Face::ShortBreak);
    TEST_ASSERT_EQUAL(1761150000000LL + nowMs, t.state().startedAt);
}

void test_server_ends_at_needs_clock_offset() {
    auto t = make(shortDurations());
    t.applyFace(Face::Display);
    t.applyFace(Face::Work);
    TEST_ASSERT_EQUAL(timer::NONE, t.serverEndsAt());
    t.setClockOffset(5000);
    TEST_ASSERT_EQUAL(nowMs + 20000 + 5000, t.serverEndsAt());
}

void test_configure_updates_idle_duration() {
    auto t = make();
    timer::Durations d;
    d.workSeconds = 50 * 60;
    t.configure(d);
    TEST_ASSERT_EQUAL_UINT32(3000, t.state().durationSeconds);
    TEST_ASSERT_EQUAL_UINT32(3000, t.state().remainingSeconds);
}

void test_configure_ignores_zero_durations() {
    auto t = make();
    timer::Durations d;
    d.workSeconds = 0;
    t.configure(d);
    TEST_ASSERT_EQUAL_UINT32(25 * 60, t.durationOf(Mode::Work));
}

// --- commands ----------------------------------------------------------------

void test_pause_command_needs_running() {
    auto t = make();
    auto r = t.applyCommand(CommandType::Pause, false, Mode::Work, nullptr);
    TEST_ASSERT_FALSE(r.executed);
    TEST_ASSERT_EQUAL_STRING("INVALID_STATE", r.errorCode);
}

void test_resume_command_needs_paused() {
    auto t = make();
    t.start(Mode::Work);
    auto r = t.applyCommand(CommandType::Resume, false, Mode::Work, nullptr);
    TEST_ASSERT_FALSE(r.executed);
    TEST_ASSERT_EQUAL_STRING("INVALID_STATE", r.errorCode);
}

void test_start_and_switch_mode_commands() {
    auto t = make(shortDurations());
    TEST_ASSERT_TRUE(t.applyCommand(CommandType::Start, true, Mode::LongBreak, nullptr).executed);
    TEST_ASSERT_EQUAL(Mode::LongBreak, t.state().mode);
    TEST_ASSERT_EQUAL(Status::Running, t.state().status);
    TEST_ASSERT_TRUE(t.applyCommand(CommandType::SwitchMode, true, Mode::Work, nullptr).executed);
    TEST_ASSERT_EQUAL(Mode::Work, t.state().mode);
    auto r = t.applyCommand(CommandType::Start, false, Mode::Work, nullptr);
    TEST_ASSERT_EQUAL_STRING("INVALID_MODE", r.errorCode);
}

void test_stop_and_release_become_idle_with_null_session() {
    auto t = make(shortDurations());
    t.start(Mode::ShortBreak);
    TEST_ASSERT_TRUE(t.applyCommand(CommandType::Stop, false, Mode::Work, nullptr).executed);
    TEST_ASSERT_EQUAL(Status::Stopped, t.state().status);
    TEST_ASSERT_EQUAL(Mode::ShortBreak, t.state().mode);
    TEST_ASSERT_EQUAL_UINT32(10, t.state().remainingSeconds);
    TEST_ASSERT_EQUAL_STRING("", t.state().sessionId);
    lastEvent("TIMER_STOPPED");

    t.start(Mode::Work);
    TEST_ASSERT_TRUE(t.applyCommand(CommandType::Release, false, Mode::Work, nullptr).executed);
    TEST_ASSERT_EQUAL(Status::Stopped, t.state().status);
    TEST_ASSERT_EQUAL_STRING("", t.state().sessionId);
}

void test_unknown_command_rejected() {
    auto t = make();
    auto r = t.applyCommand(CommandType::Unknown, false, Mode::Work, nullptr);
    TEST_ASSERT_EQUAL_STRING("INVALID_COMMAND", r.errorCode);
}

void test_command_ticks_first() {
    auto t = make(shortDurations());
    t.start(Mode::Work);
    nowMs += 20000;
    // The phase is over, so PAUSE finds FINISHED and is rejected.
    auto r = t.applyCommand(CommandType::Pause, false, Mode::Work, nullptr);
    TEST_ASSERT_EQUAL_STRING("INVALID_STATE", r.errorCode);
    TEST_ASSERT_EQUAL(Status::Finished, t.state().status);
}

// --- SET_STATE -----------------------------------------------------------------

timer::TargetState target(Status s, uint32_t duration, int64_t remaining, int64_t endsAt) {
    timer::TargetState t{};
    t.status = s;
    t.durationSeconds = duration;
    t.remainingSeconds = remaining;
    t.endsAt = endsAt;
    strcpy(t.sessionId, "c7e1aaaa-bbbb-4ccc-8ddd-eeeeffff0000");
    return t;
}

void test_set_state_running_counts_to_server_ends_at() {
    auto t = make();
    t.setClockOffset(1000000);  // server = local + 1 000 000
    auto tg = target(Status::Running, 1500, 1121, nowMs + 1000000 + 1121000);
    auto r = t.applyCommand(CommandType::SetState, true, Mode::Work, &tg);
    TEST_ASSERT_TRUE(r.executed);
    TEST_ASSERT_EQUAL(Status::Running, t.state().status);
    TEST_ASSERT_EQUAL_UINT32(1500, t.state().durationSeconds);
    TEST_ASSERT_EQUAL_STRING("c7e1aaaa-bbbb-4ccc-8ddd-eeeeffff0000", t.state().sessionId);
    TEST_ASSERT_EQUAL(nowMs + 1121000, t.state().localEndsAt);
    TEST_ASSERT_EQUAL(tg.endsAt, t.serverEndsAt());
    lastEvent("TIMER_STARTED");
}

void test_set_state_running_without_offset_uses_remaining() {
    auto t = make();
    auto tg = target(Status::Running, 1500, 600, 999999999);
    t.applyCommand(CommandType::SetState, true, Mode::Work, &tg);
    TEST_ASSERT_EQUAL(nowMs + 600000, t.state().localEndsAt);
}

void test_set_state_paused_keeps_remaining_frozen() {
    auto t = make();
    auto tg = target(Status::Paused, 300, 120, timer::NONE);
    auto r = t.applyCommand(CommandType::SetState, true, Mode::ShortBreak, &tg);
    TEST_ASSERT_TRUE(r.executed);
    TEST_ASSERT_EQUAL(Status::Paused, t.state().status);
    nowMs += 100000;
    TEST_ASSERT_EQUAL_UINT32(120, t.remainingSeconds());
    lastEvent("TIMER_PAUSED");
}

void test_set_state_rejects_invalid_targets() {
    auto t = make();
    auto bad = target(Status::Stopped, 300, 100, timer::NONE);
    TEST_ASSERT_EQUAL_STRING("INVALID_TARGET", t.applyCommand(CommandType::SetState, true, Mode::Work, &bad).errorCode);
    bad = target(Status::Running, 0, 0, timer::NONE);
    TEST_ASSERT_EQUAL_STRING("INVALID_TARGET", t.applyCommand(CommandType::SetState, true, Mode::Work, &bad).errorCode);
    bad = target(Status::Running, 300, 301, timer::NONE);
    TEST_ASSERT_EQUAL_STRING("INVALID_TARGET", t.applyCommand(CommandType::SetState, true, Mode::Work, &bad).errorCode);
    bad = target(Status::Running, 300, -1, timer::NONE);
    TEST_ASSERT_EQUAL_STRING("INVALID_TARGET", t.applyCommand(CommandType::SetState, true, Mode::Work, &bad).errorCode);
    auto ok = target(Status::Running, 300, 100, timer::NONE);
    TEST_ASSERT_EQUAL_STRING("INVALID_TARGET", t.applyCommand(CommandType::SetState, false, Mode::Work, &ok).errorCode);
    TEST_ASSERT_EQUAL_STRING("INVALID_TARGET", t.applyCommand(CommandType::SetState, true, Mode::Work, nullptr).errorCode);
    TEST_ASSERT_EQUAL(Status::Stopped, t.state().status);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_first_face_after_boot_is_baseline_only);
    RUN_TEST(test_flip_to_mode_starts_full_duration_with_new_session);
    RUN_TEST(test_pause_face_pauses_and_same_mode_resumes_keeping_session);
    RUN_TEST(test_other_mode_face_starts_new_session);
    RUN_TEST(test_pause_face_when_not_running_does_nothing);
    RUN_TEST(test_display_and_tech_change_nothing);
    RUN_TEST(test_same_face_again_is_ignored);
    RUN_TEST(test_configured_face_action_can_make_mode_face_display_only);
    RUN_TEST(test_configured_face_action_is_not_tied_to_face_name);
    RUN_TEST(test_finishes_at_zero_without_auto_start);
    RUN_TEST(test_full_cycle_work_finished_short_break);
    RUN_TEST(test_flip_to_same_mode_after_finish_starts_again);
    RUN_TEST(test_started_at_is_null_until_clock_synced);
    RUN_TEST(test_server_ends_at_needs_clock_offset);
    RUN_TEST(test_configure_updates_idle_duration);
    RUN_TEST(test_configure_ignores_zero_durations);
    RUN_TEST(test_pause_command_needs_running);
    RUN_TEST(test_resume_command_needs_paused);
    RUN_TEST(test_start_and_switch_mode_commands);
    RUN_TEST(test_stop_and_release_become_idle_with_null_session);
    RUN_TEST(test_unknown_command_rejected);
    RUN_TEST(test_command_ticks_first);
    RUN_TEST(test_set_state_running_counts_to_server_ends_at);
    RUN_TEST(test_set_state_running_without_offset_uses_remaining);
    RUN_TEST(test_set_state_paused_keeps_remaining_frozen);
    RUN_TEST(test_set_state_rejects_invalid_targets);
    return UNITY_END();
}
