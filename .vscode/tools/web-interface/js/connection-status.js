/**
 * Управление статусом подключения ESP32
 */
class ESPConnectionStatus {
    constructor() {
        this.isConnected = false;
        this.lastCheck = null;
        this.checkInterval = 5000; // 5 секунд
        this.listeners = [];
        this.checkTimeout = null;
    }
    
    /**
     * Проверка статуса подключения
     */
    async check() {
        try {
            const response = await fetch('/api/devices/192.168.20.1/status', {
                method: 'GET',
                signal: AbortSignal.timeout(3000)
            });
            
            const wasConnected = this.isConnected;
            this.isConnected = response.ok;
            this.lastCheck = Date.now();
            
            // Уведомляем слушателей об изменении статуса
            if (wasConnected !== this.isConnected) {
                this.notifyListeners();
            }
            
            return this.isConnected;
        } catch (error) {
            const wasConnected = this.isConnected;
            this.isConnected = false;
            this.lastCheck = Date.now();
            
            if (wasConnected !== this.isConnected) {
                this.notifyListeners();
            }
            
            return false;
        }
    }
    
    /**
     * Начать мониторинг статуса
     */
    startMonitoring() {
        this.check(); // Первая проверка
        this.checkTimeout = setInterval(() => this.check(), this.checkInterval);
    }
    
    /**
     * Остановить мониторинг
     */
    stopMonitoring() {
        if (this.checkTimeout) {
            clearInterval(this.checkTimeout);
            this.checkTimeout = null;
        }
    }
    
    /**
     * Добавить слушателя изменений статуса
     */
    addListener(callback) {
        this.listeners.push(callback);
    }
    
    /**
     * Удалить слушателя
     */
    removeListener(callback) {
        this.listeners = this.listeners.filter(cb => cb !== callback);
    }
    
    /**
     * Уведомить всех слушателей
     */
    notifyListeners() {
        this.listeners.forEach(callback => {
            try {
                callback(this.isConnected);
            } catch (error) {
                console.error('Error in connection status listener:', error);
            }
        });
    }
}

// Глобальный экземпляр
const espConnectionStatus = new ESPConnectionStatus();

// Начать мониторинг при загрузке страницы
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => {
        espConnectionStatus.startMonitoring();
    });
} else {
    espConnectionStatus.startMonitoring();
}

// Экспорт для использования в других модулях
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { ESPConnectionStatus, espConnectionStatus };
}
