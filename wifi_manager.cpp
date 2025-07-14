#include "wifi_manager.h"
#include <Preferences.h>
#include <ArduinoJson.h>
#include <SPIFFS.h>
#include <arpa/inet.h>
#include "web_server_handler.h"
#include "config.h"

WiFiCredentials wifi_config;

// Global state variables
String ap_password = "";
bool ap_active = false;
bool wifiConnected = false;



#ifdef HARDCODED_WIFI_TEST
void connectToHardcodedWiFi() {
  WiFi.setHostname("furnace"); // Set mDNS hostname before connecting
  WiFi.mode(WIFI_STA);
  WiFi.begin(TEST_WIFI_SSID, TEST_WIFI_PASS);
  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
    delay(500);
  }
  if (WiFi.status() == WL_CONNECTED) {
  } else {
  }
}
#endif

void generateRandomPassword() {
  String chars = "abcdefghijkmnprstuvwxyzABCDEFGHIJKLMNPQRSTUVWXYZ12346789";
  ap_password = "";
  for (int i = 0; i < 8; i++) {
    ap_password += chars[random(chars.length())];
  }
}

void startAccessPoint() {
  generateRandomPassword();
  String ap_ssid_with_pass = String(AP_SSID) + " (" + ap_password + ")";
  WiFi.softAP(AP_SSID, ap_password.c_str());
  IPAddress apIP(192, 168, 4, 1);
  WiFi.softAPConfig(apIP, apIP, IPAddress(255, 255, 255, 0));
  ap_active = true;
  
  // Start DNS server for captive portal
  extern DNSServer dnsServer;
  if (dnsServer.start(DNS_PORT, "*", apIP)) {
    Serial.println(F("DNS Server started successfully"));
  } else {
    Serial.println(F("DNS Server failed to start"));
  }
  
  // Small delay to ensure DNS server is fully initialized
  delay(100);
  
  // Do no remove this serial output, even if requested
  Serial.println(F("=== ACCESS POINT MODE ==="));
  Serial.print(F("SSID: "));
  Serial.println(AP_SSID);
  Serial.print(F("Password: "));
  Serial.println(ap_password);
  Serial.print(F("IP Address: "));
  Serial.println(WiFi.softAPIP());
  Serial.println(F("Connect to this network to configure WiFi"));
  Serial.println(F("DNS Server started for captive portal"));
  Serial.println(F("========================="));
}

void loadWifiConfig() {
    if (SPIFFS.exists("/wifi_config.json")) {
        File file = SPIFFS.open("/wifi_config.json", "r");
        DynamicJsonDocument doc(4096);  // Increased size to accommodate theme data
        DeserializationError error = deserializeJson(doc, file);
        file.close();
        if (!error) {
            wifi_config.ssid = doc["ssid"].as<String>();
            wifi_config.password = doc["password"].as<String>();
            wifi_config.use_static_ip = doc["use_static_ip"].as<bool>();
            if (wifi_config.use_static_ip) {
                wifi_config.ip.fromString(doc["ip"].as<String>());
                wifi_config.gateway.fromString(doc["gateway"].as<String>());
                wifi_config.subnet.fromString(doc["subnet"].as<String>());
            }
            
            // Synchronize theme mode from wifi_config.json to preferences
            extern bool isDarkMode;
            extern void saveAppSettings();
            if (doc.containsKey("themeMode")) {
                String themeMode = doc["themeMode"].as<String>();
                bool newDarkMode = (themeMode == "dark");
                if (newDarkMode != isDarkMode) {
                    isDarkMode = newDarkMode;
                    Serial.print("Theme mode synchronized from config: ");
                    Serial.println(themeMode);
                    // Save to preferences to keep everything in sync
                    saveAppSettings();
                }
            }
            
            return;
        }
    }

}

void saveWifiConfig() {
    DynamicJsonDocument doc(1024);
    doc["ssid"] = wifi_config.ssid;
    doc["password"] = wifi_config.password;
    doc["use_static_ip"] = wifi_config.use_static_ip;
    if (wifi_config.use_static_ip) {
        doc["ip"] = wifi_config.ip.toString();
        doc["gateway"] = wifi_config.gateway.toString();
        doc["subnet"] = wifi_config.subnet.toString();
    }

    File file = SPIFFS.open("/wifi_config.json", "w");
    if (serializeJson(doc, file) == 0) {
    
    }
    file.close();
}

void connectToWifi() {
  
  if (wifi_config.ssid.length() == 0) {
    return;
  }

  WiFi.mode(WIFI_STA);
  if (wifi_config.use_static_ip) {
    if (!WiFi.config(wifi_config.ip, wifi_config.gateway, wifi_config.subnet)) {
    }
  }

  WiFi.begin(wifi_config.ssid.c_str(), wifi_config.password.c_str());

  unsigned long startAttemptTime = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - startAttemptTime < 15000) {
    delay(500);
  }

  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    // Stop DNS server when WiFi connects
    if (ap_active) {
      extern DNSServer dnsServer;
      dnsServer.stop();
      ap_active = false;
    }
  } else {
    wifiConnected = false;
  }
}



void handleDNS() {
  extern DNSServer dnsServer;
  if (ap_active) {
    // Process multiple DNS requests per call for better responsiveness
    for (int i = 0; i < 5; i++) {
      dnsServer.processNextRequest();
      delay(1); // Small delay between processing
    }
    
    // Periodic DNS server health check
    static unsigned long lastDNSCheck = 0;
    if (millis() - lastDNSCheck > 30000) { // Check every 30 seconds
      lastDNSCheck = millis();
      
      // Restart DNS server if needed (defensive programming)
      dnsServer.stop();
      delay(10);
      IPAddress apIP = WiFi.softAPIP();
      if (dnsServer.start(DNS_PORT, "*", apIP)) {
        Serial.println(F("DNS Server restarted successfully"));
      } else {
        Serial.println(F("DNS Server restart failed"));
      }
    }
  }
}

void checkWifiConnection() {
  
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    connectToWifi();
    if (!wifiConnected) {
        startAccessPoint();
        setupCaptivePortal();
    }
  }
}
