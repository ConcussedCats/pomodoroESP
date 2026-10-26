#include "ui/buzzer.h"

#include <Arduino.h>

#include "pins.h"

namespace buzzer {
namespace {

struct Note {
    uint16_t freqHz;  // 0 = silence
    uint16_t ms;
};

constexpr uint8_t MAX_NOTES = 6;

constexpr Note UP[] = {{2000, 90}, {0, 30}, {2700, 90}, {0, 30}, {3400, 140}};
constexpr Note DOWN[] = {{3400, 90}, {0, 30}, {2700, 90}, {0, 30}, {2000, 160}};

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
bool isEnabled = true;
Note melody[MAX_NOTES];
uint8_t length = 0;
uint8_t noteIndex = 0;
uint32_t noteStartMs = 0;
bool started = false;
uint16_t currentFreq = 0;

void setTone(uint16_t freq) {
    if (freq == currentFreq) return;
    currentFreq = freq;
    ledcWriteTone(LEDC_CH_BUZZER, freq);  // freq 0 -> duty 0, silent
}

void start(const Note* notes, uint8_t count) {
    portENTER_CRITICAL(&lock);
    if (isEnabled) {
        length = count > MAX_NOTES ? MAX_NOTES : count;
        for (uint8_t i = 0; i < length; i++) melody[i] = notes[i];
        noteIndex = 0;
        started = false;
    }
    portEXIT_CRITICAL(&lock);
}

}  // namespace

void begin() {
    ledcSetup(LEDC_CH_BUZZER, BASE_FREQ_HZ, 10);
    ledcAttachPin(PIN_BUZZER, LEDC_CH_BUZZER);
    ledcWrite(LEDC_CH_BUZZER, 0);
}

void setEnabled(bool value) {
    portENTER_CRITICAL(&lock);
    isEnabled = value;
    if (!value) length = 0;
    portEXIT_CRITICAL(&lock);
}

bool enabled() {
    return isEnabled;
}

void beep(uint16_t ms, uint16_t freqHz) {
    const Note note{freqHz, ms};
    start(&note, 1);
}

void playUp() {
    start(UP, sizeof(UP) / sizeof(UP[0]));
}

void playDown() {
    start(DOWN, sizeof(DOWN) / sizeof(DOWN[0]));
}

void update(uint32_t nowMs) {
    uint16_t freq = 0;
    portENTER_CRITICAL(&lock);
    if (noteIndex < length) {
        if (!started) {
            started = true;
            noteStartMs = nowMs;
        } else if (nowMs - noteStartMs >= melody[noteIndex].ms) {
            noteIndex++;
            noteStartMs = nowMs;
        }
        if (noteIndex < length) freq = melody[noteIndex].freqHz;
    }
    portEXIT_CRITICAL(&lock);
    setTone(freq);
}

}  // namespace buzzer
