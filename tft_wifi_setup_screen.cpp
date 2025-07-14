#include "tft_ui.h"

// Static instance for callbacks
static WiFiSetupScreen* wifiSetupScreenInstance = nullptr;

// Constructor
WiFiSetupScreen::WiFiSetupScreen(TFT_UI* ui) : ui(ui) {
    wifiSetupScreenInstance = this;
}

// Destructor
WiFiSetupScreen::~WiFiSetupScreen() {
    // No dynamic memory to clean up
}

// Initialize screen
void WiFiSetupScreen::init() {
    // Setup button for "Tap to skip"
    buttonCount = 1;
    buttons = new TFT_Button[buttonCount];
    
    // Skip button - full width at bottom
    buttons[0].x = 20;
    buttons[0].y = TFT_HEIGHT - 70;  // 70px from bottom
    buttons[0].width = TFT_WIDTH - 40;  // Full width with margins
    buttons[0].height = 40;
    buttons[0].text = "Tap to skip";
    buttons[0].state = BTN_NORMAL;
    buttons[0].visible = true;
    buttons[0].bgColor = ui->getTheme().primaryColor;
    buttons[0].textColor = ui->getTheme().textColor;
    buttons[0].borderColor = ui->getTheme().borderColor;
    buttons[0].onPress = onSkip;
    buttons[0].id = 0;
    buttons[0].pressedTime = 0;
    buttons[0].pressDuration = 100;
    
    // Setup text elements
    textCount = 3;
    texts = new TFT_Text[textCount];
    
    // Line 1: "Connect to Furnace_Control WiFi"
    texts[0].x = TFT_WIDTH / 2;
    texts[0].y = 80;
    texts[0].text = "Connect to";
    texts[0].size = 2;
    texts[0].color = ui->getTheme().textColor;
    texts[0].visible = true;
    texts[0].centered = true;
    
    // Line 2: WiFi name
    texts[1].x = TFT_WIDTH / 2;
    texts[1].y = 110;
    texts[1].text = "Furnace_Control WiFi";
    texts[1].size = 2;
    texts[1].color = ui->getTheme().primaryColor;
    texts[1].visible = true;
    texts[1].centered = true;
    
    // Line 3: Password info - will be updated with actual password
    texts[2].x = TFT_WIDTH / 2;
    texts[2].y = 150;
    texts[2].text = "Password: loading...";
    texts[2].size = 2;
    texts[2].color = ui->getTheme().textColor;
    texts[2].visible = true;
    texts[2].centered = true;
    
    needsRedraw = true;
}

// Update screen data
void WiFiSetupScreen::update() {
    // Get actual AP password from wifi_manager
    extern String ap_password;
    String passwordText = "Password: " + (ap_password.length() > 0 ? ap_password : "generating...");
    
    // Update password text if it changed
    if (texts[2].text != passwordText) {
        texts[2].text = passwordText;
        needsRedraw = true;
    }
    
    // Check for theme changes
    static bool themeInitialized = false;
    static bool prevThemeIsDark = false;
    bool currentThemeIsDark = ui->getTheme().isDarkMode;
    
    if (!themeInitialized || currentThemeIsDark != prevThemeIsDark) {
        // Theme changed - update colors
        themeInitialized = true;
        prevThemeIsDark = currentThemeIsDark;
        needsRedraw = true;
        
        // Update text colors
        texts[0].color = ui->getTheme().textColor;
        texts[1].color = ui->getTheme().primaryColor;
        texts[2].color = ui->getTheme().textColor;
        
        // Update button colors
        buttons[0].bgColor = ui->getTheme().primaryColor;
        buttons[0].textColor = ui->getTheme().textColor;
        buttons[0].borderColor = ui->getTheme().borderColor;
    }
}

// Draw screen
void WiFiSetupScreen::draw() {
    if (!needsRedraw) return;
    
    // Clear main content area
    const TFT_Theme& theme = ui->getTheme();
    ui->getTFT().fillRect(0, 20, TFT_WIDTH, TFT_HEIGHT - 50, theme.backgroundColor);
    
    // Draw WiFi icon or card background
    ui->drawCard(20, 40, TFT_WIDTH - 40, 140, "WiFi Setup");
    
    // Draw text elements
    for (int i = 0; i < textCount; i++) {
        if (texts[i].centered) {
            // Calculate centered position
            int textWidth = texts[i].text.length() * 6 * texts[i].size;
            int centeredX = (TFT_WIDTH - textWidth) / 2;
            
            ui->getTFT().setTextSize(texts[i].size);
            ui->getTFT().setTextColor(texts[i].color);
            ui->getTFT().setCursor(centeredX, texts[i].y);
            ui->getTFT().print(texts[i].text);
        } else {
            ui->drawText(texts[i]);
        }
    }
    
    // Draw button
    ui->drawButton(buttons[0]);
    
    needsRedraw = false;
}

// Handle touch input
void WiFiSetupScreen::handleTouch(TouchPoint& touch) {
    // Skip navigation bar touches
    if (touch.y > TFT_HEIGHT - 30) {
        return;
    }
    
    // Check button touch
    if (ui->isTouchInButton(touch, buttons[0])) {
        if (buttons[0].onPress) {
            buttons[0].state = BTN_PRESSED;
            buttons[0].pressedTime = millis();
            buttons[0].pressDuration = 100;
            
            // Redraw button with pressed state
            ui->drawButton(buttons[0]);
            
            // Call button callback
            buttons[0].onPress();
        }
    }
}

// On screen show
void WiFiSetupScreen::onShow() {
    needsRedraw = true;
}

// Static callback for skip button
void WiFiSetupScreen::onSkip() {
    if (wifiSetupScreenInstance && wifiSetupScreenInstance->ui) {
        // Go to main screen
        wifiSetupScreenInstance->ui->setScreen(SCREEN_MAIN);
        wifiSetupScreenInstance->ui->showSuccess("WiFi setup skipped");
    }
} 