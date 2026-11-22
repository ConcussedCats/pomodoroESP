#include "sync/config.h"

#include <ArduinoJson.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>

namespace syncconfig {
namespace {

bool fail(char* out, size_t size, const char* message) {
    if (out && size) snprintf(out, size, "%s", message);
    return false;
}

bool positiveMinutes(JsonVariantConst value, uint32_t& seconds) {
    if (!value.is<uint32_t>()) return false;
    const uint32_t minutes = value.as<uint32_t>();
    if (minutes == 0 || minutes > UINT32_MAX / 60) return false;
    seconds = minutes * 60;
    return true;
}

bool parseMode(const char* value, timer::Mode& out) {
    if (!value) return false;
    if (strcmp(value, "WORK") == 0) out = timer::Mode::Work;
    else if (strcmp(value, "SHORT_BREAK") == 0) out = timer::Mode::ShortBreak;
    else if (strcmp(value, "LONG_BREAK") == 0) out = timer::Mode::LongBreak;
    else return false;
    return true;
}

bool parseStatus(const char* value, timer::Status& out) {
    if (!value) return false;
    if (strcmp(value, "RUNNING") == 0) out = timer::Status::Running;
    else if (strcmp(value, "PAUSED") == 0) out = timer::Status::Paused;
    else if (strcmp(value, "STOPPED") == 0) out = timer::Status::Stopped;
    else if (strcmp(value, "FINISHED") == 0) out = timer::Status::Finished;
    else return false;
    return true;
}

bool parseAction(const char* value, timer::FaceAction& out) {
    if (!value) return false;
    if (strcmp(value, "START_WORK") == 0) out = timer::FaceAction::StartWork;
    else if (strcmp(value, "START_SHORT_BREAK") == 0) out = timer::FaceAction::StartShortBreak;
    else if (strcmp(value, "START_LONG_BREAK") == 0) out = timer::FaceAction::StartLongBreak;
    else if (strcmp(value, "PAUSE") == 0) out = timer::FaceAction::Pause;
    else if (strcmp(value, "NONE") == 0) out = timer::FaceAction::None;
    else return false;
    return true;
}

int64_t nullableTime(JsonVariantConst value) {
    return value.isNull() ? timer::NONE : value.as<int64_t>();
}

}  // namespace

bool parse(const char* json, DeviceConfig& out, char* error, size_t errorSize) {
    if (!json) return fail(error, errorSize, "empty config");
    JsonDocument doc;
    if (deserializeJson(doc, json) != DeserializationError::Ok) return fail(error, errorSize, "invalid JSON");
    DeviceConfig parsed;

    const char* hash = doc["configHash"] | "";
    if (strlen(hash) >= sizeof(parsed.hash)) return fail(error, errorSize, "configHash too long");
    strcpy(parsed.hash, hash);

    JsonObjectConst timerObject = doc["timer"];
    if (!timerObject.isNull()) {
        if (!positiveMinutes(timerObject["pomodoroMinutes"], parsed.durations.workSeconds) ||
            !positiveMinutes(timerObject["shortBreakMinutes"], parsed.durations.shortBreakSeconds) ||
            !positiveMinutes(timerObject["longBreakMinutes"], parsed.durations.longBreakSeconds)) {
            return fail(error, errorSize, "invalid timer durations");
        }
        parsed.hasTimer = true;
    }

    JsonObjectConst hardware = doc["hardware"];
    if (!hardware.isNull()) {
        if (!hardware["vibrationEnabled"].is<bool>() || !hardware["soundEnabled"].is<bool>() ||
            !hardware["rgbBrightness"].is<uint8_t>() || hardware["rgbBrightness"].as<uint8_t>() > 100) {
            return fail(error, errorSize, "invalid hardware config");
        }
        parsed.vibrationEnabled = hardware["vibrationEnabled"].as<bool>();
        parsed.soundEnabled = hardware["soundEnabled"].as<bool>();
        parsed.rgbBrightness = hardware["rgbBrightness"].as<uint8_t>();
        parsed.hasHardware = true;
    }

    JsonObjectConst faces = doc["faces"];
    if (!faces.isNull()) {
        static const char* const names[6] = {"WORK", "SHORT_BREAK", "LONG_BREAK", "PAUSE", "DISPLAY", "TECH"};
        for (uint8_t i = 0; i < 6; i++) {
            if (!parseAction(faces[names[i]], parsed.faceActions[i])) return fail(error, errorSize, "invalid face action");
        }
        parsed.hasFaces = true;
    }

    JsonObjectConst state = doc["state"];
    if (!state.isNull()) {
        timer::State s;
        if (!parseMode(state["mode"], s.mode) || !parseStatus(state["status"], s.status) ||
            !state["durationSeconds"].is<uint32_t>() || !state["remainingSeconds"].is<uint32_t>()) {
            return fail(error, errorSize, "invalid timer state");
        }
        s.durationSeconds = state["durationSeconds"].as<uint32_t>();
        s.remainingSeconds = state["remainingSeconds"].as<uint32_t>();
        if (s.durationSeconds == 0 || s.remainingSeconds > s.durationSeconds) return fail(error, errorSize, "invalid timer state");
        s.startedAt = nullableTime(state["startedAt"]);
        s.pausedAt = nullableTime(state["pausedAt"]);
        const char* session = state["sessionId"] | "";
        if (strlen(session) >= sizeof(s.sessionId)) return fail(error, errorSize, "sessionId too long");
        strcpy(s.sessionId, session);
        s.cycleIndex = state["cycleIndex"] | 0;
        s.phaseIndex = state["phaseIndex"] | 0;
        s.localEndsAt = timer::NONE;
        parsed.state = s;
        parsed.stateEndsAt = nullableTime(state["endsAt"]);
        parsed.serverTime = state["serverTime"].is<int64_t>() ? state["serverTime"].as<int64_t>() : timer::NONE;
        parsed.hasState = true;
    }
    if (doc["serverTime"].is<int64_t>()) parsed.serverTime = doc["serverTime"].as<int64_t>();

    out = parsed;
    if (error && errorSize) error[0] = '\0';
    return true;
}

}  // namespace syncconfig
