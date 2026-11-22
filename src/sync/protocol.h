#pragma once

#include <stddef.h>
#include <stdint.h>

#include "faces/faces.h"
#include "timer/timer_core.h"

namespace syncproto {

struct Heartbeat {
    const char* firmwareVersion;
    int8_t wifiRssi;
    uint32_t uptimeSeconds;
    uint32_t freeHeapBytes;
    faces::Face face;
    const char* configHash;  // empty = JSON null
    timer::State state;
    int64_t endsAt;
};

bool buildHeartbeatJson(const Heartbeat& heartbeat, char* out, size_t size);

}  // namespace syncproto
