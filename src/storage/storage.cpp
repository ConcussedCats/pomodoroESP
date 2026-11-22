#include "storage/storage.h"

#include <Arduino.h>
#include <Preferences.h>
#include <nvs_flash.h>
#include <string.h>

namespace storage {
namespace {

constexpr const char* NS = "telos";
constexpr const char* EVENT_NS = "queue";
constexpr const char* EVENT_PARTITION = "events";
constexpr const char* KEY_BOOTS = "boot_counter";
constexpr const char* KEY_TIMER = "timer";
constexpr const char* KEY_SSID = "wifi_ssid";
constexpr const char* KEY_PASS = "wifi_pass";
constexpr const char* KEY_FACES = "face_map";
constexpr const char* KEY_ROT = "rotation";
constexpr const char* KEY_SECRET = "secret";
constexpr const char* KEY_DEVICE_ID = "device_id";
constexpr const char* KEY_API = "api_base";
constexpr const char* KEY_CONFIG = "config";
constexpr const char* KEY_CONFIG_HASH = "config_hash";
constexpr const char* KEY_EVENT_META = "events_meta";

struct EventMeta {
    uint8_t version;
    uint8_t head;
    uint8_t count;
};

Preferences prefs;
Preferences eventPrefs;
bool ready = false;
bool eventsReady = false;
timer::Saved lastTimer{};
bool hasLastTimer = false;

bool loadString(const char* key, char* out, size_t size) {
    out[0] = '\0';
    if (!ready || !prefs.isKey(key)) return false;
    const size_t n = prefs.getString(key, out, size);
    return n > 0 || out[0] == '\0';
}

}  // namespace

bool begin() {
    ready = prefs.begin(NS, false);
    if (!ready) log_e("NVS namespace '%s' could not be opened", NS);
    esp_err_t eventInit = nvs_flash_init_partition(EVENT_PARTITION);
    if (eventInit == ESP_ERR_NVS_NO_FREE_PAGES || eventInit == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase_partition(EVENT_PARTITION);
    }
    eventsReady = eventPrefs.begin(EVENT_NS, false, EVENT_PARTITION);
    if (!eventsReady) log_e("NVS event partition could not be opened");
    return ready && eventsReady;
}

uint32_t bumpBootCounter() {
    if (!ready) return 0;
    const uint32_t n = prefs.getUInt(KEY_BOOTS, 0) + 1;
    prefs.putUInt(KEY_BOOTS, n);
    return n;
}

bool loadTimer(timer::Saved& out) {
    if (!ready || !prefs.isKey(KEY_TIMER) || prefs.getBytesLength(KEY_TIMER) != sizeof(timer::Saved)) return false;
    timer::Saved s{};
    prefs.getBytes(KEY_TIMER, &s, sizeof(s));
    if (!timer::isValid(s)) return false;
    out = s;
    lastTimer = s;
    hasLastTimer = true;
    return true;
}

void saveTimer(const timer::Saved& s) {
    if (!ready) return;
    if (hasLastTimer && memcmp(&lastTimer, &s, sizeof(s)) == 0) return;
    prefs.putBytes(KEY_TIMER, &s, sizeof(s));
    lastTimer = s;
    hasLastTimer = true;
}

bool loadWifi(char ssid[33], char password[65]) {
    const bool hasSsid = loadString(KEY_SSID, ssid, 33) && ssid[0] != '\0';
    loadString(KEY_PASS, password, 65);
    return hasSsid;
}

void saveWifi(const char* ssid, const char* password) {
    if (!ready) return;
    prefs.putString(KEY_SSID, ssid);
    prefs.putString(KEY_PASS, password);
}

void clearWifi() {
    if (!ready) return;
    prefs.remove(KEY_SSID);
    prefs.remove(KEY_PASS);
}

bool hasSecret() {
    return ready && prefs.isKey(KEY_SECRET) && prefs.getString(KEY_SECRET, "").length() > 0;
}

bool loadDevice(char secret[64], int64_t& deviceId, char apiBase[128]) {
    if (!hasSecret()) return false;
    loadString(KEY_SECRET, secret, 64);
    loadString(KEY_API, apiBase, 128);
    deviceId = prefs.getLong64(KEY_DEVICE_ID, 0);
    return secret[0] != '\0' && apiBase[0] != '\0';
}

void saveDevice(const char* secret, int64_t deviceId, const char* apiBase) {
    if (!ready) return;
    prefs.putString(KEY_SECRET, secret);
    prefs.putLong64(KEY_DEVICE_ID, deviceId);
    prefs.putString(KEY_API, apiBase);
}

void clearDevice() {
    if (!ready) return;
    prefs.remove(KEY_SECRET);
    prefs.remove(KEY_DEVICE_ID);
    prefs.remove(KEY_CONFIG);
    prefs.remove(KEY_CONFIG_HASH);
}

bool loadConfig(char* json, size_t jsonSize, char hash[65]) {
    if (!ready || !json || jsonSize == 0 || !hash) return false;
    const bool hasJson = loadString(KEY_CONFIG, json, jsonSize);
    const bool hasHash = loadString(KEY_CONFIG_HASH, hash, 65);
    return hasJson && hasHash && json[0] != '\0' && hash[0] != '\0';
}

void saveConfig(const char* json, const char* hash) {
    if (!ready || !json || !hash || !json[0] || !hash[0]) return;
    prefs.putString(KEY_CONFIG, json);
    prefs.putString(KEY_CONFIG_HASH, hash);
}

bool loadEventMeta(uint8_t& head, uint8_t& count) {
    if (!eventsReady || !eventPrefs.isKey(KEY_EVENT_META) || eventPrefs.getBytesLength(KEY_EVENT_META) != sizeof(EventMeta)) return false;
    EventMeta meta{};
    eventPrefs.getBytes(KEY_EVENT_META, &meta, sizeof(meta));
    if (meta.version != 1 || meta.head >= events::CAPACITY || meta.count > events::CAPACITY) return false;
    head = meta.head;
    count = meta.count;
    return true;
}

void saveEventMeta(uint8_t head, uint8_t count) {
    if (!eventsReady) return;
    const EventMeta meta{1, head, count};
    eventPrefs.putBytes(KEY_EVENT_META, &meta, sizeof(meta));
}

bool loadEventSlot(uint8_t slot, events::Event& event) {
    if (!eventsReady || slot >= events::CAPACITY) return false;
    char key[12];
    snprintf(key, sizeof(key), "event_%02u", slot);
    if (!eventPrefs.isKey(key) || eventPrefs.getBytesLength(key) != sizeof(event)) return false;
    eventPrefs.getBytes(key, &event, sizeof(event));
    return true;
}

void saveEventSlot(uint8_t slot, const events::Event& event) {
    if (!eventsReady || slot >= events::CAPACITY) return;
    char key[12];
    snprintf(key, sizeof(key), "event_%02u", slot);
    eventPrefs.putBytes(key, &event, sizeof(event));
}

bool loadFaceMap(faces::Face out[6]) {
    if (!ready || !prefs.isKey(KEY_FACES) || prefs.getBytesLength(KEY_FACES) != 6) return false;
    uint8_t raw[6];
    prefs.getBytes(KEY_FACES, raw, sizeof(raw));
    for (int i = 0; i < 6; i++) {
        if (raw[i] > static_cast<uint8_t>(faces::Face::Tech)) return false;
    }
    for (int i = 0; i < 6; i++) out[i] = static_cast<faces::Face>(raw[i]);
    return true;
}

void saveFaceMap(const faces::Face table[6]) {
    if (!ready) return;
    uint8_t raw[6];
    for (int i = 0; i < 6; i++) raw[i] = static_cast<uint8_t>(table[i]);
    prefs.putBytes(KEY_FACES, raw, sizeof(raw));
}

bool loadRotations(int8_t out[6]) {
    if (!ready || !prefs.isKey(KEY_ROT) || prefs.getBytesLength(KEY_ROT) != 6) return false;
    int8_t raw[6];
    prefs.getBytes(KEY_ROT, raw, sizeof(raw));
    for (int i = 0; i < 6; i++) {
        if (raw[i] < -1 || raw[i] > 3) return false;
    }
    memcpy(out, raw, sizeof(raw));
    return true;
}

void saveRotations(const int8_t table[6]) {
    if (!ready) return;
    prefs.putBytes(KEY_ROT, table, 6);
}

void clearCalibration() {
    if (!ready) return;
    prefs.remove(KEY_FACES);
    prefs.remove(KEY_ROT);
}

void wipe() {
    if (!ready) return;
    prefs.clear();
    if (eventsReady) eventPrefs.clear();
    hasLastTimer = false;
}

}  // namespace storage
