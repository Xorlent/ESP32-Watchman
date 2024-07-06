# ESP32-Watchman
## Work in progress...
PoE-powered device that monitors for nearby Bluetooth devices and optionally alerts on lighting or motion changes



# Remote Facility Monitor with Bluetooth plus motion or light sensing
## Background
For remote facilities that have limited bandwidth available for video streaming, this device can notify 

## Requirements
1. M5Stack [AtomPoE Base W5500](https://shop.m5stack.com/products/atomic-poe-base-w5500), currently $18.50 USD
2. M5Stack [Atom S3 Lite](https://shop.m5stack.com/products/atoms3-lite-esp32s3-dev-kit), currently $7.50 USD
3. Either:
   - A [Ultrasonic Distance I2C](https://shop.m5stack.com/products/ultrasonic-distance-unit-i2c-rcwl-9620), currently $5.95 USD
   - A [DLight Unit](https://shop.m5stack.com/products/dlight-unit-ambient-light-sensor-bh1750fvi-tr), currently $5.50

## Cost Analysis
Total cost per assembly: Under $32 plus tax and shipping  
Programming time per unit: < 10 minutes  

## Device Cost Comparison (temperature/humidity only)
- $32: this PoESP32-based device
- $220 before stop-ship: Vertiv Watchdog 15P (discontinued)
- $190 on sale: AKCP sensorProbe1+ Pro
- $315: NTI E-MICRO-TRHP
- $199: MONNIT PoE-X Temperature
- $295: Room Alert 3S

## Device Capability Comparison
This project produces a SNMPv1/2c temperature and humidity monitoring device with flashed configuration settings and no remote management capability.  Some would see this as a positive from a security-perspective, but it could prove challenging in network environments where change is constant.  A re-flash/re-programming is required to modify any configuration options:
- Host name
- Device IP and subnet
- IP gateway
- SNMP read community string
- Authorized SNMP monitoring node IP address list

__Bottom line: If you need SNMPv3 or desire web management and/or SNMP write functionality, you could enhance this project's code or simply purchase a commercial product.__

## Programming
_Once you've successfully programmed a single unit, skip step 1.  Repeating this process takes 5 minutes from start to finish._
1. [Set up your Arduino programming environment](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/ARDUINO-SETUP.md)
2. Disassemble the PoESP32 case
   - You will need a 1.5mm (M2) allen wrench to remove a single screw [pic](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/1-Allen.jpg)
   - Inserting a small flat head screwdriver into the slots flanking the Ethernet jack [pic](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/2-Slots.jpg), carefully separate the case halves; work it side by side to avoid damage [pic](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/3-Tabs.jpg)
> [!TIP]
> If you have fingernails, it can be quicker to slide a nail between the case halves, starting with the end opposite the Ethernet port and using another nail to pull the retaining tabs back
3. In Arduino, open the project file (PoESP32-SNMP-Sensor.ino)
   - Edit the hostname, IP address, subnet, gateway, SNMP read community, and authorized hosts lists at the very top of the file.
   - Select Tools->Board->esp32 and select "ESP32 Dev Module"
4. With the USB-to-serial adapter unplugged, insert the pins in the correct orientation on the back of the PoESP32 mainboard [pic](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/4-Programmer.jpg)
> [!WARNING]
> Do not plug the PoESP32 device into Ethernet until after step 7 or you risk damaging your USB port!
5. With light tension applied to ensure good connectivity to the programming through-hole vias on the PoESP32 (see step 4 pic), plug in the USB-to-serial adapter
   - The device is now in bootloader mode
6. In Arduino
   - Select Tools->Port and select the USB-to-serial adapter
     - If you're unsure, unplug the USB-to-serial adapter, look at the port list, then plug it back in and select the new entry (repeating step 5)
   - Select Sketch->Upload to flash the device
   - When you see something similar to the following, proceed to step 7
     ```
     Writing at 0x000d0502... (100 %)
     Wrote 790896 bytes (509986 compressed) at 0x00010000 in 8.9 seconds (effective 714.8 kbit/s)...
     Hash of data verified.

     Leaving...
     Hard resetting via RTS pin...
7. Disconnect the USB-to-serial adapter and reassemble the case
8. Plug in the ENV IV sensor unit [pic](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/images/5-Assembled.jpg)
9. Connect the PoESP32 to a PoE network port and mount as appropriate
   - The holes in the PoESP32 and ENV IV sensor cases work great with zip ties for rack install or screws if attaching to a backboard
     - See the /3Dmodels folder for print-able mounting plates or [Guidance and Limitations](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/README.md#guidance-and-limitations) for more detail
   - Do not mount the ENV IV directly on top of the PoESP32, as it generates enough heat to affect sensor readings
10. Configure your monitoring platform as appropriate
    - A list of valid OIDs this sensor will respond to can be found [here](https://github.com/Xorlent/PoESP32-SNMP-Environmental-Monitor/blob/main/OIDINFO.md)
    - Paessler (PRTG) produce a great freely-downloadable SNMP tester for Windows, available [here](https://www.paessler.com/tools/snmptester)
    - If you have PRTG, pre-configured device templates are available for this project at https://github.com/Xorlent/PRTG-OIDLIBS
    - Don't have a monitoring platform?  [PRTG Freeware](https://www.paessler.com/free_network_monitor) would support monitoring and alerting for up to 20 of these devices

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
