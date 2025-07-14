#include "tft_integration.h"
#include <WiFi.h>

// Global integration instance
TFT_Integration& getTFTIntegration() {
    return TFT_Integration::getInstance();
}

// Convenience functions for main firmware
void initializeTFT() {
    getTFTIntegration().init();
}

void updateTFT() {
    getTFTIntegration().update();
}

void refreshTFTTheme() {
    getTFTIntegration().refreshTheme();
}

void forceTFTThemeRefresh() {
    getTFTIntegration().forceThemeRefresh();
}

void showTFTError(const String& error) {
    getTFTIntegration().onError(error);
}

void showTFTSuccess(const String& message) {
    getTFTIntegration().onSuccess(message);
}

void onTFTSystemStateChange(bool enabled) {
    getTFTIntegration().onSystemStateChange(enabled);
}

void onTFTTemperatureChange(float temperature) {
    getTFTIntegration().onTemperatureChange(temperature);
}

void onTFTProgramChange(int programIndex) {
    getTFTIntegration().onProgramChange(programIndex);
}

// Performance monitoring function
void printTFTPerformanceStats() {
    getTFTIntegration().printPerformanceStats();
}

// Theme management functions
void setTFTTheme(bool darkMode) {
    TFT_UI& ui = getTFTIntegration().getUI();
    
    if (darkMode != ui.getTheme().isDarkMode) {
        ui.loadTheme();
        ui.forceRedraw();
    }
}

bool getTFTThemeMode() {
    return getTFTIntegration().getUI().getTheme().isDarkMode;
}

// Screen management functions
void setTFTScreen(ScreenType screen) {
    getTFTIntegration().getUI().setScreen(screen);
}

ScreenType getCurrentTFTScreen() {
    return getTFTIntegration().getUI().getCurrentScreen();
}

// Message functions
void showTFTMessage(const String& message, uint16_t color, int duration) {
    getTFTIntegration().getUI().showMessage(message, color, duration);
}

void clearTFTMessage() {
    getTFTIntegration().getUI().showMessage("", 0, 0);
}

// System status functions
void updateTFTSystemStatus(bool systemEnabled, bool furnaceStatus, float currentTemp, float targetTemp) {
    TFT_UI& ui = getTFTIntegration().getUI();
    
    // Status updates are handled automatically by the UI
    // This function can be used to force an update if needed
    if (getTFTIntegration().isInitialized()) {
        ui.forceRedraw();
    }
}

// Program management functions
void updateTFTProgramList(const String* programNames, int programCount) {
    // Program list updates are handled automatically by the UI
    // This function can be used to force an update if needed
    if (getTFTIntegration().isInitialized()) {
        getTFTIntegration().getUI().forceRedraw();
    }
}

// Touch calibration helper
void calibrateTFTTouch() {
    TFT_UI& ui = getTFTIntegration().getUI();
    ui.calibrateTouch();
}

// Touch diagnostic functions
void testTFTTouchSPI() {
    TFT_UI& ui = getTFTIntegration().getUI();
    ui.testTouchSPI();
}

void reinitTFTTouchscreen() {
    TFT_UI& ui = getTFTIntegration().getUI();
    ui.reinitTouchscreen();
}

bool testTFTTouchHardware() {
    TFT_UI& ui = getTFTIntegration().getUI();
    return ui.testTouchHardware();
}

// WiFi status update
void updateTFTWiFiStatus(bool connected, const String& ssid) {
    if (getTFTIntegration().isInitialized()) {
        String message = connected ? "WiFi: " + ssid : "WiFi: Disconnected";
        getTFTIntegration().getUI().forceRedraw();
    }
}

// Error handling
void handleTFTError(const String& error, bool critical) {
    if (getTFTIntegration().isInitialized()) {
        getTFTIntegration().getUI().showError(error);
        
        if (critical) {
            // For critical errors, show on main screen
            getTFTIntegration().getUI().setScreen(SCREEN_MAIN);
        }
    }
}

// Diagnostic functions
void runTFTDiagnostics() {
    if (!getTFTIntegration().isInitialized()) {
        return;
    }
    
    TFT_UI& ui = getTFTIntegration().getUI();
    
    // Test display
    ui.clearScreen();
    ui.showMessage("Display Test", TFT_WHITE, 2000);
    delay(2000);
    
    // Test touch
    ui.showMessage("Touch test - touch screen", TFT_WHITE, 5000);
    unsigned long startTime = millis();
    bool touchDetected = false;
    
    while (millis() - startTime < 5000) {
        if (ui.getTouchscreen().touched()) {
            touchDetected = true;
            break;
        }
        delay(100);
    }
    
    if (touchDetected) {
        ui.showSuccess("Touch working");
    } else {
        ui.showError("Touch not detected");
    }
    
    // Test theme
    if (ui.isThemeLoaded()) {
        ui.showSuccess("Theme loaded");
    } else {
        ui.showError("Theme not loaded");
    }
    
    // Print performance stats
    printTFTPerformanceStats();
}

// Utility functions
String formatTFTTime(unsigned long timestamp) {
    return formatTime(timestamp);
}

String formatTFTTemperature(float temp) {
    return formatTemperature(temp);
}

String formatTFTDuration(unsigned long duration) {
    return formatDuration(duration);
}

// Advanced features
void enableTFTAutoRefresh(bool enabled) {
    // This could be used to enable/disable automatic theme refresh
    // For now, it's always enabled
}

void setTFTUpdateInterval(unsigned long interval) {
    // This could be used to adjust the update rate
    // For now, it's fixed at 50ms
}

// Backup and restore functions
void backupTFTSettings() {
    // This could save current TFT settings to EEPROM or SPIFFS
}

void restoreTFTSettings() {
    // This could restore TFT settings from EEPROM or SPIFFS
}

// Event logging
void logTFTEvent(const String& event) {
    if (getTFTIntegration().isInitialized()) {
        // Could log to a file or display on screen
    }
} 