/*
BLE Device Classifier

MIT License

Copyright (c) 2026 Danny McClelland

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.

Based on https://github.com/dannymcc/bluehood and https://github.com/NordicSemiconductor/bluetooth-numbers-database
*/

#ifndef BLE_CLASSIFIER_H
#define BLE_CLASSIFIER_H

#include <Arduino.h>
#include "Config.h"

// Note: ACTIVE_BT_SCANS is defined in Config.h
// (No local default needed since Config.h is always included)

// Device type constants
#define TYPE_PHONE "phone"
#define TYPE_TABLET "tablet"
#define TYPE_LAPTOP "laptop"
#define TYPE_COMPUTER "computer"
#define TYPE_WATCH "watch"
#define TYPE_HEADPHONES "audio"
#define TYPE_SPEAKER "speaker"
#define TYPE_TV "tv"
#define TYPE_VEHICLE "vehicle"
#define TYPE_SMART_HOME "smart"
#define TYPE_WEARABLE "wearable"
#define TYPE_GAMING "gaming"
#define TYPE_CAMERA "camera"
#define TYPE_PRINTER "printer"
#define TYPE_NETWORK "network"
#define TYPE_UNKNOWN "unknown"

/**
 * Classify a device based on its advertised service UUID
 * @param serviceUUID The service UUID in string format (e.g., "0x180d" or "0000180d-0000-1000-8000-00805f9b34fb")
 * @return Device type constant or nullptr if no match
 */
const char* classifyByServiceUUID(const char* serviceUUID);

/**
 * Get human-readable label for device type
 * @param deviceType Device type constant
 * @return Label string (e.g., "Phone")
 */
const char* getDeviceTypeLabel(const char* deviceType);

/**
 * Get manufacturer name from manufacturer data
 * @param manufacturerData The raw manufacturer data
 * @param dataLength Length of the manufacturer data
 * @return Manufacturer name or nullptr if unknown
 */
const char* getManufacturerName(const char* manufacturerData, size_t dataLength);

/**
 * Get human-readable name for a service UUID
 * @param serviceUUID The service UUID string (e.g., "0x180d" or "0000180d-0000-1000-8000-00805f9b34fb")
 * @return Service name or nullptr if unknown
 */
const char* getServiceName(const char* serviceUUID);

#endif // BLE_CLASSIFIER_H
