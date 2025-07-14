#include "tft_ui.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>

// External variables from main firmware
extern String primaryColor;
extern String backgroundColor;
extern String cardBackground;
extern String textColor;
extern String borderColor;
extern String highlightColor;
extern bool isDarkMode;

// Theme manager implementation
class TFT_ThemeManager {
public:
    static TFT_ThemeManager& getInstance() {
        static TFT_ThemeManager instance;
        return instance;
    }
    
    bool loadThemeFromBackend(TFT_Theme& theme) {
        if (WiFi.status() != WL_CONNECTED) {
            return false;
        }
        
        // Get theme data from the consolidated endpoint
        HTTPClient http;
        http.begin("http://localhost/api/theme");
        http.addHeader("Content-Type", "application/json");
        
        int httpCode = http.GET();
        if (httpCode == HTTP_CODE_OK) {
            String payload = http.getString();
            DynamicJsonDocument doc(2048);
            
            if (deserializeJson(doc, payload) == DeserializationError::Ok) {
                // Get current mode and theme colors from the consolidated response
                String currentMode = doc["currentMode"].as<String>();
                bool isDark = (currentMode == "dark");
                JsonObject themeColors = isDark ? doc["dark"].as<JsonObject>() : doc["light"].as<JsonObject>();
                
                // Convert colors to 565 format
                theme.primaryColor = hexToColor565(themeColors["primaryColor"].as<String>());
                theme.backgroundColor = hexToColor565(themeColors["backgroundColor"].as<String>());
                theme.cardBackground = hexToColor565(themeColors["cardBackground"].as<String>());
                theme.textColor = hexToColor565(themeColors["textColor"].as<String>());
                theme.borderColor = hexToColor565(themeColors["borderColor"].as<String>());
                theme.highlightColor = hexToColor565(themeColors["highlightColor"].as<String>());
                
                // Set standard colors
                theme.successColor = hexToColor565("#5cb85c");
                theme.warningColor = hexToColor565("#f0ad4e");
                theme.errorColor = hexToColor565("#d9534f");
                theme.disabledColor = hexToColor565("#6c757d");
                theme.isDarkMode = isDark;
                
                return true;
            }
        }
        
        http.end();
        return false;
    }
    
    void loadThemeFromGlobalVars(TFT_Theme& theme) {
        // Use the global theme variables from the main firmware
        theme.primaryColor = hexToColor565(primaryColor);
        theme.backgroundColor = hexToColor565(backgroundColor);
        theme.cardBackground = hexToColor565(cardBackground);
        theme.textColor = hexToColor565(textColor);
        theme.borderColor = hexToColor565(borderColor);
        theme.highlightColor = hexToColor565(highlightColor);
        
        // Set standard colors
        theme.successColor = hexToColor565("#5cb85c");
        theme.warningColor = hexToColor565("#f0ad4e");
        theme.errorColor = hexToColor565("#d9534f");
        theme.disabledColor = hexToColor565("#6c757d");
        theme.isDarkMode = isDarkMode;
    }
    
    uint16_t hexToColor565(const String& hex) {
        if (hex.length() < 6) {
            return 0x0000; // Black as fallback
        }
        
        String cleanHex = hex;
        if (cleanHex.startsWith("#")) {
            cleanHex = cleanHex.substring(1);
        }
        
        // Convert hex to RGB
        long number = strtol(cleanHex.c_str(), nullptr, 16);
        uint8_t r = (number >> 16) & 0xFF;
        uint8_t g = (number >> 8) & 0xFF;
        uint8_t b = number & 0xFF;
        
        // Convert to 565 format
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
    
    void setDefaultTheme(TFT_Theme& theme, bool darkMode = false) {
        if (darkMode) {
            // Dark theme defaults
            theme.primaryColor = hexToColor565("#66bb6a");
            theme.backgroundColor = hexToColor565("#121212");
            theme.cardBackground = hexToColor565("#1e1e1e");
            theme.textColor = hexToColor565("#e0e0e0");
            theme.borderColor = hexToColor565("#333333");
            theme.highlightColor = hexToColor565("#1a3a4a");
            theme.isDarkMode = true;
        } else {
            // Light theme defaults
            theme.primaryColor = hexToColor565("#4CAF50");
            theme.backgroundColor = hexToColor565("#f5f5f5");
            theme.cardBackground = hexToColor565("#ffffff");
            theme.textColor = hexToColor565("#333333");
            theme.borderColor = hexToColor565("#e0e0e0");
            theme.highlightColor = hexToColor565("#e9f7fe");
            theme.isDarkMode = false;
        }
        
        // Common colors
        theme.successColor = hexToColor565("#5cb85c");
        theme.warningColor = hexToColor565("#f0ad4e");
        theme.errorColor = hexToColor565("#d9534f");
        theme.disabledColor = hexToColor565("#6c757d");
    }
    
    String color565ToHex(uint16_t color) {
        uint8_t r = (color >> 11) << 3;
        uint8_t g = ((color >> 5) & 0x3F) << 2;
        uint8_t b = (color & 0x1F) << 3;
        
        char hex[8];
        sprintf(hex, "#%02X%02X%02X", r, g, b);
        return String(hex);
    }
    
    // Utility functions for color manipulation
    uint16_t lightenColor(uint16_t color, float factor) {
        uint8_t r = (color >> 11) << 3;
        uint8_t g = ((color >> 5) & 0x3F) << 2;
        uint8_t b = (color & 0x1F) << 3;
        
        r = min(255, (int)(r + (255 - r) * factor));
        g = min(255, (int)(g + (255 - g) * factor));
        b = min(255, (int)(b + (255 - b) * factor));
        
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
    
    uint16_t darkenColor(uint16_t color, float factor) {
        uint8_t r = (color >> 11) << 3;
        uint8_t g = ((color >> 5) & 0x3F) << 2;
        uint8_t b = (color & 0x1F) << 3;
        
        r = max(0, (int)(r * (1.0 - factor)));
        g = max(0, (int)(g * (1.0 - factor)));
        b = max(0, (int)(b * (1.0 - factor)));
        
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
    
    uint16_t blendColors(uint16_t color1, uint16_t color2, float ratio) {
        uint8_t r1 = (color1 >> 11) << 3;
        uint8_t g1 = ((color1 >> 5) & 0x3F) << 2;
        uint8_t b1 = (color1 & 0x1F) << 3;
        
        uint8_t r2 = (color2 >> 11) << 3;
        uint8_t g2 = ((color2 >> 5) & 0x3F) << 2;
        uint8_t b2 = (color2 & 0x1F) << 3;
        
        uint8_t r = r1 * (1.0 - ratio) + r2 * ratio;
        uint8_t g = g1 * (1.0 - ratio) + g2 * ratio;
        uint8_t b = b1 * (1.0 - ratio) + b2 * ratio;
        
        return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
    }
    
    bool isColorDark(uint16_t color) {
        uint8_t r = (color >> 11) << 3;
        uint8_t g = ((color >> 5) & 0x3F) << 2;
        uint8_t b = (color & 0x1F) << 3;
        
        // Calculate luminance
        float luminance = (0.299 * r + 0.587 * g + 0.114 * b) / 255.0;
        return luminance < 0.5;
    }
    
    uint16_t getContrastColor(uint16_t backgroundColor) {
        return isColorDark(backgroundColor) ? 0xFFFF : 0x0000; // White or black
    }
    
private:
    TFT_ThemeManager() = default;
};

// Theme utility functions implementation
uint16_t TFT_UI::hexToColor565(const String& hex) {
    return TFT_ThemeManager::getInstance().hexToColor565(hex);
}

void TFT_UI::loadTheme() {
    // Try to load from backend first
    if (!TFT_ThemeManager::getInstance().loadThemeFromBackend(theme)) {
        // Fall back to global variables
        TFT_ThemeManager::getInstance().loadThemeFromGlobalVars(theme);
    }
    
    themeLoaded = true;
    applyTheme();
}

void TFT_UI::applyTheme() {
    if (!themeLoaded) {
        setDefaultTheme();
        return;
    }
    
    // Check if theme has actually changed to avoid unnecessary full screen redraws
    static TFT_Theme lastTheme = {};
    static bool firstRun = true;
    
    bool themeChanged = firstRun ||
                       (lastTheme.primaryColor != theme.primaryColor) ||
                       (lastTheme.backgroundColor != theme.backgroundColor) ||
                       (lastTheme.cardBackground != theme.cardBackground) ||
                       (lastTheme.textColor != theme.textColor) ||
                       (lastTheme.isDarkMode != theme.isDarkMode);
    
    if (themeChanged) {
        // Only do full screen clear if theme actually changed
        tft.fillScreen(theme.backgroundColor);
        
        // Force redraw of current screen
        screenNeedsRedraw = true;
        
        // Also set the individual screen's needsRedraw flag
        if (screens[currentScreen]) {
            screens[currentScreen]->needsRedraw = true;
        }
        
        // Update last theme
        lastTheme = theme;
        firstRun = false;
    }
}

void TFT_UI::setDefaultTheme() {
    TFT_ThemeManager::getInstance().setDefaultTheme(theme, isDarkMode);
    themeLoaded = true;
    applyTheme();
}

void TFT_UI::checkForThemeUpdates() {
    static unsigned long lastThemeCheck = 0;
    const unsigned long THEME_CHECK_INTERVAL = 30000; // Check every 30 seconds
    
    unsigned long currentTime = millis();
    if (currentTime - lastThemeCheck < THEME_CHECK_INTERVAL) {
        return; // Not time to check yet
    }
    
    lastThemeCheck = currentTime;
    
    // Only check if WiFi is connected
    if (WiFi.status() != WL_CONNECTED) {
        return;
    }
    
    // Store current theme for comparison
    TFT_Theme currentTheme = theme;
    
    // Try to load fresh theme from backend
    if (TFT_ThemeManager::getInstance().loadThemeFromBackend(theme)) {
        // Check if theme actually changed
        bool themeChanged = (currentTheme.primaryColor != theme.primaryColor) ||
                           (currentTheme.backgroundColor != theme.backgroundColor) ||
                           (currentTheme.cardBackground != theme.cardBackground) ||
                           (currentTheme.textColor != theme.textColor) ||
                           (currentTheme.borderColor != theme.borderColor) ||
                           (currentTheme.highlightColor != theme.highlightColor) ||
                           (currentTheme.isDarkMode != theme.isDarkMode);
        
        if (themeChanged) {
            applyTheme();
        }
    }
}

// Additional theme-related utility functions
uint16_t getButtonColor(const TFT_Theme& theme, ButtonState state) {
    switch (state) {
        case BTN_NORMAL:
            return theme.primaryColor;
        case BTN_PRESSED:
            return TFT_ThemeManager::getInstance().darkenColor(theme.primaryColor, 0.2);
        case BTN_DISABLED:
            return theme.disabledColor;
        case BTN_ACTIVE:
            return TFT_ThemeManager::getInstance().lightenColor(theme.primaryColor, 0.2);
        default:
            return theme.primaryColor;
    }
}

uint16_t getTextColor(const TFT_Theme& theme, ButtonState state) {
    switch (state) {
        case BTN_DISABLED:
            return TFT_ThemeManager::getInstance().blendColors(theme.textColor, theme.backgroundColor, 0.5);
        default:
            return TFT_ThemeManager::getInstance().getContrastColor(getButtonColor(theme, state));
    }
}

uint16_t getCardShadowColor(const TFT_Theme& theme) {
    return theme.isDarkMode ? 
        TFT_ThemeManager::getInstance().lightenColor(theme.backgroundColor, 0.1) : 
        TFT_ThemeManager::getInstance().darkenColor(theme.backgroundColor, 0.1);
}

uint16_t getGridColor(const TFT_Theme& theme) {
    return TFT_ThemeManager::getInstance().blendColors(theme.textColor, theme.backgroundColor, 0.3);
}

uint16_t getContrastColor(uint16_t backgroundColor) {
    return TFT_ThemeManager::getInstance().getContrastColor(backgroundColor);
}

// Export theme manager for external use
TFT_ThemeManager& getThemeManager() {
    return TFT_ThemeManager::getInstance();
} 