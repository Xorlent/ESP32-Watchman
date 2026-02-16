# ESP32-Watchman
## PoE-powered remote facility monitor that scans for and alerts on nearby Bluetooth devices and light level changes, or detected motion
## Background
Remote, unmanned facilites can provide ample opportunity for tampering, physical theft, and network penetration attacks.  The M5Stack AtomPoE together with the ESP32 S3 SoC can be used as a physical perimeter sensing device, sending detection information to a configured Syslog server.  These Syslog messages can help provide visibility into activity that may indicate traditional physical security measures have been circumvented.  In conjunction with an [ESP32 Honeypot](https://github.com/Xorlent/The-26-Dollar-Honeypot), you can build full physical and cyber sensing capabilities for a total cost of under $70.

## Requirements
1. M5Stack [AtomPoE Base W5500](https://shop.m5stack.com/products/atomic-poe-base-w5500)
2. M5Stack [Atom S3 Lite](https://shop.m5stack.com/products/atoms3-lite-esp32s3-dev-kit)
3. Optionally, either:
   - A [Ultrasonic Distance I2C](https://shop.m5stack.com/products/ultrasonic-distance-unit-i2c-rcwl-9620)
   - A [DLight Unit](https://shop.m5stack.com/products/dlight-unit-ambient-light-sensor-bh1750fvi-tr)
4. A Syslog server to receive alerts from the device

## Configuration
All configuration is managed through a serial terminal interface. On first boot, the device automatically enters configuration mode. After initial setup, you can re-enter configuration mode at any time by pressing **'C'** within 5 seconds of device startup.

Configurable options:
- Host name
- Device IP and subnet
- IP gateway
- DNS server
- Syslog server
- NTP server
- Bluetooth RSSI threshold
- Dwell time (60-86400 seconds) - time to wait between detections of the same device

Configuration changes are saved to non-volatile memory and persist across reboots.

## LED Status Indicators
The device uses an RGB LED to indicate its current status:
- **Red** - Missing Ethernet PHY (Is the AtomPoE sled connected?)
- **Yellow** - Waiting for Ethernet link to come up
- **Green** - Connected
- **Purple** - NTP sync failure
- **Blue** - Active Bluetooth poll

## Programming
_Once you've successfully programmed a single unit, skip step 1.  Repeating this process takes 5 minutes from start to finish._
1. [Set up your Arduino programming environment](https://github.com/Xorlent/ESP32-Watchman/blob/main/ARDUINO-SETUP.md)
2. In Arduino, open the project file (PoESP32-Watchman.ino)
   - Edit the configuration parameters at the very top of the file.
   - Select Tools->Board->esp32 and select "M5AtomS3"
3. From your computer, plug a USB C cable into the Atom S3 Lite
> [!WARNING]
> Do not plug the device into Ethernet until after step 7 or you risk damaging your USB port!
4. Push the button on the side of the Atom S3 Lite for 3 seconds.  You will see a blue LED briefly light up.
   - The device is now in bootloader mode
5. In Arduino
   - Select Tools->Port and select the Atom S3 device (usually usbmodem 101 on MacOS)
     - If you're unsure, unplug the Atom S3, look at the port list, then plug it back in and select the new entry (repeating step 5)
   - Select Sketch->Upload to flash the device
   - When you see something similar to the following, proceed to step 7
     ```
     Writing at 0x000d0502... (100 %)
     Wrote 790896 bytes (509986 compressed) at 0x00010000 in 8.9 seconds (effective 714.8 kbit/s)...
     Hash of data verified.

     Leaving...
     Hard resetting via RTS pin...
6. Switch to the Serial Monitor (Tools->Serial Monitor) and configure the device
7. Unplug the Atom S3 Lite from your computer
8. (Optional) Connect the desired sensor module
9. Connect the assembly to a PoE network port and mount as appropriate
10. Configure your Syslog alerting as appropriate

## Guidance and Limitations
- The device will respond to pings from any IP address within the routable network.
- Don't have PoE ports on your network switch?  No problem: https://www.amazon.com/gp/product/B0C239DGJF

## Technical Information
- Operating Specifications
  - Operating temperature: 0°F (-17.7°C) to 140°F (60°C)
  - Operating humidity: 5% to 90% (RH), non-condensing
- Motion Sensor Range
  - Up to 4.5 meters
- Light Sensor Range
  - 1 lux minimum
- Power Consumption
  - 6W maximum via 802.3af Power-over-Ethernet
- Ethernet
  - W5500 PHY
  - 10/100 Mbit twisted pair copper
  - IEEE 802.3af Power-over-Ethernet
