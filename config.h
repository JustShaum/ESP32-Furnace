#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>
#include <IPAddress.h>
#include <time.h>



// =================================================================
//                      FIRMWARE CONFIGURATION
// =================================================================
#define FIRMWARE_VERSION "V26.0-Programs"

// =================================================================
//                          DEBUG & TEST MODES
// =================================================================
// Global debug flag for all files
// #define DEBUG

// Uncomment to enable fake temperature readings for testing without a thermocouple.
// #define FAKE_TEMPERATURE_MODE

// Uncomment to use hardcoded WiFi credentials for faster testing.
// #define HARDCODED_WIFI_TEST

// =================================================================
//                          PIN DEFINITIONS
// =================================================================
#define RELAY_PIN 27
#define MAX31855_CS_PIN 21
#define MAX31855_SCK_PIN 22
#define MAX31855_MISO_PIN 35

// =================================================================
//                          NETWORK CONFIGURATION
// =================================================================
#define AP_SSID "Furnace_Control"
#define DNS_PORT 53
#define NTP_SERVER "pool.ntp.org"
const long GMT_OFFSET_SEC = 0;
const int DAYLIGHT_OFFSET_SEC = 3600;

// =================================================================
//                          SYSTEM SETTINGS
// =================================================================
#define MAX_PROGRAMS 10
#define MAX_FAILED_READINGS 5
#define TEMP_READINGS_PER_HOUR 4  // Number of target temperature readings per hour (4 = every 15 minutes)
const int SMOOTHING_UPDATE_INTERVAL = 30000; // 30 seconds

// =================================================================
//                          FILE SYSTEM PATHS
// =================================================================
#define TEMP_LOG_FILE "/temp_log.csv"

#define THEME_CONFIG_FILE "/theme.json"
#define PROGRAMS_FILE "/programs.json"

// =================================================================
//                      FORWARD DECLARATIONS
// =================================================================
// Forward declarations for WiFi credentials
#ifdef HARDCODED_WIFI_TEST
  extern const char* TEST_WIFI_SSID;
  extern const char* TEST_WIFI_PASS;
#endif

// System Settings Structure
struct SystemSettings {
    bool pwmEnabled;
    float pwmFrequency;
    // Add other system settings fields as needed
};

// WiFi Credentials Structure
struct WiFiCredentials {
    String ssid;
    String password;
    bool use_static_ip;
    IPAddress ip;
    IPAddress gateway;
    IPAddress subnet;
    IPAddress dns;
};

// =================================================================
//                      GLOBAL VARIABLE DECLARATIONS
// =================================================================
// Web Server
extern class AsyncWebServer server;

// Temperature and System State
extern float currentTemp;
extern bool furnaceStatus;
extern bool systemEnabled;
extern float minTemp;
extern float maxTemp;

// Time and Logging
extern bool useManualTime;
extern unsigned long loggingFrequencySeconds;
extern unsigned long tempLogCleanupMinutes;
extern struct tm manualTimeSettings;

// Temperature Control
extern bool temperatureSmoothingEnabled;
extern float temperatureIncrement;
extern float* targetTemp;
extern unsigned long lastSmoothingUpdate;

// System Settings
extern SystemSettings systemSettings;

// Theme Settings
extern String primaryColor;
extern String backgroundColor;
extern String cardBackground;
extern String textColor;
extern String borderColor;
extern String highlightColor;

// Program Management
extern String programNames[MAX_PROGRAMS];
extern int activeProgram;
void loadProgram(int programIndex);
void saveProgram(int programIndex, String programName);
void saveAllPrograms();

// Temperature resolution settings
extern int tempResolution;
extern int maxTempPoints;

// Function Declarations
void setupWebServer();
void setupCaptivePortal();

bool deleteRecursive(const String& path);

extern float** programTemps;

#endif // CONFIG_H
