#include <ArduinoJson.h>
#include <string.h>
#include <unity.h>

#include "sync/config.h"
#include "sync/protocol.h"

void setUp() {}
void tearDown() {}

void test_heartbeat_contains_only_real_metrics_and_protocol_snapshot() {
    syncproto::Heartbeat h{};
    h.firmwareVersion = "0.1.0";
    h.wifiRssi = -61;
    h.uptimeSeconds = 3600;
    h.freeHeapBytes = 180000;
    h.face = faces::Face::Work;
    h.configHash = "abc123";
    h.state.mode = timer::Mode::Work;
    h.state.status = timer::Status::Running;
    h.state.durationSeconds = 1500;
    h.state.remainingSeconds = 1200;
    h.state.startedAt = timer::NONE;
    h.state.pausedAt = timer::NONE;
    strcpy(h.state.sessionId, "b0c5b9b2-3f3e-4a7e-9f2a-1f1d3c2b4a59");
    h.endsAt = 1761151500000LL;

    char json[1024];
    TEST_ASSERT_TRUE(syncproto::buildHeartbeatJson(h, json, sizeof(json)));
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, json));
    TEST_ASSERT_EQUAL_STRING("0.1.0", doc["firmwareVersion"]);
    TEST_ASSERT_EQUAL(1, doc["protocolVersion"].as<int>());
    TEST_ASSERT_TRUE(doc["capabilities"]["display"]);
    TEST_ASSERT_EQUAL(-61, doc["metrics"]["wifiRssi"].as<int>());
    TEST_ASSERT_FALSE(doc["metrics"].containsKey("battery"));
    TEST_ASSERT_TRUE(doc["state"]["startedAt"].isNull());
    TEST_ASSERT_EQUAL_INT64(1761151500000LL, doc["state"]["endsAt"].as<int64_t>());
    TEST_ASSERT_EQUAL_STRING("abc123", doc["configHash"]);
}

void test_heartbeat_nulls_end_and_session_when_not_available() {
    syncproto::Heartbeat h{};
    h.firmwareVersion = "0.1.0";
    h.face = faces::Face::Unknown;
    h.state.status = timer::Status::Stopped;
    h.endsAt = timer::NONE;
    char json[1024];
    TEST_ASSERT_TRUE(syncproto::buildHeartbeatJson(h, json, sizeof(json)));
    JsonDocument doc;
    deserializeJson(doc, json);
    TEST_ASSERT_EQUAL_STRING("DISPLAY", doc["face"]);
    TEST_ASSERT_TRUE(doc["configHash"].isNull());
    TEST_ASSERT_TRUE(doc["state"]["endsAt"].isNull());
    TEST_ASSERT_TRUE(doc["state"]["sessionId"].isNull());
}

const char* VALID_CONFIG = R"json({
  "configHash":"5f2c",
  "timer":{"pomodoroMinutes":50,"shortBreakMinutes":10,"longBreakMinutes":20,"pomoCycles":4,"patternType":"classic"},
  "hardware":{"vibrationEnabled":false,"soundEnabled":true,"rgbBrightness":73},
  "faces":{"WORK":"NONE","SHORT_BREAK":"START_SHORT_BREAK","LONG_BREAK":"START_LONG_BREAK","PAUSE":"PAUSE","DISPLAY":"START_WORK","TECH":"NONE"},
  "state":{"mode":"WORK","status":"PAUSED","durationSeconds":3000,"remainingSeconds":1234,
           "startedAt":null,"pausedAt":1761150000000,"endsAt":null,"sessionId":"abc","cycleIndex":2,"phaseIndex":3,
           "serverTime":1761150000500}
})json";

void test_config_parses_durations_hardware_faces_and_state() {
    syncconfig::DeviceConfig config;
    char error[64];
    TEST_ASSERT_TRUE_MESSAGE(syncconfig::parse(VALID_CONFIG, config, error, sizeof(error)), error);
    TEST_ASSERT_EQUAL_STRING("5f2c", config.hash);
    TEST_ASSERT_EQUAL_UINT32(3000, config.durations.workSeconds);
    TEST_ASSERT_EQUAL_UINT32(600, config.durations.shortBreakSeconds);
    TEST_ASSERT_FALSE(config.vibrationEnabled);
    TEST_ASSERT_TRUE(config.soundEnabled);
    TEST_ASSERT_EQUAL_UINT8(73, config.rgbBrightness);
    TEST_ASSERT_EQUAL(timer::FaceAction::None, config.faceActions[0]);
    TEST_ASSERT_EQUAL(timer::FaceAction::StartWork, config.faceActions[4]);
    TEST_ASSERT_TRUE(config.hasState);
    TEST_ASSERT_EQUAL(timer::Status::Paused, config.state.status);
    TEST_ASSERT_EQUAL_UINT32(1234, config.state.remainingSeconds);
    TEST_ASSERT_EQUAL_INT64(1761150000500LL, config.serverTime);
}

void test_config_rejects_invalid_hardware_and_face_action() {
    syncconfig::DeviceConfig config;
    char error[64];
    const char* brightness = R"({"configHash":"x","hardware":{"vibrationEnabled":true,"soundEnabled":true,"rgbBrightness":101}})";
    TEST_ASSERT_FALSE(syncconfig::parse(brightness, config, error, sizeof(error)));
    TEST_ASSERT_EQUAL_STRING("invalid hardware config", error);
    const char* action = R"({"configHash":"x","faces":{"WORK":"BOGUS","SHORT_BREAK":"NONE","LONG_BREAK":"NONE","PAUSE":"NONE","DISPLAY":"NONE","TECH":"NONE"}})";
    TEST_ASSERT_FALSE(syncconfig::parse(action, config, error, sizeof(error)));
    TEST_ASSERT_EQUAL_STRING("invalid face action", error);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_heartbeat_contains_only_real_metrics_and_protocol_snapshot);
    RUN_TEST(test_heartbeat_nulls_end_and_session_when_not_available);
    RUN_TEST(test_config_parses_durations_hardware_faces_and_state);
    RUN_TEST(test_config_rejects_invalid_hardware_and_face_action);
    return UNITY_END();
}
