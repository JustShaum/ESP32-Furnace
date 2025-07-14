// Shared application functions

/**
 * Show a notification in the top right corner. Use for status messages on all pages.
 * Do NOT use for actions that require explicit user confirmation (e.g., destructive actions).
 * @param {string} message - The notification message
 * @param {string} [type='info'] - Type: 'success', 'error', 'warning', 'info'
 * @param {number} [duration=4000] - Duration in ms
 */
function showNotification(message, type = 'info', duration = 4000) {
    let container = document.getElementById('notification-container');
    if (!container) {
        container = document.createElement('div');
        container.id = 'notification-container';
        container.style.position = 'fixed';
        container.style.top = '20px';
        container.style.right = '20px';
        container.style.bottom = '';
        container.style.left = '';
        container.style.zIndex = '1000';
        container.style.maxWidth = '350px';
        container.style.pointerEvents = 'none';
        document.body.appendChild(container);
    }
    
    const notification = document.createElement('div');
    notification.className = `notification ${type}`;
    notification.style.background = {
        success: '#4CAF50',
        error: '#f44336',
        warning: '#ff9800',
        info: '#2196F3'
    }[type] || '#2196F3';
    notification.style.color = '#fff';
    notification.style.padding = '12px 18px';
    notification.style.marginBottom = '10px';
    notification.style.borderRadius = '6px';
    notification.style.boxShadow = '0 2px 8px rgba(0,0,0,0.1)';
    notification.style.opacity = '1';
    notification.style.transition = 'opacity 0.3s';
    notification.style.fontSize = '1rem';
    notification.style.pointerEvents = 'auto';
    notification.textContent = message;
    container.insertBefore(notification, container.firstChild);
    setTimeout(() => {
        notification.style.opacity = '0';
        setTimeout(() => notification.remove(), 350);
    }, duration);
}

// Make globally accessible
window.showNotification = showNotification;

// Power control functions - only declare if not already defined
if (typeof PowerControl === 'undefined') {
    window.PowerControl = {
        // Toggle system power state
        toggleSystem: async function() {
            const systemToggle = document.getElementById('systemToggle');
            const isEnabled = systemToggle.classList.contains('active');
            const newState = !isEnabled;

            try {
                const response = await fetch('/api/toggleSystem', {
                    method: 'POST'
                });

                if (!response.ok) {
                    throw new Error(`HTTP error! status: ${response.status}`);
                }

                const data = await response.json();
                this.updateSystemToggleButton(data.enabled);

                // If the page has a fetchStatus function, call it to update the UI
                if (typeof window.fetchStatus === 'function') {
                    window.fetchStatus(true);
                }

                // Force TFT refresh
                fetch('/api/refresh-tft', { method: 'POST' });

                return data;
            } catch (error) {
                // Revert the toggle if there was an error
                this.updateSystemToggleButton(isEnabled);
                throw error;
            }
        },

        // Update the power button UI state
        updateSystemToggleButton: function(isEnabled) {
            const systemToggle = document.getElementById('systemToggle');
            if (!systemToggle) return;

            if (isEnabled) {
                systemToggle.classList.add('active');
                systemToggle.title = 'Power Off';
                systemToggle.innerHTML = '<i class="icon icon-power"></i>';
            } else {
                systemToggle.classList.remove('active');
                systemToggle.title = 'Power On';
                systemToggle.innerHTML = '<i class="icon icon-power-off"></i>';
            }
        },

        // Initialize power control
        init: function() {
            const systemToggle = document.getElementById('systemToggle');
            if (systemToggle) {
                systemToggle.addEventListener('click', () => this.toggleSystem());
            }
        }
    };
}

// Temperature smoothing functions
if (typeof window.TemperatureSmoothing === 'undefined') {
    window.TemperatureSmoothing = {
        // Toggle temperature smoothing
        toggleSmoothing: async function() {
        const smoothingToggle = document.getElementById('smoothingToggle');
        const isEnabled = smoothingToggle.classList.contains('active');
        const newState = !isEnabled;

        try {
            const response = await fetch('/api/smoothing', {
                method: 'POST',
                headers: {
                    'Content-Type': 'application/json',
                },
                body: JSON.stringify({ enabled: newState })
            });

            if (!response.ok) {
                throw new Error(`HTTP error! status: ${response.status}`);
            }

            const data = await response.json();
            this.updateSmoothingToggleButton(data.enabled);

            // Force TFT refresh when smoothing is toggled
            fetch('/api/refresh-tft', { method: 'POST' });

            return data;
        } catch (error) {
            // Revert the toggle if there was an error
            this.updateSmoothingToggleButton(isEnabled);
            throw error;
        }
    },

    // Update the smoothing button UI state
    updateSmoothingToggleButton: function(isEnabled) {
        const smoothingToggle = document.getElementById('smoothingToggle');
        if (!smoothingToggle) return;

        if (isEnabled) {
            smoothingToggle.classList.add('active');
            smoothingToggle.title = 'Disable Smoothing';
        } else {
            smoothingToggle.classList.remove('active');
            smoothingToggle.title = 'Enable Smoothing';
        }
    },

    // Initialize temperature smoothing control
    init: function() {
        const smoothingToggle = document.getElementById('smoothingToggle');
        if (smoothingToggle) {
            smoothingToggle.addEventListener('click', () => this.toggleSmoothing());
        }
    }
};
}

// Initialize all components when the DOM is loaded
// Note: Initialization is now handled by nav.js to ensure proper loading order
// This allows the navigation to be fully loaded before initializing the controls
