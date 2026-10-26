#pragma once
#include <stdint.h>

// Vibration motor via the HW-517 MOSFET module (LEDC PWM). Non-blocking:
// pulse() queues a pattern, update() drives it from the UI loop.
namespace vibration {

void begin();
void setEnabled(bool enabled);  // config hardware.vibrationEnabled
bool enabled();

// count pulses of onMs with offMs pauses. Replaces a pattern that is still playing.
void pulse(uint8_t count, uint16_t onMs, uint16_t offMs = 150, uint8_t strengthPercent = 100);

inline void phaseStart() { pulse(1, 200); }        // start of a phase
inline void phaseEnd() { pulse(3, 250, 200); }     // end of a phase
inline void ack() { pulse(1, 60); }                // command from the website applied

void update(uint32_t nowMs);

}  // namespace vibration
