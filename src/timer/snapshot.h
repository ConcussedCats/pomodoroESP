#pragma once
#include <stdint.h>

#include "timer/timer_core.h"

// What the cube keeps in NVS to continue after a reboot (FIRMWARE-SPEC 3.1 key "timer").
// Pure C++, unit tested on the host.
namespace timer {

constexpr uint8_t SAVED_VERSION = 1;

struct Saved {
    uint8_t version;
    Mode mode;
    Status status;
    uint8_t cycleIndex;
    uint8_t phaseIndex;
    uint32_t durationSeconds;
    uint32_t remainingSeconds;  // remaining at the moment of saving
    int64_t endsAtEpoch;        // RUNNING with a synced clock, else NONE
    char sessionId[37];
};

// Snapshot of the timer right now. Value-initialised, so equal states give equal bytes.
Saved capture(const TimerCore& core);

bool isValid(const Saved& s);

// State to start from after a reboot. The time the cube was off is unknown here, so a phase
// that was RUNNING comes back PAUSED at the saved remaining time; continueUntilEpoch() moves
// it on once the clock is synced and the end time was saved.
State restoredState(const Saved& s, int64_t nowEpoch);

}  // namespace timer
