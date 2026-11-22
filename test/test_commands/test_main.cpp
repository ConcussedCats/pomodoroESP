#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "commands/command_protocol.h"

namespace {

commands::CacheState persisted;
bool hasPersisted = false;

commands::Persistence memoryStore() {
    return {
        [](commands::CacheState& out) {
            if (!hasPersisted) return false;
            out = persisted;
            return true;
        },
        [](const commands::CacheState& value) {
            persisted = value;
            hasPersisted = true;
        },
    };
}

commands::Ack makeAck(unsigned id, bool executed = true) {
    commands::Ack ack;
    snprintf(ack.commandId, sizeof(ack.commandId), "00000000-0000-4000-8000-%012u", id);
    ack.executed = executed;
    if (!executed) strcpy(ack.errorCode, "INVALID_STATE");
    ack.state.mode = timer::Mode::ShortBreak;
    ack.state.status = timer::Status::Running;
    ack.state.durationSeconds = 300;
    ack.state.remainingSeconds = 240;
    ack.state.startedAt = 1000;
    strcpy(ack.state.sessionId, "session-a");
    ack.endsAt = 241000;
    return ack;
}

}  // namespace

void setUp() {
    persisted = commands::CacheState{};
    hasPersisted = false;
}

void tearDown() {}

void test_parse_latest_command_and_mode() {
    const char* json = R"({"commands":[{"commandId":"00000000-0000-4000-8000-000000000000","type":"STOP","mode":null,"createdAt":50,"expiresAt":20000},{"commandId":"00000000-0000-4000-8000-000000000001","type":"START","mode":"LONG_BREAK","createdAt":100,"expiresAt":30000}]})";
    commands::Command command;
    TEST_ASSERT_EQUAL(commands::ParseResult::Valid, commands::parseLatest(json, command));
    TEST_ASSERT_EQUAL_STRING("00000000-0000-4000-8000-000000000001", command.commandId);
    TEST_ASSERT_EQUAL(timer::CommandType::Start, command.type);
    TEST_ASSERT_TRUE(command.hasMode);
    TEST_ASSERT_EQUAL(timer::Mode::LongBreak, command.mode);
    TEST_ASSERT_EQUAL_INT64(30000, command.expiresAt);
}

void test_empty_and_invalid_command_responses_are_distinct() {
    commands::Command command;
    TEST_ASSERT_EQUAL(commands::ParseResult::Empty, commands::parseLatest(R"({"commands":[]})", command));
    TEST_ASSERT_EQUAL(commands::ParseResult::Invalid,
                      commands::parseLatest(R"({"commands":[{"commandId":"a","type":"START","mode":"NOPE","expiresAt":1}]})", command));
    TEST_ASSERT_EQUAL(commands::ParseResult::Invalid, commands::parseLatest("not-json", command));
}

void test_parse_set_state_running_target() {
    const char* json = R"({"commands":[{"commandId":"00000000-0000-4000-8000-000000000002","type":"SET_STATE","mode":"WORK","targetState":{"status":"RUNNING","durationSeconds":1500,"remainingSeconds":1121,"endsAt":1761151121000,"sessionId":"c7e1aaaa-bbbb-4ccc-8ddd-eeeeffff0000"},"createdAt":1761150000000,"expiresAt":1761150030000}]})";
    commands::Command command;
    TEST_ASSERT_EQUAL(commands::ParseResult::Valid, commands::parseLatest(json, command));
    TEST_ASSERT_EQUAL(timer::CommandType::SetState, command.type);
    TEST_ASSERT_TRUE(command.hasMode);
    TEST_ASSERT_EQUAL(timer::Mode::Work, command.mode);
    TEST_ASSERT_TRUE(command.hasTarget);
    TEST_ASSERT_EQUAL(timer::Status::Running, command.target.status);
    TEST_ASSERT_EQUAL_UINT32(1500, command.target.durationSeconds);
    TEST_ASSERT_EQUAL_INT64(1121, command.target.remainingSeconds);
    TEST_ASSERT_EQUAL_INT64(1761151121000, command.target.endsAt);
    TEST_ASSERT_EQUAL_STRING("c7e1aaaa-bbbb-4ccc-8ddd-eeeeffff0000", command.target.sessionId);
}

void test_parse_set_state_paused_target_without_end() {
    const char* json = R"({"commands":[{"commandId":"00000000-0000-4000-8000-000000000003","type":"SET_STATE","mode":"SHORT_BREAK","targetState":{"status":"PAUSED","durationSeconds":300,"remainingSeconds":140,"endsAt":null,"sessionId":"c7e1aaaa-bbbb-4ccc-8ddd-eeeeffff0000"},"expiresAt":1761150030000}]})";
    commands::Command command;
    TEST_ASSERT_EQUAL(commands::ParseResult::Valid, commands::parseLatest(json, command));
    TEST_ASSERT_EQUAL(timer::Status::Paused, command.target.status);
    TEST_ASSERT_EQUAL_INT64(timer::NONE, command.target.endsAt);
}

void test_set_state_requires_a_well_formed_target() {
    commands::Command command;
    TEST_ASSERT_EQUAL(commands::ParseResult::Invalid,
                      commands::parseLatest(R"({"commands":[{"commandId":"x","type":"SET_STATE","mode":"WORK","expiresAt":10}]})", command));
    TEST_ASSERT_EQUAL(commands::ParseResult::Invalid,
                      commands::parseLatest(R"({"commands":[{"commandId":"x","type":"SET_STATE","mode":"WORK","targetState":{"status":"STOPPED","durationSeconds":300,"remainingSeconds":10,"sessionId":"s"},"expiresAt":10}]})", command));
    TEST_ASSERT_EQUAL(commands::ParseResult::Invalid,
                      commands::parseLatest(R"({"commands":[{"commandId":"x","type":"SET_STATE","mode":"WORK","targetState":{"status":"RUNNING","durationSeconds":300,"remainingSeconds":10,"endsAt":"later","sessionId":"s"},"expiresAt":10}]})", command));
}

void test_release_has_no_target_state() {
    commands::Command command;
    TEST_ASSERT_EQUAL(commands::ParseResult::Valid,
                      commands::parseLatest(R"({"commands":[{"commandId":"release","type":"RELEASE","mode":null,"expiresAt":10}]})", command));
    TEST_ASSERT_EQUAL(timer::CommandType::Release, command.type);
    TEST_ASSERT_FALSE(command.hasMode);
    TEST_ASSERT_FALSE(command.hasTarget);
}

void test_ack_contains_exact_resulting_state() {
    commands::Ack ack = makeAck(1);
    char json[1024];
    TEST_ASSERT_TRUE(commands::buildAck(ack, json, sizeof(json)));
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, json));
    TEST_ASSERT_EQUAL_STRING("EXECUTED", doc["result"]);
    TEST_ASSERT_TRUE(doc["errorCode"].isNull());
    TEST_ASSERT_EQUAL_STRING("SHORT_BREAK", doc["state"]["mode"]);
    TEST_ASSERT_EQUAL_STRING("RUNNING", doc["state"]["status"]);
    TEST_ASSERT_EQUAL_UINT32(240, doc["state"]["remainingSeconds"]);
    TEST_ASSERT_EQUAL_INT64(241000, doc["state"]["endsAt"].as<int64_t>());
}

void test_cache_survives_restart_and_prevents_duplicate_execution() {
    commands::Cache before(memoryStore());
    before.begin();
    const commands::Ack original = makeAck(7, false);
    before.remember(original);

    commands::Cache after(memoryStore());
    after.begin();
    const commands::Ack* found = after.find(original.commandId);
    TEST_ASSERT_NOT_NULL(found);
    TEST_ASSERT_FALSE(found->executed);
    TEST_ASSERT_EQUAL_STRING("INVALID_STATE", found->errorCode);
    after.remember(makeAck(7));
    TEST_ASSERT_EQUAL_UINT8(1, after.size());
    TEST_ASSERT_FALSE(after.find(original.commandId)->executed);
}

void test_cache_keeps_only_last_16_results() {
    commands::Cache cache;
    cache.begin();
    for (unsigned i = 1; i <= commands::CACHE_CAPACITY + 1; i++) cache.remember(makeAck(i));
    TEST_ASSERT_EQUAL_UINT8(commands::CACHE_CAPACITY, cache.size());
    const commands::Ack first = makeAck(1);
    const commands::Ack last = makeAck(commands::CACHE_CAPACITY + 1);
    TEST_ASSERT_NULL(cache.find(first.commandId));
    TEST_ASSERT_NOT_NULL(cache.find(last.commandId));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_parse_latest_command_and_mode);
    RUN_TEST(test_empty_and_invalid_command_responses_are_distinct);
    RUN_TEST(test_parse_set_state_running_target);
    RUN_TEST(test_parse_set_state_paused_target_without_end);
    RUN_TEST(test_set_state_requires_a_well_formed_target);
    RUN_TEST(test_release_has_no_target_state);
    RUN_TEST(test_ack_contains_exact_resulting_state);
    RUN_TEST(test_cache_survives_restart_and_prevents_duplicate_execution);
    RUN_TEST(test_cache_keeps_only_last_16_results);
    return UNITY_END();
}
