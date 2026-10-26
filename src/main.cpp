#include <Arduino.h>

#ifdef TELOS_HW_TEST
#include "hwtest/hw_test.h"

void setup() {
    hwtest::setup();
}

void loop() {
    hwtest::loop();
}

#else
#error "Only env:hwtest exists so far (stage 1). The cube firmware comes in stage 2."
#endif
