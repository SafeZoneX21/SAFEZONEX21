// Real-time update utilities
class RealTimeUpdater {
    constructor() {
        this.updateIntervals = {};
        this.isActive = true;
    }

    // Start real-time updates for a specific component
    startUpdates(componentName, updateFunction, interval = 5000) {
        if (this.updateIntervals[componentName]) {
            clearInterval(this.updateIntervals[componentName]);
        }

        // Initial update
        updateFunction();

        // Set interval for periodic updates
        this.updateIntervals[componentName] = setInterval(() => {
            if (this.isActive) {
                updateFunction();
            }
        }, interval);
    }

    // Stop updates for a specific component
    stopUpdates(componentName) {
        if (this.updateIntervals[componentName]) {
            clearInterval(this.updateIntervals[componentName]);
            delete this.updateIntervals[componentName];
        }
    }

    // Stop all updates
    stopAllUpdates() {
        Object.values(this.updateIntervals).forEach(interval => {
            clearInterval(interval);
        });
        this.updateIntervals = {};
        this.isActive = false;
    }

    // Resume all updates
    resumeUpdates() {
        this.isActive = true;
    }
}

// Global real-time updater instance
window.realTimeUpdater = new RealTimeUpdater();

// Utility functions for common update operations
const RealTimeUtils = {
    // Show notification for data updates
    showUpdateNotification(message, type = 'info') {
        const notification = document.createElement('div');
        notification.className = `update-notification update-${type}`;
        notification.textContent = message;
        notification.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            padding: 10px 15px;
            border-radius: 5px;
            color: white;
            font-weight: bold;
            z-index: 10000;
            animation: slideIn 0.3s ease-out;
        `;

        // Set background color based on type
        switch(type) {
            case 'success':
                notification.style.backgroundColor = '#28a745';
                break;
            case 'warning':
                notification.style.backgroundColor = '#ffc107';
                notification.style.color = '#212529';
                break;
            case 'error':
                notification.style.backgroundColor = '#dc3545';
                break;
            default:
                notification.style.backgroundColor = '#17a2b8';
        }

        document.body.appendChild(notification);

        // Remove notification after 3 seconds
        setTimeout(() => {
            notification.style.animation = 'slideOut 0.3s ease-in';
            setTimeout(() => {
                if (notification.parentNode) {
                    notification.parentNode.removeChild(notification);
                }
            }, 300);
        }, 3000);
    },

    // Add flash animation to element
    flashElement(element, duration = 500) {
        if (!element) return;
        
        element.classList.add('update-flash');
        setTimeout(() => {
            element.classList.remove('update-flash');
        }, duration);
    },

    // Format timestamp to Indonesian locale
    formatTimestamp(timestamp) {
        return new Date(timestamp).toLocaleString('id-ID', {
            year: 'numeric',
            month: '2-digit',
            day: '2-digit',
            hour: '2-digit',
            minute: '2-digit',
            second: '2-digit'
        });
    },

    // Check if data has changed
    hasDataChanged(oldData, newData) {
        return JSON.stringify(oldData) !== JSON.stringify(newData);
    }
};

// Add CSS animations for notifications
const style = document.createElement('style');
style.textContent = `
    @keyframes slideIn {
        from {
            transform: translateX(100%);
            opacity: 0;
        }
        to {
            transform: translateX(0);
            opacity: 1;
        }
    }

    @keyframes slideOut {
        from {
            transform: translateX(0);
            opacity: 1;
        }
        to {
            transform: translateX(100%);
            opacity: 0;
        }
    }

    .update-flash {
        animation: flash 0.5s ease-in-out;
    }

    @keyframes flash {
        0% { background-color: transparent; }
        50% { background-color: #fff3cd; }
        100% { background-color: transparent; }
    }
`;
document.head.appendChild(style);

// Page visibility API to pause updates when tab is not visible
document.addEventListener('visibilitychange', () => {
    if (document.hidden) {
        window.realTimeUpdater.stopAllUpdates();
    } else {
        window.realTimeUpdater.resumeUpdates();
    }
});

// Export for use in other scripts
window.RealTimeUtils = RealTimeUtils; 