#include "SensorAccessory.h"
#include <Wire.h>

// DLIGHT ambient light sensor state
M5_DLight dlight;
bool dlightAvailable = false;
unsigned long lastLightSampleTime = 0;
unsigned long lastLightAlertTime = 0;
uint16_t lastLightValue = 0;

// Ultrasonic motion sensor state
SONIC_I2C ultrasonic;
bool ultrasonicAvailable = false;
unsigned long lastMotionSampleTime = 0;
unsigned long lastMotionAlertTime = 0;
float lastDistanceValue = 0.0;
unsigned long bootTime = 0;

// ---------------------------------------------------------------------------
// DLIGHT ambient light sensor
// ---------------------------------------------------------------------------

// Attempt to detect and initialize the DLIGHT module on I2C.
// Returns true if a sensor with a plausible reading is found.
bool detectDLightModule() {
  // M5 Atom uses GPIO2 (SDA) and GPIO1 (SCL)
  Wire.begin(2, 1);
  dlight.begin(&Wire, 2, 1);

  delay(100); // Give sensor time to initialize
  dlight.setMode(CONTINUOUSLY_H_RESOLUTION_MODE2);
  delay(350); // Wait for measurement

  uint16_t testValue = dlight.getLUX();
  Serial.printf("DLIGHT test reading: %d LUX\n", testValue);

  // Values above 54500 are considered unreasonable (no sensor present)
  if (testValue > 54500) {
    return false;
  }
  return true;
}

// Sample the ambient light level and emit a syslog message when the change
// exceeds LIGHT_DELTA_PERCENT, subject to RETRIGGER_TIME suppression.
void checkLightLevel() {
  if (!dlightAvailable) {
    return;
  }

  unsigned long currentTime = millis();

  // Respect initialization wait period
  if (currentTime - bootTime < LIGHT_INIT_WAIT) {
    return;
  }

  // Rate-limit sampling
  if (currentTime - lastLightSampleTime < LIGHT_SAMPLE_INTERVAL) {
    return;
  }
  lastLightSampleTime = currentTime;

  uint16_t currentLightValue = dlight.getLUX();

  // Store first valid reading without alerting
  if (lastLightValue == 0 && currentLightValue > 0) {
    lastLightValue = currentLightValue;
    Serial.printf("Initial light level: %d LUX\n", currentLightValue);
    return;
  }

  if (lastLightValue == 0 && currentLightValue == 0) {
    return;
  }

  int percentChange;
  if (lastLightValue == 0) {
    percentChange = 100;
  } else {
    int diff = abs((int)currentLightValue - (int)lastLightValue);
    percentChange = (diff * 100) / lastLightValue;
  }

  // Serial.printf("Light level: %d LUX (change: %d%%)\n", currentLightValue, percentChange);

  if (percentChange >= LIGHT_DELTA_PERCENT) {
    if (currentTime - lastLightAlertTime >= RETRIGGER_TIME) {
      char logMsg[256];
      snprintf(logMsg, sizeof(logMsg),
               "Room ambient light level changed from %d to %d LUX (%d%% change)",
               lastLightValue, currentLightValue, percentChange);
      Serial.println(logMsg);
      sendSyslog(logMsg);
      lastLightAlertTime = currentTime;
    } else {
      Serial.printf("Light change detected but suppressed (retrigger time not elapsed)\n");
    }
  }

  lastLightValue = currentLightValue;
}

// ---------------------------------------------------------------------------
// Ultrasonic motion sensor
// ---------------------------------------------------------------------------

// Attempt to detect and initialize the ultrasonic sensor on I2C.
// Returns true if a sensor returns a reading in the valid range (10–4499 mm).
bool detectUltrasonicModule() {
  // M5 Atom uses GPIO2 (SDA) and GPIO1 (SCL)
  Wire.begin(2, 1);
  ultrasonic.begin(&Wire, 0x57, 2, 1);

  delay(200); // Give sensor time to initialize

  float testValue = ultrasonic.getDistance();
  Serial.printf("Ultrasonic test reading: %.2f mm\n", testValue);

  if (testValue >= 10.0 && testValue <= 4499.0) {
    return true;
  }
  return false;
}

// Sample the ultrasonic distance and emit a syslog message when the change
// exceeds MOTION_DEADBAND, subject to RETRIGGER_TIME suppression.
void checkMotionLevel() {
  if (!ultrasonicAvailable) {
    return;
  }

  unsigned long currentTime = millis();

  // Respect initialization wait period
  if (currentTime - bootTime < MOTION_INIT_WAIT) {
    return;
  }

  // Rate-limit sampling
  if (currentTime - lastMotionSampleTime < MOTION_SAMPLE_INTERVAL) {
    return;
  }
  lastMotionSampleTime = currentTime;

  float currentDistanceValue = ultrasonic.getDistance();

  // Store first valid reading without alerting
  if (lastDistanceValue == 0.0 && currentDistanceValue > 0.0) {
    lastDistanceValue = currentDistanceValue;
    Serial.printf("Initial distance reading: %.2f mm\n", currentDistanceValue);
    return;
  }

  float distanceChange = abs(currentDistanceValue - lastDistanceValue);
  // Serial.printf("Distance: %.2f mm (change: %.2f mm)\n", currentDistanceValue, distanceChange);

  if (distanceChange >= MOTION_DEADBAND) {
    if (currentTime - lastMotionAlertTime >= RETRIGGER_TIME) {
      char logMsg[256];
      snprintf(logMsg, sizeof(logMsg),
               "Motion detected: Distance changed from %.2f to %.2f mm (%.2f mm change)",
               lastDistanceValue, currentDistanceValue, distanceChange);
      Serial.println(logMsg);
      sendSyslog(logMsg);
      lastMotionAlertTime = currentTime;
    } else {
      Serial.printf("Motion change detected but suppressed (retrigger time not elapsed)\n");
    }
  }

  lastDistanceValue = currentDistanceValue;
}
