#include "tft_ui.h"
#include <WiFi.h>
#include <Preferences.h>

// Global UI instance
TFT_UI tftUI;

// External theme utility functions
extern uint16_t getButtonColor(const TFT_Theme& theme, ButtonState state);
extern uint16_t getTextColor(const TFT_Theme& theme, ButtonState state);
extern uint16_t getCardShadowColor(const TFT_Theme& theme);
extern uint16_t getGridColor(const TFT_Theme& theme);
extern uint16_t getContrastColor(uint16_t backgroundColor);

// External variables from main firmware
extern float currentTemp;
extern float* targetTemp;
extern bool furnaceStatus;
extern bool systemEnabled;
extern int getCurrentTempIndex();
extern int maxTempPoints;
extern bool ap_active;
extern String ap_password;

// Constructor
TFT_UI::TFT_UI() : touchscreenSPI(VSPI), touchscreen(XPT2046_CS, XPT2046_IRQ) {
    // Initialize screen array
    for (int i = 0; i < SCREEN_COUNT; i++) {
        screens[i] = nullptr;
    }
}

// Destructor
TFT_UI::~TFT_UI() {
    deleteScreens();
    cleanupSmallBuffers();
}

// Initialize the TFT UI system
void TFT_UI::init() {
    // Prevent duplicate initialization
    static bool initialized = false;
    if (initialized) {
        return;
    }
    
    // Initialize display
    tft.init();
    tft.setRotation(1); // Landscape
    tft.fillScreen(TFT_BLACK);
    
    // Initialize touchscreen
    touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touchscreen.begin(touchscreenSPI);
    touchscreen.setRotation(1);
    

    
    // Show initialization message
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 100);
    tft.println("Furnace TFT UI");
    tft.setTextSize(1);
    tft.setCursor(10, 130);
    tft.println("Loading...");
    
    // Load theme
    loadTheme();
    
    // Load touch calibration
    loadTouchCalibration();
    
    // Quick hardware test
    delay(1000);
    
    // Test a few readings to see if we're getting valid data
    int zeroReadings = 0;
    int maxPressureReadings = 0;
    
    for (int i = 0; i < 10; i++) {
        if (touchscreen.touched()) {
            TS_Point p = touchscreen.getPoint();
            if (p.x == 0 && p.y == 0) zeroReadings++;
            if (p.z >= 4090) maxPressureReadings++;
        }
        delay(50);
    }
    
    if (zeroReadings >= 5) {
        // Touchscreen may have hardware issues - zero X/Y coordinates detected
    }
    
    if (maxPressureReadings >= 5) {
        // Touchscreen may be stuck - maximum pressure readings detected
    }
    
    // Initialize screens
    initializeScreens();
    
    // Initialize small region buffers
    initSmallBuffers();
    
    // Set initial screen
    currentScreen = SCREEN_MAIN;
    screenNeedsRedraw = true;
    
    // Mark as initialized
    initialized = true;
}

// Begin UI operations
void TFT_UI::begin() {
    if (screens[currentScreen]) {
        screens[currentScreen]->onShow();
    }
}

// Initialize all screens
void TFT_UI::initializeScreens() {
    deleteScreens(); // Clean up existing screens
    
    // Create screen instances
    screens[SCREEN_MAIN] = new MainScreen(this);
    screens[SCREEN_SETTINGS] = new SettingsScreen(this);
    screens[SCREEN_PROGRAMS] = new ProgramsScreen(this);
    screens[SCREEN_CHARTS] = new ChartsScreen(this);
    screens[SCREEN_WIFI_SETUP] = new WiFiSetupScreen(this);
    
    // Initialize screens
    for (int i = 0; i < SCREEN_COUNT; i++) {
        if (screens[i]) {
            screens[i]->init();
        }
    }
}

// Delete all screens
void TFT_UI::deleteScreens() {
    for (int i = 0; i < SCREEN_COUNT; i++) {
        if (screens[i]) {
            delete screens[i];
            screens[i] = nullptr;
        }
    }
}

// Set current screen
void TFT_UI::setScreen(ScreenType screen) {
    if (screen >= 0 && screen < SCREEN_COUNT && screens[screen]) {
        if (screens[currentScreen]) {
            screens[currentScreen]->onHide();
        }
        
        // Clear only the main content area, preserve status bar and navigation bar
        tft.fillRect(0, 20, TFT_WIDTH, TFT_HEIGHT - 50, theme.backgroundColor);
        
        currentScreen = screen;
        screenNeedsRedraw = true;
        
        if (screens[currentScreen]) {
            screens[currentScreen]->needsRedraw = true; // Force new screen to redraw
            screens[currentScreen]->onShow();
        }
        
        // Ensure status bar and navigation bar are redrawn
        drawBufferedStatusBar();
        drawBufferedNavBar();
    }
}

// Show specific screen
void TFT_UI::showScreen(ScreenType screen) {
    setScreen(screen);
}

// Main update loop
void TFT_UI::update() {
    unsigned long currentTime = millis();
    
    // Check for theme updates from backend (periodic sync)
    checkForThemeUpdates();
    
    // Adjust update frequency based on activity
    if (!shouldUseHighFrequency()) {
        adjustUpdateFrequency();
    }
    
    // Force status bar update every second for time updates (V20 anti-flashing technique)
    static unsigned long lastStatusBarUpdate = 0;
    if (currentTime - lastStatusBarUpdate >= 1000) {
        lastStatusBarUpdate = currentTime;
        drawBufferedStatusBar(); // Force status bar update for time
    }
    
    // Throttle screen updates to maintain reasonable frame rate
    if (currentTime - lastUpdate < updateInterval) {
        return;
    }
    lastUpdate = currentTime;
    
    // Handle touch input
    handleTouch();
    
    // Update current screen
    if (screens[currentScreen]) {
        screens[currentScreen]->update();
        
        // Check if screen needs redraw and propagate to main UI
        if (screens[currentScreen]->needsRedraw) {
            screenNeedsRedraw = true;
        }
    }
    
    // Handle active modals independently (no full screen redraw)
    if (hasActiveModal()) {
        if (currentScreen == SCREEN_SETTINGS) {
            SettingsScreen* settingsScreen = static_cast<SettingsScreen*>(screens[SCREEN_SETTINGS]);
            if (settingsScreen && settingsScreen->hasActiveNumberPicker()) {
                // Update picker wheels independently without triggering screen redraw
                settingsScreen->drawActiveNumberPicker();
            }
        } else if (currentScreen == SCREEN_PROGRAMS) {
            ProgramsScreen* programsScreen = static_cast<ProgramsScreen*>(screens[SCREEN_PROGRAMS]);
            if (programsScreen && programsScreen->hasActiveTimePicker()) {
                // Update picker wheels independently without triggering screen redraw
                programsScreen->drawActiveTimePicker();
            }
        }
    }
    
    // SELECTIVE REDRAW - only redraw changed regions
    if (screenNeedsRedraw) {
        // Always use selective redraw system
        drawSelectiveScreen();
        screenNeedsRedraw = false;
    }
    
    // Message display disabled - no popup notifications
}

// Force full screen redraw
void TFT_UI::forceRedraw() {
    screenNeedsRedraw = true;
    
    // Also force the individual screen to redraw
    if (screens[currentScreen]) {
        screens[currentScreen]->needsRedraw = true;
    }
}

// Handle touch input
void TFT_UI::handleTouch() {
    if (touchscreen.tirqTouched() && touchscreen.touched()) {
        unsigned long currentTime = millis();
        
        // Debounce touch
        if (currentTime - lastTouchTime < touchDebounceTime) {
            return;
        }
        lastTouchTime = currentTime;
        
        TouchPoint touch = getTouch();
        
        if (touch.isPressed) {
            // Use navigation system to handle touches
            handleNavigationTouch(touch);
        }
    }
}

// Get touch coordinates
TouchPoint TFT_UI::getTouch() {
    TouchPoint touch;
    touch.isPressed = false;
    touch.timestamp = millis();
    
    if (touchscreen.touched()) {
        TS_Point p = touchscreen.getPoint();
        

        
        // Diagnostic: Check if we're getting stuck readings
        static int consecutiveZeroCount = 0;
        static int consecutiveMaxPressure = 0;
        
        if (p.x == 0 && p.y == 0) {
            consecutiveZeroCount++;
        } else {
            consecutiveZeroCount = 0;
        }
        
        if (p.z >= 4090) {  // Near maximum pressure
            consecutiveMaxPressure++;
        } else {
            consecutiveMaxPressure = 0;
        }
        
        // Warn about potential hardware issues
        if (consecutiveZeroCount > 5) {
            consecutiveZeroCount = 0;  // Reset to avoid spam
        }
        
        if (consecutiveMaxPressure > 5) {
            consecutiveMaxPressure = 0;  // Reset to avoid spam
        }
        
        // Check if we have valid touch data (require non-zero coordinates AND reasonable pressure)
        if (p.z > 10 && p.z < 4090 && (p.x > 0 || p.y > 0)) {
            // Use calibrated values
            touch.x = map(p.x, touchXMin, touchXMax, 0, TFT_WIDTH);
            touch.y = map(p.y, touchYMin, touchYMax, 0, TFT_HEIGHT);
            
            // Constrain to screen bounds
            touch.x = constrain(touch.x, 0, TFT_WIDTH - 1);
            touch.y = constrain(touch.y, 0, TFT_HEIGHT - 1);
            
            touch.isPressed = true;
        }
    }
    
    return touch;
}

// Check if touch is within button bounds
bool TFT_UI::isTouchInButton(const TouchPoint& touch, const TFT_Button& button) {
    if (!button.visible || !touch.isPressed) {
        return false;
    }
    
    return (touch.x >= button.x && 
            touch.x <= button.x + button.width &&
            touch.y >= button.y && 
            touch.y <= button.y + button.height);
}

// Clear screen with background color
void TFT_UI::clearScreen() {
    // Clear the entire screen with the background color
    tft.fillScreen(theme.backgroundColor);
    screenNeedsRedraw = true;
}

// Draw button
void TFT_UI::drawButton(const TFT_Button& button) {
    if (!button.visible) return;
    
    uint16_t bgColor = getButtonColor(theme, button.state);
    uint16_t textColor = getTextColor(theme, button.state);
    
    // Draw button background - removed rounded corners
    tft.fillRect(button.x, button.y, button.width, button.height, bgColor);
    
    // Draw button border - removed rounded corners
    tft.drawRect(button.x, button.y, button.width, button.height, theme.borderColor);
    
    // Draw button text
    tft.setTextColor(textColor);
    tft.setTextSize(1);
    
    // Center text in button
    int textWidth = button.text.length() * 6;
    int textHeight = 8;
    int textX = button.x + (button.width - textWidth) / 2;
    int textY = button.y + (button.height - textHeight) / 2;
    
    tft.setCursor(textX, textY);
    tft.println(button.text);
}

// Draw text
void TFT_UI::drawText(const TFT_Text& text) {
    if (!text.visible) return;
    
    tft.setTextColor(text.color);
    tft.setTextSize(text.size);
    
    if (text.centered) {
        int textWidth = text.text.length() * 6 * text.size;
        int centeredX = text.x - textWidth / 2;
        tft.setCursor(centeredX, text.y);
    } else {
        tft.setCursor(text.x, text.y);
    }
    
    tft.println(text.text);
}

// Draw progress bar
void TFT_UI::drawProgressBar(const TFT_ProgressBar& bar) {
    if (!bar.visible) return;
    
    // Draw background
    tft.fillRect(bar.x, bar.y, bar.width, bar.height, bar.bgColor);
    tft.drawRect(bar.x, bar.y, bar.width, bar.height, bar.borderColor);
    
    // Draw progress fill
    float progress = bar.value / bar.maxValue;
    progress = constrain(progress, 0.0, 1.0);
    int fillWidth = bar.width * progress;
    
    if (fillWidth > 0) {
        tft.fillRect(bar.x + 1, bar.y + 1, fillWidth - 2, bar.height - 2, bar.fillColor);
    }
}

// Draw simple chart
void TFT_UI::drawChart(const TFT_Chart& chart) {
    if (!chart.visible || chart.pointCount < 2) return;
    
    // Draw background
    tft.fillRect(chart.x, chart.y, chart.width, chart.height, chart.bgColor);
    tft.drawRect(chart.x, chart.y, chart.width, chart.height, theme.borderColor);
    
    // Draw grid if enabled
    if (chart.showGrid) {
        uint16_t gridColor = getGridColor(theme);
        
        // Vertical grid lines
        for (int i = 1; i < 4; i++) {
            int x = chart.x + (chart.width * i) / 4;
            tft.drawLine(x, chart.y, x, chart.y + chart.height, gridColor);
        }
        
        // Horizontal grid lines
        for (int i = 1; i < 4; i++) {
            int y = chart.y + (chart.height * i) / 4;
            tft.drawLine(chart.x, y, chart.x + chart.width, y, gridColor);
        }
    }
    
    // Draw data points using individual point colors for multi-series support
    for (int i = 1; i < chart.pointCount; i++) {
        // Skip drawing if current or previous point is a separator (x = -1)
        if (chart.points[i-1].x < 0 || chart.points[i].x < 0) {
            continue;
        }
        
        // Map data points to screen coordinates
        int x1 = chart.x + ((chart.points[i-1].x - chart.minX) / (chart.maxX - chart.minX)) * chart.width;
        int y1 = chart.y + chart.height - ((chart.points[i-1].y - chart.minY) / (chart.maxY - chart.minY)) * chart.height;
        int x2 = chart.x + ((chart.points[i].x - chart.minX) / (chart.maxX - chart.minX)) * chart.width;
        int y2 = chart.y + chart.height - ((chart.points[i].y - chart.minY) / (chart.maxY - chart.minY)) * chart.height;
        
        // Use individual point color if available, otherwise fallback to chart line color
        uint16_t lineColor = chart.points[i].color != 0 ? chart.points[i].color : chart.lineColor;
        
        // Ensure coordinates are within chart bounds
        x1 = max(chart.x, min(chart.x + chart.width, x1));
        y1 = max(chart.y, min(chart.y + chart.height, y1));
        x2 = max(chart.x, min(chart.x + chart.width, x2));
        y2 = max(chart.y, min(chart.y + chart.height, y2));
        
        tft.drawLine(x1, y1, x2, y2, lineColor);
    }
}

// Draw card with title
void TFT_UI::drawCard(int x, int y, int width, int height, const String& title) {
    // Draw card background
    tft.fillRoundRect(x, y, width, height, 8, theme.cardBackground);
    
    // Draw card border
    tft.drawRoundRect(x, y, width, height, 8, theme.borderColor);
    
    // Draw shadow effect
    uint16_t shadowColor = getCardShadowColor(theme);
    tft.drawRoundRect(x + 2, y + 2, width, height, 8, shadowColor);
    
    // Draw title if provided
    if (title.length() > 0) {
        tft.setTextColor(theme.textColor);
        tft.setTextSize(1);
        tft.setCursor(x + 8, y + 8);
        tft.println(title);
        
        // Draw title underline
        tft.drawLine(x + 8, y + 20, x + width - 8, y + 20, theme.borderColor);
    }
}

// Draw status bar
void TFT_UI::drawStatusBar() {
    // Draw status bar background
    tft.fillRect(0, 0, TFT_WIDTH, 20, theme.cardBackground);
    tft.drawLine(0, 20, TFT_WIDTH, 20, theme.borderColor);
    
    // Draw WiFi IP address or status
    tft.setTextColor(theme.textColor);
    tft.setTextSize(1);
    tft.setCursor(5, 6);
    if (WiFi.status() == WL_CONNECTED) {
        tft.setTextColor(theme.successColor);
        tft.print(WiFi.localIP().toString());
    } else {
        tft.setTextColor(theme.errorColor);
        tft.print("No WiFi");
    }
    
    // Draw AP password in the middle if AP is active
    if (ap_active && ap_password.length() > 0) {
        tft.setTextColor(theme.textColor);
        tft.setCursor(70, 6);
        tft.print("AP Password: ");
        tft.setTextColor(theme.successColor); // Use success color for better visibility
        tft.print(ap_password);
    }
    
    // Draw current time
    tft.setTextColor(theme.textColor);
    
    // Get current time string and adjust position based on content
    String timeStr = getCurrentTime();
    int timeX = 270;  // Default position
    
    // Adjust position based on time string content
    if (timeStr.indexOf("(M)") != -1) {
        // Manual time: "HH:MM:SS (M)" (12 chars) = 72 pixels
        timeX = 246;  // Move left by 24 pixels
    } else if (timeStr.indexOf("not synced") != -1 || timeStr.indexOf("error") != -1) {
        // Time not synced: "Time not synced" (14 chars) = 84 pixels
        timeX = 220;  // Move left by 50 pixels to fit
    }
    
    tft.setCursor(timeX, 6);
    tft.print(timeStr);
}



// Show message (disabled - no popup notifications)
void TFT_UI::showMessage(const String& message, uint16_t color, int duration) {
    // Notifications disabled
}

// Show error message (disabled - no popup notifications)
void TFT_UI::showError(const String& error) {
    // Notifications disabled - only log to serial

}

// Show success message (disabled - no popup notifications)
void TFT_UI::showSuccess(const String& message) {
    // Notifications disabled
}

// Utility functions
String formatTime(unsigned long timestamp) {
    unsigned long seconds = timestamp / 1000;
    unsigned long hours = seconds / 3600;
    unsigned long minutes = (seconds % 3600) / 60;
    seconds = seconds % 60;
    
    char timeStr[9];
    sprintf(timeStr, "%02lu:%02lu:%02lu", hours, minutes, seconds);
    return String(timeStr);
}

String formatTemperature(float temp) {
    return String(temp, 1);
}

String formatDuration(unsigned long duration) {
    unsigned long seconds = duration / 1000;
    unsigned long minutes = seconds / 60;
    unsigned long hours = minutes / 60;
    
    if (hours > 0) {
        return String(hours) + "h " + String(minutes % 60) + "m";
    } else if (minutes > 0) {
        return String(minutes) + "m " + String(seconds % 60) + "s";
    } else {
        return String(seconds) + "s";
    }
}

// Touch calibration functions
void TFT_UI::calibrateTouch() {
    
    clearScreen();
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 100);
    tft.println("Touch Calibration");
    tft.setTextSize(1);
    tft.setCursor(10, 130);
    tft.println("Touch corners when prompted");
    
    delay(2000);
    
    int calPoints[4][2]; // [point][x,y]
    const char* prompts[] = {"Top-Left", "Top-Right", "Bottom-Left", "Bottom-Right"};
    
    for (int i = 0; i < 4; i++) {
        clearScreen();
        tft.setTextColor(TFT_WHITE);
        tft.setTextSize(2);
        tft.setCursor(10, 100);
        tft.println("Touch: " + String(prompts[i]));
        
        // Draw target point
        int targetX = (i == 0 || i == 2) ? 20 : TFT_WIDTH - 20;
        int targetY = (i == 0 || i == 1) ? 20 : TFT_HEIGHT - 20;
        
        tft.fillCircle(targetX, targetY, 10, TFT_RED);
        tft.drawCircle(targetX, targetY, 15, TFT_WHITE);
        
        // Wait for touch
        bool touched = false;
        while (!touched) {
            if (touchscreen.touched()) {
                TS_Point p = touchscreen.getPoint();
                if (p.z > 10) {
                    calPoints[i][0] = p.x;
                    calPoints[i][1] = p.y;
                    touched = true;
                }
                delay(100);
            }
        }
        
        // Wait for release
        while (touchscreen.touched()) {
            delay(50);
        }
        
        delay(500);
    }
    
    // Calculate calibration values
    touchXMin = min(calPoints[0][0], calPoints[2][0]);
    touchXMax = max(calPoints[1][0], calPoints[3][0]);
    touchYMin = min(calPoints[0][1], calPoints[1][1]);
    touchYMax = max(calPoints[2][1], calPoints[3][1]);
    
    // Add some margin
    int xMargin = (touchXMax - touchXMin) * 0.1;
    int yMargin = (touchYMax - touchYMin) * 0.1;
    
    touchXMin -= xMargin;
    touchXMax += xMargin;
    touchYMin -= yMargin;
    touchYMax += yMargin;
    
    touchCalibrated = true;
    
    saveTouchCalibration();
    
    clearScreen();
    tft.setTextColor(TFT_GREEN);
    tft.setTextSize(2);
    tft.setCursor(10, 100);
    tft.println("Calibration");
    tft.setCursor(10, 130);
    tft.println("Complete!");
    
    delay(2000);
    forceRedraw();
}

void TFT_UI::loadTouchCalibration() {
    Preferences prefs;
    prefs.begin("touch_cal", true);
    
    if (prefs.isKey("calibrated")) {
        touchXMin = prefs.getInt("xMin", 300);
        touchXMax = prefs.getInt("xMax", 3800);
        touchYMin = prefs.getInt("yMin", 300);
        touchYMax = prefs.getInt("yMax", 3800);
        touchCalibrated = prefs.getBool("calibrated", false);
    }
    
    prefs.end();
}

void TFT_UI::saveTouchCalibration() {
    Preferences prefs;
    prefs.begin("touch_cal", false);
    
    prefs.putInt("xMin", touchXMin);
    prefs.putInt("xMax", touchXMax);
    prefs.putInt("yMin", touchYMin);
    prefs.putInt("yMax", touchYMax);
    prefs.putBool("calibrated", touchCalibrated);
    
    prefs.end();
}

// Diagnostic function to test SPI communication
void TFT_UI::testTouchSPI() {
    // Test pin states
    digitalRead(XPT2046_CS);
    digitalRead(XPT2046_IRQ);
    digitalRead(XPT2046_CLK);
    digitalRead(XPT2046_MISO);
    digitalRead(XPT2046_MOSI);
    
    // Test multiple readings
    for (int i = 0; i < 10; i++) {
        if (touchscreen.touched()) {
            TS_Point p = touchscreen.getPoint();
        }
        delay(100);
    }
    
    // Test IRQ pin
    for (int i = 0; i < 5; i++) {
        bool irqState = digitalRead(XPT2046_IRQ);
        bool touchState = touchscreen.touched();
        delay(500);
    }
}

// Alternative touchscreen initialization with different settings
void TFT_UI::reinitTouchscreen() {
    
    // Try different SPI settings
    touchscreenSPI.end();
    delay(100);
    
    // Reinitialize with different frequency
    touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
    touchscreenSPI.setFrequency(1000000);  // Try slower frequency
    
    touchscreen.begin(touchscreenSPI);
    touchscreen.setRotation(1);
}

// Test if touchscreen hardware is responding
bool TFT_UI::testTouchHardware() {
    // Test 1: Check if IRQ pin changes state when touched
    bool irqChangeDetected = false;
    bool lastIrqState = digitalRead(XPT2046_IRQ);
    
    for (int i = 0; i < 50; i++) {  // Test for 5 seconds
        bool currentIrqState = digitalRead(XPT2046_IRQ);
        if (currentIrqState != lastIrqState) {
            irqChangeDetected = true;
            lastIrqState = currentIrqState;
        }
        delay(100);
    }
    
    if (!irqChangeDetected) {
        return false;
    }
    
    // Test 2: Check if we can get any non-zero readings
    int validReadings = 0;
    
    for (int i = 0; i < 20; i++) {
        if (touchscreen.touched()) {
            TS_Point p = touchscreen.getPoint();
            if (p.x != 0 || p.y != 0) {
                validReadings++;
            }
        }
        delay(50);
    }
    
    if (validReadings == 0) {
        return false;
    }
    
    return true;
} 

// ========================= DIGIT WHEEL =========================

// Constructor
DigitWheel::DigitWheel(TFT_UI* ui, int x, int y, int width, int height, int minDigit, int maxDigit)
    : ui(ui), wheelX(x), wheelY(y), wheelWidth(width), wheelHeight(height), 
      minDigit(minDigit), maxDigit(maxDigit), selectedValue(minDigit) {
    
    itemHeight = 20;
    scrollOffset = 0.0f;
    targetScrollOffset = 0.0f;
    isDragging = false;
    scrollVelocity = 0.0f;
    lastAnimationTime = millis();
    
    // Initialize constraint system
    constraintType = CONSTRAINT_NONE;
    constraintPosition = 0;
    constraintMaxValue = 0;
    wheelNeedsRedraw = true;
}

// Destructor
DigitWheel::~DigitWheel() {
    // Clean up
}

// Update animation and state
void DigitWheel::update() {
    unsigned long currentTime = millis();
    float deltaTime = (currentTime - lastAnimationTime) / 1000.0f;
    
    if (!isDragging) {
        // Reduced minimum velocity for inertial scrolling
        if (abs(scrollVelocity) > 0.8f) {
            float movement = scrollVelocity * deltaTime;
            float newScrollOffset = scrollOffset + movement;
            
            int totalItems = maxDigit - minDigit + 1;
            float maxScroll = (totalItems - 1) * itemHeight;
            
            if (newScrollOffset < 0.0f || newScrollOffset > maxScroll) {
                scrollVelocity = 0.0f;
                scrollOffset = constrain(newScrollOffset, 0.0f, maxScroll);
                snapToNearest();
            } else {
                scrollOffset = newScrollOffset;
                
                // Increased friction for faster stopping
                float friction = 0.93f;
                scrollVelocity *= pow(friction, deltaTime * 60.0f);
                
                // Adjusted velocity threshold for snapping
                if (abs(scrollVelocity) < 80.0f) {
                    snapToNearest();
                    scrollVelocity = 0.0f;
                }
            }
            
            wheelNeedsRedraw = true;
        }
        
        // Handle snapping with reduced speed
        if (!isDragging && abs(targetScrollOffset - scrollOffset) > 0.1f) {
            float snapSpeed = 8.0f; // Reduced from 10.0f
            float delta = targetScrollOffset - scrollOffset;
            float movement = delta * snapSpeed * deltaTime;
            
            if (abs(movement) > abs(delta)) {
                movement = delta;
            }
            
            scrollOffset += movement;
            wheelNeedsRedraw = true;
            
            if (abs(targetScrollOffset - scrollOffset) < 0.1f) {
                scrollOffset = targetScrollOffset;
                scrollVelocity = 0.0f;
            }
        }
    }
    
    updateScrollPosition();
    lastAnimationTime = currentTime;
}

// Draw the wheel
void DigitWheel::draw() {
    // Only draw if wheel needs redrawing
    if (!wheelNeedsRedraw) return;
    
    TFT_eSPI* tft = ui->getDrawingTFT();
    const TFT_Theme& theme = ui->getTheme();
    
    // Use proper white background for wheels, not overlay color
    uint16_t wheelBgColor = TFT_WHITE;
    
    // Clear entire wheel area first to remove any artifacts
    tft->fillRect(wheelX, wheelY, wheelWidth, wheelHeight, ui->getDrawingColor(wheelBgColor));
    
    // Draw wheel border with rounded corners effect
    tft->drawRect(wheelX, wheelY, wheelWidth, wheelHeight, ui->getDrawingColor(theme.borderColor));
    
    // Draw selection highlight with theme-aware styling FIRST
    int centerY = wheelY + wheelHeight / 2;
    
    // Draw selection background using proper accent color
    uint16_t selectionBg = theme.primaryColor; // Always use primary color for selection
    tft->fillRect(wheelX, centerY - itemHeight/2, wheelWidth, itemHeight, ui->getDrawingColor(selectionBg));
    
    // Draw selection border
    //tft->drawRect(wheelX + 1, centerY - itemHeight/2, wheelWidth - 2, itemHeight, ui->getDrawingColor(theme.primaryColor));
    
    // Draw digit items
    drawWheelItems();
    
    // Draw top and bottom fade areas LAST so they appear over the numbers
    // But make sure they don't overlap with the selection area
    uint16_t fadeColor = theme.isDarkMode ? 0x2104 : 0xBDF7;
    
    int selectionTop = centerY - itemHeight/2;
    int selectionBottom = centerY + itemHeight/2;
    
    // Top fade area - only if it doesn't overlap with selection
    int topFadeBottom = wheelY + 13;
    if (topFadeBottom < selectionTop) {
        tft->fillRect(wheelX + 1, wheelY + 1, wheelWidth - 2, 12, ui->getDrawingColor(fadeColor));
    }
    
    // Bottom fade area - only if it doesn't overlap with selection  
    int bottomFadeTop = wheelY + wheelHeight - 13;
    if (bottomFadeTop > selectionBottom) {
        tft->fillRect(wheelX + 1, bottomFadeTop, wheelWidth - 2, 12, ui->getDrawingColor(fadeColor));
    }

    wheelNeedsRedraw = false;
}

// Handle touch input
void DigitWheel::handleTouch(TouchPoint& touch) {
    if (touch.x >= wheelX && touch.x <= wheelX + wheelWidth &&
        touch.y >= wheelY && touch.y <= wheelY + wheelHeight) {
        
        static float lastTouchY = touch.y;
        static unsigned long lastTouchTime = millis();
        static float accumulatedDelta = 0.0f;
        
        if (touch.isPressed) {
            unsigned long currentTime = millis();
            float deltaTime = (currentTime - lastTouchTime) / 1000.0f;
            
            if (!isDragging) {
                isDragging = true;
                accumulatedDelta = 0.0f;
                lastTouchY = touch.y;
                lastTouchTime = currentTime;
                
                // Reduced velocity threshold for smoother starts
                if (abs(scrollVelocity) < 40.0f) {
                    scrollVelocity = 0.0f;
                }
            } else {
                float deltaY = touch.y - lastTouchY;
                accumulatedDelta += deltaY;
                
                // Increased threshold for movement to reduce sensitivity
                if (abs(accumulatedDelta) > 0.8f) {
                    // Base sensitivity reduced by 20%
                    float sensitivity = 0.8f;
                    
                    // Adjusted threshold for speed-based sensitivity reduction
                    if (abs(deltaY / deltaTime) > 400.0f) {
                        sensitivity *= 0.5f;
                    }
                    
                    // Reduced velocity multiplier
                    scrollVelocity = -(deltaY / deltaTime) * sensitivity * 0.8f;
                    
                    float newScrollOffset = scrollOffset - (accumulatedDelta * sensitivity);
                    
                    int totalItems = maxDigit - minDigit + 1;
                    float maxScroll = (totalItems - 1) * itemHeight;
                    scrollOffset = constrain(newScrollOffset, 0.0f, maxScroll);
                    
                    if (newScrollOffset != scrollOffset) {
                        accumulatedDelta = 0.0f;
                    }
                    
                    wheelNeedsRedraw = true;
                }
                
                lastTouchY = touch.y;
                lastTouchTime = currentTime;
            }
        } else if (isDragging) {
            isDragging = false;
            
            // Adjusted velocity threshold for inertial scrolling
            if (abs(scrollVelocity) > 40.0f) {
                // Reduced maximum velocity
                scrollVelocity = constrain(scrollVelocity, -1600.0f, 1600.0f);
            } else {
                snapToNearest();
            }
        }
    } else if (isDragging) {
        isDragging = false;
        snapToNearest();
    }
}

// Set value
void DigitWheel::setValue(int value) {
    selectedValue = constrain(value, minDigit, maxDigit);
    int index = selectedValue - minDigit;
    
    // Position the scroll so the selected value is centered
    scrollOffset = (float)(index * itemHeight);
    targetScrollOffset = scrollOffset;
    
    // Reset animation state
    scrollVelocity = 0.0f;
    isDragging = false;
    wheelNeedsRedraw = true; // Value change requires redraw
    
    // Force immediate position update
    updateScrollPosition();
}

// Draw wheel items
void DigitWheel::drawWheelItems() {
    int totalItems = maxDigit - minDigit + 1;
    int startIndex = max(0, (int)(scrollOffset / (float)itemHeight) - 2);
    int endIndex = min(totalItems - 1, startIndex + (wheelHeight / itemHeight) + 4);
    
    for (int i = startIndex; i <= endIndex; i++) {
        int digit = minDigit + i;
        int itemY = wheelY + (i * itemHeight) - (int)scrollOffset + (wheelHeight / 2);
        
        // Only draw if item is visible
        if (itemY >= wheelY - itemHeight && itemY <= wheelY + wheelHeight) {
            bool isSelected = (digit == selectedValue);
            drawWheelItem(digit, itemY, isSelected);
        }
    }
}

// Draw individual wheel item
void DigitWheel::drawWheelItem(int digit, int itemY, bool isSelected) {
    TFT_eSPI* tft = ui->getDrawingTFT();
    const TFT_Theme& theme = ui->getTheme();
    
    // Skip if outside visible area
    if (itemY < wheelY - itemHeight || itemY > wheelY + wheelHeight) {
        return;
    }
    
    // Calculate fade based on distance from center
    int centerY = wheelY + wheelHeight / 2;
    int distance = abs(itemY - centerY);
    float fade = max(0.3f, 1.0f - (distance / (float)(wheelHeight / 2)));
    
    // Set text color based on selection and fade with theme awareness
    uint16_t textColor;
    if (isSelected) {
        // Use proper contrast color for selection background (theme-aware)
        textColor = getContrastColor(theme.primaryColor);
    } else {
        // Choose base color based on theme
        uint16_t baseColor = theme.isDarkMode ? theme.textColor : theme.disabledColor;
        
        // Apply fade effect with better color accuracy
        uint8_t r = (baseColor >> 11) << 3;
        uint8_t g = ((baseColor >> 5) & 0x3F) << 2;
        uint8_t b = (baseColor & 0x1F) << 3;
        
        r = r * fade;
        g = g * fade;
        b = b * fade;
        
        textColor = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
    
    // Clear the item area first to remove any artifacts
    // Use selection background color if this item is selected, otherwise use white background
    uint16_t clearColor;
    if (isSelected) {
        clearColor = theme.primaryColor;
    } else {
        // Use white background for wheel items
        clearColor = TFT_WHITE;
    }
    tft->fillRect(wheelX + 2, itemY - 12, wheelWidth - 4, 24, ui->getDrawingColor(clearColor));
    
    // Draw text with sizing based on selection state
    tft->setTextColor(ui->getDrawingColor(textColor));
    
    // Make selected text larger and bolder
    int textSize = isSelected ? 3 : 2;  // Larger text for selected item
    tft->setTextSize(textSize);
    
    // Create digit string and ensure it's clean
    String digitStr = String(digit);
    digitStr.trim(); // Remove any whitespace
    
    // Better text positioning based on text size
    int charWidth = textSize * 6;  // Each character is 6 pixels wide per size unit
    int textWidth = digitStr.length() * charWidth;
    int textX = wheelX + (wheelWidth - textWidth) / 2;
    int textY = itemY - (textSize * 4);  // Adjust Y position based on text size
    
    // Ensure text is within wheel bounds (account for different text sizes)
    int textHeight = textSize * 8;  // Each text size unit is 8 pixels tall
    if (textY >= wheelY && textY + textHeight <= wheelY + wheelHeight) {
        tft->setCursor(textX, textY);
        tft->print(digitStr);
        
        // For selected items, draw the text twice for bolder appearance
        if (isSelected) {
            tft->setCursor(textX + 1, textY);  // Offset by 1 pixel for bold effect
            tft->print(digitStr);
        }
    }
}

// Update scroll position and selected value
void DigitWheel::updateScrollPosition() {
    int totalItems = maxDigit - minDigit + 1;
    float maxScroll = (float)((totalItems - 1) * itemHeight);
    
    // Constrain scroll offset
    scrollOffset = constrain(scrollOffset, 0.0f, maxScroll);
    
    // Calculate selected index from scroll position using same rounding as snapToNearest
    int selectedIndex = (int)round(scrollOffset / (float)itemHeight); // Use round() for consistency
    selectedIndex = constrain(selectedIndex, 0, totalItems - 1);
    
    // Update selected value
    selectedValue = minDigit + selectedIndex;
}

// Snap to nearest item
void DigitWheel::snapToNearest() {
    int totalItems = maxDigit - minDigit + 1;
    float itemPosition = scrollOffset / itemHeight;
    int nearestIndex = round(itemPosition);
    
    // Constrain to valid range
    nearestIndex = constrain(nearestIndex, 0, totalItems - 1);
    
    // Set target position
    targetScrollOffset = nearestIndex * itemHeight;
    
    // Update selected value immediately
    selectedValue = minDigit + nearestIndex;
    
    wheelNeedsRedraw = true;
}

// Get digit at position
int DigitWheel::getDigitAtPosition(int y) {
    int itemIndex = (scrollOffset + y - wheelY) / itemHeight;
    int digit = minDigit + itemIndex;
    return constrain(digit, minDigit, maxDigit);
}

// Set time constraints based on position
void DigitWheel::setTimeConstraints(int position, int maxValue) {
    constraintType = CONSTRAINT_TIME;
    constraintPosition = position;
    constraintMaxValue = maxValue;
    calculateConstraintsForDigit();
}

// Set percentage constraints based on position
void DigitWheel::setPercentageConstraints(int position, int maxValue) {
    constraintType = CONSTRAINT_PERCENTAGE;
    constraintPosition = position;
    constraintMaxValue = maxValue;
    calculateConstraintsForDigit();
}

// Set custom constraints
void DigitWheel::setCustomConstraints(int minVal, int maxVal) {
    constraintType = CONSTRAINT_NONE;
    minDigit = minVal;
    maxDigit = maxVal;
    wheelNeedsRedraw = true;
}

// Calculate constraints for this digit based on its position and context
void DigitWheel::calculateConstraintsForDigit() {
    if (constraintType == CONSTRAINT_TIME) {
        // Time format: HHMMSS (235959 max)
        // Position: 0=H1, 1=H2, 2=M1, 3=M2, 4=S1, 5=S2
        
        switch (constraintPosition) {
            case 0: // First hour digit (0-2)
                minDigit = 0;
                maxDigit = 2;
                break;
            case 1: // Second hour digit (0-9 if first is 0-1, 0-3 if first is 2)
                minDigit = 0;
                maxDigit = 9; // Default, will be constrained dynamically
                break;
            case 2: // First minute digit (0-5)
            case 4: // First second digit (0-5)
                minDigit = 0;
                maxDigit = 5;
                break;
            case 3: // Second minute digit (0-9)
            case 5: // Second second digit (0-9)
                minDigit = 0;
                maxDigit = 9;
                break;
            default:
                minDigit = 0;
                maxDigit = 9;
                break;
        }
    } else if (constraintType == CONSTRAINT_PERCENTAGE) {
        // Percentage format: 0-100
        // Position: 0=hundreds, 1=tens, 2=ones
        
        switch (constraintPosition) {
            case 0: // Hundreds digit (0-1 for 0-100%)
                minDigit = 0;
                maxDigit = (constraintMaxValue >= 100) ? 1 : 0;
                break;
            case 1: // Tens digit (0-9 if hundreds is 0, 0-0 if hundreds is 1)
                minDigit = 0;
                maxDigit = 9; // Default, will be constrained dynamically
                break;
            case 2: // Ones digit (0-9 if tens+hundreds < 10, 0-0 if tens+hundreds = 10)
                minDigit = 0;
                maxDigit = 9; // Default, will be constrained dynamically
                break;
            default:
                minDigit = 0;
                maxDigit = 9;
                break;
        }
    }
    
    // Ensure selected value is within new constraints
    selectedValue = constrain(selectedValue, minDigit, maxDigit);
    wheelNeedsRedraw = true;
}

// Update constraints dynamically based on other digit values
void DigitWheel::updateDynamicConstraints(int firstHourDigit, int hundredsDigit, int tensDigit) {
    if (constraintType == CONSTRAINT_TIME && constraintPosition == 1) {
        // Second hour digit: 0-9 if first is 0-1, 0-3 if first is 2
        if (firstHourDigit == 2) {
            maxDigit = 3;
        } else {
            maxDigit = 9;
        }
        
        // Ensure current value is within new constraints
        selectedValue = constrain(selectedValue, minDigit, maxDigit);
        wheelNeedsRedraw = true;
    } else if (constraintType == CONSTRAINT_PERCENTAGE) {
        if (constraintPosition == 1) {
            // Tens digit: 0-9 if hundreds is 0, 0-0 if hundreds is 1
            if (hundredsDigit == 1) {
                maxDigit = 0;
            } else {
                maxDigit = 9;
            }
        } else if (constraintPosition == 2) {
            // Ones digit: 0-9 if hundreds+tens < 10, 0-0 if hundreds+tens = 10
            if (hundredsDigit == 1 && tensDigit == 0) {
                maxDigit = 0;
            } else {
                maxDigit = 9;
            }
        }
        
        // Ensure current value is within new constraints
        selectedValue = constrain(selectedValue, minDigit, maxDigit);
        wheelNeedsRedraw = true;
    }
}

// Check if a digit is valid in the current context
bool DigitWheel::isValidDigitInContext(int digit) {
    if (constraintType == CONSTRAINT_NONE) {
        return (digit >= minDigit && digit <= maxDigit);
    }
    
    // For time and percentage constraints, the basic range check is sufficient
    // More complex inter-digit constraints would be handled by the parent picker
    return (digit >= minDigit && digit <= maxDigit);
}

// ========================= MULTI-DIGIT WHEEL PICKER =========================

// Constructor
MultiDigitWheelPicker::MultiDigitWheelPicker(TFT_UI* ui, float minValue, float maxValue, float step, float currentValue)
    : ui(ui), visible(false), minValue(minValue), maxValue(maxValue), step(step), isTimePicker(false) {
    
    // Calculate decimal places from step and range
    if (step >= 1.0f) {
        decimalPlaces = 0;
    } else if (step >= 0.1f) {
        decimalPlaces = 1;
    } else if (step >= 0.01f) {
        decimalPlaces = 2;
    } else {
        decimalPlaces = 3;
    }
    
    // Detect if this is a time picker (HHMM format: 0-2359)
    isTimePicker = (minValue == 0.0f && maxValue == 2359.0f && step == 1.0f && decimalPlaces == 0);
    
    // Calculate required integer digits based on max value
    int maxIntegerDigits = 1;
    if (maxValue >= 10.0f) maxIntegerDigits = 2;
    if (maxValue >= 100.0f) maxIntegerDigits = 3;
    if (maxValue >= 1000.0f) maxIntegerDigits = 4;
    if (maxValue >= 10000.0f) maxIntegerDigits = 5;
    if (maxValue >= 100000.0f) maxIntegerDigits = 6;
    
    // Calculate adaptive wheel sizing based on wheel count
    int actualWheelCount = maxIntegerDigits + decimalPlaces;
    int decimalPointWidth = (decimalPlaces > 0) ? 12 : 0;
    
    // Adaptive sizing to fit screen width (320px)
    int availableWidth = TFT_WIDTH - 40; // Leave 20px margin on each side
    int reservedWidth = decimalPointWidth;
    int wheelAreaWidth = availableWidth - reservedWidth;
    
    // Calculate wheel width and spacing adaptively
    int wheelWidth, wheelSpacing;
    if (actualWheelCount <= 3) {
        // Fewer wheels: use generous sizing
        wheelWidth = 70;
        wheelSpacing = 8;
    } else if (actualWheelCount <= 4) {
        // Medium wheel count: balanced sizing
        wheelWidth = 60;
        wheelSpacing = 6;
    } else if (actualWheelCount <= 5) {
        // More wheels: compact sizing
        wheelWidth = 50;
        wheelSpacing = 4;
    } else {
        // 6+ wheels: very compact to fit screen
        wheelWidth = 45;
        wheelSpacing = 2;
    }
    
    // Verify we fit in available space, adjust if needed
    int requiredWidth = actualWheelCount * wheelWidth + (actualWheelCount - 1) * wheelSpacing;
    if (requiredWidth > wheelAreaWidth) {
        // Still too wide, make it even more compact
        wheelWidth = wheelAreaWidth / actualWheelCount - 2;
        wheelSpacing = 1;
        if (wheelWidth < 30) wheelWidth = 30; // Minimum readable width
    }
    
    // Calculate final picker dimensions - extend to bottom of screen
    int contentWidth = actualWheelCount * wheelWidth + (actualWheelCount - 1) * wheelSpacing + decimalPointWidth;
    pickerWidth = TFT_WIDTH - 10; // Use more screen width but not full width to avoid exact edge alignment
    
    // Calculate picker height to extend to bottom of screen
    int availableHeight = TFT_HEIGHT - 20 - 30; // Total height minus status bar (20px) and nav bar (30px)
    pickerHeight = TFT_HEIGHT - 20 - 5; // Extend to near bottom, leave small margin above nav bar
    
    pickerX = 5; // Smaller margin from screen edge
    
    // Position picker just below status bar
    pickerY = 20; // Start right below status bar
    
    // No need for centering or max position checks since we're extending to bottom
    
    // Store adaptive sizing for use in createDigitWheels
    adaptiveWheelWidth = wheelWidth;
    adaptiveWheelSpacing = wheelSpacing;
    
    // Initialize digit wheels
    for (int i = 0; i < 6; i++) {
        digitWheels[i] = nullptr;
    }
    
    // Create digit wheels first
    createDigitWheels();
    
    // Set initial value after wheels are created
    setValue(currentValue);
    
    // Force update to ensure proper initial positioning
    for (int i = 0; i < wheelCount; i++) {
        if (digitWheels[i]) {
            digitWheels[i]->update();
        }
    }
    
    // Callbacks
    onSelectedCallback = nullptr;
    onCancelCallback = nullptr;
}

// Destructor
MultiDigitWheelPicker::~MultiDigitWheelPicker() {
    for (int i = 0; i < wheelCount; i++) {
        if (digitWheels[i]) {
            delete digitWheels[i];
            digitWheels[i] = nullptr;
        }
    }
}

// Create digit wheels
void MultiDigitWheelPicker::createDigitWheels() {
    // Calculate required integer digits based on max value
    int maxIntegerDigits = 1;
    if (maxValue >= 10.0f) maxIntegerDigits = 2;
    if (maxValue >= 100.0f) maxIntegerDigits = 3;
    if (maxValue >= 1000.0f) maxIntegerDigits = 4;
    if (maxValue >= 10000.0f) maxIntegerDigits = 5;
    if (maxValue >= 100000.0f) maxIntegerDigits = 6;
    
    // Special handling for time values (HHMMSS format)
    bool isTimeValue = (maxValue == 235959.0f && step == 1.0f && decimalPlaces == 0);
    
    // Special handling for percentage values (0-100)
    bool isPercentageValue = (maxValue == 100.0f && step == 1.0f && decimalPlaces == 0);
    
    // Use adaptive sizing calculated in constructor
    int wheelWidth = adaptiveWheelWidth;
    int wheelHeight = min(160, pickerHeight - 80); // Larger wheel height with more space available, leave room for buttons
    int spacing = adaptiveWheelSpacing;
    
    wheelCount = maxIntegerDigits + decimalPlaces;
    decimalWheelIndex = maxIntegerDigits;
    
    // Calculate starting position using adaptive sizing
    int totalWidth = wheelCount * wheelWidth + (wheelCount - 1) * spacing;
    if (decimalPlaces > 0) {
        totalWidth += 12; // Extra space for decimal point
    }
    
    int startX = pickerX + (pickerWidth - totalWidth) / 2;
    int wheelY = pickerY + 20; // Start higher since no title, just small margin from picker top
    
    // Create integer digit wheels (left to right: hundreds, tens, ones)
    // digitWheels[0] = most significant digit (leftmost)
    for (int i = 0; i < maxIntegerDigits; i++) {
        int x = startX + i * (wheelWidth + spacing);
        digitWheels[i] = new DigitWheel(ui, x, wheelY, wheelWidth, wheelHeight, 0, 9);
        
        // Apply constraints based on context
        if (isTimeValue) {
            digitWheels[i]->setTimeConstraints(i, (int)maxValue);
        } else if (isPercentageValue) {
            digitWheels[i]->setPercentageConstraints(i, (int)maxValue);
        }
    }
    
    // Create decimal digit wheels (left to right after decimal point)
    for (int i = 0; i < decimalPlaces; i++) {
        int x = startX + maxIntegerDigits * (wheelWidth + spacing) + 12 + i * (wheelWidth + spacing);
        digitWheels[maxIntegerDigits + i] = new DigitWheel(ui, x, wheelY, wheelWidth, wheelHeight, 0, 9);
        
        // Decimal places don't need special constraints for time/percentage
    }
}

// Show the picker
void MultiDigitWheelPicker::show() {
    if (visible) {
        return;
    }
    
    visible = true;
    pickerNeedsRedraw = true;
    
    // Save the current screen content below the status bar
    TFT_eSPI* tft = ui->getDrawingTFT();
    const TFT_Theme& theme = ui->getTheme();
    
    // Fill the entire screen with semi-transparent overlay to completely hide background
    uint16_t overlayColor = theme.isDarkMode ? 0x2104 : 0x9CE7;
    tft->fillRect(0, 0, TFT_WIDTH, TFT_HEIGHT, ui->getDrawingColor(overlayColor));
    
    // Redraw status bar on top of overlay
    ui->drawStatusBar();
    
    // Draw picker immediately to prevent flicker
    drawBackground();
    
    // Force all wheels to redraw
    for (int i = 0; i < wheelCount; i++) {
        if (digitWheels[i]) {
            digitWheels[i]->forceRedraw();
            digitWheels[i]->draw();
        }
    }
}

// Hide the picker
void MultiDigitWheelPicker::hide() {
    if (!visible) {
        return;
    }
    
    visible = false;
    pickerNeedsRedraw = false;
    
    // Clear all wheel redraw flags
    for (int i = 0; i < wheelCount; i++) {
        if (digitWheels[i]) {
            digitWheels[i]->wheelNeedsRedraw = false;
        }
    }

    if (ui) {
        // Clear everything below the status bar
        TFT_eSPI* tft = ui->getDrawingTFT();
        const TFT_Theme& theme = ui->getTheme();
        
        // Clear the main content area
        tft->fillRect(0, 20, TFT_WIDTH, TFT_HEIGHT - 50, ui->getDrawingColor(theme.backgroundColor));
        
        // Clear the navigation bar area separately
        tft->fillRect(0, TFT_HEIGHT - 30, TFT_WIDTH, 30, ui->getDrawingColor(theme.backgroundColor));
        
        // Force a complete screen redraw
        ui->forceRedraw();
    }
}

// Update animation and state
void MultiDigitWheelPicker::update() {
    if (!visible) return;
    
    // Update dynamic constraints based on current wheel values
    updateDynamicConstraints();
    
    for (int i = 0; i < wheelCount; i++) {
        if (digitWheels[i]) {
            digitWheels[i]->update();
        }
    }
    
    // Don't trigger full screen redraws for wheel animations
    // Each wheel will handle its own redrawing
}

// Draw the picker
void MultiDigitWheelPicker::draw() {
    if (!visible || !pickerNeedsRedraw) return;
    
    TFT_eSPI& tft = ui->getTFT();
    const TFT_Theme& theme = ui->getTheme();
    
    // Draw full screen semi-transparent overlay to completely hide everything underneath
    uint16_t overlayColor = theme.isDarkMode ? 0x2104 : 0x9CE7;
    
    // Fill entire screen including status bar to ensure no gaps
    tft.fillRect(0, 0, TFT_WIDTH, TFT_HEIGHT, ui->getDrawingColor(overlayColor));
    
    // Redraw status bar on top of overlay to keep it visible
    ui->drawStatusBar();
    
    // Draw picker background with shadow effect - now using full width
    // Draw shadow first (offset by 2px)
    tft.fillRoundRect(pickerX + 2, pickerY + 2, pickerWidth, pickerHeight, 10, 
                      ui->getDrawingColor(theme.isDarkMode ? 0x0000 : 0x4208));
    
    // Draw main picker background - use theme-appropriate background color
    uint16_t pickerBgColor = theme.isDarkMode ? theme.cardBackground : TFT_WHITE;
    tft.fillRoundRect(pickerX, pickerY, pickerWidth, pickerHeight, 10, ui->getDrawingColor(pickerBgColor));
    tft.drawRoundRect(pickerX, pickerY, pickerWidth, pickerHeight, 10, ui->getDrawingColor(theme.borderColor));
    
    // Fill the entire picker interior with white background
    tft.fillRect(pickerX + 1, pickerY + 1, pickerWidth - 2, pickerHeight - 2, ui->getDrawingColor(pickerBgColor));
    
    // No title - removed as requested by user
    
    // Draw wheels
    for (int i = 0; i < wheelCount; i++) {
        if (digitWheels[i]) {
            digitWheels[i]->draw();
        }
    }
    
    // Draw decimal point if needed
    if (decimalPlaces > 0) {
        drawDecimalPoint();
    }
    
    // Draw buttons
    drawButtons();

    pickerNeedsRedraw = false;
}

// Handle touch input
void MultiDigitWheelPicker::handleTouch(TouchPoint& touch) {
    if (!visible) return;
    
    // Check if touch is in button area first - prevent wheel interaction
    // Calculate button position based on wheel layout
    int wheelHeight = min(160, pickerHeight - 80); // Use same calculation as createDigitWheels
    int wheelBottom = pickerY + 20 + wheelHeight; // Bottom of wheels
    int buttonY = wheelBottom + 15; // 15px gap between wheels and buttons
    
    if (touch.y >= buttonY && touch.y <= buttonY + 35) {
        handleButtonTouch(touch);
        return; // Don't process wheel touches when in button area
    }
    
    // Check digit wheel touches only if not in button area
    // Use actual wheel positions instead of hardcoded values
    for (int i = 0; i < wheelCount; i++) {
        if (digitWheels[i]) {
            // Check if touch is within this wheel's bounds
            if (touch.x >= digitWheels[i]->wheelX && 
                touch.x <= digitWheels[i]->wheelX + digitWheels[i]->wheelWidth &&
                touch.y >= digitWheels[i]->wheelY && 
                touch.y <= digitWheels[i]->wheelY + digitWheels[i]->wheelHeight) {
                digitWheels[i]->handleTouch(touch);
                break; // Only handle one wheel at a time
            }
        }
    }
    // Don't trigger full screen redraw for wheel interactions
}

// Get current value
float MultiDigitWheelPicker::getValue() const {
    int digits[6] = {0, 0, 0, 0, 0, 0};
    
    for (int i = 0; i < wheelCount; i++) {
        if (digitWheels[i]) {
            digits[i] = digitWheels[i]->getValue();
        }
    }
    
    return combineDigits(digits);
}

// Set value
void MultiDigitWheelPicker::setValue(float value) {
    int digits[6] = {0, 0, 0, 0, 0, 0};
    parseValue(value, digits);
    
    for (int i = 0; i < wheelCount; i++) {
        if (digitWheels[i]) {
            digitWheels[i]->setValue(digits[i]);
        }
    }
    
    // Don't trigger full picker redraw for setValue - individual wheels handle their own redraw
}

// Set callbacks
void MultiDigitWheelPicker::onValueSelected(void (*callback)(float)) {
    onSelectedCallback = callback;
}

void MultiDigitWheelPicker::onCancelled(void (*callback)()) {
    onCancelCallback = callback;
}

// Draw background
void MultiDigitWheelPicker::drawBackground() {
    if (!visible) return;
    
    TFT_eSPI* tft = ui->getDrawingTFT();
    const TFT_Theme& theme = ui->getTheme();
    
    // Draw full screen semi-transparent overlay to completely hide background
    uint16_t overlayColor = theme.isDarkMode ? 0x2104 : 0x9CE7;
    tft->fillRect(0, 0, TFT_WIDTH, TFT_HEIGHT, ui->getDrawingColor(overlayColor));
    
    // Redraw status bar on top of overlay
    ui->drawStatusBar();
    
    // Draw picker background with shadow effect
    tft->fillRoundRect(pickerX + 2, pickerY + 2, pickerWidth, pickerHeight, 10, 
                      ui->getDrawingColor(theme.isDarkMode ? 0x0000 : 0x4208));
    
    // Use theme-appropriate background color instead of hardcoded white
    uint16_t pickerBgColor = theme.isDarkMode ? theme.cardBackground : TFT_WHITE;
    tft->fillRoundRect(pickerX, pickerY, pickerWidth, pickerHeight, 10, ui->getDrawingColor(pickerBgColor));
    tft->drawRoundRect(pickerX, pickerY, pickerWidth, pickerHeight, 10, ui->getDrawingColor(theme.borderColor));
    
    // Fill the entire picker interior with white background
    tft->fillRect(pickerX + 1, pickerY + 1, pickerWidth - 2, pickerHeight - 2, ui->getDrawingColor(pickerBgColor));
    
    // No title - removed as requested by user
    
    // Draw wheels
    for (int i = 0; i < wheelCount; i++) {
        if (digitWheels[i]) {
            digitWheels[i]->draw();
        }
    }
    
    // Draw decimal point if needed
    if (decimalPlaces > 0) {
        drawDecimalPoint();
    }
    
    // Draw buttons
    drawButtons();
}

// Draw buttons
void MultiDigitWheelPicker::drawButtons() {
    TFT_eSPI* tft = ui->getDrawingTFT();
    const TFT_Theme& theme = ui->getTheme();
    
    // Calculate button position based on wheel layout
    int wheelHeight = min(160, pickerHeight - 80); // Use same calculation as createDigitWheels
    int wheelBottom = pickerY + 20 + wheelHeight; // Bottom of wheels
    int buttonY = wheelBottom + 15; // 15px gap between wheels and buttons
    
    int buttonWidth = 80;
    int buttonHeight = 35;
    int buttonSpacing = 20;
    
    // Calculate button positions
    int totalButtonWidth = buttonWidth * 2 + buttonSpacing;
    int startX = pickerX + (pickerWidth - totalButtonWidth) / 2;
    
    // Draw Select button
    tft->fillRoundRect(startX, buttonY, buttonWidth, buttonHeight, 5, ui->getDrawingColor(theme.primaryColor));
    tft->drawRoundRect(startX, buttonY, buttonWidth, buttonHeight, 5, ui->getDrawingColor(theme.borderColor));
    tft->setTextColor(ui->getDrawingColor(theme.textColor));
    tft->setTextSize(1);
    tft->setCursor(startX + (buttonWidth - 30) / 2, buttonY + (buttonHeight - 8) / 2);
    tft->print("Select");
    
    // Draw Cancel button
    int cancelX = startX + buttonWidth + buttonSpacing;
    tft->fillRoundRect(cancelX, buttonY, buttonWidth, buttonHeight, 5, ui->getDrawingColor(theme.errorColor));
    tft->drawRoundRect(cancelX, buttonY, buttonWidth, buttonHeight, 5, ui->getDrawingColor(theme.borderColor));
    tft->setTextColor(ui->getDrawingColor(theme.textColor));
    tft->setCursor(cancelX + (buttonWidth - 30) / 2, buttonY + (buttonHeight - 8) / 2);
    tft->print("Cancel");
}

// Draw decimal point
void MultiDigitWheelPicker::drawDecimalPoint() {
    TFT_eSPI* tft = ui->getDrawingTFT();
    const TFT_Theme& theme = ui->getTheme();
    
    // Use adaptive sizing values
    int wheelWidth = adaptiveWheelWidth;
    int spacing = adaptiveWheelSpacing;
    
    // Calculate required integer digits dynamically
    int maxIntegerDigits = 1;
    if (maxValue >= 10.0f) maxIntegerDigits = 2;
    if (maxValue >= 100.0f) maxIntegerDigits = 3;
    if (maxValue >= 1000.0f) maxIntegerDigits = 4;
    
    int totalWidth = wheelCount * wheelWidth + (wheelCount - 1) * spacing + 12;
    int startX = pickerX + (pickerWidth - totalWidth) / 2;
    
    int dotX = startX + maxIntegerDigits * (wheelWidth + spacing) + 4;
    int dotY = pickerY + 20 + 80; // Center of typical wheel height (160/2 = 80), adjusted for no title
    
    // Draw decimal point with theme-aware color and subtle shadow
    if (!theme.isDarkMode) {
        // Draw shadow for light mode
        tft->fillCircle(dotX + 1, dotY + 1, 4, ui->getDrawingColor(0x4208)); // Slightly larger
    }
    
    // Draw main decimal point
    uint16_t dotColor = theme.isDarkMode ? theme.textColor : theme.primaryColor;
    tft->fillCircle(dotX, dotY, 4, ui->getDrawingColor(dotColor));
    
    // Add subtle border for better visibility
    tft->drawCircle(dotX, dotY, 4, ui->getDrawingColor(theme.borderColor)); // Slightly larger
}

// Handle button touch
void MultiDigitWheelPicker::handleButtonTouch(TouchPoint& touch) {
    if (!touch.isPressed) return;
    
    
    
    // Calculate button position based on wheel layout - same as drawButtons
    int wheelHeight = min(160, pickerHeight - 80); // Use same calculation as createDigitWheels
    int wheelBottom = pickerY + 20 + wheelHeight; // Bottom of wheels
    int buttonY = wheelBottom + 15; // 15px gap between wheels and buttons
    
    int buttonWidth = 80;
    int buttonHeight = 35;
    int buttonSpacing = 20;
    
    int totalButtonWidth = buttonWidth * 2 + buttonSpacing;
    int startX = pickerX + (pickerWidth - totalButtonWidth) / 2;
    
    // Check Select button
    if (touch.x >= startX && touch.x <= startX + buttonWidth &&
        touch.y >= buttonY && touch.y <= buttonY + buttonHeight) {
        
        if (onSelectedCallback) {
            float value = getValue();
            onSelectedCallback(value);
        }
        hide();
    }
    
    // Check Cancel button
    int cancelX = startX + buttonWidth + buttonSpacing;
    if (touch.x >= cancelX && touch.x <= cancelX + buttonWidth &&
        touch.y >= buttonY && touch.y <= buttonY + buttonHeight) {
        

        if (onCancelCallback) {
            onCancelCallback();
        }
        hide();
    }
}

// Parse value into digits
void MultiDigitWheelPicker::parseValue(float value, int* digits) {
    // Initialize all digits to 0
    for (int i = 0; i < 6; i++) {
        digits[i] = 0;
    }
    
    // Constrain value to valid range
    value = constrain(value, minValue, maxValue);
    
    // Convert to fixed-point integer for processing
    int intValue = (int)(value * pow(10, decimalPlaces) + 0.5f);
    
    // Extract digits from right to left (least significant to most significant)
    // digits[0] = most significant integer digit
    // digits[wheelCount-1] = least significant decimal digit
    for (int i = wheelCount - 1; i >= 0; i--) {
        digits[i] = intValue % 10;
        intValue /= 10;
    }
}

// Combine digits into value
float MultiDigitWheelPicker::combineDigits(int* digits) const {
    float value = 0;
    
    // Calculate integer digits dynamically
    int maxIntegerDigits = 1;
    if (maxValue >= 10.0f) maxIntegerDigits = 2;
    if (maxValue >= 100.0f) maxIntegerDigits = 3;
    if (maxValue >= 1000.0f) maxIntegerDigits = 4;
    if (maxValue >= 10000.0f) maxIntegerDigits = 5;
    if (maxValue >= 100000.0f) maxIntegerDigits = 6;
    
    // Start with the highest place value for integer digits
    float multiplier = pow(10, maxIntegerDigits - 1);
    
    // Process integer digits (left to right, most significant to least significant)
    for (int i = 0; i < maxIntegerDigits; i++) {
        value += digits[i] * multiplier;
        multiplier /= 10;
    }
    
    // Process decimal digits (left to right after decimal point)
    for (int i = maxIntegerDigits; i < wheelCount; i++) {
        value += digits[i] * multiplier;
        multiplier /= 10;
    }
    
    return constrain(value, minValue, maxValue);
}

// Check if picker needs redrawing
bool MultiDigitWheelPicker::needsRedraw() const {
    return pickerNeedsRedraw;
}

// Force redraw of picker
void MultiDigitWheelPicker::forceRedraw() {
    if (visible) {
        pickerNeedsRedraw = true;
        
        // Also force redraw of all digit wheels
        for (int i = 0; i < wheelCount; i++) {
            if (digitWheels[i]) {
                digitWheels[i]->forceRedraw();
            }
        }
    }
}

// Check if picker is both visible and properly drawn
bool MultiDigitWheelPicker::isActiveAndVisible() const {
    return visible && pickerNeedsRedraw;
}

// Validate and fix picker state if out of sync
void MultiDigitWheelPicker::validateState() {
    if (visible && !pickerNeedsRedraw) {
        // Picker is supposed to be visible but not scheduled for redraw
        // This indicates the state is out of sync - force redraw
        forceRedraw();
    }
}

// Draw only static elements (background, buttons, decimal point) without wheels
void MultiDigitWheelPicker::drawStaticElements() {
    if (!visible) {
        return;
    }
    
    // Get TFT and theme references
    TFT_eSPI* tft = ui->getDrawingTFT();
    const TFT_Theme& theme = ui->getTheme();
    
    // Draw the full picker if it needs redrawing
    if (pickerNeedsRedraw) {
        drawBackground();
        
        // Draw decimal point if needed
        if (decimalPlaces > 0) {
            drawDecimalPoint();
        }
        
        drawButtons();
        
        // Also force draw all wheels when drawing static elements for first time
        // This ensures wheels are visible immediately when picker is shown
        for (int i = 0; i < wheelCount; i++) {
            if (digitWheels[i]) {
                digitWheels[i]->forceRedraw();
                digitWheels[i]->draw();
            }
        }
        
        // Draw colon separator for time format (HH:MM)
        if (isTimePicker) {  // Only for time pickers (HH:MM format)
            // Calculate position between 2nd and 3rd wheel (between hours and minutes)
            if (digitWheels[1] && digitWheels[2]) {
                int colonX = digitWheels[1]->wheelX + digitWheels[1]->wheelWidth + 
                            (digitWheels[2]->wheelX - (digitWheels[1]->wheelX + digitWheels[1]->wheelWidth)) / 2;
                int colonY = digitWheels[1]->wheelY + digitWheels[1]->wheelHeight / 2;
                
                tft->setTextColor(theme.textColor);
                tft->setTextSize(3);
                tft->setCursor(colonX - 6, colonY - 12); // Center the colon
                tft->print(":");
            }
        }
        
        // Clear redraw flag after drawing
        pickerNeedsRedraw = false;
    }
}

// ========================= END MULTI-DIGIT WHEEL PICKER =========================

// ========================= DOUBLE BUFFERING REMOVED =========================
// Double buffering was removed due to insufficient RAM on ESP32
// Using selective redraw and small region buffering instead for flicker reduction
// ========================= END REMOVED SECTION =========================

// ========================= SMALL REGION BUFFERING =========================

// Initialize small region buffers for frequently changing elements
void TFT_UI::initSmallBuffers() {
    if (!smallBuffersEnabled) return;
    
    // Clean up existing buffers
    cleanupSmallBuffers();
    
    // Create status bar buffer (320x20)
    statusBarBuffer = new TFT_eSprite(&tft);
    if (!statusBarBuffer->createSprite(TFT_WIDTH, 20)) {
        delete statusBarBuffer;
        statusBarBuffer = nullptr;
    }
    
    // Create navigation bar buffer (320x30)
    navBarBuffer = new TFT_eSprite(&tft);
    if (!navBarBuffer->createSprite(TFT_WIDTH, 30)) {
        delete navBarBuffer;
        navBarBuffer = nullptr;
    }
    
    // Create temperature display buffer (100x40)
    tempDisplayBuffer = new TFT_eSprite(&tft);
    if (!tempDisplayBuffer->createSprite(100, 40)) {
        delete tempDisplayBuffer;
        tempDisplayBuffer = nullptr;
    }
}

// Clean up small region buffers
void TFT_UI::cleanupSmallBuffers() {
    if (statusBarBuffer) {
        statusBarBuffer->deleteSprite();
        delete statusBarBuffer;
        statusBarBuffer = nullptr;
    }
    
    if (navBarBuffer) {
        navBarBuffer->deleteSprite();
        delete navBarBuffer;
        navBarBuffer = nullptr;
    }
    
    if (tempDisplayBuffer) {
        tempDisplayBuffer->deleteSprite();
        delete tempDisplayBuffer;
        tempDisplayBuffer = nullptr;
    }
}

// Draw buffered status bar
void TFT_UI::drawBufferedStatusBar() {
    if (!statusBarBuffer) {
        // Fallback to direct drawing
        drawStatusBar();
        return;
    }
    
    // Track what was last displayed for anti-flashing (V20 technique)
    static String lastWiFiStatus = "";
    static String lastTimeStr = "";
    static String lastAPStatus = "";
    
    // Get current values
    String wifiStatus;
    if (WiFi.status() == WL_CONNECTED) {
        wifiStatus = WiFi.localIP().toString();
    } else {
        wifiStatus = "No WiFi";
    }
    
    String apStatus = "";
    if (ap_active && ap_password.length() > 0) {
        apStatus = "AP: " + ap_password;
    }
    
    String timeStr = getCurrentTime();
    
    // Check what needs updating
    bool wifiChanged = (wifiStatus != lastWiFiStatus);
    bool apChanged = (apStatus != lastAPStatus);
    bool timeChanged = (timeStr != lastTimeStr);
    
    // Only redraw if something changed
    if (wifiChanged || apChanged || timeChanged) {
        
        // Draw to buffer
        statusBarBuffer->fillSprite(theme.cardBackground);
        statusBarBuffer->drawLine(0, 19, TFT_WIDTH, 19, theme.borderColor);
        
        // Draw WiFi IP address or status
        statusBarBuffer->setTextColor(theme.textColor);
        statusBarBuffer->setTextSize(1);
        statusBarBuffer->setCursor(5, 6);
        if (WiFi.status() == WL_CONNECTED) {
            statusBarBuffer->setTextColor(theme.successColor);
            statusBarBuffer->print(wifiStatus);
        } else {
            statusBarBuffer->setTextColor(theme.errorColor);
            statusBarBuffer->print(wifiStatus);
        }
        
        // Draw AP password in the middle if AP is active
        if (ap_active && ap_password.length() > 0) {
            statusBarBuffer->setTextColor(theme.textColor);
            statusBarBuffer->setCursor(100, 6);
            statusBarBuffer->print("AP: ");
            statusBarBuffer->setTextColor(theme.successColor); // Use success color for better visibility
            statusBarBuffer->print(ap_password);
        }
        
        // Draw current time
        statusBarBuffer->setTextColor(theme.textColor);
        
        // Adjust position based on time string content
        int timeX = 270;  // Default position
        if (timeStr.indexOf("(M)") != -1) {
            // Manual time: "HH:MM:SS (M)" (12 chars) = 72 pixels
            timeX = 246;  // Move left by 24 pixels for manual time indicator
        } else if (timeStr.indexOf("not synced") != -1 || timeStr.indexOf("error") != -1) {
            // Time not synced: "Time not synced" (14 chars) = 84 pixels
            timeX = 220;  // Move left by 50 pixels to fit
        }
        
        statusBarBuffer->setCursor(timeX, 6);
        statusBarBuffer->print(timeStr);
        
        // Push buffer to screen
        statusBarBuffer->pushSprite(0, 0);
        
        // Update last displayed values
        lastWiFiStatus = wifiStatus;
        lastAPStatus = apStatus;
        lastTimeStr = timeStr;
    }
}

// Draw buffered navigation bar
void TFT_UI::drawBufferedNavBar() {
    if (!navBarBuffer) {
        // Fallback to direct drawing
        drawNavigationBar();
        return;
    }
    
    // Clear buffer
    navBarBuffer->fillSprite(theme.cardBackground);
    
    // Draw navigation bar content to buffer
    navBarBuffer->drawLine(0, 0, TFT_WIDTH, 0, theme.borderColor);
    
    // Draw navigation buttons with 4-button layout
    int buttonWidthEach = 80; // Equal width for all 4 buttons
    int buttonHeight = 25;
    int buttonY = 2;
    
    // Main button
    navBarBuffer->fillRect(0, buttonY, buttonWidthEach, buttonHeight, 
                          currentScreen == SCREEN_MAIN ? theme.primaryColor : theme.cardBackground);
    navBarBuffer->drawRect(0, buttonY, buttonWidthEach, buttonHeight, theme.borderColor);
    navBarBuffer->setTextColor(currentScreen == SCREEN_MAIN ? theme.backgroundColor : theme.textColor);
    navBarBuffer->setTextSize(1);
    navBarBuffer->setCursor((buttonWidthEach - 24) / 2, buttonY + 9);
    navBarBuffer->print("Main");
    
    // Settings button
    int settingsX = buttonWidthEach;
    navBarBuffer->fillRect(settingsX, buttonY, buttonWidthEach, buttonHeight,
                          currentScreen == SCREEN_SETTINGS ? theme.primaryColor : theme.cardBackground);
    navBarBuffer->drawRect(settingsX, buttonY, buttonWidthEach, buttonHeight, theme.borderColor);
    navBarBuffer->setTextColor(currentScreen == SCREEN_SETTINGS ? theme.backgroundColor : theme.textColor);
    navBarBuffer->setCursor(settingsX + (buttonWidthEach - 48) / 2, buttonY + 9);
    navBarBuffer->print("Settings");
    
    // Programs button
    int programsX = buttonWidthEach * 2;
    navBarBuffer->fillRect(programsX, buttonY, buttonWidthEach, buttonHeight,
                          currentScreen == SCREEN_PROGRAMS ? theme.primaryColor : theme.cardBackground);
    navBarBuffer->drawRect(programsX, buttonY, buttonWidthEach, buttonHeight, theme.borderColor);
    navBarBuffer->setTextColor(currentScreen == SCREEN_PROGRAMS ? theme.backgroundColor : theme.textColor);
    navBarBuffer->setCursor(programsX + (buttonWidthEach - 48) / 2, buttonY + 9);
    navBarBuffer->print("Programs");
    
    // Charts button
    int chartsX = buttonWidthEach * 3;
    navBarBuffer->fillRect(chartsX, buttonY, buttonWidthEach, buttonHeight,
                          currentScreen == SCREEN_CHARTS ? theme.primaryColor : theme.cardBackground);
    navBarBuffer->drawRect(chartsX, buttonY, buttonWidthEach, buttonHeight, theme.borderColor);
    navBarBuffer->setTextColor(currentScreen == SCREEN_CHARTS ? theme.backgroundColor : theme.textColor);
    navBarBuffer->setCursor(chartsX + (buttonWidthEach - 36) / 2, buttonY + 9);
    navBarBuffer->print("Charts");
    
    // Push buffer to screen
    navBarBuffer->pushSprite(0, TFT_HEIGHT - 30);
}

// Draw buffered temperature display
void TFT_UI::drawBufferedTempDisplay(int x, int y, int width, int height, const String& text) {
    if (!tempDisplayBuffer) {
        // Fallback to direct drawing
        tft.fillRect(x, y, width, height, theme.cardBackground);
        tft.setTextColor(theme.textColor);
        tft.setTextSize(2);
        tft.setCursor(x + 5, y + 5);
        tft.print(text);
        return;
    }
    
    // Only proceed if buffer is large enough
    if (width > 100 || height > 40) {
        // For large areas, use direct drawing
        tft.fillRect(x, y, width, height, theme.cardBackground);
        tft.setTextColor(theme.textColor);
        tft.setTextSize(2);
        tft.setCursor(x + 5, y + 5);
        tft.print(text);
        return;
    }
    
    // Draw to buffer
    tempDisplayBuffer->fillSprite(theme.cardBackground);
    tempDisplayBuffer->setTextColor(theme.textColor);
    tempDisplayBuffer->setTextSize(2);
    tempDisplayBuffer->setCursor(5, 5);
    tempDisplayBuffer->print(text);
    
    // Push buffer to screen
    tempDisplayBuffer->pushSprite(x, y);
}

// ========================= END SMALL REGION BUFFERING =========================

// New selective screen drawing method
void TFT_UI::drawSelectiveScreen() {
    // Always draw status bar first
    drawStatusBar();
    
    // Draw the current screen content
    if (screens[currentScreen]) {
        screens[currentScreen]->draw();
    }
    
    // Draw navigation bar last unless there's an active modal
    if (!hasActiveModal()) {
        drawNavigationBar();
    }
}

// Message display methods removed - no popup notifications 

// ========================= ANTI-FLICKERING UTILITIES =========================

// Text change detection to avoid unnecessary redraws
bool TFT_UI::hasTextChanged(const String& newText, const String& oldText) {
    return newText != oldText;
}

// Optimized text drawing that only redraws if text has changed
void TFT_UI::drawOptimizedText(int x, int y, const String& newText, String& oldText, 
                              uint16_t color, uint8_t size, bool clearBackground) {
    if (hasTextChanged(newText, oldText)) {
        if (clearBackground) {
            // Clear previous text area
            int textWidth = oldText.length() * 6 * size;
            int textHeight = 8 * size;
            tft.fillRect(x, y, textWidth, textHeight, theme.backgroundColor);
        }
        
        // Draw new text
        tft.setTextColor(color);
        tft.setTextSize(size);
        tft.setCursor(x, y);
        tft.print(newText);
        
        // Update stored text
        oldText = newText;
    }
}

// Optimized button drawing that only redraws if state changed
void TFT_UI::drawOptimizedButton(TFT_Button& button, ButtonState previousState) {
    if (button.state != previousState || button.state == BTN_PRESSED) {
        drawButton(button);
    }
}

// Reduced flicker card drawing using outline-first method
void TFT_UI::drawCardOptimized(int x, int y, int width, int height, const String& title, 
                              bool forceRedraw) {
    if (!forceRedraw) return;
    
    // Draw outline first to establish boundaries
    tft.drawRoundRect(x, y, width, height, 8, theme.borderColor);
    
    // Fill interior (slightly smaller to avoid overdrawing border)
    tft.fillRoundRect(x + 1, y + 1, width - 2, height - 2, 7, theme.cardBackground);
    
    // Draw title if provided
    if (title.length() > 0) {
        tft.setTextColor(theme.textColor);
        tft.setTextSize(1);
        tft.setCursor(x + 8, y + 8);
        tft.print(title);
        
        // Draw title underline
        tft.drawLine(x + 8, y + 20, x + width - 8, y + 20, theme.borderColor);
    }
}

// ========================= END ANTI-FLICKERING UTILITIES ========================= 

// ========================= ADAPTIVE UPDATE FREQUENCY =========================

// Adjust update frequency based on activity
void TFT_UI::adjustUpdateFrequency() {
    unsigned long currentTime = millis();
    
    // Check if there's been recent touch activity
    bool recentActivity = (currentTime - lastTouchTime < 2000);
    
    // Check if any animations are actually running
    bool animationsRunning = false;
    if (currentScreen == SCREEN_SETTINGS) {
        SettingsScreen* settingsScreen = static_cast<SettingsScreen*>(screens[SCREEN_SETTINGS]);
        if (settingsScreen && settingsScreen->hasActiveAnimations()) {
            animationsRunning = true;
        }
    } else if (currentScreen == SCREEN_PROGRAMS) {
        ProgramsScreen* programsScreen = static_cast<ProgramsScreen*>(screens[SCREEN_PROGRAMS]);
        if (programsScreen && programsScreen->hasActiveAnimations()) {
            animationsRunning = true;
        }
    }
    
    // Adjust update interval based on activity
    if (recentActivity || animationsRunning) {
        updateInterval = 50; // 20 FPS for smooth interaction
    } else {
        updateInterval = 100; // 10 FPS for idle state
    }
}

// Force high frequency updates temporarily
void TFT_UI::forceHighFrequencyUpdates(int durationMs) {
    updateInterval = 33; // 30 FPS
    highFrequencyUntil = millis() + durationMs;
}

// Check if we should use high frequency updates
bool TFT_UI::shouldUseHighFrequency() {
    return millis() < highFrequencyUntil;
}

// Check if any screen has an active modal
bool TFT_UI::hasActiveModal() {
    if (currentScreen == SCREEN_SETTINGS) {
        SettingsScreen* settingsScreen = static_cast<SettingsScreen*>(screens[SCREEN_SETTINGS]);
        return settingsScreen && settingsScreen->hasActiveNumberPicker();
    } else if (currentScreen == SCREEN_PROGRAMS) {
        ProgramsScreen* programsScreen = static_cast<ProgramsScreen*>(screens[SCREEN_PROGRAMS]);
        return programsScreen && programsScreen->hasActiveTimePicker();
    }
    return false;
}

// ========================= END ADAPTIVE UPDATE FREQUENCY =========================

// Update dynamic constraints across all wheels
void MultiDigitWheelPicker::updateDynamicConstraints() {
    // Special handling for time values (HHMMSS format)
    bool isTimeValue = (maxValue == 235959.0f && step == 1.0f && decimalPlaces == 0);
    
    // Special handling for percentage values (0-100)
    bool isPercentageValue = (maxValue == 100.0f && step == 1.0f && decimalPlaces == 0);
    
    if (isTimeValue && wheelCount >= 2) {
        // Get first hour digit value
        int firstHourDigit = digitWheels[0] ? digitWheels[0]->getValue() : 0;
        
        // Update second hour digit constraints
        if (digitWheels[1]) {
            digitWheels[1]->updateDynamicConstraints(firstHourDigit, 0, 0);
        }
    } else if (isPercentageValue && wheelCount >= 3) {
        // Get hundreds and tens digit values
        int hundredsDigit = digitWheels[0] ? digitWheels[0]->getValue() : 0;
        int tensDigit = digitWheels[1] ? digitWheels[1]->getValue() : 0;
        
        // Update tens digit constraints
        if (digitWheels[1]) {
            digitWheels[1]->updateDynamicConstraints(0, hundredsDigit, 0);
        }
        
        // Update ones digit constraints
        if (digitWheels[2]) {
            digitWheels[2]->updateDynamicConstraints(0, hundredsDigit, tensDigit);
        }
    }
}

void MultiDigitWheelPicker::setPickerDimensions(int x, int y, int width, int height) {
    pickerX = x;
    pickerY = y;
    pickerWidth = width;
    pickerHeight = height;
    
    // Recalculate wheel positions
    int wheelWidth = 60;
    int wheelHeight = 160;
    int spacing = 4;
    int startX = pickerX + (pickerWidth - (wheelCount * wheelWidth + (wheelCount - 1) * spacing)) / 2;
    int wheelY = pickerY + 20; // Start higher since no title, just small margin from picker top
    
    // Update wheel positions
    for (int i = 0; i < wheelCount; i++) {
        if (digitWheels[i]) {
            digitWheels[i]->wheelX = startX + i * (wheelWidth + spacing);
            digitWheels[i]->wheelY = wheelY;
            digitWheels[i]->wheelWidth = wheelWidth;
            digitWheels[i]->wheelHeight = wheelHeight;
            digitWheels[i]->forceRedraw();
        }
    }
    
    pickerNeedsRedraw = true;
}