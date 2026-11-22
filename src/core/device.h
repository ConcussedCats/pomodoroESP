#pragma once
#include <stdint.h>

// Identity and small system helpers.
namespace device {

// eFuse MAC as 12 upper-case hex chars, e.g. "A1B2C3D4E5F6" (FIRMWARE-SPEC 3.2).
const char* hardwareId();

// Random UUID v4 from the hardware RNG.
void newUuid(char out[37]);

// Milliseconds since boot, 64 bit (does not wrap like millis()).
int64_t monotonicMs();

}  // namespace device
