#include "temperature_log_handler.h"
#include "config.h"

// Define the temperature log file path
#ifndef TEMP_LOG_FILE
#define TEMP_LOG_FILE "/temp_log.csv"
#endif

void setupTemperatureLogHandler(AsyncWebServer& server) {
  
  // Main endpoint for temperature log retrieval (CSV, supports ?max=N)
  server.on("/api/templog", HTTP_GET, [](AsyncWebServerRequest *request) {
    
    int maxLines = 0;
    if (request->hasParam("max")) {
      maxLines = request->getParam("max")->value().toInt();
    }
    
    // SPIFFS is already mounted in setup(), do not re-mount here.
    
    // Check if file exists
    bool fileExists = SPIFFS.exists(TEMP_LOG_FILE);
    
    if (!fileExists) {
      // Try to create the file
      File tempFile = SPIFFS.open(TEMP_LOG_FILE, FILE_WRITE);
      if (tempFile) {
        tempFile.println("Timestamp,Temperature,Target,FurnaceStatus");
        tempFile.close();
      } else {
        request->send(500, "text/plain", "Failed to create temperature log file");
        return;
      }
    }
    
    File file = SPIFFS.open(TEMP_LOG_FILE, "r");
    if (!file || file.isDirectory()) {
      request->send(500, "text/plain", "Failed to open temperature log file");
      return;
    }

    AsyncWebServerResponse *response = request->beginChunkedResponse("text/csv", [file, maxLines](uint8_t *buffer, size_t maxLen, size_t index) -> size_t {
      File f = file;
      int mLines = maxLines;
      static int lineCount = 0;

      if (index == 0) {
          lineCount = 0;
      }

      if (!f.available() || (mLines > 0 && lineCount >= mLines)) {
        return 0;
      }

      String line = f.readStringUntil('\n');
      if (line.length() > 0) {
          line += "\n";
          lineCount++;
          strncpy((char*)buffer, line.c_str(), maxLen);
          return line.length();
      }
      return 0;
    });

    request->send(response);
  });
}
