#include "faces/mpu6050.h"

#include <Arduino.h>
#include <Wire.h>

#include "pins.h"

namespace mpu6050 {
namespace {

constexpr uint8_t REG_SMPLRT_DIV = 0x19;
constexpr uint8_t REG_CONFIG = 0x1A;
constexpr uint8_t REG_ACCEL_CONFIG = 0x1C;
constexpr uint8_t REG_ACCEL_XOUT_H = 0x3B;
constexpr uint8_t REG_PWR_MGMT_1 = 0x6B;
constexpr uint8_t REG_PWR_MGMT_2 = 0x6C;
constexpr uint8_t REG_WHO_AM_I = 0x75;

constexpr float LSB_PER_G = 16384.0f;  // ±2 g

uint8_t lastWhoAmI = 0;
bool wireStarted = false;

bool writeReg(uint8_t reg, uint8_t value) {
    Wire.beginTransmission(I2C_ADDRESS);
    Wire.write(reg);
    Wire.write(value);
    return Wire.endTransmission() == 0;
}

bool readRegs(uint8_t reg, uint8_t* buf, uint8_t len) {
    Wire.beginTransmission(I2C_ADDRESS);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) return false;
    if (Wire.requestFrom(I2C_ADDRESS, len) != len) return false;
    for (uint8_t i = 0; i < len; i++) buf[i] = Wire.read();
    return true;
}

}  // namespace

bool begin() {
    if (!wireStarted) {
        Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL, 400000);
        Wire.setTimeOut(20);
        wireStarted = true;
    }
    if (!writeReg(REG_PWR_MGMT_1, 0x80)) return false;  // device reset
    delay(100);
    if (!readRegs(REG_WHO_AM_I, &lastWhoAmI, 1)) return false;

    return writeReg(REG_PWR_MGMT_1, 0x00)       // wake up, internal 8 MHz clock
        && writeReg(REG_PWR_MGMT_2, 0x07)       // gyro X/Y/Z standby, accel on
        && writeReg(REG_CONFIG, 0x04)           // DLPF: accel 21 Hz
        && writeReg(REG_SMPLRT_DIV, 19)         // 1 kHz / (1 + 19) = 50 Hz
        && writeReg(REG_ACCEL_CONFIG, 0x00);    // ±2 g
}

bool read(Accel& out) {
    uint8_t buf[6];
    if (!readRegs(REG_ACCEL_XOUT_H, buf, sizeof(buf))) return false;
    out.x = int16_t((buf[0] << 8) | buf[1]) / LSB_PER_G;
    out.y = int16_t((buf[2] << 8) | buf[3]) / LSB_PER_G;
    out.z = int16_t((buf[4] << 8) | buf[5]) / LSB_PER_G;
    return true;
}

uint8_t whoAmI() {
    return lastWhoAmI;
}

}  // namespace mpu6050
