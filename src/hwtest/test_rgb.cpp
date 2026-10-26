// Part test 1: RGB LED (common cathode, R GPIO25, G GPIO32, B GPIO33).
//
// Auto mode loops through: pure R, G, B (checks the pin order), white, brightness steps,
// then the effects used by the firmware (breathe, blink, slow blink).
// Serial keys: r g b w = that colour at 100 %, 0 = off, 1..4 = brightness 100/60/20/5,
//              a = back to auto mode.
#include <Arduino.h>

#include "ui/rgb_led.h"

namespace {

struct Step {
    const char* expect;  // what you should see
    rgb::Color color;
    rgb::Effect effect;
    uint8_t brightness;
    uint32_t ms;
};

constexpr Step STEPS[] = {
    {"RED only (GPIO25)", {255, 0, 0}, rgb::Effect::Solid, 100, 2500},
    {"GREEN only (GPIO32)", {0, 255, 0}, rgb::Effect::Solid, 100, 2500},
    {"BLUE only (GPIO33)", {0, 0, 255}, rgb::Effect::Solid, 100, 2500},
    {"WHITE (all three)", {255, 255, 255}, rgb::Effect::Solid, 100, 2500},
    {"OFF", rgb::OFF, rgb::Effect::Solid, 100, 1500},
    {"WORK coral, brightness 100%", rgb::WORK, rgb::Effect::Solid, 100, 2000},
    {"WORK coral, brightness 60%", rgb::WORK, rgb::Effect::Solid, 60, 2000},
    {"WORK coral, brightness 20%", rgb::WORK, rgb::Effect::Solid, 20, 2000},
    {"WORK coral, brightness 5%", rgb::WORK, rgb::Effect::Solid, 5, 2000},
    {"SHORT_BREAK green", rgb::SHORT_BREAK, rgb::Effect::Solid, 60, 2000},
    {"LONG_BREAK blue", rgb::LONG_BREAK, rgb::Effect::Solid, 60, 2000},
    {"PAUSED: slow breathing (coral)", rgb::WORK, rgb::Effect::Breathe, 60, 6000},
    {"FINISHED: 3 short blinks, then solid", rgb::SHORT_BREAK, rgb::Effect::Blink, 60, 3000},
    {"PROVISIONING: blue blinking 1 Hz", rgb::PROVISIONING, rgb::Effect::BlinkSlow, 60, 4000},
    {"NO_WIFI colour (yellow)", rgb::NO_WIFI, rgb::Effect::Solid, 60, 2000},
};
constexpr size_t STEP_COUNT = sizeof(STEPS) / sizeof(STEPS[0]);

bool autoMode = true;
size_t step = 0;
uint32_t stepStartMs = 0;

void startStep(size_t i, uint32_t now) {
    step = i;
    stepStartMs = now;
    const Step& s = STEPS[i];
    rgb::setBrightness(s.brightness);
    rgb::show(s.color, s.effect);
    Serial.printf("[%2u/%u] expect: %s\n", unsigned(i + 1), unsigned(STEP_COUNT), s.expect);
}

void manual(const char* expect, rgb::Color c) {
    autoMode = false;
    rgb::show(c);
    Serial.printf("[manual] expect: %s, brightness %u%%\n", expect, rgb::brightness());
}

void handleKey(char key, uint32_t now) {
    switch (key) {
        case 'r': manual("RED", {255, 0, 0}); break;
        case 'g': manual("GREEN", {0, 255, 0}); break;
        case 'b': manual("BLUE", {0, 0, 255}); break;
        case 'w': manual("WHITE", {255, 255, 255}); break;
        case '0': manual("OFF", rgb::OFF); break;
        case '1': rgb::setBrightness(100); Serial.println("brightness 100%"); break;
        case '2': rgb::setBrightness(60); Serial.println("brightness 60%"); break;
        case '3': rgb::setBrightness(20); Serial.println("brightness 20%"); break;
        case '4': rgb::setBrightness(5); Serial.println("brightness 5%"); break;
        case 'a':
            autoMode = true;
            Serial.println("auto mode");
            startStep(0, now);
            break;
        default: break;
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("=== Telos part test 1: RGB LED ===");
    Serial.println("keys: r g b w 0 | 1..4 brightness | a auto");
    rgb::begin();
    startStep(0, millis());
}

void loop() {
    const uint32_t now = millis();
    while (Serial.available()) handleKey(char(Serial.read()), now);

    if (autoMode && now - stepStartMs >= STEPS[step].ms) {
        if (step + 1 == STEP_COUNT) Serial.println("--- loop done, starting again ---");
        startStep((step + 1) % STEP_COUNT, now);
    }
    rgb::update(now);
    delay(10);
}
