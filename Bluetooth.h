#pragma once

#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEAdvertisedDevice.h>
#include <WS2812B.h>
#include "Config.h"

// External variables (defined in main .ino)
extern BLEScan* pBLEScan;
extern WS2812B rgbled;

bool isRandomizedMAC(const char* macAddress);
void processBluetoothDevice(BLEAdvertisedDevice& device, unsigned long currentTime, unsigned long squelchTime);
