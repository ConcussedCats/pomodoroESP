#include "timer/snapshot.h"

#include <string.h>

namespace timer {

Saved capture(const TimerCore& core) {
    Saved s{};
    const State& st = core.state();
    s.version = SAVED_VERSION;
    s.mode = st.mode;
    s.status = st.status;
    s.cycleIndex = st.cycleIndex;
    s.phaseIndex = st.phaseIndex;
    s.durationSeconds = st.durationSeconds;
    s.remainingSeconds = core.remainingSeconds();
    s.endsAtEpoch = core.epochEndsAt();
    memcpy(s.sessionId, st.sessionId, sizeof(s.sessionId));
    s.sessionId[sizeof(s.sessionId) - 1] = '\0';
    return s;
}

bool isValid(const Saved& s) {
    if (s.version != SAVED_VERSION) return false;
    if (s.mode != Mode::Work && s.mode != Mode::ShortBreak && s.mode != Mode::LongBreak) return false;
    if (s.status != Status::Running && s.status != Status::Paused && s.status != Status::Stopped &&
        s.status != Status::Finished)
        return false;
    if (s.durationSeconds == 0 || s.remainingSeconds > s.durationSeconds) return false;
    return memchr(s.sessionId, '\0', sizeof(s.sessionId)) != nullptr;
}

State restoredState(const Saved& s, int64_t nowEpoch) {
    State st;
    st.mode = s.mode;
    st.status = s.status == Status::Running ? Status::Paused : s.status;
    st.durationSeconds = s.durationSeconds;
    st.remainingSeconds = s.remainingSeconds;
    st.cycleIndex = s.cycleIndex;
    st.phaseIndex = s.phaseIndex;
    st.startedAt = NONE;
    st.pausedAt = st.status == Status::Paused ? nowEpoch : NONE;
    st.localEndsAt = NONE;
    memcpy(st.sessionId, s.sessionId, sizeof(st.sessionId));
    return st;
}

}  // namespace timer
