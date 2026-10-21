/**
 * UI индикация статуса подключения ESP32
 */

class ConnectionStatusIndicator {
    constructor() {
        this.indicator = null;
        this.autoFillIndicator = null;
        this.setupIndicators();
        
        // Слушаем изменения статуса
        if (typeof espConnectionStatus !== 'undefined') {
            espConnectionStatus.addListener((isConnected) => {
                this.updateIndicators(isConnected);
            });
            
            // Первоначальное обновление
            this.updateIndicators(espConnectionStatus.isConnected);
        }
    }
    
    /**
     * Настройка индикаторов
     */
    setupIndicators() {
        // Ищем существующий индикатор или создаем новый
        this.indicator = document.getElementById('esp-connection-indicator');
        
        if (!this.indicator) {
            // Создаем индикатор если его нет
            this.indicator = document.createElement('div');
            this.indicator.id = 'esp-connection-indicator';
            this.indicator.className = 'connection-indicator';
            
            // Вставляем в нужное место (например, в header или в начало body)
            const header = document.querySelector('header');
            const nav = document.querySelector('nav');
            const firstElement = header || nav || document.body;
            
            if (firstElement) {
                firstElement.insertBefore(this.indicator, firstElement.firstChild);
            } else {
                document.body.insertBefore(this.indicator, document.body.firstChild);
            }
        }
        
        // Индикатор статуса автозаполнения
        this.autoFillIndicator = document.getElementById('auto-fill-status-indicator');
        if (!this.autoFillIndicator) {
            // Создаем индикатор автозаполнения
            this.autoFillIndicator = document.createElement('div');
            this.autoFillIndicator.id = 'auto-fill-status-indicator';
            this.autoFillIndicator.className = 'status-badge';
            
            // Вставляем рядом с формой добавления канала
            const channelForm = document.getElementById('add-channel-form') || 
                               document.querySelector('.channel-form') ||
                               document.querySelector('form');
            
            if (channelForm) {
                channelForm.insertBefore(this.autoFillIndicator, channelForm.firstChild);
            } else {
                // Если форма не найдена, вставляем в body
                document.body.appendChild(this.autoFillIndicator);
            }
        }
    }
    
    /**
     * Обновление индикаторов
     */
    updateIndicators(isConnected) {
        this.updateConnectionIndicator(isConnected);
        this.updateAutoFillIndicator(isConnected);
    }
    
    /**
     * Обновление индикатора подключения
     */
    updateConnectionIndicator(isConnected) {
        if (!this.indicator) return;
        
        if (isConnected) {
            this.indicator.className = 'connection-indicator status-online';
            this.indicator.innerHTML = `
                <span class="status-dot online"></span>
                <span class="status-text">ESP32 Online</span>
            `;
        } else {
            this.indicator.className = 'connection-indicator status-offline';
            this.indicator.innerHTML = `
                <span class="status-dot offline"></span>
                <span class="status-text">ESP32 Offline</span>
            `;
        }
    }
    
    /**
     * Обновление индикатора автозаполнения
     */
    updateAutoFillIndicator(isConnected) {
        if (!this.autoFillIndicator) return;
        
        if (isConnected) {
            this.autoFillIndicator.textContent = 'Автозаполнение: Включено';
            this.autoFillIndicator.className = 'status-badge status-online';
        } else {
            this.autoFillIndicator.textContent = 'Автозаполнение: Отключено (ESP offline)';
            this.autoFillIndicator.className = 'status-badge status-offline';
        }
    }
}

// Инициализация
let connectionStatusIndicator;
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => {
        connectionStatusIndicator = new ConnectionStatusIndicator();
    });
} else {
    connectionStatusIndicator = new ConnectionStatusIndicator();
}

// Экспорт для использования в других модулях
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { ConnectionStatusIndicator, connectionStatusIndicator };
}
