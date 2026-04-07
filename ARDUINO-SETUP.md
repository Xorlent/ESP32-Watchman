1. Download the latest Arduino IDE appropriate for your operating system (https://www.arduino.cc/en/software)
2. Open the Arduino IDE
3. Open the Preferences window
   - In the "Additional Board Manager URLs" field, paste the following:
   ```https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json```
4. Close/save the Preferences window
5. Open the Boards Manager
   - Type "esp32" in the search field and select "esp32 by Espressif Systems" (__NOT "Arduino ESP32 Boards"__)
     - Install version 3.3.7
6. Open the Libraries Manager
   - Type "ESP32IMDB" in the search field and select "ESP32IMDB by Xorlent"
     - Install version 1.1.3
   - Type "ESP32_WS2812B" in the search field and select "ESP32_WS2812B by Xorlent"
     - Install version 1.0.5
   - Type "NTP" in the search field and select "NTP by Stefan Staub"
     - Install version 1.8.0
