// Part test 4: vibration motor M1 via the HW-517 MOSFET module, PWM on GPIO16.
// The motor is powered from the LM2596 output (set it to ~3.0-3.3 V first!).
// One pulse right after start (no button needed). Then:
//   S2 short = next mode, S1 short = play the pattern of the mode, LED colour = mode
//   mode 1 RED    - phase start: 1 pulse 200 ms
//   mode 2 GREEN  - phase end: 3 pulses 250 ms
//   mode 3 BLUE   - command ack: 1 very short pulse 60 ms
//   mode 4 WHITE  - strength: 3 pulses of 600 ms at 30 %, 60 %, 100 % PWM
// Serial keys 1..4 select a mode, space plays.
#include <Arduino.h>

#include "buttons/buttons.h"
#include "ui/rgb_led.h"
#include "ui/vibration.h"

namespace {

constexpr rgb::Color MODE_COLORS[] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}, {255, 255, 255}};
const char* MODE_NAMES[] = {"phase start: 1 x 200 ms", "phase end: 3 x 250 ms", "ack: 1 x 60 ms",
                            "strength: 30 / 60 / 100 %"};
constexpr uint8_t MODE_COUNT = 4;

constexpr uint8_t STRENGTHS[] = {30, 60, 100};
constexpr uint32_t STRENGTH_PULSE_MS = 600;
constexpr uint32_t STRENGTH_STEP_MS = 1200;

uint8_t mode = 0;
int8_t strengthStep = -1;  // -1 = not running
uint32_t strengthStepMs = 0;

void selectMode(uint8_t m) {
    mode = m;
    strengthStep = -1;
    rgb::show(MODE_COLORS[m]);
    Serial.printf("mode %u: %s  (S1 = play, S2 = next mode)\n", unsigned(m + 1), MODE_NAMES[m]);
}

void play(uint32_t now) {
    Serial.printf("play: %s\n", MODE_NAMES[mode]);
    switch (mode) {
        case 0: vibration::phaseStart(); break;
        case 1: vibration::phaseEnd(); break;
        case 2: vibration::ack(); break;
        case 3:
            strengthStep = 0;
            strengthStepMs = now - STRENGTH_STEP_MS;  // first pulse right away
            break;
    }
}

void updateStrength(uint32_t now) {
    if (strengthStep < 0 || now - strengthStepMs < STRENGTH_STEP_MS) return;
    if (strengthStep >= int8_t(sizeof(STRENGTHS))) {
        strengthStep = -1;
        Serial.println("strength done - is 30 % still felt? is 100 % clearly stronger?");
        return;
    }
    Serial.printf("  pulse %d: %u %%\n", strengthStep + 1, STRENGTHS[strengthStep]);
    vibration::pulse(1, STRENGTH_PULSE_MS, 0, STRENGTHS[strengthStep]);
    strengthStepMs = now;
    strengthStep++;
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("=== Telos part test 4: vibration motor (GPIO16 -> HW-517 -> M1) ===");
    rgb::begin();
    rgb::setBrightness(60);
    vibration::begin();
    buttons::begin();

    Serial.println("startup pulse: 300 ms");
    vibration::pulse(1, 300);
    selectMode(0);
}

void loop() {
    const uint32_t now = millis();

    buttons::Event e;
    while (buttons::poll(now, e)) {
        Serial.printf("button %s %s\n", buttons::name(e.button), buttons::name(e.gesture));
        if (e.gesture != buttons::Gesture::Short) continue;
        if (e.button == buttons::Id::S1) play(now);
        else selectMode((mode + 1) % MODE_COUNT);
    }
    while (Serial.available()) {
        const char key = char(Serial.read());
        if (key >= '1' && key <= '4') selectMode(key - '1');
        if (key == ' ') play(now);
    }

    updateStrength(now);
    vibration::update(now);
    rgb::update(now);
    delay(2);
}
