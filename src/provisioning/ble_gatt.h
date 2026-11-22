#pragma once
#include <stddef.h>

// BLE GATT provisioning service (docs/esp32/BLE-GATT.md, NimBLE).
// Callbacks run in the NimBLE task and only copy data; the app loop takes it with take*().
namespace blegatt {

constexpr const char* SERVICE_UUID = "7e1a0001-5c1e-4c3a-9f2e-6b7d0c0f1a00";
constexpr const char* INFO_UUID = "7e1a0002-5c1e-4c3a-9f2e-6b7d0c0f1a00";
constexpr const char* PROVISIONING_UUID = "7e1a0003-5c1e-4c3a-9f2e-6b7d0c0f1a00";
constexpr const char* STATUS_UUID = "7e1a0004-5c1e-4c3a-9f2e-6b7d0c0f1a00";
constexpr const char* CONTROL_UUID = "7e1a0005-5c1e-4c3a-9f2e-6b7d0c0f1a00";
constexpr size_t MAX_PAYLOAD = 512;

// Starts BLE (name Telos-Cube-XXXX) and advertises the provisioning service.
void start(const char* deviceInfoJson);
// Stops advertising and frees the BLE stack.
void stop();
bool running();
bool clientConnected();

// Status characteristic: {"state":"...","errorCode":null|"..."}, notified to the website.
void setStatus(const char* state, const char* errorCode);

// True once per new connection (the website reads the status right after subscribing).
bool takeNewConnection();
// Provisioning payload written by the website; true when there is a new one.
bool takeProvisioning(char* out, size_t size);
bool takeCancel();

const char* name();  // "Telos-Cube-XXXX"

}  // namespace blegatt
