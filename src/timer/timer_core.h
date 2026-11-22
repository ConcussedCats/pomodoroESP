#pragma once
#include <stdint.h>

#include <functional>

#include "faces/faces.h"

// Local Pomodoro timer of the cube: face rules, commands, SET_STATE / RELEASE.
// Mirrors tools/esp32-emulator/cube-state.mjs (PROTOCOL.md sections 1, 3.5, 5).
// Pure C++ (no Arduino), so it is unit tested on the host (test/test_timer_core).
namespace timer {

enum class Mode : uint8_t { Work, ShortBreak, LongBreak };
enum class Status : uint8_t { Running, Paused, Stopped, Finished };

enum class EventType : uint8_t { FaceChanged, Started, Paused, Resumed, Stopped, Finished, StateSync };

constexpr int64_t NONE = -1;  // "null" for timestamps

struct State {
    Mode mode = Mode::Work;
    Status status = Status::Stopped;
    uint32_t durationSeconds = 25 * 60;
    uint32_t remainingSeconds = 25 * 60;  // at startedAt while RUNNING, frozen while PAUSED
    int64_t startedAt = NONE;             // epoch ms, NONE until the clock is synced
    int64_t pausedAt = NONE;              // epoch ms
    char sessionId[37] = "";              // UUID v4, "" = null
    uint8_t cycleIndex = 0;               // owned by the server, echoed back
    uint8_t phaseIndex = 0;
    int64_t localEndsAt = NONE;           // monotonic ms, RUNNING only (never sent)
};

struct Durations {
    uint32_t workSeconds = 25 * 60;
    uint32_t shortBreakSeconds = 5 * 60;
    uint32_t longBreakSeconds = 15 * 60;
};

// Clock and id sources are injected: monotonic ms for counting down, epoch ms when known.
struct Env {
    std::function<int64_t()> monotonicMs;
    std::function<int64_t()> epochMs;                // NONE while the clock is not synced
    std::function<void(char out[37])> newUuid;       // UUID v4
    std::function<void(EventType, faces::Face face)> onEvent;  // face = Unknown unless FACE_CHANGED
};

struct CommandResult {
    bool executed;
    const char* errorCode;  // nullptr when executed
};

enum class CommandType : uint8_t { Start, Pause, Resume, Stop, SwitchMode, SetState, Release, Unknown };

struct TargetState {
    Status status;  // Running or Paused
    uint32_t durationSeconds;
    int64_t remainingSeconds;  // signed: invalid negative values must be rejected
    int64_t endsAt;            // server clock, NONE if absent
    char sessionId[37];
};

class TimerCore {
public:
    explicit TimerCore(Env env);

    void configure(const Durations& d);
    const Durations& durations() const { return durations_; }
    uint32_t durationOf(Mode mode) const;

    // serverTime - monotonicNow, learned from server responses (PROTOCOL.md 1.6).
    void setClockOffset(int64_t offsetMs) { clockOffset_ = offsetMs; hasClockOffset_ = true; }
    bool hasClockOffset() const { return hasClockOffset_; }

    // A new accepted face. The first face after boot is only a baseline (no timer action):
    // a cube lying on WORK when it is switched on must not start a session by itself.
    void applyFace(faces::Face face);
    faces::Face face() const { return face_; }

    CommandResult applyCommand(CommandType type, bool hasMode, Mode mode, const TargetState* target);

    // Start a mode directly (button S2 "start the suggested mode"); same as flipping to it.
    void start(Mode mode);

    // Call often: switches RUNNING -> FINISHED at zero. Returns true when it finished now.
    bool tick();

    uint32_t remainingSeconds() const;
    const State& state() const { return state_; }
    // endsAt on the server clock, NONE unless RUNNING and the clock offset is known.
    int64_t serverEndsAt() const;

    // Restore a snapshot (stage 3: NVS). Takes the state as is, emits nothing.
    void restore(const State& s) { state_ = s; }

    // After a reboot: take the saved state and tell the server with STATE_SYNC.
    void restoreAndSync(const State& s);

    // Continue a PAUSED phase up to an absolute end on the epoch clock (restore after a
    // reboot once the time is known). Already over -> FINISHED. Needs a synced clock.
    bool continueUntilEpoch(int64_t endsAtEpoch);

    // End of the running phase on the epoch clock, NONE if not running or no clock.
    int64_t epochEndsAt() const;

private:
    State fresh(Mode mode, Status status) const;
    void emit(EventType type, faces::Face face = faces::Face::Unknown);
    void pause();
    void resume();
    void stop();
    CommandResult setState(bool hasMode, Mode mode, const TargetState* target);

    Env env_;
    Durations durations_;
    State state_;
    faces::Face face_ = faces::Face::Unknown;
    bool hasBaseline_ = false;
    int64_t clockOffset_ = 0;
    bool hasClockOffset_ = false;
};

const char* name(Mode mode);
const char* name(Status status);
const char* name(EventType type);
bool modeForFace(faces::Face face, Mode& out);
// Local guess of the next phase for the FINISHED screen while the server's
// suggestedNextMode is unknown (offline): after work a short break, after a break work.
Mode localNextMode(Mode finished);

}  // namespace timer
