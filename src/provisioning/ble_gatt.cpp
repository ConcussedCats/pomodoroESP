#include "provisioning/ble_gatt.h"

#include <Arduino.h>
#include <NimBLEDevice.h>
#include <esp_mac.h>
#include <string.h>

extern "C" {
#include "nimble/nimble/host/include/host/ble_hs_id.h"
#include "nimble/nimble/host/include/host/ble_hs_pvcy.h"
}

#include <string>

#include "core/device.h"

namespace blegatt {
namespace {

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
bool isRunning = false;
volatile bool connected = false;
volatile bool newConnection = false;
volatile bool cancelRequested = false;
bool hasPayload = false;
char payload[MAX_PAYLOAD + 1];
char bleName[20] = "";

NimBLECharacteristic* statusChr = nullptr;

// NimBLE's setValue(const T&) copies sizeof(T) bytes: a char* would store the pointer and a
// char[] the whole array with its trailing zeros. Always pass the text bytes explicitly.
void setText(NimBLECharacteristic* c, const char* s) {
    c->setValue(reinterpret_cast<const uint8_t*>(s), strlen(s));
}

class ServerCallbacks : public NimBLEServerCallbacks {
    void onConnect(NimBLEServer*, ble_gap_conn_desc*) override {
        connected = true;
        newConnection = true;
        // Keep advertising while connected: Windows keeps a background link to the cube, and
        // without advertising "Settings > Add device" can never reach it to pair.
        NimBLEDevice::startAdvertising();
        // No security request from the cube: Windows ignores it and the link drops after the
        // 30 s SMP timeout. Chrome starts pairing itself when the encrypted write is refused.
    }
    void onAuthenticationComplete(ble_gap_conn_desc* desc) override {
        log_i("BLE: link %s, bonded %d", desc->sec_state.encrypted ? "encrypted" : "NOT encrypted",
              desc->sec_state.bonded);
    }
    void onDisconnect(NimBLEServer*) override {
        connected = false;
    }
};

class ProvisioningCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        const std::string v = c->getValue();
        portENTER_CRITICAL(&lock);
        const size_t n = v.size() > MAX_PAYLOAD ? MAX_PAYLOAD : v.size();
        memcpy(payload, v.data(), n);
        payload[n] = '\0';
        hasPayload = true;
        portEXIT_CRITICAL(&lock);
        // Wipe the stack's copy of the Wi-Fi password.
        static const uint8_t none = 0;
        c->setValue(&none, 0);
    }
};

class ControlCallbacks : public NimBLECharacteristicCallbacks {
    void onWrite(NimBLECharacteristic* c) override {
        const std::string v = c->getValue();
        if (v.find("CANCEL") != std::string::npos) cancelRequested = true;
    }
};

// A static random BLE address derived from the eFuse MAC: stable for this board, but not the
// public Bluetooth MAC, so a Windows PC that cached a broken pairing state for the public
// address sees a fresh device. Not a changing (private) address: privacy stays off.
void useStaticRandomAddress() {
    uint8_t mac[6];
    esp_efuse_mac_get_default(mac);
    uint8_t rnd[6];  // little-endian, as NimBLE expects
    for (int i = 0; i < 6; i++) rnd[i] = mac[5 - i] ^ 0x5A;
    rnd[5] |= 0xC0;  // two top bits 11 = static random address
    NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
    ble_hs_pvcy_rpa_config(NIMBLE_HOST_DISABLE_PRIVACY);
    const int rc = ble_hs_id_set_rnd(rnd);
    log_i("BLE: static random address %02X:%02X:%02X:%02X:%02X:%02X (rc %d)", rnd[5], rnd[4], rnd[3], rnd[2],
          rnd[1], rnd[0], rc);
}

ServerCallbacks serverCallbacks;
ProvisioningCallbacks provisioningCallbacks;
ControlCallbacks controlCallbacks;

}  // namespace

const char* name() {
    if (bleName[0] == '\0') snprintf(bleName, sizeof(bleName), "Telos-Cube-%s", device::hardwareId() + 8);
    return bleName;
}

void start(const char* deviceInfoJson) {
    if (isRunning) return;
    NimBLEDevice::init(name());
    useStaticRandomAddress();
    // LE Secure Connections, "Just Works" (the cube has no keypad), with bonding: Windows always
    // creates a bond and refuses to pair with a device that says "no bonding" ("Authentication
    // failed" in Chrome). Bonds are wiped on factory reset (stage 9).
    NimBLEDevice::setSecurityAuth(true, false, true);
    NimBLEDevice::setSecurityIOCap(BLE_HS_IO_NO_INPUT_OUTPUT);
    NimBLEDevice::setMTU(517);

    NimBLEServer* server = NimBLEDevice::createServer();
    server->setCallbacks(&serverCallbacks, false);
    NimBLEService* svc = server->createService(SERVICE_UUID);

    NimBLECharacteristic* info = svc->createCharacteristic(INFO_UUID, NIMBLE_PROPERTY::READ);
    setText(info, deviceInfoJson);

    NimBLECharacteristic* prov = svc->createCharacteristic(
        PROVISIONING_UUID, NIMBLE_PROPERTY::WRITE | NIMBLE_PROPERTY::WRITE_ENC, MAX_PAYLOAD);
    prov->setCallbacks(&provisioningCallbacks);

    statusChr = svc->createCharacteristic(STATUS_UUID, NIMBLE_PROPERTY::READ | NIMBLE_PROPERTY::NOTIFY);
    setText(statusChr, "{\"state\":\"IDLE\",\"errorCode\":null}");

    NimBLECharacteristic* control = svc->createCharacteristic(CONTROL_UUID, NIMBLE_PROPERTY::WRITE);
    control->setCallbacks(&controlCallbacks);

    svc->start();
    NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
    adv->addServiceUUID(SERVICE_UUID);
    adv->setScanResponse(true);  // the 128-bit UUID and the name do not both fit one packet
    adv->start();
    isRunning = true;
    log_i("BLE: advertising as %s", name());
}

void stop() {
    if (!isRunning) return;
    NimBLEDevice::getAdvertising()->stop();
    NimBLEDevice::deinit(true);
    statusChr = nullptr;
    isRunning = false;
    connected = false;
    log_i("BLE: stopped");
}

bool running() {
    return isRunning;
}

bool clientConnected() {
    return connected;
}

void setStatus(const char* state, const char* errorCode) {
    if (!statusChr) return;
    char json[96];
    if (errorCode) snprintf(json, sizeof(json), "{\"state\":\"%s\",\"errorCode\":\"%s\"}", state, errorCode);
    else snprintf(json, sizeof(json), "{\"state\":\"%s\",\"errorCode\":null}", state);
    setText(statusChr, json);
    statusChr->notify();
    log_i("BLE status: %s", json);
}

bool takeNewConnection() {
    const bool v = newConnection;
    newConnection = false;
    return v;
}

bool takeProvisioning(char* out, size_t size) {
    portENTER_CRITICAL(&lock);
    const bool had = hasPayload;
    if (had) {
        strncpy(out, payload, size - 1);
        out[size - 1] = '\0';
        memset(payload, 0, sizeof(payload));
        hasPayload = false;
    }
    portEXIT_CRITICAL(&lock);
    return had;
}

bool takeCancel() {
    const bool v = cancelRequested;
    cancelRequested = false;
    return v;
}

}  // namespace blegatt
