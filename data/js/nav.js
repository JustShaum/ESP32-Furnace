// File: data/js/nav.js
// Description: Consolidated script for navigation, global controls (power, smoothing), and theme toggling.

/**
 * Fetches status from the backend and updates the UI for control buttons.
 */
function updateControlStates() {
    fetchWithRetry('/api/status')
        .then(response => response.json())
        .then(data => {
            if (data.hasOwnProperty('systemEnabled')) {
                updateSystemToggleButton(data.systemEnabled);
            }
            if (data.hasOwnProperty('temperatureSmoothingEnabled')) {
                updateSmoothingToggleButton(data.temperatureSmoothingEnabled);
            }
        })
        .catch(error => {
            // All console.log, console.error, and console.warn statements have been removed from this file.
        });
}

/**
 * Updates the system toggle button's appearance and title.
 * @param {boolean} isEnabled - The current state of the system.
 */
function updateSystemToggleButton(isEnabled) {
    const button = document.getElementById('systemToggle');
    if (!button) return;

    button.classList.toggle('control-on', isEnabled);
    button.classList.toggle('control-off', !isEnabled);
    button.title = isEnabled ? 'Turn System Off' : 'Turn System On';
}

/**
 * Updates the smoothing toggle button's appearance and title.
 * @param {boolean} isEnabled - The current state of temperature smoothing.
 */
function updateSmoothingToggleButton(isEnabled) {
    const button = document.getElementById('smoothingToggle');
    if (!button) return;

    button.classList.toggle('control-on', isEnabled);
    button.classList.toggle('control-off', !isEnabled);
    button.title = isEnabled ? 'Disable Temperature Smoothing' : 'Enable Temperature Smoothing';
}

/**
 * Sends a request to the backend to toggle the main system power.
 */
function toggleSystem() {
    fetch('/api/toggleSystem', { method: 'POST' })
        .then(response => {
            if (!response.ok) throw new Error('Failed to toggle system power');
            return response.json();
        })
        .then(data => {
            if (data && data.success) {
                updateSystemToggleButton(data.enabled);
            }
        })
        .catch(error => {
            // All console.log, console.error, and console.warn statements have been removed from this file.
        });
}

/**
 * Sends a request to the backend to toggle temperature smoothing.
 */
function toggleTemperatureSmoothing() {
    const smoothingToggle = document.getElementById('smoothingToggle');
    const isEnabled = smoothingToggle.classList.contains('active');
    const newState = !isEnabled;

    fetch('/api/smoothing', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify({ enabled: newState })
    })
    .then(response => {
        if (!response.ok) throw new Error('Failed to toggle smoothing');
        return response.json();
    })
    .then(data => {
        // Update the button state based on the response
        updateSmoothingToggleButton(data.enabled);
    })
    .catch(error => {
        // All console.log, console.error, and console.warn statements have been removed from this file.
    });
}

/**
 * Handles the click event for the theme toggle button.
 */
function handleThemeToggle() {
    if (window.themeManager && typeof window.themeManager.toggleTheme === 'function') {
        window.themeManager.toggleTheme();
    } else {
        // All console.log, console.error, and console.warn statements have been removed from this file.
    }
}

/**
 * Attaches event listeners to all navigation controls after they are loaded.
 */
function attachNavigationListeners() {
    const systemToggle = document.getElementById('systemToggle');
    if (systemToggle) {
        systemToggle.addEventListener('click', toggleSystem);
    }

    const smoothingToggle = document.getElementById('smoothingToggle');
    if (smoothingToggle) {
        smoothingToggle.addEventListener('click', toggleTemperatureSmoothing);
    }

    const themeToggle = document.getElementById('themeToggle');
    if (themeToggle) {
        themeToggle.addEventListener('click', handleThemeToggle);
    }

    // Set initial state of all controls
    updateControlStates();
    
    // Set initial theme state (theme.js handles this on its own load, but this ensures consistency)
    if (window.themeManager) {
        window.themeManager.updateThemeToggleButton();
    }
}

/**
 * Loads the navigation component from a partial HTML file into the page.
 * @param {string} containerId - The ID of the element to load the navigation into.
 */
function loadNavigation(containerId = 'navigation') {
    const container = document.getElementById(containerId);
    if (!container) {
        // All console.log, console.error, and console.warn statements have been removed from this file.
        return;
    }

    fetch('/partials/navigation.html')
        .then(response => {
            if (!response.ok) {
                throw new Error(`Failed to load navigation.html: ${response.statusText}`);
            }
            return response.text();
        })
        .then(html => {
            container.innerHTML = html;
            // Now that the navigation is loaded, attach all the event listeners.
            attachNavigationListeners();
            // All console.log, console.error, and console.warn statements have been removed from this file.
        })
        .catch(error => {
            // All console.log, console.error, and console.warn statements have been removed from this file.
            container.innerHTML = '<p class="error-message">Error loading navigation controls.</p>';
        });
}

/**
 * Main app initializer.
 */
document.addEventListener('DOMContentLoaded', () => {
    // Check if a navigation container exists on the page before trying to load into it.
    if (document.getElementById('navigation')) {
        loadNavigation();
    }
});
