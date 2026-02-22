/*
GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007

Libraries and supporting code incorporates other licenses, see https://github.com/Xorlent/ESP32-Watchman/blob/main/LICENSE-3RD-PARTY.md

NOTE: Static default settings are located in Config.h
Runtime configuration is done via the Serial console.
To enter configuration mode, open the serial console during bootup, press "C" and hit enter.
*/

//#define IMDB_ENABLE_PERSISTENCE 0

#include <Ethernet.h>
#include <BLEDevice.h>
#include <ESP32IMDB.h>
#include <SPI.h>
#include <NTP.h>
#include <EthernetUDP.h>
#include <Preferences.h>
#include "M5_DLight.h"
#include "Unit_Sonic.h"
#include "BLEClassifier.h"
#include "Database.h"
#include "Bluetooth.h"
#include "SensorAccessory.h"
#include "Config.h"

// RGBLED Indicator setup
//
// Red = Missing Etherent PHY (Is the AtomPoE sled connected)?
// Yellow = Waiting for Ethernet link to come up
// Green = Connected
// Purple = NTP sync failure
// Blue = Active Bluetooth poll
// Flashing Blue = Active Bluetooth probe (service enumeration)
#include <WS2812B.h>
WS2812B rgbled;
#define LED_DATA_PIN 35
// End RGBLED Indicator setup

// Configuration mode flag (local to main sketch)
bool configMode = false;

// Bluetooth setup
BLEScan *pBLEScan;
unsigned long scanTime;
unsigned long scanSquelch;
unsigned long lastBLEScanTime = 0;
// End Bluetooth setup

// UDP for NTP time sync setup
EthernetUDP udp;
NTP ntp(udp);
unsigned long lastNTPUpdate = 0;
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
  char syslogMessage[1420];
  // Format: <PRI>TIMESTAMP HOSTNAME MESSAGE
  // PRI = facility * 8 + severity (facility 1 = user, severity 6 = info)
  int headerLength = snprintf(syslogMessage, sizeof(syslogMessage), "<14>%s %s ", 
           ntp.formattedTime("%b %d %T "), 
           configHostname);
  
  // Calculate remaining space for message (leave room for null terminator)
  int maxMessageLength = sizeof(syslogMessage) - headerLength - 1;
  
  // Truncate message if needed to fit within 1420 byte limit
  if (strlen(message) > maxMessageLength) {
    strncat(syslogMessage, message, maxMessageLength - 3);
    strcat(syslogMessage, "...");  // Indicate truncation
  } else {
    strcat(syslogMessage, message);
  }
  
  syslogUdp.beginPacket(configSyslog, SYSLOG_PORT);
  syslogUdp.print(syslogMessage);
  syslogUdp.endPacket();
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
  rgbled.begin(LED_DATA_PIN);
  
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
      rgbled.set("red", 50); // Awaiting Ethernet PHY, set status LED to red
      delay(1000); // Wait for AtomPoE to be connected
    }
  }

  // Verify Ethernet link up
  while(Ethernet.linkStatus() != LinkON)
  {
    Serial.print("Waiting for Ethernet link.");
    Serial.println();
    rgbled.set("yellow", 50); // Awaiting link up, set status LED to yellow
    delay(1000);
  }

  // Ethernet online, set status LED to green
  rgbled.set("green", 50);

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
#if ACTIVE_BT_SCANS
  Serial.println("Active BT Scan: Enabled");
#else
  Serial.println("Active BT Scan: Disabled");
#endif
/*
#if IMDB_ENABLE_PERSISTENCE
  Serial.println("Persistence: Enabled");
#else
  Serial.println("Persistence: Disabled");
#endif
*/
  Serial.println("=============================\n");

  // Initialize ESP32IMDB In-Memory Database
  // Define table structure: Address (MAC), LastSeen (EPOCH), TimesSeen (INT32)
  IMDBColumn columns[] = {
    {"Address", IMDB_TYPE_MAC},
    {"LastSeen", IMDB_TYPE_EPOCH},
    {"TimesSeen", IMDB_TYPE_INT32}
  };
  
  IMDBResult dbResult = db.createTable(columns, 3);
  if (dbResult == IMDB_OK) {
    Serial.println("ESP32IMDB table created successfully.");
  } else {
    Serial.printf("Failed to create ESP32IMDB table: %s\n", ESP32IMDB::resultToString(dbResult));
  }

  // Initialize Bluetooth
	BLEDevice::init("");
	pBLEScan = BLEDevice::getScan();
#if ACTIVE_BT_SCANS
	pBLEScan->setActiveScan(true);
#else
	pBLEScan->setActiveScan(false);
#endif

  // Initialize UDP for syslog
  syslogUdp.begin(514);
  
  // Record boot time for motion detection init wait
  bootTime = millis();
  
  // Detect and initialize DLIGHT module
  Serial.println("Checking for DLIGHT ambient light sensor...");
  dlightAvailable = detectDLightModule();
  if (dlightAvailable) {
    Serial.printf("DLIGHT module detected and initialized. Light alerts will begin after %d second initialization period.\n", LIGHT_INIT_WAIT / 1000);
    Serial.println();
    lastLightSampleTime = millis(); // Initialize sample timer
  } else {
    Serial.println("DLIGHT module not detected. Checking for ultrasonic sensor...");
    
    // If DLIGHT not found, try ultrasonic sensor
    ultrasonicAvailable = detectUltrasonicModule();
    if (ultrasonicAvailable) {
      Serial.printf("Ultrasonic sensor detected and initialized. Motion alerts will begin after %d second initialization period.\n", MOTION_INIT_WAIT / 1000);
      Serial.println();
      lastMotionSampleTime = millis(); // Initialize sample timer
    } else {
      Serial.println("Ultrasonic sensor not detected. Motion monitoring disabled.");
    }
  }
}

void loop() {
  unsigned long currentTime = millis();
  
  // Update NTP once per day (86400 seconds = 24 hours)
  if (currentTime - lastNTPUpdate >= 86400000 || lastNTPUpdate == 0) {
    ntp.update();
    lastNTPUpdate = currentTime;
  }
  
  scanTime = ntp.epoch();

  // If the NTP time does not look right, keep trying
  while(scanTime < 1767225600){ // DTS is at least Jan 1, 2026
    Serial.println("NTP time sync failed, retrying...");
    rgbled.set("purple", 50); // Awaiting successful NTP, set status LED to purple
    delay(1000);
    ntp.update(); // Re-attempt NTP sync
    scanTime = ntp.epoch();
    lastNTPUpdate = millis(); // Update the timer after successful sync
  }

  // Check if 60 seconds have passed since last BLE scan
  if (currentTime - lastBLEScanTime >= 60000) {
    scanSquelch = scanTime - DWELL_TIME;

    rgbled.set("blue", 50); // Starting Bluetooth scan, set status LED to blue
    BLEScanResults* scanResults = pBLEScan->start(5);
    Serial.printf("%d nearby Bluetooth devices.\n", scanResults->getCount());
    rgbled.set("black", 50); // Finished Bluetooth scan, disable status LED
    
    for(int i = 0; i < scanResults->getCount(); i++) {
      BLEAdvertisedDevice currentBLE = scanResults->getDevice(i);
      if (currentBLE.getRSSI() > BT_RSSI_THRESHOLD) {
        Serial.println(currentBLE.toString());
        processBluetoothDevice(currentBLE, scanTime, scanSquelch);
      }
    }
    Serial.println();
    
    lastBLEScanTime = currentTime; // Update last scan time
  }
  
  // Ambient light level monitoring routine
  checkLightLevel();
  
  // Motion detection monitoring routine
  checkMotionLevel();
}