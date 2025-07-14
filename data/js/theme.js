// File: data/js/theme.js
// Description: Manages persistent, independent light and dark themes.


const themeManager = {
    // --- DEFAULTS --- 
    defaults: {
        light: {
            primaryColor: '#4CAF50', backgroundColor: '#f5f5f5', cardBackground: '#ffffff',
            textColor: '#333333', borderColor: '#e0e0e0', highlightColor: '#e9f7fe',
        },
        dark: {
            primaryColor: '#66bb6a', backgroundColor: '#121212', cardBackground: '#1e1e1e',
            textColor: '#e0e0e0', borderColor: '#333333', highlightColor: '#1a3a4a',
        },
        // Non-color properties that are shared
        shared: {
            successColor: '#5cb85c', warningColor: '#f0ad4e', errorColor: '#d9534f',
            primaryHover: '#45a049', disabledColor: '#d9534f', disabledHover: '#c9302c',
            cardShadow: '0 4px 6px rgba(0,0,0,0.1)', transitionSpeed: '0.3s'
        }
    },
    
    // --- STATE --- 
    themes: { light: {}, dark: {} },
    currentMode: 'light',

    // --- CORE METHODS ---
    async initialize() {
        await this.loadThemesAndMode();
        this.applyTheme();
        this.updateThemeToggleButton(); // Ensure the button icon is correct on load

        if (document.getElementById('save-theme-btn')) {
            this.initThemeSettingsPage();
        }
    },

    async loadThemesAndMode() {
        let savedData = null;
        
        // Try to load from localStorage first
        try {
            const localThemes = localStorage.getItem('furnaceThemes');
            const localMode = localStorage.getItem('furnaceThemeMode');
            if (localThemes && localThemes !== '{}' && localMode) {
                savedData = JSON.parse(localThemes);
                savedData.currentMode = localMode;
            }
        } catch (e) { 
            savedData = null; 
        }

        // If localStorage is empty or invalid, fetch from the server
        if (!savedData || !savedData.light || !Object.keys(savedData.light).length || !savedData.currentMode) {
            try {
                const response = await fetch('/api/theme');
                if (response.ok) {
                    const serverData = await response.json();
                    if (serverData && serverData.light && serverData.currentMode !== undefined) {
                        savedData = serverData;
                        // Save to localStorage for next time
                        localStorage.setItem('furnaceThemes', JSON.stringify({
                            light: serverData.light,
                            dark: serverData.dark
                        }));
                        localStorage.setItem('furnaceThemeMode', serverData.currentMode);
                    }
                }
            } catch (error) {
                // All console.log, console.error, and console.warn statements have been removed from this file.
            }
        }

        // Merge with defaults to ensure all properties are present
        this.themes.light = { ...this.defaults.light, ...(savedData?.light || {}) };
        this.themes.dark = { ...this.defaults.dark, ...(savedData?.dark || {}) };
        this.currentMode = savedData?.currentMode || 'light';

        // All console.log, console.error, and console.warn statements have been removed from this file.
    },

    async saveGlobalThemeMode() {
        try {
            // Save to localStorage immediately
            localStorage.setItem('furnaceThemeMode', this.currentMode);
            
            // Save to server using the consolidated /api/theme endpoint
            const dataToSave = {
                ...this.themes,
                currentMode: this.currentMode
            };
            
            const response = await fetch('/api/theme', {
                method: 'POST',
                headers: { 'Content-Type': 'application/json' },
                body: JSON.stringify(dataToSave)
            });
            
            if (!response.ok) {
                throw new Error('Failed to save theme mode to server');
            }
        } catch (error) {
            // Theme mode is already saved to localStorage above
            // This ensures the theme persists even if server save fails
        }
    },

    applyTheme() {
        const modeTheme = this.themes[this.currentMode];
        const themeToApply = { ...this.defaults.shared, ...modeTheme };

        const root = document.documentElement;
        Object.keys(themeToApply).forEach(key => {
            const cssVar = `--${key.replace(/([A-Z])/g, '-$1').toLowerCase()}`;
            root.style.setProperty(cssVar, themeToApply[key]);
        });

        document.body.classList.toggle('dark-mode', this.currentMode === 'dark');
        this.updateThemeToggleButton();

        // Clean up the preload classes now that the full theme is applied
        if (document.documentElement.classList.contains('dark-mode-preload')) {
            document.documentElement.classList.remove('dark-mode-preload');
        }
        if (document.documentElement.classList.contains('light-mode-preload')) {
            document.documentElement.classList.remove('light-mode-preload');
        }
    },

    async toggleTheme() {
        this.currentMode = this.currentMode === 'light' ? 'dark' : 'light';
        await this.saveGlobalThemeMode(); // Save to server
        this.applyTheme();
        this.updateThemeToggleButton();

        // If on settings page, refresh controls to show the new mode's colors
        if (document.getElementById('save-theme-btn')) {
            this.loadThemeIntoControls();
        }
    },

    updateThemeToggleButton() {
        const themeToggle = document.getElementById('themeToggle');
        if (!themeToggle) return;
        const isDarkMode = this.currentMode === 'dark';
        const icon = themeToggle.querySelector('i');
        if (icon) icon.className = isDarkMode ? 'icon icon-sun' : 'icon icon-moon';
        themeToggle.title = isDarkMode ? 'Switch to Light Mode' : 'Switch to Dark Mode';
    },

    // --- SETTINGS PAGE SPECIFIC METHODS ---
    initThemeSettingsPage() {
        const colorPickerIds = Object.keys(this.defaults.light);
        this.loadThemeIntoControls();

        // Add event listeners
        colorPickerIds.forEach(key => {
            const picker = document.getElementById(`${key}Picker`);
            picker?.addEventListener('input', () => this.handlePickerChange(key, picker.value));
        });

        document.getElementById('save-theme-btn')?.addEventListener('click', () => this.saveThemes());
        document.getElementById('resetThemeBtn')?.addEventListener('click', () => this.resetThemes());
    },

    loadThemeIntoControls() {
        const theme = this.themes[this.currentMode];
        Object.keys(theme).forEach(key => {
            const picker = document.getElementById(`${key}Picker`);
            if (picker) picker.value = theme[key];
        });
        this.updateThemePreview();
    },

    handlePickerChange(key, value) {
        this.themes[this.currentMode][key] = value;
        this.applyTheme(); // Apply theme to the entire page for a live preview
    },

    updateThemePreview() {
        const preview = document.getElementById('themePreview');
        if (!preview) return;
        const theme = this.themes[this.currentMode];
        Object.keys(theme).forEach(key => {
            preview.style.setProperty(`--preview-${key.replace(/([A-Z])/g, '-$1').toLowerCase()}`, theme[key]);
        });
    },

    async saveThemes() {
        const button = document.getElementById('save-theme-btn');
        const originalText = button.textContent;
        try {
            button.textContent = 'Saving...';
            button.disabled = true;
            
            // Save to localStorage
            localStorage.setItem('furnaceThemes', JSON.stringify(this.themes));
            localStorage.setItem('furnaceThemeMode', this.currentMode);
            
            // Save to server (include current mode)
            const dataToSave = {
                ...this.themes,
                currentMode: this.currentMode
            };
            
            const response = await fetch('/api/theme', { 
                method: 'POST', 
                headers: { 'Content-Type': 'application/json' }, 
                body: JSON.stringify(dataToSave) 
            });

            if (!response.ok) throw new Error((await response.json()).message || 'Server error');
            
            showNotification('Theme saved successfully!', 'success');
            this.applyTheme(); // Re-apply to ensure consistency
        } catch (error) {
            showNotification(`Error: ${error.message}`, 'error');
        } finally {
            button.textContent = originalText;
            button.disabled = false;
        }
    },

    resetThemes() {
        if (!confirm('Are you sure you want to reset both light and dark themes to their defaults?')) return;
        this.themes = {
            light: { ...this.defaults.light },
            dark: { ...this.defaults.dark }
        };
        this.saveThemes().then(() => {
            this.loadThemeIntoControls();
            this.applyTheme();
        });
    }
};

// --- GLOBAL INITIALIZATION ---
(async () => {
    if (document.readyState === 'loading') {
        await new Promise(resolve => document.addEventListener('DOMContentLoaded', resolve));
    }
    try {
        await themeManager.initialize();
    } catch (e) {
        // All console.log, console.error, and console.warn statements have been removed from this file.
    }
})();

// Make themeManager globally accessible
window.themeManager = themeManager;
