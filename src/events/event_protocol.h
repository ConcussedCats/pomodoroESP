#pragma once

#include <stddef.h>
#include <stdint.h>

#include "events/event_queue.h"

namespace events {

constexpr uint8_t MAX_BATCH = 8;

struct Batch {
    uint8_t count = 0;
    char eventIds[MAX_BATCH][37] = {};
};

// Builds one oldest-first batch. A batch never mixes bootIds because bootId is request-level.
bool buildRequest(const Queue& queue, char* out, size_t size, Batch& batch);

// Number of leading requested events that received a terminal result and may be removed.
uint8_t terminalPrefix(const char* responseJson, const Batch& batch);

}  // namespace events
