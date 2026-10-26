#pragma once

// Pin map from the Fritzing schematic project.fzz (see FIRMWARE-SPEC.md 1.1).
// GPIO16/17 are free only on ESP32-WROOM modules (on WROVER they belong to PSRAM).

constexpr int PIN_I2C_SDA   = 21;  // MPU6050 (GY-521), I2C address 0x68, INT not wired
constexpr int PIN_I2C_SCL   = 22;

constexpr int PIN_TFT_SCK   = 18;  // ST7789 240x240, SPI mode 3, no CS, backlight tied to 3.3 V
constexpr int PIN_TFT_MOSI  = 23;  // (TFT_eSPI takes these from build flags in platformio.ini)
constexpr int PIN_TFT_RST   = 26;
constexpr int PIN_TFT_DC    = 27;
constexpr int PIN_TFT_CS    = -1;
constexpr int PIN_TFT_MISO  = 35;  // not wired; parks the SPI MISO on an unused input-only pin
                                   // (the default would be GPIO19 = button S1)

constexpr int PIN_LED_R     = 25;  // RGB LED, common cathode, 220 Ohm per leg
constexpr int PIN_LED_G     = 32;
constexpr int PIN_LED_B     = 33;

constexpr int PIN_VIBRATION = 16;  // HW-517 MOSFET module -> motor M1 (powered from LM2596)
constexpr int PIN_BUZZER    = 13;  // NPN Q3 -> passive piezo J1 (5 V), LEDC tone

constexpr int PIN_BUTTON_1  = 19;  // S1 to GND, active LOW, INPUT_PULLUP
constexpr int PIN_BUTTON_2  = 17;  // S2 to GND, active LOW, INPUT_PULLUP

// LEDC channels (Arduino-ESP32 2.x: channel pairs share a timer -> keep different
// frequencies on different pairs).
constexpr int LEDC_CH_LED_R     = 0;  // timer 0, 5 kHz
constexpr int LEDC_CH_LED_G     = 1;  // timer 0, 5 kHz
constexpr int LEDC_CH_LED_B     = 2;  // timer 1, 5 kHz
constexpr int LEDC_CH_VIBRATION = 4;  // timer 2, 1 kHz
constexpr int LEDC_CH_BUZZER    = 6;  // timer 3, tone frequency
