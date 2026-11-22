#include "net/wifi.h"

#include <Arduino.h>
#include <WiFi.h>
#include <string.h>

#include "core/device.h"
#include "storage/storage.h"

namespace wifi {
namespace {

constexpr uint32_t ATTEMPT_TIMEOUT_MS = 15000;
constexpr uint32_t BACKOFF_FIRST_MS = 1000;
constexpr uint32_t BACKOFF_MAX_MS = 60000;

char storedSsid[33] = "";
char storedPass[65] = "";
State current = State::Off;
Failure failure = Failure::None;
uint32_t attemptStartMs = 0;
uint32_t retryAtMs = 0;
uint32_t backoffMs = BACKOFF_FIRST_MS;

// Written from the Wi-Fi event task.
volatile uint8_t disconnectReason = 0;
volatile bool disconnected = false;

Failure failureFor(uint8_t reason) {
    switch (reason) {
        case WIFI_REASON_AUTH_FAIL:
        case WIFI_REASON_4WAY_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_HANDSHAKE_TIMEOUT:
        case WIFI_REASON_AUTH_EXPIRE:
            return Failure::AuthFailed;
        case WIFI_REASON_NO_AP_FOUND:
            return Failure::NotFound;
        default:
            return Failure::Timeout;
    }
}

void startAttempt(uint32_t now) {
    disconnected = false;
    disconnectReason = 0;
    current = State::Connecting;
    attemptStartMs = now;
    log_i("Wi-Fi: connecting to '%s'", storedSsid);  // never the password
    WiFi.begin(storedSsid, storedPass);
}

void scheduleRetry(uint32_t now, Failure why) {
    failure = why;
    WiFi.disconnect(false, false);
    current = State::Waiting;
    retryAtMs = now + backoffMs;
    log_w("Wi-Fi: %s, next try in %lu s", name(why), (unsigned long)(backoffMs / 1000));
    backoffMs = backoffMs * 2 > BACKOFF_MAX_MS ? BACKOFF_MAX_MS : backoffMs * 2;
}

}  // namespace

void begin() {
    WiFi.persistent(false);  // credentials live in our NVS namespace only
    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);
    char host[24];
    snprintf(host, sizeof(host), "telos-cube-%s", device::hardwareId() + 8);
    WiFi.setHostname(host);
    WiFi.onEvent(
        [](WiFiEvent_t, WiFiEventInfo_t info) {
            // ASSOC_LEAVE is our own WiFi.disconnect() before a new attempt, not a failure.
            if (info.wifi_sta_disconnected.reason == WIFI_REASON_ASSOC_LEAVE) return;
            disconnectReason = info.wifi_sta_disconnected.reason;
            disconnected = true;
        },
        ARDUINO_EVENT_WIFI_STA_DISCONNECTED);

    if (storage::loadWifi(storedSsid, storedPass)) {
        startAttempt(millis());
    } else {
        current = State::Off;
        log_i("Wi-Fi: no credentials stored, offline");
    }
}

void loop(uint32_t now) {
    switch (current) {
        case State::Off:
            break;
        case State::Connecting:
            if (WiFi.status() == WL_CONNECTED) {
                current = State::Connected;
                failure = Failure::None;
                backoffMs = BACKOFF_FIRST_MS;
                log_i("Wi-Fi: connected to '%s', %d dBm, IP %s", storedSsid, WiFi.RSSI(),
                      WiFi.localIP().toString().c_str());
            } else if (disconnected) {
                scheduleRetry(now, failureFor(disconnectReason));
            } else if (now - attemptStartMs >= ATTEMPT_TIMEOUT_MS) {
                scheduleRetry(now, Failure::Timeout);
            }
            break;
        case State::Connected:
            if (WiFi.status() != WL_CONNECTED) {
                backoffMs = BACKOFF_FIRST_MS;
                scheduleRetry(now, failureFor(disconnectReason));
            }
            break;
        case State::Waiting:
            if (int32_t(now - retryAtMs) >= 0) startAttempt(now);
            break;
    }
}

void setCredentials(const char* ssid, const char* password) {
    strncpy(storedSsid, ssid, sizeof(storedSsid) - 1);
    storedSsid[sizeof(storedSsid) - 1] = '\0';
    strncpy(storedPass, password, sizeof(storedPass) - 1);
    storedPass[sizeof(storedPass) - 1] = '\0';
    storage::saveWifi(storedSsid, storedPass);
    WiFi.disconnect(false, false);
    backoffMs = BACKOFF_FIRST_MS;
    failure = Failure::None;
    startAttempt(millis());
}

void forget() {
    storage::clearWifi();
    memset(storedPass, 0, sizeof(storedPass));
    storedSsid[0] = '\0';
    WiFi.disconnect(false, false);
    current = State::Off;
    failure = Failure::None;
}

State state() {
    return current;
}

Failure lastFailure() {
    return failure;
}

const char* ssid() {
    return storedSsid;
}

int8_t rssi() {
    return current == State::Connected ? int8_t(WiFi.RSSI()) : 0;
}

bool connected() {
    return current == State::Connected;
}

const char* name(State s) {
    switch (s) {
        case State::Off: return "off";
        case State::Connecting: return "connecting";
        case State::Connected: return "connected";
        case State::Waiting: return "waiting";
    }
    return "?";
}

const char* name(Failure f) {
    switch (f) {
        case Failure::None: return "-";
        case Failure::AuthFailed: return "WIFI_AUTH_FAILED";
        case Failure::NotFound: return "WIFI_NOT_FOUND";
        case Failure::Timeout: return "WIFI_TIMEOUT";
    }
    return "-";
}

}  // namespace wifi
