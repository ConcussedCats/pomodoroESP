#pragma once

#include <stddef.h>
#include <stdint.h>

#include "timer/timer_core.h"

namespace syncconfig {

struct DeviceConfig {
    char hash[65] = "";
    bool hasTimer = false;
    timer::Durations durations;
    bool hasHardware = false;
    bool vibrationEnabled = true;
    bool soundEnabled = true;
    uint8_t rgbBrightness = 60;
    bool hasFaces = false;
    timer::FaceAction faceActions[6] = {timer::FaceAction::StartWork, timer::FaceAction::StartShortBreak,
                                        timer::FaceAction::StartLongBreak, timer::FaceAction::Pause,
                                        timer::FaceAction::None, timer::FaceAction::None};
    bool hasState = false;
    timer::State state;
    int64_t stateEndsAt = timer::NONE;
    int64_t serverTime = timer::NONE;
};

bool parse(const char* json, DeviceConfig& out, char* error, size_t errorSize);

}  // namespace syncconfig
