#pragma once
#include <stddef.h>
#include <stdint.h>

#include "faces/faces.h"
#include "timer/snapshot.h"

// NVS namespace "telos" (FIRMWARE-SPEC 3.1). Plain NVS: flash encryption is not enabled on
// this pet project (it would burn eFuses). The Wi-Fi password and the device secret are
// still never written to the log.
namespace storage {

bool begin();

uint32_t bumpBootCounter();  // +1 per boot, returns the new value

bool loadTimer(timer::Saved& out);
// Writes only when the bytes differ from the last write (flash wear).
void saveTimer(const timer::Saved& s);

bool loadWifi(char ssid[33], char password[65]);
void saveWifi(const char* ssid, const char* password);
void clearWifi();

// Calibration of the physical cube: face per board orientation, screen rotation per orientation.
bool loadFaceMap(faces::Face out[6]);
void saveFaceMap(const faces::Face table[6]);
bool loadRotations(int8_t out[6]);
void saveRotations(const int8_t table[6]);
void clearCalibration();

// Factory reset (stage 9): erases the whole namespace.
void wipe();

}  // namespace storage
