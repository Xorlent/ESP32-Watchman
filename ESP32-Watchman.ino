/*
GNU GENERAL PUBLIC LICENSE Version 3, 29 June 2007
https://github.com/Xorlent/The-26-Dollar-Honeypot

Libraries and supporting code incorporates other licenses, see https://github.com/Xorlent/ESP32-Watchman/blob/main/LICENSE-3RD-PARTY.md
*/
////////------------------------------------------- CONFIGURATION SETTINGS AREA --------------------------------------------////////

const uint8_t hostName[] = "ESP32-Watchman"; // Set hostname, no spaces, no domain name per RFC 3164

// Ethernet configuration:
IPAddress ip(192, 168, 12, 61); // Set device IP address.
IPAddress gateway(192, 168, 12, 1); // Set default gateway IP address.
IPAddress subnet(255, 255, 255, 0); // Set network subnet mask.
IPAddress dns1(9, 9, 9, 9); // Set DNS server IP.

// Set your Syslog destination:
IPAddress syslogSvr(192, 168, 12, 111);

// Select your NTP server info by configuring and uncommenting ONLY ONE line below:
// IPAddress ntpSvr(192, 168, 1, 2); // Set internal NTP server IP address.
const char* ntpSvr = "pool.ntp.org"; // Or set a NTP DNS server hostname.

// Set the minimum Bluetooth signal strength to report.  Setting this value closer to 0 will decrease sensitivity.
// RSSI guidance: -80 = weak, -70 = reasonable (a typical wall will prevent this signal level), -60 = good, -50 = strong (basically, within a few feet of device)
int BT_RSSI_THRESHOLD = -71;

// Set the number of seconds to wait between detections of the same Bluetooth device (MAC) address.
// Most mobile devices randomize MAC addresses, so this will impact results.
int DWELL_TIME = 3600;

////////--------------------------------------- DO NOT EDIT ANYTHING BELOW THIS LINE ---------------------------------------////////

#include <Ethernet.h>
#include "BLEDevice.h"
#include <sqlite3.h>
#include <SPI.h>
#include <FS.h>
#include "SPIFFS.h"
#include "NTP.h"
#include <EthernetUDP.h>
#include <Preferences.h>

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
// End Preferences data setup

// SPI Ethernet config setup
#define ETH_PHY_CS      6
#define ETH_SPI_SCK     5
#define ETH_SPI_MISO    7
#define ETH_SPI_MOSI    8
// End SPI Etherenet config setup

// SQLite DB setup
sqlite3 *BTDB;
sqlite3_stmt *res;
sqlite3_stmt *res2;
sqlite3_stmt *res3;
const char *tail;
const char *tail2;
const char *tail3;
// End SQLite DB setup

// Bluetooth setup
BLEScan *pBLEScan;
unsigned long scanTime;
unsigned long scanSquelch;
// End Bluetooth setup

// UDP for NTP time sync setup
EthernetUDP udp;
NTP ntp(udp);
// End UDP for NTP time sync setup

// Syslog setup
EthernetUDP syslogUdp;
const int SYSLOG_PORT = 514;

void sendSyslog(const char* message) {
  char syslogMessage[512];
  // Format: <PRI>TIMESTAMP HOSTNAME MESSAGE
  // PRI = facility * 8 + severity (facility 1 = user, severity 6 = info)
  snprintf(syslogMessage, sizeof(syslogMessage), "<14>%s %s %s", 
           ntp.getFormattedTime().c_str(), 
           hostName, 
           message);
  syslogUdp.beginPacket(syslogSvr, SYSLOG_PORT);
  syslogUdp.print(syslogMessage);
  syslogUdp.endPacket();
}

void getEthernetMAC(char* configDirective, int reinitialize){
  SavedConfig.begin(configDirective);
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
  // Initialize the RGB LED
  FastLED.addLeds<WS2812, LED_DATA_PIN, GRB>(rgbled, 1);
  // Set the LED brightness, can be adjusted per user preference (0-255)
  FastLED.setBrightness(50);

  // Initialize Ethernet
  getEthernetMAC("DeviceMAC",0);
  byte mac[] = {0x00, 0x08, 0xDC, MAC4[0], MAC5[0], MAC6[0]};
  SPI.begin(ETH_SPI_SCK, ETH_SPI_MISO, ETH_SPI_MOSI, -1);
  Ethernet.init(ETH_PHY_CS);
  Ethernet.begin(mac, ip, dns1, gateway, subnet);

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
  SPIFFS.begin(true); // true = format on mount failure

  // Setup NTP time synchronization
  ntp.begin(ntpSvr); // Start the NTP client

  // Initialize SQLite DB
  sqlite3_initialize();
  // Open the SQLite DB file (if it does not exit, create it)
  sqlite3_open("/spiffs/BTDB.db", &BTDB);

  // Create the Bluetooth device tracking table (BTClients)
  String createTable = "CREATE TABLE IF NOT EXISTS BTClients (Address, LastSeen INT, TimesSeen INT);"; // Will skip if the table was already created.
  sqlite3_prepare_v2(BTDB, createTable.c_str(), -1, &res, &tail);
  Serial.println(sqlite3_step(res)); // Should output 101 for success / SQL done
  sqlite3_finalize(res);

  // Initialize Bluetooth
	BLEDevice::init("");
	pBLEScan = BLEDevice::getScan();
	pBLEScan->setActiveScan(true);

  // Initialize UDP for syslog
  syslogUdp.begin(514);
}

void loop() {
  // put your main code here, to run repeatedly:
  // Update NTP
  ntp.update();
  scanTime = ntp.epoch();

  // If the NTP time does not look right, keep trying
  while(scanTime < 1700000000){
    rgbled[0] = CRGB::Purple; // Awaiting successful NTP, set status LED to purple
    FastLED.show();
    scanTime = ntp.epoch();
    delay(1000);
  }

  scanSquelch = scanTime - DWELL_TIME;

  rgbled[0] = CRGB::Blue; // Starting Bluetooth scan, set status LED to blue
  FastLED.show();
	BLEScanResults scanResults = pBLEScan->start(5);
  Serial.printf("%d nearby Bluetooth devices.",scanResults.getCount());
  Serial.println();
  rgbled[0] = CRGB::Black; // Finished Bluetooth scan, disable status LED
  FastLED.show();
  for(int i = 0; i < scanResults.getCount(); i++)
  {
    BLEAdvertisedDevice currentBLE = scanResults.getDevice(i);
    if (currentBLE.getRSSI()>BT_RSSI_THRESHOLD){
      //BLEAddress currentBLEAddr = currentBLE.getAddress();
      Serial.println(currentBLE.toString().c_str());
      String query = "SELECT Address, LastSeen FROM BTClients WHERE Address LIKE '";
      query += currentBLE.getAddress().toString().c_str();
      query += "' AND LastSeen < ";
      query += scanSquelch;
      query += ";";
      int retval = sqlite3_prepare_v2(BTDB, query.c_str(), query.length()+1, &res, &tail);
      if (retval != SQLITE_OK) {
        Serial.printf("%s: %s\n", sqlite3_errstr(sqlite3_extended_errcode(BTDB)), sqlite3_errmsg(BTDB));
        Serial.print(" -> Problem preparing query. ");
        Serial.println(query.c_str());
        sqlite3_finalize(res);
        return;
      }
      else {
        if(sqlite3_step(res) == SQLITE_ROW){
          int64_t lastSeen = sqlite3_column_int64(res, 1);
          String logMessage = "Device ID " + String(currentBLE.getAddress().toString().c_str()) + 
                            " -> Last Seen: " + String(lastSeen);
          Serial.print(logMessage.c_str());
          Serial.println();
          sendSyslog(logMessage.c_str());
          String updateStmt = "UPDATE BTClients SET TimesSeen = TimesSeen + 1 WHERE Address LIKE '";
          updateStmt += currentBLE.getAddress().toString().c_str();
          updateStmt += "';";
          int retvalupdate = sqlite3_prepare_v2(BTDB, updateStmt.c_str(), -1, &res2, &tail2);
          if (retvalupdate != SQLITE_OK) {
            sqlite3_finalize(res2);
            sqlite3_finalize(res);
            return;
          }
          else {
            if(sqlite3_step(res2) == SQLITE_DONE) {
              Serial.print("Updated times seen.");
              Serial.println();
              sqlite3_finalize(res2);
              updateStmt = "UPDATE BTClients SET LastSeen = ";
              updateStmt += scanTime;
              updateStmt += " WHERE Address LIKE '";
              updateStmt += currentBLE.getAddress().toString().c_str();
              updateStmt += "';";
              int retvalupdate2 = sqlite3_prepare_v2(BTDB, updateStmt.c_str(), -1, &res2, &tail2);
              if (retvalupdate2 != SQLITE_OK) {
                sqlite3_finalize(res2);
                sqlite3_finalize(res);
                return;
              }
              else {
                if(sqlite3_step(res2) == SQLITE_DONE) {
                  Serial.print("Updated last seen time.");
                  Serial.println();
                }
                sqlite3_finalize(res2);
              }
            }
          }
        } 
        else {
          String query3 = "SELECT Address, LastSeen FROM BTClients WHERE Address LIKE '";
          query3 += currentBLE.getAddress().toString().c_str();
          query3 += "';";
          int retval3 = sqlite3_prepare_v2(BTDB, query3.c_str(), query3.length()+1, &res3, &tail3);
          if (retval3 != SQLITE_OK) {
            Serial.print("Failed to prepare squelch check query: ");
            Serial.println(query3.c_str());
            sqlite3_finalize(res3);
            sqlite3_finalize(res);
            return;
          }
          if(sqlite3_step(res3) == SQLITE_ROW){
            int64_t lastSeen = sqlite3_column_int64(res3, 1);
            String logMessage = "Device ID " + String(currentBLE.getAddress().toString().c_str()) + 
                              " SQUELCHED. -> Last Seen: " + String(lastSeen);
            Serial.print(logMessage.c_str());
            Serial.println();
            sendSyslog(logMessage.c_str());
            sqlite3_finalize(res3);
            sqlite3_finalize(res);
          } //Squelch period
          else {
            String logMessage = "Device ID " + String(currentBLE.getAddress().toString().c_str()) + 
                              " -> NOT PREVIOUSLY SEEN.";
            Serial.print(logMessage.c_str());
            sendSyslog(logMessage.c_str());
            Serial.print("  ");
            String insertStmt = "INSERT INTO BTClients VALUES('";
            insertStmt += currentBLE.getAddress().toString().c_str();
            insertStmt += "',";
            insertStmt += scanTime;
            insertStmt += ",1);";
            int retvalinsert = sqlite3_prepare_v2(BTDB, insertStmt.c_str(), -1, &res2, &tail2);
            if (retvalinsert != SQLITE_OK) {
              Serial.print("Failed to prepare insert statement: ");
              Serial.println(insertStmt.c_str());
              sqlite3_finalize(res2);
              sqlite3_finalize(res3);
              sqlite3_finalize(res);
              return;
            }
            else {
              if(sqlite3_step(res2) == SQLITE_DONE) {
                Serial.print("Added new device to database: ");
                Serial.println(currentBLE.getAddress().toString().c_str());
                Serial.println();
              }
              sqlite3_finalize(res2);
            }
          }
        }
        sqlite3_finalize(res);
      }
    }
  }
  Serial.println();
}
