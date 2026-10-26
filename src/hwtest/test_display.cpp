// Part test 5: ST7789 1.3" 240x240 IPS, SPI mode 3, no CS (SCK 18, MOSI 23, RST 26, DC 27).
// Backlight is wired to 3.3 V, so the screen is lit as soon as there is power.
//   S2 short = next screen, S1 short = action on the screen
//   screen 1 COLOURS  - full red / green / blue / white / black (S1 = next colour)
//   screen 2 GEOMETRY - 1 px frame, corner labels, centre cross (S1 = rotate 0..3)
//   screen 3 TEXT     - the fonts the firmware will use
//   screen 4 SPEED    - live MM:SS countdown + full-screen fill rate
// Serial keys 1..4 select a screen, space = action.
#include <Arduino.h>

#include "buttons/buttons.h"
#include "ui/display.h"

namespace {

constexpr uint8_t SCREEN_COUNT = 4;
const char* SCREEN_NAMES[] = {"colours", "geometry", "text", "speed"};

struct Fill {
    const char* name;
    uint16_t color;
    uint16_t text;
};
constexpr Fill FILLS[] = {
    {"RED", TFT_RED, TFT_WHITE},     {"GREEN", TFT_GREEN, TFT_BLACK}, {"BLUE", TFT_BLUE, TFT_WHITE},
    {"WHITE", TFT_WHITE, TFT_BLACK}, {"BLACK", TFT_BLACK, TFT_WHITE},
};
constexpr uint8_t FILL_COUNT = sizeof(FILLS) / sizeof(FILLS[0]);

uint8_t screen = 0;
uint8_t fillIndex = 0;
uint8_t rotation = 0;
uint32_t countdownStartMs = 0;
uint32_t lastSecond = UINT32_MAX;
uint32_t lastFpsMs = 0;

TFT_eSPI& t() {
    return display::tft();
}

void drawColour() {
    const Fill& f = FILLS[fillIndex];
    t().fillScreen(f.color);
    t().setTextColor(f.text, f.color);
    t().setTextDatum(MC_DATUM);
    t().drawString(f.name, 120, 110, 4);
    t().drawString("S1 = next colour", 120, 150, 2);
    Serial.printf("colour: %s\n", f.name);
}

void drawGeometry() {
    t().setRotation(rotation);
    t().fillScreen(TFT_BLACK);
    t().drawRect(0, 0, 240, 240, TFT_WHITE);       // must touch all four edges
    t().drawRect(2, 2, 236, 236, TFT_DARKGREY);
    t().drawLine(120, 100, 120, 140, TFT_YELLOW);  // centre cross
    t().drawLine(100, 120, 140, 120, TFT_YELLOW);
    t().setTextColor(TFT_WHITE, TFT_BLACK);
    t().setTextDatum(TL_DATUM);
    t().drawString("TL", 6, 6, 2);
    t().setTextDatum(TR_DATUM);
    t().drawString("TR", 233, 6, 2);
    t().setTextDatum(BL_DATUM);
    t().drawString("BL", 6, 233, 2);
    t().setTextDatum(BR_DATUM);
    t().drawString("BR", 233, 233, 2);
    t().setTextDatum(MC_DATUM);
    char buf[24];
    snprintf(buf, sizeof(buf), "rotation %u", rotation);
    t().drawString(buf, 120, 165, 2);
    t().drawString("S1 = rotate", 120, 185, 2);
    Serial.printf("geometry, rotation %u\n", rotation);
}

void drawText() {
    t().setRotation(0);
    t().fillScreen(TFT_BLACK);
    t().setTextDatum(TL_DATUM);
    t().setTextColor(TFT_DARKGREY, TFT_BLACK);
    t().drawString("font 2: Telos cube 0.1.0", 6, 6, 2);
    t().setTextColor(TFT_WHITE, TFT_BLACK);
    t().drawString("WORK  focus", 6, 30, 4);
    t().setTextColor(TFT_GREEN, TFT_BLACK);
    t().drawString("SHORT BREAK", 6, 62, 4);
    t().setTextColor(TFT_ORANGE, TFT_BLACK);
    t().setTextDatum(MC_DATUM);
    t().drawString("25:00", 120, 150, 7);
    t().setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    t().drawString("abc ABC 0123456789 :-.%", 120, 215, 2);
    Serial.println("text screen");
}

void drawSpeedStatic(uint32_t now) {
    t().setRotation(0);
    t().fillScreen(TFT_BLACK);
    t().setTextDatum(MC_DATUM);
    t().setTextColor(TFT_DARKGREY, TFT_BLACK);
    t().drawString("countdown from 25:00", 120, 20, 2);
    t().drawString("S1 = measure fill rate", 120, 220, 2);
    countdownStartMs = now;
    lastSecond = UINT32_MAX;
    Serial.println("speed screen");
}

void updateCountdown(uint32_t now) {
    const uint32_t elapsed = (now - countdownStartMs) / 1000;
    if (elapsed == lastSecond) return;
    lastSecond = elapsed;
    const uint32_t remaining = elapsed >= 1500 ? 0 : 1500 - elapsed;
    char buf[8];
    snprintf(buf, sizeof(buf), "%02lu:%02lu", (unsigned long)(remaining / 60), (unsigned long)(remaining % 60));
    t().setTextColor(TFT_WHITE, TFT_BLACK);
    t().setTextDatum(MC_DATUM);
    t().setTextPadding(t().textWidth("88:88", 7));
    t().drawString(buf, 120, 110, 7);
    t().setTextPadding(0);
}

void measureFill(uint32_t now) {
    const uint16_t colors[] = {TFT_RED, TFT_GREEN, TFT_BLUE, TFT_BLACK};
    const uint32_t start = micros();
    for (int i = 0; i < 20; i++) t().fillScreen(colors[i % 4]);
    const uint32_t us = micros() - start;
    const float fps = 20.0f * 1e6f / us;
    Serial.printf("fillScreen x20: %lu us, %.1f full frames/s\n", (unsigned long)us, fps);
    drawSpeedStatic(now);
    char buf[32];
    snprintf(buf, sizeof(buf), "fill: %.1f frames/s", fps);
    t().setTextColor(TFT_GREEN, TFT_BLACK);
    t().drawString(buf, 120, 180, 2);
    lastFpsMs = now;
}

void showScreen(uint8_t s, uint32_t now) {
    screen = s;
    Serial.printf("screen %u: %s  (S1 = action, S2 = next screen)\n", unsigned(s + 1), SCREEN_NAMES[s]);
    switch (s) {
        case 0: t().setRotation(0); drawColour(); break;
        case 1: drawGeometry(); break;
        case 2: drawText(); break;
        case 3: drawSpeedStatic(now); break;
    }
}

void action(uint32_t now) {
    switch (screen) {
        case 0: fillIndex = (fillIndex + 1) % FILL_COUNT; drawColour(); break;
        case 1: rotation = (rotation + 1) % 4; drawGeometry(); break;
        case 2: break;
        case 3: measureFill(now); break;
    }
}

}  // namespace

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.println();
    Serial.println("=== Telos part test 5: display ST7789 240x240 (SPI mode 3, no CS) ===");
    buttons::begin();
    const uint32_t start = millis();
    display::begin();
    Serial.printf("display init: %lu ms\n", (unsigned long)(millis() - start));
    showScreen(0, millis());
}

void loop() {
    const uint32_t now = millis();

    buttons::Event e;
    while (buttons::poll(now, e)) {
        Serial.printf("button %s %s\n", buttons::name(e.button), buttons::name(e.gesture));
        if (e.gesture != buttons::Gesture::Short) continue;
        if (e.button == buttons::Id::S1) action(now);
        else showScreen((screen + 1) % SCREEN_COUNT, now);
    }
    while (Serial.available()) {
        const char key = char(Serial.read());
        if (key >= '1' && key <= '4') showScreen(key - '1', now);
        if (key == ' ') action(now);
    }

    if (screen == 3) updateCountdown(now);
    delay(2);
}
