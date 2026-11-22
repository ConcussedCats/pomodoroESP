#include "sync/protocol.h"

#include <ArduinoJson.h>

namespace syncproto {
namespace {

void timestamp(JsonObject object, const char* key, int64_t value) {
    if (value == timer::NONE) object[key] = nullptr;
    else object[key] = value;
}

}  // namespace

bool buildHeartbeatJson(const Heartbeat& h, char* out, size_t size) {
    if (!out || size == 0 || !h.firmwareVersion) return false;
    JsonDocument doc;
    doc["firmwareVersion"] = h.firmwareVersion;
    doc["protocolVersion"] = 1;
    JsonObject capabilities = doc["capabilities"].to<JsonObject>();
    capabilities["vibration"] = true;
    capabilities["sound"] = true;
    capabilities["rgb"] = true;
    capabilities["display"] = true;
    capabilities["imu"] = true;
    JsonObject metrics = doc["metrics"].to<JsonObject>();
    metrics["wifiRssi"] = h.wifiRssi;
    metrics["uptimeSeconds"] = h.uptimeSeconds;
    metrics["freeHeapBytes"] = h.freeHeapBytes;
    doc["face"] = h.face == faces::Face::Unknown ? "DISPLAY" : faces::name(h.face);
    if (h.configHash && h.configHash[0]) doc["configHash"] = h.configHash;
    else doc["configHash"] = nullptr;

    JsonObject state = doc["state"].to<JsonObject>();
    state["mode"] = timer::name(h.state.mode);
    state["status"] = timer::name(h.state.status);
    state["durationSeconds"] = h.state.durationSeconds;
    state["remainingSeconds"] = h.state.remainingSeconds;
    timestamp(state, "startedAt", h.state.startedAt);
    timestamp(state, "pausedAt", h.state.pausedAt);
    if (h.state.status == timer::Status::Running && h.endsAt != timer::NONE) state["endsAt"] = h.endsAt;
    else state["endsAt"] = nullptr;
    if (h.state.sessionId[0]) state["sessionId"] = h.state.sessionId;
    else state["sessionId"] = nullptr;
    state["cycleIndex"] = h.state.cycleIndex;
    state["phaseIndex"] = h.state.phaseIndex;

    if (measureJson(doc) + 1 > size) return false;
    serializeJson(doc, out, size);
    return true;
}

}  // namespace syncproto
