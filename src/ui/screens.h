#pragma once
#include <stdint.h>

#include "faces/faces.h"
#include "timer/timer_core.h"

// Screens on the ST7789 (FIRMWARE-SPEC 3.10). Redraws the static part only when the
// screen or the timer phase changes, and the numbers once per second, so nothing flickers.
namespace screens {

enum class Screen : uint8_t { Main, Info, Tech };

struct View {
    Screen screen;
    const timer::State* state;
    uint32_t remainingSeconds;
    faces::Face face;
    bool sensorOk;
    uint32_t eventsThisBoot;
    const char* toast;  // short message on the bottom line, nullptr = none
};

void begin();
void render(const View& v);
// Forces a full redraw on the next render (after a toast or a manual screen switch).
void invalidate();

// Turns the picture with the cube: each tilt has its own rotation (0..3 = 0/90/180/270 deg).
// Lying flat or upside down gives no "down" direction on the screen, so the last rotation stays.
void followOrientation(faces::Orientation o);
uint8_t rotation();

Screen forFace(faces::Face face);  // DISPLAY -> Info, TECH -> Tech, others -> Main

}  // namespace screens
