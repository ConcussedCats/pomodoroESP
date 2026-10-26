#include "buttons/buttons.h"

#include <Arduino.h>

#include "pins.h"

namespace buttons {
namespace {

struct Button {
    int pin;
    bool pressed = false;      // debounced
    bool rawPressed = false;
    uint32_t rawChangedMs = 0;
    uint32_t pressedAtMs = 0;
    bool longNotified = false;
    bool veryLongFired = false;
};

Button items[2] = {{PIN_BUTTON_1}, {PIN_BUTTON_2}};

bool pollOne(Button& b, Id id, uint32_t now, Event& out) {
    const bool raw = digitalRead(b.pin) == LOW;
    if (raw != b.rawPressed) {
        b.rawPressed = raw;
        b.rawChangedMs = now;
    }

    if (b.rawPressed != b.pressed && now - b.rawChangedMs >= DEBOUNCE_MS) {
        b.pressed = b.rawPressed;
        if (b.pressed) {
            b.pressedAtMs = now;
            b.longNotified = false;
            b.veryLongFired = false;
        } else if (!b.veryLongFired) {
            const uint32_t held = now - b.pressedAtMs;
            out = {id, held < LONG_MS ? Gesture::Short : Gesture::Long};
            return true;
        }
        return false;
    }

    if (b.pressed && !b.veryLongFired) {
        const uint32_t held = now - b.pressedAtMs;
        if (held >= VERY_LONG_MS) {
            b.veryLongFired = true;
            out = {id, Gesture::VeryLong};
            return true;
        }
        if (held >= LONG_MS && !b.longNotified) {
            b.longNotified = true;
            out = {id, Gesture::HoldStart3s};
            return true;
        }
    }
    return false;
}

}  // namespace

void begin() {
    for (auto& b : items) {
        pinMode(b.pin, INPUT_PULLUP);
        b.rawPressed = b.pressed = digitalRead(b.pin) == LOW;
        // A button held during boot must be released first: no gesture until then.
        b.veryLongFired = b.pressed;
    }
}

bool poll(uint32_t nowMs, Event& out) {
    return pollOne(items[0], Id::S1, nowMs, out) || pollOne(items[1], Id::S2, nowMs, out);
}

bool isPressed(Id button) {
    return items[static_cast<int>(button)].pressed;
}

const char* name(Id button) {
    return button == Id::S1 ? "S1" : "S2";
}

const char* name(Gesture g) {
    switch (g) {
        case Gesture::Short: return "short";
        case Gesture::HoldStart3s: return "hold 3s";
        case Gesture::Long: return "long";
        case Gesture::VeryLong: return "hold 10s";
    }
    return "?";
}

}  // namespace buttons
