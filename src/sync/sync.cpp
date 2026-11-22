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

void setStatus(ServerStatus value) {
    serverStatus = value;
}

bool unauthorized(const https::Response& response) {
    if (response.status != 401) return false;
    JsonDocument doc;
    return deserializeJson(doc, response.body) == DeserializationError::Ok &&
           strcmp(doc["errorCode"] | "", "DEVICE_UNAUTHORIZED") == 0;
}

void deliver(const Update& update) {
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    pending = update;
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

void worker(void*) {
    uint32_t nextHeartbeatMs = 0;
    uint32_t heartbeatSeconds = DEFAULT_HEARTBEAT_SECONDS;
    uint32_t commandPollSeconds = 3;
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
            wasReady = true;
        }
        const uint32_t now = millis();
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
        const https::Response response =
            https::request("POST", auth.apiBase, "/api/devices/heartbeat", body, auth.secret);
        memset(auth.secret, 0, sizeof(auth.secret));
        Update update;
        if (unauthorized(response)) {
            update.unauthorized = true;
            deliver(update);
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
        if (result["serverTime"].is<int64_t>()) {
            update.hasServerTime = true;
            update.serverTime = result["serverTime"].as<int64_t>();
        }
        update.pendingCommands = result["pendingCommands"] | 0;
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
                    update.unauthorized = true;
                    setStatus(ServerStatus::Disabled);
                } else if (config.status == 200 && config.body.length() < CONFIG_JSON_SIZE) {
                    update.hasConfig = true;
                    memcpy(update.configJson, config.body.c_str(), config.body.length() + 1);
                    JsonDocument configDoc;
                    if (deserializeJson(configDoc, config.body) == DeserializationError::Ok) {
                        JsonVariantConst serverTime = configDoc["state"]["serverTime"];
                        if (!serverTime.is<int64_t>()) serverTime = configDoc["serverTime"];
                        if (serverTime.is<int64_t>()) {
                            update.hasServerTime = true;
                            update.serverTime = serverTime.as<int64_t>();
                        }
                    }
                } else {
                    log_w("sync: config fetch failed, HTTP %d", config.status);
                }
            }
        }
        deliver(update);
        nextHeartbeatMs = millis() + heartbeatSeconds * 1000;
        vTaskDelay(pdMS_TO_TICKS(IDLE_MS));
    }
}

}  // namespace

void begin() {
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

void publish(const Snapshot& value) {
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    latest = value;
    hasSnapshot = true;
    xSemaphoreGive(sharedMutex);
}

bool takeUpdate(Update& out) {
    xSemaphoreTake(sharedMutex, portMAX_DELAY);
    const bool available = hasUpdate;
    if (available) {
        out = pending;
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
