#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include "config.h"
#include "wifi_manager.h"
#include "web_server_handler.h"
#include "temperature_log_handler.h"

// --- Needed for resolution update logic ---
extern void initializeTemperatureArrays();
extern void saveAppSettings();
extern bool shouldRestart;
extern unsigned long restartTime;

// Extern declaration for time sync status
extern bool timeIsSynchronized;

// External variables
extern AsyncWebServer server;

// PID variables
extern bool pidEnabled;
extern float pidKp;
extern float pidKi;
extern float pidKd;
extern float pidSampleTime;
extern int pidOutputMin;
extern int pidOutputMax;
extern float pidSetpointWindow;
extern void resetPID();

// deleteRecursive function is defined in the main .ino file

void setupCaptivePortal() {
  // Serve setup page for the root path with proper headers
  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/setup.html", "text/html");
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    request->send(response);
  });
  
  // Explicit setup page handler with proper headers
  server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/setup.html", "text/html");
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    request->send(response);
  });
  
  // Handle common captive portal detection URLs
  // Android devices - Android expects 204 No Content for successful internet, redirect for captive portal
  server.on("/generate_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/setup");
  });
  
  server.on("/gen_204", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/setup");
  });
  
  // Apple devices
  server.on("/hotspot-detect.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/setup");
  });
  
  server.on("/library/test/success.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/setup");
  });
  
  // Microsoft devices
  server.on("/connectivity-check.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/setup");
  });
  
  server.on("/check_network_status.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/setup");
  });
  
  server.on("/ncsi.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/setup");
  });
  
  server.on("/connecttest.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/setup");
  });
  
  // Firefox
  server.on("/success.txt", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/setup");
  });
  
  // Ubuntu/Linux
  server.on("/connectivity-check", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/setup");
  });
  
  // Additional common detection URLs
  server.on("/mobile/status.php", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/setup");
  });
  
  server.on("/kindle-wifi/wifistub.html", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->redirect("/setup");
  });
  
  server.on("/api/scan", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(4096);
    JsonArray networks = doc.createNestedArray("networks");
    
    int n = WiFi.scanComplete();
    if (n == WIFI_SCAN_FAILED) {
      WiFi.scanNetworks(true);
    } else if (n > 0) {
      for (int i = 0; i < n; ++i) {
        JsonObject network = networks.createNestedObject();
        network["ssid"] = WiFi.SSID(i);
        network["rssi"] = WiFi.RSSI(i);
        network["encryption"] = WiFi.encryptionType(i) != WIFI_AUTH_OPEN;
      }
      WiFi.scanDelete();
      WiFi.scanNetworks(true); // Start new scan
    }
    
    String response;
    serializeJson(doc, response);
    
    AsyncWebServerResponse *apiResponse = request->beginResponse(200, "application/json", response);
    apiResponse->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    apiResponse->addHeader("Pragma", "no-cache");
    apiResponse->addHeader("Expires", "0");
    request->send(apiResponse);
  });
  
  server.on("/api/connect", HTTP_POST, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(200);
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    request->send(response);
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    DynamicJsonDocument doc(768);
    deserializeJson(doc, data, len);
    
    wifi_config.ssid = doc["ssid"].as<String>();
    wifi_config.password = doc["password"].as<String>();
    wifi_config.use_static_ip = doc["use_static_ip"].as<bool>();
    
    if (wifi_config.use_static_ip) {
      wifi_config.ip.fromString(doc["ip"].as<String>());
      wifi_config.gateway.fromString(doc["gateway"].as<String>());
      wifi_config.subnet.fromString(doc["subnet"].as<String>());
      wifi_config.dns.fromString(doc["dns"].as<String>());
    }
    
    saveWifiConfig();
    
    AsyncWebServerResponse *finalResponse = request->beginResponse(200, "text/plain", "Configuration saved. Restarting...");
    finalResponse->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    finalResponse->addHeader("Pragma", "no-cache");
    finalResponse->addHeader("Expires", "0");
    request->send(finalResponse);
    
    delay(1000);
    ESP.restart();
  });

  // Catch-all handler for captive portal with proper headers
  server.onNotFound([](AsyncWebServerRequest *request) {
    // For any unhandled request, redirect to setup page
    AsyncWebServerResponse *response = request->beginResponse(302);
    response->addHeader("Location", "/setup");
    response->addHeader("Cache-Control", "no-cache, no-store, must-revalidate");
    response->addHeader("Pragma", "no-cache");
    response->addHeader("Expires", "0");
    request->send(response);
  });

  server.begin();
  
  // Add a small delay to ensure server is fully started
  delay(100);
}


void setupWebServer() {
  // First, set up the temperature log handler
  setupTemperatureLogHandler(server);
  
  // Debug endpoint to list all registered routes
  // (Removed)
  // Debug endpoint to check SPIFFS status
  // (Removed)
  
  server.onNotFound([](AsyncWebServerRequest *request) {
    // For API routes, return JSON 404
    if (request->url().startsWith("/api/")) {
      DynamicJsonDocument doc(256);
      doc["error"] = "Not Found";
      doc["path"] = request->url();
      doc["method"] = request->methodToString();
      
      String json;
      serializeJson(doc, json);
      request->send(404, "application/json", json);
    } else {
      // For non-API routes, serve index.html for SPA routing
      request->send(SPIFFS, "/index.html", "text/html");
    }
  });

  server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
    request->send(SPIFFS, "/index.html", "text/html");
  });

  // Setup page handler
  server.on("/setup", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/setup.html", "text/html");
  });

  // API endpoint to load system settings
  server.on("/api/settings/load", HTTP_GET, [](AsyncWebServerRequest *request) {
    // Create a JSON document to hold the settings
    DynamicJsonDocument doc(1024);
    
    // Add system settings
    doc["useManualTime"] = useManualTime;
    doc["loggingFrequencySeconds"] = loggingFrequencySeconds;
    doc["tempLogCleanupMinutes"] = tempLogCleanupMinutes;
    doc["temperatureIncrement"] = temperatureIncrement;
    doc["temperatureSmoothingEnabled"] = temperatureSmoothingEnabled;
    
    // Add WiFi settings
    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["ssid"] = wifi_config.ssid;
    
    wifi["use_static_ip"] = wifi_config.use_static_ip;
    if (wifi_config.use_static_ip) {
      wifi["ip"] = wifi_config.ip.toString();
      wifi["gateway"] = wifi_config.gateway.toString();
      wifi["subnet"] = wifi_config.subnet.toString();
      wifi["dns"] = wifi_config.dns.toString();
    }
    
    // Add time settings if in manual mode
    if (useManualTime) {
      JsonObject time = doc.createNestedObject("time");
      time["hour"] = manualTimeSettings.tm_hour;
      time["minute"] = manualTimeSettings.tm_min;
      time["second"] = manualTimeSettings.tm_sec;
      time["day"] = manualTimeSettings.tm_mday;
      time["month"] = manualTimeSettings.tm_mon + 1; // tm_mon is 0-11
      time["year"] = manualTimeSettings.tm_year + 1900; // Years since 1900
    }
    
    // Add theme settings
    doc["primaryColor"] = primaryColor;
    doc["backgroundColor"] = backgroundColor;
    doc["cardBackground"] = cardBackground;
    doc["textColor"] = textColor;
    doc["borderColor"] = borderColor;
    doc["highlightColor"] = highlightColor;
    doc["isDarkMode"] = isDarkMode;
    
    // Serialize the JSON to a string
    String jsonResponse;
    serializeJson(doc, jsonResponse);
    
    // Send the response
    request->send(200, "application/json", jsonResponse);
  });

  // Time Sync Endpoint
  server.on("/api/syncTime", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!wifiConnected) {
      request->send(400, "application/json", "{\"success\":false,\"error\":\"WiFi not connected\"}");
      return;
    }
    
    // Call the syncTime function to sync with NTP server
    syncTime();
    
    if (timeIsSynchronized) {
      struct tm timeinfo;
      if (getLocalTime(&timeinfo)) {
        char timeStr[20];
        strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
        String response = "{\"success\":true,\"message\":\"Time synchronized with NTP server\",\"currentTime\":\"" + String(timeStr) + "\"}";
        request->send(200, "application/json", response);
      } else {
        request->send(500, "application/json", "{\"success\":false,\"error\":\"Failed to get local time after sync\"}");
      }
    } else {
      request->send(500, "application/json", "{\"success\":false,\"error\":\"Time synchronization failed\"}");
    }
  });

  // Temperature Log Endpoints
  server.on("/api/log/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!SPIFFS.exists("/temp_log.csv")) {
      request->send(404, "text/plain", "Temperature log file not found");
      return;
    }
    
    // Set headers for file download
    AsyncWebServerResponse *response = request->beginResponse(SPIFFS, "/temp_log.csv", "text/csv");
    response->addHeader("Content-Disposition", "attachment; filename=temperature_log.csv");
    response->addHeader("Cache-Control", "no-cache");
    request->send(response);
  });

  server.on("/api/log/clear", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (SPIFFS.exists("/temp_log.csv")) {
      if (SPIFFS.remove("/temp_log.csv")) {
        // Recreate an empty log file
        File file = SPIFFS.open("/temp_log.csv", "w");
        if (file) {
          file.println("timestamp,temperature,setpoint");
          file.close();
        }
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Temperature log cleared successfully\"}");
      } else {
        request->send(500, "application/json", "{\"success\":false,\"error\":\"Failed to delete temperature log file\"}");
      }
    } else {
      // If file doesn't exist, return success since the end result is the same (no log)
      request->send(200, "application/json", "{\"success\":true,\"message\":\"Temperature log already empty\"}");
    }
  });

  // Temperature Settings Endpoints
  server.on("/api/settings/temperature", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(256);
    doc["tempIncrement"] = temperatureIncrement;
    doc["tempResolution"] = tempResolution;
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });







  // WiFi API Endpoint
  server.on("/api/wifi", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(512);
    
    doc["ssid"] = WiFi.SSID();
    doc["rssi"] = WiFi.RSSI();
    doc["ip"] = WiFi.localIP().toString();
    doc["mac"] = WiFi.macAddress();
    doc["status"] = WiFi.status() == WL_CONNECTED ? "connected" : "disconnected";
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // Handle WiFi settings update
  server.on("/api/wifi", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Check if the request has a body
    if (request->hasParam("ssid", true) && request->hasParam("password", true)) {
      String ssid = request->getParam("ssid", true)->value();
      String password = request->getParam("password", true)->value();
      
      // Update WiFi credentials
      wifi_config.ssid = ssid;
      wifi_config.password = password;
      
      // Save to preferences
      saveWifiConfig();
      
      // Attempt to connect to the new network
      connectToWifi();
      
      // Return success response
      DynamicJsonDocument doc(256);
      doc["success"] = true;
      doc["message"] = "WiFi settings updated. Attempting to connect...";
      
      String json;
      serializeJson(doc, json);
      request->send(200, "application/json", json);
    } else {
      // Return error if required parameters are missing
      request->send(400, "application/json", "{\"success\":false,\"error\":\"Missing required parameters (ssid, password)\"}");
    }
  });

  // System Logs API Endpoint
  server.on("/api/log", HTTP_GET, [](AsyncWebServerRequest *request) {
    // In a real implementation, you would read logs from a file or buffer
    // For now, we'll return a sample response
    DynamicJsonDocument doc(1024);
    JsonArray logs = doc.createNestedArray("logs");
    
    // Add sample log entry
    JsonObject logEntry = logs.createNestedObject();
    logEntry["timestamp"] = getFullTimestamp();
    logEntry["level"] = "info";
    logEntry["message"] = "System started";
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // Temperature Settings Endpoints
  server.on("/api/settings/temperature", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(256);
    doc["tempIncrement"] = temperatureIncrement;
    doc["tempResolution"] = tempResolution;
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // Lite Status API Endpoint - Minimal data for frequent updates
  server.on("/api/status/lite", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(200); // Slightly larger buffer to include smoothing data
    
    // Only include frequently changing essential data
    doc["currentTemp"] = currentTemp;
    doc["currentTime"] = getCurrentTime();
    doc["targetTemp"] = targetTemp[getCurrentTempIndex()];
    doc["systemEnabled"] = systemEnabled;
    doc["furnaceStatus"] = furnaceStatus;
    
    // Include smoothing data for proper target temperature display
    doc["temperatureSmoothingEnabled"] = temperatureSmoothingEnabled;
    if (temperatureSmoothingEnabled) {
      doc["smoothedTargetTemp"] = getSmoothedTargetTemperature();
    }
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // Controls Status API Endpoint
  server.on("/api/controls/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(512);
    
    // Control Status
    doc["systemEnabled"] = systemEnabled;
    doc["furnaceStatus"] = furnaceStatus;
    doc["temperatureSmoothingEnabled"] = temperatureSmoothingEnabled;
    doc["currentTemp"] = currentTemp;
    doc["targetTemp"] = targetTemp[getCurrentTempIndex()];
    doc["pwmEnabled"] = pwmEnabled;
    doc["pwmFrequency"] = pwmFrequency;
    
    // Add relay status if available
    #ifdef RELAY_PIN
    doc["relayStatus"] = digitalRead(RELAY_PIN) == HIGH ? "ON" : "OFF";
    #endif
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // Get All Programs API Endpoint
  server.on("/api/programs", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(4096); // May need adjustment based on program count
    JsonArray programs = doc.createNestedArray("programs");
    
    for (int i = 0; i < MAX_PROGRAMS; i++) {
      if (programNames[i].length() > 0) {
        JsonObject program = programs.createNestedObject();
        program["id"] = i;
        program["name"] = programNames[i];
        // Add temperature points for this program
        JsonArray temps = program.createNestedArray("temperatures");
        for (int j = 0; j < 96; j++) { // Assuming 96 points (15-min intervals for 24h)
          temps.add(programTemps[i][j]);
        }
      }
    }
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // Enhanced Load Program API Endpoint
  server.on("/api/loadProgram", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument responseDoc(512);
    
    // Validate program ID parameter
    if (!request->hasParam("id")) {
      responseDoc["success"] = false;
      responseDoc["error"] = "Missing required parameter: id";
      String json;
      serializeJson(responseDoc, json);
      request->send(400, "application/json", json);
      return;
    }
    
    String idParam = request->getParam("id")->value();
    int programId = idParam.toInt();
    
    // Validate program ID range
    if (programId < 0 || programId >= MAX_PROGRAMS) {
      responseDoc["success"] = false;
      responseDoc["error"] = "Program ID out of range (0-" + String(MAX_PROGRAMS - 1) + ")";
      String json;
      serializeJson(responseDoc, json);
      request->send(400, "application/json", json);
      return;
    }
    
    // Check if program exists
    if (programNames[programId].length() == 0) {
      responseDoc["success"] = false;
      responseDoc["error"] = "Program slot " + String(programId) + " is empty";
      String json;
      serializeJson(responseDoc, json);
      request->send(404, "application/json", json);
      return;
    }
    
    // Validate program has meaningful temperature data (using same logic as TFT)
    int firstNonZero = 0;
    while (firstNonZero < maxTempPoints && programTemps[programId][firstNonZero] == 0.0f) firstNonZero++;
    int lastNonZero = maxTempPoints - 1;
    while (lastNonZero >= 0 && programTemps[programId][lastNonZero] == 0.0f) lastNonZero--;
    int startIdx = firstNonZero > 0 ? firstNonZero - 1 : 0;
    int progLen = (startIdx <= lastNonZero) ? (lastNonZero - startIdx + 1) : 0;
    
    if (progLen <= 1) {
      responseDoc["success"] = false;
      responseDoc["error"] = "Program '" + programNames[programId] + "' has no valid temperature data";
      String json;
      serializeJson(responseDoc, json);
      request->send(422, "application/json", json); // Unprocessable Entity
      return;
    }
    
    // Get and validate offset parameter
    int offset = 0;
    if (request->hasParam("offset")) {
      offset = request->getParam("offset")->value().toInt();
      if (offset < 0 || offset >= maxTempPoints) {
        responseDoc["success"] = false;
        responseDoc["error"] = "Offset out of range (0-" + String(maxTempPoints - 1) + ")";
        String json;
        serializeJson(responseDoc, json);
        request->send(400, "application/json", json);
        return;
      }
    }
    
    // Load the program with offset handling
    if (offset > 0) {
      // Apply offset: Copy program data to targetTemp, starting at offset, wrapping around
      for (int i = 0; i < maxTempPoints; i++) {
        int destIdx = (offset + i) % maxTempPoints;
        if (i == 0) {
          // First point should always be 0 (matches trimming logic)
          targetTemp[destIdx] = 0.0f;
        } else if (progLen > 0 && (i - 1) < progLen) {
          // Copy program data points
          targetTemp[destIdx] = programTemps[programId][startIdx + (i - 1)];
        } else {
          // Fill remaining points with 0
          targetTemp[destIdx] = 0.0f;
        }
      }
      activeProgram = programId;
      
      // Log the program load with offset
      Serial.print("API: Loaded program ");
      Serial.print(programId);
      Serial.print(" (");
      Serial.print(programNames[programId]);
      Serial.print(") with offset ");
      Serial.print(offset);
      Serial.print(" (");
      Serial.print(progLen);
      Serial.println(" temperature points)");
    } else {
      // Load program normally (no offset)
      loadProgram(programId);
      
      Serial.print("API: Loaded program ");
      Serial.print(programId);
      Serial.print(" (");
      Serial.print(programNames[programId]);
      Serial.print(") normally (");
      Serial.print(progLen);
      Serial.println(" temperature points)");
    }
    
    // Prepare success response
    responseDoc["success"] = true;
    responseDoc["message"] = "Program loaded successfully";
    responseDoc["programId"] = programId;
    responseDoc["programName"] = programNames[programId];
    responseDoc["temperaturePoints"] = progLen;
    responseDoc["offset"] = offset;
    
    if (offset > 0) {
      // Calculate approximate start time for user feedback
      float hoursPerPoint = 24.0f / maxTempPoints;
      float startHours = offset * hoursPerPoint;
      int startHour = (int)startHours;
      int startMinute = (int)((startHours - startHour) * 60);
      
      responseDoc["warning"] = "Program started mid-way through";
      responseDoc["estimatedStartTime"] = String(startHour) + ":" + (startMinute < 10 ? "0" : "") + String(startMinute);
    }
    
    String json;
    serializeJson(responseDoc, json);
    request->send(200, "application/json", json);
  });

  // PWM API ENDPOINTS
  server.on("/api/pwm", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(128);
    doc["enabled"] = pwmEnabled;
    doc["frequency"] = pwmFrequency;
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });
  
  server.on("/api/pwm", HTTP_POST, 
    [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      DynamicJsonDocument doc(128);
      if (deserializeJson(doc, data, len)) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
        return;
      }

      bool changed = false;
      if (doc.containsKey("pwm_enabled")) {
        pwmEnabled = doc["pwm_enabled"].as<bool>();
        systemSettings.pwmEnabled = pwmEnabled;
        changed = true;
      }

      if (doc.containsKey("pwm_frequency")) {
        float freq = doc["pwm_frequency"].as<float>();
        if (freq >= 0.1 && freq <= 40000.0) {
          pwmFrequency = freq;
          changed = true;
        } else {
          request->send(400, "application/json", "{\"error\":\"Invalid frequency. Must be between 0.1 and 40,000 Hz.\"}");
          return;
        }
      }
      
      if (changed) {
        saveWifiConfig();
        request->send(200, "application/json", "{\"success\":true}");
      } else {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"No valid fields provided\"}");
      }
    }
  );

  // Temperature Smoothing API ENDPOINT
  server.on("/api/smoothing", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Toggle smoothing state
    temperatureSmoothingEnabled = !temperatureSmoothingEnabled;
    saveWifiConfig();
    String resp = String("{\"enabled\":") + (temperatureSmoothingEnabled ? "true" : "false") + "}";
    request->send(200, "application/json", resp);
  });

  // Logging Settings API ENDPOINT
  server.on("/api/settings/logging", HTTP_POST, 
    [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      DynamicJsonDocument doc(256);
      if (deserializeJson(doc, data, len)) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
      }
      
      bool changed = false;
      if (doc.containsKey("loggingFrequencySeconds")) {
        int freq = doc["loggingFrequencySeconds"].as<int>();
        if (freq >= 15 && freq <= 3600) {
          loggingFrequencySeconds = freq;
          changed = true;
        } else {
          request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid loggingFrequencySeconds\"}");
          return;
        }
      }

      if (doc.containsKey("tempLogCleanupMinutes")) {
        int tempCleanup = doc["tempLogCleanupMinutes"].as<int>();
        if (tempCleanup >= 0) {
          tempLogCleanupMinutes = tempCleanup;
          changed = true;
        } else {
          request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid tempLogCleanupMinutes\"}");
          return;
        }
      }
      
      if (changed) {
        saveAppSettings();
        request->send(200, "application/json", "{\"success\":true}");
      } else {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"No valid fields provided\"}");
      }
    }
  );

  // Theme API endpoints
  server.on("/api/theme", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument responseDoc(2048);
    bool success = false;
    String currentMode = "light"; // Default mode

    if (SPIFFS.exists("/wifi_config.json")) {
      File configFile = SPIFFS.open("/wifi_config.json", "r");
      if (configFile) {
        DynamicJsonDocument configDoc(4096);
        DeserializationError error = deserializeJson(configDoc, configFile);
        configFile.close();

        if (!error) {
          // Get theme colors if available
          if (configDoc.containsKey("light") && configDoc.containsKey("dark")) {
            responseDoc["light"] = configDoc["light"];
            responseDoc["dark"] = configDoc["dark"];
            success = true;
          }
          
          // Get current theme mode
          if (configDoc.containsKey("themeMode")) {
            currentMode = configDoc["themeMode"].as<String>();
          }
        } else {
          Serial.println("Warning: wifi_config.json is corrupted, using default theme");
        }
      }
    }

    // Add default colors if not loaded from config
    if (!success) {
      JsonObject light = responseDoc.createNestedObject("light");
      light["primaryColor"] = "#4CAF50";
      light["backgroundColor"] = "#f5f5f5";
      light["cardBackground"] = "#ffffff";
      light["textColor"] = "#333333";
      light["borderColor"] = "#e0e0e0";
      light["highlightColor"] = "#e9f7fe";

      JsonObject dark = responseDoc.createNestedObject("dark");
      dark["primaryColor"] = "#66bb6a";
      dark["backgroundColor"] = "#121212";
      dark["cardBackground"] = "#1e1e1e";
      dark["textColor"] = "#e0e0e0";
      dark["borderColor"] = "#333333";
      dark["highlightColor"] = "#1a3a4a";
    }

    // Always include current mode in response
    responseDoc["currentMode"] = currentMode;

    String jsonResponse;
    serializeJson(responseDoc, jsonResponse);
    request->send(200, "application/json", jsonResponse);
  });

  server.on("/api/theme", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Send immediate response to prevent watchdog timeout
    request->send(200, "application/json", "{\"success\":true,\"message\":\"Theme save initiated\"}");
  }, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Only process when we have the complete payload
    if (index + len != total) return;
    
    // Parse the JSON data quickly
    DynamicJsonDocument doc(2048);
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
      return; // Already sent response above
    }

    // Schedule the actual file operations for the next loop iteration
    // This prevents blocking the HTTP handler
    static String pendingThemeData;
    pendingThemeData = "";
    serializeJson(doc, pendingThemeData);
    
    // Set a flag to process theme data in the main loop
    extern bool pendingThemeSave;
    extern String pendingThemeJson;
    pendingThemeSave = true;
    pendingThemeJson = pendingThemeData;
  });



  // Temperature settings endpoints
  server.on("/api/settings/temperature", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(256);
    
    // Add current temperature settings to the response
    doc["temperatureIncrement"] = temperatureIncrement;
    doc["tempResolution"] = String(tempResolution, 1); // Format to 1 decimal place
    
    String jsonResponse;
    serializeJson(doc, jsonResponse);
    request->send(200, "application/json", jsonResponse);
  });

  server.on("/api/settings/temperature", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, 
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Parse the JSON data from the received chunk
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, data, len);

    if (error) {
        request->send(400, "application/json", "{\"success\":false,\"message\":\"Invalid JSON: " + String(error.c_str()) + "\"}");
        return;
    }

    bool changed = false;

    // Update temperature increment if provided
    if (doc.containsKey("tempIncrement")) {
        float newIncrement = doc["tempIncrement"].as<float>();
        if (newIncrement >= 1.0 && newIncrement <= 100.0) {
            temperatureIncrement = newIncrement;
            changed = true;
        }
    }
    
    // Also accept temperatureIncrement for consistency
    if (doc.containsKey("temperatureIncrement")) {
        float newIncrement = doc["temperatureIncrement"].as<float>();
        if (newIncrement >= 1.0 && newIncrement <= 100.0) {
            temperatureIncrement = newIncrement;
            changed = true;
        }
    }

    // Update temperature resolution if provided
    if (doc.containsKey("tempResolution")) {
        float newResolution = doc["tempResolution"].as<float>();
        if (newResolution > 0 && newResolution <= 1.0) {
            tempResolution = newResolution;
            changed = true;
        }
    }

    // Save changes if any were made
    if (changed) {
        saveAppSettings();
        request->send(200, "application/json", "{\"success\":true,\"message\":\"Temperature settings updated\"}");
    } else {
        request->send(200, "application/json", "{\"success\":true,\"message\":\"No changes detected\"}");
    }
  });

  // Serve static files
  server.serveStatic("/js/", SPIFFS, "/js/").setCacheControl("max-age=31536000");
  server.serveStatic("/css/", SPIFFS, "/css/").setCacheControl("max-age=31536000");
  server.serveStatic("/favicon.ico", SPIFFS, "/favicon.ico").setCacheControl("max-age=31536000");
  server.serveStatic("/", SPIFFS, "/").setDefaultFile("index.html").setCacheControl("no-cache, no-store, must-revalidate");
  
  server.on("/settings", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/settings.html", "text/html");
  });
  
  server.on("/programs", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/programs.html", "text/html");
  });
  
  server.on("/filemanager", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/filemanager.html", "text/html");
  });

  server.on("/filemanager", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send(SPIFFS, "/filemanager.html", "text/html");
  });

  server.on("/api/list", HTTP_GET, [](AsyncWebServerRequest *request) {
    String path = request->hasParam("path") ? request->getParam("path")->value() : "/";
    
    if (!path.startsWith("/")) { path = "/" + path; }
    if (path != "/" && path.endsWith("/")) { path.remove(path.length() - 1); }

    File root = SPIFFS.open("/");
    if (!root) {
      return request->send(500, "text/plain", "SPIFFS error");
    }

    DynamicJsonDocument uniqueChildren(2048);
    JsonObject children = uniqueChildren.to<JsonObject>();

    File file = root.openNextFile();
    while (file) {
        String itemPath = String(file.path());
        
        if (itemPath.startsWith(path)) {
            String relativePath;
            if (path == "/") {
                if (itemPath.length() > 1) { relativePath = itemPath.substring(1); } 
                else { relativePath = ""; }
            } else {
                if (itemPath.length() > path.length() && itemPath.charAt(path.length()) == '/') {
                    relativePath = itemPath.substring(path.length() + 1);
                } else {
                    relativePath = "";
                }
            }

            if (relativePath.length() > 0) {
                int firstSlash = relativePath.indexOf('/');
                String childName = (firstSlash == -1) ? relativePath : relativePath.substring(0, firstSlash);

                if (childName.length() > 0 && !childName.startsWith(".")) {
                    if(!children.containsKey(childName)){
                      children[childName] = true;
                    }
                }
            }
        }
        file.close();
        file = root.openNextFile();
    }
    root.close();

    DynamicJsonDocument doc(4096);
    JsonArray files = doc.to<JsonArray>();
    for (JsonPair kv : children) {
        String childName = kv.key().c_str();
        String fullChildPath = (path == "/") ? "/" + childName : path + "/" + childName;
        
        File entry = SPIFFS.open(fullChildPath);
        if (entry) {
            JsonObject f = files.createNestedObject();
            f["name"] = childName;
            f["type"] = entry.isDirectory() ? "folder" : "file";
            f["size"] = entry.size();
            f["date"] = entry.getLastWrite();
            entry.close();
        }
    }

    String response;
    serializeJson(doc, response);
    request->send(200, "application/json", response);
  });

  server.on("/api/file", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("path")) return request->send(400, "text/plain", "Missing path");
    String p = request->getParam("path")->value();
    if (!SPIFFS.exists(p)) return request->send(404, "text/plain", "File not found");
    request->send(SPIFFS, p, "text/plain");
  });

  server.on("/api/download", HTTP_GET, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("path")) return request->send(400, "text/plain", "Missing path");
    String p = request->getParam("path")->value();
    if (!SPIFFS.exists(p)) return request->send(404, "text/plain", "File not found");
    request->send(SPIFFS, p, "application/octet-stream", true);
  });

  server.on("/api/delete", HTTP_POST, [](AsyncWebServerRequest *request) {
    if (!request->hasParam("path", true)) {
      return request->send(400, "application/json", "{\"success\":false, \"error\":\"Missing path parameter\"}");
    }
    String path = request->getParam("path", true)->value();

    if (path == "/" || path.isEmpty()) {
      return request->send(400, "application/json", "{\"success\":false, \"error\":\"Cannot delete root directory\"}");
    }

    if (!SPIFFS.exists(path)) {
      return request->send(404, "application/json", "{\"success\":false, \"error\":\"File or directory not found\"}");
    }

    bool success = deleteRecursive(path);

    if (success) {
      request->send(200, "application/json", "{\"success\":true, \"message\":\"Deleted successfully\"}");
    } else {
      request->send(500, "application/json", "{\"success\":false, \"error\":\"Failed to delete. Check Serial monitor for details.\"}");
    }
  });

  server.on("/api/create", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, 
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    if (index == 0) {
      DynamicJsonDocument doc(256);
      if (deserializeJson(doc, data, len)) return request->send(400, "application/json", "{\"error\":\"Invalid JSON\"}");
      String p = doc["path"];
      if (p.isEmpty()) return request->send(400, "application/json", "{\"error\":\"Missing path\"}");
      if (SPIFFS.mkdir(p)) request->send(200, "application/json", "{\"success\":true}");
      else request->send(500, "application/json", "{\"error\":\"Failed to create directory\"}");
    }
  });

  server.on("/api/edit", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"success\":true}");
  }, [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
    String p;
    if (request->hasParam("path")) p = request->getParam("path")->value();
    else return;
    if (!index) request->_tempFile = SPIFFS.open(p, "w");
    if (len) request->_tempFile.write(data, len);
    if (final) request->_tempFile.close();
  });

  server.on("/api/upload", HTTP_POST, [](AsyncWebServerRequest *request) {
    request->send(200, "application/json", "{\"success\":true, \"message\":\"Upload complete\"}");
  }, [](AsyncWebServerRequest *request, const String& filename, size_t index, uint8_t *data, size_t len, bool final) {
    String p = "/";
    if (request->hasParam("path", true)) p = request->getParam("path", true)->value();
    if (!p.endsWith("/")) p += "/";
    String fp = p + filename;
    if (!index) request->_tempFile = SPIFFS.open(fp, "w");
    if (len) request->_tempFile.write(data, len);
    if (final) request->_tempFile.close();
  });

  server.serveStatic("/", SPIFFS, "/");

  // Configure CORS for all API endpoints at the beginning
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin", "*");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
  DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

  // Handle preflight requests - must be before any other route handlers
  server.on("*", HTTP_OPTIONS, [](AsyncWebServerRequest *request) {
    AsyncWebServerResponse *response = request->beginResponse(204);
    response->addHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
    response->addHeader("Access-Control-Allow-Headers", "Content-Type");
    response->addHeader("Access-Control-Max-Age", "86400");
    request->send(response);
  });

  // Serve static files from SPIFFS - must be before the catch-all route
  server.serveStatic("/", SPIFFS, "/");

  // Enhanced not found handler with better error reporting - must be the last route
  server.onNotFound([](AsyncWebServerRequest *request) {
    if (request->url().startsWith("/api/")) {
      // For API requests, return JSON error
      DynamicJsonDocument doc(256);
      doc["error"] = "Not Found";
      doc["path"] = request->url();
      doc["method"] = request->methodToString();
      
      String json;
      serializeJson(doc, json);
      request->send(404, "application/json", json);
    } else {
      // For non-API requests, serve index.html for SPA routing
      request->send(SPIFFS, "/index.html", "text/html");
    }
  });
  
  // Add debug endpoint to list all registered routes
  server.on("/api/debug/routes", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(1024);
    doc["status"] = "success";
    doc["message"] = "Route debugging endpoint";
    doc["endpoints"] = "[/api/status, /api/controls/status, /api/programs, /api/loadProgram, etc.]";
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // Toggle system power
  server.on("/api/toggleSystem", HTTP_POST, [](AsyncWebServerRequest *request) {
    systemEnabled = !systemEnabled;

    // Handle system state change
    if (!systemEnabled) {
      // When turning off, ensure furnace is off
      furnaceStatus = false;
      digitalWrite(RELAY_PIN, LOW);
    } else {
      // When turning on, let the main loop handle furnace control
      // The controlFurnace() function will handle the relay state
    }

    // Return the new state
    DynamicJsonDocument doc(128);
    doc["success"] = true;
    doc["systemEnabled"] = systemEnabled;
    doc["enabled"] = systemEnabled;  // Include both for compatibility
    String json;
    serializeJson(doc, json);
    
    request->send(200, "application/json", json);
  });

  // Endpoint to update temperature resolution (points per hour)
  server.on(
    "/api/updateResolution",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      // onRequest handler (required, but not used for body POST)
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      DynamicJsonDocument doc(256);
      deserializeJson(doc, data, len);
      int newResolution = doc["resolution"].as<int>();
      if (newResolution == 1 || newResolution == 2 || newResolution == 4 || newResolution == 6 || newResolution == 12) {
        tempResolution = newResolution;
        maxTempPoints = 24 * tempResolution;
        initializeTemperatureArrays(); // Ensure arrays are reallocated and populated
        // If you have profile names logic, reset as in V24 if needed.
        // Example: for (int i = 0; i < MAX_PROGRAMS; i++) programNames[i] = (i == 0 ? "Default" : "");
        // Example: saveAllPrograms();
        saveAppSettings(); // Save new resolution
        request->send(200, "text/plain", "Resolution updated. Restarting...");
        shouldRestart = true;
        restartTime = millis() + 1000;
      } else {
        request->send(400, "text/plain", "Invalid resolution value");
      }
    }
  );

  // Consolidated status endpoint with all information
  server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(2048);
    
    // System status with tempResolution and maxTempPoints
    doc["systemEnabled"] = systemEnabled;
    doc["tempResolution"] = tempResolution;
    doc["maxTempPoints"] = maxTempPoints;
    doc["currentTemp"] = currentTemp;
    doc["targetTemp"] = targetTemp[getCurrentTempIndex()];
    doc["currentTempIndex"] = getCurrentTempIndex();
    doc["smoothedTargetTemp"] = getSmoothedTargetTemperature();
    doc["minTemp"] = minTemp;
    doc["maxTemp"] = maxTemp;
    doc["temperatureIncrement"] = temperatureIncrement;
    doc["temperatureSmoothingEnabled"] = temperatureSmoothingEnabled;
    doc["useManualTime"] = useManualTime;
    doc["currentTime"] = getCurrentTime();
    doc["uptime"] = millis() / 1000;
    
    // Add UTC offset from preferences (cached)
    static int cachedUtcOffset = -999; // Invalid value to force first load
    if (cachedUtcOffset == -999) {
      Preferences preferences;
      preferences.begin("furnace", true);
      cachedUtcOffset = preferences.getInt("utcOffset", 0);
      preferences.end();
    }
    doc["utcOffset"] = cachedUtcOffset;
    
    // Logging cleanup settings
    doc["tempLogCleanupMinutes"] = tempLogCleanupMinutes;
    doc["loggingFrequencySeconds"] = loggingFrequencySeconds;
    doc["loggingFrequencyMinutes"] = loggingFrequencySeconds > 0 ? (loggingFrequencySeconds / 60) : 0;
    doc["timeIsSynchronized"] = timeIsSynchronized;
    
    // Add targetTemps array with dynamic resolution (optimized)
    JsonArray targetTemps = doc.createNestedArray("targetTemps");
    if (targetTemp != nullptr) {
      int numReadings = maxTempPoints;
      for (int i = 0; i < numReadings; i++) {
        targetTemps.add(targetTemp[i]);
      }
    } else {
      for (int i = 0; i < 24; i++) {
        targetTemps.add(0.0);
      }
    }
    
    // WiFi status
    doc["wifiConnected"] = WiFi.status() == WL_CONNECTED;
    JsonObject wifi = doc.createNestedObject("wifi");
    wifi["connected"] = WiFi.status() == WL_CONNECTED;
    if (wifi["connected"]) {
      wifi["ssid"] = WiFi.SSID();
      wifi["rssi"] = WiFi.RSSI();
      wifi["ip"] = WiFi.localIP().toString();
    }
    
    // Storage status (SPIFFS) - optimized
    JsonObject storage = doc.createNestedObject("storage");
    static unsigned long lastStorageCheck = 0;
    static bool storageInitialized = false;
    static size_t cachedTotalBytes = 0;
    static size_t cachedUsedBytes = 0;
    static bool cachedTempLogExists = false;
    
    // Cache storage info for 5 seconds to avoid repeated SPIFFS calls
    if (millis() - lastStorageCheck > 5000 || !storageInitialized) {
      if (SPIFFS.totalBytes() > 0) {
        cachedTotalBytes = SPIFFS.totalBytes();
        cachedUsedBytes = SPIFFS.usedBytes();
        cachedTempLogExists = SPIFFS.exists(TEMP_LOG_FILE);
        storageInitialized = true;
      } else {
        cachedTotalBytes = 0;
        cachedUsedBytes = 0;
        cachedTempLogExists = false;
        storageInitialized = true;
      }
      lastStorageCheck = millis();
    }
    
    // Use cached values
    if (cachedTotalBytes > 0) {
      storage["type"] = "SPIFFS";
      storage["totalBytes"] = cachedTotalBytes;
      storage["usedBytes"] = cachedUsedBytes;
      storage["freeBytes"] = cachedTotalBytes - cachedUsedBytes;
      storage["percentUsed"] = (cachedUsedBytes * 100) / cachedTotalBytes;
      storage["tempLogExists"] = cachedTempLogExists;
      storage["errorLogExists"] = false;
    } else {
      storage["type"] = "SPIFFS";
      storage["totalBytes"] = 0;
      storage["usedBytes"] = 0;
      storage["freeBytes"] = 0;
      storage["percentUsed"] = 0;
      storage["tempLogExists"] = false;
      storage["errorLogExists"] = false;
      storage["error"] = "SPIFFS not available";
    }
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  // Temperature log endpoint is now handled in temperature_log_handler.h

  // System reset endpoint
  server.on("/api/reset", HTTP_POST, [](AsyncWebServerRequest *request) {
    // Clear preferences
    Preferences preferences;
    preferences.begin("furnace", false);
    preferences.clear();
    preferences.end();
    
    // Delete all programs
    if (SPIFFS.exists("/programs.json")) {
      SPIFFS.remove("/programs.json");
    }
    
    // Clear error logs
    if (SPIFFS.exists("/error_log.csv")) {
      SPIFFS.remove("/error_log.csv");
    }
    
    // Clear temperature logs
    if (SPIFFS.exists("/temp_log.csv")) {
      SPIFFS.remove("/temp_log.csv");
    }
    
    // Send success response
    request->send(200, "application/json", "{\"success\":true,\"message\":\"All settings reset. Device will restart.\"}\n");
    
    // Schedule a restart
    shouldRestart = true;
    restartTime = millis() + 2000; // Restart after 2 seconds
  });

  // Temperature update endpoint
  server.on("/api/updateTemp", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    // Handle JSON payload
    if (request->contentType() == "application/json") {
      DynamicJsonDocument doc(256);
      DeserializationError error = deserializeJson(doc, data, len);
      
      if (error) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON payload\"}");
        return;
      }
      
      if (!doc.containsKey("index") || !doc.containsKey("temp")) {
        request->send(400, "application/json", "{\"error\":\"Missing index or temp parameter\"}");
        return;
      }
      
      int tempIndex = doc["index"];
      float temp = doc["temp"];
      
      if (tempIndex < 0 || tempIndex >= maxTempPoints) {
        request->send(400, "application/json", "{\"error\":\"Invalid temperature index\"}");
        return;
      }
      
      targetTemp[tempIndex] = temp;
      request->send(200, "application/json", "{\"success\":true}");
      return;
    }
    
    // Handle form-encoded parameters
    if (!request->hasParam("index", true) || !request->hasParam("temp", true)) {
      request->send(400, "application/json", "{\"error\":\"Missing index or temp parameter\"}");
      return;
    }
    
    int tempIndex = request->getParam("index", true)->value().toInt();
    float temp = request->getParam("temp", true)->value().toFloat();
    
    if (tempIndex < 0 || tempIndex >= maxTempPoints) {
      request->send(400, "application/json", "{\"error\":\"Invalid temperature index\"}");
      return;
    }
    
    targetTemp[tempIndex] = temp;
    request->send(200, "application/json", "{\"success\":true}");
  });

  // Temperature range update endpoint
  server.on("/api/updateRange", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    DynamicJsonDocument doc(256);
    DeserializationError error = deserializeJson(doc, data, len);
    
    if (error) {
      request->send(400, "application/json", "{\"error\":\"Invalid JSON payload\"}");
      return;
    }
    
    if (!doc.containsKey("min") || !doc.containsKey("max")) {
      request->send(400, "application/json", "{\"error\":\"Missing min or max parameter\"}");
      return;
    }
    
    float newMin = doc["min"];
    float newMax = doc["max"];
    
    if (newMin >= newMax) {
      request->send(400, "application/json", "{\"error\":\"Min must be less than max\"}");
      return;
    }
    
    minTemp = newMin;
    maxTemp = newMax;
    request->send(200, "application/json", "{\"success\":true}");
  });

  // Temperature log handler already initialized at the beginning of this function

  // Time synchronization endpoint
  server.on(
    "/api/syncTime",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {
      // onRequest handler (required, but not used for body POST)
    },
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      // In a real implementation, this would sync with an NTP server
      // For now, we'll just update the timeIsSynchronized flag
      timeIsSynchronized = true;
      
      // Update the manual time flag
      useManualTime = false;
      
      // Save the settings
      saveAppSettings();
      
      // Get current time to return to the client
      time_t now = time(nullptr);
      struct tm timeinfo;
      localtime_r(&now, &timeinfo);
      
      char timeStr[64];
      strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
      
      DynamicJsonDocument doc(256);
      doc["success"] = true;
      doc["message"] = "Time synchronized successfully";
      doc["currentTime"] = String(timeStr);
      doc["useManualTime"] = false;
      
      String json;
      serializeJson(doc, json);
      request->send(200, "application/json", json);
    }
  );

  // Manual time setting endpoint
  server.on(
    "/api/time",
    HTTP_POST,
    [](AsyncWebServerRequest *request) {},
    NULL,
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      DynamicJsonDocument doc(512);
      DeserializationError error = deserializeJson(doc, data, len);
      
      if (error) {
        request->send(400, "application/json", "{\"error\":\"Invalid JSON payload\"}");
        return;
      }
      
      // Check for UTC offset
      if (doc.containsKey("utcOffset")) {
        int utcOffset = doc["utcOffset"].as<int>();
        // Save UTC offset to preferences
        Preferences preferences;
        preferences.begin("furnace", false);
        preferences.putInt("utcOffset", utcOffset);
        preferences.end();
      }
      
      // Check if we're using manual time
      if (doc.containsKey("useManualTime")) {
        useManualTime = doc["useManualTime"].as<bool>();
        
        if (useManualTime && doc.containsKey("time")) {
          // Parse the time string from the frontend (format: YYYY-MM-DDTHH:MM)
          String timeStr = doc["time"].as<String>();
          
          if (timeStr.length() >= 16) { // YYYY-MM-DDTHH:MM format is at least 16 chars
            
            // Set the manual time from the string
            struct tm timeinfo;
            // Parse YYYY-MM-DDTHH:MM format
            int year = timeStr.substring(0, 4).toInt();
            int month = timeStr.substring(5, 7).toInt();
            int day = timeStr.substring(8, 10).toInt();
            int hour = timeStr.substring(11, 13).toInt();
            int minute = timeStr.substring(14, 16).toInt();
            
            timeinfo.tm_year = year - 1900;  // Years since 1900
            timeinfo.tm_mon = month - 1;     // 0-11
            timeinfo.tm_mday = day;
            timeinfo.tm_hour = hour;
            timeinfo.tm_min = minute;
            timeinfo.tm_sec = 0; // Seconds not provided in the input
            timeinfo.tm_isdst = -1;  // Let the system determine DST
            
            // Set the system time
            time_t t = mktime(&timeinfo);
            struct timeval now = { .tv_sec = t };
            settimeofday(&now, NULL);
            
            // Update the manual time settings
            manualTimeSettings = timeinfo;
            
            // Update the time sync status
            timeIsSynchronized = true;
            
            // Save the settings
            saveAppSettings();
            
            // Format the time for the response
            char timeStr[64];
            strftime(timeStr, sizeof(timeStr), "%Y-%m-%d %H:%M:%S", &timeinfo);
            
            DynamicJsonDocument responseDoc(256);
            responseDoc["success"] = true;
            responseDoc["message"] = "Time set successfully";
            responseDoc["currentTime"] = String(timeStr);
            responseDoc["useManualTime"] = true;
            
            // Add UTC offset to the response
            Preferences preferences;
            preferences.begin("furnace", true);
            int utcOffset = preferences.getInt("utcOffset", 0);
            preferences.end();
            responseDoc["utcOffset"] = utcOffset;
            
            String json;
            serializeJson(responseDoc, json);
            request->send(200, "application/json", json);
            return;
          }
        } else {
          // Just update the useManualTime flag
          saveAppSettings();
          
          DynamicJsonDocument responseDoc(128);
          responseDoc["success"] = true;
          responseDoc["useManualTime"] = useManualTime;
          
          // Add UTC offset to the response
          Preferences preferences;
          preferences.begin("furnace", true);
          int utcOffset = preferences.getInt("utcOffset", 0);
          preferences.end();
          responseDoc["utcOffset"] = utcOffset;
          
          String json;
          serializeJson(responseDoc, json);
          request->send(200, "application/json", json);
          return;
        }
      }
      
      // If we get here, the request was invalid
      request->send(400, "application/json", "{\"error\":\"Invalid time data\"}");
    }
  );

  // Save Program API Endpoint
  server.on("/api/saveProgram", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
    DynamicJsonDocument doc(4096);
    if (deserializeJson(doc, data, len)) {
      request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
      return;
    }
    
    int programIndex = doc["index"] | -1;
    String name = doc["name"] | "";
    String description = doc["description"] | "";
    JsonArray temps = doc["temps"];
    
    if (programIndex < 0 || programIndex >= MAX_PROGRAMS || name.length() == 0 || !temps.size()) {
      String errorMsg = "Invalid input: ";
      if (programIndex < 0) errorMsg += "invalid program index; ";
      if (programIndex >= MAX_PROGRAMS) errorMsg += "program index out of range; ";
      if (name.length() == 0) errorMsg += "program name is empty; ";
      if (!temps.size()) errorMsg += "no temperature data provided; ";
      
      DynamicJsonDocument errorDoc(256);
      errorDoc["success"] = false;
      errorDoc["error"] = errorMsg;
      String errorJson;
      serializeJson(errorDoc, errorJson);
      request->send(400, "application/json", errorJson);
      return;
    }
    // Save name and temps
    programNames[programIndex] = name;
    // --- Trim to a single leading zero and remove trailing zeros ---
    int firstNonZero = 0;
    while (firstNonZero < (int)temps.size() && temps[firstNonZero] == 0.0f) {
      firstNonZero++;
    }
    int startIdx = firstNonZero > 0 ? firstNonZero - 1 : 0;
    int lastNonZero = temps.size() - 1;
    while (lastNonZero >= 0 && temps[lastNonZero] == 0.0f) {
      lastNonZero--;
    }
    std::vector<float> trimmedTemps;
    if (startIdx <= lastNonZero) {
      for (int i = startIdx; i <= lastNonZero; ++i) {
        trimmedTemps.push_back(temps[i].as<float>());
      }
    }
    // After trimming, ensure the first value is 0
    if (!trimmedTemps.empty() && trimmedTemps[0] != 0.0f) {
      trimmedTemps.insert(trimmedTemps.begin(), 0.0f);
    } else if (trimmedTemps.empty()) {
      trimmedTemps.push_back(0.0f);
    }
    // Save trimmed temps to programTemps
    size_t trimmedLen = trimmedTemps.size();
    for (size_t i = 0; i < trimmedLen && i < maxTempPoints; i++) {
      programTemps[programIndex][i] = trimmedTemps[i];
    }
    for (size_t i = trimmedLen; i < maxTempPoints; i++) {
      programTemps[programIndex][i] = 0.0;
    }
    // Ensure the last point is always 0 if there is any data
    if (trimmedLen > 0) {
      programTemps[programIndex][trimmedLen - 1] = 0.0;
    }
    // TODO: Save description if needed
    saveAllPrograms(); // Persist to storage
    DynamicJsonDocument resp(128);
    resp["success"] = true;
    resp["message"] = "Program saved successfully";
    String json;
    serializeJson(resp, json);
    request->send(200, "application/json", json);
  });

  // PID Settings API Endpoints
  server.on("/api/settings/pid", HTTP_GET, [](AsyncWebServerRequest *request) {
    DynamicJsonDocument doc(512);
    
    doc["enabled"] = pidEnabled;
    doc["kp"] = pidKp;
    doc["ki"] = pidKi;
    doc["kd"] = pidKd;
    doc["sampleTime"] = pidSampleTime;
    doc["outputMin"] = pidOutputMin;
    doc["outputMax"] = pidOutputMax;
    doc["setpointWindow"] = pidSetpointWindow;
    
    String json;
    serializeJson(doc, json);
    request->send(200, "application/json", json);
  });

  server.on("/api/settings/pid", HTTP_POST, [](AsyncWebServerRequest *request) {}, NULL, 
    [](AsyncWebServerRequest *request, uint8_t *data, size_t len, size_t index, size_t total) {
      DynamicJsonDocument doc(512);
      DeserializationError error = deserializeJson(doc, data, len);
      
      if (error) {
        request->send(400, "application/json", "{\"success\":false,\"error\":\"Invalid JSON\"}");
        return;
      }
      
      // Update PID settings
      if (doc.containsKey("enabled")) pidEnabled = doc["enabled"].as<bool>();
      if (doc.containsKey("kp")) pidKp = doc["kp"].as<float>();
      if (doc.containsKey("ki")) pidKi = doc["ki"].as<float>();
      if (doc.containsKey("kd")) pidKd = doc["kd"].as<float>();
      if (doc.containsKey("sampleTime")) pidSampleTime = doc["sampleTime"].as<float>();
      if (doc.containsKey("outputMin")) pidOutputMin = doc["outputMin"].as<int>();
      if (doc.containsKey("outputMax")) pidOutputMax = doc["outputMax"].as<int>();
      if (doc.containsKey("setpointWindow")) pidSetpointWindow = doc["setpointWindow"].as<float>();
      
      // Reset PID when settings change
      resetPID();
      
      // Save settings
      saveAppSettings();
      
      DynamicJsonDocument responseDoc(256);
      responseDoc["success"] = true;
      responseDoc["message"] = "PID settings saved successfully";
      
      String json;
      serializeJson(responseDoc, json);
      request->send(200, "application/json", json);
    }
  );

  // Start the server
  server.serveStatic("/favicon.ico", SPIFFS, "/favicon.ico");
  server.begin();
}
