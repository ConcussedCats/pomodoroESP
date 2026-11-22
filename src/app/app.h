#pragma once

// Cube firmware, stage 2: works fully offline. Faces drive the timer, the screen,
// RGB LED, vibration and sound follow it; buttons switch screens / start the next phase.
namespace app {

void setup();
void loop();

}  // namespace app
