#include "events/event_protocol.h"

#include <ArduinoJson.h>
#include <string.h>

namespace events {
namespace {

void timestamp(JsonObject object, const char* key, int64_t value) {
    if (value == timer::NONE) object[key] = nullptr;
    else object[key] = value;
}

void stateJson(JsonObject object, const Event& event) {
    const timer::State& state = event.state;
    object["mode"] = timer::name(state.mode);
    object["status"] = timer::name(state.status);
    object["durationSeconds"] = state.durationSeconds;
    object["remainingSeconds"] = state.remainingSeconds;
    timestamp(object, "startedAt", state.startedAt);
    timestamp(object, "pausedAt", state.pausedAt);
    if (state.status == timer::Status::Running && event.endsAt != timer::NONE) object["endsAt"] = event.endsAt;
    else object["endsAt"] = nullptr;
    if (state.sessionId[0]) object["sessionId"] = state.sessionId;
    else object["sessionId"] = nullptr;
    object["cycleIndex"] = state.cycleIndex;
    object["phaseIndex"] = state.phaseIndex;
}

bool terminal(const char* result) {
    return result && (strcmp(result, "APPLIED") == 0 || strcmp(result, "DUPLICATE") == 0 ||
                      strcmp(result, "STALE") == 0 || strcmp(result, "IGNORED") == 0 ||
                      strcmp(result, "REJECTED") == 0);
}

}  // namespace

bool buildRequest(const Queue& queue, char* out, size_t size, Batch& batch) {
    batch = Batch{};
    const Event* first = queue.front();
    if (!first || !out || size == 0) return false;
    JsonDocument doc;
    doc["bootId"] = first->bootId;
    JsonArray array = doc["events"].to<JsonArray>();
    for (uint8_t i = 0; i < queue.size() && batch.count < MAX_BATCH; i++) {
        const Event* event = queue.at(i);
        if (!event || strcmp(event->bootId, first->bootId) != 0) break;
        JsonObject item = array.add<JsonObject>();
        item["eventId"] = event->eventId;
        item["seq"] = event->seq;
        item["type"] = timer::name(event->type);
        if (event->type == timer::EventType::FaceChanged) item["face"] = faces::name(event->face);
        timestamp(item, "occurredAt", event->occurredAt);
        stateJson(item["state"].to<JsonObject>(), *event);
        if (measureJson(doc) + 1 > size) {
            array.remove(array.size() - 1);
            break;
        }
        strcpy(batch.eventIds[batch.count++], event->eventId);
    }
    if (batch.count == 0 || measureJson(doc) + 1 > size) return false;
    serializeJson(doc, out, size);
    return true;
}

uint8_t terminalPrefix(const char* json, const Batch& batch) {
    if (!json || batch.count == 0) return 0;
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return 0;
    JsonArrayConst results = doc["results"];
    uint8_t handled = 0;
    for (; handled < batch.count; handled++) {
        bool found = false;
        for (JsonObjectConst item : results) {
            if (strcmp(item["eventId"] | "", batch.eventIds[handled]) == 0 && terminal(item["result"])) {
                found = true;
                break;
            }
        }
        if (!found) break;
    }
    return handled;
}

}  // namespace events
