/**
 * API endpoint для проверки статуса подключения ESP32
 * Интегрировать в bridge.js или server.js
 */

const net = require('net');

/**
 * Проверка подключения к ESP32 через Telnet
 * @param {string} ip - IP адрес ESP32
 * @param {number} port - Порт (по умолчанию 23)
 * @param {number} timeout - Таймаут в мс (по умолчанию 2000)
 * @returns {Promise<boolean>}
 */
async function checkESP32Connection(ip, port = 23, timeout = 2000) {
    return new Promise((resolve) => {
        const socket = new net.Socket();
        let resolved = false;
        
        socket.setTimeout(timeout);
        
        socket.on('connect', () => {
            if (!resolved) {
                resolved = true;
                socket.destroy();
                resolve(true);
            }
        });
        
        socket.on('timeout', () => {
            if (!resolved) {
                resolved = true;
                socket.destroy();
                resolve(false);
            }
        });
        
        socket.on('error', (error) => {
            if (!resolved) {
                resolved = true;
                socket.destroy();
                resolve(false);
            }
        });
        
        try {
            socket.connect(port, ip);
        } catch (error) {
            if (!resolved) {
                resolved = true;
                resolve(false);
            }
        }
    });
}

/**
 * Добавить endpoint для проверки статуса
 * Использование: addStatusEndpoint(app) где app - Express приложение
 */
function addStatusEndpoint(app) {
    /**
     * GET /api/devices/:ip/status
     * Проверка статуса подключения к ESP32
     */
    app.get('/api/devices/:ip/status', async (req, res) => {
        const { ip } = req.params;
        const port = parseInt(req.query.port) || 23;
        
        try {
            const connected = await checkESP32Connection(ip, port);
            
            res.json({
                connected,
                lastSeen: connected ? new Date().toISOString() : null,
                latency: null, // Можно добавить измерение задержки
                ip,
                port
            });
        } catch (error) {
            console.error('Error checking ESP32 status:', error);
            res.status(500).json({
                connected: false,
                lastSeen: null,
                latency: null,
                error: error.message,
                ip,
                port
            });
        }
    });
}

/**
 * Альтернативная реализация для использования с существующим telnet клиентом
 * Если у вас уже есть telnet соединение, можно использовать его для проверки
 */
function addStatusEndpointWithTelnetClient(app, telnetClient) {
    app.get('/api/devices/:ip/status', async (req, res) => {
        const { ip } = req.params;
        
        try {
            // Если есть активное telnet соединение, проверяем его
            if (telnetClient && telnetClient.isConnected && telnetClient.host === ip) {
                res.json({
                    connected: true,
                    lastSeen: new Date().toISOString(),
                    latency: telnetClient.latency || null,
                    ip,
                    port: telnetClient.port || 23
                });
            } else {
                // Иначе проверяем через прямое подключение
                const connected = await checkESP32Connection(ip);
                res.json({
                    connected,
                    lastSeen: connected ? new Date().toISOString() : null,
                    latency: null,
                    ip,
                    port: 23
                });
            }
        } catch (error) {
            console.error('Error checking ESP32 status:', error);
            res.status(500).json({
                connected: false,
                lastSeen: null,
                latency: null,
                error: error.message,
                ip,
                port: 23
            });
        }
    });
}

// Экспорт для использования
if (typeof module !== 'undefined' && module.exports) {
    module.exports = {
        checkESP32Connection,
        addStatusEndpoint,
        addStatusEndpointWithTelnetClient
    };
}
