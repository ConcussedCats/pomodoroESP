#pragma once

#include <stdint.h>

#include <functional>

#include "faces/faces.h"
#include "timer/timer_core.h"

namespace events {

// The persistent queue is capped by protocol requirements even though its dedicated
// NVS partition has spare room for wear levelling and NVS metadata.
constexpr uint8_t CAPACITY = 50;
constexpr uint8_t RECORD_VERSION = 1;

struct Event {
    uint8_t version = RECORD_VERSION;
    char bootId[37] = "";
    char eventId[37] = "";
    uint32_t seq = 0;
    timer::EventType type = timer::EventType::StateSync;
    faces::Face face = faces::Face::Unknown;
    int64_t occurredAt = timer::NONE;
    timer::State state;
    int64_t endsAt = timer::NONE;
};

struct Persistence {
    std::function<bool(uint8_t& head, uint8_t& count)> loadMeta;
    std::function<void(uint8_t head, uint8_t count)> saveMeta;
    std::function<bool(uint8_t slot, Event& event)> loadSlot;
    std::function<void(uint8_t slot, const Event& event)> saveSlot;
};

class Queue {
public:
    explicit Queue(Persistence persistence = {});

    void begin();
    void enqueue(const Event& event);
    const Event* at(uint8_t offset) const;
    const Event* front() const { return at(0); }
    void removeOldest(uint8_t count);
    // Drops timestamped events older than 30 days. Null timestamps cannot be aged safely.
    uint8_t dropExpired(int64_t serverNow);
    void clear();

    uint8_t size() const { return count_; }
    uint32_t revision() const { return revision_; }
    uint32_t droppedWhenFull() const { return droppedWhenFull_; }

private:
    bool valid(const Event& event) const;
    uint8_t slotAt(uint8_t offset) const { return uint8_t((head_ + offset) % CAPACITY); }
    void persistMeta();

    Persistence persistence_;
    Event records_[CAPACITY];
    uint8_t head_ = 0;
    uint8_t count_ = 0;
    uint32_t revision_ = 1;
    uint32_t droppedWhenFull_ = 0;
};

}  // namespace events
