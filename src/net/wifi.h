#pragma once
#include <stdint.h>

// Wi-Fi station with stored credentials (FIRMWARE-SPEC 3.5): reconnects by itself with a
// growing pause 1 -> 2 -> 4 ... -> 60 s. The password is never logged.
namespace wifi {

enum class State : uint8_t {
    Off,         // no credentials stored
    Connecting,  // trying now
    Connected,
    Waiting,     // failed, next attempt after the backoff
};

// Why the last attempt failed; the names match the BLE status codes (BLE-GATT.md).
enum class Failure : uint8_t { None, AuthFailed, NotFound, Timeout };

void begin();  // loads credentials from NVS and starts connecting if there are any
void loop(uint32_t nowMs);

// Store new credentials and connect to them now (debug console).
void setCredentials(const char* ssid, const char* password);
// Provisioning: connect to new credentials without saving them; persistCurrent() saves them
// after pairing succeeded, restoreSaved() goes back to the stored network after a failure.
void tryCredentials(const char* ssid, const char* password);
void persistCurrent();
void restoreSaved();
void forget();

// Debug: drop the connection and keep it down for `ms`, as if the router disappeared.
void simulateOutage(uint32_t ms);

State state();
Failure lastFailure();
const char* ssid();  // "" when off
int8_t rssi();       // dBm, 0 when not connected
bool connected();

const char* name(State s);
const char* name(Failure f);  // "WIFI_AUTH_FAILED", "WIFI_NOT_FOUND", "WIFI_TIMEOUT", "-"

}  // namespace wifi
