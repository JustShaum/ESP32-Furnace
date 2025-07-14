#include "tft_ui.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// External variables from main firmware
extern String programNames[MAX_PROGRAMS];
extern int activeProgram;
extern float** programTemps;
extern int maxTempPoints;
extern float currentTemp;
extern float* targetTemp;
extern bool furnaceStatus;
extern bool systemEnabled;
extern float getSmoothedTargetTemperature();
extern int getCurrentTempIndex();
extern bool temperatureSmoothingEnabled;

// Helper function to format program data consistent with web interface
// This matches the JavaScript trimming logic in programs.js
struct TrimmedProgramData {
    std::vector<float> temps;
    int startIndex;
    int trimmedLength;
};

TrimmedProgramData trimProgramData(float* sourceTemps, int maxPoints) {
    TrimmedProgramData result;
    
    if (!sourceTemps || maxPoints <= 0) {
        result.temps.push_back(0.0f);
        result.startIndex = 0;
        result.trimmedLength = 1;
        return result;
    }
    
    // Find first non-zero (matches JavaScript logic)
    int firstNonZero = 0;
    while (firstNonZero < maxPoints && sourceTemps[firstNonZero] == 0.0f) {
        firstNonZero++;
    }
    
    // If there were leading zeros, keep one (matches JavaScript: firstNonZero > 0 ? firstNonZero - 1 : 0)
    int startIdx = firstNonZero > 0 ? firstNonZero - 1 : 0;
    
    // Find last non-zero (matches JavaScript logic)
    int lastNonZero = maxPoints - 1;
    while (lastNonZero >= 0 && sourceTemps[lastNonZero] == 0.0f) {
        lastNonZero--;
    }
    
    // Extract trimmed data
    if (startIdx <= lastNonZero) {
        for (int i = startIdx; i <= lastNonZero; i++) {
            result.temps.push_back(sourceTemps[i]);
        }
        result.startIndex = startIdx;
    }
    
    // After trimming, ensure the first value is 0 (matches JavaScript logic)
    if (result.temps.size() > 0 && result.temps[0] != 0.0f) {
        result.temps.insert(result.temps.begin(), 0.0f);
        result.startIndex = 0;
    } else if (result.temps.empty()) {
        result.temps.push_back(0.0f);
        result.startIndex = 0;
    }
    
    result.trimmedLength = result.temps.size();
    return result;
}

// Helper function to validate program data before scheduling
bool validateProgramData(int programIndex) {
    if (programIndex < 0 || programIndex >= MAX_PROGRAMS) {
        return false;
    }
    
    if (programNames[programIndex].length() == 0) {
        return false;
    }
    
    // Check if program has any meaningful temperature data
    TrimmedProgramData trimmed = trimProgramData(programTemps[programIndex], maxTempPoints);
    if (trimmed.trimmedLength <= 1) {
        return false;
    }
    
    return true;
}

// Static instance for callbacks
static ProgramsScreen* programsScreenInstance = nullptr;

// Constructor
ProgramsScreen::ProgramsScreen(TFT_UI* ui) : ui(ui) {
    programsScreenInstance = this;
    selectedProgram = 0;
    programRunning = false;
    scrollOffset = 0; // Add scrolling support
    showingStartDialog = false;
    showingTimeScheduler = false;
    showingCreateDialog = false;
    editingProgram = false;
    editingTemps = nullptr;
    showingTempPicker = false;
    editingTempIndex = 0;
    timePicker = nullptr;
    tempPicker = nullptr;
}

// Destructor
ProgramsScreen::~ProgramsScreen() {
    // Clean up time picker
    if (timePicker) {
        delete timePicker;
        timePicker = nullptr;
    }
    
    // Clean up temperature picker
    if (tempPicker) {
        delete tempPicker;
        tempPicker = nullptr;
    }
    
    // Clean up editing temperature array
    if (editingTemps) {
        delete[] editingTemps;
        editingTemps = nullptr;
    }
}

// Initialize screen
void ProgramsScreen::init() {
    // Initialize buttons - up, down, and select only
    buttonCount = 3;
    buttons = new TFT_Button[buttonCount];
    
    // Program up button - moved to left side, made shorter and thinner
    buttons[0].x = 5;
    buttons[0].y = 140;
    buttons[0].width = 50;  // Thinner than BUTTON_WIDTH_SMALL (60)
    buttons[0].height = 30; // Shorter than BUTTON_HEIGHT (40)
    buttons[0].text = "Up";
    buttons[0].state = BTN_NORMAL;
    buttons[0].visible = true;
    buttons[0].bgColor = ui->getTheme().primaryColor;
    buttons[0].textColor = ui->getTheme().textColor;
    buttons[0].borderColor = ui->getTheme().borderColor;
    buttons[0].onPress = nullptr; // Handle in handleButtonPress
    buttons[0].id = 0;
    buttons[0].pressedTime = 0;
    buttons[0].pressDuration = 100;
    
    // Program down button - moved to left side, added gap from Up button
    buttons[1].x = 5;
    buttons[1].y = 175;  // 5px gap from Up button (140+30+5=175)
    buttons[1].width = 50;  // Thinner than BUTTON_WIDTH_SMALL (60)
    buttons[1].height = 30; // Shorter than BUTTON_HEIGHT (40)
    buttons[1].text = "Dn";
    buttons[1].state = BTN_NORMAL;
    buttons[1].visible = true;
    buttons[1].bgColor = ui->getTheme().primaryColor;
    buttons[1].textColor = ui->getTheme().textColor;
    buttons[1].borderColor = ui->getTheme().borderColor;
    buttons[1].onPress = nullptr; // Handle in handleButtonPress
    buttons[1].id = 1;
    buttons[1].pressedTime = 0;
    buttons[1].pressDuration = 100;
    
    // Select program button - moved to right side, full height since no create button
    buttons[2].x = 60;
    buttons[2].y = 140;
    buttons[2].width = 50;  // Smaller width
    buttons[2].height = 65; // Full height since no create button needed below
    buttons[2].text = "Sel";  // Shortened text to fit
    buttons[2].state = BTN_NORMAL;
    buttons[2].visible = true;
    buttons[2].bgColor = ui->getTheme().primaryColor;
    buttons[2].textColor = ui->getTheme().textColor;
    buttons[2].borderColor = ui->getTheme().borderColor;
    buttons[2].onPress = onProgramSelect;
    buttons[2].id = 2;
    buttons[2].pressedTime = 0;
    buttons[2].pressDuration = 100;
    
    // Initialize text elements
    textCount = 2; // Reduced from 4
    texts = new TFT_Text[textCount];
    
    // Selected program display - aligned with "Program Preview" title
    texts[0].x = 140;
    texts[0].y = 33;  // Aligned with "Program Preview" title (y=33)
    texts[0].text = "Program 1";
    texts[0].size = 1;
    texts[0].color = ui->getTheme().textColor;
    texts[0].visible = true;
    texts[0].centered = false;
    
    // Program status - spaced further apart from program name
    texts[1].x = 200;  // Moved further right for better spacing
    texts[1].y = 33;   // Same height as program name
    texts[1].text = "Ready";
    texts[1].size = 1;
    texts[1].color = ui->getTheme().textColor;
    texts[1].visible = true;
    texts[1].centered = false;
    
    needsRedraw = true;
}

// Update screen data
void ProgramsScreen::update() {
    // Update temperature picker if visible (highest priority)
    if (tempPicker && showingTempPicker) {
        tempPicker->update();
        return; // Don't update other elements when temp picker is shown
    }
    
    // Update time picker if visible
    if (timePicker && showingTimeScheduler) {
        timePicker->update();
        
        // Don't trigger full screen redraws for wheel animations
        // The picker will handle its own wheel drawing independently
        
        return; // Don't update other elements when picker is shown
    }
    
    // Update program status
    updateProgramStatus();
    
    // Update selected program display
    if (selectedProgram >= 0 && selectedProgram < MAX_PROGRAMS) {
        if (programNames[selectedProgram].length() > 0) {
            texts[0].text = programNames[selectedProgram];
        } else {
            texts[0].text = "Program " + String(selectedProgram + 1);
        }
    }
    
    // Check if selected program has valid temperature data using the validation function
    bool hasValidData = false;
    int dataPoints = 0;
    if (selectedProgram >= 0 && selectedProgram < MAX_PROGRAMS && programNames[selectedProgram].length() > 0) {
        TrimmedProgramData trimmed = trimProgramData(programTemps[selectedProgram], maxTempPoints);
        hasValidData = (trimmed.trimmedLength > 1);
        dataPoints = trimmed.trimmedLength;
    }
    
    // Update status text with more detailed information
    if (programRunning && selectedProgram == activeProgram) {
        texts[1].text = "Running";
        texts[1].color = ui->getTheme().successColor;
    } else if (programNames[selectedProgram].length() == 0) {
        texts[1].text = "Empty Slot";
        texts[1].color = ui->getTheme().textColor;
    } else if (!hasValidData) {
        texts[1].text = "No Data";
        texts[1].color = ui->getTheme().errorColor;
    } else {
        texts[1].text = "Ready (" + String(dataPoints) + " pts)";
        texts[1].color = ui->getTheme().textColor;
    }
    
    // Update button states for scrolling (like settings screen)
    buttons[0].state = (selectedProgram > 0 || scrollOffset > 0) ? BTN_NORMAL : BTN_DISABLED; // Up button
    buttons[1].state = (selectedProgram < MAX_PROGRAMS - 1 || scrollOffset < MAX_PROGRAMS - 4) ? BTN_NORMAL : BTN_DISABLED; // Down button
    
    // Handle non-blocking button state reset
    unsigned long currentTime = millis();
    bool buttonStateChanged = false;
    
    for (int i = 0; i < buttonCount; i++) {
        if (buttons[i].state == BTN_PRESSED) {
            if (currentTime - buttons[i].pressedTime >= buttons[i].pressDuration) {
                // Update the button state based on its function
                if (i == 0) { // Up button
                    buttons[i].state = (selectedProgram > 0 || scrollOffset > 0) ? BTN_NORMAL : BTN_DISABLED;
                } else if (i == 1) { // Down button
                    buttons[i].state = (selectedProgram < MAX_PROGRAMS - 1 || scrollOffset < MAX_PROGRAMS - 4) ? BTN_NORMAL : BTN_DISABLED;
                } else {
                    buttons[i].state = BTN_NORMAL;
                }
                buttonStateChanged = true;
            }
        }
    }
    
    if (buttonStateChanged) {
        needsRedraw = true;
    }
}

// Draw screen
void ProgramsScreen::draw() {
    if (!needsRedraw) {
        return;
    }
    
    // Don't redraw the main screen when any picker is active
    if ((timePicker && showingTimeScheduler) || (tempPicker && showingTempPicker)) {
        needsRedraw = false;
        return;
    }
    

    
    // Draw program create dialog if active
    if (showingCreateDialog) {
        drawProgramCreateDialog();
        needsRedraw = false;
        return;
    }
    
    // Draw program start dialog if active
    if (showingStartDialog) {
        drawProgramStartDialog();
        needsRedraw = false;
        return;
    }
    
    // Draw program list
    drawProgramList();
    
    // Draw program preview
    drawProgramPreview();
    
    // Draw program controls
    drawProgramControls();
    
    // Draw buttons
    for (int i = 0; i < buttonCount; i++) {
        ui->drawButton(buttons[i]);
    }
    
    // Draw text elements
    for (int i = 0; i < textCount; i++) {
        ui->drawText(texts[i]);
    }
    
    // Draw time picker if visible (draws on top of everything)
    if (timePicker && showingTimeScheduler) {
        // Validate picker state and fix if out of sync
        timePicker->validateState();
        
        // Don't redraw the picker here - it's already drawn in showTimeScheduler()
        // The picker handles its own drawing and updates
    }
    
    needsRedraw = false;
}

// Handle touch input
void ProgramsScreen::handleTouch(TouchPoint& touch) {
    // Handle temperature picker touch if visible (highest priority)
    if (tempPicker && showingTempPicker) {
        if (tempPicker->isVisible()) {
            tempPicker->handleTouch(touch);
            return;
        } else {
            showingTempPicker = false;
            needsRedraw = true;
        }
    }
    
    // Handle time picker touch if visible
    if (timePicker && showingTimeScheduler) {
        // Validate picker state before handling touch
        timePicker->validateState();
        
        // Debug: Log picker state
        // Only handle touch if picker is properly visible
        if (timePicker->isVisible()) {
            timePicker->handleTouch(touch);
            return; // Don't handle other touches when picker is shown
        } else {
            // Picker state is invalid - hide it
            showingTimeScheduler = false;
            needsRedraw = true;
        }
    }
    
    // Handle create dialog touches if active
    if (showingCreateDialog) {
        handleCreateDialogTouch(touch);
        return;
    }
    
    // Handle start dialog touches if active - DON'T skip navigation bar touches
    if (showingStartDialog) {
        handleStartDialogTouch(touch);
        return;
    }
    
    // Skip navigation bar touches - let the navigation system handle them
    if (touch.y > TFT_HEIGHT - 30) {
        return;
    }
    
    // Check button touches
    for (int i = 0; i < buttonCount; i++) {
        if (ui->isTouchInButton(touch, buttons[i])) {
            if (buttons[i].state != BTN_DISABLED) {
                buttons[i].state = BTN_PRESSED;
                buttons[i].pressedTime = millis();
                buttons[i].pressDuration = 100; // 100ms press duration
                needsRedraw = true;
                
                // Handle button press or call callback
                if (buttons[i].onPress) {
                    buttons[i].onPress();
                } else {
                    handleButtonPress(i);
                }
            }
            break;
        }
    }
    
    // Check program list touches - updated for wider centered position
    if (touch.x >= 110 && touch.x <= 230 && touch.y >= 145 && touch.y <= 205) { // Updated for wider list: 210 -> 230
        int programIndex = (touch.y - 145) / 15; // Adjusted for tighter spacing
        if (programIndex >= 0 && programIndex < 4) { // 4 visible programs
            int actualProgram = scrollOffset + programIndex;
            if (actualProgram < MAX_PROGRAMS) {
                selectedProgram = actualProgram;
                needsRedraw = true;
            }
        }
    }
}

// Draw program list
void ProgramsScreen::drawProgramList() {
    // Program list moved to center, between buttons on left and info on right - made wider
    ui->drawCard(110, 140, 120, 65, ""); // Made wider: 100 -> 120
    
    TFT_eSPI& tft = ui->getTFT();
    const TFT_Theme& theme = ui->getTheme();
    
    // Draw 4 visible program entries with scrolling
    for (int i = 0; i < 4; i++) {
        int programIndex = scrollOffset + i;
        if (programIndex >= MAX_PROGRAMS) break;
        
        int y = 145 + i * 15; // Tighter spacing to fit in smaller height
        
        // Highlight selected program
        if (programIndex == selectedProgram) {
            tft.fillRect(113, y - 2, 114, 13, theme.highlightColor); // Made wider: 94 -> 114
        }
        
        // Draw program name
        tft.setTextColor(theme.textColor);
        tft.setTextSize(1);
        tft.setCursor(117, y);
        
        if (programNames[programIndex].length() > 0) {
            String displayName = programNames[programIndex];
            if (displayName.length() > 15) { // Increased from 12 to 15 characters for wider space
                displayName = displayName.substring(0, 15) + "...";
            }
            tft.print(displayName);
        } else {
            tft.print("Prog " + String(programIndex + 1));
        }
        
        // Draw active indicator
        if (programIndex == activeProgram && programRunning) {
            tft.fillCircle(220, y + 4, 2, theme.successColor); // Moved further right: 200 -> 220
        }
    }
    
    // Draw scroll indicator
    drawScrollIndicator();
}

// Draw program preview
void ProgramsScreen::drawProgramPreview() {
    // Full width program preview along the top
    ui->drawCard(5, 25, 310, 110, "Program Preview");
    
    TFT_eSPI& tft = ui->getTFT();
    const TFT_Theme& theme = ui->getTheme();
    
    // Draw simple program visualization
    if (selectedProgram >= 0 && selectedProgram < MAX_PROGRAMS && programTemps && programTemps[selectedProgram]) {
        // Draw temperature curve
        int chartX = 15;
        int chartY = 45; // Adjusted for top card
        int chartWidth = 290; // Full width chart
        int chartHeight = 70; // Good height for visibility
        
        // Draw chart background
        tft.fillRect(chartX, chartY, chartWidth, chartHeight, theme.cardBackground);
        tft.drawRect(chartX, chartY, chartWidth, chartHeight, theme.borderColor);
        
        // Find min/max temperatures for scaling
        float minTemp = 9999, maxTemp = -9999;
        for (int i = 0; i < maxTempPoints; i++) {
            if (programTemps[selectedProgram][i] > maxTemp) maxTemp = programTemps[selectedProgram][i];
            if (programTemps[selectedProgram][i] < minTemp && programTemps[selectedProgram][i] > 0) {
                minTemp = programTemps[selectedProgram][i];
            }
        }
        
        if (maxTemp > minTemp && maxTemp > 0) {
            // Use the trimmed data for consistent display with web interface
            TrimmedProgramData trimmed = trimProgramData(programTemps[selectedProgram], maxTempPoints);
            
            if (trimmed.trimmedLength > 1) {
                // Recalculate min/max from trimmed data
                minTemp = 9999, maxTemp = -9999;
                for (size_t i = 0; i < trimmed.temps.size(); i++) {
                    if (trimmed.temps[i] > maxTemp) maxTemp = trimmed.temps[i];
                    if (trimmed.temps[i] < minTemp && trimmed.temps[i] >= 0) minTemp = trimmed.temps[i];
                }
                
                // Add some padding to the temperature range for better visualization
                if (minTemp == maxTemp) {
                    minTemp = max(0.0f, minTemp - 10.0f);
                    maxTemp = maxTemp + 10.0f;
                } else {
                    float tempRange = maxTemp - minTemp;
                    minTemp = max(0.0f, minTemp - tempRange * 0.1f);
                    maxTemp = maxTemp + tempRange * 0.1f;
                }
                
                // Draw temperature curve using trimmed data
                bool firstPoint = true;
                int lastX = 0, lastY = 0;
                
                for (size_t i = 0; i < trimmed.temps.size(); i++) {
                    int x = chartX + (i * chartWidth) / (trimmed.temps.size() - 1);
                    int y = chartY + chartHeight - ((trimmed.temps[i] - minTemp) / (maxTemp - minTemp)) * chartHeight;
                    
                    // Ensure coordinates are within chart bounds
                    x = max(chartX, min(chartX + chartWidth, x));
                    y = max(chartY, min(chartY + chartHeight, y));
                    
                    if (!firstPoint) {
                        tft.drawLine(lastX, lastY, x, y, theme.primaryColor);
                    }
                    
                    firstPoint = false;
                    lastX = x;
                    lastY = y;
                }
            }
        }
        
        // Calculate program duration from trimmed data for accuracy
        TrimmedProgramData durationData = trimProgramData(programTemps[selectedProgram], maxTempPoints);
        float hoursPerPoint = 24.0f / maxTempPoints;
        float programDuration = durationData.trimmedLength * hoursPerPoint;
        int chartEndX = chartX + chartWidth;

        // Draw axis labels with calculated values
        tft.setTextColor(theme.textColor);
        tft.setTextSize(1);
        tft.setCursor(chartX, chartY + chartHeight + 5);
        tft.print("Min: " + String(minTemp, 0) + "C");
        tft.setCursor(chartX + 100, chartY + chartHeight + 5);
        tft.print("Max: " + String(maxTemp, 0) + "C");
        tft.setCursor(chartEndX - 24, chartY + chartHeight + 5); // 24px for space
        tft.print(String((int)roundf(programDuration)) + "h");
        
        // Draw a dashed vertical line at the program end using trimmed data
        if (durationData.trimmedLength > 0 && durationData.trimmedLength < maxTempPoints) {
            int endX = chartX + chartWidth; // Program end is at the right edge of the chart now
            for (int y = chartY; y < chartY + chartHeight; y += 4) {
                tft.drawPixel(endX, y, theme.borderColor);
            }
        }
    } else {
        // No program data
        tft.setTextColor(theme.textColor);
        tft.setTextSize(1);
        tft.setCursor(120, 80);
        tft.print("No program data available");
    }
}

// Draw program controls
void ProgramsScreen::drawProgramControls() {
    // Add info box on the right side - made narrower and repositioned
    ui->drawCard(235, 140, 80, 65, ""); // Made narrower: 100 -> 80, moved right: 210 -> 235
    
    TFT_eSPI& tft = ui->getTFT();
    const TFT_Theme& theme = ui->getTheme();
    
    // Draw labels (these don't change)
    tft.setTextColor(theme.textColor);
    tft.setTextSize(1);
    tft.setCursor(245, 150); // Moved right: 240 -> 245 (additional 5px)
    tft.print("Current:");
    tft.setCursor(245, 175); // Moved right: 240 -> 245 (additional 5px)
    tft.print("Target:");
    
    // Draw temperature values with selective clearing
    // Display current temperature with error handling
    extern bool thermocoupleError;
    String tempStr = thermocoupleError ? "ERROR" : String(currentTemp, 1) + "C";
    uint16_t tempColor = thermocoupleError ? theme.errorColor : theme.textColor;
    drawSelectiveTemperature(245, 160, tempStr, lastCurrentTempStr, tempColor); // Moved right: 240 -> 245 (additional 5px)
    
    // Get smoothed target temperature (same logic as main screen)
    float displayTargetTemp = 0.0;
    if (temperatureSmoothingEnabled) {
        displayTargetTemp = getSmoothedTargetTemperature();
    } else {
        int currentIndex = getCurrentTempIndex();
        if (currentIndex >= 0 && currentIndex < maxTempPoints && targetTemp) {
            displayTargetTemp = targetTemp[currentIndex];
        }
    }
    drawSelectiveTemperature(245, 185, String(displayTargetTemp, 1) + "C", lastTargetTempStr, theme.errorColor); // Moved right: 240 -> 245 (additional 5px) and fixed to use smoothed target
}

// Draw scroll indicator
void ProgramsScreen::drawScrollIndicator() {
    if (MAX_PROGRAMS <= 4) return;
    
    int indicatorHeight = 50; // Reduced to match smaller card height
    int indicatorY = 145;
    int indicatorX = 225; // Moved to match wider program list position
    
    // Draw scroll track
    ui->getTFT().drawRect(indicatorX, indicatorY, 3, indicatorHeight, ui->getTheme().borderColor);
    
    // Draw scroll thumb
    int thumbHeight = (4 * indicatorHeight) / MAX_PROGRAMS;
    int thumbY = indicatorY + (scrollOffset * indicatorHeight) / MAX_PROGRAMS;
    
    ui->getTFT().fillRect(indicatorX + 1, thumbY, 1, thumbHeight, ui->getTheme().primaryColor);
}

// Selective temperature drawing method to prevent text artifacts
void ProgramsScreen::drawSelectiveTemperature(int x, int y, const String& newText, String& oldText, uint32_t textColor) {
    // Force redraw every 10th call to handle edge cases where values don't update
    static int forceRedrawCounter = 0;
    forceRedrawCounter++;
    bool forceRedraw = (forceRedrawCounter % 10 == 0);
    
    // Only redraw if text has changed or forced refresh
    if (newText != oldText || forceRedraw) {
        TFT_eSPI& tft = ui->getTFT();
        const TFT_Theme& theme = ui->getTheme();
        
        // Calculate text dimensions for clearing
        int textWidth = max(newText.length() * 6, oldText.length() * 6) + 15; // Extra padding for safety
        int textHeight = 8 + 6; // Text size 1 + more padding
        
        // Clear the text area using card background (not white)
        tft.fillRect(x - 3, y - 3, textWidth, textHeight, theme.cardBackground);
        
        // Draw the new text with specified color
        tft.setTextColor(textColor);
        tft.setTextSize(1);
        tft.setCursor(x, y);
        tft.print(newText);
        
        // Update the stored text
        oldText = newText;
        
        // Debug output for temperature updates
        if (forceRedraw) {
            Serial.println("TFT: Forced temperature display refresh");
        }
    }
}

// Handle button press
void ProgramsScreen::handleButtonPress(int buttonIndex) {
    switch (buttonIndex) {
        case 0: // Up button - smart selection/scrolling
            if (selectedProgram > 0) {
                selectedProgram--;
                
                // If selection moved above visible area, scroll up
                if (selectedProgram < scrollOffset) {
                    scrollOffset = selectedProgram;
                }
                needsRedraw = true;
            }
            break;
            
        case 1: // Down button - smart selection/scrolling
            if (selectedProgram < MAX_PROGRAMS - 1) {
                selectedProgram++;
                
                // If selection moved below visible area, scroll down
                if (selectedProgram >= scrollOffset + 4) {
                    scrollOffset = selectedProgram - 3;
                }
                needsRedraw = true;
            }
            break;
    }
}

// Update program status
void ProgramsScreen::updateProgramStatus() {
    // Check if program is actually running and has valid temperature data
    bool hasValidData = false;
    if (activeProgram >= 0 && activeProgram < MAX_PROGRAMS) {
        // Use the new validation function for consistency with web interface
        hasValidData = validateProgramData(activeProgram);
    }
    
    // Update program running status - only consider running if system is enabled AND program has valid data
    programRunning = (activeProgram >= 0 && systemEnabled && hasValidData);
    
    // Update button states based on program status
    // Removed buttons[4] and buttons[5] as they are no longer used
}

// Button callback implementations (removed onProgramUp and onProgramDown - now handled by handleButtonPress)

void ProgramsScreen::onProgramSelect() {
    if (!programsScreenInstance) return;
    
    // Show program start options dialog
    programsScreenInstance->showProgramStartDialog();
}

// Program start dialog methods
void ProgramsScreen::showProgramStartDialog() {
    showingStartDialog = true;
    needsRedraw = true;
    
    // Clear only the main content area, preserve status bar and navigation bar
    TFT_eSPI& tft = ui->getTFT();
    const TFT_Theme& theme = ui->getTheme();
    tft.fillRect(0, 20, TFT_WIDTH, TFT_HEIGHT - 50, theme.backgroundColor);
}

void ProgramsScreen::hideProgramStartDialog() {
    showingStartDialog = false;
    needsRedraw = true;
    
    // Clear only the main content area, preserve status bar and navigation bar
    TFT_eSPI& tft = ui->getTFT();
    const TFT_Theme& theme = ui->getTheme();
    tft.fillRect(0, 20, TFT_WIDTH, TFT_HEIGHT - 50, theme.backgroundColor);
    
    // Force full screen redraw to clear dialog artifacts
    ui->forceRedraw();
}

void ProgramsScreen::drawProgramStartDialog() {
    if (!showingStartDialog) return;
    
    TFT_eSPI& tft = ui->getTFT();
    const TFT_Theme& theme = ui->getTheme();
    
    // Draw dialog background - only in main content area
    int dialogX = 60;
    int dialogY = 80;
    int dialogWidth = 200;
    int dialogHeight = 100;
    
    // Draw dialog box
    tft.fillRoundRect(dialogX, dialogY, dialogWidth, dialogHeight, 5, theme.cardBackground);
    tft.drawRoundRect(dialogX, dialogY, dialogWidth, dialogHeight, 5, theme.borderColor);
    
    // Draw title
    tft.setTextColor(theme.textColor);
    tft.setTextSize(2);
    tft.setCursor(dialogX + 20, dialogY + 10);
    tft.print("Start Program");
    
    // Draw buttons
    // Start Now button
    tft.fillRoundRect(dialogX + 10, dialogY + 45, 80, 20, 3, theme.primaryColor);
    tft.drawRoundRect(dialogX + 10, dialogY + 45, 80, 20, 3, theme.borderColor);
    tft.setTextSize(1);
    tft.setCursor(dialogX + 20, dialogY + 50);
    tft.print("Start Now");
    
    // Schedule button
    tft.fillRoundRect(dialogX + 100, dialogY + 45, 80, 20, 3, theme.primaryColor);
    tft.drawRoundRect(dialogX + 100, dialogY + 45, 80, 20, 3, theme.borderColor);
    tft.setCursor(dialogX + 115, dialogY + 50);
    tft.print("Schedule");
    
    // Cancel button
    tft.fillRoundRect(dialogX + 55, dialogY + 70, 80, 20, 3, theme.errorColor);
    tft.drawRoundRect(dialogX + 55, dialogY + 70, 80, 20, 3, theme.borderColor);
    tft.setCursor(dialogX + 75, dialogY + 75);
    tft.print("Cancel");
}

void ProgramsScreen::handleStartDialogTouch(TouchPoint& touch) {
    int dialogX = 60;
    int dialogY = 80;
    
    // Check Start Now button
    if (touch.x >= dialogX + 10 && touch.x <= dialogX + 90 && 
        touch.y >= dialogY + 45 && touch.y <= dialogY + 65) {
        hideProgramStartDialog();
        startProgramImmediately();
        return;
    }
    
    // Check Schedule button
    if (touch.x >= dialogX + 100 && touch.x <= dialogX + 180 && 
        touch.y >= dialogY + 45 && touch.y <= dialogY + 65) {
        hideProgramStartDialog();
        showTimeScheduler();
        return;
    }
    
    // Check Cancel button
    if (touch.x >= dialogX + 55 && touch.x <= dialogX + 135 && 
        touch.y >= dialogY + 70 && touch.y <= dialogY + 90) {
        hideProgramStartDialog();
        return;
    }
}

// Time scheduling methods
void ProgramsScreen::showTimeScheduler() {
    // Clean up existing picker if any
    if (timePicker) {
        delete timePicker;
        timePicker = nullptr;
    }
    
    // Create time picker (format: HHMM, 0-2359)
    timePicker = new MultiDigitWheelPicker(ui, 0, 2359, 1, 1200); // Default to 12:00
    // Use default dimensions like settings picker - no custom setPickerDimensions call
    
    // Apply time constraints to limit digits to valid time values
    // 1st digit (hours tens): 0-2
    // 2nd digit (hours ones): 0-9 (or 0-3 if first digit is 2)
    // 3rd digit (minutes tens): 0-5
    // 4th digit (minutes ones): 0-9
    for (int i = 0; i < timePicker->getWheelCount(); i++) {
        if (timePicker->getDigitWheel(i)) {
            switch (i) {
                case 0: // Hours tens digit: 0-2
                    timePicker->getDigitWheel(i)->setCustomConstraints(0, 2);
                    break;
                case 1: // Hours ones digit: 0-9 (will be dynamically constrained)
                    timePicker->getDigitWheel(i)->setCustomConstraints(0, 9);
                    break;
                case 2: // Minutes tens digit: 0-5
                    timePicker->getDigitWheel(i)->setCustomConstraints(0, 5);
                    break;
                case 3: // Minutes ones digit: 0-9
                    timePicker->getDigitWheel(i)->setCustomConstraints(0, 9);
                    break;
            }
        }
    }
    
    timePicker->onValueSelected(onTimeSelected);
    timePicker->onCancelled(onTimeScheduleCancelled);
    
    showingTimeScheduler = true;
    needsRedraw = true;
    
            // Draw background for picker modal - cover full screen for modal effect
        TFT_eSPI& tft = ui->getTFT();
        const TFT_Theme& theme = ui->getTheme();
        tft.fillRect(0, 0, TFT_WIDTH, TFT_HEIGHT, theme.backgroundColor);
    
    // Show and validate the picker
    timePicker->show();
    timePicker->validateState();
    
    // Draw static elements first
    timePicker->drawStaticElements();
    
    // Force immediate draw of all wheels
    for (int i = 0; i < timePicker->getWheelCount(); i++) {
        if (timePicker->getDigitWheel(i)) {
            timePicker->getDigitWheel(i)->forceRedraw();
            timePicker->getDigitWheel(i)->draw();
        }
    }
}

void ProgramsScreen::hideTimeScheduler() {
    showingTimeScheduler = false;
    if (timePicker) {
        timePicker->hide();
        delete timePicker;
        timePicker = nullptr;
    }
    needsRedraw = true;
    
    // Force complete screen refresh to clear picker artifacts and restore navigation
    ui->clearScreen();
    ui->forceRedraw();
    
    // Ensure navigation bar is redrawn
    ui->drawNavigationBar();
}



// Program execution methods
void ProgramsScreen::startProgramImmediately() {
    if (selectedProgram < 0 || selectedProgram >= MAX_PROGRAMS) return;
    
    // Validate program data before attempting to start
    if (!validateProgramData(selectedProgram)) {
        ui->showError("Program has no valid temperature data");
        return;
    }
    
    // Allow time for screen to redraw after dialog close
    delay(100);
    
    // Fetch system status to get currentTempIndex and maxTempPoints
    HTTPClient http;
    http.begin("http://localhost/api/status");
    int httpCode = http.GET();
    int offset = 0;
    int pointsPerDay = 96;
    if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        DynamicJsonDocument statusDoc(1024);
        if (deserializeJson(statusDoc, response) == DeserializationError::Ok) {
            if (statusDoc.containsKey("currentTempIndex")) {
                offset = statusDoc["currentTempIndex"].as<int>();
            }
            if (statusDoc.containsKey("maxTempPoints")) {
                pointsPerDay = statusDoc["maxTempPoints"].as<int>();
            }
            if (offset >= pointsPerDay) offset = 0;
        }
    }
    http.end();
    
    // Start program at the next scheduled point (mirroring web UI logic)
    http.begin("http://localhost/api/loadProgram?id=" + String(selectedProgram) + "&offset=" + String(offset));
    httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        DynamicJsonDocument responseDoc(256);
        if (deserializeJson(responseDoc, response) == DeserializationError::Ok) {
            bool success = responseDoc["success"].as<bool>();
            if (success) {
                String message = "Program started: " + programNames[selectedProgram];
                ui->showSuccess(message);
                ui->forceRedraw();  // Force complete UI refresh
            } else {
                String error = responseDoc["error"].as<String>();
                ui->showError("Start failed: " + error);
            }
        }
    } else {
        ui->showError("Connection failed (HTTP " + String(httpCode) + ")");
    }
    http.end();
}


void ProgramsScreen::startProgramScheduled(int hour, int minute) {
    if (selectedProgram < 0 || selectedProgram >= MAX_PROGRAMS) return;
    
    // Validate program data before attempting to start
    if (!validateProgramData(selectedProgram)) {
        ui->showError("Program has no valid temperature data");
        return;
    }
    
    // Allow time for screen to redraw after time picker close
    delay(100);
    
    // Use existing API to get system status and calculate offset
    HTTPClient http;
    http.begin("http://localhost/api/status");
    
    int httpCode = http.GET();
    if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        DynamicJsonDocument statusDoc(1024);
        if (deserializeJson(statusDoc, response) == DeserializationError::Ok) {
            int pointsPerDay = statusDoc["maxTempPoints"] | 96;
            int interval = 1440 / pointsPerDay; // minutes per point
            int scheduleMinutes = hour * 60 + minute;
            int offset = scheduleMinutes / interval;
            
            // Snap to next available point if not exact (matches web interface logic)
            if (scheduleMinutes % interval != 0) offset++;
            if (offset >= pointsPerDay) offset = 0;
            
            // Use existing API to load program with calculated offset
            http.end();
            http.begin("http://localhost/api/loadProgram?id=" + String(selectedProgram) + "&offset=" + String(offset));
            
            httpCode = http.GET();
            if (httpCode == HTTP_CODE_OK) {
                String response = http.getString();
                DynamicJsonDocument responseDoc(256);
                if (deserializeJson(responseDoc, response) == DeserializationError::Ok) {
                    bool success = responseDoc["success"].as<bool>();
                    if (success) {
                        String timeStr = String(hour) + ":" + (minute < 10 ? "0" : "") + String(minute);
                        String message = "Scheduled: " + programNames[selectedProgram] + " @ " + timeStr;
                        ui->showSuccess(message);
                        ui->forceRedraw();  // Force complete UI refresh
                    } else {
                        String error = responseDoc["error"].as<String>();
                        ui->showError("Schedule failed: " + error);
                    }
                }
            } else {
                ui->showError("Connection failed (HTTP " + String(httpCode) + ")");
            }
        }
    } else {
        ui->showError("Failed to get system status");
    }
    
    http.end();
}

// Program creation/editing methods
void ProgramsScreen::showProgramCreateDialog() {
    showingCreateDialog = true;
    needsRedraw = true;
    
    // Initialize editing program data
    if (!editingTemps) {
        editingTemps = new float[maxTempPoints];
        // Initialize with a simple pattern - low at night, higher during day
        for (int i = 0; i < maxTempPoints; i++) {
            int hour = (i * 1440 / maxTempPoints) / 60; // Convert to hour of day
            if (hour >= 6 && hour <= 18) {
                editingTemps[i] = 200.0f; // Day temperature
            } else {
                editingTemps[i] = 100.0f; // Night temperature
            }
        }
        editingTemps[maxTempPoints - 1] = 0.0f; // Always end at 0
    }
    
    editingProgramName = "New Program";
    editingProgram = true;
    
    // Clear screen area
    TFT_eSPI& tft = ui->getTFT();
    const TFT_Theme& theme = ui->getTheme();
    tft.fillRect(0, 20, TFT_WIDTH, TFT_HEIGHT - 50, theme.backgroundColor);
}

void ProgramsScreen::hideProgramCreateDialog() {
    showingCreateDialog = false;
    editingProgram = false;
    needsRedraw = true;
    
    // Clean up editing data
    if (editingTemps) {
        delete[] editingTemps;
        editingTemps = nullptr;
    }
    
    // Clear screen
    TFT_eSPI& tft = ui->getTFT();
    const TFT_Theme& theme = ui->getTheme();
    tft.fillRect(0, 20, TFT_WIDTH, TFT_HEIGHT - 50, theme.backgroundColor);
    
    ui->forceRedraw();
}

void ProgramsScreen::drawProgramCreateDialog() {
    if (!showingCreateDialog) return;
    
    TFT_eSPI& tft = ui->getTFT();
    const TFT_Theme& theme = ui->getTheme();
    
    // Draw main dialog area
    tft.fillRect(10, 30, 300, 170, theme.cardBackground);
    tft.drawRect(10, 30, 300, 170, theme.borderColor);
    
    // Title
    tft.setTextColor(theme.textColor);
    tft.setTextSize(2);
    tft.setCursor(15, 35);
    tft.print("Create Program");
    
    // Program name
    tft.setTextSize(1);
    tft.setCursor(15, 55);
    tft.print("Name: ");
    tft.print(editingProgramName);
    
    // Show a few key temperature points
    tft.setCursor(15, 75);
    tft.print("Temps: Night ");
    tft.print((int)editingTemps[0]);
    tft.print("C, Day ");
    tft.print((int)editingTemps[maxTempPoints/2]);
    tft.print("C");
    
    // Draw simple temperature preview graph
    int graphX = 15, graphY = 95, graphW = 280, graphH = 60;
    tft.drawRect(graphX, graphY, graphW, graphH, theme.borderColor);
    
    // Draw temperature line
    float maxTemp = 250.0f;
    for (int i = 1; i < 24; i++) { // Sample 24 hours
        int tempIndex = (i * maxTempPoints) / 24;
        int x1 = graphX + ((i-1) * graphW) / 24;
        int x2 = graphX + (i * graphW) / 24;
        int y1 = graphY + graphH - (editingTemps[tempIndex-maxTempPoints/24] * graphH / maxTemp);
        int y2 = graphY + graphH - (editingTemps[tempIndex] * graphH / maxTemp);
        tft.drawLine(x1, y1, x2, y2, theme.primaryColor);
    }
    
    // Control buttons
    // Create button
    tft.fillRoundRect(20, 165, 80, 25, 3, theme.successColor);
    tft.drawRoundRect(20, 165, 80, 25, 3, theme.borderColor);
    tft.setTextColor(theme.textColor);
    tft.setCursor(35, 173);
    tft.print("Create");
    
    // Edit Temps button
    tft.fillRoundRect(110, 165, 80, 25, 3, theme.primaryColor);
    tft.drawRoundRect(110, 165, 80, 25, 3, theme.borderColor);
    tft.setCursor(120, 173);
    tft.print("Edit Temps");
    
    // Cancel button
    tft.fillRoundRect(200, 165, 80, 25, 3, theme.errorColor);
    tft.drawRoundRect(200, 165, 80, 25, 3, theme.borderColor);
    tft.setCursor(225, 173);
    tft.print("Cancel");
}

void ProgramsScreen::handleCreateDialogTouch(TouchPoint& touch) {
    if (!showingCreateDialog || !touch.isPressed) return;
    
    // Check button touches
    if (touch.y >= 165 && touch.y <= 190) {
        if (touch.x >= 20 && touch.x <= 100) {
            // Create button
            createBasicProgram();
        } else if (touch.x >= 110 && touch.x <= 190) {
            // Edit Temps button - show temp picker for midday temperature
            showTempPicker(maxTempPoints / 2);
        } else if (touch.x >= 200 && touch.x <= 280) {
            // Cancel button
            hideProgramCreateDialog();
        }
    }
}

void ProgramsScreen::createBasicProgram() {
    // Find first empty slot
    int emptySlot = -1;
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        if (programNames[i].length() == 0) {
            emptySlot = i;
            break;
        }
    }
    
    if (emptySlot == -1) {
        ui->showError("No empty program slots available");
        return;
    }
    
    // Save the program
    saveProgramToServer();
}

void ProgramsScreen::saveProgramToServer() {
    // Find first empty slot
    int emptySlot = -1;
    for (int i = 0; i < MAX_PROGRAMS; i++) {
        if (programNames[i].length() == 0) {
            emptySlot = i;
            break;
        }
    }
    
    if (emptySlot == -1) {
        ui->showError("No empty program slots");
        return;
    }
    
    // Format the program data using the same trimming logic as JavaScript
    TrimmedProgramData trimmed = trimProgramData(editingTemps, maxTempPoints);
    
    // Create JSON payload
    HTTPClient http;
    http.begin("http://localhost/api/saveProgram");
    http.addHeader("Content-Type", "application/json");
    
    DynamicJsonDocument doc(2048);
    doc["index"] = emptySlot;
    doc["name"] = editingProgramName;
    doc["description"] = "Created on TFT";
    
    JsonArray temps = doc.createNestedArray("temps");
    for (size_t i = 0; i < trimmed.temps.size(); i++) {
        temps.add(trimmed.temps[i]);
    }
    
    String jsonBody;
    serializeJson(doc, jsonBody);
    
    Serial.print("TFT: Saving program to slot ");
    Serial.print(emptySlot);
    Serial.print(" with ");
    Serial.print(trimmed.temps.size());
    Serial.println(" temperature points");
    
    int httpCode = http.POST(jsonBody);
    if (httpCode == HTTP_CODE_OK) {
        String response = http.getString();
        DynamicJsonDocument responseDoc(256);
        if (deserializeJson(responseDoc, response) == DeserializationError::Ok) {
            bool success = responseDoc["success"].as<bool>();
            if (success) {
                ui->showSuccess("Program created successfully!");
                hideProgramCreateDialog();
                
                // Update local data immediately for UI responsiveness
                programNames[emptySlot] = editingProgramName;
                for (int i = 0; i < maxTempPoints; i++) {
                    if (i < (int)trimmed.temps.size()) {
                        programTemps[emptySlot][i] = trimmed.temps[i];
                    } else {
                        programTemps[emptySlot][i] = 0.0f;
                    }
                }
                
                selectedProgram = emptySlot; // Select the newly created program
            } else {
                String error = responseDoc["error"].as<String>();
                ui->showError("Save failed: " + error);
            }
        }
    } else {
        ui->showError("Failed to save (HTTP " + String(httpCode) + ")");
    }
    
    http.end();
}

// Temperature editing methods
void ProgramsScreen::showTempPicker(int tempIndex) {
    if (tempIndex < 0 || tempIndex >= maxTempPoints) return;
    
    editingTempIndex = tempIndex;
    showingTempPicker = true;
    
    // Clean up existing picker
    if (tempPicker) {
        delete tempPicker;
        tempPicker = nullptr;
    }
    
    // Create temperature picker (0-1200°C range)
    tempPicker = new MultiDigitWheelPicker(ui, 0, 1200, 1, (int)editingTemps[tempIndex]);
    tempPicker->onValueSelected(onTempSelected);
    tempPicker->onCancelled(onTempCancelled);
    
    // Show picker
    tempPicker->show();
    needsRedraw = true;
}

void ProgramsScreen::hideTempPicker() {
    showingTempPicker = false;
    if (tempPicker) {
        tempPicker->hide();
        delete tempPicker;
        tempPicker = nullptr;
    }
    needsRedraw = true;
    ui->clearScreen();
    ui->forceRedraw();
}

// Temperature picker callbacks
void ProgramsScreen::onTempSelected(float value) {
    if (!programsScreenInstance || !programsScreenInstance->editingTemps) return;
    
    int tempIndex = programsScreenInstance->editingTempIndex;
    if (tempIndex >= 0 && tempIndex < maxTempPoints) {
        programsScreenInstance->editingTemps[tempIndex] = value;
        
        // Update day/night pattern based on the edited value
        int hour = (tempIndex * 1440 / maxTempPoints) / 60;
        bool isDayTime = (hour >= 6 && hour <= 18);
        
        // Apply similar temperature to similar time periods
        for (int i = 0; i < maxTempPoints; i++) {
            int iHour = (i * 1440 / maxTempPoints) / 60;
            bool iIsDayTime = (iHour >= 6 && iHour <= 18);
            if (iIsDayTime == isDayTime) {
                programsScreenInstance->editingTemps[i] = value;
            }
        }
        
        // Ensure last point is always 0
        programsScreenInstance->editingTemps[maxTempPoints - 1] = 0.0f;
    }
    
    programsScreenInstance->hideTempPicker();
}

void ProgramsScreen::onTempCancelled() {
    if (!programsScreenInstance) return;
    programsScreenInstance->hideTempPicker();
}

// Time picker callbacks
void ProgramsScreen::onTimeSelected(float value) {
    if (!programsScreenInstance) return;
    
    // Parse HHMM format
    int timeValue = (int)value;
    int hour = timeValue / 100;
    int minute = timeValue % 100;
    
    // Validate time
    if (hour >= 0 && hour <= 23 && minute >= 0 && minute <= 59) {
        // Hide the picker and clean up
        programsScreenInstance->showingTimeScheduler = false;
        if (programsScreenInstance->timePicker) {
            programsScreenInstance->timePicker->hide();
        }
        
        // Force complete screen refresh - clear full screen and restore navigation
        programsScreenInstance->ui->clearScreen();
        programsScreenInstance->needsRedraw = true;
        programsScreenInstance->ui->forceRedraw();
        
        // Small delay to ensure screen is fully redrawn
        delay(50);
        
        // Ensure navigation bar is redrawn
        programsScreenInstance->ui->drawNavigationBar();
        
        // Start the program
        programsScreenInstance->startProgramScheduled(hour, minute);
    } else {
        programsScreenInstance->ui->showError("Invalid time format");
    }
}

void ProgramsScreen::onTimeScheduleCancelled() {
    if (!programsScreenInstance) return;
    
    // Hide the picker without saving and clean up
    programsScreenInstance->showingTimeScheduler = false;
    if (programsScreenInstance->timePicker) {
        programsScreenInstance->timePicker->hide();
    }
    
    // Force complete screen refresh - clear full screen and restore navigation
    programsScreenInstance->ui->clearScreen();
    programsScreenInstance->needsRedraw = true;
    programsScreenInstance->ui->forceRedraw();
    
    // Small delay to ensure screen is fully redrawn
    delay(50);
    
    // Ensure navigation bar is redrawn
    programsScreenInstance->ui->drawNavigationBar();
    
    // Show cancellation message after screen is refreshed
    programsScreenInstance->ui->showMessage("Time selection cancelled", programsScreenInstance->ui->getTheme().warningColor, 2000);
}

    // Removed onStartProgram and onStopProgram functions - start/stop buttons have been removed

// Check if there are active animations (for adaptive update frequency)
bool ProgramsScreen::hasActiveAnimations() {
    if (timePicker && showingTimeScheduler) {
        // Check if any digit wheels are animating
        for (int i = 0; i < timePicker->getWheelCount(); i++) {
            if (timePicker->getDigitWheel(i) && timePicker->getDigitWheel(i)->isAnimating()) {
                return true;
            }
        }
    }
    return false;
}

// Check if time picker is active (for modal blocking)
bool ProgramsScreen::hasActiveTimePicker() {
    return timePicker && showingTimeScheduler && timePicker->isVisible();
}

// Draw active time picker wheels without full screen redraw
void ProgramsScreen::drawActiveTimePicker() {
    if (timePicker && showingTimeScheduler && timePicker->isVisible()) {
        // Only draw wheels that actually need redrawing
        for (int i = 0; i < timePicker->getWheelCount(); i++) {
            if (timePicker->getDigitWheel(i) && timePicker->getDigitWheel(i)->needsRedraw()) {
                timePicker->getDigitWheel(i)->draw();
            }
        }
    }
} 