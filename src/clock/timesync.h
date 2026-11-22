#pragma once
#include <stddef.h>
#include <stdint.h>

// Wall clock from SNTP (FIRMWARE-SPEC 3.6). Starts once Wi-Fi is up; until the first sync
// epochMs() is NONE (-1), so events and snapshots carry occurredAt / startedAt = null.
// (Namespace is not "clock": that name is taken by the C library function.)
namespace timesync {

void begin();
void loop();  // starts SNTP when Wi-Fi is connected

bool synced();
int64_t epochMs();  // -1 until synced

// Local time for the screen ("14:05"), Europe/Kyiv. Empty string until synced.
void formatLocal(int64_t epochMs, char* out, size_t size);

}  // namespace timesync
