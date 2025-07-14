# ESP32-Furnace

An electric furnace controller intended to be run on the ESP32-2432S028 CYD. Running a MAX31855 thermocouple temperature board for a K-Type temp probe. On a 2MB/2MB (No OTA) partition scheme.
Provided a way to schedule programs, and automate the runtime of an electric furnace over the course of 24 hours with GUI's through both the TFT touchscreen and web. 

Running on Arduino IDE
esp32 by Espessif Systems - 3.2.0 installed. 

## Installed Libraries

- NTPclient by Fabrice Weinberg - 3.2.1
- Adafruit Max31855 by Adafruit - 1.4.2
- ArduinoJson by Benoit Blanchon - 7.4.1
- Async TCP by ESP32Async - 3.4.1
- TFT_eSPI by Bodmer - 2.5.43
- Time by Michael Margolis - 1.6.1
- XPT2046_Touchscreen by Paul Soffregen - 1.4
- ESPAsyncWebServer by lacamera - 3.1.0

# Please note I do NOT know how to write code, at best I can understand some of it. This was built using LLM's and a lot of iterations for bugs, features and general styling. There was just a gap that needed to be filled. 
