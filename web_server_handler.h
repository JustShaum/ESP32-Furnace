#ifndef WEB_SERVER_HANDLER_H
#define WEB_SERVER_HANDLER_H

#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <time.h>
#include <Preferences.h>

// Forward declaration of AsyncWebServer
class AsyncWebServer;

// Declare the server variable as extern
extern AsyncWebServer server;

// System Settings Struct (defined in config.h)
struct SystemSettings;  // Forward declaration

// Theme Settings
struct ThemeSettings {
    String primaryColor;
    String backgroundColor;
    String cardBackground;
    String textColor;
    String borderColor;
    String highlightColor;
    bool isDarkMode;
};

// Function declarations
void syncTime();
float getSmoothedTargetTemperature();

// Forward declarations for global variables
extern float currentTemp;
extern bool furnaceStatus;
extern bool systemEnabled;
extern float minTemp;
extern float maxTemp;
extern bool useManualTime;
extern unsigned long loggingFrequencySeconds;
extern unsigned long tempLogCleanupMinutes;
extern bool temperatureSmoothingEnabled;
extern float temperatureIncrement;
extern struct tm manualTimeSettings;
extern SystemSettings systemSettings;
extern bool pwmEnabled;
extern float pwmFrequency;
extern String primaryColor;
extern String backgroundColor;
extern String cardBackground;
extern String textColor;
extern String borderColor;
extern String highlightColor;
extern bool isDarkMode;

// Temperature control variables
extern float* targetTemp; // Dynamically allocated array for temperature targets

// Program Management
// MAX_PROGRAMS is defined in config.h
extern String programNames[MAX_PROGRAMS];
extern int activeProgram;
void loadProgram(int programIndex);
void saveProgram(int programIndex, String programName);
void saveAllPrograms();

// Function declarations
void setupCaptivePortal();
void setupWebServer();
bool deleteRecursive(const String& path);
void logError(const String& error);
String getFullTimestamp();
int getCurrentTempIndex();
String getCurrentTime();

#endif // WEB_SERVER_HANDLER_H
