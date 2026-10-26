#pragma once
#include <TFT_eSPI.h>

// ST7789 240x240 (SPI mode 3, no CS). Pins and driver come from build flags in
// platformio.ini. The backlight is wired to 3.3 V, so it cannot be dimmed or turned off.
namespace display {

void begin();
TFT_eSPI& tft();

}  // namespace display
