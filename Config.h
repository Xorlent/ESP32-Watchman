#pragma once

#include <Arduino.h>
#include <Ethernet.h>
#include <Preferences.h>
#include <NTP.h>
#include <EthernetUDP.h>
#include <SPI.h>

////////--------------------------------------------- USER CONFIGURATION SETTINGS -----------------------------------------------////////

// DEFAULT NETWORK CONFIGURATION
// These values are used if no saved configuration exists
#define DEFAULT_HOSTNAME "ESP32-Watchman"
#define DEFAULT_IP "192.168.1.100"
#define DEFAULT_GATEWAY "192.168.1.1"
#define DEFAULT_SUBNET "255.255.255.0"
#define DEFAULT_DNS "9.9.9.9"
#define DEFAULT_NTP_SERVER "pool.ntp.org"
#define DEFAULT_SYSLOG "192.168.1.10"

// BLUETOOTH CONFIGURATION
// Set the minimum Bluetooth signal strength to report. Setting this value closer to 0 will decrease sensitivity.
// RSSI guidance: -80 = weak, -70 = reasonable (a typical wall will prevent this signal level), -60 = good, -50 = strong (basically, within a few feet of device)
#define DEFAULT_BT_RSSI_THRESHOLD -71

// Set the number of seconds to wait between detections of the same Bluetooth device (MAC) address.
// Most mobile devices randomize MAC addresses, so this will impact results.
#define DEFAULT_DWELL_TIME 3600

// Set to 1 to actively connect to detected Bluetooth devices to get additional service details for richer results
// Set to 0 to perform all BlueTooth scanning passively
#define ACTIVE_BT_SCANS 0

// SENSOR CONFIGURATION
// These constants are defined in Config.cpp and declared as extern in SensorAccessory.h
// Minimum time in milliseconds between retriggers of the same event (light or motion) to prevent log flooding from sensor noise.
#define DEFAULT_RETRIGGER_TIME 60000

// LIGHT SENSOR SETTINGS
// Time to wait after startup before starting light detection, in milliseconds (default 3 minutes).
// This allows time for the device to boot and for the user to leave the area before light sensing begins.
#define DEFAULT_LIGHT_INIT_WAIT 180000
// Set the percentage change in ambient light level required to trigger a log message.
// Setting this value lower will increase sensitivity, but may result in more false positives due to sensor noise.
#define DEFAULT_LIGHT_DELTA_PERCENT 20
// Set the minimum interval between ambient light samples to check for changes, in milliseconds.
#define DEFAULT_LIGHT_SAMPLE_INTERVAL 3000

// MOTION SENSOR SETTINGS
// Time to wait after startup before starting motion detection, in milliseconds (default 3 minutes).
// This allows time for the device to boot and for the user to leave the area before motion sensing begins.
#define DEFAULT_MOTION_INIT_WAIT 180000
// Set the deadband (mm) for motion to be detected. Setting this value lower will increase sensitivity, but may result in more false positives due to sensor noise.
#define DEFAULT_MOTION_DEADBAND 10
// Set the minimum interval between motion sensing samples, in milliseconds.
#define DEFAULT_MOTION_SAMPLE_INTERVAL 3000

////////----------------------------------------- END OF USER CONFIGURATION SETTINGS --------------------------------------------////////

// Sensor configuration constants (defined in Config.cpp)
extern const unsigned int RETRIGGER_TIME;
extern const unsigned int LIGHT_INIT_WAIT;
extern const unsigned int LIGHT_DELTA_PERCENT;
extern const unsigned long LIGHT_SAMPLE_INTERVAL;
extern const unsigned int MOTION_INIT_WAIT;
extern const unsigned int MOTION_DEADBAND;
extern const unsigned long MOTION_SAMPLE_INTERVAL;

// ETH pin definitions
#ifndef ETH_PHY_CS
#define ETH_PHY_CS  6
#endif
#ifndef ETH_SPI_SCK
#define ETH_SPI_SCK 5
#endif
#ifndef ETH_SPI_MISO
#define ETH_SPI_MISO 7
#endif
#ifndef ETH_SPI_MOSI
#define ETH_SPI_MOSI 8
#endif

// Runtime configuration variables
extern char configHostname[64];
extern char configNtpServer[128];
extern IPAddress configIp;
extern IPAddress configGateway;
extern IPAddress configSubnet;
extern IPAddress configDns;
extern IPAddress configSyslog;
extern int BT_RSSI_THRESHOLD;
extern int DWELL_TIME;
extern bool needsConfiguration;
extern bool ethernetInitialized;
extern Preferences SavedConfig;
extern uint8_t MAC4[2];
extern uint8_t MAC5[2];
extern uint8_t MAC6[2];
extern NTP ntp;

// Forward declarations of validators and helpers (defined in main .ino)
bool validateIPAddress(const char* input, IPAddress& addr);
bool validateHostname(const char* input);
bool validateRSSI(const char* input, int& value);
bool validateDwellTime(const char* input, int& value);
bool validateNtpServer(const char* input);
void getEthernetMAC(char* configDirective, int reinitialize);
void sendSyslog(const char* message);
void formatTimestamp(int64_t timestamp, char* buffer, size_t bufferSize);

// Functions defined in Config.cpp
String readSerialLine();
void saveConfiguration();
void loadConfiguration();
void enterConfigurationMode();
