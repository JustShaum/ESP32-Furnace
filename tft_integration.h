#ifndef TFT_INTEGRATION_H
#define TFT_INTEGRATION_H

#include "tft_ui.h"
#include "config.h"

// Integration constants - Optimized for performance when using both TFT and Web
#define TFT_UPDATE_INTERVAL 200  // 5 FPS for better performance (was 10 FPS)
#define TFT_THEME_REFRESH_INTERVAL 30000  // 30 seconds

// Integration class
class TFT_Integration {
public:
    static TFT_Integration& getInstance() {
        static TFT_Integration instance;
        return instance;
    }
    
    // Initialize TFT system
    void init() {
        // Initialize TFT UI
        tftUI.init();
        
        // Load initial theme
        refreshTheme();
        
        // Begin UI operations
        tftUI.begin();
        
        // Check if WiFi credentials are saved
        extern WiFiCredentials wifi_config;
        if (wifi_config.ssid.length() == 0) {
            // No WiFi credentials - show WiFi setup screen
            tftUI.setScreen(SCREEN_WIFI_SETUP);
        } else {
            // WiFi credentials exist - show main screen
            tftUI.setScreen(SCREEN_MAIN);
        }
        
        initialized = true;
        lastThemeRefresh = millis();
    }
    
    // Main update loop - call from Arduino loop()
    void update() {
        if (!initialized) return;
        
        unsigned long currentTime = millis();
        
        // Update UI at regular intervals
        if (currentTime - lastUpdate >= TFT_UPDATE_INTERVAL) {
            lastUpdate = currentTime;
            
            // Update TFT UI
            tftUI.update();
            
            // Disable automatic theme refresh to prevent unnecessary full screen redraws
            // Theme refresh should only happen when explicitly requested
            // if (currentTime - lastThemeRefresh >= TFT_THEME_REFRESH_INTERVAL) {
            //     refreshTheme();
            //     lastThemeRefresh = currentTime;
            // }
        }
    }
    
    // Refresh theme from backend - only when explicitly requested
    void refreshTheme() {
        if (WiFi.status() == WL_CONNECTED) {
            tftUI.loadTheme();
        }
    }
    
    // Force theme refresh (for settings changes)
    void forceThemeRefresh() {
        tftUI.loadTheme();
    }
    
    // Get UI instance
    TFT_UI& getUI() {
        return tftUI;
    }
    
    // Check if initialized
    bool isInitialized() const {
        return initialized;
    }
    
    // Force screen update
    void forceUpdate() {
        if (initialized) {
            tftUI.forceRedraw();
        }
    }
    
    // Handle system events
    void onSystemStateChange(bool enabled) {
        if (initialized) {
            tftUI.forceRedraw();
        }
    }
    
    void onTemperatureChange(float temperature) {
        if (initialized) {
            // Temperature updates are handled automatically by the UI
        }
    }
    
    void onProgramChange(int programIndex) {
        if (initialized) {
            tftUI.showMessage("Program " + String(programIndex + 1) + " active", 
                             tftUI.getTheme().successColor, 2000);
        }
    }
    
    void onError(const String& error) {
        if (initialized) {
            tftUI.showError(error);
        }
    }
    
    void onSuccess(const String& message) {
        if (initialized) {
            tftUI.showSuccess(message);
        }
    }
    
    // Performance monitoring
    void printPerformanceStats() {
        if (initialized) {
            unsigned long currentTime = millis();
            float fps = 1000.0 / (currentTime - lastUpdate);
        }
    }
    
private:
    TFT_Integration() : initialized(false), lastUpdate(0), lastThemeRefresh(0) {}
    
    bool initialized;
    unsigned long lastUpdate;
    unsigned long lastThemeRefresh;
};

// Global integration instance
extern TFT_Integration& getTFTIntegration();

// Convenience functions for main firmware
void initializeTFT();
void updateTFT();
void refreshTFTTheme();
void forceTFTThemeRefresh(); // Force theme refresh (for settings changes)
void showTFTError(const String& error);
void showTFTSuccess(const String& message);
void onTFTSystemStateChange(bool enabled);
void onTFTTemperatureChange(float temperature);
void onTFTProgramChange(int programIndex);
void runTFTDiagnostics();
void testTFTTouchSPI();
void reinitTFTTouchscreen();
bool testTFTTouchHardware();

#endif // TFT_INTEGRATION_H 