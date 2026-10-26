// Part test 2: buttons S1 (GPIO19) and S2 (GPIO17), to GND with INPUT_PULLUP.
// The RGB LED (checked in part test 1) shows what the firmware sees:
//   S1 held = red, S2 held = green, both = yellow,
//   held 3 s = blue, held 10 s = purple, short press released = short white flash.
// Serial: every gesture with hold time and the number of raw contact bounces.
#include <Arduino.h>

#include "buttons/buttons.h"
#include "pins.h"
#include "ui/rgb_led.h"

namespace {

struct Stats {
    int pin;
    bool rawLast = false;
    bool pressed = false;
    uint32_t pressStartMs = 0;
    uint32_t rawEdges = 0;  // raw level changes during one press (bounce)
    bool held3s = false;
    bool held10s = false;
};

Stats stats[2] = {{PIN_BUTTON_1}, {PIN_BUTTON_2}};
uint32_t flashUntilMs = 0;

void sampleRaw() {
    for (auto& s : stats) {
        const bool raw = digitalRead(s.pin) == LOW;
        if (raw != s.rawLast) {
            s.rawLast = raw;
            s.rawEdges++;
        }
    }
}

void showLed(uint32_t now) {
    if (now < flashUntilMs) {
        rgb::show({255, 255, 255});
        return;
    }
    const Stats& a = stats[0];
    const Stats& b = stats[1];
    const Stats* held = a.pressed ? &a : (b.pressed ? &b : nullptr);
    if (held && held->held10s) {
        rgb::show({180, 0, 255});
    } else if (held && held->held3s) {
        rgb::show({0, 0, 255});
    } else if (a.pressed && b.pressed) {
        rgb::show({255, 160, 0});
    } else if (a.pressed) {
        rgb::show({255, 0, 0});
    } else if (b.pressed) {
        rgb::show({0, 255, 0});
    } else {
        rgb::show(rgb::OFF);
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("=== Telos part test 2: buttons S1 (GPIO19), S2 (GPIO17) ===");
    Serial.println("LED: S1 red, S2 green, both yellow, 3 s blue, 10 s purple");

    rgb::begin();
    rgb::setBrightness(100);
    // LED self check first, so a loose LED leg is not mistaken for a button problem.
    const rgb::Color check[] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
    const char* names[] = {"RED", "GREEN", "BLUE"};
    for (int i = 0; i < 3; i++) {
        Serial.printf("LED check: %s\n", names[i]);
        rgb::show(check[i]);
        rgb::update(millis());
        delay(1000);
    }
    rgb::show(rgb::OFF);
    rgb::update(millis());
    buttons::begin();
    for (auto& s : stats) s.rawLast = digitalRead(s.pin) == LOW;
    Serial.printf("idle levels: S1 %s, S2 %s (both should be HIGH = released)\n",
                  digitalRead(PIN_BUTTON_1) ? "HIGH" : "LOW", digitalRead(PIN_BUTTON_2) ? "HIGH" : "LOW");
}

void loop() {
    const uint32_t now = millis();
    sampleRaw();

    for (int i = 0; i < 2; i++) {
        Stats& s = stats[i];
        const bool pressed = buttons::isPressed(static_cast<buttons::Id>(i));
        if (pressed && !s.pressed) {
            s.pressStartMs = now;
            s.held3s = s.held10s = false;
            Serial.printf("%s down   raw edges %lu (1 = clean contact)\n", i == 0 ? "S1" : "S2",
                          (unsigned long)s.rawEdges);
            s.rawEdges = 0;
        }
        s.pressed = pressed;
    }

    buttons::Event e;
    while (buttons::poll(now, e)) {
        Stats& s = stats[static_cast<int>(e.button)];
        const uint32_t held = now - s.pressStartMs;
        switch (e.gesture) {
            case buttons::Gesture::HoldStart3s: s.held3s = true; break;
            case buttons::Gesture::VeryLong: s.held10s = true; break;
            case buttons::Gesture::Short: flashUntilMs = now + 120; break;
            default: break;
        }
        Serial.printf("%s %-8s held %5lu ms, raw edges since down %lu (1 = clean)\n", buttons::name(e.button),
                      buttons::name(e.gesture), (unsigned long)held, (unsigned long)s.rawEdges);
        if (e.gesture == buttons::Gesture::Short || e.gesture == buttons::Gesture::Long) s.rawEdges = 0;
    }

    showLed(now);
    rgb::update(now);
    delay(1);
}
