#include "ui/rgb_led.h"

#include <Arduino.h>
#include <math.h>

#include "pins.h"

namespace rgb {
namespace {

constexpr uint32_t PWM_FREQ = 5000;
constexpr uint8_t PWM_BITS = 8;
constexpr uint32_t BREATHE_PERIOD_MS = 3000;
constexpr uint32_t BLINK_PERIOD_MS = 300;
constexpr uint8_t BLINK_COUNT = 3;
constexpr uint32_t SLOW_BLINK_PERIOD_MS = 1000;

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
Color color = OFF;
Effect effect = Effect::Solid;
uint32_t effectStartMs = 0;
uint8_t brightnessPercent = 60;

void write(Color c, float level) {
    // Perceived brightness is roughly quadratic in PWM duty.
    const float scale = level * level * (brightnessPercent / 100.0f) * (brightnessPercent / 100.0f);
    ledcWrite(LEDC_CH_LED_R, lroundf(c.r * scale));
    ledcWrite(LEDC_CH_LED_G, lroundf(c.g * scale));
    ledcWrite(LEDC_CH_LED_B, lroundf(c.b * scale));
}

}  // namespace

void begin() {
    ledcSetup(LEDC_CH_LED_R, PWM_FREQ, PWM_BITS);
    ledcSetup(LEDC_CH_LED_G, PWM_FREQ, PWM_BITS);
    ledcSetup(LEDC_CH_LED_B, PWM_FREQ, PWM_BITS);
    ledcAttachPin(PIN_LED_R, LEDC_CH_LED_R);
    ledcAttachPin(PIN_LED_G, LEDC_CH_LED_G);
    ledcAttachPin(PIN_LED_B, LEDC_CH_LED_B);
    write(OFF, 0);
}

void setBrightness(uint8_t percent) {
    portENTER_CRITICAL(&lock);
    brightnessPercent = percent > 100 ? 100 : percent;
    portEXIT_CRITICAL(&lock);
}

uint8_t brightness() {
    return brightnessPercent;
}

void show(Color c, Effect e) {
    portENTER_CRITICAL(&lock);
    color = c;
    effect = e;
    effectStartMs = millis();
    portEXIT_CRITICAL(&lock);
}

void update(uint32_t nowMs) {
    portENTER_CRITICAL(&lock);
    const Color c = color;
    const Effect e = effect;
    const uint32_t elapsed = nowMs - effectStartMs;
    portEXIT_CRITICAL(&lock);

    float level = 1.0f;
    switch (e) {
        case Effect::Solid:
            break;
        case Effect::Breathe: {
            const float phase = (elapsed % BREATHE_PERIOD_MS) / float(BREATHE_PERIOD_MS);
            level = 0.15f + 0.85f * (0.5f - 0.5f * cosf(phase * 2.0f * PI));
            break;
        }
        case Effect::Blink:
            if (elapsed < BLINK_PERIOD_MS * BLINK_COUNT) {
                level = (elapsed % BLINK_PERIOD_MS) < BLINK_PERIOD_MS / 2 ? 1.0f : 0.0f;
            }
            break;
        case Effect::BlinkSlow:
            level = (elapsed % SLOW_BLINK_PERIOD_MS) < SLOW_BLINK_PERIOD_MS / 2 ? 1.0f : 0.0f;
            break;
    }
    write(c, level);
}

}  // namespace rgb
