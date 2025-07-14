#include "tft_ui.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "web_server_handler.h"

// External variables from main firmware
extern float currentTemp;
extern float* targetTemp;
extern bool furnaceStatus;
extern bool systemEnabled;
extern int getCurrentTempIndex();
extern int maxTempPoints;
extern float temperatureIncrement;
extern String getCurrentTime();
extern float getSmoothedTargetTemperature();

// Static instances for callbacks
static MainScreen* mainScreenInstance = nullptr;

// Forward declarations for static callback functions
static void onSystemToggle();
static void onTargetTempUp();
static void onTargetTempDown();
static void onRefresh();

// Constructor
MainScreen::MainScreen(TFT_UI* ui) : ui(ui) {
    mainScreenInstance = this;
    
    // Initialize chart data
    chartDataSize = 60; // Store 60 data points
    chartData = new float[chartDataSize];
    targetTempData = new float[chartDataSize]; // Add target temperature tracking
    chartIndex = 0;
    lastChartUpdate = 0;
    lastSecondUpdate = 0; // Initialize 1-second update timer
    
    // Initialize change tracking variables for anti-flashing
    lastDisplayedCurrentTemp = -999.0;
    lastDisplayedTargetTemp = -999.0;
    lastDisplayedFurnaceStatus = false;
    lastDisplayedSystemEnabled = false;
    lastTimeString = "";
    
    // Clear chart data
    for (int i = 0; i < chartDataSize; i++) {
        chartData[i] = 0.0;
        targetTempData[i] = 0.0;
    }
    
    // Initialize temperature chart - expanded to fill space where temp bar was
    tempChart.x = 10;
    tempChart.y = 72;  // Moved down 5px from 67
    tempChart.width = 230;  // Reduced by 5px from 240
    tempChart.height = 100;  // Reduced by 5px from 105
    tempChart.minX = 0;
    tempChart.maxX = chartDataSize - 1;
    tempChart.minY = 0;
    tempChart.maxY = 1200;  // Match the system maxTemp default
    tempChart.points = nullptr;
    tempChart.pointCount = 0;
    tempChart.maxPoints = chartDataSize * 2; // Double capacity for current + target data
    tempChart.lineColor = ui->getTheme().primaryColor;
    tempChart.bgColor = ui->getTheme().cardBackground;
    tempChart.gridColor = ui->getTheme().borderColor;
    tempChart.visible = true;
    tempChart.showGrid = true;
    
    // Temperature progress bar removed - no longer needed
    // tempBar is kept for compatibility but not used
    tempBar.x = 0;
    tempBar.y = 0;
    tempBar.width = 0;
    tempBar.height = 0;
    tempBar.value = 0;
    tempBar.maxValue = 1000;
    tempBar.fillColor = ui->getTheme().primaryColor;
    tempBar.bgColor = ui->getTheme().cardBackground;
    tempBar.borderColor = ui->getTheme().borderColor;
    tempBar.visible = false;  // Hide the progress bar
}

// Destructor
MainScreen::~MainScreen() {
    if (chartData) {
        delete[] chartData;
    }
    if (targetTempData) {
        delete[] targetTempData;
    }
    if (tempChart.points) {
        delete[] tempChart.points;
    }
}

// Initialize screen
void MainScreen::init() {
    // Initialize buttons (removed duplicate navigation buttons)
    buttonCount = 4;
    buttons = new TFT_Button[buttonCount];
    
    // System toggle button
    buttons[0].x = 254;
    buttons[0].y = 27;
    buttons[0].width = BUTTON_WIDTH_SMALL;
    buttons[0].height = BUTTON_HEIGHT;
    buttons[0].text = systemEnabled ? "ON" : "OFF";  // Initialize text based on current state
    buttons[0].state = BTN_NORMAL;  // Always allow button to be pressed
    buttons[0].visible = true;
    buttons[0].bgColor = systemEnabled ? ui->getTheme().successColor : ui->getTheme().errorColor;
    buttons[0].textColor = ui->getTheme().textColor;
    buttons[0].borderColor = ui->getTheme().borderColor;
    buttons[0].onPress = onSystemToggle;
    buttons[0].id = 0;
    buttons[0].pressedTime = 0;
    buttons[0].pressDuration = 100;
    
    // Target temperature up button
    buttons[1].x = 254;
    buttons[1].y = 27 + BUTTON_HEIGHT + 4; // 4px gap
    buttons[1].width = BUTTON_WIDTH_SMALL;
    buttons[1].height = BUTTON_HEIGHT;
    buttons[1].text = "Temp+";
    buttons[1].state = BTN_NORMAL;
    buttons[1].visible = true;
    buttons[1].bgColor = ui->getTheme().primaryColor;
    buttons[1].textColor = ui->getTheme().textColor;
    buttons[1].borderColor = ui->getTheme().borderColor;
    buttons[1].onPress = onTargetTempUp;
    buttons[1].id = 1;
    buttons[1].pressedTime = 0;
    buttons[1].pressDuration = 100;
    
    // Target temperature down button
    buttons[2].x = 254;
    buttons[2].y = 27 + 2 * (BUTTON_HEIGHT + 4); // 4px gap
    buttons[2].width = BUTTON_WIDTH_SMALL;
    buttons[2].height = BUTTON_HEIGHT;
    buttons[2].text = "Temp-";
    buttons[2].state = BTN_NORMAL;
    buttons[2].visible = true;
    buttons[2].bgColor = ui->getTheme().primaryColor;
    buttons[2].textColor = ui->getTheme().textColor;
    buttons[2].borderColor = ui->getTheme().borderColor;
    buttons[2].onPress = onTargetTempDown;
    buttons[2].id = 2;
    buttons[2].pressedTime = 0;
    buttons[2].pressDuration = 100;
    
    // Ref button
    buttons[3].x = 254;
    buttons[3].y = 27 + 3 * (BUTTON_HEIGHT + 4); // 4px gap
    buttons[3].width = BUTTON_WIDTH_SMALL;
    buttons[3].height = BUTTON_HEIGHT;
    buttons[3].text = "Refresh";
    buttons[3].state = BTN_NORMAL;
    buttons[3].visible = true;
    buttons[3].bgColor = ui->getTheme().primaryColor;
    buttons[3].textColor = ui->getTheme().textColor;
    buttons[3].borderColor = ui->getTheme().borderColor;
    buttons[3].onPress = onRefresh;
    buttons[3].id = 3;
    buttons[3].pressedTime = 0;
    buttons[3].pressDuration = 100;
    
    // Initialize text elements
    textCount = 4;  // Reduced from 5 - removing time display (already in status bar)
    texts = new TFT_Text[textCount];
    
    // Current temperature display (moved 5px left)
    texts[0].x = 135;
    texts[0].y = 27;
    texts[0].text = "25.0C";
    texts[0].size = 2;
    texts[0].color = ui->getTheme().textColor;
    texts[0].visible = true;
    texts[0].centered = false;
    
    // Target temperature display (moved 5px left)
    texts[1].x = 135;
    texts[1].y = 47;
    texts[1].text = "T:";
    texts[1].size = 2;
    texts[1].color = ui->getTheme().textColor;
    texts[1].visible = true;
    texts[1].centered = false;
    
    // System status display (moved up)
    texts[2].x = 15;
    texts[2].y = 45;  // Changed from 50 to 35
    texts[2].text = "System: OFF";
    texts[2].size = 1;
    texts[2].color = ui->getTheme().textColor;
    texts[2].visible = true;
    texts[2].centered = false;
    
    // Furnace status display (moved up)
    texts[3].x = 15;
    texts[3].y = 55;  // Changed from 60 to 45
    texts[3].text = "Furnace: OFF";
    texts[3].size = 1;
    texts[3].color = ui->getTheme().textColor;
    texts[3].visible = true;
    texts[3].centered = false;
    
    // Initialize chart points
    if (tempChart.points) {
        delete[] tempChart.points;
    }
    tempChart.points = new ChartPoint[chartDataSize * 2]; // Double capacity for current + target data
    tempChart.pointCount = 0;
    
    needsRedraw = true;
}

// Update screen data
void MainScreen::update() {
    unsigned long currentTime = millis();
    
    // Check what needs updating to avoid unnecessary redraws (V20 anti-flashing technique)
    bool tempChanged = (abs(currentTemp - lastDisplayedCurrentTemp) > 0.1);
    bool furnaceChanged = (furnaceStatus != lastDisplayedFurnaceStatus);
    bool systemChanged = (systemEnabled != lastDisplayedSystemEnabled);
    bool chartDataChanged = false;
    bool timeChanged = false;
    bool buttonStateChanged = false;
    
    // Get current time string and check if it changed
    String currentTimeStr = getCurrentTime();
    timeChanged = (currentTimeStr != lastTimeString);
    
    // Get smoothed target temperature from API
    float displayTargetTemp = 0.0;
    int tempIndex = getCurrentTempIndex();
    if (tempIndex >= 0 && tempIndex < maxTempPoints && targetTemp) {
        // Use smoothed target temperature if available, otherwise fall back to raw target
        displayTargetTemp = getSmoothedTargetTemperature();
    }
    bool targetChanged = (abs(displayTargetTemp - lastDisplayedTargetTemp) > 0.1);
    
    // Update temperature chart data every 15 seconds (reduced frequency for performance)
    if (currentTime - lastChartUpdate > 15000) {
        lastChartUpdate = currentTime;
        
        // Get current target temperature
        float currentTargetTemp = getSmoothedTargetTemperature();
        
        // Add new data points with bounds checking (only if temperature sensor is working)
        extern bool thermocoupleError;
        if (chartIndex >= 0 && chartIndex < chartDataSize && !thermocoupleError && currentTemp >= 0 && currentTemp < 2000) {
            chartData[chartIndex] = currentTemp;
            targetTempData[chartIndex] = currentTargetTemp;
            chartIndex = (chartIndex + 1) % chartDataSize;
        } else {
            // Reset if corrupted
            chartIndex = 0;
        }
        
        // Update chart points
        updateChart();
        chartDataChanged = true;
    }
    
    // Update frequency control - optimized for performance when using both TFT and Web
    static unsigned long lastCurrentTempUpdate = 0;
    static unsigned long lastTimeUpdate = 0;
    static unsigned long lastOtherUpdate = 0;
    
    bool shouldUpdateCurrentTemp = (currentTime - lastCurrentTempUpdate > 1500); // 1.5 seconds for temp (was 1s)
    bool shouldUpdateTime = (currentTime - lastTimeUpdate > 2000); // 2 seconds for time (was 1s)
    bool shouldUpdateOthers = (currentTime - lastOtherUpdate > 3000); // 3 seconds for others (was 2s)
    
    // Update timers
    if (tempChanged && shouldUpdateCurrentTemp) {
        lastCurrentTempUpdate = currentTime;
    }
    if (timeChanged && shouldUpdateTime) {
        lastTimeUpdate = currentTime;
    }
    if ((systemChanged || furnaceChanged || targetChanged || buttonStateChanged || chartDataChanged) && shouldUpdateOthers) {
        lastOtherUpdate = currentTime;
    }
    
    // Handle button text changes (state is managed in the button reset logic)
    static bool prevSystemEnabled = false;
    bool systemStateChanged = (systemEnabled != prevSystemEnabled);
    if (systemStateChanged) {
        buttons[0].text = systemEnabled ? "ON" : "OFF";
        buttons[0].bgColor = systemEnabled ? ui->getTheme().successColor : ui->getTheme().errorColor;
        prevSystemEnabled = systemEnabled;
        buttonStateChanged = true;
    }
    
    // Handle non-blocking button state reset
    for (int i = 0; i < buttonCount; i++) {
        if (buttons[i].state == BTN_PRESSED) {
            if (currentTime - buttons[i].pressedTime >= buttons[i].pressDuration) {
                buttons[i].state = BTN_NORMAL;  // Always return to normal state to allow pressing
                buttonStateChanged = true;
            }
        }
    }
    
    // Check for theme changes
    static bool prevThemeIsDark = false;
    static bool themeInitialized = false;
    bool currentThemeIsDark = ui->getTheme().isDarkMode;
    
    if (!themeInitialized || currentThemeIsDark != prevThemeIsDark) {
        // Theme changed - force full redraw
        needsRedraw = true;
        prevThemeIsDark = currentThemeIsDark;
        themeInitialized = true;
        
        // Update all text and button colors
        for (int i = 0; i < textCount; i++) {
            texts[i].color = ui->getTheme().textColor;
        }
        for (int i = 0; i < buttonCount; i++) {
            buttons[i].bgColor = ui->getTheme().primaryColor;
            buttons[i].textColor = ui->getTheme().textColor;
            buttons[i].borderColor = ui->getTheme().borderColor;
        }
        
        // Update status text colors
        texts[2].color = systemEnabled ? ui->getTheme().successColor : ui->getTheme().errorColor;
        texts[3].color = furnaceStatus ? ui->getTheme().successColor : ui->getTheme().errorColor;
        
        // Update chart and progress bar colors
        tempChart.lineColor = ui->getTheme().primaryColor;
        tempChart.bgColor = ui->getTheme().cardBackground;
        tempChart.gridColor = ui->getTheme().borderColor;
        tempBar.fillColor = ui->getTheme().primaryColor;
        tempBar.bgColor = ui->getTheme().cardBackground;
        tempBar.borderColor = ui->getTheme().borderColor;
        
        return; // Force full redraw in draw() method
    }
    
    // SELECTIVE DRAWING - Only draw what changed (V20 technique)
    
    // Draw current temperature if changed (Primary color) - 1.5 second update
    if (tempChanged && shouldUpdateCurrentTemp) {
        extern bool thermocoupleError;
        texts[0].text = "C:" + formatTemperature(currentTemp);
        texts[0].color = thermocoupleError ? ui->getTheme().errorColor : ui->getTheme().primaryColor;  // Red for error, primary color for normal
        drawSelectiveText(0); // Only redraw this text element
        lastDisplayedCurrentTemp = currentTemp;
    }
    
    // Draw target temperature if changed (Error color) - 3 second update
    if (targetChanged && shouldUpdateOthers) {
        if (tempIndex >= 0 && tempIndex < maxTempPoints && targetTemp) {
            texts[1].text = "T:" + formatTemperature(displayTargetTemp);
        } else {
            texts[1].text = "T: --C";
        }
        texts[1].color = ui->getTheme().errorColor;  // Color code with Error color
        drawSelectiveText(1); // Only redraw this text element
        lastDisplayedTargetTemp = displayTargetTemp;
    }
    
    // Draw system status if changed - 3 second update
    if (systemChanged && shouldUpdateOthers) {
        texts[2].text = "System: " + String(systemEnabled ? "ON" : "OFF");
        texts[2].color = systemEnabled ? ui->getTheme().successColor : ui->getTheme().errorColor;
        drawSelectiveText(2); // Only redraw this text element
        lastDisplayedSystemEnabled = systemEnabled;
    }
    
    // Draw furnace status if changed - 3 second update
    if (furnaceChanged && shouldUpdateOthers) {
        texts[3].text = "Furnace: " + String(furnaceStatus ? "ON" : "OFF");
        texts[3].color = furnaceStatus ? ui->getTheme().successColor : ui->getTheme().errorColor;
        drawSelectiveText(3); // Only redraw this text element
        lastDisplayedFurnaceStatus = furnaceStatus;
    }
    
    // Draw button if state changed - IMMEDIATE update for responsive feedback
    if (buttonStateChanged) {
        for (int i = 0; i < buttonCount; i++) {
            drawSelectiveButton(i);
        }
    }
    
    // Draw chart if data changed - 15 second update (handled separately)
    // Add protection against too frequent chart updates during slow performance
    static unsigned long lastChartDraw = 0;
    if (chartDataChanged && (currentTime - lastChartDraw > 3000)) { // Minimum 3 seconds between chart redraws (increased for performance)
        drawSelectiveChart(); // Only redraw the chart area
        lastChartDraw = currentTime;
    }
    
    // Update time string tracking
    if (timeChanged) {
        lastTimeString = currentTimeStr;
    }
    
    // DON'T set needsRedraw = true here - we've already drawn what changed
}

// Draw screen
void MainScreen::draw() {
    if (!needsRedraw) return;
    
    // Full screen redraw (only for initial draw or theme changes)
    // Clear main content area only - avoid status bar (top 20px) and navigation bar (bottom 30px)
    const TFT_Theme& theme = ui->getTheme();
    ui->getTFT().fillRect(0, 20, TFT_WIDTH, TFT_HEIGHT - 50, theme.backgroundColor);
    
    // Draw main temperature card
    drawTemperatureCard();
    
    // Draw control card
    drawControlCard();
    
    // Draw status card
    drawStatusCard();
    
    // Draw buttons
    for (int i = 0; i < buttonCount; i++) {
        ui->drawButton(buttons[i]);
    }
    
    // Draw text elements
    for (int i = 0; i < textCount; i++) {
        ui->drawText(texts[i]);
    }
    
    needsRedraw = false;
}

// Selective drawing methods (V20 anti-flashing technique)
void MainScreen::drawSelectiveText(int index) {
    if (index < 0 || index >= textCount) return;
    
    // Calculate text dimensions for background clearing
    int textWidth = texts[index].text.length() * 6 * texts[index].size + 10; // Extra padding
    int textHeight = 8 * texts[index].size + 4; // Extra padding
    
    // Clear background for all text elements to prevent ghost text
    ui->getTFT().fillRect(texts[index].x - 2, texts[index].y - 2, 
                          textWidth, textHeight, ui->getTheme().cardBackground);
    
    // Redraw only this text element
    ui->drawText(texts[index]);
}

void MainScreen::drawSelectiveButton(int index) {
    if (index < 0 || index >= buttonCount) return;
    
    // Clear just the button area with background color
    ui->getTFT().fillRect(buttons[index].x - 2, buttons[index].y - 2, 
                          buttons[index].width + 4, buttons[index].height + 4, 
                          ui->getTheme().cardBackground);
    
    // Redraw only this button
    ui->drawButton(buttons[index]);
}

void MainScreen::drawSelectiveChart() {
    // Only redraw the chart data, not the entire background
    // This prevents flashing by avoiding multiple fill operations
    
    // Clear just the chart data area (not the borders)
    ui->getTFT().fillRect(tempChart.x + 1, tempChart.y + 1, 
                          tempChart.width - 2, tempChart.height - 2, 
                          tempChart.bgColor);
    
    // Redraw only the chart data
    ui->drawChart(tempChart);
}

// Progress bar drawing method removed - no longer needed

// Handle touch input
void MainScreen::handleTouch(TouchPoint& touch) {
    // Skip navigation bar touches - let the navigation system handle them
    if (touch.y > TFT_HEIGHT - 30) {
        return;
    }
    
    // Check regular buttons
    for (int i = 0; i < buttonCount; i++) {
        if (ui->isTouchInButton(touch, buttons[i])) {
            if (buttons[i].onPress) {
                buttons[i].state = BTN_PRESSED;
                buttons[i].pressedTime = millis();
                buttons[i].pressDuration = 100; // 100ms press duration
                
                // Use selective drawing for button press
                drawSelectiveButton(i);
                
                // Call button callback
                buttons[i].onPress();
            }
            break;
        }
    }
    // Button state reset is handled by timer in update() method for proper visual feedback
}

// On screen show
void MainScreen::onShow() {
    needsRedraw = true;
    
    // Ensure chart has initial data regardless of WiFi connectivity
    if (chartData && targetTempData) {
        // Always add current readings to ensure chart displays something (only if sensor is working)
        extern bool thermocoupleError;
        if (!thermocoupleError) {
            chartData[chartIndex] = currentTemp >= 0 ? currentTemp : 0;
            targetTempData[chartIndex] = getSmoothedTargetTemperature();
            chartIndex = (chartIndex + 1) % chartDataSize;
        }
    }
    
    updateChart();
}

// Draw temperature card
void MainScreen::drawTemperatureCard() {
    ui->drawCard(5, 22, 240, 155, "Temperature Monitor");  // Reduced height from 165 to 155
    
    // Draw temperature chart (expanded to fill space)
    ui->drawChart(tempChart);
    
    // Progress bar removed - chart now fills the space
}

// Draw temperature card with optimized redraw to prevent flashing
void MainScreen::drawTemperatureCardBuffered() {
    // Just use the standard drawing method - the chart itself doesn't flash as much
    // The main issue was the text elements flashing
    drawTemperatureCard();
}

// Draw control card
void MainScreen::drawControlCard() {
    // Height: 4 buttons + 3 gaps + some margin
    int cardHeight = 4 * BUTTON_HEIGHT + 3 * 4 + 12; // 12px extra margin
    ui->drawCard(250, 22, 65, cardHeight, "Controls");
}

// Draw status card
void MainScreen::drawStatusCard() {
    ui->drawCard(5, 180, 240, 20, "");  // Moved up from 185 to 180 to maintain spacing
    
    // Draw additional status information
    TFT_eSPI& tft = ui->getTFT();
    const TFT_Theme& theme = ui->getTheme();
    
    // Clear the card area first to prevent text overlapping
    tft.fillRect(8, 182, 234, 16, theme.cardBackground);  // Adjusted y position from 187 to 182
    
    tft.setTextSize(1);
    tft.setTextColor(theme.textColor);
    
    // WiFi status
    tft.setCursor(10, 187);  // Adjusted y position from 192 to 187
    tft.print("WiFi: ");
    tft.setTextColor(WiFi.status() == WL_CONNECTED ? theme.successColor : theme.errorColor);
    tft.print(WiFi.status() == WL_CONNECTED ? "Connected" : "Disconnected");
    
    // Memory usage
    tft.setTextColor(theme.textColor);
    tft.setCursor(150, 187);  // Adjusted y position from 192 to 187
    tft.print("RAM: ");
    tft.print(ESP.getFreeHeap() / 1024);
    tft.print(" KB");
}



// Update chart data
void MainScreen::updateChart() {
    if (!tempChart.points) return;
    
    // Validate chartDataSize and chartIndex to prevent corruption
    if (chartDataSize <= 0 || chartIndex < 0 || chartIndex >= chartDataSize) {
        chartIndex = 0;
        return;
    }
    
    // Create dual-line chart: current temperature (blue) and target temperature (red)
    tempChart.pointCount = 0;
    
    // First, add all current temperature points as a continuous line
    for (int i = 0; i < chartDataSize && tempChart.pointCount < tempChart.maxPoints; i++) {
        int dataIndex = (chartIndex + i) % chartDataSize;
        if (dataIndex >= 0 && dataIndex < chartDataSize && chartData[dataIndex] >= 0) {
            tempChart.points[tempChart.pointCount].x = i;
            tempChart.points[tempChart.pointCount].y = chartData[dataIndex];
            tempChart.points[tempChart.pointCount].color = ui->getTheme().primaryColor; // Blue for current temp
            tempChart.pointCount++;
        }
    }
    
    // Add a separator point to break the line connection (invisible point)
    if (tempChart.pointCount < tempChart.maxPoints) {
        tempChart.points[tempChart.pointCount].x = -1; // Invalid X coordinate as separator
        tempChart.points[tempChart.pointCount].y = 0;
        tempChart.points[tempChart.pointCount].color = 0; // No color (separator)
        tempChart.pointCount++;
    }
    
    // Then, add all target temperature points as a separate continuous line
    for (int i = 0; i < chartDataSize && tempChart.pointCount < tempChart.maxPoints; i++) {
        int dataIndex = (chartIndex + i) % chartDataSize;
        if (dataIndex >= 0 && dataIndex < chartDataSize && targetTempData[dataIndex] >= 0) {
            tempChart.points[tempChart.pointCount].x = i;
            tempChart.points[tempChart.pointCount].y = targetTempData[dataIndex];
            tempChart.points[tempChart.pointCount].color = ui->getTheme().errorColor; // Red for target temp
            tempChart.pointCount++;
        }
    }
    
    // Set chart max X to accommodate time range
    tempChart.maxX = chartDataSize - 1;
    
    // Get min/max temperatures directly from global variables (more efficient than HTTP API)
    bool apiDataValid = false;
    extern float minTemp, maxTemp;
    
    // Use global min/max if available and reasonable  
    if (minTemp >= 0 && maxTemp > minTemp && maxTemp <= 2000) {
        tempChart.minY = minTemp - 10;
        tempChart.maxY = maxTemp + 10;
        apiDataValid = true;
    }
    
    // If API data is not valid, calculate from both current and target temperature data
    if (!apiDataValid) {
        float minTemp = 9999, maxTemp = -9999;
        bool hasValidData = false;
        
        // Check current temperature data
        for (int i = 0; i < chartDataSize; i++) {
            int dataIndex = (chartIndex + i) % chartDataSize;
            if (dataIndex >= 0 && dataIndex < chartDataSize) {
                if (chartData[dataIndex] > 0 && chartData[dataIndex] < 2000) {
                    if (chartData[dataIndex] < minTemp) minTemp = chartData[dataIndex];
                    if (chartData[dataIndex] > maxTemp) maxTemp = chartData[dataIndex];
                    hasValidData = true;
                }
                if (targetTempData[dataIndex] > 0 && targetTempData[dataIndex] < 2000) {
                    if (targetTempData[dataIndex] < minTemp) minTemp = targetTempData[dataIndex];
                    if (targetTempData[dataIndex] > maxTemp) maxTemp = targetTempData[dataIndex];
                    hasValidData = true;
                }
            }
        }
        
        if (hasValidData && maxTemp > minTemp && (maxTemp - minTemp) < 1500) { // Reasonable range check
            tempChart.minY = minTemp - 10;
            tempChart.maxY = maxTemp + 10;
        } else {
            // Use reasonable defaults if no data available or data is corrupted
            tempChart.minY = 0;
            tempChart.maxY = 1200;
        }
    }
    

    

}

// Helper method
String MainScreen::formatTemperature(float temp) {
    extern bool thermocoupleError;
    if (thermocoupleError) {
        return "ERROR";
    }
    return String(temp, 1) + "C";  // Removed degree symbol - TFT can't display it
}

// Static button callback implementations
static void onSystemToggle() {
    if (!mainScreenInstance) return;
    
    // Toggle system state directly (more efficient than HTTP request)
    extern bool systemEnabled, furnaceStatus;
    
    systemEnabled = !systemEnabled;
    
    // Handle system state change (same logic as web API)
    if (!systemEnabled) {
        // When turning off, ensure furnace is off
        furnaceStatus = false;
        digitalWrite(RELAY_PIN, LOW);  // RELAY_PIN is a macro from config.h
    }
    // When turning on, let the main loop handle furnace control
    
    mainScreenInstance->getUI()->showSuccess("System " + String(systemEnabled ? "enabled" : "disabled"));
}

static void onTargetTempUp() {
    if (!mainScreenInstance) return;
    
    int tempIndex = getCurrentTempIndex();
    if (tempIndex >= 0 && tempIndex < maxTempPoints && targetTemp) {
        float newTemp = targetTemp[tempIndex] + 50.0;
        if (newTemp <= 1200.0) {
            // Update temperature directly (more efficient than HTTP request)
            targetTemp[tempIndex] = newTemp;
            mainScreenInstance->getUI()->showSuccess("Target: " + mainScreenInstance->formatTempForCallback(newTemp));
        } else {
            mainScreenInstance->getUI()->showError("Maximum temperature reached");
        }
    }
}

static void onTargetTempDown() {
    if (!mainScreenInstance) return;
    
    int tempIndex = getCurrentTempIndex();
    if (tempIndex >= 0 && tempIndex < maxTempPoints && targetTemp) {
        float newTemp = targetTemp[tempIndex] - 50.0;
        if (newTemp >= 0.0) {
            // Update temperature directly (more efficient than HTTP request)
            targetTemp[tempIndex] = newTemp;
            mainScreenInstance->getUI()->showSuccess("Target: " + mainScreenInstance->formatTempForCallback(newTemp));
        } else {
            mainScreenInstance->getUI()->showError("Minimum temperature reached");
        }
    }
}

static void onRefresh() {
    if (!mainScreenInstance) return;
    mainScreenInstance->needsRedraw = true;
}

 