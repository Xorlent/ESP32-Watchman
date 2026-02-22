#pragma once

#include <Arduino.h>
#include <ESP32IMDB.h>

// Shared database instance (defined in Database.cpp)
extern ESP32IMDB db;

bool updateDeviceTimesSeen(const char* macAddress);
bool updateDeviceLastSeen(const char* macAddress, unsigned long timestamp);
bool insertNewDevice(const char* macAddress, unsigned long timestamp);
int64_t getDeviceLastSeen(const char* macAddress);
