#include "sync/sync.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <esp_system.h>
#include <string.h>

#include "core/device.h"
#include "net/https.h"
#include "storage/storage.h"
#include "sync/protocol.h"

namespace devicesync {
namespace {

constexpr uint32_t DEFAULT_HEARTBEAT_SECONDS = 30;
constexpr uint32_t RETRY_MS = 5000;
constexpr uint32_t IDLE_MS = 200;

SemaphoreHandle_t sharedMutex = nullptr;
Snapshot latest;
bool hasSnapshot = false;
Update pending;
bool hasUpdate = false;
volatile ServerStatus serverStatus = ServerStatus::Disabled;

struct Credentials {
    bool valid = false;
    char secret[64] = "";
    char apiBase[128] = "";
};
Credentials credentials;

struct EventRequest {
    uint32_t generation = 0;
    bool available = false;
    char json[EVENT_REQUEST_SIZE] = "";
};
EventRequest eventRequest;

struct AckRequest {
    uint32_t generation = 0;
    bool available = false;
    char commandId[37] = "";
    char json[COMMAND_ACK_SIZE] = "";
};
AckRequest ackRequest;

void setStatus(ServerStatus value) {
    serverStatus = value;
}

bool unauthorized(const https::Response& response) {
    if (response.status != 401) return false;
    JsonDocument doc;
    return deserializeJson(doc, response.body) == DeserializationError::Ok &&
           strcmp(doc["errorCode"] | "", "DEVICE_UNAUTHORIZED") == 0;
}

void signalUnauthorized() {
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    pending.unauthorized = true;
    hasUpdate = true;
    xSemaphoreGive(sharedMutex);
}

void deliverMetadata(bool hasServerTime, int64_t serverTime, uint32_t pendingCommands) {
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    if (hasServerTime) {
        pending.hasServerTime = true;
        pending.serverTime = serverTime;
    }
    if (pendingCommands) pending.pendingCommands = pendingCommands;
    hasUpdate = true;
    xSemaphoreGive(sharedMutex);
}

void deliverText(bool& flag, char* destination, size_t size, const String& value) {
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    flag = true;
    memcpy(destination, value.c_str(), value.length() + 1 < size ? value.length() + 1 : size);
    destination[size - 1] = '\0';
    hasUpdate = true;
    xSemaphoreGive(sharedMutex);
}

bool snapshot(Snapshot& out) {
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    const bool available = hasSnapshot;
    if (available) out = latest;
    xSemaphoreGive(sharedMutex);
    return available;
}

Credentials currentCredentials() {
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    const Credentials copy = credentials;
    xSemaphoreGive(sharedMutex);
    return copy;
}

EventRequest currentEvents() {
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    const EventRequest copy = eventRequest;
    xSemaphoreGive(sharedMutex);
    return copy;
}

AckRequest currentAck() {
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    const AckRequest copy = ackRequest;
    xSemaphoreGive(sharedMutex);
    return copy;
}

void worker(void*) {
    uint32_t nextHeartbeatMs = 0;
    uint32_t heartbeatSeconds = DEFAULT_HEARTBEAT_SECONDS;
    uint32_t commandPollSeconds = 3;
    uint32_t sentEventGeneration = 0;
    uint32_t nextEventRetryMs = 0;
    uint32_t nextCommandPollMs = 0;
    uint32_t sentAckGeneration = 0;
    uint32_t nextAckRetryMs = 0;
    bool heartbeatCompleted = false;
    bool wasReady = false;
    for (;;) {
        Snapshot state;
        if (!snapshot(state)) {
            vTaskDelay(pdMS_TO_TICKS(IDLE_MS));
            continue;
        }
        Credentials auth = currentCredentials();
        const bool paired = auth.valid;
        const bool ready = paired && state.wifiConnected;
        if (!ready) {
            setStatus(paired ? ServerStatus::Offline : ServerStatus::Disabled);
            wasReady = false;
            vTaskDelay(pdMS_TO_TICKS(IDLE_MS));
            continue;
        }
        if (!wasReady) {
            nextHeartbeatMs = 0;  // heartbeat immediately after reconnect
            sentEventGeneration = 0;
            nextEventRetryMs = 0;
            nextCommandPollMs = 0;
            sentAckGeneration = 0;
            nextAckRetryMs = 0;
            heartbeatCompleted = false;
            wasReady = true;
        }
        const uint32_t now = millis();

        EventRequest queued = currentEvents();
        if (queued.available && queued.generation != sentEventGeneration &&
            (nextEventRetryMs == 0 || int32_t(now - nextEventRetryMs) >= 0)) {
            setStatus(ServerStatus::Contacting);
            const https::Response response =
                https::request("POST", auth.apiBase, "/api/device/events", queued.json, auth.secret);
            if (unauthorized(response)) {
                signalUnauthorized();
                setStatus(ServerStatus::Disabled);
                wasReady = false;
                continue;
            }
            if (response.status == 200 && response.body.length() < EVENT_RESPONSE_SIZE) {
                deliverText(pending.hasEventResponse, pending.eventResponse, sizeof(pending.eventResponse), response.body);
                JsonDocument responseDoc;
                if (deserializeJson(responseDoc, response.body) == DeserializationError::Ok &&
                    responseDoc["state"]["serverTime"].is<int64_t>()) {
                    deliverMetadata(true, responseDoc["state"]["serverTime"].as<int64_t>(), 0);
                }
                sentEventGeneration = queued.generation;
                nextEventRetryMs = 0;
                setStatus(ServerStatus::Online);
                continue;  // reconnect order: events first, heartbeat on the next pass
            }
            log_w("sync: event upload failed, HTTP %d", response.status);
            nextEventRetryMs = now + RETRY_MS;
            if (response.status == 0) setStatus(ServerStatus::Offline);
        }

        AckRequest ack = currentAck();
        if (heartbeatCompleted && ack.available && ack.generation != sentAckGeneration &&
            (nextAckRetryMs == 0 || int32_t(now - nextAckRetryMs) >= 0)) {
            char path[96];
            snprintf(path, sizeof(path), "/api/device/commands/%s/ack", ack.commandId);
            setStatus(ServerStatus::Contacting);
            const https::Response response = https::request("POST", auth.apiBase, path, ack.json, auth.secret);
            if (unauthorized(response)) {
                signalUnauthorized();
                setStatus(ServerStatus::Disabled);
                wasReady = false;
                continue;
            }
            if (response.status == 200 || response.status == 410) {
                sentAckGeneration = ack.generation;
                nextAckRetryMs = 0;
                nextCommandPollMs = 0;
                setStatus(ServerStatus::Online);
                continue;
            }
            log_w("sync: command ack failed, HTTP %d", response.status);
            nextAckRetryMs = now + RETRY_MS;
            if (response.status == 0) setStatus(ServerStatus::Offline);
        }

        if (heartbeatCompleted && int32_t(now - nextCommandPollMs) >= 0) {
            Credentials commandAuth = currentCredentials();
            if (commandAuth.valid) {
                const https::Response commands =
                    https::request("GET", commandAuth.apiBase, "/api/device/commands", nullptr, commandAuth.secret);
                memset(commandAuth.secret, 0, sizeof(commandAuth.secret));
                if (unauthorized(commands)) {
                    signalUnauthorized();
                    setStatus(ServerStatus::Disabled);
                    wasReady = false;
                    continue;
                }
                if (commands.status == 200 && commands.body.length() < COMMAND_RESPONSE_SIZE) {
                    deliverText(pending.hasCommands, pending.commandsJson, sizeof(pending.commandsJson), commands.body);
                    setStatus(ServerStatus::Online);
                } else {
                    log_w("sync: command poll failed, HTTP %d", commands.status);
                    if (commands.status == 0) setStatus(ServerStatus::Offline);
                }
            }
            nextCommandPollMs = millis() + commandPollSeconds * 1000;
        }

        if (nextHeartbeatMs != 0 && int32_t(now - nextHeartbeatMs) < 0) {
            vTaskDelay(pdMS_TO_TICKS(IDLE_MS));
            continue;
        }

        syncproto::Heartbeat heartbeat{FIRMWARE_VERSION,
                                       state.wifiRssi,
                                       uint32_t(device::monotonicMs() / 1000),
                                       esp_get_free_heap_size(),
                                       state.face,
                                       state.configHash,
                                       state.state,
                                       state.endsAt};
        char body[1024];
        if (!syncproto::buildHeartbeatJson(heartbeat, body, sizeof(body))) {
            log_e("sync: heartbeat JSON did not fit");
            nextHeartbeatMs = now + RETRY_MS;
            continue;
        }

        setStatus(ServerStatus::Contacting);
#ifdef TELOS_DEBUG
        log_i("sync: heartbeat with %u free heap bytes, %u stack words free", esp_get_free_heap_size(),
              uxTaskGetStackHighWaterMark(nullptr));
#endif
        const https::Response response =
            https::request("POST", auth.apiBase, "/api/devices/heartbeat", body, auth.secret);
        memset(auth.secret, 0, sizeof(auth.secret));
        if (unauthorized(response)) {
            signalUnauthorized();
            setStatus(ServerStatus::Disabled);
            wasReady = false;
            vTaskDelay(pdMS_TO_TICKS(IDLE_MS));
            continue;
        }
        if (response.status != 200) {
            setStatus(ServerStatus::Offline);
            nextHeartbeatMs = now + RETRY_MS;
            vTaskDelay(pdMS_TO_TICKS(IDLE_MS));
            continue;
        }

        JsonDocument result;
        if (deserializeJson(result, response.body) != DeserializationError::Ok) {
            log_w("sync: invalid heartbeat response");
            setStatus(ServerStatus::Offline);
            nextHeartbeatMs = now + RETRY_MS;
            continue;
        }
        setStatus(ServerStatus::Online);
        bool hasServerTime = false;
        int64_t serverTime = timer::NONE;
        if (result["serverTime"].is<int64_t>()) {
            hasServerTime = true;
            serverTime = result["serverTime"].as<int64_t>();
        }
        const uint32_t pendingCommands = result["pendingCommands"] | 0;
        const uint32_t interval = result["heartbeatIntervalSeconds"] | heartbeatSeconds;
        if (interval > 0 && interval <= 3600) heartbeatSeconds = interval;
        const uint32_t commandInterval = result["commandPollIntervalSeconds"] | commandPollSeconds;
        if (commandInterval > 0 && commandInterval <= 3600) commandPollSeconds = commandInterval;

        const char* remoteHash = result["configHash"] | "";
        if (remoteHash[0] && strcmp(remoteHash, state.configHash) != 0) {
            Credentials configAuth = currentCredentials();
            if (configAuth.valid) {
                const https::Response config =
                    https::request("GET", configAuth.apiBase, "/api/device/config", nullptr, configAuth.secret);
                memset(configAuth.secret, 0, sizeof(configAuth.secret));
                if (unauthorized(config)) {
                    signalUnauthorized();
                    setStatus(ServerStatus::Disabled);
                } else if (config.status == 200 && config.body.length() < CONFIG_JSON_SIZE) {
                    deliverText(pending.hasConfig, pending.configJson, sizeof(pending.configJson), config.body);
                    JsonDocument configDoc;
                    if (deserializeJson(configDoc, config.body) == DeserializationError::Ok) {
                        JsonVariantConst configServerTime = configDoc["state"]["serverTime"];
                        if (!configServerTime.is<int64_t>()) configServerTime = configDoc["serverTime"];
                        if (configServerTime.is<int64_t>()) {
                            hasServerTime = true;
                            serverTime = configServerTime.as<int64_t>();
                        }
                    }
                } else {
                    log_w("sync: config fetch failed, HTTP %d", config.status);
                }
            }
        }
        deliverMetadata(hasServerTime, serverTime, pendingCommands);
        heartbeatCompleted = true;
        if (pendingCommands > 0) nextCommandPollMs = 0;
        nextHeartbeatMs = millis() + heartbeatSeconds * 1000;
        vTaskDelay(pdMS_TO_TICKS(IDLE_MS));
    }
}

}  // namespace

void begin() {
    if (sharedMutex) return;
    sharedMutex = xSemaphoreCreateMutex();
    reloadCredentials();
    xTaskCreatePinnedToCore(worker, "telos-sync", 12288, nullptr, 1, nullptr, 0);
}

void reloadCredentials() {
    Credentials next;
    int64_t deviceId = 0;
    next.valid = storage::loadDevice(next.secret, deviceId, next.apiBase);
    if (!sharedMutex) return;
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    memset(credentials.secret, 0, sizeof(credentials.secret));
    credentials = next;
    xSemaphoreGive(sharedMutex);
    memset(next.secret, 0, sizeof(next.secret));
}

void publishEvents(const char* json) {
    if (!sharedMutex) return;
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    const bool available = json && json[0];
    if (available != eventRequest.available ||
        (available && strncmp(eventRequest.json, json, sizeof(eventRequest.json)) != 0)) {
        eventRequest.available = available;
        if (available) snprintf(eventRequest.json, sizeof(eventRequest.json), "%s", json);
        else eventRequest.json[0] = '\0';
        eventRequest.generation++;
        if (eventRequest.generation == 0) eventRequest.generation = 1;
    }
    xSemaphoreGive(sharedMutex);
}

void retryEvents() {
    if (!sharedMutex) return;
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    if (eventRequest.available) {
        eventRequest.generation++;
        if (eventRequest.generation == 0) eventRequest.generation = 1;
    }
    xSemaphoreGive(sharedMutex);
}

void submitCommandAck(const char* commandId, const char* json) {
    if (!sharedMutex || !commandId || !commandId[0] || !json || !json[0]) return;
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    snprintf(ackRequest.commandId, sizeof(ackRequest.commandId), "%s", commandId);
    snprintf(ackRequest.json, sizeof(ackRequest.json), "%s", json);
    ackRequest.available = true;
    ackRequest.generation++;
    if (ackRequest.generation == 0) ackRequest.generation = 1;
    xSemaphoreGive(sharedMutex);
}

void publish(const Snapshot& value) {
    if (!sharedMutex) return;
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    latest = value;
    hasSnapshot = true;
    xSemaphoreGive(sharedMutex);
}

bool takeUpdate(Update& out) {
    if (!sharedMutex) return false;
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    const bool available = hasUpdate;
    if (available) {
        out = pending;
        pending = Update{};
        hasUpdate = false;
    }
    xSemaphoreGive(sharedMutex);
    return available;
}

ServerStatus status() {
    return serverStatus;
}

const char* name(ServerStatus value) {
    switch (value) {
        case ServerStatus::Disabled: return "disabled";
        case ServerStatus::Offline: return "offline";
        case ServerStatus::Contacting: return "contacting";
        case ServerStatus::Online: return "online";
    }
    return "offline";
}

}  // namespace devicesync
