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

#include "BLEClassifier.h"
#include <cstring>

// Service UUID patterns (normalized to 8-character hex strings)
// Format: {uuid_pattern, device_type}
struct ServiceUUIDPattern {
  const char* pattern;
  const char* deviceType;
};

static const ServiceUUIDPattern SERVICE_UUID_PATTERNS[] = {
  // Wearables / Fitness
  {"0000180d", TYPE_WEARABLE},  // Heart Rate Service
  {"0000181c", TYPE_WEARABLE},  // User Data
  {"00001814", TYPE_WEARABLE},  // Running Speed and Cadence
  {"00001816", TYPE_WEARABLE},  // Cycling Speed and Cadence
  {"00001818", TYPE_WEARABLE},  // Cycling Power
  {"0000181b", TYPE_WEARABLE},  // Body Composition
  {"0000181d", TYPE_WEARABLE},  // Weight Scale
  
  // Health devices
  {"00001810", TYPE_WEARABLE},  // Blood Pressure
  {"00001808", TYPE_WEARABLE},  // Glucose
  {"00001809", TYPE_WEARABLE},  // Health Thermometer
  
  // Audio devices (A2DP and related)
  {"0000110b", TYPE_HEADPHONES},  // A2DP Audio Sink
  {"0000110a", TYPE_HEADPHONES},  // A2DP Audio Source
  {"0000111e", TYPE_HEADPHONES},  // Handsfree
  {"0000111f", TYPE_HEADPHONES},  // Handsfree Audio Gateway
  {"00001108", TYPE_HEADPHONES},  // Headset
  {"0000110d", TYPE_HEADPHONES},  // A2DP (Advanced Audio)
  {"00001203", TYPE_HEADPHONES},  // Generic Audio
  {"0000184e", TYPE_HEADPHONES},  // Audio Stream Control
  {"0000184f", TYPE_HEADPHONES},  // Broadcast Audio Scan
  {"00001850", TYPE_HEADPHONES},  // Published Audio Capabilities
  {"00001853", TYPE_HEADPHONES},  // Common Audio
  
  // Gaming / HID
  {"00001812", TYPE_GAMING},  // Human Interface Device (keyboards, mice, controllers)
  {"00001124", TYPE_GAMING},  // HID (legacy)
  
  // Apple-specific (Continuity, AirDrop, etc.)
  {"d0611e78", TYPE_PHONE},  // Apple Continuity
  {"7905f431", TYPE_PHONE},  // Apple Notification Center
  {"89d3502b", TYPE_PHONE},  // Apple Media Service
  {"0000fd6f", TYPE_PHONE},  // Apple Continuity short UUID
  
  // Google/Android
  {"0000fe9f", TYPE_PHONE},  // Google Fast Pair
  {"0000fe2c", TYPE_PHONE},  // Google Nearby
  
  // Smart Home / IoT
  {"0000181a", TYPE_SMART_HOME},  // Environmental Sensing
  {"0000fef5", TYPE_SMART_HOME},  // Philips Hue / Dialog
  {"0000fee7", TYPE_SMART_HOME},  // Tencent IoT
  {"0000feaa", TYPE_SMART_HOME},  // Google Eddystone (beacons)
  {"0000feab", TYPE_SMART_HOME},  // Nokia beacons
  
  // Trackers / Finders
  {"0000feed", TYPE_SMART_HOME},  // Tile
  {"0000febe", TYPE_SMART_HOME},  // Bose
  {"0000feec", TYPE_SMART_HOME},  // Tile
  
  // Location/Navigation
  {"00001819", TYPE_WEARABLE},  // Location and Navigation
  
  // Watches (specific manufacturer UUIDs)
  {"cba20d00", TYPE_WATCH},  // SwitchBot
  {"0000fee0", TYPE_WATCH},  // Xiaomi Mi Band / Amazfit
  {"0000feea", TYPE_WATCH},  // Swirl Networks (wearables)
  
  // Printers
  {"00001118", TYPE_PRINTER},  // Direct Printing
  {"00001119", TYPE_PRINTER},  // Reference Printing
  
  // Camera
  {"00001822", TYPE_CAMERA},  // Camera Profile
};

static const int SERVICE_UUID_PATTERN_COUNT = sizeof(SERVICE_UUID_PATTERNS) / sizeof(ServiceUUIDPattern);

// Service UUID to human-readable name mapping
struct ServiceUUIDName {
  const char* uuid;
  const char* name;
};

static const ServiceUUIDName SERVICE_UUID_NAMES[] = {
  // Core Services
  {"00001808", "Glucose"},
  {"00001809", "Health Thermometer"},
  
  // Health & Fitness
  {"0000180d", "Heart Rate"},
  {"0000180e", "Phone Alert Status"},
  {"0000180f", "Battery Service"},
  {"00001810", "Blood Pressure"},
  {"00001811", "Alert Notification"},
  {"00001812", "Human Interface Device"},
  {"00001813", "Scan Parameters"},
  {"00001814", "Running Speed/Cadence"},
  {"00001815", "Automation IO"},
  {"00001816", "Cycling Speed/Cadence"},
  {"00001818", "Cycling Power"},
  {"00001819", "Location/Navigation"},
  {"0000181a", "Environmental Sensing"},
  {"0000181b", "Body Composition"},
  {"0000181c", "User Data"},
  {"0000181d", "Weight Scale"},
  {"0000181e", "Bond Management"},
  {"0000181f", "Continuous Glucose Monitoring"},
  {"00001820", "Internet Protocol Support"},
  {"00001821", "Indoor Positioning"},
  {"00001822", "Pulse Oximeter"},
  {"00001823", "HTTP Proxy"},
  {"00001824", "Transport Discovery"},
  {"00001825", "Object Transfer"},
  {"00001826", "Fitness Machine"},
  {"00001827", "Mesh Provisioning"},
  {"00001828", "Mesh Proxy"},
  {"00001829", "Reconnection Configuration"},
  {"0000183a", "Insulin Delivery"},
  {"0000183b", "Binary Sensor"},
  {"0000183c", "Emergency Configuration"},
  {"0000183e", "Physical Activity Monitor"},

  // Audio Services
  {"0000110a", "A2DP Audio Source"},
  {"0000110b", "A2DP Audio Sink"},
  {"0000110c", "A/V Remote Control Target"},
  {"0000110d", "Advanced Audio"},
  {"0000110e", "A/V Remote Control"},
  {"00001108", "Headset"},
  {"0000111e", "Handsfree"},
  {"0000111f", "Handsfree Audio Gateway"},
  {"00001203", "Generic Audio"},
  {"00001843", "Audio Input Control"},
  {"00001844", "Volume Control"},
  {"00001845", "Volume Offset Control"},
  {"00001846", "Coordinated Set Identification"},
  {"00001847", "Device Time"},
  {"00001848", "Media Control"},
  {"00001849", "Generic Media Control"},
  {"0000184a", "Constant Tone Extension"},
  {"0000184b", "Telephone Bearer"},
  {"0000184c", "Generic Telephone Bearer"},
  {"0000184d", "Microphone Control"},
  {"0000184e", "Audio Stream Control"},
  {"0000184f", "Broadcast Audio Scan"},
  {"00001850", "Published Audio Capabilities"},
  {"00001851", "Basic Audio Announcement"},
  {"00001852", "Broadcast Audio Announcement"},
  {"00001853", "Common Audio"},
  {"00001854", "Hearing Access"},
  {"00001855", "Telephony and Media Audio"},
  {"00001856", "Public Broadcast Announcement"},
  {"00001857", "Electronic Shelf Label"},
  {"00001858", "Gaming Audio"},
  {"00001859", "Mesh Proxy Solicitation"},
  
  // Apple Services
  {"d0611e78", "Apple Continuity"},
  {"7905f431", "Apple Notification Center"},
  {"89d3502b", "Apple Media Service"},
  {"0000fd6f", "Apple Continuity"},
  {"9fa480e0", "Apple Generic"},
  
  // Google Services
  {"0000fe9f", "Google Fast Pair"},
  {"0000fe2c", "Google Nearby"},
  {"0000feaa", "Google Eddystone"},
  
  // Other Common Services
  {"0000feed", "Tile Tracker"},
  {"0000feec", "Tile"},
  {"0000fee0", "Xiaomi Mi Band"},
  {"0000fee7", "Tencent IoT"},
  {"0000fef5", "Dialog Semiconductor"},
  {"0000fe0f", "Philips Hue"},
  {"0000fe59", "Nordic DFU"},
  {"0000febb", "Adafruit File Transfer"},
  {"00001118", "Direct Printing"},
  {"00001119", "Reference Printing"},
  {"00001124", "HID Legacy"},
};

static const int SERVICE_UUID_NAME_COUNT = sizeof(SERVICE_UUID_NAMES) / sizeof(ServiceUUIDName);

// Manufacturer ID to name mapping
struct ManufacturerName {
  uint16_t companyId;
  const char* name;
};

static const ManufacturerName MANUFACTURER_NAMES[] = {
  // Major Tech Companies
  {0x0001, "Nokia"},
  {0x0002, "Intel"},
  {0x0006, "Microsoft"},
  {0x0008, "Motorola"},
  {0x001D, "Qualcomm"},
  {0x004C, "Apple"},
  {0x0046, "MediaTek"},
  {0x005D, "Realtek"},
  {0x0075, "Samsung"},
  {0x00CC, "Beats"},
  {0x00E0, "Google"},
  {0x012D, "Sony"},
  {0x0157, "Xiaomi"},
  {0x01AB, "Meta"},
  {0x022D, "Oppo"},
  {0x0272, "OnePlus"},
  {0x02C5, "Lenovo"},
  {0x02E5, "LG"},
  {0x02ED, "HTC"},
  {0x038F, "Huawei"},
  {0x041E, "Dell"},
  {0x0599, "Motorola Solutions"},
  {0x067C, "Tile"},
  {0x089A, "vivo"},
  {0x08CA, "Realme"},
  
  // Wearables & Fitness
  {0x0067, "Jabra"},
  {0x008D, "Garmin"},
  {0x009F, "Suunto"},
  {0x00DF, "Misfit"},
  {0x01B5, "Nest"},
  {0x01FC, "Wahoo Fitness"},
  {0x02F2, "GoPro"},
  {0x03BF, "Under Armour"},
  {0x0499, "Polar"},
  {0x06D5, "Fitbit"},
  {0x0768, "Peloton"},
  
  // Audio Equipment
  {0x0009, "Plantronics"},
  {0x004B, "Sennheiser"},
  {0x0087, "Bose"},
  {0x0103, "Bang & Olufsen"},
  {0x014F, "Bowers & Wilkins"},
  {0x0150, "Pioneer"},
  {0x0141, "Harman"},
  {0x0159, "Anker"},
  {0x0357, "Skullcandy"},
  {0x04AD, "Shure"},
  {0x05A7, "Sonos"},
  {0x0618, "Audio-Technica"},
  {0x0A26, "Yamaha"},
  
  // Smart Home & IoT
  {0x02A5, "Yeelight"},
  {0x03C4, "Amazon"},
  {0x0514, "Airthings"},
  {0x0600, "iRobot"},
  {0x06C4, "Philips"},
  {0x067F, "ecobee"},
  {0x083A, "Nanoleaf"},
  {0x0A12, "Dyson"},
  
  // Gaming
  {0x0171, "Nintendo"},
  {0x02AC, "Razer"},
  {0x046D, "Logitech"},
  {0x0590, "Valve"},
  
  // Automotive
  {0x010E, "Audi"},
  {0x011F, "Volkswagen"},
  {0x0120, "Porsche"},
  {0x017C, "Mercedes-Benz"},
  {0x020F, "Jaguar Land Rover"},
  {0x023F, "BMW"},
  {0x02D0, "Tesla"},
  {0x071B, "Ford"},
  {0x0816, "Hyundai"},
  {0x08A6, "Honda"},
  {0x0913, "Toyota"},
  {0x0939, "Rivian"},
  {0x0A10, "Subaru"},
};

static const int MANUFACTURER_NAME_COUNT = sizeof(MANUFACTURER_NAMES) / sizeof(ManufacturerName);

// Device type labels
struct DeviceTypeInfo {
  const char* type;
  const char* label;
};

static const DeviceTypeInfo DEVICE_TYPE_INFO[] = {
  {TYPE_PHONE, "Phone"},
  {TYPE_TABLET, "Tablet"},
  {TYPE_LAPTOP, "Laptop"},
  {TYPE_COMPUTER, "Computer"},
  {TYPE_WATCH, "Watch"},
  {TYPE_HEADPHONES, "Audio"},
  {TYPE_SPEAKER, "Speaker"},
  {TYPE_TV, "TV/Display"},
  {TYPE_VEHICLE, "Vehicle"},
  {TYPE_SMART_HOME, "Smart Home"},
  {TYPE_WEARABLE, "Wearable"},
  {TYPE_GAMING, "Gaming"},
  {TYPE_CAMERA, "Camera"},
  {TYPE_PRINTER, "Printer"},
  {TYPE_NETWORK, "Network"},
  {TYPE_UNKNOWN, "Unknown"},
};

static const int DEVICE_TYPE_INFO_COUNT = sizeof(DEVICE_TYPE_INFO) / sizeof(DeviceTypeInfo);

// Helper function to convert string to lowercase
static void toLowerStr(char* dest, const char* src, size_t maxLen) {
  size_t i = 0;
  while (src[i] && i < maxLen - 1) {
    dest[i] = tolower(src[i]);
    i++;
  }
  dest[i] = '\0';
}

// Helper function to remove dashes from UUID string
static void removeDashes(char* str) {
  char* src = str;
  char* dst = str;
  while (*src) {
    if (*src != '-') {
      *dst++ = *src;
    }
    src++;
  }
  *dst = '\0';
}

const char* classifyByServiceUUID(const char* serviceUUID) {
  if (!serviceUUID || strlen(serviceUUID) == 0) {
    return nullptr;
  }
  
  // Normalize UUID to lowercase and remove dashes
  char normalized[256];
  toLowerStr(normalized, serviceUUID, sizeof(normalized));
  removeDashes(normalized);
  
  // Check against patterns
  for (int i = 0; i < SERVICE_UUID_PATTERN_COUNT; i++) {
    if (strstr(normalized, SERVICE_UUID_PATTERNS[i].pattern) != nullptr) {
      return SERVICE_UUID_PATTERNS[i].deviceType;
    }
  }
  
  return nullptr;
}

const char* getDeviceTypeLabel(const char* deviceType) {
  if (!deviceType) {
    return "Unknown";
  }
  
  for (int i = 0; i < DEVICE_TYPE_INFO_COUNT; i++) {
    if (strcmp(deviceType, DEVICE_TYPE_INFO[i].type) == 0) {
      return DEVICE_TYPE_INFO[i].label;
    }
  }
  
  return "Unknown";
}

const char* getManufacturerName(const char* manufacturerData, size_t dataLength) {
  if (!manufacturerData || dataLength < 2) {
    return nullptr;
  }
  
  // Extract company ID (first 2 bytes in little-endian format)
  const uint8_t* data = reinterpret_cast<const uint8_t*>(manufacturerData);
  uint16_t companyId = (data[1] << 8) | data[0];
  
  // Look up manufacturer name
  for (int i = 0; i < MANUFACTURER_NAME_COUNT; i++) {
    if (companyId == MANUFACTURER_NAMES[i].companyId) {
      return MANUFACTURER_NAMES[i].name;
    }
  }
  
  return nullptr;
}
const char* getServiceName(const char* serviceUUID) {
  if (!serviceUUID) {
    return nullptr;
  }
  
  // Normalize UUID to lowercase and remove dashes
  char normalized[256];
  toLowerStr(normalized, serviceUUID, sizeof(normalized));
  removeDashes(normalized);
  
  // Extract the 16-bit UUID portion (first 8 chars of normalized UUID)
  char shortUUID[9];
  strncpy(shortUUID, normalized, 8);
  shortUUID[8] = '\0';
  
  // Look up service name
  for (int i = 0; i < SERVICE_UUID_NAME_COUNT; i++) {
    if (strcmp(shortUUID, SERVICE_UUID_NAMES[i].uuid) == 0) {
      return SERVICE_UUID_NAMES[i].name;
    }
  }
  
  return nullptr;
}