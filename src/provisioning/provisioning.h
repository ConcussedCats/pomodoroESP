#pragma once
#include <stdint.h>

// Provisioning and pairing (FIRMWARE-SPEC 3.3, 3.4; BLE-GATT.md; PROTOCOL.md 3.1).
// BLE gives Wi-Fi + a one-time pairing token + apiBaseUrl; the cube joins Wi-Fi, calls
// POST {apiBaseUrl}/api/devices/pair and keeps the returned device secret in NVS.
namespace provisioning {

enum class Phase : uint8_t {
    Off,             // BLE off (paired cube in normal use)
    Idle,            // advertising, waiting for credentials
    WifiConnecting,  // joining the network from the payload
    Pairing,         // calling /api/devices/pair
    Paired,          // done, BLE stops shortly
};

using ServerTimeHandler = void (*)(int64_t serverTimeMs);

void begin(ServerTimeHandler onServerTime);
void start();  // advertise (no secret, or long press of the blue button)
void cancel();  // stop provisioning (only meaningful for an already paired cube)
void loop(uint32_t nowMs);

Phase phase();
bool active();          // BLE provisioning running
const char* status();   // last BLE status text, e.g. "WIFI_CONNECTING" or "WIFI_FAILED: WIFI_AUTH_FAILED"

}  // namespace provisioning
