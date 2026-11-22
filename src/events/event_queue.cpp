#include "events/event_queue.h"

#include <string.h>

namespace events {
namespace {

constexpr int64_t MAX_AGE_MS = 30LL * 24 * 60 * 60 * 1000;

bool terminated(const char value[37]) {
    return memchr(value, '\0', 37) != nullptr;
}

}  // namespace

Queue::Queue(Persistence persistence) : persistence_(std::move(persistence)) {}

bool Queue::valid(const Event& event) const {
    if (event.version != RECORD_VERSION || !terminated(event.bootId) || !terminated(event.eventId) ||
        event.bootId[0] == '\0' || event.eventId[0] == '\0' || event.seq == 0) return false;
    if (static_cast<uint8_t>(event.type) > static_cast<uint8_t>(timer::EventType::StateSync)) return false;
    if (event.face > faces::Face::Unknown) return false;
    if (event.state.durationSeconds == 0 || event.state.remainingSeconds > event.state.durationSeconds) return false;
    return true;
}

void Queue::begin() {
    uint8_t savedHead = 0;
    uint8_t savedCount = 0;
    if (!persistence_.loadMeta || !persistence_.loadMeta(savedHead, savedCount) || savedHead >= CAPACITY ||
        savedCount > CAPACITY) {
        clear();
        return;
    }
    head_ = savedHead;
    count_ = 0;
    for (uint8_t i = 0; i < savedCount; i++) {
        Event event;
        if (!persistence_.loadSlot || !persistence_.loadSlot(slotAt(i), event) || !valid(event)) break;
        records_[slotAt(i)] = event;
        count_++;
    }
    if (count_ != savedCount) persistMeta();
    revision_++;
}

void Queue::persistMeta() {
    if (persistence_.saveMeta) persistence_.saveMeta(head_, count_);
}

void Queue::enqueue(const Event& event) {
    if (!valid(event)) return;
    uint8_t slot;
    if (count_ == CAPACITY) {
        slot = head_;
        head_ = uint8_t((head_ + 1) % CAPACITY);
        droppedWhenFull_++;
    } else {
        slot = slotAt(count_);
        count_++;
    }
    records_[slot] = event;
    if (persistence_.saveSlot) persistence_.saveSlot(slot, event);
    persistMeta();
    revision_++;
}

const Event* Queue::at(uint8_t offset) const {
    return offset < count_ ? &records_[slotAt(offset)] : nullptr;
}

void Queue::removeOldest(uint8_t count) {
    if (count > count_) count = count_;
    if (count == 0) return;
    head_ = uint8_t((head_ + count) % CAPACITY);
    count_ -= count;
    persistMeta();
    revision_++;
}

uint8_t Queue::dropExpired(int64_t serverNow) {
    uint8_t dropped = 0;
    while (const Event* event = front()) {
        if (event->occurredAt == timer::NONE || serverNow - event->occurredAt <= MAX_AGE_MS) break;
        removeOldest(1);
        dropped++;
    }
    return dropped;
}

void Queue::clear() {
    head_ = 0;
    count_ = 0;
    persistMeta();
    revision_++;
}

}  // namespace events
