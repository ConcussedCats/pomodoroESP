#pragma once

#include <stdint.h>

#include "faces/faces.h"
#include "timer/timer_core.h"

namespace devicesync {

constexpr size_t CONFIG_JSON_SIZE = 3072;
constexpr size_t EVENT_REQUEST_SIZE = 4096;
constexpr size_t EVENT_RESPONSE_SIZE = 2048;
constexpr size_t COMMAND_RESPONSE_SIZE = 3072;
constexpr size_t COMMAND_ACK_SIZE = 1024;

enum class ServerStatus : uint8_t { Disabled, Offline, Contacting, Online };

struct Snapshot {
    timer::State state;
    int64_t endsAt = timer::NONE;
    faces::Face face = faces::Face::Unknown;
    bool wifiConnected = false;
    int8_t wifiRssi = 0;
    char configHash[65] = "";
};

struct Update {
    bool unauthorized = false;
    bool hasServerTime = false;
    int64_t serverTime = timer::NONE;
    uint32_t pendingCommands = 0;
    bool hasConfig = false;
    char configJson[CONFIG_JSON_SIZE] = "";
    bool hasEventResponse = false;
    char eventResponse[EVENT_RESPONSE_SIZE] = "";
    bool hasCommands = false;
    char commandsJson[COMMAND_RESPONSE_SIZE] = "";
};

// Starts the blocking HTTPS worker pinned to core 0. TimerCore remains owned by app::loop.
void begin();
// Call on the app task after pairing or clearing the device. The worker never touches NVS.
void reloadCredentials();
void publishEvents(const char* requestJson);  // nullptr / empty = queue empty
void retryEvents();  // response did not acknowledge the oldest event
void submitCommandAck(const char* commandId, const char* json);
void publish(const Snapshot& snapshot);
bool takeUpdate(Update& out);
ServerStatus status();
const char* name(ServerStatus status);

}  // namespace devicesync
