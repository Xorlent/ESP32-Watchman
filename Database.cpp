#include "Database.h"

// ESP32IMDB In-Memory Database instance
ESP32IMDB db;

// ---------------------------------------------------------------------------
// Database helper functions for BT device tracking
// ---------------------------------------------------------------------------

// Increment the TimesSeen counter for an existing device.
bool updateDeviceTimesSeen(const char* macAddress) {
  uint8_t macBytes[6];
  if (!ESP32IMDB::parseMacAddress(macAddress, macBytes)) {
    Serial.printf("Failed to parse MAC address: %s\n", macAddress);
    return false;
  }

  // UPDATE BTClients SET TimesSeen = TimesSeen + 1 WHERE Address = macAddress
  IMDBResult result = db.updateWithMath("Address", &macBytes, "TimesSeen", IMDB_MATH_ADD, 1);

  if (result == IMDB_OK) {
    Serial.println("Updated times seen.");
    return true;
  } else {
    Serial.printf("Failed to update TimesSeen: %s\n", ESP32IMDB::resultToString(result));
    return false;
  }
}

// Update the LastSeen timestamp for an existing device.
bool updateDeviceLastSeen(const char* macAddress, unsigned long timestamp) {
  uint8_t macBytes[6];
  if (!ESP32IMDB::parseMacAddress(macAddress, macBytes)) {
    Serial.printf("Failed to parse MAC address: %s\n", macAddress);
    return false;
  }

  uint32_t epochTime = (uint32_t)timestamp;

  // UPDATE BTClients SET LastSeen = timestamp WHERE Address = macAddress
  IMDBResult result = db.update("Address", &macBytes, "LastSeen", &epochTime);

  if (result == IMDB_OK) {
    Serial.println("Updated last seen time.");
    return true;
  } else {
    Serial.printf("Failed to update LastSeen: %s\n", ESP32IMDB::resultToString(result));
    return false;
  }
}

// Insert a new device record with TimesSeen = 1.
bool insertNewDevice(const char* macAddress, unsigned long timestamp) {
  uint8_t macBytes[6];
  if (!ESP32IMDB::parseMacAddress(macAddress, macBytes)) {
    Serial.printf("Failed to parse MAC address: %s\n", macAddress);
    return false;
  }

  uint32_t epochTime = (uint32_t)timestamp;
  int32_t timesSeen = 1;

  // INSERT INTO BTClients (Address, LastSeen, TimesSeen) VALUES(macAddress, timestamp, 1)
  const void* values[] = {&macBytes, &epochTime, &timesSeen};
  IMDBResult result = db.insert(values);

  if (result == IMDB_OK) {
    Serial.printf("Added new device to database: %s\n", macAddress);
    return true;
  } else {
    Serial.printf("Failed to insert device: %s\n", ESP32IMDB::resultToString(result));
    return false;
  }
}

// Return the LastSeen epoch timestamp for a device, or -1 if not found.
int64_t getDeviceLastSeen(const char* macAddress) {
  uint8_t macBytes[6];
  if (!ESP32IMDB::parseMacAddress(macAddress, macBytes)) {
    Serial.printf("Failed to parse MAC address: %s\n", macAddress);
    return -1;
  }

  // SELECT LastSeen FROM BTClients WHERE Address = macAddress
  IMDBSelectResult result;
  IMDBResult queryResult = db.select("LastSeen", "Address", &macBytes, &result);

  if (queryResult == IMDB_OK && result.hasValue) {
    return (int64_t)result.epochValue;
  } else if (queryResult == IMDB_ERROR_NO_RECORDS) {
    return -1; // Device not found
  } else {
    Serial.printf("Failed to select LastSeen: %s\n", ESP32IMDB::resultToString(queryResult));
    return -1;
  }
}
