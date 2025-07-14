#include "config.h"
#include <WiFi.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>
#include <SPI.h>
#include <Adafruit_MAX31855.h>
#include <time.h>
#include <Preferences.h>
#include <ESPmDNS.h>
#include <DNSServer.h>

#include "wifi_manager.h"
#include "web_server_handler.h"
#include "temperature_log_handler.h"
#include "tft_integration.h"

Preferences preferences;

// Define global variables that are declared as extern in other files
AsyncWebServer server(80);
DNSServer dnsServer;

// Temperature and System State
float currentTemp = 0.0;
bool furnaceStatus = false;
bool systemEnabled = true;
float minTemp = 0.0;
float maxTemp = 1200.0;

// Time and Logging
bool useManualTime = false;
unsigned long loggingFrequencySeconds = 60;
unsigned long errorCleanupMinutes = 300;
unsigned long tempLogCleanupMinutes = 1440;

// Temperature Control
bool temperatureSmoothingEnabled = true;
float temperatureIncrement = 25.0;

// Manual Time Settings
struct tm manualTimeSettings;

// System Settings
SystemSettings systemSettings = {
    .pwmEnabled = true,
    .pwmFrequency = 1000.0
};

// Theme Settings - Light Mode (default)
String primaryColor = "#4CAF50";
String backgroundColor = "#f5f5f5";
String cardBackground = "#ffffff";
String textColor = "#333333";
String borderColor = "#e0e0e0";
String highlightColor = "#e9f7fe";
bool isDarkMode = false;

// Profile Management
String programNames[MAX_PROGRAMS] = {"Default", "", "", "", "", "", "", "", "", ""};
int activeProgram = 0;

#ifdef HARDCODED_WIFI_TEST
const char* TEST_WIFI_SSID = "";
const char* TEST_WIFI_PASS = "";
#endif

// Device initialization
Adafruit_MAX31855 thermocouple(MAX31855_SCK_PIN, MAX31855_CS_PIN, MAX31855_MISO_PIN);

// Temperature resolution settings
int tempResolution = 4;
int maxTempPoints = 96;

// Time zone settings
int utcOffset = 0;

// Make sure these variables are accessible to other compilation units
extern int tempResolution;
extern int maxTempPoints;

// Temperature variables
float* targetTemp = NULL;
float** programTemps = NULL;
unsigned long lastSmoothingUpdate = 0;

// Logging configuration
unsigned long lastTempLogCleanupCheck = 0;
unsigned long lastLogTime = 0;
#define MAX_LOG_SIZE 1440
#define TEMP_LOG_FILE "/temp_log.csv"

// Timing variables
unsigned long lastTempCheck = 0;
unsigned long lastDisplayUpdate = 0;
unsigned long lastTimeSync = 0;
unsigned long lastWifiCheck = 0;
unsigned long lastDnsCheck = 0;
unsigned long lastManualTimeUpdate = 0;

// PWM variables
bool pwmEnabled = true;
float pwmFrequency = 1000.0;
unsigned long pwmCycleStart = 0;
unsigned long pwmPeriodMs = 10000;
unsigned long pwmOnTimeMs = 0;
bool pwmRelayState = false;

// PID Control variables
bool pidEnabled = false;
float pidKp = 2.0;
float pidKi = 0.1;
float pidKd = 0.05;
float pidSampleTime = 1.0;
int pidOutputMin = 0;
int pidOutputMax = 100;
float pidSetpointWindow = 2.0;
float pidIntegral = 0.0;
float pidLastError = 0.0;
unsigned long pidLastTime = 0;

// Error handling variables
bool thermocoupleError = false;
bool timeIsSynchronized = false;
int failedTempReadings = 0;

// Restart management
bool shouldRestart = false;
unsigned long restartTime = 0;

// Theme save management - non-blocking file operations
bool pendingThemeSave = false;
String pendingThemeJson = "";

// Function prototypes
void startAccessPoint();
void loadWifiConfig();
void saveWifiConfig();
bool deleteRecursive(const String& path);
void generateRandomPassword();
void createDefaultConfig();
void connectToWifi();
void setupCaptivePortal();
void checkWifiConnection();
void syncTime();
int getCurrentHour();
int getCurrentMinute();
int getCurrentTempIndex();
String getCurrentTime();
String getFullTimestamp();
void readTemperature();
void controlFurnace();
void logTemperature();
void loadProgramsFromSPIFFS();
void saveAllPrograms();
void loadProgram(int programIndex);
void saveProgram(int programIndex, String programName);
void setupWebServer();
void initializeTemperatureArrays();
float getSmoothedTargetTemperature();
void checkTempLogCleanup();
void checkLogFiles();
// Add PID function prototypes
void resetPID();
float calculatePIDOutput(float setpoint, float input, unsigned long currentTime);
void processPendingThemeSave();

void checkTempLogCleanup() {
  if (tempLogCleanupMinutes <= 0) return;

  unsigned long currentMillis = millis();
  if (currentMillis - lastTempLogCleanupCheck >= (unsigned long)tempLogCleanupMinutes * 60000) {
    lastTempLogCleanupCheck = currentMillis;
    if (SPIFFS.exists(TEMP_LOG_FILE)) {
      File newFile = SPIFFS.open(TEMP_LOG_FILE, FILE_WRITE);
      if (newFile) {
        newFile.println("Timestamp,Temperature,Target,FurnaceStatus");
        newFile.close();
      }
    }
  }
}

void initializeTemperatureArrays() {
  if (targetTemp != NULL) {
    free(targetTemp);
    targetTemp = NULL;
  }
  
  if (programTemps != NULL) {
    for (int i = 0; i < MAX_PROGRAMS; i++) {
      if (programTemps[i] != NULL) {
        free(programTemps[i]);
        programTemps[i] = NULL;
      }
    }
    free(programTemps);
    programTemps = NULL;
  }
  
  maxTempPoints = 24 * tempResolution;
  
  targetTemp = (float*)malloc(maxTempPoints * sizeof(float));
  if (targetTemp == NULL) {
    return;
  }
  
  for (int i = 0; i < maxTempPoints; i++) {
    targetTemp[i] = 0.0;
  }
  
  programTemps = (float**)malloc(MAX_PROGRAMS * sizeof(float*));
  if (programTemps == NULL) {
    Serial.println("ERROR: Failed to allocate memory for programTemps array");
    return;
  }
  
  for (int i = 0; i < MAX_PROGRAMS; i++) {
    programTemps[i] = (float*)malloc(maxTempPoints * sizeof(float));
    if (programTemps[i] == NULL) {
      Serial.print(F("Failed to allocate memory for program "));
      Serial.println(i);
      return;
    }
    
    for (int j = 0; j < maxTempPoints; j++) {
      programTemps[i][j] = 0.0;
    }
  }
}

void listSPIFFSFiles(const char* dirname = "/", uint8_t levels = 2) {
  File root = SPIFFS.open(dirname);
  if (!root) {
    return;
  }
  if (!root.isDirectory()) {
    root.close();
    return;
  }
  File file = root.openNextFile();
  int fileCount = 0;
  while (file) {
    if (file.isDirectory()) {
      if (levels) {
        listSPIFFSFiles(file.path(), levels - 1);
      }
    }
    file = root.openNextFile();
    fileCount++;
  }
  root.close();
}

void setup() {
  Serial.begin(115200);
  Serial.println("Furnace Controller Starting...");
  
  while (!Serial);
  
  bool spiffsMounted = SPIFFS.begin(true);
  
  if (!spiffsMounted) {
    Serial.println("SPIFFS mount failed, attempting to format...");
    if (SPIFFS.format()) {
      Serial.println("SPIFFS formatted successfully");
      spiffsMounted = SPIFFS.begin(true);
    }
    
    if (!spiffsMounted) {
      Serial.println("ERROR: SPIFFS initialization failed!");
      return;
    }
  }
  
  Serial.println("SPIFFS initialized successfully");
  
  size_t totalBytes = SPIFFS.totalBytes();
  size_t usedBytes = SPIFFS.usedBytes();
  
  if (!SPIFFS.exists(TEMP_LOG_FILE)) {
    File file = SPIFFS.open(TEMP_LOG_FILE, FILE_WRITE);
    if (file) {
      file.println("Timestamp,Temperature,Target,FurnaceStatus");
      file.close();
    }
  }
  
  listSPIFFSFiles();
  
  if (SPIFFS.exists(TEMP_LOG_FILE)) {
  } else {
  }

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, LOW);

  loadAppSettings();
  loadWifiConfig();
  
  initializeTemperatureArrays();

  if (!SPIFFS.exists(TEMP_LOG_FILE)) {
    File file = SPIFFS.open(TEMP_LOG_FILE, FILE_WRITE);
    if (!file) {
      size_t total = SPIFFS.totalBytes();
      size_t used = SPIFFS.usedBytes();
      
      File testFile = SPIFFS.open("/test_file.txt", FILE_WRITE);
      if (testFile) {
        testFile.println("Test file created successfully");
        testFile.close();
        
        SPIFFS.remove("/test_file.txt");
        
        file = SPIFFS.open(TEMP_LOG_FILE, FILE_WRITE);
        if (file) {
        }
      }
    }
    
    if (file) {
      size_t bytesWritten = file.println("Timestamp,Temperature,Target,FurnaceStatus");
      file.close();
      
      if (SPIFFS.exists(TEMP_LOG_FILE)) {
        File verifyFile = SPIFFS.open(TEMP_LOG_FILE, FILE_READ);
        if (verifyFile) {
          verifyFile.close();
        }
      }
    }
  } else {
    File file = SPIFFS.open(TEMP_LOG_FILE, FILE_READ);
    if (file) {
      if (file.size() > 0) {
        String firstLine = file.readStringUntil('\n');
      }
      file.close();
    }
  }

  if (!thermocouple.begin()) {
    Serial.println("ERROR: Thermocouple initialization failed!");
    thermocoupleError = true;
  } else {
    Serial.println("Thermocouple initialized successfully");
    double initialTemp = thermocouple.readCelsius();
    thermocoupleError = (isnan(initialTemp) || initialTemp == 0.0);
    if (thermocoupleError) {
      Serial.println("ERROR: Initial temperature reading failed!");
    } else {
      Serial.print("Initial temperature: ");
      Serial.print(initialTemp);
      Serial.println("°C");
    }
  }

#ifdef HARDCODED_WIFI_TEST
  connectToHardcodedWiFi();
  wifiConnected = (WiFi.status() == WL_CONNECTED);
#else
  WiFi.setHostname("furnace");
  connectToWifi();
#endif

  if (!wifiConnected) {
    Serial.println("WiFi connection failed, starting Access Point mode");
    startAccessPoint();
    setupCaptivePortal();
  } else {
    Serial.print("WiFi connected! IP address: ");
    Serial.println(WiFi.localIP());
    Serial.print("Hostname: ");
    Serial.println(WiFi.getHostname());
    
    if (!MDNS.begin("furnace")) {
      Serial.println("mDNS responder failed to start");
    } else {
      Serial.println("mDNS responder started");
    }
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
    syncTime();
    loadProgramsFromSPIFFS();
    if (programNames[0].length() > 0) {
      loadProgram(0);
    }
  }

  setupWebServer();
  checkLogFiles();
  
  // Reset PID controller on startup
  resetPID();
  
  // Initialize TFT display
  initializeTFT();
  
  Serial.println("Furnace Controller setup complete!");
  Serial.println("System ready for operation.");
  if (pidEnabled) {
    Serial.println("PID Control: ENABLED");
    Serial.print("PID Settings - Kp: ");
    Serial.print(pidKp);
    Serial.print(", Ki: ");
    Serial.print(pidKi);
    Serial.print(", Kd: ");
    Serial.print(pidKd);
    Serial.print(", Setpoint Window: ±");
    Serial.print(pidSetpointWindow);
    Serial.println("°C");
  } else {
    Serial.println("PID Control: DISABLED");
  }
  delay(2000);
}

void loop() {
  unsigned long currentMillis = millis();
 
  checkTempLogCleanup();

  if (!ap_active && currentMillis - lastWifiCheck >= 30000) {
    lastWifiCheck = currentMillis;
    checkWifiConnection();
  }
  
  // Handle DNS requests for captive portal - process more frequently for better responsiveness
  if (ap_active) {
    handleDNS();
  }

  if (wifiConnected && currentMillis - lastTimeSync >= 3600000) {
    lastTimeSync = currentMillis;
    if (!useManualTime) {
      syncTime();
    }
  }

  if (currentMillis - lastTempCheck >= 500) {
    lastTempCheck = currentMillis;
    readTemperature();
    if (!thermocoupleError && systemEnabled) {
      controlFurnace();
    } else {
      furnaceStatus = false;
      digitalWrite(RELAY_PIN, LOW);
    }
    
    if (currentMillis - lastLogTime >= (loggingFrequencySeconds * 1000)) {
      lastLogTime = currentMillis;
      if (!thermocoupleError) {
        logTemperature();
      }
    }
  }

  if (useManualTime && currentMillis - lastManualTimeUpdate >= 1000) {
    lastManualTimeUpdate = currentMillis;
    
    manualTimeSettings.tm_sec++;
    
    if (manualTimeSettings.tm_sec >= 60) {
      manualTimeSettings.tm_sec = 0;
      manualTimeSettings.tm_min++;
      
      if (manualTimeSettings.tm_min >= 60) {
        manualTimeSettings.tm_min = 0;
        manualTimeSettings.tm_hour++;
        
        if (manualTimeSettings.tm_hour >= 24) {
          manualTimeSettings.tm_hour = 0;
          manualTimeSettings.tm_mday++;
          
          if (manualTimeSettings.tm_mday > 31) {
            manualTimeSettings.tm_mday = 1;
            manualTimeSettings.tm_mon++;
            
            if (manualTimeSettings.tm_mon >= 12) {
              manualTimeSettings.tm_mon = 0;
              manualTimeSettings.tm_year++;
            }
          }
        }
      }
    }
  }

  if (shouldRestart && millis() > restartTime) {
    ESP.restart();
  }
  
  // Process any pending theme save operations (non-blocking)
  processPendingThemeSave();
  
  // Update TFT display
  updateTFT();
}

void createDefaultAppSettings() {
  preferences.begin("furnace", false);
  
  preferences.putBool("smoothingEnabled", true);
  preferences.putInt("tempResolution", 4);
  preferences.putInt("loggingFrequency", 60);
  preferences.putInt("tempLogCleanupMins", 1440);
  preferences.putInt("tempIncrement", 25);
  preferences.putBool("pwmEnabled", true);

  // PID Settings
  preferences.putBool("pidEnabled", false);
  preferences.putFloat("pidKp", 2.0);
  preferences.putFloat("pidKi", 0.1);
  preferences.putFloat("pidKd", 0.05);
  preferences.putFloat("pidSampleTime", 1.0);
  preferences.putInt("pidOutputMin", 0);
  preferences.putInt("pidOutputMax", 100);
  preferences.putFloat("pidSetpointWindow", 2.0);

  preferences.putString("primaryColor", "#4CAF50");
  preferences.putString("backgroundColor", "#f5f5f5");
  preferences.putString("cardBackground", "#ffffff");
  preferences.putString("textColor", "#333333");
  preferences.putString("borderColor", "#e0e0e0");
  preferences.putString("highlightColor", "#e9f7fe");
  preferences.putBool("isDarkMode", false);

  // Timezone settings
  preferences.putInt("utcOffset", 0);

  preferences.end();
}

void loadAppSettings() {
  preferences.begin("furnace", true);

  if (!preferences.isKey("smoothingEnabled")) {
    preferences.end();
    createDefaultAppSettings();
    preferences.begin("furnace", true);
  }
  temperatureSmoothingEnabled = preferences.getBool("smoothingEnabled", true);
  tempResolution = preferences.getInt("tempResolution", 4);
  maxTempPoints = 24 * tempResolution;
  loggingFrequencySeconds = preferences.getInt("loggingFrequency", 60);
  tempLogCleanupMinutes = preferences.getInt("tempLogCleanupMins", 1440);
  temperatureIncrement = preferences.getInt("tempIncrement", 25);
  pwmEnabled = preferences.getBool("pwmEnabled", true);
  systemSettings.pwmEnabled = pwmEnabled;

  // Load PID Settings
  pidEnabled = preferences.getBool("pidEnabled", false);
  pidKp = preferences.getFloat("pidKp", 2.0);
  pidKi = preferences.getFloat("pidKi", 0.1);
  pidKd = preferences.getFloat("pidKd", 0.05);
  pidSampleTime = preferences.getFloat("pidSampleTime", 1.0);
  pidOutputMin = preferences.getInt("pidOutputMin", 0);
  pidOutputMax = preferences.getInt("pidOutputMax", 100);
  pidSetpointWindow = preferences.getFloat("pidSetpointWindow", 2.0);

  primaryColor = preferences.getString("primaryColor", "#4CAF50");
  backgroundColor = preferences.getString("backgroundColor", "#f5f5f5");
  cardBackground = preferences.getString("cardBackground", "#ffffff");
  textColor = preferences.getString("textColor", "#333333");
  borderColor = preferences.getString("borderColor", "#e0e0e0");
  highlightColor = preferences.getString("highlightColor", "#e9f7fe");
  isDarkMode = preferences.getBool("isDarkMode", false);
  
  // Load timezone settings
  utcOffset = preferences.getInt("utcOffset", 0);
  
  preferences.end();
}

void saveAppSettings() {
  preferences.begin("furnace", false);

  preferences.putBool("smoothingEnabled", temperatureSmoothingEnabled);
  preferences.putInt("tempResolution", tempResolution);
  preferences.putInt("loggingFrequency", loggingFrequencySeconds);
  preferences.putInt("tempLogCleanupMins", tempLogCleanupMinutes);
  preferences.putInt("tempIncrement", temperatureIncrement);
  preferences.putBool("pwmEnabled", pwmEnabled);

  // Save PID Settings
  preferences.putBool("pidEnabled", pidEnabled);
  preferences.putFloat("pidKp", pidKp);
  preferences.putFloat("pidKi", pidKi);
  preferences.putFloat("pidKd", pidKd);
  preferences.putFloat("pidSampleTime", pidSampleTime);
  preferences.putInt("pidOutputMin", pidOutputMin);
  preferences.putInt("pidOutputMax", pidOutputMax);
  preferences.putFloat("pidSetpointWindow", pidSetpointWindow);

  preferences.putString("primaryColor", primaryColor);
  preferences.putString("backgroundColor", backgroundColor);
  preferences.putString("cardBackground", cardBackground);
  preferences.putString("textColor", textColor);
  preferences.putString("borderColor", borderColor);
  preferences.putString("highlightColor", highlightColor);
  preferences.putBool("isDarkMode", isDarkMode);

  // Save timezone settings
  preferences.putInt("utcOffset", utcOffset);

  preferences.end();
}

void syncTime() {
  if (!wifiConnected) {
    timeIsSynchronized = false;
    return;
  }

  configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET_SEC, NTP_SERVER);
  
  int retry = 0;
  const int maxRetries = 5;
  struct tm timeinfo;
  
  while (retry < maxRetries && !getLocalTime(&timeinfo)) {
    delay(1000);
    retry++;
  }

  if (retry >= maxRetries) {
    timeIsSynchronized = false;
    return;
  }

  timeIsSynchronized = true;
}

struct tm getAdjustedTime() {
  struct tm adjustedTime;
  
  if (useManualTime) {
    adjustedTime = manualTimeSettings;
    return adjustedTime;
  }

  if (!timeIsSynchronized) {
    memset(&adjustedTime, 0, sizeof(adjustedTime));
    return adjustedTime;
  }

  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) {
    memset(&adjustedTime, 0, sizeof(adjustedTime));
    return adjustedTime;
  }
  
  Preferences preferences;
  preferences.begin("furnace", true);
  int utcOffset = preferences.getInt("utcOffset", 0);
  preferences.end();
  
  adjustedTime = timeinfo;
  adjustedTime.tm_hour += utcOffset;
  
  if (adjustedTime.tm_hour < 0) {
    adjustedTime.tm_hour += 24;
  } else if (adjustedTime.tm_hour >= 24) {
    adjustedTime.tm_hour -= 24;
  }
  
  return adjustedTime;
}

int getCurrentHour() {
  struct tm adjustedTime = getAdjustedTime();
  return adjustedTime.tm_hour;
}

int getCurrentMinute() {
  struct tm adjustedTime = getAdjustedTime();
  return adjustedTime.tm_min;
}

int getCurrentTempIndex() {
    struct tm adjustedTime = getAdjustedTime();
    int hour = adjustedTime.tm_hour;
    int minute = adjustedTime.tm_min;
    
    return (hour * tempResolution) + (minute * tempResolution / 60);
}

String getCurrentTime() {
  if (useManualTime) {
    char timeString[15];
    sprintf(timeString, "%02d:%02d:%02d (M)", manualTimeSettings.tm_hour, manualTimeSettings.tm_min, manualTimeSettings.tm_sec);
    return String(timeString);
  }

  if (!timeIsSynchronized) {
    return F("Time not synced");
  }

  struct tm timeinfo;
  if(!getLocalTime(&timeinfo)) {
    return F("Time error");
  }
  
  Preferences preferences;
  preferences.begin("furnace", true);
  int utcOffset = preferences.getInt("utcOffset", 0);
  preferences.end();
  
  int hour = timeinfo.tm_hour + utcOffset;
  
  if (hour < 0) {
    hour += 24;
  } else if (hour >= 24) {
    hour -= 24;
  }
  
  char timeString[9];
  sprintf(timeString, "%02d:%02d:%02d", hour, timeinfo.tm_min, timeinfo.tm_sec);
  return String(timeString);
}

String getFullTimestamp() {
  struct tm adjustedTime = getAdjustedTime();
  
  char timeString[25];
  sprintf(timeString, "%04d-%02d-%02d %02d:%02d:%02d", 
          adjustedTime.tm_year + 1900, 
          adjustedTime.tm_mon + 1, 
          adjustedTime.tm_mday,
          adjustedTime.tm_hour, 
          adjustedTime.tm_min, 
          adjustedTime.tm_sec);
  return String(timeString);
}

float getSmoothedTargetTemperature() {
  if (!temperatureSmoothingEnabled) {
    int currentIndex = getCurrentTempIndex();
    return targetTemp[currentIndex];
  }
  
  struct tm adjustedTime = getAdjustedTime();
  int currentHour = adjustedTime.tm_hour;
  int currentMinute = adjustedTime.tm_min;
  int currentSecond = adjustedTime.tm_sec;
  
  int currentIndex = (currentHour * tempResolution) + (currentMinute * tempResolution / 60);
  int nextIndex = (currentIndex + 1) % maxTempPoints;
  
  int minutesInTimeSlot = 60 / tempResolution;
  int secondsInTimeSlot = minutesInTimeSlot * 60;
  int secondsInCurrentTimeSlot = (currentMinute % minutesInTimeSlot) * 60 + currentSecond;
  float progress = (float)secondsInCurrentTimeSlot / secondsInTimeSlot;
  
  float currentTemp = targetTemp[currentIndex];
  float nextTemp = targetTemp[nextIndex];
  
  float smoothedTemp = currentTemp + (nextTemp - currentTemp) * progress;
  
  return smoothedTemp;
}

void controlFurnace() {
  float currentTargetTemp;
  if (temperatureSmoothingEnabled) {
    currentTargetTemp = getSmoothedTargetTemperature();
  } else {
    int currentIndex = getCurrentTempIndex();
    currentIndex = max(0, min(currentIndex, maxTempPoints - 1));
    currentTargetTemp = targetTemp[currentIndex];
  }

  // Check if we're within the PID setpoint window
  float tempError = abs(currentTargetTemp - currentTemp);
  bool withinSetpointWindow = tempError <= pidSetpointWindow;

  if (pidEnabled && withinSetpointWindow) {
    // Use PID control when within setpoint window
    unsigned long now = millis();
    float pidOutput = calculatePIDOutput(currentTargetTemp, currentTemp, now);
    
    // Convert PID output (0-100) to PWM duty cycle
    float duty = pidOutput / 100.0;
    duty = constrain(duty, 0.0f, 1.0f);
    
    pwmOnTimeMs = (unsigned long)(pwmPeriodMs * duty);
    
    if (now - pwmCycleStart >= pwmPeriodMs) {
      pwmCycleStart = now;
    }
    
    if ((now - pwmCycleStart) < pwmOnTimeMs) {
      if (!pwmRelayState) {
        digitalWrite(RELAY_PIN, HIGH);
        pwmRelayState = true;
        furnaceStatus = true;
      }
    } else {
      if (pwmRelayState) {
        digitalWrite(RELAY_PIN, LOW);
        pwmRelayState = false;
        furnaceStatus = false;
      }
    }
  } else if (pwmEnabled) {
    // Use PWM control when outside setpoint window or PID disabled
    float error = currentTargetTemp - currentTemp;
    float maxErr = 10.0f;
    float minErr = -10.0f;
    float clampedError = constrain(error, minErr, maxErr);
    float duty = (clampedError + maxErr) / (2 * maxErr);
    duty = constrain(duty, 0.0f, 1.0f);
    pwmOnTimeMs = (unsigned long)(pwmPeriodMs * duty);
    
    unsigned long now = millis();
    if (now - pwmCycleStart >= pwmPeriodMs) {
      pwmCycleStart = now;
    }
    
    if ((now - pwmCycleStart) < pwmOnTimeMs) {
      if (!pwmRelayState) {
        digitalWrite(RELAY_PIN, HIGH);
        pwmRelayState = true;
        furnaceStatus = true;
      }
    } else {
      if (pwmRelayState) {
        digitalWrite(RELAY_PIN, LOW);
        pwmRelayState = false;
        furnaceStatus = false;
      }
    }
  } else {
    // Simple On/Off control
    if (currentTemp < currentTargetTemp) {
      if (!furnaceStatus) {
        furnaceStatus = true;
        digitalWrite(RELAY_PIN, HIGH);
      }
    } else if (currentTemp > currentTargetTemp) {
      if (furnaceStatus) {
        furnaceStatus = false;
        digitalWrite(RELAY_PIN, LOW);
      }
    }
  }
}

void readTemperature() {
#ifdef FAKE_TEMPERATURE_MODE
  float period_ms = 600000.0;
  float minFake = 100.0;
  float maxFake = 800.0;
  float amplitude = (maxFake - minFake) / 2.0;
  float offset = minFake + amplitude;
  float phase = (millis() % (long)period_ms) / period_ms * 2.0 * 3.14159265;
  currentTemp = offset + amplitude * sin(phase);
  failedTempReadings = 0;
  thermocoupleError = false;
#else
  double tempC = thermocouple.readCelsius();

  // Check for various error conditions
  bool hasError = false;
  
  // Check for NaN or invalid readings
  if (isnan(tempC)) {
    hasError = true;
  }
  
  // Check for specific MAX31855 error values
  else if (tempC < -200.0 || tempC > 1800.0) {
    // Temperature out of reasonable range for MAX31855
    hasError = true;
  }
  
  // Check for common disconnected sensor readings
  else if (tempC == 0.0 || tempC == -0.25 || tempC == -2048.0) {
    hasError = true;
  }
  
  // Use MAX31855 built-in error detection
  uint8_t fault = thermocouple.readError();
  if (fault != 0) {
    hasError = true;
    // Log specific fault type for debugging
    if (fault & MAX31855_FAULT_OPEN) {
      Serial.println("Thermocouple fault: Open circuit (no sensor connected)");
    }
    if (fault & MAX31855_FAULT_SHORT_GND) {
      Serial.println("Thermocouple fault: Short to ground");
    }
    if (fault & MAX31855_FAULT_SHORT_VCC) {
      Serial.println("Thermocouple fault: Short to VCC");
    }
  }
  
  if (hasError) {
    failedTempReadings++;

    if (failedTempReadings >= MAX_FAILED_READINGS) {
      thermocoupleError = true;
      currentTemp = 0.0; // Set to safe default when error detected
    }
  } else {
    failedTempReadings = 0;
    thermocoupleError = false;
    currentTemp = tempC;
  }
#endif
}

void logTemperature() {
  if (thermocoupleError) {
    return;
  }
  
  int currentIndex = getCurrentTempIndex();
  String timestamp = getFullTimestamp();
  String logLine = timestamp + "," + String(currentTemp, 1) + "," + 
                   String(targetTemp[currentIndex], 1) + "," +
                   (furnaceStatus ? "ON" : "OFF") + "\n";
  
  if (SPIFFS.exists(TEMP_LOG_FILE)) {
    File checkFile = SPIFFS.open(TEMP_LOG_FILE, FILE_READ);
    if (checkFile && checkFile.size() > MAX_LOG_SIZE * 1000) {
      checkFile.close();

      SPIFFS.remove(TEMP_LOG_FILE);
      
      File newFile = SPIFFS.open(TEMP_LOG_FILE, FILE_WRITE);
      if (newFile) {
        newFile.println("Timestamp,Temperature,Target,FurnaceStatus");
        newFile.print(logLine);
        newFile.close();
        return;
      } else {
        return;
      }
    } else if (checkFile) {
      checkFile.close();
    }
  }
  
  File file = SPIFFS.open(TEMP_LOG_FILE, FILE_APPEND);
  if (!file) {
    file = SPIFFS.open(TEMP_LOG_FILE, FILE_WRITE);
    if (file) {
      file.println("Timestamp,Temperature,Target,FurnaceStatus");
    } else {
      return;
    }
  }
  
  file.print(logLine);
  file.close();
}

void saveProgram(int programIndex, String programName) {
  if (programIndex < 0 || programIndex >= MAX_PROGRAMS) return;

  String previousProgramName = programNames[programIndex];
  bool isNewProgram = (previousProgramName.length() == 0);
  
  programNames[programIndex] = programName;
  
  if (isNewProgram) {
    for (int i = 0; i < maxTempPoints; i++) {
      int hour = (i * 60 / tempResolution) / 60;
      if (hour >= 6 && hour < 18) {
        programTemps[programIndex][i] = 200.0;
      } else {    
        programTemps[programIndex][i] = 100.0;
      }
    }
    
    if (maxTempPoints > 0) {
      programTemps[programIndex][maxTempPoints - 1] = 0.0;
    }
  } else {
    for (int i = 0; i < maxTempPoints; i++) {
      programTemps[programIndex][i] = targetTemp[i];
    }
  }
  
  if (maxTempPoints > 0) {
    programTemps[programIndex][maxTempPoints - 1] = 0.0;
  }

  saveAllPrograms();
}

void saveAllPrograms() {
  DynamicJsonDocument doc(8192);
  JsonArray programs = doc.createNestedArray("programs");

  for (int i = 0; i < MAX_PROGRAMS; i++) {
    if (programNames[i].length() > 0) {
      JsonObject program = programs.createNestedObject();
      program["name"] = programNames[i];
      program["index"] = i;
      JsonArray temps = program.createNestedArray("temps");
      for (int j = 0; j < maxTempPoints; j++) {
        temps.add(programTemps[i][j]);
      }
    }
  }

  File file = SPIFFS.open("/programs.json", FILE_WRITE);
  if (file) {
    serializeJson(doc, file);
    file.close();
  }
}

void loadProgram(int programIndex) {
  if (programIndex < 0 || programIndex >= MAX_PROGRAMS) return;
  if (programNames[programIndex].length() == 0) return;

  activeProgram = programIndex;
  for (int i = 0; i < maxTempPoints; i++) targetTemp[i] = programTemps[programIndex][i];
  
  // Force TFT UI refresh when program is loaded
  onTFTProgramChange(programIndex);
}

void loadProgramsFromSPIFFS() {
  if (SPIFFS.exists("/programs.json")) {
    File file = SPIFFS.open("/programs.json", FILE_READ);
    if (file) {
      DynamicJsonDocument doc(8192);
      DeserializationError error = deserializeJson(doc, file);
      file.close();

      if (!error) {
        for (int i = 0; i < MAX_PROGRAMS; i++) {
          programNames[i] = "";
          for (int j = 0; j < maxTempPoints; j++) {
            programTemps[i][j] = 0.0;
          }
        }
        
        JsonArray programs = doc["programs"].as<JsonArray>();
        for (JsonObject program : programs) {
          int index = program["index"];
          if (index >= 0 && index < MAX_PROGRAMS) {
            programNames[index] = program["name"].as<String>();
            JsonArray temps = program["temps"].as<JsonArray>();
            
            int numPoints = min((int)temps.size(), maxTempPoints);
            for (int i = 0; i < numPoints; i++) {
              programTemps[index][i] = temps[i].as<float>();
            }
            
            for (int i = numPoints; i < maxTempPoints; i++) {
              programTemps[index][i] = 0.0;
            }
          }
        }
      }
    }
  } else {
    programNames[0] = "Default";
    for (int i = 0; i < maxTempPoints; i++) {
      int hour = (i * 60 / tempResolution) / 60;
      if (hour >= 6 && hour < 18) {
        programTemps[0][i] = 200.0;
      } else {
        programTemps[0][i] = 100.0;
      }
    }
    
    if (maxTempPoints > 0) {
      programTemps[0][maxTempPoints - 1] = 0.0;
    }
    saveProgram(0, "Default");
  }
}

bool deleteRecursive(const String& path) {
  File f = SPIFFS.open(path, "r");
  if (!f) {
    return true;
  }

  bool isDir = f.isDirectory();
  f.close();

  if (!isDir) {
    if (SPIFFS.remove(path)) {
      return true;
    } else {
      return false;
    }
  } else {
    File dir = SPIFFS.open(path, "r");
    if (!dir) {
        return false;
    }

    File child = dir.openNextFile();
    while (child) {
      String childPath = String(child.path());
      child.close(); 
      if (!deleteRecursive(childPath)) {
        dir.close();
        return false;
      }
      child = dir.openNextFile();
    }
    dir.close();

    Serial.printf("  - Deleting empty directory: %s\n", path.c_str());
    if (SPIFFS.rmdir(path)) {
      Serial.printf("  - Successfully deleted directory.\n");
      return true;
    } else {
      Serial.printf("  - ERROR: Failed to delete directory.\n");
      return false;
    }
  }
}

float calculatePIDOutput(float setpoint, float input, unsigned long currentTime) {
  if (!pidEnabled) return 0.0;
  
  // Check if enough time has passed since last calculation
  if (currentTime - pidLastTime < (unsigned long)(pidSampleTime * 1000)) {
    return 0.0; // Return previous output if not time for new calculation
  }
  
  float error = setpoint - input;
  float deltaTime = (currentTime - pidLastTime) / 1000.0; // Convert to seconds
  
  // Proportional term
  float proportional = pidKp * error;
  
  // Integral term
  pidIntegral += pidKi * error * deltaTime;
  
  // Derivative term
  float derivative = pidKd * (error - pidLastError) / deltaTime;
  
  // Calculate output
  float output = proportional + pidIntegral + derivative;
  
  // Clamp output to min/max range
  output = constrain(output, pidOutputMin, pidOutputMax);
  
  // Anti-windup: if output is saturated, don't accumulate integral
  if (output >= pidOutputMax || output <= pidOutputMin) {
    pidIntegral -= pidKi * error * deltaTime;
  }
  
  // Update for next iteration
  pidLastError = error;
  pidLastTime = currentTime;
  
  return output;
}

void resetPID() {
  pidIntegral = 0.0;
  pidLastError = 0.0;
  pidLastTime = 0;
}

void checkLogFiles() {
  if (!SPIFFS.exists(TEMP_LOG_FILE)) {
    File tempLog = SPIFFS.open(TEMP_LOG_FILE, FILE_WRITE);
    if (tempLog) {
      tempLog.println("Timestamp,Temperature,Target,FurnaceStatus");
      tempLog.close();
    }
  }
}

void processPendingThemeSave() {
  if (!pendingThemeSave) return;
  
  // Parse the pending theme JSON
  DynamicJsonDocument doc(2048);
  DeserializationError error = deserializeJson(doc, pendingThemeJson);
  
  if (error) {
    Serial.println("Error parsing pending theme data");
    pendingThemeSave = false;
    return;
  }
  
  // Create or load existing config
  DynamicJsonDocument configDoc(4096);
  
  if (SPIFFS.exists("/wifi_config.json")) {
    File configFile = SPIFFS.open("/wifi_config.json", "r");
    if (configFile && configFile.size() > 0) {
      DeserializationError error = deserializeJson(configDoc, configFile);
      if (error) {
        Serial.println("Warning: wifi_config.json corrupted, creating new theme settings");
        configDoc.clear();
      }
    }
    if (configFile) configFile.close();
  }
  
  // Update theme settings
  if (doc.containsKey("light") && doc.containsKey("dark")) {
    configDoc["light"] = doc["light"];
    configDoc["dark"] = doc["dark"];
  }
  
  // Update theme mode
  if (doc.containsKey("currentMode")) {
    String mode = doc["currentMode"].as<String>();
    if (mode == "light" || mode == "dark") {
      configDoc["themeMode"] = mode;
      
      // Update global variables
      isDarkMode = (mode == "dark");
      saveAppSettings(); // Save to preferences
      
      // Force TFT theme refresh
      extern void forceTFTThemeRefresh();
      forceTFTThemeRefresh();
    }
  }
  
  // Save the config file
  File configFile = SPIFFS.open("/wifi_config.json", "w");
  if (configFile) {
    serializeJson(configDoc, configFile);
    configFile.close();
    Serial.println("Theme settings saved successfully");
  } else {
    Serial.println("Error: Failed to save theme settings");
  }
  
  // Clear the pending flag
  pendingThemeSave = false;
  pendingThemeJson = "";
}
