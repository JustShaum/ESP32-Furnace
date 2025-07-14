#include "tft_ui.h"

// Navigation handler class
class TFT_Navigation {
public:
    static TFT_Navigation& getInstance() {
        static TFT_Navigation instance;
        return instance;
    }
    
    // Handle navigation bar touch
    bool handleNavigationTouch(TFT_UI* ui, const TouchPoint& touch) {
        if (touch.y < TFT_HEIGHT - 30) {
            return false; // Not in navigation area
        }
        
        // Calculate button positions to match the drawing logic
        // Layout: Main(80px) | Settings(80px) | Programs(80px) | Charts(80px)
        int buttonWidthEach = 80; // Equal width for all 4 buttons
        
        ScreenType targetScreen = SCREEN_MAIN;
        
        if (touch.x < buttonWidthEach) {
            targetScreen = SCREEN_MAIN;
        } else if (touch.x < buttonWidthEach * 2) {
            targetScreen = SCREEN_SETTINGS;
        } else if (touch.x < buttonWidthEach * 3) {
            targetScreen = SCREEN_PROGRAMS;
        } else {
            targetScreen = SCREEN_CHARTS;
        }
        
        // Only navigate if different screen
        if (targetScreen != ui->getCurrentScreen()) {
            ui->setScreen(targetScreen);
            return true;
        }
        
        return false;
    }
    
    // Draw navigation bar with current screen highlighting
    void drawNavigationBar(TFT_UI* ui) {
        TFT_eSPI& tft = ui->getTFT();
        const TFT_Theme& theme = ui->getTheme();
        
        int navHeight = 30;
        int navY = TFT_HEIGHT - navHeight;
        int buttonWidth = TFT_WIDTH / 4; // 4 buttons now
        
        // Draw navigation background
        tft.fillRect(0, navY, TFT_WIDTH, navHeight, theme.cardBackground);
        tft.drawLine(0, navY, TFT_WIDTH, navY, theme.borderColor);
        
        // Draw navigation buttons with proper highlighting
        // Layout: Main(80px) | Settings(80px) | Programs(80px) | Charts(80px)
        int buttonWidthEach = 80; // Equal width for all 4 buttons
        
        drawNavigationButton(ui, 0, navY, buttonWidthEach, navHeight, "Main", SCREEN_MAIN);
        drawNavigationButton(ui, buttonWidthEach, navY, buttonWidthEach, navHeight, "Settings", SCREEN_SETTINGS);
        drawNavigationButton(ui, buttonWidthEach * 2, navY, buttonWidthEach, navHeight, "Programs", SCREEN_PROGRAMS);
        drawNavigationButton(ui, buttonWidthEach * 3, navY, buttonWidthEach, navHeight, "Charts", SCREEN_CHARTS);
    }
    
    // Get screen title
    String getScreenTitle(ScreenType screen) {
        switch (screen) {
            case SCREEN_MAIN:
                return "Main Dashboard";
            case SCREEN_SETTINGS:
                return "Settings";
            case SCREEN_PROGRAMS:
                return "Programs";
            case SCREEN_CHARTS:
                return "Charts";
            case SCREEN_SETUP:
                return "Setup";
            default:
                return "Unknown";
        }
    }
    
    // Get screen icon (simple text icons for now)
    String getScreenIcon(ScreenType screen) {
        switch (screen) {
            case SCREEN_MAIN:
                return "M";
            case SCREEN_SETTINGS:
                return "S";
            case SCREEN_PROGRAMS:
                return "P";
            case SCREEN_CHARTS:
                return "C";
            case SCREEN_SETUP:
                return "U";
            default:
                return "?";
        }
    }
    
    // Check if screen transition is allowed
    bool canNavigateTo(ScreenType from, ScreenType to) {
        // Allow navigation between all screens for now
        // Could be enhanced with state-based restrictions
        return true;
    }
    
    // Handle back button navigation
    void handleBackButton(TFT_UI* ui) {
        ScreenType currentScreen = ui->getCurrentScreen();
        
        // Navigate to logical parent screen
        switch (currentScreen) {
            case SCREEN_SETTINGS:
            case SCREEN_PROGRAMS:
            case SCREEN_CHARTS:
                ui->setScreen(SCREEN_MAIN);
                break;
            case SCREEN_SETUP:
                ui->setScreen(SCREEN_SETTINGS);
                break;
            default:
                // Already on main screen, no back navigation
                break;
        }
    }
    
    // Create breadcrumb navigation
    void drawBreadcrumbs(TFT_UI* ui, int x, int y) {
        TFT_eSPI& tft = ui->getTFT();
        const TFT_Theme& theme = ui->getTheme();
        ScreenType currentScreen = ui->getCurrentScreen();
        
        tft.setTextColor(theme.textColor);
        tft.setTextSize(1);
        tft.setCursor(x, y);
        
        // Show navigation path
        switch (currentScreen) {
            case SCREEN_MAIN:
                tft.print("Main");
                break;
            case SCREEN_SETTINGS:
                tft.print("Main > Settings");
                break;
            case SCREEN_PROGRAMS:
                tft.print("Main > Programs");
                break;
            case SCREEN_CHARTS:
                tft.print("Main > Charts");
                break;
            case SCREEN_SETUP:
                tft.print("Main > Settings > Setup");
                break;
        }
    }
    
    // Gesture navigation support
    bool handleGesture(TFT_UI* ui, const TouchPoint& startTouch, const TouchPoint& endTouch) {
        int deltaX = endTouch.x - startTouch.x;
        int deltaY = endTouch.y - startTouch.y;
        
        // Check for swipe gestures
        if (abs(deltaX) > abs(deltaY) && abs(deltaX) > 50) {
            ScreenType currentScreen = ui->getCurrentScreen();
            
            if (deltaX > 0) {
                // Swipe right - go to previous screen
                switch (currentScreen) {
                    case SCREEN_SETTINGS:
                        ui->setScreen(SCREEN_MAIN);
                        return true;
                    case SCREEN_PROGRAMS:
                        ui->setScreen(SCREEN_SETTINGS);
                        return true;
                }
            } else {
                // Swipe left - go to next screen
                switch (currentScreen) {
                    case SCREEN_MAIN:
                        ui->setScreen(SCREEN_SETTINGS);
                        return true;
                    case SCREEN_SETTINGS:
                        ui->setScreen(SCREEN_PROGRAMS);
                        return true;
                }
            }
        }
        
        return false;
    }
    
    // Show navigation hints
    void showNavigationHints(TFT_UI* ui) {
        ui->showMessage("Swipe left/right to navigate, tap nav bar", ui->getTheme().textColor, 3000);
    }
    
    // Handle quick access shortcuts
    bool handleQuickAccess(TFT_UI* ui, const TouchPoint& touch) {
        // Check for corner touches for quick access
        if (touch.x < 20 && touch.y < 20) {
            // Top-left corner - quick settings
            ui->setScreen(SCREEN_SETTINGS);
            return true;
        }
        
        if (touch.x > TFT_WIDTH - 20 && touch.y < 20) {
            // Top-right corner - quick programs
            ui->setScreen(SCREEN_PROGRAMS);
            return true;
        }
        
        return false;
    }
    
    // Save navigation state
    void saveNavigationState(ScreenType screen) {
        // Could save to preferences for persistence
        lastScreen = screen;
    }
    
    // Restore navigation state
    ScreenType getLastScreen() {
        return lastScreen;
    }
    
private:
    TFT_Navigation() : lastScreen(SCREEN_MAIN) {}
    
    ScreenType lastScreen;
    
    // Draw individual navigation button
    void drawNavigationButton(TFT_UI* ui, int x, int y, int width, int height, 
                            const String& label, ScreenType screen) {
        TFT_eSPI& tft = ui->getTFT();
        const TFT_Theme& theme = ui->getTheme();
        
        bool isActive = (screen == ui->getCurrentScreen());
        
        // Draw button background - removed rounded corners
        uint16_t bgColor = isActive ? theme.primaryColor : theme.cardBackground;
        tft.fillRect(x, y, width, height, bgColor);
        tft.drawRect(x, y, width, height, theme.borderColor);
        
        // Draw button text
        tft.setTextColor(theme.textColor);
        tft.setTextSize(1);
        
        // Center text in button
        int textWidth = label.length() * 6;
        int textX = x + (width - textWidth) / 2;
        int textY = y + (height - 8) / 2;
        
        tft.setCursor(textX, textY);
        tft.print(label);
        
        // Draw active indicator
        if (isActive) {
            tft.fillRect(x + 2, y + height - 3, width - 4, 2, theme.highlightColor);
        }
    }
};

// Global navigation instance
TFT_Navigation& getNavigation() {
    return TFT_Navigation::getInstance();
}

// Enhanced TFT_UI navigation methods
void TFT_UI::drawNavigationBar() {
    getNavigation().drawNavigationBar(this);
}

// Enhanced touch handling with navigation
void TFT_UI::handleNavigationTouch(const TouchPoint& touch) {
    // Check if a modal is active - if so, block all navigation
    if (hasActiveModal()) {
        // Only pass touch to the current screen when modal is visible
        if (screens[currentScreen]) {
            screens[currentScreen]->handleTouch(const_cast<TouchPoint&>(touch));
        }
        return;
    }
    
    // Check for navigation bar touch
    if (getNavigation().handleNavigationTouch(this, touch)) {
        return;
    }
    
    // Check for quick access shortcuts
    if (getNavigation().handleQuickAccess(this, touch)) {
        return;
    }
    
    // Pass to current screen
    if (screens[currentScreen]) {
        screens[currentScreen]->handleTouch(const_cast<TouchPoint&>(touch));
    }
}

// Screen transition with animation support
void TFT_UI::setScreenWithAnimation(ScreenType screen, bool animate) {
    if (screen == currentScreen) return;
    
    if (animate) {
        // Simple fade animation
        for (int alpha = 255; alpha >= 0; alpha -= 15) {
            // Fade out current screen
            delay(10);
        }
    }
    
    // Save navigation state
    getNavigation().saveNavigationState(currentScreen);
    
    // Switch screens
    setScreen(screen);
    
    if (animate) {
        // Fade in new screen
        for (int alpha = 0; alpha <= 255; alpha += 15) {
            delay(10);
        }
    }
}

// Navigation utility functions
String TFT_UI::getCurrentScreenTitle() {
    return getNavigation().getScreenTitle(currentScreen);
}

void TFT_UI::showNavigationHints() {
    getNavigation().showNavigationHints(this);
}

void TFT_UI::handleBackButton() {
    getNavigation().handleBackButton(this);
}

// Enhanced update method with navigation
void TFT_UI::updateWithNavigation() {
    unsigned long currentTime = millis();
    
    // Throttle updates
    if (currentTime - lastUpdate < updateInterval) {
        return;
    }
    lastUpdate = currentTime;
    
    // Handle touch input with navigation
    handleTouch();
    
    // Update current screen
    if (screens[currentScreen]) {
        screens[currentScreen]->update();
    }
    
    // Redraw if needed
    if (screenNeedsRedraw) {
        clearScreen();
        drawStatusBar();
        
        // Draw breadcrumbs
        getNavigation().drawBreadcrumbs(this, 5, 0);
        
        // Draw current screen
        if (screens[currentScreen]) {
            screens[currentScreen]->draw();
        }
        
        // Draw navigation bar
        drawNavigationBar();
        
        screenNeedsRedraw = false;
    }
    
    // Message display disabled - no popup notifications
}

// Navigation keyboard shortcuts (if hardware buttons are available)
void TFT_UI::handleKeyboardShortcuts(char key) {
    switch (key) {
        case 'h':
        case 'H':
            setScreen(SCREEN_MAIN);
            break;
        case 's':
        case 'S':
            setScreen(SCREEN_SETTINGS);
            break;
        case 'p':
        case 'P':
            setScreen(SCREEN_PROGRAMS);
            break;
        case 'c':
        case 'C':
            setScreen(SCREEN_CHARTS);
            break;
        case 'b':
        case 'B':
            handleBackButton();
            break;
        case '?':
            showNavigationHints();
            break;
    }
} 