/*
GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

Libraries and supporting code incorporates other licenses, see https://github.com/Xorlent/ESP32-Watchman/blob/main/LICENSE-3RD-PARTY.md
*/
////////--------------------------------------------- DEFAULT CONFIG SETTINGS -----------------------------------------------////////

// Default values (used if no configuration exists)
const char DEFAULT_HOSTNAME[] = "ESP32-Watchman";
const char DEFAULT_IP[] = "192.168.1.100";
const char DEFAULT_GATEWAY[] = "192.168.1.1";
const char DEFAULT_SUBNET[] = "255.255.255.0";
const char DEFAULT_DNS[] = "9.9.9.9";
const char DEFAULT_NTP_SERVER[] = "pool.ntp.org";
const char DEFAULT_SYSLOG[] = "192.168.1.10";

// Set the minimum Bluetooth signal strength to report.  Setting this value closer to 0 will decrease sensitivity.
// RSSI guidance: -80 = weak, -70 = reasonable (a typical wall will prevent this signal level), -60 = good, -50 = strong (basically, within a few feet of device)
int BT_RSSI_THRESHOLD = -71;

// Set the number of seconds to wait between detections of the same Bluetooth device (MAC) address.
// Most mobile devices randomize MAC addresses, so this will impact results.
int DWELL_TIME = 3600;

// Minimum time in milliseconds between retriggers of the same event (light or motion) to prevent log flooding from sensor noise.
const unsigned int RETRIGGER_TIME = 60000;

// Time to wait after startup before starting light detection, in milliseconds (default 3 minutes).
// This allows time for the device to boot and for the user to leave the area before light sensing begins.
const unsigned int LIGHT_INIT_WAIT = 180000;
// Set the percentage change in ambient light level required to trigger a log message.
// Setting this value lower will increase sensitivity, but may result in more false positives due to sensor noise.
const unsigned int LIGHT_DELTA_PERCENT = 20;
// Set the minimum interval between ambient light samples to check for changes, in milliseconds.
const unsigned long LIGHT_SAMPLE_INTERVAL = 3000;

// Time to wait after startup before starting motion detection, in milliseconds (default 3 minutes).
// This allows time for the device to boot and for the user to leave the area before motion sensing begins.
const unsigned int MOTION_INIT_WAIT = 180000;
// Set the deadband (mm) for motion to be detected. Setting this value lower will increase sensitivity, but may result in more false positives due to sensor noise.
const unsigned int MOTION_DEADBAND = 10;
// Set the minimum interval between motion sensing samples, in milliseconds.
const unsigned long MOTION_SAMPLE_INTERVAL = 3000;

////////--------------------------------------- DO NOT EDIT ANYTHING BELOW THIS LINE ---------------------------------------////////

// Configuration loaded from preferences
char configHostname[64];
char configNtpServer[128];
IPAddress configIp;
IPAddress configGateway;
IPAddress configSubnet;
IPAddress configDns;
IPAddress configSyslog;

#include <Ethernet.h>
#include "BLEDevice.h"
#include <sqlite3.h>
#include <SPI.h>
#include <FS.h>
#include "SPIFFS.h"
#include "NTP.h"
#include <EthernetUDP.h>
#include <Preferences.h>
#include "M5_DLight.h"
#include "Unit_Sonic.h"

// RGBLED Indicator setup
//
// Red = Missing Etherent PHY (Is the AtomPoE sled connected)?
// Yellow = Waiting for Ethernet link to come up
// Green = Connected
// Purple = NTP sync failure
// Blue = Active Bluetooth poll
#define FASTLED_ALL_PINS_HARDWARE_SPI
#include <FastLED.h>
CRGB rgbled[1];
#define LED_DATA_PIN 35
// End RGBLED Indicator setup

// Preferences data setup
//
// Creates and saves an appropriate random MAC address to the device, re-loading this uniquely-generated MAC on each restart
Preferences SavedConfig;
uint8_t MAC4[2] = {};
uint8_t MAC5[2] = {};
uint8_t MAC6[2] = {};

// Configuration flags
bool configMode = false;
bool needsConfiguration = false;
bool ethernetInitialized = false;
// End Preferences data setup

// SPI Ethernet config setup
#define ETH_PHY_CS      6
#define ETH_SPI_SCK     5
#define ETH_SPI_MISO    7
#define ETH_SPI_MOSI    8
// End SPI Etherenet config setup

// SQLite DB setup
sqlite3 *BTDB;
// End SQLite DB setup

// Bluetooth setup
BLEScan *pBLEScan;
unsigned long scanTime;
unsigned long scanSquelch;
// End Bluetooth setup

// DLIGHT ambient light sensor setup
M5_DLight dlight;
bool dlightAvailable = false;
unsigned long lastLightSampleTime = 0;
unsigned long lastLightAlertTime = 0;
uint16_t lastLightValue = 0;
// End DLIGHT setup

// Ultrasonic motion sensor setup
SONIC_I2C ultrasonic;
bool ultrasonicAvailable = false;
unsigned long lastMotionSampleTime = 0;
unsigned long lastMotionAlertTime = 0;
float lastDistanceValue = 0.0;
unsigned long bootTime = 0;
// End ultrasonic setup

// UDP for NTP time sync setup
EthernetUDP udp;
NTP ntp(udp);
// End UDP for NTP time sync setup

// Syslog setup
EthernetUDP syslogUdp;
const int SYSLOG_PORT = 514;

// Helper function to format Unix timestamp
void formatTimestamp(int64_t timestamp, char* buffer, size_t bufferSize) {
  time_t t = (time_t)timestamp;
  struct tm* timeinfo = gmtime(&t);
  strftime(buffer, bufferSize, "%b %d %T ", timeinfo);
}

void sendSyslog(const char* message) {
  char syslogMessage[512];
  // Format: <PRI>TIMESTAMP HOSTNAME MESSAGE
  // PRI = facility * 8 + severity (facility 1 = user, severity 6 = info)
  snprintf(syslogMessage, sizeof(syslogMessage), "<14>%s %s %s", 
           ntp.formattedTime("%b %d %T "), 
           configHostname, 
           message);
  syslogUdp.beginPacket(configSyslog, SYSLOG_PORT);
  syslogUdp.print(syslogMessage);
  syslogUdp.endPacket();
}

// DLIGHT module detection and initialization
bool detectDLightModule() {
  // Try to initialize the DLIGHT module on I2C
  // M5 Atom uses GPIO2 (SDA) and GPIO1 (SCL)
  Wire.begin(2, 1);
  dlight.begin(&Wire, 2, 1);
  
  // Attempt to read from the sensor to verify presence
  delay(100); // Give sensor time to initialize
  dlight.setMode(CONTINUOUSLY_H_RESOLUTION_MODE2);
  delay(350); // Wait for measurement
  
  uint16_t testValue = dlight.getLUX();
  
  Serial.printf("DLIGHT test reading: %d LUX\n", testValue);
  
  // Check for unreasonable values that indicate no sensor present
  if (testValue > 54500) {
    return false; // Unreasonable reading indicates no sensor
  }
  
  return true; // Sensor responded with plausible data
}

// Check and report ambient light changes
void checkLightLevel() {
  if (!dlightAvailable) {
    return; // No sensor available
  }
  
  unsigned long currentTime = millis();
  
  // Check if we're still in the initialization wait period
  if (currentTime - bootTime < LIGHT_INIT_WAIT) {
    return; // Still in init wait period, don't alert yet
  }
  
  // Check if it's time to sample
  if (currentTime - lastLightSampleTime < LIGHT_SAMPLE_INTERVAL) {
    return; // Not time yet
  }
  
  // Update sample time
  lastLightSampleTime = currentTime;
  
  // Read current light level
  uint16_t currentLightValue = dlight.getLUX();
  
  // If this is the first reading, just store it
  if (lastLightValue == 0 && currentLightValue > 0) {
    lastLightValue = currentLightValue;
    Serial.printf("Initial light level: %d LUX\n", currentLightValue);
    return;
  }
  
  // Calculate percentage change
  if (lastLightValue == 0 && currentLightValue == 0) {
    return; // No change, both are zero
  }
  
  int percentChange;
  if (lastLightValue == 0) {
    percentChange = 100;
  } else {
    int diff = abs((int)currentLightValue - (int)lastLightValue);
    percentChange = (diff * 100) / lastLightValue;
  }
  
  Serial.printf("Light level: %d LUX (change: %d%%)\n", currentLightValue, percentChange);
  
  // Check if change exceeds threshold
  if (percentChange >= LIGHT_DELTA_PERCENT) {
    // Check if enough time has passed since last alert to prevent log flooding
    if (currentTime - lastLightAlertTime >= RETRIGGER_TIME) {
      char logMsg[256];
      snprintf(logMsg, sizeof(logMsg), 
               "Room ambient light level changed from %d to %d LUX (%d%% change)",
               lastLightValue, currentLightValue, percentChange);
      Serial.println(logMsg);
      sendSyslog(logMsg);
      lastLightAlertTime = currentTime; // Update last alert time
    } else {
      Serial.printf("Light change detected but suppressed (retrigger time not elapsed)\n");
    }
  }
  
  // Update last value
  lastLightValue = currentLightValue;
}

// Ultrasonic module detection and initialization
bool detectUltrasonicModule() {
  // Try to initialize the ultrasonic module on I2C
  // M5 Atom uses GPIO2 (SDA) and GPIO1 (SCL)
  Wire.begin(2, 1);
  ultrasonic.begin(&Wire, 0x57, 2, 1);
  
  // Attempt to read from the sensor to verify presence
  delay(200); // Give sensor time to initialize
  
  float testValue = ultrasonic.getDistance();
  
  // If we get a reasonable value (10-4500 mm)
  // Consider the module present
  Serial.printf("Ultrasonic test reading: %.2f mm\n", testValue);
  
  // Check if the value is within valid range
  if (testValue >= 10.0 && testValue <= 4500.0) {
    return true; // Sensor responded with valid data
  }
  return false;
}

// Check and report motion/distance changes
void checkMotionLevel() {
  if (!ultrasonicAvailable) {
    return; // No sensor available
  }
  
  unsigned long currentTime = millis();
  
  // Check if we're still in the initialization wait period
  if (currentTime - bootTime < MOTION_INIT_WAIT) {
    return; // Still in init wait period, don't alert yet
  }
  
  // Check if it's time to sample
  if (currentTime - lastMotionSampleTime < MOTION_SAMPLE_INTERVAL) {
    return; // Not time yet
  }
  
  // Update sample time
  lastMotionSampleTime = currentTime;
  
  // Read current distance
  float currentDistanceValue = ultrasonic.getDistance();
  
  // If this is the first reading (after init wait), just store it
  if (lastDistanceValue == 0.0 && currentDistanceValue > 0.0) {
    lastDistanceValue = currentDistanceValue;
    Serial.printf("Initial distance reading: %.2f mm\n", currentDistanceValue);
    return;
  }
  
  // Calculate the change in distance
  float distanceChange = abs(currentDistanceValue - lastDistanceValue);
  
  Serial.printf("Distance: %.2f mm (change: %.2f mm)\n", currentDistanceValue, distanceChange);
  
  // Check if change exceeds deadband threshold
  if (distanceChange >= MOTION_DEADBAND) {
    // Check if enough time has passed since last alert to prevent log flooding
    if (currentTime - lastMotionAlertTime >= RETRIGGER_TIME) {
      char logMsg[256];
      snprintf(logMsg, sizeof(logMsg), 
               "Motion detected: Distance changed from %.2f to %.2f mm (%.2f mm change)",
               lastDistanceValue, currentDistanceValue, distanceChange);
      Serial.println(logMsg);
      sendSyslog(logMsg);
      lastMotionAlertTime = currentTime; // Update last alert time
    } else {
      Serial.printf("Motion change detected but suppressed (retrigger time not elapsed)\n");
    }
  }
  
  // Update last value
  lastDistanceValue = currentDistanceValue;
}

// Database helper functions
bool updateDeviceTimesSeen(const char* macAddress) {
  sqlite3_stmt *stmt = nullptr;
  char query[256];
  snprintf(query, sizeof(query), "UPDATE BTClients SET TimesSeen = TimesSeen + 1 WHERE Address = ?");
  
  int retval = sqlite3_prepare_v2(BTDB, query, -1, &stmt, nullptr);
  if (retval != SQLITE_OK) {
    Serial.printf("Failed to prepare update TimesSeen: %s\n", sqlite3_errmsg(BTDB));
    if (stmt) sqlite3_finalize(stmt);
    return false;
  }
  
  sqlite3_bind_text(stmt, 1, macAddress, -1, SQLITE_TRANSIENT);
  
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  if (success) {
    Serial.println("Updated times seen.");
  }
  
  sqlite3_finalize(stmt);
  return success;
}

bool updateDeviceLastSeen(const char* macAddress, unsigned long timestamp) {
  sqlite3_stmt *stmt = nullptr;
  char query[256];
  snprintf(query, sizeof(query), "UPDATE BTClients SET LastSeen = ? WHERE Address = ?");
  
  int retval = sqlite3_prepare_v2(BTDB, query, -1, &stmt, nullptr);
  if (retval != SQLITE_OK) {
    Serial.printf("Failed to prepare update LastSeen: %s\n", sqlite3_errmsg(BTDB));
    if (stmt) sqlite3_finalize(stmt);
    return false;
  }
  
  sqlite3_bind_int64(stmt, 1, timestamp);
  sqlite3_bind_text(stmt, 2, macAddress, -1, SQLITE_TRANSIENT);
  
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  if (success) {
    Serial.println("Updated last seen time.");
  }
  
  sqlite3_finalize(stmt);
  return success;
}

bool insertNewDevice(const char* macAddress, unsigned long timestamp) {
  sqlite3_stmt *stmt = nullptr;
  char query[256];
  snprintf(query, sizeof(query), "INSERT INTO BTClients (Address, LastSeen, TimesSeen) VALUES(?, ?, 1)");
  
  int retval = sqlite3_prepare_v2(BTDB, query, -1, &stmt, nullptr);
  if (retval != SQLITE_OK) {
    Serial.printf("Failed to prepare insert: %s\n", sqlite3_errmsg(BTDB));
    if (stmt) sqlite3_finalize(stmt);
    return false;
  }
  
  sqlite3_bind_text(stmt, 1, macAddress, -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, timestamp);
  
  bool success = (sqlite3_step(stmt) == SQLITE_DONE);
  if (success) {
    Serial.printf("Added new device to database: %s\n", macAddress);
  }
  
  sqlite3_finalize(stmt);
  return success;
}

int64_t getDeviceLastSeen(const char* macAddress) {
  sqlite3_stmt *stmt = nullptr;
  char query[256];
  snprintf(query, sizeof(query), "SELECT LastSeen FROM BTClients WHERE Address = ?");
  
  int retval = sqlite3_prepare_v2(BTDB, query, -1, &stmt, nullptr);
  if (retval != SQLITE_OK) {
    Serial.printf("Failed to prepare select: %s\n", sqlite3_errmsg(BTDB));
    if (stmt) sqlite3_finalize(stmt);
    return -1;
  }
  
  sqlite3_bind_text(stmt, 1, macAddress, -1, SQLITE_TRANSIENT);
  
  int64_t lastSeen = -1;
  if (sqlite3_step(stmt) == SQLITE_ROW) {
    lastSeen = sqlite3_column_int64(stmt, 0);
  }
  
  sqlite3_finalize(stmt);
  return lastSeen;
}

void processBluetoothDevice(BLEAdvertisedDevice& device, unsigned long currentTime, unsigned long squelchTime) {
  // Copy MAC address to local buffer
  char macAddress[18];
  strncpy(macAddress, device.getAddress().toString().c_str(), sizeof(macAddress) - 1);
  macAddress[sizeof(macAddress) - 1] = '\0';
  
  int64_t lastSeen = getDeviceLastSeen(macAddress);
  
  // Device not in database - insert it
  if (lastSeen == -1) {
    char logMsg[256];
    snprintf(logMsg, sizeof(logMsg), "Device ID %s -> NOT PREVIOUSLY SEEN.", macAddress);
    Serial.println(logMsg);
    sendSyslog(logMsg);
    insertNewDevice(macAddress, currentTime);
    return;
  }
  
  // Device is squelched (seen too recently)
  if (lastSeen >= squelchTime) {
    char timeStr[32];
    formatTimestamp(lastSeen, timeStr, sizeof(timeStr));
    char logMsg[256];
    snprintf(logMsg, sizeof(logMsg), "Device ID %s SQUELCHED. -> Last Seen: %s", macAddress, timeStr);
    Serial.println(logMsg);
    sendSyslog(logMsg);
    return;
  }
  
  // Device seen outside squelch period - update record
  char timeStr[32];
  formatTimestamp(lastSeen, timeStr, sizeof(timeStr));
  char logMsg[256];
  snprintf(logMsg, sizeof(logMsg), "Device ID %s -> Last Seen: %s", macAddress, timeStr);
  Serial.println(logMsg);
  sendSyslog(logMsg);
  
  if (updateDeviceTimesSeen(macAddress)) {
    updateDeviceLastSeen(macAddress, currentTime);
  }
}

// Configuration validation and input functions
bool validateIPAddress(const char* input, IPAddress& addr) {
  int parts[4];
  int matched = sscanf(input, "%d.%d.%d.%d", &parts[0], &parts[1], &parts[2], &parts[3]);
  if (matched != 4) return false;
  
  for (int i = 0; i < 4; i++) {
    if (parts[i] < 0 || parts[i] > 255) return false;
  }
  
  addr = IPAddress(parts[0], parts[1], parts[2], parts[3]);
  return true;
}

bool validateHostname(const char* input) {
  if (strlen(input) == 0 || strlen(input) > 63) return false;
  for (size_t i = 0; i < strlen(input); i++) {
    char c = input[i];
    if (!isalnum(c) && c != '-' && c != '_') return false;
  }
  return true;
}

bool validateRSSI(const char* input, int& value) {
  value = atoi(input);
  return (value >= -90 && value <= -50);
}

bool validateDwellTime(const char* input, int& value) {
  value = atoi(input);
  return (value >= 60 && value <= 86400); // 1 minute to 24 hours
}

bool validateNtpServer(const char* input) {
  if (strlen(input) == 0 || strlen(input) >= 128) return false;
  
  // Check if it's a valid IP address
  IPAddress testAddr;
  if (validateIPAddress(input, testAddr)) return true;
  
  // Check if it's a valid hostname (alphanumeric, dots, hyphens)
  for (size_t i = 0; i < strlen(input); i++) {
    char c = input[i];
    if (!isalnum(c) && c != '.' && c != '-') return false;
  }
  
  // Must not start or end with dot or hyphen
  if (input[0] == '.' || input[0] == '-' || 
      input[strlen(input)-1] == '.' || input[strlen(input)-1] == '-') {
    return false;
  }
  
  return true;
}

String readSerialLine() {
  String input = "";
  while (true) {
    if (Serial.available() > 0) {
      char c = Serial.read();
      if (c == '\n' || c == '\r') {
        break;  // Return input (even if empty)
      } else if (c == '\b' || c == 127) { // Backspace
        if (input.length() > 0) {
          input.remove(input.length() - 1);
          Serial.print("\b \b");
        }
      } else if (isPrintable(c)) {
        input += c;
        Serial.print(c);
      }
    }
    delay(10);
  }
  Serial.println();
  return input;
}

void saveConfiguration() {
  SavedConfig.begin("DeviceConfig", false);
  SavedConfig.putString("hostname", configHostname);
  SavedConfig.putString("ntpServer", configNtpServer);
  SavedConfig.putUInt("ip", (uint32_t)configIp);
  SavedConfig.putUInt("gateway", (uint32_t)configGateway);
  SavedConfig.putUInt("subnet", (uint32_t)configSubnet);
  SavedConfig.putUInt("dns", (uint32_t)configDns);
  SavedConfig.putUInt("syslog", (uint32_t)configSyslog);
  SavedConfig.putInt("rssi", BT_RSSI_THRESHOLD);
  SavedConfig.putInt("dwellTime", DWELL_TIME);
  SavedConfig.end();
  Serial.println("\nConfiguration saved successfully!");
}

void loadConfiguration() {
  SavedConfig.begin("DeviceConfig", true);
  
  String savedHostname = SavedConfig.getString("hostname", "");
  String savedNtpServer = SavedConfig.getString("ntpServer", "");
  uint32_t savedIp = SavedConfig.getUInt("ip", 0);
  
  // Check if configuration exists
  if (savedHostname.length() == 0 || savedIp == 0) {
    // No saved configuration - load factory defaults and flag for configuration
    needsConfiguration = true;
    strncpy(configHostname, DEFAULT_HOSTNAME, sizeof(configHostname) - 1);
    configHostname[sizeof(configHostname) - 1] = '\0';
    strncpy(configNtpServer, DEFAULT_NTP_SERVER, sizeof(configNtpServer) - 1);
    configNtpServer[sizeof(configNtpServer) - 1] = '\0';
    
    validateIPAddress(DEFAULT_IP, configIp);
    validateIPAddress(DEFAULT_GATEWAY, configGateway);
    validateIPAddress(DEFAULT_SUBNET, configSubnet);
    validateIPAddress(DEFAULT_DNS, configDns);
    validateIPAddress(DEFAULT_SYSLOG, configSyslog);
  } else {
    // Load saved configuration
    strncpy(configHostname, savedHostname.c_str(), sizeof(configHostname) - 1);
    configHostname[sizeof(configHostname) - 1] = '\0';
    strncpy(configNtpServer, savedNtpServer.c_str(), sizeof(configNtpServer) - 1);
    configNtpServer[sizeof(configNtpServer) - 1] = '\0';
    
    configIp = savedIp;
    configGateway = SavedConfig.getUInt("gateway", 0);
    configSubnet = SavedConfig.getUInt("subnet", 0);
    configDns = SavedConfig.getUInt("dns", 0);
    configSyslog = SavedConfig.getUInt("syslog", 0);
    BT_RSSI_THRESHOLD = SavedConfig.getInt("rssi", BT_RSSI_THRESHOLD);
    DWELL_TIME = SavedConfig.getInt("dwellTime", DWELL_TIME);
  }
  
  SavedConfig.end();
}

void enterConfigurationMode() {
  Serial.println("\n\n========================================");
  Serial.println("    ESP32-Watchman Configuration");
  Serial.println("========================================\n");
  
  // Hostname
  while (true) {
    Serial.print("Enter Hostname (current: ");
    Serial.print(configHostname);
    Serial.print(") [ENTER uses current/default value]: ");
    String input = readSerialLine();
    if (input.length() == 0) break; // Keep current
    if (validateHostname(input.c_str())) {
      strncpy(configHostname, input.c_str(), sizeof(configHostname) - 1);
      configHostname[sizeof(configHostname) - 1] = '\0';
      break;
    }
    Serial.println("Invalid hostname. Use only alphanumeric characters, hyphens, and underscores (max 63 chars).");
  }
  
  // IP Address
  while (true) {
    Serial.print("Enter IP Address (current: ");
    Serial.print(configIp);
    Serial.print(") [ENTER uses current/default value]: ");
    String input = readSerialLine();
    if (input.length() == 0) break;
    if (validateIPAddress(input.c_str(), configIp)) break;
    Serial.println("Invalid IP address. Format: xxx.xxx.xxx.xxx");
  }
  
  // Subnet Mask
  while (true) {
    Serial.print("Enter Subnet Mask (current: ");
    Serial.print(configSubnet);
    Serial.print(") [ENTER uses current/default value]: ");
    String input = readSerialLine();
    if (input.length() == 0) break;
    if (validateIPAddress(input.c_str(), configSubnet)) break;
    Serial.println("Invalid subnet mask. Format: xxx.xxx.xxx.xxx");
  }
  
  // Gateway
  while (true) {
    Serial.print("Enter Gateway Address (current: ");
    Serial.print(configGateway);
    Serial.print(") [ENTER uses current/default value]: ");
    String input = readSerialLine();
    if (input.length() == 0) break;
    if (validateIPAddress(input.c_str(), configGateway)) break;
    Serial.println("Invalid gateway address. Format: xxx.xxx.xxx.xxx");
  }
  
  // DNS
  while (true) {
    Serial.print("Enter DNS Server Address (current: ");
    Serial.print(configDns);
    Serial.print(") [ENTER uses current/default value]: ");
    String input = readSerialLine();
    if (input.length() == 0) break;
    if (validateIPAddress(input.c_str(), configDns)) break;
    Serial.println("Invalid DNS address. Format: xxx.xxx.xxx.xxx");
  }
  
  // Syslog
  while (true) {
    Serial.print("Enter Syslog Server Address (current: ");
    Serial.print(configSyslog);
    Serial.print(") [ENTER uses current/default value]: ");
    String input = readSerialLine();
    if (input.length() == 0) break;
    if (validateIPAddress(input.c_str(), configSyslog)) break;
    Serial.println("Invalid syslog address. Format: xxx.xxx.xxx.xxx");
  }
  
  // NTP Server
  while (true) {
    Serial.print("Enter NTP Server Name or Address (current: ");
    Serial.print(configNtpServer);
    Serial.print(") [ENTER uses current/default value]: ");
    String input = readSerialLine();
    if (input.length() == 0) break;
    if (validateNtpServer(input.c_str())) {
      strncpy(configNtpServer, input.c_str(), sizeof(configNtpServer) - 1);
      configNtpServer[sizeof(configNtpServer) - 1] = '\0';
      break;
    }
    Serial.println("Invalid NTP server. Must be a valid hostname or IP address.");
  }
  
  // RSSI Threshold
  Serial.println("\nThis value sets the minimum Bluetooth signal strength to report.");
  Serial.println("Setting this value closer to 0 will decrease sensitivity.");
  Serial.println("RSSI guidance: -80 = weak, -70 = reasonable (a typical wall will prevent this signal level),");
  Serial.println("               -60 = good, -50 = strong (basically, within a few feet of device)");
  while (true) {
    Serial.print("Enter Bluetooth RSSI Threshold -90 to -50 (current: ");
    Serial.print(BT_RSSI_THRESHOLD);
    Serial.print(") [ENTER uses current/default value]: ");
    String input = readSerialLine();
    if (input.length() == 0) break;
    int value;
    if (validateRSSI(input.c_str(), value)) {
      BT_RSSI_THRESHOLD = value;
      break;
    }
    Serial.println("Invalid RSSI value. Must be between -90 and -50.");
  }
  
  // Dwell Time
  while (true) {
    Serial.print("Enter Dwell Time in seconds, 60-86400 (current: ");
    Serial.print(DWELL_TIME);
    Serial.print(") [ENTER uses current/default value]: ");
    String input = readSerialLine();
    if (input.length() == 0) break;
    int value;
    if (validateDwellTime(input.c_str(), value)) {
      DWELL_TIME = value;
      break;
    }
    Serial.println("Invalid dwell time. Must be between 60 and 86400 seconds.");
  }
  
  // Save configuration
  saveConfiguration();
  
  // Initialize Ethernet for NTP testing
  Serial.println("\nInitializing network...");
  getEthernetMAC("DeviceMAC", 0);
  byte mac[] = {0x00, 0x08, 0xDC, MAC4[0], MAC5[0], MAC6[0]};
  SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI, -1);
  Ethernet.init(ETH_PHY_CS);
  Ethernet.begin(mac, configIp, configDns, configGateway, configSubnet);
  ethernetInitialized = true; // Mark as initialized
  
  // Verify Ethernet hardware
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("ERROR: Ethernet PHY not found. Cannot test NTP.");
    Serial.println("Configuration saved but network test skipped.");
    delay(3000);
    if (!needsConfiguration) {
      // Reconfiguration - restart required
      Serial.println("Restarting...");
      delay(2000);
      ESP.restart();
    }
    return; // First boot - will continue to normal operation
  }
  
  // Wait for Ethernet link
  Serial.print("Waiting for Ethernet link");
  int linkAttempts = 0;
  while(Ethernet.linkStatus() != LinkON && linkAttempts < 10) {
    Serial.print(".");
    delay(1000);
    linkAttempts++;
  }
  Serial.println();
  
  if (Ethernet.linkStatus() != LinkON) {
    Serial.println("WARNING: Ethernet link did not come up. Cannot test NTP.");
    Serial.println("Configuration saved but network test skipped.");
    delay(3000);
    if (!needsConfiguration) {
      // Reconfiguration - restart required
      Serial.println("Restarting...");
      delay(2000);
      ESP.restart();
    }
    return; // First boot - will continue to normal operation
  }
  
  Serial.println("Network initialized successfully.");
  
  // Test NTP
  Serial.println("Testing NTP synchronization...");
  ntp.begin(configNtpServer);
  delay(2000);
  ntp.update();
  delay(2000);
  unsigned long testTime = ntp.epoch();
  
  if (testTime > 1735689600) { // Jan 1, 2025
    Serial.print("NTP sync successful! Current time: ");
    Serial.println(ntp.formattedTime("%b %d %T "));
  } else {
    Serial.println("NTP sync FAILED! Please check your NTP server settings.");
  }
  
  Serial.println("\n========================================");
  
  if (needsConfiguration) {
    // First-time configuration - continue to normal operation
    Serial.println("Configuration complete!");
    Serial.println("Continuing to normal operation...");
    Serial.println("========================================\n");
    delay(2000);
  } else {
    // Reconfiguration - restart required
    Serial.println("Configuration complete! Restarting...");
    Serial.println("========================================\n");
    delay(3000);
    ESP.restart();
  }
}

void getEthernetMAC(char* configDirective, int reinitialize){
  SavedConfig.begin(configDirective, false);
  if (reinitialize){
    SavedConfig.remove("MAC4");
    SavedConfig.remove("MAC5");
    SavedConfig.remove("MAC6");
    Serial.print("Resetting and regenerating Ethernet MAC Address...");
    Serial.println();
  }
  uint8_t MAC4Buf[2] = {};
  uint8_t MAC5Buf[2] = {};
  uint8_t MAC6Buf[2] = {};
  if (!SavedConfig.getBytes("MAC4",MAC4Buf,1) || !SavedConfig.getBytes("MAC5",MAC5Buf,1) || !SavedConfig.getBytes("MAC6",MAC6Buf,1)) {
    MAC4[0] = esp_random() & 0xff;
    MAC5[0] = esp_random() & 0xff;
    MAC6[0] = esp_random() & 0xff;
    SavedConfig.putBytes("MAC4",MAC4,1);
    SavedConfig.putBytes("MAC5",MAC5,1);
    SavedConfig.putBytes("MAC6",MAC6,1);
  }
  else {
    memcpy(MAC4, MAC4Buf, 1);
    memcpy(MAC5, MAC5Buf, 1);
    memcpy(MAC6, MAC6Buf, 1);
  }
  SavedConfig.end();
}

void setup() {
  Serial.begin(115200);
  delay(2000);
  
  Serial.println("\n\nESP32-Watchman Starting...");
  
  // Initialize the RGB LED early for status indication
  FastLED.addLeds<WS2812, LED_DATA_PIN, GRB>(rgbled, 1);
  FastLED.setBrightness(50);
  
  // Load saved configuration
  loadConfiguration();
  
  // If no configuration exists, force configuration mode
  if (needsConfiguration) {
    Serial.println("\n*** NO CONFIGURATION FOUND ***");
    Serial.println("Entering configuration mode...\n");
    delay(1000);
    enterConfigurationMode();
    // After first-time config, flow continues below to normal operation
  }
  
  if (!needsConfiguration) {
    // Only show this prompt if already configured
    Serial.println("Press 'C' within 5 seconds to enter configuration mode.\n");
    
    // Check for manual configuration mode entry
    unsigned long startTime = millis();
    while (millis() - startTime < 5000) {
      if (Serial.available() > 0) {
        char c = Serial.read();
        if (c == 'C' || c == 'c') {
          configMode = true;
          // Flush serial buffer
          delay(50);
          while (Serial.available() > 0) {
            Serial.read();
          }
          break;
        }
      }
      delay(50);
    }
    
    if (configMode) {
      enterConfigurationMode();
      return; // Will restart after reconfiguration
    }
  }
  
  Serial.println("Starting normal operation...\n");

  // Initialize Ethernet (skip if already initialized during first-time config)
  if (!ethernetInitialized) {
    getEthernetMAC("DeviceMAC",0);
    byte mac[] = {0x00, 0x08, 0xDC, MAC4[0], MAC5[0], MAC6[0]};
    SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI, -1);
    Ethernet.init(ETH_PHY_CS);
    Ethernet.begin(mac, configIp, configDns, configGateway, configSubnet);
  } else {
    Serial.println("Network already initialized, continuing...");
  }

  // Verify Ethernet hardware
  if (Ethernet.hardwareStatus() == EthernetNoHardware) {
    Serial.println("Ethernet PHY not found.");
    while (true) {
      rgbled[0] = CRGB::Red; // Awaiting Ethernet PHY, set status LED to red
      FastLED.show();
      delay(1000); // Wait for AtomPoE to be connected
    }
  }

  // Verify Ethernet link up
  while(Ethernet.linkStatus() != LinkON)
  {
    Serial.print("Waiting for Ethernet link.");
    Serial.println();
    rgbled[0] = CRGB::Yellow; // Awaiting link up, set status LED to yellow
    FastLED.show();
    delay(1000);
  }

  // Ethernet online, set status LED to green
  rgbled[0] = CRGB::Green;
  FastLED.show();

  // Initialize local storage (SPIFFS)
  SPIFFS.begin(true); // true forces format on a mount failure

  // Setup NTP time synchronization
  ntp.begin(configNtpServer); // Start the NTP client
  
  // Display current configuration
  Serial.println("\n=== Current Configuration ===");
  Serial.printf("Hostname: %s\n", configHostname);
  Serial.printf("IP: %s\n", configIp.toString().c_str());
  Serial.printf("Gateway: %s\n", configGateway.toString().c_str());
  Serial.printf("Subnet: %s\n", configSubnet.toString().c_str());
  Serial.printf("DNS: %s\n", configDns.toString().c_str());
  Serial.printf("Syslog: %s\n", configSyslog.toString().c_str());
  Serial.printf("NTP: %s\n", configNtpServer);
  Serial.printf("RSSI Threshold: %d\n", BT_RSSI_THRESHOLD);
  Serial.printf("Dwell Time: %d seconds\n", DWELL_TIME);
  Serial.println("=============================\n");

  // Initialize SQLite DB
  sqlite3_initialize();
  // Open the SQLite DB file (if it does not exit, create it)
  sqlite3_open("/spiffs/BTDB.db", &BTDB);

  // Create the Bluetooth device tracking table (BTClients)
  sqlite3_stmt *stmt = nullptr;
  const char* createTable = "CREATE TABLE IF NOT EXISTS BTClients (Address, LastSeen INT, TimesSeen INT);";
  int retval = sqlite3_prepare_v2(BTDB, createTable, -1, &stmt, nullptr);
  if (retval == SQLITE_OK) {
    int result = sqlite3_step(stmt); // Should output 101 (SQLITE_DONE) for success
    Serial.printf("Table creation result: %d\n", result);
  } else {
    Serial.printf("Failed to create table: %s\n", sqlite3_errmsg(BTDB));
  }
  sqlite3_finalize(stmt);

  // Initialize Bluetooth
	BLEDevice::init("");
	pBLEScan = BLEDevice::getScan();
	pBLEScan->setActiveScan(true);

  // Initialize UDP for syslog
  syslogUdp.begin(514);
  
  // Record boot time for motion detection init wait
  bootTime = millis();
  
  // Detect and initialize DLIGHT module
  Serial.println("Checking for DLIGHT ambient light sensor...");
  dlightAvailable = detectDLightModule();
  if (dlightAvailable) {
    Serial.println("DLIGHT module detected and initialized.");
    lastLightSampleTime = millis(); // Initialize sample timer
  } else {
    Serial.println("DLIGHT module not detected. Checking for ultrasonic sensor...");
    
    // If DLIGHT not found, try ultrasonic sensor
    ultrasonicAvailable = detectUltrasonicModule();
    if (ultrasonicAvailable) {
      Serial.printf("Ultrasonic sensor detected and initialized. Motion alerts will begin after %d second initialization period.\n", MOTION_INIT_WAIT / 1000);
      lastMotionSampleTime = millis(); // Initialize sample timer
    } else {
      Serial.println("Ultrasonic sensor not detected. Motion monitoring disabled.");
    }
  }
}

void loop() {
  // Update NTP
  ntp.update();
  scanTime = ntp.epoch();

  // If the NTP time does not look right, keep trying
  while(scanTime < 1767225600){ // DTS is at least Jan 1, 2026
    Serial.println("NTP time sync failed, retrying...");
    rgbled[0] = CRGB::Purple; // Awaiting successful NTP, set status LED to purple
    FastLED.show();
    delay(1000);
    ntp.update(); // Re-attempt NTP sync
    scanTime = ntp.epoch();
  }

  scanSquelch = scanTime - DWELL_TIME;

  rgbled[0] = CRGB::Blue; // Starting Bluetooth scan, set status LED to blue
  FastLED.show();
	BLEScanResults* scanResults = pBLEScan->start(5);
  Serial.printf("%d nearby Bluetooth devices.\n", scanResults->getCount());
  rgbled[0] = CRGB::Black; // Finished Bluetooth scan, disable status LED
  FastLED.show();
  
  for(int i = 0; i < scanResults->getCount(); i++) {
    BLEAdvertisedDevice currentBLE = scanResults->getDevice(i);
    if (currentBLE.getRSSI() > BT_RSSI_THRESHOLD) {
      Serial.println(currentBLE.toString().c_str());
      processBluetoothDevice(currentBLE, scanTime, scanSquelch);
    }
  }
  Serial.println();
  
  // Ambient light level monitoring routine
  checkLightLevel();
  
  // Motion detection monitoring routine
  checkMotionLevel();
}
