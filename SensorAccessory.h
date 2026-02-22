#pragma once

#include <Arduino.h>
#include "M5_DLight.h"
#include "Unit_Sonic.h"
#include "Config.h"

// External config constants (defined in Config.cpp)
extern const unsigned int RETRIGGER_TIME;
extern const unsigned int LIGHT_INIT_WAIT;
extern const unsigned int LIGHT_DELTA_PERCENT;
extern const unsigned long LIGHT_SAMPLE_INTERVAL;
extern const unsigned int MOTION_INIT_WAIT;
extern const unsigned int MOTION_DEADBAND;
extern const unsigned long MOTION_SAMPLE_INTERVAL;

// Sensor state (defined in SensorAccessory.cpp)
extern M5_DLight dlight;
extern bool dlightAvailable;
extern unsigned long lastLightSampleTime;
extern unsigned long lastLightAlertTime;
extern uint16_t lastLightValue;

extern SONIC_I2C ultrasonic;
extern bool ultrasonicAvailable;
extern unsigned long lastMotionSampleTime;
extern unsigned long lastMotionAlertTime;
extern float lastDistanceValue;
extern unsigned long bootTime;

bool detectDLightModule();
void checkLightLevel();
bool detectUltrasonicModule();
void checkMotionLevel();
