#pragma once

#include <stddef.h>
#include <stdint.h>

#include <functional>

#include "timer/timer_core.h"

namespace commands {

constexpr uint8_t CACHE_CAPACITY = 16;

struct Command {
    char commandId[37] = "";
    timer::CommandType type = timer::CommandType::Unknown;
    bool hasMode = false;
    timer::Mode mode = timer::Mode::Work;
    int64_t expiresAt = timer::NONE;
};

struct Ack {
    char commandId[37] = "";
    bool executed = false;
    char errorCode[32] = "";
    timer::State state;
    int64_t endsAt = timer::NONE;
};

enum class ParseResult : uint8_t { Empty, Valid, Invalid };

// The backend may return older still-open records first. Only the newest command
// is actionable; superseded commands are resolved by the server.
ParseResult parseLatest(const char* json, Command& out);
bool buildAck(const Ack& ack, char* out, size_t size);

struct CacheState {
    uint8_t version = 1;
    uint8_t count = 0;
    uint8_t next = 0;
    Ack records[CACHE_CAPACITY];
};

struct Persistence {
    std::function<bool(CacheState&)> load;
    std::function<void(const CacheState&)> save;
};

class Cache {
public:
    explicit Cache(Persistence persistence = {});
    void begin();
    const Ack* find(const char* commandId) const;
    void remember(const Ack& ack);
    void clear();
    uint8_t size() const { return state_.count; }

private:
    bool valid(const CacheState& state) const;
    Persistence persistence_;
    CacheState state_;
};

}  // namespace commands
