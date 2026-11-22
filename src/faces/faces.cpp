#include "faces/faces.h"

#include <Arduino.h>
#include <math.h>

namespace faces {
namespace {

constexpr uint32_t PERIOD_MS = 20;          // 50 Hz
constexpr uint32_t REINIT_EVERY_MS = 1000;  // retry a missing / failed sensor

portMUX_TYPE lock = portMUX_INITIALIZER_UNLOCKED;
Reading shared;

// Default table until a calibration is stored in NVS (storage::saveFaceMap).
// Recorded on the breadboard rig (2026-11-22): lying flat = PAUSE, the four tilts are
// WORK / SHORT_BREAK / LONG_BREAK / DISPLAY, upside down = TECH.
constexpr Face DEFAULT_MAPPING[6] = {
    Face::Display,     // +X
    Face::Work,        // -X
    Face::LongBreak,   // +Y
    Face::ShortBreak,  // -Y
    Face::Pause,       // +Z  flat on the desk
    Face::Tech,        // -Z  upside down
};
Face mapping[6] = {DEFAULT_MAPPING[0], DEFAULT_MAPPING[1], DEFAULT_MAPPING[2],
                   DEFAULT_MAPPING[3], DEFAULT_MAPPING[4], DEFAULT_MAPPING[5]};

Orientation classify(const mpu6050::Accel& a) {
    const float v[3] = {a.x, a.y, a.z};
    const float norm = sqrtf(v[0] * v[0] + v[1] * v[1] + v[2] * v[2]);
    if (norm < NORM_MIN_G || norm > NORM_MAX_G) return Orientation::None;

    int best = 0;
    for (int i = 1; i < 3; i++) {
        if (fabsf(v[i]) > fabsf(v[best])) best = i;
    }
    float second = 0;
    for (int i = 0; i < 3; i++) {
        if (i != best) second = fmaxf(second, fabsf(v[i]));
    }
    const float strength = fabsf(v[best]);
    if (strength < AXIS_MIN_G || strength - second < AXIS_MARGIN_G) return Orientation::None;
    return static_cast<Orientation>(best * 2 + (v[best] < 0 ? 1 : 0));
}

void task(void*) {
    // begin() already tried the sensor; do not reset it again if it answered.
    portENTER_CRITICAL(&lock);
    bool sensorOk = shared.sensorOk;
    portEXIT_CRITICAL(&lock);
    uint32_t lastInitMs = millis();
    Orientation candidate = Orientation::None;
    uint32_t candidateSinceMs = millis();
    TickType_t wake = xTaskGetTickCount();

    for (;;) {
        vTaskDelayUntil(&wake, pdMS_TO_TICKS(PERIOD_MS));
        const uint32_t now = millis();

        if (!sensorOk && now - lastInitMs >= REINIT_EVERY_MS) {
            lastInitMs = now;
            sensorOk = mpu6050::begin();
            if (sensorOk) log_i("MPU6050 found, WHO_AM_I=0x%02X", mpu6050::whoAmI());
        }

        mpu6050::Accel accel{0, 0, 0};
        if (sensorOk && !mpu6050::read(accel)) {
            sensorOk = false;
            lastInitMs = now;
            log_w("MPU6050 read failed, retrying");
        }

        const Orientation now_o = sensorOk ? classify(accel) : Orientation::None;
        if (now_o != candidate) {
            candidate = now_o;
            candidateSinceMs = now;
        }

        portENTER_CRITICAL(&lock);
        shared.sensorOk = sensorOk;
        shared.accel = accel;
        shared.candidate = candidate;
        shared.candidateSinceMs = candidateSinceMs;
        if (candidate != Orientation::None && candidate != shared.stable &&
            now - candidateSinceMs >= STABLE_MS) {
            shared.stable = candidate;
            const Face face = mapping[static_cast<int>(candidate)];
            if (face != shared.face) {
                shared.face = face;
                shared.changes++;
            }
        }
        portEXIT_CRITICAL(&lock);
    }
}

}  // namespace

bool begin() {
    const bool ok = mpu6050::begin();
    if (ok) {
        log_i("MPU6050 found, WHO_AM_I=0x%02X", mpu6050::whoAmI());
    } else {
        log_e("MPU6050 not found on I2C (SDA 21, SCL 22)");
    }
    portENTER_CRITICAL(&lock);
    shared.sensorOk = ok;
    portEXIT_CRITICAL(&lock);
    // Core 1 together with the Arduino loop; the network will live on core 0.
    xTaskCreatePinnedToCore(task, "faces", 4096, nullptr, 3, nullptr, 1);
    return ok;
}

Reading read() {
    portENTER_CRITICAL(&lock);
    const Reading copy = shared;
    portEXIT_CRITICAL(&lock);
    return copy;
}

void getMapping(Face out[6]) {
    portENTER_CRITICAL(&lock);
    for (int i = 0; i < 6; i++) out[i] = mapping[i];
    portEXIT_CRITICAL(&lock);
}

void defaultMapping(Face out[6]) {
    for (int i = 0; i < 6; i++) out[i] = DEFAULT_MAPPING[i];
}

void setMapping(const Face table[6]) {
    portENTER_CRITICAL(&lock);
    for (int i = 0; i < 6; i++) mapping[i] = table[i];
    if (shared.stable != Orientation::None) {
        const Face face = mapping[static_cast<int>(shared.stable)];
        if (face != shared.face) {
            shared.face = face;
            shared.changes++;
        }
    }
    portEXIT_CRITICAL(&lock);
}

}  // namespace faces
