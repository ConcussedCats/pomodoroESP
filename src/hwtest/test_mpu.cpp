// Part test 6: MPU6050 (GY-521) on I2C SDA 21 / SCL 22, face detection.
// Uses the parts checked before: display (live values), RGB (colour per axis),
// vibration (short tick on every accepted face change), buttons.
//   Screen: which board axis points up (+X..-Z), the face from the default table,
//   raw g values, |g|, and a bar while a new orientation waits its 600 ms.
//   S1 short = print a calibration line on serial (axis -> face you name in chat).
#include <Arduino.h>
#include <math.h>

#include "buttons/buttons.h"
#include "faces/faces.h"
#include "ui/display.h"
#include "ui/rgb_led.h"
#include "ui/vibration.h"

namespace {

constexpr uint32_t REDRAW_MS = 100;
constexpr rgb::Color AXIS_COLORS[] = {
    {255, 0, 0},    // +X red
    {255, 120, 0},  // -X orange
    {0, 255, 0},    // +Y green
    {0, 255, 160},  // -Y teal
    {0, 0, 255},    // +Z blue
    {180, 0, 255},  // -Z purple
};

uint32_t lastRedrawMs = 0;
faces::Orientation lastStable = faces::Orientation::None;
uint32_t flips = 0;

TFT_eSPI& t() {
    return display::tft();
}

void row(const char* text, int x, int y, int font, uint16_t color, uint8_t datum = TL_DATUM, int pad = 228) {
    t().setTextDatum(datum);
    t().setTextColor(color, TFT_BLACK);
    t().setTextPadding(pad);
    t().drawString(text, x, y, font);
    t().setTextPadding(0);
}

void drawStatic() {
    t().fillScreen(TFT_BLACK);
    t().fillRect(0, 0, 240, 22, TFT_NAVY);
    t().setTextColor(TFT_WHITE, TFT_NAVY);
    t().setTextDatum(TL_DATUM);
    t().drawString("MPU6050 face test", 6, 3, 2);
    t().setTextColor(TFT_DARKGREY, TFT_BLACK);
    t().drawString("axis up", 6, 28, 2);
    t().drawString("face (default table)", 6, 112, 2);
    t().drawString("g  x / y / z   |g|", 6, 158, 2);
}

uint16_t to565(rgb::Color c) {
    return t().color565(c.r, c.g, c.b);
}

void redraw(uint32_t now) {
    const faces::Reading r = faces::read();
    char buf[48];

    if (!r.sensorOk) {
        row("NO SENSOR", 120, 70, 4, TFT_RED, MC_DATUM);
        row("check SDA 21 / SCL 22 / 3V3 / GND", 6, 130, 2, TFT_RED);
        return;
    }

    const bool hasStable = r.stable != faces::Orientation::None;
    const uint16_t c = hasStable ? to565(AXIS_COLORS[int(r.stable)]) : TFT_LIGHTGREY;
    row(faces::name(r.stable), 120, 72, 7, c, MC_DATUM, 150);
    row(faces::name(r.face), 6, 130, 4, TFT_WHITE);

    const float norm = sqrtf(r.accel.x * r.accel.x + r.accel.y * r.accel.y + r.accel.z * r.accel.z);
    snprintf(buf, sizeof(buf), "%+.2f %+.2f %+.2f  %.2f", r.accel.x, r.accel.y, r.accel.z, norm);
    row(buf, 6, 176, 2, TFT_WHITE);

    // Progress bar while a different orientation waits to become stable.
    const bool waiting = r.candidate != faces::Orientation::None && r.candidate != r.stable;
    const uint32_t held = waiting ? now - r.candidateSinceMs : 0;
    const int width = waiting ? int(232 * (held > faces::STABLE_MS ? faces::STABLE_MS : held) / faces::STABLE_MS) : 0;
    t().fillRect(4, 200, 232, 10, TFT_BLACK);
    t().drawRect(4, 200, 232, 10, TFT_DARKGREY);
    if (width > 0) t().fillRect(4, 200, width, 10, to565(AXIS_COLORS[int(r.candidate)]));

    snprintf(buf, sizeof(buf), "now %s  flips %lu  who 0x%02X", faces::name(r.candidate), (unsigned long)flips,
             mpu6050::whoAmI());
    row(buf, 6, 218, 2, TFT_LIGHTGREY);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("=== Telos part test 6: MPU6050 face detection (SDA 21, SCL 22) ===");
    rgb::begin();
    rgb::setBrightness(60);
    vibration::begin();
    buttons::begin();
    display::begin();
    drawStatic();

    if (faces::begin()) {
        Serial.printf("MPU6050 OK, WHO_AM_I = 0x%02X (0x68 = genuine)\n", mpu6050::whoAmI());
    } else {
        Serial.println("MPU6050 NOT FOUND - check wiring; the test keeps retrying every second");
    }
}

void loop() {
    const uint32_t now = millis();

    const faces::Reading r = faces::read();
    if (r.stable != lastStable) {
        lastStable = r.stable;
        if (r.stable != faces::Orientation::None) {
            flips++;
            rgb::show(AXIS_COLORS[int(r.stable)]);
            vibration::ack();
            Serial.printf("accepted: axis %s up -> face %s (g %+.2f %+.2f %+.2f)\n", faces::name(r.stable),
                          faces::name(r.face), r.accel.x, r.accel.y, r.accel.z);
        }
    }

    buttons::Event e;
    while (buttons::poll(now, e)) {
        if (e.button == buttons::Id::S1 && e.gesture == buttons::Gesture::Short) {
            Serial.printf("snapshot: stable %s, now %s, g %+.3f %+.3f %+.3f, sensor %s\n", faces::name(r.stable),
                          faces::name(r.candidate), r.accel.x, r.accel.y, r.accel.z, r.sensorOk ? "ok" : "missing");
        }
    }

    rgb::update(now);
    vibration::update(now);
    if (now - lastRedrawMs >= REDRAW_MS) {
        lastRedrawMs = now;
        redraw(now);
    }
    delay(5);
}
