#pragma once
#include <stdint.h>

// Passive piezo through NPN Q3, tone from LEDC. Volume is fixed (on/off only).
// Non-blocking: play*() starts a melody, update() advances it from the UI loop.
namespace buzzer {

constexpr uint16_t BASE_FREQ_HZ = 2700;  // around the piezo resonance

void begin();
void setEnabled(bool enabled);  // config hardware.soundEnabled
bool enabled();

void beep(uint16_t ms = 80, uint16_t freqHz = BASE_FREQ_HZ);
void playUp();    // start of work (like timer_sound_up on the website)
void playDown();  // end of a phase / break (like timer_sound_down)

void update(uint32_t nowMs);

}  // namespace buzzer
