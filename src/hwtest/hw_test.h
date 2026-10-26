#pragma once

// Stage 1: hardware check screen (env:hwtest). Shows the live face from the MPU6050,
// raw acceleration, button levels; buttons trigger vibration, sound and RGB brightness.
namespace hwtest {

void setup();
void loop();

}  // namespace hwtest
