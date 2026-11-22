#pragma once
#include <stdint.h>

// RGB LED (common cathode) on three LEDC channels. Brightness 0..100 = config
// hardware.rgbBrightness. Effects are rendered by update(), called from the UI loop.
namespace rgb {

struct Color {
    uint8_t r, g, b;
};

constexpr Color OFF{0, 0, 0};
constexpr Color WORK{255, 50, 30};         // red-coral
constexpr Color SHORT_BREAK{52, 152, 219}; // blue, matches the web timer
constexpr Color LONG_BREAK{46, 204, 113};  // green, matches the web timer
constexpr Color PROVISIONING{0, 0, 255};
constexpr Color NO_WIFI{255, 160, 0};      // yellow

enum class Effect : uint8_t {
    Solid,
    Breathe,   // slow breathing (PAUSED)
    Blink,     // short blinks, then solid (FINISHED)
    BlinkSlow  // endless on/off blinking (PROVISIONING)
};

void begin();
void setBrightness(uint8_t percent);  // 0..100
uint8_t brightness();
void show(Color color, Effect effect = Effect::Solid);
// Short flash on top of the current colour (e.g. yellow "no Wi-Fi" every 5 s).
void flash(Color color, uint16_t ms);
void update(uint32_t nowMs);

}  // namespace rgb
