#pragma once
#include <stdint.h>

// Buttons S1 / S2 (to GND, INPUT_PULLUP), debounced by polling.
// Proposed use (FIRMWARE-SPEC 3.11): S1 short = wake / switch screen, S1 hold 3 s = BLE
// re-provisioning, S1 hold 10 s = factory reset; S2 short = start the suggested next mode.
namespace buttons {

enum class Id : uint8_t { S1, S2 };

enum class Gesture : uint8_t {
    Short,       // released before 3 s
    HoldStart3s, // still held, 3 s reached (feedback only)
    Long,        // released between 3 s and 10 s
    VeryLong,    // 10 s reached while held (fires once, no release event after it)
};

struct Event {
    Id button;
    Gesture gesture;
};

constexpr uint32_t DEBOUNCE_MS = 40;
constexpr uint32_t LONG_MS = 3000;
constexpr uint32_t VERY_LONG_MS = 10000;

void begin();
// Call every few ms. Returns true and fills `out` when a gesture happened.
bool poll(uint32_t nowMs, Event& out);
bool isPressed(Id button);  // debounced level

const char* name(Id button);
const char* name(Gesture gesture);

}  // namespace buttons
