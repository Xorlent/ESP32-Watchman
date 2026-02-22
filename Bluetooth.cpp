#include "Bluetooth.h"
#include "Database.h"
#include "BLEClassifier.h"
#include <BLEClient.h>
#include <BLERemoteService.h>
#include <BLERemoteCharacteristic.h>
#include <map>

// Note: ACTIVE_BT_SCANS is defined in Config.h (included via BLEClassifier.h)

// ---------------------------------------------------------------------------
// Bluetooth helper functions
// ---------------------------------------------------------------------------

// Return true if the locally-administered bit is set in the MAC address,
// which indicates the address has been randomized by the operating system.
bool isRandomizedMAC(const char* macAddress) {
  uint8_t firstByte = 0;
  if (sscanf(macAddress, "%hhx", &firstByte) == 1) {
    return (firstByte & 0x02) != 0;
  }
  return false;
}

// Classify a detected BLE device, look up its history in the database, and
// emit an appropriate syslog message (new / squelched / returning device).
void processBluetoothDevice(BLEAdvertisedDevice& device, unsigned long currentTime, unsigned long squelchTime) {
  // Copy MAC address to local buffer
  char macAddress[18];
  strncpy(macAddress, device.getAddress().toString().c_str(), sizeof(macAddress) - 1);
  macAddress[sizeof(macAddress) - 1] = '\0';

  bool isRandomized = isRandomizedMAC(macAddress);
  const char* randomizedTag = isRandomized ? " [RANDOMIZED MAC]" : "";

  // Classify device by manufacturer data, service UUID, and name
  const char* deviceType = nullptr;
  const char* deviceLabel = "";
  const char* manufacturerName = nullptr;

  // Try manufacturer data first (most commonly available in passive scans)
  if (device.haveManufacturerData()) {
    String mfgData = device.getManufacturerData();
    manufacturerName = getManufacturerName(mfgData.c_str(), mfgData.length());
  }

  // Try to classify by service UUID (rarely available in passive scans)
  if (!deviceType && device.haveServiceUUID()) {
    String uuidStr = device.getServiceUUID().toString();
    deviceType = classifyByServiceUUID(uuidStr.c_str());
  }

  // Get device's last seen time (needed for both classification and processing)
  int64_t lastSeen = getDeviceLastSeen(macAddress);

  // Variables to store GATT-discovered information
  String gattDeviceName = "";
  String gattServices = "";

#if ACTIVE_BT_SCANS
  // For new devices, attempt an active GATT connection for richer details
  if (lastSeen == -1) {
    Serial.printf("NEW DEVICE %s - Attempting GATT connection for detailed info...\n", macAddress);
    
    // Flash LED blue a few times before attempting connection
    for (int i = 0; i < 3; i++) {
      rgbled.set("blue", 50);
      delay(200);
      rgbled.set("black", 50);
      delay(200);
    }
    
    // LED flashing variables for GATT scan indication during service enumeration
    unsigned long lastLEDToggle = millis();
    bool ledState = true;
    
    BLEClient* pClient = BLEDevice::createClient();
    if (pClient != nullptr) {
      // Keep LED black during connection attempt
      
      if (pClient->connect(&device)) {
        Serial.println("  GATT connected successfully!");

        // Try to read device name from Generic Access service (0x1800 / 0x2A00)
        BLERemoteService* pGenericAccessService = pClient->getService(BLEUUID((uint16_t)0x1800));
        if (pGenericAccessService != nullptr) {
          BLERemoteCharacteristic* pDeviceNameChar =
              pGenericAccessService->getCharacteristic(BLEUUID((uint16_t)0x2A00));
          if (pDeviceNameChar != nullptr && pDeviceNameChar->canRead()) {
            gattDeviceName = pDeviceNameChar->readValue();
            if (gattDeviceName.length() > 0) {
              Serial.printf("  Device Name: %s\n", gattDeviceName.c_str());
            }
          }
        }
        
        // Toggle LED after reading device name
        if (millis() - lastLEDToggle >= 200) {
          ledState = !ledState;
          rgbled.set(ledState ? "blue" : "black", 50);
          lastLEDToggle = millis();
        }

        // Enumerate GATT services and build human-readable service list
        std::map<std::string, BLERemoteService*>* services = pClient->getServices();
        if (services && services->size() > 0) {
          Serial.printf("  Found %d GATT services:\n", services->size());
          int serviceCount = 0;
          for (auto service : *services) {
            String serviceUUID = service.second->getUUID().toString();
            const char* serviceName = getServiceName(serviceUUID.c_str());

            if (serviceName) {
              Serial.printf("    - %s (%s)\n", serviceName, serviceUUID.c_str());
              if (gattServices.length() > 0) {
                gattServices += ", ";
              }
              gattServices += serviceName;

              // Try classification by service UUID if not already classified
              if (!deviceType) {
                deviceType = classifyByServiceUUID(serviceUUID.c_str());
              }
            } else {
              // Unknown services go to Serial only (not syslog)
              Serial.printf("    - Unknown (%s)\n", serviceUUID.c_str());
            }
            
            // Flash LED every 200ms or every 2 services (whichever comes first)
            serviceCount++;
            if (millis() - lastLEDToggle >= 200 || serviceCount % 2 == 0) {
              ledState = !ledState;
              rgbled.set(ledState ? "blue" : "black", 50);
              lastLEDToggle = millis();
            }
          }
        }
        pClient->disconnect();
        
        // Turn off LED after GATT connection completes
        rgbled.set("black", 50);
      } else {
        Serial.println("  GATT connection failed (device may not accept connections)");
        // Turn off LED after failed connection
        rgbled.set("black", 50);
      }
      delete pClient;
    }
  }
#endif

  // Get label for device type if classified
  if (deviceType) {
    deviceLabel = getDeviceTypeLabel(deviceType);
  }

  // Build device info string with manufacturer, type, and GATT-discovered details
  char deviceInfo[512] = "";
  char buffer[512] = "";

  if (manufacturerName && strlen(deviceLabel) > 0) {
    snprintf(buffer, sizeof(buffer), " [%s %s]", manufacturerName, deviceLabel);
  } else if (manufacturerName) {
    snprintf(buffer, sizeof(buffer), " [%s]", manufacturerName);
  } else if (strlen(deviceLabel) > 0) {
    snprintf(buffer, sizeof(buffer), " [%s]", deviceLabel);
  }
  strncpy(deviceInfo, buffer, sizeof(deviceInfo) - 1);

  if (gattDeviceName.length() > 0) {
    snprintf(buffer, sizeof(buffer), "%s Name:\"%s\"", deviceInfo, gattDeviceName.c_str());
    strncpy(deviceInfo, buffer, sizeof(deviceInfo) - 1);
  }

  if (gattServices.length() > 0) {
    snprintf(buffer, sizeof(buffer), "%s Services:[%s]", deviceInfo, gattServices.c_str());
    strncpy(deviceInfo, buffer, sizeof(deviceInfo) - 1);
  }

  // --- Emit syslog based on device history ---

  // Device not in database — insert it
  if (lastSeen == -1) {
    char logMsg[640];
    snprintf(logMsg, sizeof(logMsg), "Device ID %s%s%s -> NOT PREVIOUSLY SEEN.",
             macAddress, randomizedTag, deviceInfo);
    Serial.println(logMsg);
    sendSyslog(logMsg);
    insertNewDevice(macAddress, currentTime);
    return;
  }

  // Device is squelched (seen too recently)
  if (lastSeen >= squelchTime) {
    char timeStr[32];
    formatTimestamp(lastSeen, timeStr, sizeof(timeStr));
    char logMsg[640];
    snprintf(logMsg, sizeof(logMsg), "Device ID %s%s%s SQUELCHED. -> Last Seen: %s",
             macAddress, randomizedTag, deviceInfo, timeStr);
    Serial.println(logMsg);
    sendSyslog(logMsg);
    return;
  }

  // Device seen outside squelch period — update record
  char timeStr[32];
  formatTimestamp(lastSeen, timeStr, sizeof(timeStr));
  char logMsg[640];
  snprintf(logMsg, sizeof(logMsg), "Device ID %s%s%s -> Last Seen: %s",
           macAddress, randomizedTag, deviceInfo, timeStr);
  Serial.println(logMsg);
  sendSyslog(logMsg);

  if (updateDeviceTimesSeen(macAddress)) {
    updateDeviceLastSeen(macAddress, currentTime);
  }
}
