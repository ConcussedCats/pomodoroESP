#include "commands/command_protocol.h"

#include <ArduinoJson.h>
#include <string.h>
#include <utility>

namespace commands {
namespace {

timer::CommandType commandType(const char* value) {
    if (strcmp(value, "START") == 0) return timer::CommandType::Start;
    if (strcmp(value, "PAUSE") == 0) return timer::CommandType::Pause;
    if (strcmp(value, "RESUME") == 0) return timer::CommandType::Resume;
    if (strcmp(value, "STOP") == 0) return timer::CommandType::Stop;
    if (strcmp(value, "SWITCH_MODE") == 0) return timer::CommandType::SwitchMode;
    if (strcmp(value, "SET_STATE") == 0) return timer::CommandType::SetState;
    if (strcmp(value, "RELEASE") == 0) return timer::CommandType::Release;
    return timer::CommandType::Unknown;
}

bool parseMode(const char* value, timer::Mode& out) {
    if (!value) return false;
    if (strcmp(value, "WORK") == 0) out = timer::Mode::Work;
    else if (strcmp(value, "SHORT_BREAK") == 0) out = timer::Mode::ShortBreak;
    else if (strcmp(value, "LONG_BREAK") == 0) out = timer::Mode::LongBreak;
    else return false;
    return true;
}

void timestamp(JsonObject object, const char* key, int64_t value) {
    if (value == timer::NONE) object[key] = nullptr;
    else object[key] = value;
}

}  // namespace

ParseResult parseLatest(const char* json, Command& out) {
    out = Command{};
    if (!json) return ParseResult::Invalid;
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok || !doc["commands"].is<JsonArrayConst>()) {
        return ParseResult::Invalid;
    }
    JsonArrayConst list = doc["commands"];
    if (list.size() == 0) return ParseResult::Empty;
    JsonObjectConst item = list[list.size() - 1];
    const char* id = item["commandId"] | "";
    if (id[0] == '\0' || strlen(id) >= sizeof(out.commandId) || !item["type"].is<const char*>() ||
        !item["expiresAt"].is<int64_t>()) {
        return ParseResult::Invalid;
    }
    strcpy(out.commandId, id);
    out.type = commandType(item["type"].as<const char*>());
    out.expiresAt = item["expiresAt"].as<int64_t>();
    if (!item["mode"].isNull()) {
        if (!parseMode(item["mode"].as<const char*>(), out.mode)) return ParseResult::Invalid;
        out.hasMode = true;
    }
    return ParseResult::Valid;
}

bool buildAck(const Ack& ack, char* out, size_t size) {
    if (!out || size == 0 || ack.commandId[0] == '\0') return false;
    JsonDocument doc;
    doc["result"] = ack.executed ? "EXECUTED" : "REJECTED";
    if (ack.executed || ack.errorCode[0] == '\0') doc["errorCode"] = nullptr;
    else doc["errorCode"] = ack.errorCode;
    JsonObject state = doc["state"].to<JsonObject>();
    state["mode"] = timer::name(ack.state.mode);
    state["status"] = timer::name(ack.state.status);
    state["durationSeconds"] = ack.state.durationSeconds;
    state["remainingSeconds"] = ack.state.remainingSeconds;
    timestamp(state, "startedAt", ack.state.startedAt);
    timestamp(state, "pausedAt", ack.state.pausedAt);
    if (ack.state.status == timer::Status::Running) timestamp(state, "endsAt", ack.endsAt);
    else state["endsAt"] = nullptr;
    if (ack.state.sessionId[0]) state["sessionId"] = ack.state.sessionId;
    else state["sessionId"] = nullptr;
    state["cycleIndex"] = ack.state.cycleIndex;
    state["phaseIndex"] = ack.state.phaseIndex;
    if (measureJson(doc) + 1 > size) return false;
    serializeJson(doc, out, size);
    return true;
}

Cache::Cache(Persistence persistence) : persistence_(std::move(persistence)) {}

bool Cache::valid(const CacheState& value) const {
    if (value.version != 1 || value.count > CACHE_CAPACITY || value.next >= CACHE_CAPACITY) return false;
    for (uint8_t i = 0; i < value.count; i++) {
        if (value.records[i].commandId[0] == '\0' || memchr(value.records[i].commandId, '\0', 37) == nullptr ||
            memchr(value.records[i].errorCode, '\0', sizeof(value.records[i].errorCode)) == nullptr) return false;
    }
    return true;
}

void Cache::begin() {
    CacheState loaded;
    if (persistence_.load && persistence_.load(loaded) && valid(loaded)) state_ = loaded;
    else clear();
}

const Ack* Cache::find(const char* commandId) const {
    if (!commandId || !commandId[0]) return nullptr;
    for (uint8_t i = 0; i < state_.count; i++) {
        if (strcmp(state_.records[i].commandId, commandId) == 0) return &state_.records[i];
    }
    return nullptr;
}

void Cache::remember(const Ack& ack) {
    if (!ack.commandId[0] || memchr(ack.commandId, '\0', 37) == nullptr) return;
    for (uint8_t i = 0; i < state_.count; i++) {
        if (strcmp(state_.records[i].commandId, ack.commandId) == 0) return;
    }
    state_.records[state_.next] = ack;
    state_.next = uint8_t((state_.next + 1) % CACHE_CAPACITY);
    if (state_.count < CACHE_CAPACITY) state_.count++;
    if (persistence_.save) persistence_.save(state_);
}

void Cache::clear() {
    state_ = CacheState{};
    if (persistence_.save) persistence_.save(state_);
}

}  // namespace commands
