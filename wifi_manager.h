#ifndef WIFI_MANAGER_H
#define WIFI_MANAGER_H

#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <DNSServer.h>
#include "config.h"

// Forward declaration of WiFiCredentials (defined in config.h)
struct WiFiCredentials;

extern WiFiCredentials wifi_config;

// WiFi State Variables
extern String ap_password;
extern bool ap_active;
extern bool wifiConnected;

void connectToHardcodedWiFi();
void startAccessPoint();
void loadWifiConfig();
void saveWifiConfig();
void connectToWifi();
void handleDNS();
void checkWifiConnection();

#endif // WIFI_MANAGER_H
