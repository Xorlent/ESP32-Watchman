# ESP32-Watchman
## PoE-powered remote facility monitor that scans for and alerts on nearby Bluetooth devices
### *To-do: Optionally alert on motion*
> [!NOTE]
> There have been recent news articles about ESP32 hidden features, but [researchers disagree about whether they constitute a backdoor](https://darkmentor.com/blog/esp32_non-backdoor) or are simply not publicly documented.

## Requirements
1. M5Stack [AtomPoE Base W5500](https://shop.m5stack.com/products/atomic-poe-base-w5500), currently $18.50 USD
2. M5Stack [Atom S3 Lite](https://shop.m5stack.com/products/atoms3-lite-esp32s3-dev-kit), currently $7.50 USD
3. Optional (not yet implemented in code) Either:
   - A [Ultrasonic Distance I2C](https://shop.m5stack.com/products/ultrasonic-distance-unit-i2c-rcwl-9620), currently $5.95 USD
   - A [DLight Unit](https://shop.m5stack.com/products/dlight-unit-ambient-light-sensor-bh1750fvi-tr), currently $5.50

## Cost Analysis
Total cost per assembly: Under $32 plus tax and shipping  
Programming time per unit: < 10 minutes  

## Device Capability Comparison
This project generates Syslog notifications for all detected activity.  A re-flash/re-programming is required to modify any configuration options:
- Host name
- Device IP and subnet
- IP gateway
- Syslog server
- NTP server
- Bluetooth detection thresholds

## Programming
_Once you've successfully programmed a single unit, skip step 1.  Repeating this process takes 5 minutes from start to finish._
1. [Set up your Arduino programming environment](https://github.com/Xorlent/ESP32-Watchman/blob/main/ARDUINO-SETUP.md)
2. In Arduino, open the project file (PoESP32-Watchman.ino)
   - Edit the configuration parameters at the very top of the file.
   - Select Tools->Board->esp32 and select "ESP32 S3 PowerFeather"
3. From your computer, plug a USB C cable into the Atom S3 Lite [pic](https://github.com/Xorlent/ESP32-Watchman/blob/main/images/4-Programmer.jpg)
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
6. Unplug the Atom S3 Lite from your computer
7. Plug in the desired sensor module [pic](https://github.com/Xorlent/ESP32-Watchman/blob/main/images/5-Assembled.jpg)
8. Connect the assembly to a PoE network port and mount as appropriate
   - See the /3Dmodels folder for print-able mounting plates or [Guidance and Limitations](https://github.com/Xorlent/ESP32-Watchman/blob/main/README.md#guidance-and-limitations) for more detail
9. Configure your Syslog alerting as appropriate

## Guidance and Limitations
- The device will respond to pings from any IP address within the routable network.
- Don't have PoE ports on your network switch?  No problem: https://www.amazon.com/gp/product/B0C239DGJF

## Technical Information
- Operating Specifications
  - Operating temperature: 0°F (-17.7°C) to 140°F (60°C)
  - Operating humidity: 5% to 90% (RH), non-condensing
- Motion Sensor Range
  - Up to 3.5 meters
- Light Sensor Range
  - 1 lux minimum
- Power Consumption
  - 6W maximum via 802.3af Power-over-Ethernet
- Ethernet
  - W5500 PHY
  - 10/100 Mbit twisted pair copper
  - IEEE 802.3af Power-over-Ethernet
