#include <ArduinoJson.h>
#include <stdio.h>
#include <string.h>
#include <unity.h>

#include "events/event_protocol.h"
#include "events/event_queue.h"

namespace {

events::Event slots[events::CAPACITY];
bool slotPresent[events::CAPACITY];
uint8_t savedHead;
uint8_t savedCount;
bool hasMeta;

events::Persistence memoryStore() {
    return {
        [](uint8_t& head, uint8_t& count) {
            if (!hasMeta) return false;
            head = savedHead;
            count = savedCount;
            return true;
        },
        [](uint8_t head, uint8_t count) {
            savedHead = head;
            savedCount = count;
            hasMeta = true;
        },
        [](uint8_t slot, events::Event& event) {
            if (!slotPresent[slot]) return false;
            event = slots[slot];
            return true;
        },
        [](uint8_t slot, const events::Event& event) {
            slots[slot] = event;
            slotPresent[slot] = true;
        },
    };
}

events::Event makeEvent(uint32_t seq, const char* boot = "boot-a", int64_t occurredAt = 1000) {
    events::Event event;
    snprintf(event.bootId, sizeof(event.bootId), "%s", boot);
    snprintf(event.eventId, sizeof(event.eventId), "00000000-0000-4000-8000-%012lu", (unsigned long)seq);
    event.seq = seq;
    event.type = seq % 2 ? timer::EventType::FaceChanged : timer::EventType::Started;
    event.face = faces::Face::Work;
    event.occurredAt = occurredAt;
    event.state.mode = timer::Mode::Work;
    event.state.status = timer::Status::Running;
    event.state.durationSeconds = 1500;
    event.state.remainingSeconds = 1500;
    strcpy(event.state.sessionId, "session-a");
    event.endsAt = 1501000;
    return event;
}

}  // namespace

void setUp() {
    memset(slots, 0, sizeof(slots));
    memset(slotPresent, 0, sizeof(slotPresent));
    savedHead = savedCount = 0;
    hasMeta = false;
}

void tearDown() {}

void test_queue_survives_restart_in_oldest_first_order() {
    events::Queue before(memoryStore());
    before.begin();
    before.enqueue(makeEvent(1));
    before.enqueue(makeEvent(2));
    before.enqueue(makeEvent(3));
    before.removeOldest(1);

    events::Queue after(memoryStore());
    after.begin();
    TEST_ASSERT_EQUAL_UINT8(2, after.size());
    TEST_ASSERT_EQUAL_UINT32(2, after.front()->seq);
    TEST_ASSERT_EQUAL_UINT32(3, after.at(1)->seq);
}

void test_full_queue_keeps_newest_50_events() {
    events::Queue queue;
    queue.begin();
    for (uint32_t i = 1; i <= events::CAPACITY + 1; i++) queue.enqueue(makeEvent(i));
    TEST_ASSERT_EQUAL_UINT8(events::CAPACITY, queue.size());
    TEST_ASSERT_EQUAL_UINT32(2, queue.front()->seq);
    TEST_ASSERT_EQUAL_UINT32(events::CAPACITY + 1, queue.at(events::CAPACITY - 1)->seq);
    TEST_ASSERT_EQUAL_UINT32(1, queue.droppedWhenFull());
}

void test_batch_never_mixes_boot_ids_and_has_complete_state() {
    events::Queue queue;
    queue.begin();
    queue.enqueue(makeEvent(1, "old-boot"));
    queue.enqueue(makeEvent(2, "old-boot"));
    queue.enqueue(makeEvent(1, "new-boot"));
    char json[4096];
    events::Batch batch;
    TEST_ASSERT_TRUE(events::buildRequest(queue, json, sizeof(json), batch));
    TEST_ASSERT_EQUAL_UINT8(2, batch.count);
    JsonDocument doc;
    TEST_ASSERT_FALSE(deserializeJson(doc, json));
    TEST_ASSERT_EQUAL_STRING("old-boot", doc["bootId"]);
    TEST_ASSERT_EQUAL(2, doc["events"].size());
    TEST_ASSERT_EQUAL_STRING("FACE_CHANGED", doc["events"][0]["type"]);
    TEST_ASSERT_EQUAL_STRING("WORK", doc["events"][0]["face"]);
    TEST_ASSERT_EQUAL_INT64(1501000, doc["events"][0]["state"]["endsAt"].as<int64_t>());
}

void test_terminal_prefix_accepts_every_protocol_terminal_result() {
    events::Batch batch;
    batch.count = 3;
    strcpy(batch.eventIds[0], "a");
    strcpy(batch.eventIds[1], "b");
    strcpy(batch.eventIds[2], "c");
    const char* response = R"({"results":[{"eventId":"a","result":"APPLIED"},{"eventId":"b","result":"DUPLICATE"},{"eventId":"c","result":"REJECTED"}]})";
    TEST_ASSERT_EQUAL_UINT8(3, events::terminalPrefix(response, batch));
    const char* partial = R"({"results":[{"eventId":"a","result":"IGNORED"},{"eventId":"c","result":"STALE"}]})";
    TEST_ASSERT_EQUAL_UINT8(1, events::terminalPrefix(partial, batch));
}

void test_expiration_only_drops_old_timestamped_prefix() {
    constexpr int64_t day = 24LL * 60 * 60 * 1000;
    events::Queue queue;
    queue.begin();
    queue.enqueue(makeEvent(1, "boot", 1000));
    queue.enqueue(makeEvent(2, "boot", timer::NONE));
    queue.enqueue(makeEvent(3, "boot", 1000));
    TEST_ASSERT_EQUAL_UINT8(1, queue.dropExpired(31 * day));
    TEST_ASSERT_EQUAL_UINT32(2, queue.front()->seq);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_queue_survives_restart_in_oldest_first_order);
    RUN_TEST(test_full_queue_keeps_newest_50_events);
    RUN_TEST(test_batch_never_mixes_boot_ids_and_has_complete_state);
    RUN_TEST(test_terminal_prefix_accepts_every_protocol_terminal_result);
    RUN_TEST(test_expiration_only_drops_old_timestamped_prefix);
    return UNITY_END();
}
