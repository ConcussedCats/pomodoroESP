#pragma once
#include <stdint.h>

// Minimal MPU6050 driver: only the accelerometer is needed to find the face.
// ±2 g range, DLPF ~21 Hz, gyro in standby.
namespace mpu6050 {

constexpr uint8_t I2C_ADDRESS = 0x68;

struct Accel {
    float x, y, z;  // in g
};

// Returns false if the chip does not answer. whoAmI() tells which chip answered.
bool begin();
bool read(Accel& out);
uint8_t whoAmI();  // 0x68 for a genuine MPU6050; clones answer 0x70 / 0x72 / 0x98

}  // namespace mpu6050
