#pragma once
#include <stdint.h>

#include "faces/mpu6050.h"

// Cube face from the MPU6050 gravity vector. Runs in its own FreeRTOS task (50 Hz).
// A face is accepted when one axis has |g| > 0.8 g, clearly dominates the other axes
// and the whole reading stays the same for >= 600 ms (FIRMWARE-SPEC 3.8).
namespace faces {

// CamelCase values: Arduino.h defines DISPLAY as a macro. Server names come from name().
enum class Face : uint8_t { Work, ShortBreak, LongBreak, Pause, Display, Tech, Unknown };

// Board orientation = which axis points up (towards the sky) and its sign.
enum class Orientation : uint8_t { PosX, NegX, PosY, NegY, PosZ, NegZ, None };

constexpr uint32_t STABLE_MS = 600;
constexpr float AXIS_MIN_G = 0.8f;      // dominant axis must exceed this
constexpr float AXIS_MARGIN_G = 0.35f;  // ...and beat the second axis by this much
constexpr float NORM_MIN_G = 0.75f;     // reject readings while the cube is moving
constexpr float NORM_MAX_G = 1.25f;

struct Reading {
    bool sensorOk = false;
    mpu6050::Accel accel{0, 0, 0};
    Orientation candidate = Orientation::None;  // what the sensor says right now
    uint32_t candidateSinceMs = 0;
    Orientation stable = Orientation::None;      // last accepted orientation
    Face face = Face::Unknown;                   // stable orientation mapped to a face
    uint32_t changes = 0;                        // +1 on every accepted face change
};

// Starts the sensor task. Returns false if the MPU6050 did not answer (the task keeps
// retrying in the background, so the sensor may appear later).
bool begin();
Reading read();

// Orientation -> face table. Default until calibration (TECH screen, stored in NVS later).
void setMapping(const Face table[6]);

const char* name(Face face);
const char* name(Orientation o);

}  // namespace faces
