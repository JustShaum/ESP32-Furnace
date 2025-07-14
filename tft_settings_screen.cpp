#include "tft_ui.h"
#include <WiFi.h>

// External variables from main firmware
extern bool pwmEnabled;
extern float pwmFrequency;
extern bool pidEnabled;
extern float pidKp;
extern float pidKi;
extern float pidKd;
extern float pidSampleTime;
extern int pidOutputMin;
extern int pidOutputMax;
extern float pidSetpointWindow;
extern bool isDarkMode;
extern bool useManualTime;
extern int utcOffset;
extern struct tm manualTimeSettings;
// Logging and temperature settings
extern unsigned long loggingFrequencySeconds;
extern int errorCleanupMinutes;
extern unsigned long tempLogCleanupMinutes;
extern float temperatureIncrement;

// External save functions from main firmware
extern void saveAppSettings();
extern void resetPID();
extern void syncTime();
extern bool timeIsSynchronized;

// Settings data structure
struct SettingItem {
    String name;
    String value;
    bool isToggle;
    bool isEditable;
    float minValue;
    float maxValue;
    float step;
};

// Static instance for callbacks
static SettingsScreen* settingsScreenInstance = nullptr;

// Settings list
static const int MAX_SETTINGS = 18; // Reduced by 1 after removing Dark Mode setting
static SettingItem settingsItems[MAX_SETTINGS];

// Special picker type for time setting
static bool isTimePickerActive = false;

// Constructor
SettingsScreen::SettingsScreen(TFT_UI* ui) : ui(ui) {
    settingsScreenInstance = this;
    scrollOffset = 0;
    selectedSetting = 0;
    
    // Initialize number picker
    numberPicker = nullptr;
    showingNumberPicker = false;
    
    // Initialize settings items
    initializeSettings();
}

// Destructor
SettingsScreen::~SettingsScreen() {
    // Clean up number picker
    if (numberPicker) {
        delete numberPicker;
        numberPicker = nullptr;
    }
}

// Validate external variables
bool SettingsScreen::validateExternalVariables() {
    // Check if external variables are accessible
    try {
        // Try to access the variables
        bool pwmTest = pwmEnabled;
        float freqTest = pwmFrequency;
        bool pidTest = pidEnabled;
        bool themeTest = isDarkMode;
        bool timeTest = useManualTime;
        int offsetTest = utcOffset;
        
        return true;
    } catch (...) {
        return false;
    }
}

// Initialize settings items
void SettingsScreen::initializeSettings() {
    // Validate external variables first
    if (!validateExternalVariables()) {
        return;
    }
    
    int i = 0;
    
    // PWM Settings - with safety checks
    settingsItems[i++] = {"PWM Enable", String(pwmEnabled ? "ON" : "OFF"), true, true, 0, 1, 1};
    settingsItems[i++] = {"PWM Frequency", String(pwmFrequency, 1) + " Hz", false, true, 0.1, 40000, 0.1};
    
    // PID Settings - with safety checks
    settingsItems[i++] = {"PID Enable", String(pidEnabled ? "ON" : "OFF"), true, true, 0, 1, 1};
    settingsItems[i++] = {"PID Kp", String(pidKp, 2), false, true, 0, 100, 0.1};
    settingsItems[i++] = {"PID Ki", String(pidKi, 3), false, true, 0, 10, 0.01};
    settingsItems[i++] = {"PID Kd", String(pidKd, 3), false, true, 0, 10, 0.01};
    settingsItems[i++] = {"PID Sample Time", String(pidSampleTime, 1) + " s", false, true, 0.1, 10, 0.1};
    settingsItems[i++] = {"PID Output Min", String(pidOutputMin) + " %", false, true, 0, 100, 1};
    settingsItems[i++] = {"PID Output Max", String(pidOutputMax) + " %", false, true, 0, 100, 1};
    settingsItems[i++] = {"PID Setpoint Window", String(pidSetpointWindow, 1), false, true, 0.1, 10, 0.1};
    
    // Time Settings - with safety checks (Dark Mode option removed)
    settingsItems[i++] = {"Manual Time", String(useManualTime ? "ON" : "OFF"), true, true, 0, 1, 1};
    settingsItems[i++] = {"UTC Offset", String(utcOffset) + " h", false, true, -12, 14, 1};
    
    // Add "Set Time" option only when manual time is enabled
    if (useManualTime) {
        char timeStr[9];
        sprintf(timeStr, "%02d:%02d:%02d", manualTimeSettings.tm_hour, manualTimeSettings.tm_min, manualTimeSettings.tm_sec);
        settingsItems[i++] = {"Set Time", String(timeStr), false, true, 0, 235959, 1};
    }
    
    // Logging Settings
    settingsItems[i++] = {"Log Frequency", String((unsigned long)loggingFrequencySeconds) + " s", false, true, 15, 3600, 1};
    settingsItems[i++] = {"Error Cleanup", String(errorCleanupMinutes) + " min", false, true, 0, 1440, 1};
    settingsItems[i++] = {"Temp Log Cleanup", String((unsigned long)tempLogCleanupMinutes) + " min", false, true, 0, 1440, 1};
    
    // Temperature Settings
    settingsItems[i++] = {"Temp Increment", String(temperatureIncrement, 1) + " C", false, true, 1, 100, 1};
    
    // System Actions
    settingsItems[i++] = {"Save Settings", "Press to save", false, false, 0, 0, 0};
}

// Initialize screen
void SettingsScreen::init() {
    // Initialize buttons
    buttonCount = 5;
    buttons = new TFT_Button[buttonCount];
    
    // Back button
    buttons[0].x = 10;
    buttons[0].y = 25;
    buttons[0].width = BUTTON_WIDTH_MEDIUM;
    buttons[0].height = BUTTON_HEIGHT;
    buttons[0].text = "Back";
    buttons[0].state = BTN_NORMAL;
    buttons[0].visible = true;
    buttons[0].bgColor = ui->getTheme().primaryColor;
    buttons[0].textColor = ui->getTheme().textColor;
    buttons[0].borderColor = ui->getTheme().borderColor;
    buttons[0].onPress = onBackPress;
    buttons[0].id = 0;
    buttons[0].pressedTime = 0;
    buttons[0].pressDuration = 100;
    
    // Up button
    buttons[1].x = 270;
    buttons[1].y = 70;
    buttons[1].width = BUTTON_WIDTH_SMALL;
    buttons[1].height = BUTTON_HEIGHT;
    buttons[1].text = "Up";
    buttons[1].state = BTN_NORMAL;
    buttons[1].visible = true;
    buttons[1].bgColor = ui->getTheme().primaryColor;
    buttons[1].textColor = ui->getTheme().textColor;
    buttons[1].borderColor = ui->getTheme().borderColor;
    buttons[1].onPress = nullptr;
    buttons[1].id = 1;
    buttons[1].pressedTime = 0;
    buttons[1].pressDuration = 100;
    
    // Down button
    buttons[2].x = 270;
    buttons[2].y = 115;
    buttons[2].width = BUTTON_WIDTH_SMALL;
    buttons[2].height = BUTTON_HEIGHT;
    buttons[2].text = "Down";
    buttons[2].state = BTN_NORMAL;
    buttons[2].visible = true;
    buttons[2].bgColor = ui->getTheme().primaryColor;
    buttons[2].textColor = ui->getTheme().textColor;
    buttons[2].borderColor = ui->getTheme().borderColor;
    buttons[2].onPress = nullptr;
    buttons[2].id = 2;
    buttons[2].pressedTime = 0;
    buttons[2].pressDuration = 100;
    
    // Toggle/Edit button
    buttons[3].x = 270;
    buttons[3].y = 160;
    buttons[3].width = BUTTON_WIDTH_SMALL;
    buttons[3].height = BUTTON_HEIGHT;
    buttons[3].text = "Edit";
    buttons[3].state = BTN_NORMAL;
    buttons[3].visible = true;
    buttons[3].bgColor = ui->getTheme().primaryColor;
    buttons[3].textColor = ui->getTheme().textColor;
    buttons[3].borderColor = ui->getTheme().borderColor;
    buttons[3].onPress = nullptr;
    buttons[3].id = 3;
    buttons[3].pressedTime = 0;
    buttons[3].pressDuration = 100;
    
    // Save All button
    buttons[4].x = 100;
    buttons[4].y = 25;
    buttons[4].width = BUTTON_WIDTH_LARGE;
    buttons[4].height = BUTTON_HEIGHT;
    buttons[4].text = "Save All";
    buttons[4].state = BTN_NORMAL;
    buttons[4].visible = true;
    buttons[4].bgColor = ui->getTheme().primaryColor;
    buttons[4].textColor = ui->getTheme().textColor;
    buttons[4].borderColor = ui->getTheme().borderColor;
    buttons[4].onPress = onSaveSettings;
    buttons[4].id = 4;
    buttons[4].pressedTime = 0;
    buttons[4].pressDuration = 100;
    
    // Initialize text elements
    textCount = 1;
    texts = new TFT_Text[textCount];
    
    // Title (hidden as requested)
    texts[0].x = 160;
    texts[0].y = 5;
    texts[0].text = "Settings";
    texts[0].size = 2;
    texts[0].color = ui->getTheme().textColor;
    texts[0].visible = false;
    texts[0].centered = true;
    
    needsRedraw = true;
}

// On screen show
void SettingsScreen::onShow() {
    // Re-initialize settings to ensure fresh data
    initializeSettings();
    
    // Force immediate update of values to current state
    forceUpdateSettingsValues();
    
    // Force redraw
    needsRedraw = true;
}

// Update screen data
void SettingsScreen::update() {
    // Update number picker if visible
    if (numberPicker && showingNumberPicker) {
        numberPicker->update();
        
        // Don't trigger full screen redraws for wheel animations
        // The picker will handle its own wheel drawing independently
        
        return; // Don't update other elements when picker is shown
    }
    
    // Update settings values
    updateSettingsValues();
    
    // Update button states - Up/Down are always enabled unless at absolute limits
    buttons[1].state = (selectedSetting > 0 || scrollOffset > 0) ? BTN_NORMAL : BTN_DISABLED;
    buttons[2].state = (selectedSetting < MAX_SETTINGS - 1 || scrollOffset < MAX_SETTINGS - 6) ? BTN_NORMAL : BTN_DISABLED;
    
    // Update edit button based on selected setting
    if (selectedSetting < MAX_SETTINGS) {
        if (settingsItems[selectedSetting].isToggle) {
            buttons[3].text = "Toggle";
        } else if (settingsItems[selectedSetting].isEditable) {
            buttons[3].text = "Edit";
        } else {
            buttons[3].text = "Action";
        }
    }
    
    // Handle non-blocking button state reset
    unsigned long currentTime = millis();
    bool buttonStateChanged = false;
    
    for (int i = 0; i < buttonCount; i++) {
        if (buttons[i].state == BTN_PRESSED) {
            if (currentTime - buttons[i].pressedTime >= buttons[i].pressDuration) {
                buttons[i].state = BTN_NORMAL;
                buttonStateChanged = true;
            }
        }
    }
    
    if (buttonStateChanged) {
        needsRedraw = true;
    }
}

// Draw screen
void SettingsScreen::draw() {
    if (!needsRedraw) {
        return;
    }
    
    // Validate that UI is available
    if (!ui) {
        return;
    }
    
    // Validate that TFT is available
    TFT_eSPI& tft = ui->getTFT();
    
    // Test drawing a simple pixel to verify TFT is working
    tft.drawPixel(0, 0, ui->getTheme().textColor);
    
    // Clear any existing content in the settings area
    ui->getTFT().fillRect(5, 70, 260, 135, ui->getTheme().backgroundColor);
    
    // Draw main settings card (without title)
    ui->drawCard(5, 70, 260, 135, "");
    
    // Draw visible settings - increased to 7 items due to taller card
    int visibleCount = 0;
    for (int i = 0; i < 7; i++) {
        int settingIndex = scrollOffset + i;
        if (settingIndex < MAX_SETTINGS) {
            bool isSelected = (settingIndex == selectedSetting);
            drawSettingsCard(i, settingsItems[settingIndex].name, 
                            settingsItems[settingIndex].value, isSelected);
            visibleCount++;
        }
    }
    
    // Draw scroll indicator
    drawScrollIndicator();
    
    // Draw buttons
    for (int i = 0; i < buttonCount; i++) {
        if (buttons[i].visible) {
            ui->drawButton(buttons[i]);
        }
    }
    
    // Draw text elements
    for (int i = 0; i < textCount; i++) {
        if (texts[i].visible) {
            ui->drawText(texts[i]);
        }
    }
    
    // Draw number picker if visible (draws on top of everything)
    if (numberPicker && showingNumberPicker) {
        // Validate picker state and fix if out of sync
        numberPicker->validateState();
        
        // Draw the picker overlay (background, buttons, decimal point only)
        // Individual wheels are drawn separately in the main update loop
        numberPicker->drawStaticElements();
    }
    
    needsRedraw = false;
}

// Handle touch input
void SettingsScreen::handleTouch(TouchPoint& touch) {
    // Handle number picker touch if visible
    if (numberPicker && showingNumberPicker) {
        // Validate picker state before handling touch
        numberPicker->validateState();
        
        // Only handle touch if picker is properly visible
        if (numberPicker->isVisible()) {
            numberPicker->handleTouch(touch);
            return; // Don't handle other touches when picker is shown
        } else {
            // Picker state is invalid - hide it
            showingNumberPicker = false;
            needsRedraw = true;
        }
    }
    
    // Skip navigation bar touches - let the navigation system handle them
    if (touch.y > TFT_HEIGHT - 30) {
        return;
    }
    
    // Check button touches
    for (int i = 0; i < buttonCount; i++) {
        if (ui->isTouchInButton(touch, buttons[i])) {
            if (buttons[i].onPress) {
                buttons[i].state = BTN_PRESSED;
                buttons[i].pressedTime = millis();
                buttons[i].pressDuration = 100; // 100ms press duration
                needsRedraw = true;
                
                // Call button callback
                buttons[i].onPress();
            } else {
                // Handle scroll and edit buttons
                handleButtonPress(i);
            }
            break;
        }
    }
    
    // Check settings item touches - updated for 7 visible items
    if (touch.x >= 5 && touch.x <= 265 && touch.y >= 70 && touch.y <= 205) {
        int itemIndex = (touch.y - 70) / 18;
        int settingIndex = scrollOffset + itemIndex;
        
        if (settingIndex < MAX_SETTINGS && itemIndex < 7) { // Ensure within 7 visible items
            selectedSetting = settingIndex;
            needsRedraw = true;
        }
    }
}

// Update settings values
void SettingsScreen::updateSettingsValues() {
    static unsigned long lastValuesUpdate = 0;
    
    if (millis() - lastValuesUpdate > 1000) { // Update values max once per second
        forceUpdateSettingsValues();
        lastValuesUpdate = millis();
    }
}

// Force immediate update of settings values (bypasses throttle)
void SettingsScreen::forceUpdateSettingsValues() {
    int i = 0;
    
    // Update PWM settings
    settingsItems[i++].value = String(pwmEnabled ? "ON" : "OFF");
    settingsItems[i++].value = String(pwmFrequency, 1) + " Hz";
    
    // Update PID settings
    settingsItems[i++].value = String(pidEnabled ? "ON" : "OFF");
    settingsItems[i++].value = String(pidKp, 2);
    settingsItems[i++].value = String(pidKi, 3);
    settingsItems[i++].value = String(pidKd, 3);
    settingsItems[i++].value = String(pidSampleTime, 1) + " s";
    settingsItems[i++].value = String(pidOutputMin) + " %";
    settingsItems[i++].value = String(pidOutputMax) + " %";
    settingsItems[i++].value = String(pidSetpointWindow, 1);
    
    // Update time settings (Dark Mode option removed)
    settingsItems[i++].value = String(useManualTime ? "ON" : "OFF");
    settingsItems[i++].value = String(utcOffset) + " h";
    
    // Action items
    settingsItems[i++].value = "Press to save";
}

// Draw individual settings card
void SettingsScreen::drawSettingsCard(int index, const String& title, const String& value, bool selected) {
    int y = 75 + index * 18;
    
    // Draw selection background
    if (selected) {
        ui->getTFT().fillRect(8, y - 2, 254, 16, ui->getTheme().highlightColor);
    }
    
    // Draw setting name
    ui->getTFT().setTextColor(ui->getTheme().textColor);
    ui->getTFT().setTextSize(1);
    ui->getTFT().setCursor(12, y);
    ui->getTFT().print(title);
    
    // Draw setting value
    ui->getTFT().setCursor(180, y);
    ui->getTFT().print(value);
    
    // Draw separator line
    if (index < 6) {  // Updated for 7 items (0-6, so separator after items 0-5)
        ui->getTFT().drawLine(10, y + 14, 260, y + 14, ui->getTheme().borderColor);
    }
}

// Draw scroll indicator
void SettingsScreen::drawScrollIndicator() {
    if (MAX_SETTINGS <= 7) return;
    
    int indicatorHeight = 105;  // Increased from 80 to match new card height
    int indicatorY = 75;
    int indicatorX = 265;
    
    // Draw scroll track
    ui->getTFT().drawRect(indicatorX, indicatorY, 3, indicatorHeight, ui->getTheme().borderColor);
    
    // Draw scroll thumb
    int thumbHeight = (7 * indicatorHeight) / MAX_SETTINGS;
    int thumbY = indicatorY + (scrollOffset * indicatorHeight) / MAX_SETTINGS;
    
    ui->getTFT().fillRect(indicatorX + 1, thumbY, 1, thumbHeight, ui->getTheme().primaryColor);
}

// Handle button press
void SettingsScreen::handleButtonPress(int buttonIndex) {
    switch (buttonIndex) {
        case 1: // Up button - smart selection/scrolling
            if (selectedSetting > 0) {
                selectedSetting--;
                
                // If selection moved above visible area, scroll up
                if (selectedSetting < scrollOffset) {
                    scrollOffset = selectedSetting;
                }
                needsRedraw = true;
            }
            break;
            
        case 2: // Down button - smart selection/scrolling
            if (selectedSetting < MAX_SETTINGS - 1) {
                selectedSetting++;
                
                // If selection moved below visible area, scroll down
                if (selectedSetting >= scrollOffset + 7) {
                    scrollOffset = selectedSetting - 6;
                }
                needsRedraw = true;
            }
            break;
            
        case 3: // Toggle/Edit
            if (selectedSetting < MAX_SETTINGS) {
                handleSettingEdit(selectedSetting);
            }
            break;
    }
}

// Handle setting edit
void SettingsScreen::handleSettingEdit(int settingIndex) {
    if (settingIndex >= MAX_SETTINGS) return;
    
    SettingItem& item = settingsItems[settingIndex];
    
    if (item.isToggle) {
        // Toggle boolean values
        toggleSetting(settingIndex);
    } else if (item.isEditable) {
        // Edit numeric values (simplified - in real implementation might show number pad)
        editSetting(settingIndex);
    } else {
        // Action items
        performAction(settingIndex);
    }
}

// Toggle setting
void SettingsScreen::toggleSetting(int settingIndex) {
    switch (settingIndex) {
        case 0: // PWM Enable
            pwmEnabled = !pwmEnabled;
            ui->showMessage("PWM " + String(pwmEnabled ? "enabled" : "disabled"));
            break;
            
        case 2: // PID Enable
            pidEnabled = !pidEnabled;
            ui->showMessage("PID " + String(pidEnabled ? "enabled" : "disabled"));
            break;
            
        case 10: // Manual Time (was case 11, Dark Mode removed)
            useManualTime = !useManualTime;
            ui->showMessage("Manual time " + String(useManualTime ? "enabled" : "disabled"));
            // Refresh settings to show/hide "Set Time" option
            initializeSettings();
            break;
    }
    
    // Immediately update the displayed values to reflect the change
    forceUpdateSettingsValues();
    needsRedraw = true;
}

// Edit setting using number wheel picker
void SettingsScreen::editSetting(int settingIndex) {
    if (showingNumberPicker) return; // Already showing picker
    
    SettingItem& item = settingsItems[settingIndex];
    float currentValue = getCurrentSettingValue(settingIndex);
    
    // Clean up existing picker if any
    if (numberPicker) {
        delete numberPicker;
        numberPicker = nullptr;
    }
    
    // Create new multi-digit picker with setting constraints
    numberPicker = new MultiDigitWheelPicker(ui, item.minValue, item.maxValue, item.step, currentValue);
    
    // Set up callbacks
    numberPicker->onValueSelected(onNumberSelected);
    numberPicker->onCancelled(onNumberCancelled);
    
    // Show the picker
    showingNumberPicker = true;
    numberPicker->show();
    needsRedraw = true; // Force redraw to show the picker
    
    // Immediately draw all wheels to make them visible
    for (int i = 0; i < numberPicker->getWheelCount(); i++) {
        if (numberPicker->getDigitWheel(i)) {
            numberPicker->getDigitWheel(i)->draw();
        }
    }
}

// Get current setting value
float SettingsScreen::getCurrentSettingValue(int settingIndex) {
    switch (settingIndex) {
        case 1: return pwmFrequency;
        case 3: return pidKp;
        case 4: return pidKi;
        case 5: return pidKd;
        case 6: return pidSampleTime;
        case 7: return pidOutputMin;
        case 8: return pidOutputMax;
        case 9: return pidSetpointWindow;
        case 11: return utcOffset; // was case 12, adjusted for removed Dark Mode
        case 12: // Set Time (only when manual time is enabled) - was case 13
            if (useManualTime) {
                // Format time as HHMMSS for the picker
                return (manualTimeSettings.tm_hour * 10000) + 
                       (manualTimeSettings.tm_min * 100) + 
                       manualTimeSettings.tm_sec;
            }
            return 0;
        case 13: return (float)loggingFrequencySeconds; // was case 14
        case 14: return (float)errorCleanupMinutes; // was case 15
        case 15: return (float)tempLogCleanupMinutes; // was case 16
        case 16: return temperatureIncrement; // was case 17
        default: return 0;
    }
}

// Set setting value
void SettingsScreen::setSettingValue(int settingIndex, float value) {
    switch (settingIndex) {
        case 1: pwmFrequency = value; break;
        case 3: pidKp = value; break;
        case 4: pidKi = value; break;
        case 5: pidKd = value; break;
        case 6: pidSampleTime = value; break;
        case 7: pidOutputMin = (int)value; break;
        case 8: pidOutputMax = (int)value; break;
        case 9: pidSetpointWindow = value; break;
        case 11: utcOffset = (int)value; break; // was case 12, adjusted for removed Dark Mode
        case 12: // Set Time (only when manual time is enabled) - was case 13
            if (useManualTime) {
                // Parse HHMMSS format back to time components
                int timeValue = (int)value;
                int hours = timeValue / 10000;
                int minutes = (timeValue % 10000) / 100;
                int seconds = timeValue % 100;
                
                // Validate and constrain values
                hours = constrain(hours, 0, 23);
                minutes = constrain(minutes, 0, 59);
                seconds = constrain(seconds, 0, 59);
                
                // Update manual time settings
                manualTimeSettings.tm_hour = hours;
                manualTimeSettings.tm_min = minutes;
                manualTimeSettings.tm_sec = seconds;
                
                // Update settings display
                initializeSettings();
            }
            break;
        case 13: loggingFrequencySeconds = (unsigned long)value; break; // was case 14
        case 14: errorCleanupMinutes = (int)value; break; // was case 15
        case 15: tempLogCleanupMinutes = (unsigned long)value; break; // was case 16
        case 16: temperatureIncrement = value; break; // was case 17
    }
    
    // Force immediate update when any setting value is changed
    forceUpdateSettingsValues();
}

// Perform action
void SettingsScreen::performAction(int settingIndex) {
    // Find the "Save Settings" item dynamically
    if (settingIndex < MAX_SETTINGS && settingsItems[settingIndex].name == "Save Settings") {
        onSaveSettings();
    }
}

// Handle scrolling
void SettingsScreen::handleScrolling(TouchPoint& touch) {
    // Simple scroll handling - could be enhanced with momentum
    static int lastTouchY = 0;
    static unsigned long lastScrollTime = 0;
    
    unsigned long currentTime = millis();
    if (currentTime - lastScrollTime > 100) {
        int deltaY = touch.y - lastTouchY;
        
        if (deltaY > 20 && scrollOffset > 0) {
            scrollOffset--;
            needsRedraw = true;
            lastScrollTime = currentTime;
        } else if (deltaY < -20 && scrollOffset < MAX_SETTINGS - 6) {
            scrollOffset++;
            needsRedraw = true;
            lastScrollTime = currentTime;
        }
    }
    
    lastTouchY = touch.y;
}

// Helper function to make HTTP POST request with retry
// Save current settings directly using main firmware functions
void SettingsScreen::saveCurrentSettings() {
    ui->showMessage("Saving settings...", ui->getTheme().textColor, 1000);
    
    try {
        // All global variables are already updated by setSettingValue()
        // We just need to persist them to preferences
        
        saveAppSettings();
        
        // Reset PID when settings change (same as web interface does)
        resetPID();
        
        ui->showSuccess("Settings saved successfully");
        
    } catch (...) {
        ui->showError("Failed to save settings - system error");
    }
}

// updateBackendThemeMode removed - Dark Mode option no longer available in TFT settings

// Button callback implementations
void SettingsScreen::onBackPress() {
    if (!settingsScreenInstance) return;
    settingsScreenInstance->ui->setScreen(SCREEN_MAIN);
}

void SettingsScreen::onPwmToggle() {
    if (!settingsScreenInstance) return;
    settingsScreenInstance->toggleSetting(0);
}

void SettingsScreen::onPidToggle() {
    if (!settingsScreenInstance) return;
    settingsScreenInstance->toggleSetting(2);
}

// onThemeToggle removed - Dark Mode option no longer available in TFT settings

void SettingsScreen::onTimeSync() {
    if (!settingsScreenInstance) return;
    
    // Check WiFi connection first
    if (WiFi.status() != WL_CONNECTED) {
        settingsScreenInstance->ui->showError("WiFi not connected - cannot sync time");
        return;
    }
    
    // Call the main firmware's time sync function directly
    extern void syncTime();
    syncTime();
    
    // Check if time sync was successful
    extern bool timeIsSynchronized;
    if (timeIsSynchronized) {
        settingsScreenInstance->ui->showSuccess("Time synchronized");
    } else {
        settingsScreenInstance->ui->showError("Failed to sync time");
    }
}

void SettingsScreen::onSaveSettings() {
    if (!settingsScreenInstance) return;
    settingsScreenInstance->saveCurrentSettings();
}

// Number picker callbacks
void SettingsScreen::onNumberSelected(float value) {
    if (!settingsScreenInstance) return;
    
    // Apply the selected value
    settingsScreenInstance->setSettingValue(settingsScreenInstance->selectedSetting, value);
    
    // Hide the picker and clean up
    settingsScreenInstance->showingNumberPicker = false;
    if (settingsScreenInstance->numberPicker) {
        settingsScreenInstance->numberPicker->hide();
    }
    
    // Clear screen and force complete redraw to remove picker artifacts
    settingsScreenInstance->ui->clearScreen();
    settingsScreenInstance->needsRedraw = true;
    settingsScreenInstance->ui->forceRedraw();
    
    // Small delay to ensure screen is fully redrawn before showing message
    delay(100);
    
    // Show confirmation message after screen is refreshed
    if (isTimePickerActive) {
        // Format time message for better readability
        int timeValue = (int)value;
        int hours = timeValue / 10000;
        int minutes = (timeValue % 10000) / 100;
        int seconds = timeValue % 100;
        
        char timeStr[10];
        sprintf(timeStr, "%02d:%02d:%02d", hours, minutes, seconds);
        settingsScreenInstance->ui->showSuccess("Time set to " + String(timeStr));
        
        isTimePickerActive = false; // Reset flag
    } else {
        settingsScreenInstance->ui->showSuccess("Value updated to " + String(value, 2));
    }
}

void SettingsScreen::onNumberCancelled() {
    if (!settingsScreenInstance) return;
    
    // Hide the picker without saving and clean up
    settingsScreenInstance->showingNumberPicker = false;
    if (settingsScreenInstance->numberPicker) {
        settingsScreenInstance->numberPicker->hide();
    }
    
    // Clear screen and force complete redraw to remove picker artifacts
    settingsScreenInstance->ui->clearScreen();
    settingsScreenInstance->needsRedraw = true;
    settingsScreenInstance->ui->forceRedraw();
    
    // Small delay to ensure screen is fully redrawn before showing message
    delay(100);
    
    // Reset time picker flag
    isTimePickerActive = false;
    
    // Show cancellation message after screen is refreshed
    settingsScreenInstance->ui->showMessage("Edit cancelled", settingsScreenInstance->ui->getTheme().warningColor, 2000);
} 

// Check if there are active animations (for adaptive update frequency)
bool SettingsScreen::hasActiveAnimations() {
    if (numberPicker && showingNumberPicker) {
        // Check if any digit wheels are animating
        for (int i = 0; i < numberPicker->getWheelCount(); i++) {
            if (numberPicker->getDigitWheel(i) && numberPicker->getDigitWheel(i)->isAnimating()) {
                return true;
            }
        }
    }
    return false;
}

// Check if number picker is active (for modal blocking)
bool SettingsScreen::hasActiveNumberPicker() {
    return numberPicker && showingNumberPicker && numberPicker->isVisible();
}

// Draw active number picker wheels without full screen redraw
void SettingsScreen::drawActiveNumberPicker() {
    if (numberPicker && showingNumberPicker && numberPicker->isVisible()) {
        // Only draw wheels that actually need redrawing
        for (int i = 0; i < numberPicker->getWheelCount(); i++) {
            if (numberPicker->getDigitWheel(i) && numberPicker->getDigitWheel(i)->needsRedraw()) {
                numberPicker->getDigitWheel(i)->draw();
            }
        }
    }
} 