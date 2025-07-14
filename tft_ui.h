#ifndef TFT_UI_H
#define TFT_UI_H

#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <HTTPClient.h>
#include <WiFi.h>
#include <ArduinoJson.h>

// Screen dimensions
#define TFT_WIDTH 320
#define TFT_HEIGHT 240

// Touchscreen pins - Correct configuration
#define XPT2046_IRQ 36   // T_IRQ
#define XPT2046_CS 33    // T_CS
#define XPT2046_MOSI 32  // T_DIN
#define XPT2046_MISO 39  // T_OUT
#define XPT2046_CLK 25   // T_CLK

// Note: These pins match the T_IRQ, T_CS, T_DIN, T_OUT, T_CLK labels on your TFT board

// Touch calibration values
#define TOUCH_X_MIN 200
#define TOUCH_X_MAX 3700
#define TOUCH_Y_MIN 240
#define TOUCH_Y_MAX 3800

// Button dimensions optimized for resistive touch
#define BTN_HEIGHT 40
#define BTN_WIDTH_SMALL 60
#define BTN_WIDTH_MEDIUM 80
#define BTN_WIDTH_LARGE 120

// UI constants
#define STATUS_BAR_HEIGHT 20
#define NAV_BAR_HEIGHT 30
#define CARD_MARGIN 8
#define CARD_PADDING 8
#define SCROLL_THRESHOLD 5
#define MAX_PROGRAMS 10

// Legacy button constants for compatibility
#define BUTTON_HEIGHT BTN_HEIGHT
#define BUTTON_WIDTH_SMALL BTN_WIDTH_SMALL
#define BUTTON_WIDTH_MEDIUM BTN_WIDTH_MEDIUM
#define BUTTON_WIDTH_LARGE BTN_WIDTH_LARGE

// Colors (RGB 565 format)
#define COLOR_WHITE 0xFFFF
#define COLOR_BLACK 0x0000
#define COLOR_GRAY 0x8410
#define COLOR_RED 0xF800
#define COLOR_GREEN 0x07E0
#define COLOR_BLUE 0x001F
#define COLOR_YELLOW 0xFFE0
#define COLOR_ORANGE 0xFD20

// Theme structure
struct TFT_Theme {
    uint16_t primaryColor;
    uint16_t backgroundColor;
    uint16_t cardBackground;
    uint16_t textColor;
    uint16_t borderColor;
    uint16_t highlightColor;
    uint16_t successColor;
    uint16_t warningColor;
    uint16_t errorColor;
    uint16_t disabledColor;
    bool isDarkMode;
};

// Screen types
enum ScreenType {
    SCREEN_MAIN = 0,
    SCREEN_SETTINGS = 1,
    SCREEN_PROGRAMS = 2,
    SCREEN_CHARTS = 3,
    SCREEN_SETUP = 4,
    SCREEN_WIFI_SETUP = 5,
    SCREEN_COUNT
};

// Button states
enum ButtonState {
    BTN_NORMAL = 0,
    BTN_PRESSED = 1,
    BTN_DISABLED = 2,
    BTN_ACTIVE = 3
};

// Touch point structure
struct TouchPoint {
    int x;
    int y;
    bool isPressed;
    unsigned long timestamp;
};

// UI components
struct TFT_Button {
    int x, y, width, height;
    String text;
    ButtonState state;
    bool visible;
    uint16_t bgColor;
    uint16_t textColor;
    uint16_t borderColor;
    void (*onPress)();
    int id;
    unsigned long pressedTime;  // Timestamp when button was pressed
    unsigned long pressDuration; // How long to stay pressed (default 100ms)
};

struct TFT_Text {
    int x, y;
    String text;
    uint8_t size;
    uint16_t color;
    bool visible;
    bool centered;
};

struct TFT_ProgressBar {
    int x, y, width, height;
    float value;
    float maxValue;
    uint16_t fillColor;
    uint16_t bgColor;
    uint16_t borderColor;
    bool visible;
};

struct ChartPoint {
    float x, y;
    uint16_t color;
};

struct TFT_Chart {
    int x, y, width, height;
    float minX, maxX, minY, maxY;
    ChartPoint* points;
    int pointCount;
    int maxPoints;
    uint16_t lineColor;
    uint16_t bgColor;
    uint16_t gridColor;
    bool visible;
    bool showGrid;
};

// Base screen class
class TFT_Screen {
public:
    virtual ~TFT_Screen() {}
    virtual void init() = 0;
    virtual void update() = 0;
    virtual void draw() = 0;
    virtual void handleTouch(TouchPoint& touch) = 0;
    virtual void onShow() {}
    virtual void onHide() {}
    
    // Common screen elements
    int buttonCount = 0;
    TFT_Button* buttons = nullptr;
    int textCount = 0;
    TFT_Text* texts = nullptr;
    bool needsRedraw = true;
};

// Main TFT UI Manager class
class TFT_UI {
public:
    TFT_UI();
    ~TFT_UI();
    
    // Initialization
    void init();
    void begin();
    
    // Theme management
    void loadTheme();
    void applyTheme();
    uint16_t hexToColor565(const String& hex);
    void checkForThemeUpdates(); // Periodic theme sync with backend
    
    // Screen management
    void setScreen(ScreenType screen);
    void showScreen(ScreenType screen);
    ScreenType getCurrentScreen() { return currentScreen; }
    
    // Update and rendering
    void update();
    void forceRedraw();
    
    // Touch handling
    void handleTouch();
    TouchPoint getTouch();
    bool isTouchInButton(const TouchPoint& touch, const TFT_Button& button);
    void calibrateTouch();
    void loadTouchCalibration();
    void saveTouchCalibration();
    void testTouchSPI();
    void reinitTouchscreen();
    bool testTouchHardware();
    
    // Navigation methods
    void handleNavigationTouch(const TouchPoint& touch);
    void setScreenWithAnimation(ScreenType screen, bool animate = true);
    String getCurrentScreenTitle();
    void showNavigationHints();
    void handleBackButton();
    void updateWithNavigation();
    void handleKeyboardShortcuts(char key);
    
    // Modal checking
    bool hasActiveModal();
    
    // Drawing utilities
    void drawButton(const TFT_Button& button);
    void drawText(const TFT_Text& text);
    void drawProgressBar(const TFT_ProgressBar& bar);
    void drawChart(const TFT_Chart& chart);
    void drawCard(int x, int y, int width, int height, const String& title);
    void drawStatusBar();
    void drawNavigationBar();
    
    // Utility functions
    void clearScreen();
    void showMessage(const String& message, uint16_t color = 0, int duration = 2000);
    void showError(const String& error);
    void showSuccess(const String& message);
    
    // Selective drawing methods to reduce flickering
    void drawSelectiveScreen();
    
    // Anti-flickering utilities
    bool hasTextChanged(const String& newText, const String& oldText);
    void drawOptimizedText(int x, int y, const String& newText, String& oldText, 
                          uint16_t color, uint8_t size, bool clearBackground = true);
    void drawOptimizedButton(TFT_Button& button, ButtonState previousState);
    void drawCardOptimized(int x, int y, int width, int height, const String& title, 
                          bool forceRedraw = true);
    
    // Adaptive update frequency
    void adjustUpdateFrequency();
    void forceHighFrequencyUpdates(int durationMs = 1000);
    bool shouldUseHighFrequency();
    
    // Data access
    TFT_Theme& getTheme() { return theme; }
    bool isThemeLoaded() { return themeLoaded; }
    
    // Hardware access
    TFT_eSPI& getTFT() { return tft; }
    XPT2046_Touchscreen& getTouchscreen() { return touchscreen; }
    
    // Get current drawing target (always main TFT)
    TFT_eSPI* getDrawingTFT() { return &tft; }
    
    // Color conversion for drawing operations (simplified)
    uint32_t getDrawingColor(uint16_t color565) { return color565; }
    
private:
    // Hardware
    TFT_eSPI tft;
    SPIClass touchscreenSPI;
    XPT2046_Touchscreen touchscreen;
    
    // Small region buffers for frequently changing elements  
    TFT_eSprite* tempDisplayBuffer = nullptr;
    TFT_eSprite* statusBarBuffer = nullptr;
    TFT_eSprite* navBarBuffer = nullptr;
    bool smallBuffersEnabled = true;
    
    // Theme
    TFT_Theme theme;
    bool themeLoaded = false;
    
    // Screen management
    ScreenType currentScreen = SCREEN_MAIN;
    TFT_Screen* screens[SCREEN_COUNT];
    bool screenNeedsRedraw = true;
    
    // Touch handling
    TouchPoint lastTouch;
    unsigned long lastTouchTime = 0;
    unsigned long touchDebounceTime = 100;
    
    // Message display (disabled - no popup notifications)
    
    // Update timing
    unsigned long lastUpdate = 0;
    unsigned long updateInterval = 50; // 20 FPS
    unsigned long highFrequencyUntil = 0; // For temporary high frequency updates
    
    // Touch calibration
    int touchXMin = 300;
    int touchXMax = 3800;
    int touchYMin = 300;
    int touchYMax = 3800;
    bool touchCalibrated = false;
    
    // Initialize screens
    void initializeScreens();
    void deleteScreens();
    
    // HTTP client for theme loading
    HTTPClient httpClient;
    
    // Small region buffer methods
    void initSmallBuffers();
    void cleanupSmallBuffers();
    void drawBufferedStatusBar();
    void drawBufferedNavBar();
    void drawBufferedTempDisplay(int x, int y, int width, int height, const String& text);
    
    // Default theme colors
    void setDefaultTheme();
};

// Forward declarations
class TFT_UI;

// Individual digit wheel for multi-digit picker
// Enhanced DigitWheel class with contextual constraints
class DigitWheel {
public:
    DigitWheel(TFT_UI* ui, int x, int y, int width, int height, int minDigit, int maxDigit);
    ~DigitWheel();
    
    void update();
    void draw();
    void handleTouch(TouchPoint& touch);
    
    void setValue(int value);
    int getValue() const { return selectedValue; }
    
    void forceRedraw() { wheelNeedsRedraw = true; }
    bool needsRedraw() const { return wheelNeedsRedraw; }
    bool isAnimating() const { return abs(scrollVelocity) > 0.1f || abs(targetScrollOffset - scrollOffset) > 1.0f; }
    
    // Enhanced constraint methods
    void setTimeConstraints(int position, int maxValue = 235959);
    void setPercentageConstraints(int position, int maxValue = 100);
    void setCustomConstraints(int minVal, int maxVal);
    void updateDynamicConstraints(int firstHourDigit = 0, int hundredsDigit = 0, int tensDigit = 0);
    
private:
    friend class MultiDigitWheelPicker; // Allow access to private members
    
    TFT_UI* ui;
    int wheelX, wheelY, wheelWidth, wheelHeight;
    int minDigit, maxDigit;
    int selectedValue;
    
    // Scrolling and animation
    float scrollOffset;
    float targetScrollOffset;
    float scrollVelocity;
    bool isDragging;
    int itemHeight;
    unsigned long lastAnimationTime;
    
    // Drawing
    bool wheelNeedsRedraw = true;
    
    // Constraint context
    enum ConstraintType {
        CONSTRAINT_NONE,
        CONSTRAINT_TIME,
        CONSTRAINT_PERCENTAGE
    } constraintType = CONSTRAINT_NONE;
    
    int constraintPosition = 0;
    int constraintMaxValue = 0;
    
    // Drawing and animation methods
    void drawWheelItems();
    void drawWheelItem(int digit, int itemY, bool isSelected);
    void updateScrollPosition();
    void snapToNearest();
    int getDigitAtPosition(int y);
    
    // Constraint calculation methods
    void calculateConstraintsForDigit();
    bool isValidDigitInContext(int digit);
};

// Multi-digit wheel picker for intuitive number input
class MultiDigitWheelPicker {
public:
    MultiDigitWheelPicker(TFT_UI* ui, float minValue, float maxValue, float step, float currentValue = 0);
    ~MultiDigitWheelPicker();
    
    void show();
    void hide();
    void update();
    void draw();
    void handleTouch(TouchPoint& touch);
    
    float getValue() const;
    void setValue(float value);
    
    bool isVisible() const { return visible; }
    bool needsRedraw() const;
    void forceRedraw();
    void validateState();
    
    void onValueSelected(void (*callback)(float));
    void onCancelled(void (*callback)());
    
    // Debug and access methods
    int getWheelCount() const { return wheelCount; }
    DigitWheel* getDigitWheel(int index) const { return (index >= 0 && index < wheelCount) ? digitWheels[index] : nullptr; }
    
    // Static drawing methods for external use
    void drawStaticElements();
    bool isActiveAndVisible() const;
    
    void setPickerDimensions(int x, int y, int width, int height);
    
private:
    TFT_UI* ui;
    bool visible;
    bool pickerNeedsRedraw = true;
    
    // Value constraints
    float minValue, maxValue, step;
    int decimalPlaces;
    int wheelCount;
    int decimalWheelIndex;
    
    // UI layout
    int pickerX, pickerY, pickerWidth, pickerHeight;
    
    // Adaptive sizing
    int adaptiveWheelWidth;
    int adaptiveWheelSpacing;
    
    // Digit wheels
    DigitWheel* digitWheels[6];
    
    // Callbacks
    void (*onSelectedCallback)(float);
    void (*onCancelCallback)();
    
    // Time picker flag
    bool isTimePicker;
    
    // Internal methods
    void createDigitWheels();
    void updateDynamicConstraints();
    void drawBackground();
    void drawButtons();
    void drawDecimalPoint();
    void handleButtonTouch(TouchPoint& touch);
    void parseValue(float value, int* digits);
    float combineDigits(int* digits) const;
};

class MainScreen : public TFT_Screen {
public:
    MainScreen(TFT_UI* ui);
    ~MainScreen();
    
    void init() override;
    void update() override;
    void draw() override;
    void handleTouch(TouchPoint& touch) override;
    void onShow() override;
    
    // Public accessor methods for static callbacks
    TFT_UI* getUI() { return ui; }
    String formatTempForCallback(float temp) { return formatTemperature(temp); }
    
private:
    TFT_UI* ui;
    
    // Chart data
    float* chartData;
    float* targetTempData; // Target temperature tracking over time
    int chartDataSize;
    int chartIndex;
    unsigned long lastChartUpdate;
    unsigned long lastSecondUpdate; // Timer for 1-second updates
    
    // Change tracking variables for anti-flashing (from V20 technique)
    float lastDisplayedCurrentTemp;
    float lastDisplayedTargetTemp;
    bool lastDisplayedFurnaceStatus;
    bool lastDisplayedSystemEnabled;
    String lastTimeString;
    
    // UI components
    TFT_Chart tempChart;
    TFT_ProgressBar tempBar;
    
    // Drawing methods
    void drawTemperatureCard();
    void drawTemperatureCardBuffered();
    void drawControlCard();
    void drawStatusCard();
    void updateChart();
    
    // Selective drawing methods (V20 anti-flashing technique)
    void drawSelectiveText(int index);
    void drawSelectiveButton(int index);
    void drawSelectiveChart();
    void drawSelectiveProgressBar();

    // Helper methods
    String formatTemperature(float temp);
};

class SettingsScreen : public TFT_Screen {
public:
    SettingsScreen(TFT_UI* ui);
    ~SettingsScreen();
    
    void init() override;
    void update() override;
    void draw() override;
    void handleTouch(TouchPoint& touch) override;
    void onShow() override;
    
    // Animation checking for adaptive update frequency
    bool hasActiveAnimations();
    
    // Check if number picker is active (for modal blocking)
    bool hasActiveNumberPicker();
    
    // Draw active number picker wheels without full screen redraw
    void drawActiveNumberPicker();
    
private:
    TFT_UI* ui;
    int scrollOffset = 0;
    int selectedSetting = 0;
    
    // Multi-digit wheel picker
    MultiDigitWheelPicker* numberPicker;
    bool showingNumberPicker;
    
    // Settings management methods
    void initializeSettings();
    void updateSettingsValues();
    void forceUpdateSettingsValues();
    void handleButtonPress(int buttonIndex);
    void handleSettingEdit(int settingIndex);
    void toggleSetting(int settingIndex);
    void editSetting(int settingIndex);
    float getCurrentSettingValue(int settingIndex);
    void setSettingValue(int settingIndex, float value);
    void performAction(int settingIndex);
    bool validateExternalVariables();
    
    // Number picker callbacks
    static void onNumberSelected(float value);
    static void onNumberCancelled();
    
    // Button callbacks
    static void onBackPress();
    static void onPwmToggle();
    static void onPidToggle();
    static void onTimeSync();
    static void onSaveSettings();
    
    void drawSettingsCard(int index, const String& title, const String& value, bool selected);
    void drawScrollIndicator();
    void handleScrolling(TouchPoint& touch);
    void saveCurrentSettings();
    

};

class ProgramsScreen : public TFT_Screen {
public:
    ProgramsScreen(TFT_UI* ui);
    ~ProgramsScreen();
    
    void init() override;
    void update() override;
    void draw() override;
    void handleTouch(TouchPoint& touch) override;
    
    // Animation checking for adaptive update frequency
    bool hasActiveAnimations();
    
    // Check if time picker is active (for modal blocking)
    bool hasActiveTimePicker();
    
    // Draw active time picker wheels without full screen redraw
    void drawActiveTimePicker();
    
private:
    TFT_UI* ui;
    int selectedProgram = 0;
    bool programRunning = false;
    int scrollOffset = 0; // Add scrolling support
    
    // Temperature display tracking for selective drawing
    String lastCurrentTempStr = "";
    String lastTargetTempStr = "";
    
    // Program start dialog state
    bool showingStartDialog = false;
    bool showingTimeScheduler = false;
    
    // Program creation/editing state
    bool showingCreateDialog = false;
    bool editingProgram = false;
    float* editingTemps = nullptr;
    String editingProgramName = "";
    
    // Time picker for scheduling
    MultiDigitWheelPicker* timePicker = nullptr;
    
    // Temperature picker for program creation
    MultiDigitWheelPicker* tempPicker = nullptr;
    bool showingTempPicker = false;
    int editingTempIndex = 0;
    
    // Button callbacks
    static void onProgramSelect();
    
    void drawProgramList();
    void drawProgramPreview();
    void drawProgramControls();
    void updateProgramStatus();
    void drawScrollIndicator();
    void handleButtonPress(int buttonIndex);
    
    // Program start dialog methods
    void showProgramStartDialog();
    void hideProgramStartDialog();
    void drawProgramStartDialog();
    void handleStartDialogTouch(TouchPoint& touch);
    
    // Program creation/editing methods
    void showProgramCreateDialog();
    void hideProgramCreateDialog();
    void drawProgramCreateDialog();
    void handleCreateDialogTouch(TouchPoint& touch);
    void createBasicProgram();
    void saveProgramToServer();
    
    // Temperature editing methods
    void showTempPicker(int tempIndex);
    void hideTempPicker();
    static void onTempSelected(float value);
    static void onTempCancelled();
    
    // Time scheduling methods
    void showTimeScheduler();
    void hideTimeScheduler();
    void drawTimeScheduler();
    void handleTimeSchedulerTouch(TouchPoint& touch);
    
    // Program execution methods
    void startProgramImmediately();
    void startProgramScheduled(int hour, int minute);
    
    // Time picker callbacks
    static void onTimeSelected(float value);
    static void onTimeScheduleCancelled();
    
    // Selective drawing method to prevent temperature text artifacts
    void drawSelectiveTemperature(int x, int y, const String& newText, String& oldText, uint32_t textColor);
};

class ChartsScreen : public TFT_Screen {
public:
    ChartsScreen(TFT_UI* ui);
    ~ChartsScreen();
    
    void init() override;
    void update() override;
    void draw() override;
    void handleTouch(TouchPoint& touch) override;
    void onShow() override;
    
    // Public members for chart display
    float minTempDisplay;
    float maxTempDisplay;
    
    // Chart data management
    void updateChartData();
    
private:
    TFT_UI* ui;
    
    // Chart settings
    int chartStartHour;
    int chartEndHour;
    
    // Main 24-hour schedule chart
    TFT_Chart scheduleChart;
    
    // Theme tracking for chart color updates
    bool themeInitialized = false;
    bool prevThemeIsDark = false;
    
    // Private chart drawing methods
    void drawCurrentTimeIndicator();
    void drawTimeAxis();
};

class WiFiSetupScreen : public TFT_Screen {
public:
    WiFiSetupScreen(TFT_UI* ui);
    ~WiFiSetupScreen();
    
    void init() override;
    void update() override;
    void draw() override;
    void handleTouch(TouchPoint& touch) override;
    void onShow() override;
    
private:
    TFT_UI* ui;
    
    // Skip callback
    static void onSkip();
};

// Global UI instance
extern TFT_UI tftUI;

// Global program data
extern String programNames[MAX_PROGRAMS];

// Utility functions
String formatTime(unsigned long timestamp);
String formatTemperature(float temp);
String formatDuration(unsigned long duration);

// External functions from main firmware
extern String getCurrentTime();

// External WiFi variables for status display
extern String ap_password;
extern bool ap_active;
extern bool wifiConnected;

// External time variables
extern bool useManualTime;

// External hardware pins from config.h
#include "config.h"

#endif // TFT_UI_H 