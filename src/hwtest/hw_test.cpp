#include "hwtest/hw_test.h"

#include <Arduino.h>

#include "buttons/buttons.h"
#include "faces/faces.h"
#include "ui/buzzer.h"
#include "ui/display.h"
#include "ui/rgb_led.h"
#include "ui/vibration.h"

namespace hwtest {
namespace {

constexpr uint32_t REDRAW_MS = 100;
constexpr uint8_t BRIGHTNESS_STEPS[] = {100, 60, 20, 5};

uint32_t lastRedrawMs = 0;
uint32_t lastFaceChanges = 0;
uint8_t brightnessStep = 1;  // 60 %
bool nextSoundUp = true;
rgb::Color lastModeColor = rgb::WORK;
char lastAction[40] = "-";

uint16_t to565(rgb::Color c) {
    return display::tft().color565(c.r, c.g, c.b);
}

rgb::Color faceColor(faces::Face f) {
    switch (f) {
        case faces::Face::Work: return rgb::WORK;
        case faces::Face::ShortBreak: return rgb::SHORT_BREAK;
        case faces::Face::LongBreak: return rgb::LONG_BREAK;
        case faces::Face::Pause: return lastModeColor;
        case faces::Face::Display: return {200, 200, 200};
        case faces::Face::Tech: return {160, 0, 255};
        case faces::Face::Unknown: return rgb::OFF;
    }
    return rgb::OFF;
}

void setAction(const char* text) {
    snprintf(lastAction, sizeof(lastAction), "%s", text);
    log_i("action: %s", text);
}

void onFace(faces::Face f) {
    if (f == faces::Face::Work || f == faces::Face::ShortBreak || f == faces::Face::LongBreak) {
        lastModeColor = faceColor(f);
    }
    rgb::show(faceColor(f), f == faces::Face::Pause ? rgb::Effect::Breathe : rgb::Effect::Solid);
    vibration::ack();
    char text[40];
    snprintf(text, sizeof(text), "face -> %s", faces::name(f));
    setAction(text);
}

void onButton(const buttons::Event& e) {
    using buttons::Gesture;
    using buttons::Id;
    if (e.button == Id::S1) {
        switch (e.gesture) {
            case Gesture::Short:
                vibration::phaseStart();
                setAction("S1 short: vibration x1");
                break;
            case Gesture::HoldStart3s:
                buzzer::beep(60);
                setAction("S1 3s reached");
                break;
            case Gesture::Long:
                vibration::phaseEnd();
                setAction("S1 long: vib x3 (BLE later)");
                break;
            case Gesture::VeryLong:
                buzzer::playDown();
                setAction("S1 10s: reset (stage 9)");
                break;
        }
    } else {
        switch (e.gesture) {
            case Gesture::Short:
                nextSoundUp ? buzzer::playUp() : buzzer::playDown();
                setAction(nextSoundUp ? "S2 short: sound up" : "S2 short: sound down");
                nextSoundUp = !nextSoundUp;
                break;
            case Gesture::Long: {
                brightnessStep = (brightnessStep + 1) % sizeof(BRIGHTNESS_STEPS);
                rgb::setBrightness(BRIGHTNESS_STEPS[brightnessStep]);
                char text[40];
                snprintf(text, sizeof(text), "S2 long: RGB %u%%", BRIGHTNESS_STEPS[brightnessStep]);
                setAction(text);
                break;
            }
            default:
                break;
        }
    }
}

void drawStatic() {
    TFT_eSPI& t = display::tft();
    t.fillScreen(TFT_BLACK);
    t.fillRect(0, 0, 240, 22, TFT_NAVY);
    t.setTextColor(TFT_WHITE, TFT_NAVY);
    t.setTextDatum(TL_DATUM);
    t.drawString("Telos HW test  v" FIRMWARE_VERSION, 6, 3, 2);

    t.setTextColor(TFT_DARKGREY, TFT_BLACK);
    t.drawString("face", 6, 28, 2);
    t.drawString("axis up / candidate", 6, 82, 2);
    t.drawString("accel g  x / y / z  |g|", 6, 122, 2);
    t.drawString("buttons", 6, 160, 2);
    t.drawString("last", 6, 196, 2);
}

void row(const char* text, int y, int font, uint16_t color) {
    TFT_eSPI& t = display::tft();
    t.setTextColor(color, TFT_BLACK);
    t.setTextPadding(228);
    t.drawString(text, 6, y, font);
    t.setTextPadding(0);
}

void redraw(uint32_t now) {
    const faces::Reading r = faces::read();
    char buf[64];

    if (!r.sensorOk) {
        row("MPU6050 missing", 46, 4, TFT_RED);
    } else {
        const rgb::Color c = faceColor(r.face);
        row(faces::name(r.face), 46, 4, r.face == faces::Face::Unknown ? TFT_LIGHTGREY : to565(c));
    }

    if (r.candidate != faces::Orientation::None && r.candidate != r.stable) {
        const uint32_t held = now - r.candidateSinceMs;
        snprintf(buf, sizeof(buf), "%s   cand %s %lu/%lu ms", faces::name(r.stable), faces::name(r.candidate),
                 (unsigned long)(held > faces::STABLE_MS ? faces::STABLE_MS : held),
                 (unsigned long)faces::STABLE_MS);
    } else {
        snprintf(buf, sizeof(buf), "%s   cand %s   who 0x%02X", faces::name(r.stable),
                 faces::name(r.candidate), mpu6050::whoAmI());
    }
    row(buf, 100, 2, TFT_WHITE);

    const float norm = sqrtf(r.accel.x * r.accel.x + r.accel.y * r.accel.y + r.accel.z * r.accel.z);
    snprintf(buf, sizeof(buf), "%+.2f %+.2f %+.2f  %.2f", r.accel.x, r.accel.y, r.accel.z, norm);
    row(buf, 140, 2, TFT_WHITE);

    snprintf(buf, sizeof(buf), "S1 %s   S2 %s   RGB %u%%", buttons::isPressed(buttons::Id::S1) ? "DOWN" : "up",
             buttons::isPressed(buttons::Id::S2) ? "DOWN" : "up", rgb::brightness());
    row(buf, 178, 2, TFT_WHITE);

    row(lastAction, 214, 2, TFT_YELLOW);
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(200);
    log_i("Telos cube %s, hardware test", FIRMWARE_VERSION);

    rgb::begin();
    vibration::begin();
    buzzer::begin();
    buttons::begin();
    display::begin();
    rgb::setBrightness(BRIGHTNESS_STEPS[brightnessStep]);

    // Self test: R, G, B one after another, a beep and one vibration pulse.
    TFT_eSPI& t = display::tft();
    t.setTextDatum(MC_DATUM);
    const rgb::Color steps[] = {{255, 0, 0}, {0, 255, 0}, {0, 0, 255}};
    const uint16_t screenColors[] = {TFT_RED, TFT_GREEN, TFT_BLUE};
    const char* labels[] = {"RED", "GREEN", "BLUE"};
    for (int i = 0; i < 3; i++) {
        t.fillScreen(screenColors[i]);
        t.setTextColor(TFT_WHITE, screenColors[i]);
        t.drawString(labels[i], 120, 120, 4);
        rgb::show(steps[i]);
        rgb::update(millis());
        log_i("self test: LED and screen %s", labels[i]);
        delay(500);
    }
    rgb::show(rgb::OFF);
    buzzer::beep(120);
    vibration::pulse(1, 200);

    faces::begin();
    drawStatic();
}

void loop() {
    const uint32_t now = millis();

    buttons::Event e;
    while (buttons::poll(now, e)) {
        log_i("button %s %s", buttons::name(e.button), buttons::name(e.gesture));
        onButton(e);
    }

    const faces::Reading r = faces::read();
    if (r.changes != lastFaceChanges) {
        lastFaceChanges = r.changes;
        onFace(r.face);
    }

    rgb::update(now);
    vibration::update(now);
    buzzer::update(now);

    if (now - lastRedrawMs >= REDRAW_MS) {
        lastRedrawMs = now;
        redraw(now);
    }
    delay(5);
}

}  // namespace hwtest
