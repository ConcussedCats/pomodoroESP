// Part test 3: passive piezo J1 via NPN Q3, tone on GPIO13 (LEDC).
// Buttons (part test 2) drive the test, the LED (part test 1) shows the mode:
//   S2 short = next mode, S1 short = play the sound of the mode.
//   mode 1 RED    - single beep 2.7 kHz
//   mode 2 GREEN  - 8 numbered beeps 3800..5900 Hz (find the loudest one)
//   mode 3 BLUE   - melodies: start of work (up) / end of phase (down), alternating
// Serial keys 1/2/3 select a mode, space plays.
#include <Arduino.h>

#include "buttons/buttons.h"
#include "pins.h"
#include "ui/buzzer.h"
#include "ui/rgb_led.h"

namespace {

constexpr uint16_t SWEEP_START_HZ = 3800;
constexpr uint16_t SWEEP_STEP_HZ = 300;
constexpr uint8_t SWEEP_COUNT = 8;
constexpr uint32_t SWEEP_BEEP_MS = 300;
constexpr uint32_t SWEEP_GAP_MS = 500;

constexpr rgb::Color MODE_COLORS[] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
const char* MODE_NAMES[] = {"single beep (base frequency)", "sweep: 8 beeps 3800..5900 Hz", "melodies up / down"};

uint8_t mode = 0;
bool nextUp = true;

// Sweep state (non-blocking).
int8_t sweepIndex = -1;  // -1 = not running
uint32_t sweepStepMs = 0;

void selectMode(uint8_t m) {
    mode = m;
    sweepIndex = -1;
    rgb::show(MODE_COLORS[m]);
    Serial.printf("mode %u: %s  (S1 = play, S2 = next mode)\n", unsigned(m + 1), MODE_NAMES[m]);
}

void play(uint32_t now) {
    switch (mode) {
        case 0:
            Serial.printf("beep %u Hz, 150 ms\n", unsigned(buzzer::BASE_FREQ_HZ));
            buzzer::beep(150);
            break;
        case 1:
            Serial.println("sweep start");
            sweepIndex = 0;
            sweepStepMs = now - SWEEP_GAP_MS;  // first beep right away
            break;
        case 2:
            Serial.println(nextUp ? "melody UP (start of work)" : "melody DOWN (end of phase)");
            nextUp ? buzzer::playUp() : buzzer::playDown();
            nextUp = !nextUp;
            break;
    }
}

void updateSweep(uint32_t now) {
    if (sweepIndex < 0 || now - sweepStepMs < SWEEP_GAP_MS) return;
    if (sweepIndex >= SWEEP_COUNT) {
        sweepIndex = -1;
        rgb::show(MODE_COLORS[mode]);
        Serial.println("sweep done - which number was the loudest?");
        return;
    }
    const uint16_t freq = SWEEP_START_HZ + sweepIndex * SWEEP_STEP_HZ;
    Serial.printf("  beep #%d: %u Hz\n", sweepIndex + 1, freq);
    buzzer::beep(SWEEP_BEEP_MS, freq);
    rgb::show(sweepIndex % 2 ? MODE_COLORS[1] : rgb::Color{255, 255, 255});  // flash per beep to help counting
    sweepStepMs = now;
    sweepIndex++;
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("=== Telos part test 3: piezo buzzer (GPIO13 -> NPN -> J1) ===");
    rgb::begin();
    rgb::setBrightness(60);
    buzzer::begin();
    buttons::begin();

    // Three beeps without any button, LED flashes white with each one.
    for (int i = 1; i <= 3; i++) {
        Serial.printf("startup beep %d/3 (2700 Hz)\n", i);
        rgb::show({255, 255, 255});
        rgb::update(millis());
        ledcWriteTone(LEDC_CH_BUZZER, 2700);
        delay(200);
        ledcWriteTone(LEDC_CH_BUZZER, 0);
        rgb::show(rgb::OFF);
        rgb::update(millis());
        delay(800);
    }
    selectMode(0);
}

void loop() {
    const uint32_t now = millis();

    buttons::Event e;
    while (buttons::poll(now, e)) {
        Serial.printf("button %s %s\n", buttons::name(e.button), buttons::name(e.gesture));
        if (e.gesture != buttons::Gesture::Short) continue;
        if (e.button == buttons::Id::S1) play(now);
        else selectMode((mode + 1) % 3);
    }
    while (Serial.available()) {
        const char key = char(Serial.read());
        if (key >= '1' && key <= '3') selectMode(key - '1');
        if (key == ' ') play(now);
    }

    updateSweep(now);
    buzzer::update(now);
    rgb::update(now);
    delay(2);
}
