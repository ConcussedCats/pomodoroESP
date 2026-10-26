#include "ui/display.h"

namespace display {
namespace {
TFT_eSPI screen;
}

void begin() {
    screen.init();
    screen.setRotation(0);
    screen.fillScreen(TFT_BLACK);
}

TFT_eSPI& tft() {
    return screen;
}

}  // namespace display
