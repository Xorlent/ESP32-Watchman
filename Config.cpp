#include "Config.h"

// ---------------------------------------------------------------------------
// Runtime configuration variables
// ---------------------------------------------------------------------------

char configHostname[64];
char configNtpServer[128];
IPAddress configIp;
IPAddress configGateway;
IPAddress configSubnet;
IPAddress configDns;
IPAddress configSyslog;
int BT_RSSI_THRESHOLD = DEFAULT_BT_RSSI_THRESHOLD;
int DWELL_TIME = DEFAULT_DWELL_TIME;
bool needsConfiguration = false;
bool ethernetInitialized = false;

// Sensor configuration constants
const unsigned int RETRIGGER_TIME = DEFAULT_RETRIGGER_TIME;
const unsigned int LIGHT_INIT_WAIT = DEFAULT_LIGHT_INIT_WAIT;
const unsigned int LIGHT_DELTA_PERCENT = DEFAULT_LIGHT_DELTA_PERCENT;
const unsigned long LIGHT_SAMPLE_INTERVAL = DEFAULT_LIGHT_SAMPLE_INTERVAL;
const unsigned int MOTION_INIT_WAIT = DEFAULT_MOTION_INIT_WAIT;
const unsigned int MOTION_DEADBAND = DEFAULT_MOTION_DEADBAND;
const unsigned long MOTION_SAMPLE_INTERVAL = DEFAULT_MOTION_SAMPLE_INTERVAL;

// Persistent storage and MAC address components
Preferences SavedConfig;
uint8_t MAC4[2] = {};
uint8_t MAC5[2] = {};
uint8_t MAC6[2] = {};

// ---------------------------------------------------------------------------
// Serial input helper
// ---------------------------------------------------------------------------

// Read a line from the Serial console with basic backspace support.
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

// ---------------------------------------------------------------------------
// Persistent configuration
// ---------------------------------------------------------------------------

// Persist the current runtime configuration to NVS.
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

// Load saved configuration from NVS, falling back to factory defaults when
// no valid configuration is found.
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

// ---------------------------------------------------------------------------
// Interactive configuration mode (Serial console)
// ---------------------------------------------------------------------------

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
  while (Ethernet.linkStatus() != LinkON && linkAttempts < 10) {
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
