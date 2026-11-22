#include "provisioning/provisioning.h"

#include <Arduino.h>
#include <ArduinoJson.h>
#include <string.h>

#include "core/device.h"
#include "net/https.h"
#include "net/wifi.h"
#include "provisioning/ble_gatt.h"
#include "storage/storage.h"

namespace provisioning {
namespace {

constexpr uint32_t WIFI_TIMEOUT_MS = 20000;
constexpr uint32_t STOP_AFTER_PAIRED_MS = 3000;  // let the website read PAIRED, then free BLE

Phase current = Phase::Off;
ServerTimeHandler serverTimeHandler = nullptr;
uint32_t deadlineMs = 0;
uint32_t pairedAtMs = 0;
char statusText[64] = "";

// The request in progress (the password is wiped when it is done).
char ssid[33] = "";
char password[65] = "";
char token[96] = "";
char apiBase[128] = "";

void wipe() {
    memset(password, 0, sizeof(password));
    memset(token, 0, sizeof(token));
}

void setStatus(const char* state, const char* errorCode = nullptr) {
    if (errorCode) snprintf(statusText, sizeof(statusText), "%s: %s", state, errorCode);
    else snprintf(statusText, sizeof(statusText), "%s", state);
    blegatt::setStatus(state, errorCode);
}

// Failure: report it, go back to waiting for new credentials (BLE-GATT.md "Status states").
void fail(const char* state, const char* errorCode) {
    setStatus(state, errorCode);
    wipe();
    wifi::restoreSaved();
    current = Phase::Idle;
}

void deviceInfo(char* out, size_t size) {
    snprintf(out, size,
             "{\"hardwareId\":\"%s\",\"firmwareVersion\":\"%s\",\"protocolVersion\":1,\"provisioned\":%s,"
             "\"capabilities\":{\"vibration\":true,\"sound\":true,\"rgb\":true,\"display\":true,\"imu\":true}}",
             device::hardwareId(), FIRMWARE_VERSION, storage::hasSecret() ? "true" : "false");
}

bool copyField(JsonDocument& doc, const char* key, char* out, size_t size, bool allowEmpty) {
    JsonVariant v = doc[key];
    if (!v.is<const char*>()) return false;
    const char* s = v.as<const char*>();
    const size_t n = strlen(s);
    if (n >= size || (!allowEmpty && n == 0)) return false;
    memcpy(out, s, n + 1);
    return true;
}

void onPayload(const char* json) {
    JsonDocument doc;
    const bool ok = deserializeJson(doc, json) == DeserializationError::Ok && doc["v"].as<int>() == 1 &&
                    copyField(doc, "ssid", ssid, sizeof(ssid), false) &&            // <= 32 bytes
                    copyField(doc, "password", password, sizeof(password), true) &&  // <= 64, "" = open
                    copyField(doc, "pairingToken", token, sizeof(token), false) &&
                    copyField(doc, "apiBaseUrl", apiBase, sizeof(apiBase), false) && https::urlAllowed(apiBase);
    doc.clear();
    if (!ok) {
        // Not "fail()": the stored Wi-Fi was not touched.
        wipe();
        setStatus("INVALID_PAYLOAD", "INVALID_PAYLOAD");
        current = Phase::Idle;
        return;
    }
    setStatus("CREDENTIALS_RECEIVED");
    log_i("provisioning: Wi-Fi '%s', server %s", ssid, apiBase);  // no password, no token
    wifi::tryCredentials(ssid, password);
    setStatus("WIFI_CONNECTING");
    deadlineMs = millis() + WIFI_TIMEOUT_MS;
    current = Phase::WifiConnecting;
}

const char* errorCodeFor(const https::Response& r) {
    static char code[48];
    if (r.status == 0) return https::name(r.error);
    JsonDocument doc;
    if (deserializeJson(doc, r.body) == DeserializationError::Ok && doc["errorCode"].is<const char*>()) {
        snprintf(code, sizeof(code), "%s", doc["errorCode"].as<const char*>());
        return code;
    }
    switch (r.status) {
        case 400: return "PAIRING_TOKEN_INVALID";
        case 410: return "PAIRING_TOKEN_EXPIRED";
        case 409: return "PAIRING_TOKEN_USED";
        default: snprintf(code, sizeof(code), "HTTP_%d", r.status); return code;
    }
}

void pair() {
    char body[384];
    snprintf(body, sizeof(body),
             "{\"pairingToken\":\"%s\",\"hardwareId\":\"%s\",\"firmwareVersion\":\"%s\",\"protocolVersion\":1,"
             "\"name\":\"Pomodoro Cube\",\"capabilities\":{\"vibration\":true,\"sound\":true,\"rgb\":true,"
             "\"display\":true,\"imu\":true}}",
             token, device::hardwareId(), FIRMWARE_VERSION);
    const https::Response r = https::request("POST", apiBase, "/api/devices/pair", body, nullptr);
    memset(body, 0, sizeof(body));
    if (r.status != 201) {
        log_w("provisioning: pairing failed, HTTP %d", r.status);
        fail("PAIRING_FAILED", errorCodeFor(r));
        return;
    }
    JsonDocument doc;
    if (deserializeJson(doc, r.body) != DeserializationError::Ok || !doc["deviceSecret"].is<const char*>()) {
        fail("PAIRING_FAILED", "INVALID_SERVER_RESPONSE");
        return;
    }
    storage::saveDevice(doc["deviceSecret"].as<const char*>(), doc["deviceId"].as<int64_t>(), apiBase);
    wifi::persistCurrent();
    if (serverTimeHandler && doc["serverTime"].is<int64_t>()) serverTimeHandler(doc["serverTime"].as<int64_t>());
    log_i("provisioning: paired, device id %lld", (long long)doc["deviceId"].as<int64_t>());  // never the secret
    doc.clear();
    wipe();
    setStatus("PAIRED");
    pairedAtMs = millis();
    current = Phase::Paired;
}

}  // namespace

void begin(ServerTimeHandler onServerTime) {
    serverTimeHandler = onServerTime;
}

void start() {
    if (current != Phase::Off) return;
    char info[256];
    deviceInfo(info, sizeof(info));
    blegatt::start(info);
    setStatus("IDLE");
    current = Phase::Idle;
}

void cancel() {
    if (current == Phase::Off) return;
    if (current == Phase::WifiConnecting || current == Phase::Pairing) wifi::restoreSaved();
    wipe();
    blegatt::stop();
    current = Phase::Off;
}

void loop(uint32_t now) {
    if (current == Phase::Off) return;

    if (blegatt::takeNewConnection() && (current == Phase::Idle)) {
        setStatus("IDLE");  // the website reads the status right after subscribing: no stale error
    }
    if (blegatt::takeCancel() && current != Phase::Paired) {
        if (current == Phase::WifiConnecting) wifi::restoreSaved();
        wipe();
        setStatus("IDLE");
        current = Phase::Idle;
    }

    static char payload[blegatt::MAX_PAYLOAD + 1];
    if (current == Phase::Idle && blegatt::takeProvisioning(payload, sizeof(payload))) {
        onPayload(payload);
        memset(payload, 0, sizeof(payload));
    }

    switch (current) {
        case Phase::WifiConnecting:
            if (wifi::connected()) {
                setStatus("PAIRING");
                current = Phase::Pairing;
            } else if (wifi::state() == wifi::State::Waiting) {
                fail("WIFI_FAILED", wifi::name(wifi::lastFailure()));
            } else if (int32_t(now - deadlineMs) >= 0) {
                fail("WIFI_FAILED", "WIFI_TIMEOUT");
            }
            break;
        case Phase::Pairing:
            pair();  // blocking HTTPS request, up to ~10 s
            break;
        case Phase::Paired:
            if (now - pairedAtMs >= STOP_AFTER_PAIRED_MS) {
                blegatt::stop();
                current = Phase::Off;
            }
            break;
        default:
            break;
    }
}

Phase phase() {
    return current;
}

bool active() {
    return current != Phase::Off;
}

const char* status() {
    return statusText;
}

}  // namespace provisioning
