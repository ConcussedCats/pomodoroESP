#include "ui/vibration.h"

#include <Arduino.h>

#include "pins.h"

namespace vibration {
namespace {

constexpr uint32_t PWM_FREQ = 1000;
constexpr uint8_t PWM_BITS = 8;

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
bool isEnabled = true;
uint8_t remaining = 0;  // pulses left, including the current one
uint16_t onMs = 0, offMs = 0;
uint8_t duty = 0;
uint32_t pulseStartMs = 0;
bool motorOn = false;

void setMotor(bool on) {
    motorOn = on;
    ledcWrite(LEDC_CH_VIBRATION, on ? duty : 0);
}

}  // namespace

void begin() {
    ledcSetup(LEDC_CH_VIBRATION, PWM_FREQ, PWM_BITS);
    ledcAttachPin(PIN_VIBRATION, LEDC_CH_VIBRATION);
    ledcWrite(LEDC_CH_VIBRATION, 0);
}

void setEnabled(bool value) {
    portENTER_CRITICAL(&lock);
    isEnabled = value;
    if (!value) remaining = 0;
    portEXIT_CRITICAL(&lock);
}

bool enabled() {
    return isEnabled;
}

void pulse(uint8_t count, uint16_t on, uint16_t off, uint8_t strengthPercent) {
    portENTER_CRITICAL(&lock);
    if (isEnabled && count > 0) {
        remaining = count;
        onMs = on;
        offMs = off;
        duty = uint8_t(255u * (strengthPercent > 100 ? 100 : strengthPercent) / 100u);
        pulseStartMs = millis();
    }
    portEXIT_CRITICAL(&lock);
}

void update(uint32_t nowMs) {
    portENTER_CRITICAL(&lock);
    bool want = false;
    if (remaining > 0) {
        const uint32_t elapsed = nowMs - pulseStartMs;
        if (elapsed < onMs) {
            want = true;
        } else if (elapsed >= uint32_t(onMs) + offMs) {
            remaining--;
            pulseStartMs = nowMs;
            want = remaining > 0;
        }
    }
    portEXIT_CRITICAL(&lock);
    if (want != motorOn) setMotor(want);
}

}  // namespace vibration
