#ifndef TEMPERATURE_LOG_HANDLER_H
#define TEMPERATURE_LOG_HANDLER_H

#include <Arduino.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <SPIFFS.h>
#include <ArduinoJson.h>

// Forward declaration of the setup function
void setupTemperatureLogHandler(AsyncWebServer& server);

#endif // TEMPERATURE_LOG_HANDLER_H
