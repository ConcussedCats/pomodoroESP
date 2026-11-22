#include "timer/timer_core.h"

#include <string.h>

namespace timer {
namespace {

constexpr CommandResult EXECUTED{true, nullptr};

CommandResult rejected(const char* code) {
    return {false, code};
}

bool isMode(faces::Face f) {
    return f == faces::Face::Work || f == faces::Face::ShortBreak || f == faces::Face::LongBreak;
}

}  // namespace

TimerCore::TimerCore(Env env) : env_(std::move(env)) {
    state_ = fresh(Mode::Work, Status::Stopped);
}

void TimerCore::configure(const Durations& d) {
    durations_.workSeconds = d.workSeconds > 0 ? d.workSeconds : 25 * 60;
    durations_.shortBreakSeconds = d.shortBreakSeconds > 0 ? d.shortBreakSeconds : 5 * 60;
    durations_.longBreakSeconds = d.longBreakSeconds > 0 ? d.longBreakSeconds : 15 * 60;
    // An idle timer shows the new full duration right away.
    if (state_.status == Status::Stopped) state_ = fresh(state_.mode, Status::Stopped);
}

void TimerCore::configureFaceAction(faces::Face face, FaceAction action) {
    const uint8_t i = static_cast<uint8_t>(face);
    if (i < 6) faceActions_[i] = action;
}

FaceAction TimerCore::faceAction(faces::Face face) const {
    const uint8_t i = static_cast<uint8_t>(face);
    return i < 6 ? faceActions_[i] : FaceAction::None;
}

uint32_t TimerCore::durationOf(Mode mode) const {
    switch (mode) {
        case Mode::Work: return durations_.workSeconds;
        case Mode::ShortBreak: return durations_.shortBreakSeconds;
        case Mode::LongBreak: return durations_.longBreakSeconds;
    }
    return durations_.workSeconds;
}

State TimerCore::fresh(Mode mode, Status status) const {
    State s;
    s.mode = mode;
    s.status = status;
    s.durationSeconds = durationOf(mode);
    s.remainingSeconds = s.durationSeconds;
    return s;
}

void TimerCore::emit(EventType type, faces::Face face) {
    if (env_.onEvent) env_.onEvent(type, face);
}

uint32_t TimerCore::remainingSeconds() const {
    if (state_.status != Status::Running) return state_.remainingSeconds;
    const int64_t left = state_.localEndsAt - env_.monotonicMs();
    if (left <= 0) return 0;
    return uint32_t((left + 999) / 1000);  // ceil, like the emulator
}

int64_t TimerCore::serverEndsAt() const {
    if (state_.status != Status::Running || state_.localEndsAt == NONE || !hasClockOffset_) return NONE;
    return state_.localEndsAt + clockOffset_;
}

void TimerCore::restoreAndSync(const State& s) {
    state_ = s;
    state_.localEndsAt = NONE;
    emit(EventType::StateSync);
}

int64_t TimerCore::epochEndsAt() const {
    if (state_.status != Status::Running || state_.localEndsAt == NONE) return NONE;
    const int64_t epoch = env_.epochMs();
    if (epoch == NONE) return NONE;
    return epoch + (state_.localEndsAt - env_.monotonicMs());
}

bool TimerCore::continueUntilEpoch(int64_t endsAtEpoch) {
    const int64_t epoch = env_.epochMs();
    if (state_.status != Status::Paused || endsAtEpoch == NONE || epoch == NONE) return false;
    const int64_t left = endsAtEpoch - epoch;
    if (left <= 0) {
        state_.remainingSeconds = 0;
        state_.status = Status::Finished;
        state_.startedAt = NONE;
        state_.pausedAt = NONE;
        state_.localEndsAt = NONE;
        emit(EventType::Finished);
        return true;
    }
    state_.status = Status::Running;
    state_.remainingSeconds = uint32_t((left + 999) / 1000);
    state_.startedAt = epoch;
    state_.pausedAt = NONE;
    state_.localEndsAt = env_.monotonicMs() + left;
    emit(EventType::Resumed);
    return true;
}

void TimerCore::start(Mode mode) {
    state_ = fresh(mode, Status::Running);
    const int64_t now = env_.monotonicMs();
    state_.startedAt = env_.epochMs();
    state_.localEndsAt = now + int64_t(state_.remainingSeconds) * 1000;
    env_.newUuid(state_.sessionId);
    emit(EventType::Started);
}

void TimerCore::pause() {
    state_.remainingSeconds = remainingSeconds();
    state_.status = Status::Paused;
    state_.pausedAt = env_.epochMs();
    state_.startedAt = NONE;
    state_.localEndsAt = NONE;
    emit(EventType::Paused);
}

void TimerCore::resume() {
    state_.status = Status::Running;
    state_.startedAt = env_.epochMs();
    state_.localEndsAt = env_.monotonicMs() + int64_t(state_.remainingSeconds) * 1000;
    state_.pausedAt = NONE;
    emit(EventType::Resumed);
}

void TimerCore::stop() {
    state_ = fresh(state_.mode, Status::Stopped);
    emit(EventType::Stopped);
}

bool TimerCore::tick() {
    if (state_.status != Status::Running || remainingSeconds() != 0) return false;
    state_.remainingSeconds = 0;
    state_.status = Status::Finished;
    state_.startedAt = NONE;
    state_.pausedAt = NONE;
    state_.localEndsAt = NONE;
    emit(EventType::Finished);
    return true;
}

void TimerCore::applyFace(faces::Face f) {
    if (f == faces::Face::Unknown) return;
    const bool changed = f != face_;
    face_ = f;
    if (!hasBaseline_) {
        hasBaseline_ = true;
        emit(EventType::FaceChanged, f);
        return;
    }
    if (!changed) return;
    tick();  // a phase that ended a moment ago is FINISHED, not RUNNING
    emit(EventType::FaceChanged, f);

    const FaceAction action = faceAction(f);
    Mode mode = Mode::Work;
    const bool startsMode = action == FaceAction::StartWork || action == FaceAction::StartShortBreak ||
                            action == FaceAction::StartLongBreak;
    if (action == FaceAction::StartShortBreak) mode = Mode::ShortBreak;
    else if (action == FaceAction::StartLongBreak) mode = Mode::LongBreak;
    if (startsMode) {
        const bool sameMode = state_.mode == mode;
        if (sameMode && state_.status == Status::Paused) {
            resume();
        } else if (!(sameMode && state_.status == Status::Running)) {
            start(mode);
        }
        return;
    }
    if (action == FaceAction::Pause && state_.status == Status::Running) pause();
}

CommandResult TimerCore::applyCommand(CommandType type, bool hasMode, Mode mode, const TargetState* target) {
    tick();
    switch (type) {
        case CommandType::Start:
        case CommandType::SwitchMode:
            if (!hasMode) return rejected("INVALID_MODE");
            start(mode);
            return EXECUTED;
        case CommandType::Pause:
            if (state_.status != Status::Running) return rejected("INVALID_STATE");
            pause();
            return EXECUTED;
        case CommandType::Resume:
            if (state_.status != Status::Paused) return rejected("INVALID_STATE");
            resume();
            return EXECUTED;
        case CommandType::Stop:
        case CommandType::Release:
            // RELEASE: the browser continues the phase, the cube becomes idle.
            stop();
            return EXECUTED;
        case CommandType::SetState:
            return setState(hasMode, mode, target);
        case CommandType::Unknown:
            break;
    }
    return rejected("INVALID_COMMAND");
}

CommandResult TimerCore::setState(bool hasMode, Mode mode, const TargetState* t) {
    if (!hasMode || t == nullptr) return rejected("INVALID_TARGET");
    if (t->status != Status::Running && t->status != Status::Paused) return rejected("INVALID_TARGET");
    if (t->durationSeconds == 0) return rejected("INVALID_TARGET");
    if (t->remainingSeconds < 0 || t->remainingSeconds > int64_t(t->durationSeconds)) return rejected("INVALID_TARGET");

    state_ = fresh(mode, t->status);
    state_.durationSeconds = t->durationSeconds;
    state_.remainingSeconds = uint32_t(t->remainingSeconds);
    strncpy(state_.sessionId, t->sessionId, sizeof(state_.sessionId) - 1);
    state_.sessionId[sizeof(state_.sessionId) - 1] = '\0';

    const int64_t now = env_.monotonicMs();
    if (t->status == Status::Running) {
        const bool knowsServerClock = t->endsAt != NONE && hasClockOffset_;
        state_.startedAt = env_.epochMs();
        state_.localEndsAt = knowsServerClock ? t->endsAt - clockOffset_ : now + t->remainingSeconds * 1000;
        emit(EventType::Started);
    } else {
        state_.pausedAt = env_.epochMs();
        emit(EventType::Paused);
    }
    return EXECUTED;
}

const char* name(Mode m) {
    switch (m) {
        case Mode::Work: return "WORK";
        case Mode::ShortBreak: return "SHORT_BREAK";
        case Mode::LongBreak: return "LONG_BREAK";
    }
    return "WORK";
}

const char* name(Status s) {
    switch (s) {
        case Status::Running: return "RUNNING";
        case Status::Paused: return "PAUSED";
        case Status::Stopped: return "STOPPED";
        case Status::Finished: return "FINISHED";
    }
    return "STOPPED";
}

const char* name(EventType t) {
    switch (t) {
        case EventType::FaceChanged: return "FACE_CHANGED";
        case EventType::Started: return "TIMER_STARTED";
        case EventType::Paused: return "TIMER_PAUSED";
        case EventType::Resumed: return "TIMER_RESUMED";
        case EventType::Stopped: return "TIMER_STOPPED";
        case EventType::Finished: return "TIMER_FINISHED";
        case EventType::StateSync: return "STATE_SYNC";
    }
    return "STATE_SYNC";
}

const char* name(FaceAction action) {
    switch (action) {
        case FaceAction::StartWork: return "START_WORK";
        case FaceAction::StartShortBreak: return "START_SHORT_BREAK";
        case FaceAction::StartLongBreak: return "START_LONG_BREAK";
        case FaceAction::Pause: return "PAUSE";
        case FaceAction::None: return "NONE";
    }
    return "NONE";
}

bool modeForFace(faces::Face f, Mode& out) {
    if (!isMode(f)) return false;
    out = f == faces::Face::Work ? Mode::Work : (f == faces::Face::ShortBreak ? Mode::ShortBreak : Mode::LongBreak);
    return true;
}

Mode localNextMode(Mode finished) {
    return finished == Mode::Work ? Mode::ShortBreak : Mode::Work;
}

}  // namespace timer
