#include "tft_ui.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// External variables from main firmware
extern float* targetTemp;
extern int maxTempPoints;
extern int getCurrentTempIndex();
extern float getSmoothedTargetTemperature();
extern String getCurrentTime();

// Static instances for callbacks
static ChartsScreen* chartsScreenInstance = nullptr;

// Constructor
ChartsScreen::ChartsScreen(TFT_UI* ui) : ui(ui) {
    chartsScreenInstance = this;
    
    // Initialize chart view settings
    chartStartHour = 0;
    chartEndHour = 24;
    minTempDisplay = 0;
    maxTempDisplay = 1200;
    needsRedraw = true;
    
    // Initialize the main 24-hour chart
    scheduleChart.x = 10;
    scheduleChart.y = 25;       // Moved up since no title
    scheduleChart.width = 300;  // Full width chart
    scheduleChart.height = 160; // Expanded back to 160px
    scheduleChart.minX = 0;
    scheduleChart.maxX = 23;    // 24 hours (0-23)
    scheduleChart.minY = 0;
    scheduleChart.maxY = 1200;
    scheduleChart.points = nullptr;
    scheduleChart.pointCount = 0;
    scheduleChart.maxPoints = maxTempPoints + 10; // Extra capacity
    scheduleChart.lineColor = ui->getTheme().primaryColor;
    scheduleChart.bgColor = ui->getTheme().cardBackground;
    scheduleChart.gridColor = ui->getTheme().borderColor;
    scheduleChart.visible = true;
    scheduleChart.showGrid = true;
}

// Destructor
ChartsScreen::~ChartsScreen() {
    if (scheduleChart.points) {
        delete[] scheduleChart.points;
    }
}

// Initialize screen
void ChartsScreen::init() {
    // No buttons - simplified interface
    buttonCount = 0;
    buttons = nullptr;
    
    // Initialize text elements
    textCount = 2;
    texts = new TFT_Text[textCount];
    
    // Max temperature scale indicator (top left corner)
    texts[0].x = 15;
    texts[0].y = 30;  // Moved down to avoid overlapping with top bar
    texts[0].text = "1200C";
    texts[0].size = 1;
    texts[0].color = ui->getTheme().textColor;
    texts[0].visible = true;
    texts[0].centered = false;
    
    // Min temperature scale indicator (bottom left corner)
    texts[1].x = 15;
    texts[1].y = 192;  // scheduleChart.y + scheduleChart.height + 7
    texts[1].text = "0C";
    texts[1].size = 1;
    texts[1].color = ui->getTheme().textColor;
    texts[1].visible = true;
    texts[1].centered = false;
    
    // Initialize chart points
    if (scheduleChart.points) {
        delete[] scheduleChart.points;
    }
    scheduleChart.points = new ChartPoint[scheduleChart.maxPoints];
    scheduleChart.pointCount = 0;
    
    needsRedraw = true;
}

// Update screen data
void ChartsScreen::update() {
    unsigned long currentTime = millis();
    
    // Check for theme changes and update chart colors accordingly
    bool currentThemeIsDark = ui->getTheme().isDarkMode;
    
    if (!themeInitialized || currentThemeIsDark != prevThemeIsDark) {
        // Theme changed - force full redraw and update chart colors
        needsRedraw = true;
        prevThemeIsDark = currentThemeIsDark;
        themeInitialized = true;
        
        // Update chart colors to match current theme
        scheduleChart.lineColor = ui->getTheme().primaryColor;
        scheduleChart.bgColor = ui->getTheme().cardBackground;
        scheduleChart.gridColor = ui->getTheme().borderColor;
        
        // Update text colors
        for (int i = 0; i < textCount; i++) {
            texts[i].color = ui->getTheme().textColor;
        }
        
        // Update chart point colors for existing data
        for (int i = 0; i < scheduleChart.pointCount; i++) {
            scheduleChart.points[i].color = ui->getTheme().primaryColor;
        }
        
        return; // Force full redraw in draw() method
    }
    
    // Auto-refresh chart data when changes are detected (every 10 seconds)
    static unsigned long lastChartUpdate = 0;
    if (currentTime - lastChartUpdate > 10000) {
        lastChartUpdate = currentTime;
        
        // Check if target temperature data has changed
        static int lastTempChecksum = 0;
        int currentChecksum = 0;
        if (targetTemp) {
            for (int i = 0; i < maxTempPoints; i++) {
                currentChecksum += (int)(targetTemp[i] * 10); // Simple checksum
            }
        }
        
        if (currentChecksum != lastTempChecksum) {
            updateChartData();
            lastTempChecksum = currentChecksum;
        }
    }
    
    // Update temperature scale displays
    String maxTempStr = String((int)maxTempDisplay) + "C";
    String minTempStr = String((int)minTempDisplay) + "C";
    
    // Update max temperature display (top left)
    if (texts[0].text != maxTempStr) {
        texts[0].text = maxTempStr;
        needsRedraw = true;
    }
    
    // Update min temperature display (bottom left)
    if (texts[1].text != minTempStr) {
        texts[1].text = minTempStr;
        needsRedraw = true;
    }
}

// Draw screen
void ChartsScreen::draw() {
    if (!needsRedraw) return;
    
    // Clear main content area (avoid status bar and navigation bar)
    const TFT_Theme& theme = ui->getTheme();
    ui->getTFT().fillRect(0, 20, TFT_WIDTH, TFT_HEIGHT - 50, theme.backgroundColor);
    
    // Draw chart card (no title)
    ui->drawCard(5, 20, 310, 170, "");
    
    // Draw the schedule chart
    ui->drawChart(scheduleChart);
    
    // Draw current time indicator on chart
    drawCurrentTimeIndicator();
    
    // Draw time axis at bottom of chart
    drawTimeAxis();
    
    // Draw text elements
    for (int i = 0; i < textCount; i++) {
        ui->drawText(texts[i]);
    }
    
    needsRedraw = false;
}

// Handle touch input
void ChartsScreen::handleTouch(TouchPoint& touch) {
    // Skip navigation bar touches
    if (touch.y > TFT_HEIGHT - 30) {
        return;
    }
    
    // No buttons to handle - simplified interface
}

// On screen show
void ChartsScreen::onShow() {
    needsRedraw = true;
    updateChartData(); // Load initial data, but won't auto-refresh afterwards
}

// Update chart data from target temperature array
void ChartsScreen::updateChartData() {
    if (!scheduleChart.points || !targetTemp) return;
    
    scheduleChart.pointCount = 0;
    
    // Calculate how many points per hour based on maxTempPoints
    float pointsPerHour = (float)maxTempPoints / 24.0;
    
    // Find min/max temperatures for auto-scaling
    float minTemp = 9999, maxTemp = -9999;
    bool hasValidData = false;
    
    for (int i = 0; i < maxTempPoints; i++) {
        if (targetTemp[i] >= 0 && targetTemp[i] <= 2000) {
            if (targetTemp[i] < minTemp) minTemp = targetTemp[i];
            if (targetTemp[i] > maxTemp) maxTemp = targetTemp[i];
            hasValidData = true;
        }
    }
    
    // Auto-fit chart range with padding (no manual zoom controls)
    if (hasValidData && maxTemp > minTemp) {
        float padding = (maxTemp - minTemp) * 0.1; // 10% padding
        minTempDisplay = max(0.0f, minTemp - padding);
        maxTempDisplay = maxTemp + padding;
        scheduleChart.minY = minTempDisplay;
        scheduleChart.maxY = maxTempDisplay;
    } else {
        // Use defaults if no valid data
        minTempDisplay = 0;
        maxTempDisplay = 1200;
        scheduleChart.minY = 0;
        scheduleChart.maxY = 1200;
    }
    
    // Add data points to chart
    for (int i = 0; i < maxTempPoints && scheduleChart.pointCount < scheduleChart.maxPoints; i++) {
        if (targetTemp[i] >= 0) {
            scheduleChart.points[scheduleChart.pointCount].x = (float)i / pointsPerHour; // Convert to hours
            scheduleChart.points[scheduleChart.pointCount].y = targetTemp[i];
            scheduleChart.points[scheduleChart.pointCount].color = ui->getTheme().primaryColor;
            scheduleChart.pointCount++;
        }
    }
}

// Draw current time indicator on the chart
void ChartsScreen::drawCurrentTimeIndicator() {
    if (!targetTemp) return;
    
    // Get current time index and convert to hours
    int currentIndex = getCurrentTempIndex();
    if (currentIndex < 0 || currentIndex >= maxTempPoints) return;
    
    float pointsPerHour = (float)maxTempPoints / 24.0;
    float currentHour = (float)currentIndex / pointsPerHour;
    
    // Calculate x position on chart
    int indicatorX = scheduleChart.x + ((currentHour - scheduleChart.minX) / (scheduleChart.maxX - scheduleChart.minX)) * scheduleChart.width;
    
    // Ensure it's within chart bounds
    if (indicatorX >= scheduleChart.x && indicatorX <= scheduleChart.x + scheduleChart.width) {
        // Draw vertical line indicator
        ui->getTFT().drawLine(indicatorX, scheduleChart.y, indicatorX, scheduleChart.y + scheduleChart.height, 
                             ui->getTheme().errorColor);
        
        // Draw small triangle at top
        int triSize = 3;
        for (int i = 0; i < triSize; i++) {
            ui->getTFT().drawLine(indicatorX - i, scheduleChart.y - i, indicatorX + i, scheduleChart.y - i, 
                                 ui->getTheme().errorColor);
        }
    }
}

// Draw time axis at bottom of chart
void ChartsScreen::drawTimeAxis() {
    TFT_eSPI& tft = ui->getTFT();
    const TFT_Theme& theme = ui->getTheme();
    
    tft.setTextColor(theme.textColor);
    tft.setTextSize(1);
    
    // Draw time labels every 6 hours (6, 12, 18) - skip 0:00
    for (int hour = 6; hour <= 18; hour += 6) {
        int x = scheduleChart.x + ((float)hour / (scheduleChart.maxX - scheduleChart.minX)) * scheduleChart.width;
        
        // Ensure x is within chart bounds
        if (x >= scheduleChart.x && x <= scheduleChart.x + scheduleChart.width) {
            // Draw tick mark
            tft.drawLine(x, scheduleChart.y + scheduleChart.height, x, scheduleChart.y + scheduleChart.height + 3, theme.borderColor);
            
            // Draw hour label - moved down a few more pixels
            String hourStr = String(hour) + ":00";
            int textWidth = hourStr.length() * 6; // Approximate text width
            int textX = x - textWidth / 2; // Center text on tick
            int textY = scheduleChart.y + scheduleChart.height + 8; // Moved down from +5 to +8
            
            tft.setCursor(textX, textY);
            tft.print(hourStr);
        }
    }
}

// No static callbacks needed - simplified interface 