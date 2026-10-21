/*
 * HTTP-to-Telnet Bridge
 * Мост между веб-интерфейсом и Telnet сервером ESP32
 * 
 * ESP32 EtherCAT Slave v4
 */

const express = require('express');
const http = require('http');
const socketIo = require('socket.io');
const net = require('net');

// Новые модули для Device Profiles архитектуры
const DeviceProfiles = require('./device-profiles');
const DeviceRegistry = require('./device-registry');
const ConfigurationBuilder = require('./configuration-builder');
const SyncManager = require('./sync-manager');
const LocalStorageManager = require('./local-storage');

const app = express();
const server = http.createServer(app);
const io = socketIo(server, {
    cors: {
        origin: "*",
        methods: ["GET", "POST"]
    }
});

// Конфигурация
const DEFAULT_TELNET_HOST = process.env.TELNET_HOST || '192.168.20.1';
const TELNET_PORT = 23;
const HTTP_PORT = Number.parseInt(process.env.HTTP_PORT || process.env.PORT || '3000', 10);

// Active device (для stats и быстрых кнопок UI)
let activeTelnetHost = DEFAULT_TELNET_HOST;

// Device Profiles архитектура (создаем ДО использования)
const deviceProfiles = new DeviceProfiles();
const deviceRegistry = new DeviceRegistry();

// Local Storage Manager для сохранения/восстановления настроек (создаем ДО использования)
const localStorageManager = new LocalStorageManager();

// Discovered devices (daisy-chain / scan)
// key: ip string
const devices = new Map();
// Загружаем сохраненные устройства при старте
const savedDevices = localStorageManager.loadDevices();
if (savedDevices.size > 0) {
    for (const [ip, device] of savedDevices.entries()) {
        devices.set(ip, device);
    }
    console.log(`[Bridge] Restored ${devices.size} device(s) from storage`);
} else {
    devices.set(activeTelnetHost, { host: activeTelnetHost, system: {}, network: {}, lastSeen: Date.now() });
}

// Локальное хранилище каналов GPIO для каждого устройства
// key: ip string, value: array of channel objects
const localChannels = localStorageManager.loadChannels();
if (localChannels.size > 0) {
    console.log(`[Bridge] Restored channels for ${localChannels.size} device(s) from storage`);
}

// Загружаем реестр устройств
localStorageManager.loadRegistry(deviceRegistry);

// SyncManager будет инициализирован после определения sendJsonRpcOnce
let syncManager = null;

// Очередь запросов для предотвращения конфликтов с одним сокетом W5500
const requestQueue = [];
let isProcessingQueue = false;
const QUEUE_DELAY_MS = 150; // Задержка между запросами (150ms) - дает время ESP32 вернуть сокет в LISTEN

// Статический контент
app.use(express.static('public'));
app.use(express.json());

// Хранилище статистики
let currentStats = {
    ethercat: {
        state: 'INIT',
        rx: 0,
        tx: 0,
        dropped: 0
    },
    test: {
        frames_sent: 0,
        frames_received: 0,
        frames_dropped: 0,
        min_latency_us: 0,
        max_latency_us: 0,
        avg_latency_us: 0,
        latency_samples: 0,
        test_duration_ms: 0
    },
    telnet: {
        running: false,
        connections_count: 0,
        commands_processed: 0
    }
};

// Telnet клиент (для постоянного соединения для получения notifications и API запросов)
let telnetClient = null;
let telnetConnected = false;
let reconnectTimer = null;
let reconnectAttempts = 0;
const MAX_RECONNECT_ATTEMPTS = 10;
const RECONNECT_DELAY = 5000; // 5 секунд
let persistentBuffer = ''; // Буфер для накопления данных в постоянном соединении
// Очередь ожидающих ответов через постоянное соединение
const pendingRequests = new Map(); // id -> { resolve, reject, timeout }

function disconnectTelnet() {
    try {
        if (reconnectTimer) {
            clearTimeout(reconnectTimer);
            reconnectTimer = null;
        }
        if (telnetClient) {
            telnetClient.removeAllListeners();
            telnetClient.destroy();
        }
    } catch (_) {}
    telnetClient = null;
    telnetConnected = false;
    reconnectAttempts = 0;
    persistentBuffer = ''; // Очищаем буфер при отключении
    
    // Отклоняем все ожидающие запросы
    for (const [id, pending] of pendingRequests.entries()) {
        clearTimeout(pending.timeout);
        pending.reject(new Error('Connection disconnected'));
    }
    pendingRequests.clear();
}

function setActiveDeviceHost(host) {
    if (!host || typeof host !== 'string') return;
    if (host === activeTelnetHost) return;
    activeTelnetHost = host.trim();
    if (!devices.has(activeTelnetHost)) {
        devices.set(activeTelnetHost, { host: activeTelnetHost, system: {}, network: {}, lastSeen: Date.now() });
        // Сохраняем устройства при добавлении нового
        localStorageManager.saveDevices(devices);
    }
    // Переподключаемся для получения notifications от нового устройства
    disconnectTelnet();
    reconnectAttempts = 0;
    connectTelnet();
    io.emit('devices_update', { active: activeTelnetHost, devices: Array.from(devices.values()) });
}

// Подключение к Telnet
function connectTelnet() {
    if (telnetClient && telnetConnected) {
        return;
    }

    // Очищаем предыдущий таймер переподключения
    if (reconnectTimer) {
        clearTimeout(reconnectTimer);
        reconnectTimer = null;
    }

    // Проверяем количество попыток переподключения
    if (reconnectAttempts >= MAX_RECONNECT_ATTEMPTS) {
        console.error(`[Telnet] Max reconnect attempts (${MAX_RECONNECT_ATTEMPTS}) reached. Stopping reconnection.`);
        io.emit('telnet_status', { 
            connected: false, 
            host: activeTelnetHost, 
            error: `Max reconnect attempts reached. Please check ESP32 connection.` 
        });
        return;
    }

    reconnectAttempts++;
    if (reconnectAttempts > 1) {
        console.log(`[Telnet] Reconnect attempt ${reconnectAttempts}/${MAX_RECONNECT_ATTEMPTS}...`);
    }

    telnetClient = new net.Socket();
    
    // Настройка keepalive для поддержания соединения
    telnetClient.setKeepAlive(true, 60000); // Keepalive каждые 60 секунд
    telnetClient.setNoDelay(true); // Отключить Nagle algorithm для низкой задержки
    
    const host = activeTelnetHost;
    
    // Таймаут подключения
    telnetClient.setTimeout(10000, () => {
        console.error(`[Telnet] Connection timeout to ${host}:${TELNET_PORT}`);
        telnetClient.destroy();
    });
    
    telnetClient.connect(TELNET_PORT, host, () => {
        console.log(`[Telnet] Connected to ${host}:${TELNET_PORT} for notifications and API`);
        telnetConnected = true;
        reconnectAttempts = 0; // Сброс счетчика при успешном подключении
        persistentBuffer = ''; // Очищаем буфер при новом подключении
        // Убеждаемся, что activeTelnetHost установлен
        if (!activeTelnetHost || activeTelnetHost !== host) {
            activeTelnetHost = host;
        }
        io.emit('telnet_status', { connected: true, host, mode: 'persistent' });
        console.log(`[Telnet] Persistent connection ready, activeTelnetHost: ${activeTelnetHost}`);
    });

    telnetClient.on('data', (data) => {
        const response = data.toString();
        persistentBuffer += response;
        
        // Парсинг JSON-RPC ответов и notifications
        // Ищем полные JSON объекты в буфере (по балансу скобок)
        while (persistentBuffer.length > 0) {
            let jsonStart = -1;
            let braceCount = 0;
            let found = false;
            
            for (let i = 0; i < persistentBuffer.length; i++) {
                if (persistentBuffer[i] === '{') {
                    if (jsonStart === -1) {
                        jsonStart = i;
                        braceCount = 1;
                    } else {
                        braceCount++;
                    }
                } else if (persistentBuffer[i] === '}') {
                    braceCount--;
                    if (braceCount === 0 && jsonStart !== -1) {
                        // Найден полный JSON объект
                        const jsonStr = persistentBuffer.substring(jsonStart, i + 1);
                        try {
                            const json = JSON.parse(jsonStr);
                            
                            // Если это ответ на запрос (с id)
                            if (json.id !== undefined) {
                                const requestId = Number(json.id);
                                const pending = pendingRequests.get(requestId);
                                if (pending) {
                                    clearTimeout(pending.timeout);
                                    pendingRequests.delete(requestId);
                                    pending.resolve(json);
                                    console.log(`[Telnet] Received response for request ${requestId}, method: ${json.method || 'unknown'}`);
                                } else {
                                    console.log(`[Telnet] Received response for unknown request ${requestId} (pending requests: ${pendingRequests.size})`);
                                }
                            }
                            // Если это уведомление (notification) без id (например, keyboard_key_pressed)
                            else if (json.method && !json.id) {
                                console.log(`[Telnet] Received notification: ${json.method}`, JSON.stringify(json).substring(0, 200));
                                io.emit('telnet_result', json);
                                io.emit('telnet_result_device', { host: activeTelnetHost, result: json });
                            }
                            // Если это результат без id (статистика и т.д.)
                            else if (json.result) {
                                // Обновление статистики
                                if (json.result.stats) {
                                    currentStats = {
                                        ethercat: json.result.stats.ethercat || currentStats.ethercat,
                                        test: json.result.stats.test || currentStats.test,
                                        telnet: json.result.stats.telnet || currentStats.telnet
                                    };
                                    console.log('[Telnet] Stats updated:', JSON.stringify(currentStats, null, 2));
                                    io.emit('stats_update', currentStats);
                                }
                                
                                // Отправка всех результатов через WebSocket
                                io.emit('telnet_result', json.result);
                                io.emit('telnet_result_device', { host: activeTelnetHost, result: json.result });
                            }
                            
                            // Удаляем обработанный JSON из буфера
                            persistentBuffer = persistentBuffer.substring(i + 1);
                            found = true;
                            break;
                        } catch (parseError) {
                            // Невалидный JSON - удаляем и продолжаем
                            persistentBuffer = persistentBuffer.substring(i + 1);
                            found = true;
                            break;
                        }
                    }
                }
            }
            
            if (!found) {
                // Не найден полный JSON объект - оставляем в буфере для следующего пакета
                // Ограничиваем размер буфера (максимум 10KB)
                if (persistentBuffer.length > 10000) {
                    console.warn(`[Telnet] Buffer too large (${persistentBuffer.length} bytes), clearing...`);
                    persistentBuffer = '';
                }
                break;
            }
        }
        
        io.emit('telnet_response', response);
    });

    telnetClient.on('close', () => {
        console.log('[Telnet] Persistent connection closed (for notifications and API)');
        telnetConnected = false;
        telnetClient = null;
        persistentBuffer = ''; // Очищаем буфер при закрытии
        
        // Отклоняем все ожидающие запросы
        for (const [id, pending] of pendingRequests.entries()) {
            clearTimeout(pending.timeout);
            pending.reject(new Error('Connection closed'));
        }
        pendingRequests.clear();
        
        io.emit('telnet_status', { connected: false, host: activeTelnetHost, mode: 'persistent' });
        
        // Переподключение через задержку (экспоненциальная задержка)
        const delay = Math.min(RECONNECT_DELAY * Math.pow(1.5, reconnectAttempts - 1), 30000); // Максимум 30 секунд
        console.log(`[Telnet] Reconnecting for notifications and API in ${delay/1000} seconds...`);
        
        reconnectTimer = setTimeout(() => {
            reconnectTimer = null;
            connectTelnet();
        }, delay);
    });

    telnetClient.on('error', (err) => {
        const errorMsg = err.message;
        console.error(`[Telnet] Error: ${errorMsg}`);
        telnetConnected = false;
        
        // Не переподключаемся сразу при ошибке - ждем события 'close'
        // Но если это критическая ошибка, переподключаемся
        if (errorMsg.includes('ECONNRESET') || errorMsg.includes('ETIMEDOUT')) {
            // Эти ошибки обрабатываются через событие 'close'
            io.emit('telnet_status', { 
                connected: false, 
                host: activeTelnetHost, 
                error: errorMsg,
                reconnectAttempts: reconnectAttempts 
            });
        } else {
            // Другие ошибки - переподключаемся сразу
            io.emit('telnet_status', { 
                connected: false, 
                host: activeTelnetHost, 
                error: errorMsg 
            });
            if (telnetClient) {
                telnetClient.destroy();
            }
        }
    });
    
    telnetClient.on('timeout', () => {
        console.error(`[Telnet] Socket timeout`);
        telnetClient.destroy();
    });
}

// ============================================================================
// Telnet helpers (one-shot JSON-RPC, for discovery / per-device ops)
// ============================================================================

// Обработка очереди запросов
async function processQueue() {
    if (isProcessingQueue || requestQueue.length === 0) {
        return;
    }
    
    isProcessingQueue = true;
    console.log(`[Queue] Starting queue processing, ${requestQueue.length} requests in queue`);
    
    while (requestQueue.length > 0) {
        const { host, method, params, timeoutMs, resolve, reject } = requestQueue.shift();
        console.log(`[Queue] Processing ${method} to ${host} (${requestQueue.length} remaining)`);
        
        try {
            const result = await sendJsonRpcOnceDirect(host, method, params, timeoutMs);
            resolve(result);
            console.log(`[Queue] ✓ ${method} completed successfully`);
        } catch (error) {
            console.error(`[Queue] ✗ ${method} failed:`, error.message);
            reject(error);
        }
        
        // Небольшая задержка между запросами для предотвращения конфликтов
        if (requestQueue.length > 0) {
            console.log(`[Queue] Waiting ${QUEUE_DELAY_MS}ms before next request...`);
            await new Promise(resolve => setTimeout(resolve, QUEUE_DELAY_MS));
        }
    }
    
    console.log(`[Queue] Queue processing completed`);
    isProcessingQueue = false;
}

// Обертка для очереди запросов
// Использует постоянное соединение, если оно доступно, иначе создает one-shot соединение
function sendJsonRpcOnce(host, method, params = {}, timeoutMs = 800) {
    return new Promise((resolve, reject) => {
        if (!host || host.trim() === '') {
            reject(new Error('Host is required'));
            return;
        }
        
        // Если постоянное соединение активно и это тот же хост, используем его
        if (telnetConnected && telnetClient && host === activeTelnetHost) {
            console.log(`[sendJsonRpcOnce] Using persistent connection for ${method} to ${host}`);
            sendJsonRpcViaPersistent(host, method, params, timeoutMs)
                .then(resolve)
                .catch((err) => {
                    console.error(`[sendJsonRpcOnce] Persistent connection failed for ${method}, falling back to one-shot:`, err.message);
                    // Fallback на one-shot соединение при ошибке постоянного соединения
                    requestQueue.push({ host, method, params, timeoutMs, resolve, reject });
                    processQueue().catch(processErr => {
                        console.error(`[Queue] Error in processQueue:`, processErr);
                    });
                });
            return;
        }
        
        // Иначе используем очередь и one-shot соединения
        console.log(`[sendJsonRpcOnce] Persistent connection not available (connected: ${telnetConnected}, host: ${host}, active: ${activeTelnetHost}), using queue for ${method}`);
        console.log(`[Queue] Adding ${method} to queue (queue size: ${requestQueue.length + 1})`);
        requestQueue.push({ host, method, params, timeoutMs, resolve, reject });
        processQueue().catch(err => {
            console.error(`[Queue] Error in processQueue:`, err);
        });
    });
}

// Отправка JSON-RPC запроса через постоянное соединение
function sendJsonRpcViaPersistent(host, method, params = {}, timeoutMs = 800) {
    return new Promise((resolve, reject) => {
        if (!telnetConnected || !telnetClient) {
            reject(new Error('Persistent connection not available'));
            return;
        }
        
        // Проверяем, что соединение действительно готово
        if (telnetClient.destroyed || telnetClient.readyState !== 'open') {
            reject(new Error('Persistent connection is not ready'));
            return;
        }
        
        const id = Date.now() + Math.floor(Math.random() * 100000);
        const payload = { jsonrpc: "2.0", id, method, params };
        const cmdStr = JSON.stringify(payload) + '\r\n';
        
        const timeout = setTimeout(() => {
            const pending = pendingRequests.get(id);
            if (pending) {
                pendingRequests.delete(id);
                pending.reject(new Error(`timeout after ${timeoutMs}ms`));
            }
        }, timeoutMs);
        
        pendingRequests.set(id, { resolve, reject, timeout });
        
        console.log(`[Telnet] Sending ${method} via persistent connection (id=${id})`);
        try {
            telnetClient.write(cmdStr);
        } catch (err) {
            pendingRequests.delete(id);
            clearTimeout(timeout);
            reject(new Error(`Failed to write to persistent connection: ${err.message}`));
        }
    });
}

// Прямая отправка запроса (без очереди)
function sendJsonRpcOnceDirect(host, method, params = {}, timeoutMs = 800) {
    return new Promise((resolve, reject) => {
        const socket = new net.Socket();
        let buffer = '';
        const id = Date.now() + Math.floor(Math.random() * 100000);
        const payload = { jsonrpc: "2.0", id, method, params };

        const onError = (err) => {
            try { socket.destroy(); } catch (_) {}
            console.error(`[sendJsonRpcOnce] Error for ${method} to ${host}:${TELNET_PORT}:`, err.message);
            // Обновляем статус как отключенный только при критических ошибках
            if (err.message.includes('ECONNREFUSED') || err.message.includes('ETIMEDOUT')) {
                io.emit('telnet_status', { 
                    connected: false, 
                    host, 
                    error: err.message,
                    mode: 'one-shot'
                });
            }
            reject(err);
        };

        const timer = setTimeout(() => {
            console.error(`[sendJsonRpcOnce] Timeout after ${timeoutMs}ms for ${method} to ${host}:${TELNET_PORT}`);
            console.error(`[sendJsonRpcOnce] Buffer content: ${buffer.substring(0, 500)}`);
            onError(new Error(`timeout after ${timeoutMs}ms`));
        }, timeoutMs);

        socket.setTimeout(timeoutMs + 500, () => {
            console.error(`[sendJsonRpcOnce] Socket timeout for ${method} to ${host}:${TELNET_PORT}`);
            onError(new Error('socket timeout'));
        });

        socket.on('error', (err) => {
            clearTimeout(timer);
            console.error(`[sendJsonRpcOnce] Socket error for ${method} to ${host}:${TELNET_PORT}:`, err.message, err.code);
            onError(err);
        });

        console.log(`[sendJsonRpcOnce] Attempting connection to ${host}:${TELNET_PORT} for ${method}...`);
        socket.connect(TELNET_PORT, host, () => {
            const cmdStr = JSON.stringify(payload) + '\r\n';
            console.log(`[sendJsonRpcOnce] ✓ Connected to ${host}:${TELNET_PORT}, sending ${method} (id=${id})`);
            console.log(`[sendJsonRpcOnce] Command payload: ${cmdStr.substring(0, 200)}`);
            // Обновляем статус как подключенный при успешном подключении
            io.emit('telnet_status', { connected: true, host, mode: 'one-shot' });
            socket.write(cmdStr);
        });

        socket.on('data', (data) => {
            buffer += data.toString();
            
            // Пытаемся найти полный JSON объект в буфере
            // Ищем начало JSON объекта с правильным ID
            let processed = false;
            
            while (!processed && buffer.length > 0) {
                let jsonStart = -1;
                let braceCount = 0;
                let found = false;
                
                for (let i = 0; i < buffer.length; i++) {
                    if (buffer[i] === '{') {
                        if (jsonStart === -1) {
                            jsonStart = i;
                            braceCount = 1;
                        } else {
                            braceCount++;
                        }
                    } else if (buffer[i] === '}') {
                        braceCount--;
                        if (braceCount === 0 && jsonStart !== -1) {
                            // Найден полный JSON объект
                            const jsonStr = buffer.substring(jsonStart, i + 1);
                            try {
                                const json = JSON.parse(jsonStr);
                                const responseId = json && json.id !== undefined ? Number(json.id) : null;
                                const expectedId = Number(id);
                                
                            if (json && responseId === expectedId) {
                                clearTimeout(timer);
                                try { socket.destroy(); } catch (_) {}
                                console.log(`[sendJsonRpcOnce] Received response for ${method} (id=${id}):`, JSON.stringify(json).substring(0, 200));
                                // Обновляем статус как подключенный при успешном получении ответа
                                io.emit('telnet_status', { connected: true, host, mode: 'one-shot', lastSuccess: Date.now() });
                                resolve(json);
                                return;
                                } else if (json && json.id !== undefined) {
                                    // Другой ответ - удаляем его из буфера и продолжаем поиск
                                    buffer = buffer.substring(i + 1);
                                    found = true;
                                    break;
                                } else if (json && json.method && !json.id) {
                                    // Это notification (например, keyboard_key_pressed) - отправляем в веб-интерфейс
                                    console.log(`[sendJsonRpcOnce] Received notification: ${json.method}`);
                                    io.emit('telnet_result', json);
                                    // Удаляем notification из буфера и продолжаем поиск ответа
                                    buffer = buffer.substring(i + 1);
                                    found = true;
                                    break;
                                }
                            } catch (e) {
                                // Невалидный JSON - удаляем и продолжаем
                                buffer = buffer.substring(i + 1);
                                found = true;
                                break;
                            }
                        }
                    }
                }
                
                if (!found) {
                    processed = true;
                }
            }
            
            // Если буфер слишком большой (больше 10KB), очищаем его
            if (buffer.length > 10000) {
                console.warn(`[sendJsonRpcOnce] Buffer too large (${buffer.length} bytes), clearing...`);
                buffer = '';
            }
        });


        socket.on('close', () => {
            // If closed without response, timeout handler will fire (or error already)
            if (buffer.length > 0) {
                console.log(`[sendJsonRpcOnce] Socket closed with buffer: ${buffer.substring(0, 200)}`);
            }
        });
    });
}

// Инициализация SyncManager (после определения sendJsonRpcOnce)
syncManager = new SyncManager(deviceProfiles, deviceRegistry, sendJsonRpcOnce);

async function probeDevice(host) {
    const sys = await sendJsonRpcOnce(host, 'get_system_info', {}, 800);
    const netCfg = await sendJsonRpcOnce(host, 'get_network_config', {}, 800);
    const system = sys?.result?.system || {};
    const network = netCfg?.result?.network || {};
    
    // Автоматическое определение профиля устройства
    if (syncManager && system) {
        const profileResult = await syncManager.detectDeviceProfile(host);
        if (profileResult.success) {
            console.log(`[probeDevice] Detected profile ${profileResult.profile} for ${host}`);
        }
    }
    
    return {
        host,
        system,
        network,
        lastSeen: Date.now()
    };
}

async function scanDaisyChain(startHost, maxHops = 32) {
    const visited = new Set();
    const found = [];
    let current = startHost;

    for (let hop = 0; hop < maxHops; hop++) {
        if (!current || visited.has(current)) break;
        visited.add(current);

        try {
            const dev = await probeDevice(current);
            found.push(dev);
            devices.set(current, dev);
            // Сохраняем устройства при обнаружении нового
            localStorageManager.saveDevices(devices);
            io.emit('devices_scan_progress', { host: current, ok: true, hop });

            const nextIp = (dev.network?.next_device_ip || '').trim();
            if (!nextIp || nextIp === '0.0.0.0') break;
            current = nextIp;
        } catch (e) {
            io.emit('devices_scan_progress', { host: current, ok: false, hop, error: String(e?.message || e) });
            break;
        }
    }

    return found;
}

// ============================================================================
// Devices API (discovery + active selection)
// ============================================================================

app.get('/api/devices', (req, res) => {
    res.json({
        active: activeTelnetHost,
        devices: Array.from(devices.values())
    });
});

app.post('/api/devices/active', (req, res) => {
    const ip = (req.body && req.body.ip) ? String(req.body.ip) : '';
    if (!ip) return res.status(400).json({ error: 'ip required' });
    setActiveDeviceHost(ip);
    res.json({ success: true, active: activeTelnetHost });
});

app.post('/api/devices/scan', async (req, res) => {
    const start_ip = (req.body && req.body.start_ip) ? String(req.body.start_ip) : activeTelnetHost;
    const max_hops = (req.body && req.body.max_hops) ? Number(req.body.max_hops) : 32;
    try {
        const list = await scanDaisyChain(start_ip, max_hops);
        io.emit('devices_update', { active: activeTelnetHost, devices: Array.from(devices.values()) });
        res.json({ success: true, count: list.length, devices: list });
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// Per-device API (one-shot connect)
app.post('/api/devices/:ip/network/next', async (req, res) => {
    const host = req.params.ip;
    const { next_device_ip } = req.body || {};
    try {
        const resp = await sendJsonRpcOnce(host, 'set_next_device_ip', { next_device_ip }, 2000);
        res.json(resp);
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

app.get('/api/devices/:ip/gpio/channels', async (req, res) => {
    const host = req.params.ip;
    try {
        const resp = await sendJsonRpcOnce(host, 'get_gpio_channels', {}, 3000); // Увеличен timeout до 3 секунд
        if (resp && resp.success !== undefined) {
            res.json(resp);
        } else {
            // Если ответ не содержит success, считаем это ошибкой
            res.status(503).json({ success: false, error: 'Invalid response from device' });
        }
    } catch (e) {
        const errorMsg = String(e?.message || e);
        console.error(`[API] get_gpio_channels error for ${host}:`, errorMsg);
        // Возвращаем 503 (Service Unavailable) вместо 500 для ошибок соединения
        const status = (errorMsg.includes('ECONNREFUSED') || errorMsg.includes('ETIMEDOUT') || errorMsg.includes('timeout'))
            ? 503
            : 500;
        const friendlyError = (errorMsg.includes('ECONNREFUSED') || errorMsg.includes('ETIMEDOUT') || errorMsg.includes('timeout'))
            ? `Cannot connect to ESP32 at ${host}:23. Check if device is powered on and connected to network.`
            : errorMsg;
        res.status(status).json({ success: false, error: friendlyError, host });
    }
});

// API: Сохранение канала в локальное хранилище (НЕ на ESP32)
app.post('/api/devices/:ip/gpio/channel', async (req, res) => {
    const host = req.params.ip;
    const params = req.body || {};
    try {
        console.log(`[API] Saving channel locally for ${host}:`, JSON.stringify(params));
        
        // Получаем или создаем массив каналов для этого устройства
        if (!localChannels.has(host)) {
            localChannels.set(host, []);
        }
        const channels = localChannels.get(host);
        
        // Если указан index, обновляем существующий канал, иначе добавляем новый
        if (params.index !== undefined && params.index !== null && params.index !== '') {
            const idx = parseInt(params.index, 10);
            if (idx >= 0 && idx < channels.length) {
                // Обновляем существующий канал
                channels[idx] = { ...params, index: idx };
                console.log(`[API] Updated channel at index ${idx}`);
            } else {
                // Индекс вне диапазона - добавляем как новый
                const newIdx = channels.length;
                channels.push({ ...params, index: newIdx });
                console.log(`[API] Added new channel at index ${newIdx} (requested ${idx} was out of range)`);
            }
        } else {
            // Добавляем новый канал
            const newIdx = channels.length;
            channels.push({ ...params, index: newIdx });
            console.log(`[API] Added new channel at index ${newIdx}`);
        }
        
        // Сохраняем в файл после изменения
        localStorageManager.saveChannels(localChannels);
        
        // Возвращаем успешный ответ
        res.json({
            success: true,
            result: {
                index: params.index !== undefined ? parseInt(params.index, 10) : channels.length - 1,
                count: channels.length
            }
        });
    } catch (e) {
        console.error(`[API] save_channel_local error:`, e.message);
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Получение локальных каналов (с сервера, НЕ с ESP32)
app.get('/api/devices/:ip/gpio/channels/local', async (req, res) => {
    const host = req.params.ip;
    try {
        const channels = localChannels.get(host) || [];
        res.json({
            success: true,
            result: {
                channels: channels,
                count: channels.length
            }
        });
    } catch (e) {
        console.error(`[API] get_local_channels error:`, e.message);
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Синхронизация локальных каналов с ESP32 (управляемая)
app.post('/api/devices/:ip/gpio/channels/sync', async (req, res) => {
    const host = req.params.ip;
    const { save_profile_before_sync = false, profile_name } = req.body || {};
    
    try {
        const channels = localChannels.get(host) || [];
        if (channels.length === 0) {
            return res.json({ success: true, message: 'No channels to sync', synced: 0 });
        }
        
        // Сохраняем сырой профиль перед синхронизацией, если запрошено
        let savedProfile = null;
        if (save_profile_before_sync) {
            const config = {
                gpio_channels: channels,
                device_ip: host,
                synced_at: new Date().toISOString()
            };
            const saveResult = localStorageManager.saveProfile(host, profile_name || 'channels_pre_sync', config);
            if (saveResult.success) {
                savedProfile = saveResult.filename;
                console.log(`[API] Saved profile before channel sync: ${savedProfile}`);
            }
        }
        
        console.log(`[API] Syncing ${channels.length} channels to ${host}...`);
        const results = [];
        let successCount = 0;
        let errorCount = 0;
        
        // Отправляем каналы последовательно на ESP32
        for (const channel of channels) {
            try {
                const resp = await sendJsonRpcOnce(host, 'set_gpio_channel', channel, 5000);
                if (resp.success) {
                    successCount++;
                    results.push({ index: channel.index, success: true });
                } else {
                    errorCount++;
                    results.push({ index: channel.index, success: false, error: resp.error || 'Unknown error' });
                }
            } catch (e) {
                errorCount++;
                results.push({ index: channel.index, success: false, error: e.message });
            }
        }
        
        console.log(`[API] Sync completed: ${successCount} success, ${errorCount} errors`);
        
        // НЕ сохраняем автоматически - синхронизация управляемая
        // Пользователь должен явно сохранить профиль перед синхронизацией
        
        const response = {
            success: true,
            synced: successCount,
            errors: errorCount,
            results: results
        };
        
        if (savedProfile) {
            response.saved_profile = savedProfile;
        }
        
        res.json(response);
    } catch (e) {
        console.error(`[API] sync_channels error:`, e.message);
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Удаление канала из локального хранилища
app.post('/api/devices/:ip/gpio/channel/delete/local', async (req, res) => {
    const host = req.params.ip;
    const { index } = req.body || {};
    try {
        if (index === undefined || index === null) {
            return res.status(400).json({ success: false, error: 'Index is required' });
        }
        
        const channels = localChannels.get(host) || [];
        const idx = parseInt(index, 10);
        
        if (idx >= 0 && idx < channels.length) {
            channels.splice(idx, 1);
            // Обновляем индексы оставшихся каналов
            channels.forEach((ch, i) => { ch.index = i; });
            console.log(`[API] Deleted channel at index ${idx} from local storage`);
            
            // Сохраняем в файл после удаления
            localStorageManager.saveChannels(localChannels);
            
            res.json({ success: true, count: channels.length });
        } else {
            res.status(404).json({ success: false, error: 'Channel not found' });
        }
    } catch (e) {
        console.error(`[API] delete_channel_local error:`, e.message);
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

app.post('/api/devices/:ip/gpio/channel/delete', async (req, res) => {
    const host = req.params.ip;
    const params = req.body || {};
    try {
        console.log(`[API] delete_gpio_channel to ${host}:`, JSON.stringify(params));
        const resp = await sendJsonRpcOnce(host, 'delete_gpio_channel', params, 3000);
        res.json(resp);
    } catch (e) {
        console.error(`[API] delete_gpio_channel error:`, e.message);
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

app.get('/api/devices/:ip/encoders', async (req, res) => {
    const host = req.params.ip;
    try {
        const resp = await sendJsonRpcOnce(host, 'get_encoders', {}, 1200);
        res.json(resp);
    } catch (e) {
        const errorMsg = String(e?.message || e);
        const isConnError = errorMsg.includes('ECONNREFUSED') || errorMsg.includes('ETIMEDOUT') || errorMsg.includes('timeout');
        const friendlyError = isConnError
            ? `Cannot connect to ESP32 at ${host}:23. Check if device is powered on and connected to network.`
            : errorMsg;
        res.status(isConnError ? 503 : 500).json({ success: false, error: friendlyError, host });
    }
});

// StepGen (LinuxCNC STEP/DIR)
app.get('/api/devices/:ip/stepgen/config', async (req, res) => {
    const host = req.params.ip;
    try {
        const resp = await sendJsonRpcOnce(host, 'get_stepgen_config', {}, 2000);
        res.json(resp);
    } catch (e) {
        const errorMsg = String(e?.message || e);
        const isConnError = errorMsg.includes('ECONNREFUSED') || errorMsg.includes('ETIMEDOUT') || errorMsg.includes('timeout');
        const friendlyError = isConnError
            ? `Cannot connect to ESP32 at ${host}:23. Check if device is powered on and connected to network.`
            : errorMsg;
        res.status(isConnError ? 503 : 500).json({ success: false, error: friendlyError, host });
    }
});

app.post('/api/devices/:ip/stepgen/config', async (req, res) => {
    const host = req.params.ip;
    const params = req.body || {};
    try {
        const resp = await sendJsonRpcOnce(host, 'set_stepgen_config', params, 2000);
        res.json(resp);
    } catch (e) {
        const errorMsg = String(e?.message || e);
        const isConnError = errorMsg.includes('ECONNREFUSED') || errorMsg.includes('ETIMEDOUT') || errorMsg.includes('timeout');
        const friendlyError = isConnError
            ? `Cannot connect to ESP32 at ${host}:23. Check if device is powered on and connected to network.`
            : errorMsg;
        res.status(isConnError ? 503 : 500).json({ success: false, error: friendlyError, host });
    }
});

app.post('/api/devices/:ip/encoder', async (req, res) => {
    const host = req.params.ip;
    const params = req.body || {};
    try {
        const resp = await sendJsonRpcOnce(host, 'set_encoder', params, 2000);
        res.json(resp);
    } catch (e) {
        const errorMsg = String(e?.message || e);
        const isConnError = errorMsg.includes('ECONNREFUSED') || errorMsg.includes('ETIMEDOUT') || errorMsg.includes('timeout');
        const friendlyError = isConnError
            ? `Cannot connect to ESP32 at ${host}:23. Check if device is powered on and connected to network.`
            : errorMsg;
        res.status(isConnError ? 503 : 500).json({ success: false, error: friendlyError, host });
    }
});

app.post('/api/devices/:ip/encoder/delete', async (req, res) => {
    const host = req.params.ip;
    const params = req.body || {};
    try {
        const resp = await sendJsonRpcOnce(host, 'delete_encoder', params, 2000);
        res.json(resp);
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

app.post('/api/devices/:ip/encoder/reset', async (req, res) => {
    const host = req.params.ip;
    const params = req.body || {};
    try {
        const resp = await sendJsonRpcOnce(host, 'reset_encoder', params, 2000);
        res.json(resp);
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// ============================================================================
// ESP-NOW per-device (JSON-RPC one-shot, does not depend on active socket)
// ============================================================================

app.get('/api/devices/:ip/espnow/status', async (req, res) => {
    const host = req.params.ip;
    try {
        const resp = await sendJsonRpcOnce(host, 'get_espnow_status', {}, 2500);
        res.json(resp);
    } catch (e) {
        const errorMsg = String(e?.message || e);
        // Если это ошибка подключения, возвращаем 503, иначе 500
        if (errorMsg.includes('ECONNREFUSED') || errorMsg.includes('ETIMEDOUT') || errorMsg.includes('timeout')) {
            res.status(503).json({ success: false, error: `Device ${host} is unavailable: ${errorMsg}` });
        } else {
            res.status(500).json({ success: false, error: errorMsg });
        }
    }
});

app.post('/api/devices/:ip/espnow/discover', async (req, res) => {
    const host = req.params.ip;
    try {
        const resp = await sendJsonRpcOnce(host, 'discover_espnow_devices', {}, 9000);
        res.json(resp);
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

app.get('/api/devices/:ip/espnow/configs', async (req, res) => {
    const host = req.params.ip;
    try {
        const resp = await sendJsonRpcOnce(host, 'get_all_configs', {}, 4000);
        res.json(resp);
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

app.post('/api/devices/:ip/espnow/configs/refresh', async (req, res) => {
    const host = req.params.ip;
    try {
        const resp = await sendJsonRpcOnce(host, 'refresh_all_configs', {}, 9000);
        res.json(resp);
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Тест USB клавиатуры
app.post('/api/devices/:ip/test/usb-keyboard', async (req, res) => {
    const host = req.params.ip;
    const { key_code, modifiers = 0, duration_ms = 100 } = req.body || {};
    
    if (key_code === undefined) {
        return res.status(400).json({ success: false, error: 'key_code is required' });
    }
    
    try {
        const resp = await sendJsonRpcOnce(host, 'test_usb_keyboard', {
            key_code: Number(key_code),
            modifiers: Number(modifiers),
            duration_ms: Number(duration_ms)
        }, 2000);
        res.json(resp);
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Валидация всех USB keyboard каналов (health check)
app.post('/api/devices/:ip/validate/usb-keyboard', async (req, res) => {
    const host = req.params.ip;
    const { test_enabled = false, test_duration_ms = 50 } = req.body || {};
    
    try {
        const resp = await sendJsonRpcOnce(host, 'validate_usb_keyboard', {
            test_enabled: Boolean(test_enabled),
            test_duration_ms: Number(test_duration_ms)
        }, 5000); // Больше timeout, т.к. может быть много каналов
        res.json(resp);
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Отправка команды через Telnet
app.post('/api/command', (req, res) => {
    const { command } = req.body;
    
    if (!command) return res.status(400).json({ success: false, error: 'command is required' });
    const host = activeTelnetHost;
    // Legacy endpoint: previously required persistent telnet connection.
    // Make it robust by using one-shot JSON-RPC.
    sendJsonRpcOnce(host, command, {}, 1500)
        .then((resp) => {
            // Also push to websocket listeners for UI consistency
            if (resp && resp.result) io.emit('telnet_result', resp.result);
            res.json(resp);
        })
        .catch((e) => res.status(503).json({ success: false, error: String(e?.message || e) }));
});

// API: Получение системной информации
app.get('/api/system/info', async (req, res) => {
    try {
        // Используем хост из запроса или активный хост
        const host = req.query.host || activeTelnetHost || DEFAULT_TELNET_HOST;
        if (!host) {
            return res.status(503).json({ success: false, error: 'No active device selected' });
        }
        // Убеждаемся, что activeTelnetHost установлен
        if (host !== activeTelnetHost) {
            setActiveDeviceHost(host);
        }
        const resp = await sendJsonRpcOnce(host, 'get_system_info', {}, 1500);
        if (resp && resp.result) io.emit('telnet_result', resp.result);
        res.json(resp);
    } catch (e) {
        const errorMsg = String(e?.message || e);
        const host = activeTelnetHost || DEFAULT_TELNET_HOST;
        const friendlyError = errorMsg.includes('ECONNREFUSED') || errorMsg.includes('ETIMEDOUT') || errorMsg.includes('timeout')
            ? `Cannot connect to ESP32 at ${host}:23. Check if device is powered on and connected to network.`
            : errorMsg;
        res.status(503).json({ success: false, error: friendlyError, host });
    }
});

// API: Получение сетевой конфигурации
app.get('/api/network/config', async (req, res) => {
    try {
        const host = activeTelnetHost || DEFAULT_TELNET_HOST;
        if (!host) {
            return res.status(503).json({ success: false, error: 'No active device selected' });
        }
        const resp = await sendJsonRpcOnce(host, 'get_network_config', {}, 1500);
        if (resp && resp.result) io.emit('telnet_result', resp.result);
        res.json(resp);
    } catch (e) {
        const errorMsg = String(e?.message || e);
        const host = activeTelnetHost || DEFAULT_TELNET_HOST;
        const friendlyError = errorMsg.includes('ECONNREFUSED') || errorMsg.includes('ETIMEDOUT') || errorMsg.includes('timeout')
            ? `Cannot connect to ESP32 at ${host}:23. Check if device is powered on and connected to network.`
            : errorMsg;
        res.status(503).json({ success: false, error: friendlyError, host });
    }
});

// API: Получение полной конфигурации
app.get('/api/config', async (req, res) => {
    try {
        const host = activeTelnetHost || DEFAULT_TELNET_HOST;
        console.log(`[API] /api/config requested, host: ${host}`);
        if (!host) {
            return res.status(503).json({ success: false, error: 'No active device selected' });
        }
        console.log(`[API] Calling get_config on ${host}:${TELNET_PORT}`);
        const resp = await sendJsonRpcOnce(host, 'get_config', {}, 8000); // Увеличен таймаут до 8 секунд
        console.log(`[API] get_config response received:`, resp ? 'success' : 'null');
        if (resp && resp.result) {
            console.log(`[API] get_config response size:`, JSON.stringify(resp).length, 'bytes');
        }
        if (resp && resp.result) io.emit('telnet_result', resp.result);
        res.json(resp);
    } catch (e) {
        const errorMsg = String(e?.message || e);
        const host = activeTelnetHost || DEFAULT_TELNET_HOST;
        console.error(`[API] /api/config error:`, errorMsg);
        const friendlyError = errorMsg.includes('ECONNREFUSED') || errorMsg.includes('ETIMEDOUT') || errorMsg.includes('timeout')
            ? `Cannot connect to ESP32 at ${host}:23. Check if device is powered on and connected to network.`
            : errorMsg;
        res.status(503).json({ success: false, error: friendlyError, host });
    }
});

// API: Получение конфигурации конкретного устройства
app.get('/api/devices/:ip/config', async (req, res) => {
    const host = req.params.ip;
    try {
        console.log(`[API] Getting config from ${host}:${TELNET_PORT}`);
        const resp = await sendJsonRpcOnce(host, 'get_config', {}, 8000);
        console.log(`[API] get_config response received from ${host}:`, resp ? 'success' : 'null');
        if (resp && resp.result) {
            console.log(`[API] get_config response size:`, JSON.stringify(resp).length, 'bytes');
        }
        res.json(resp);
    } catch (e) {
        const errorMsg = String(e?.message || e);
        const status = (errorMsg.includes('ECONNREFUSED') || errorMsg.includes('ETIMEDOUT') || errorMsg.includes('timeout')) ? 503 : 500;
        res.status(status).json({ success: false, error: errorMsg, host });
    }
});

// API: Получение списка GPIO каналов
app.get('/api/gpio/channels', async (req, res) => {
    try {
        const host = activeTelnetHost || DEFAULT_TELNET_HOST;
        if (!host) {
            return res.status(503).json({ success: false, error: 'No active device selected' });
        }
        const resp = await sendJsonRpcOnce(host, 'get_gpio_channels', {}, 5000);
        if (resp && resp.result) io.emit('telnet_result', resp.result);
        res.json(resp);
    } catch (e) {
        const errorMsg = String(e?.message || e);
        const host = activeTelnetHost || DEFAULT_TELNET_HOST;
        const friendlyError = errorMsg.includes('ECONNREFUSED') || errorMsg.includes('ETIMEDOUT') || errorMsg.includes('timeout')
            ? `Cannot connect to ESP32 at ${host}:23. Check if device is powered on and connected to network.`
            : errorMsg;
        res.status(503).json({ success: false, error: friendlyError, host });
    }
});

// API: Получение списка энкодеров
app.get('/api/encoders', async (req, res) => {
    try {
        const host = activeTelnetHost || DEFAULT_TELNET_HOST;
        console.log(`[API] /api/encoders requested, host: ${host}`);
        if (!host) {
            return res.status(503).json({ success: false, error: 'No active device selected' });
        }
        console.log(`[API] Calling get_encoders on ${host}:${TELNET_PORT}`);
        const resp = await sendJsonRpcOnce(host, 'get_encoders', {}, 8000); // Увеличен таймаут до 8 секунд
        console.log(`[API] get_encoders response received:`, resp ? 'success' : 'null');
        if (resp && resp.result) {
            console.log(`[API] get_encoders response size:`, JSON.stringify(resp).length, 'bytes');
        }
        if (resp && resp.result) io.emit('telnet_result', resp.result);
        res.json(resp);
    } catch (e) {
        const errorMsg = String(e?.message || e);
        const host = activeTelnetHost || DEFAULT_TELNET_HOST;
        console.error(`[API] /api/encoders error:`, errorMsg);
        const friendlyError = errorMsg.includes('ECONNREFUSED') || errorMsg.includes('ETIMEDOUT') || errorMsg.includes('timeout')
            ? `Cannot connect to ESP32 at ${host}:23. Check if device is powered on and connected to network.`
            : errorMsg;
        res.status(503).json({ success: false, error: friendlyError, host });
    }
});

// API: Получение конфигурации IO Expansion
app.get('/api/io-expansion/config', async (req, res) => {
    try {
        const host = activeTelnetHost || DEFAULT_TELNET_HOST;
        if (!host) {
            return res.status(503).json({ success: false, error: 'No active device selected' });
        }
        const resp = await sendJsonRpcOnce(host, 'get_io_expansion_config', {}, 2000);
        if (resp && resp.result) io.emit('telnet_result', resp.result);
        res.json(resp);
    } catch (e) {
        const errorMsg = String(e?.message || e);
        const host = activeTelnetHost || DEFAULT_TELNET_HOST;
        const friendlyError = errorMsg.includes('ECONNREFUSED') || errorMsg.includes('ETIMEDOUT') || errorMsg.includes('timeout')
            ? `Cannot connect to ESP32 at ${host}:23. Check if device is powered on and connected to network.`
            : errorMsg;
        res.status(503).json({ success: false, error: friendlyError, host });
    }
});

// API: Установка конфигурации IO Expansion
app.post('/api/io-expansion/config', async (req, res) => {
    try {
        const host = activeTelnetHost;
        const params = req.body || {};
        const resp = await sendJsonRpcOnce(host, 'set_io_expansion_config', params, 2500);
        if (resp && resp.result) io.emit('telnet_result', resp.result);
        res.json(resp);
    } catch (e) {
        res.status(503).json({ success: false, error: String(e?.message || e) });
    }
});

// ============================================================================
// ESP-NOW (Day 63)
// ============================================================================

// API: ESP-NOW status
app.get('/api/espnow/status', async (req, res) => {
    try {
        const host = activeTelnetHost;
        const resp = await sendJsonRpcOnce(host, 'get_espnow_status', {}, 2000);
        if (resp && resp.result) io.emit('telnet_result', resp.result);
        res.json(resp);
    } catch (e) {
        res.status(503).json({ success: false, error: String(e?.message || e) });
    }
});

// API: ESP-NOW discovery
app.post('/api/espnow/discover', async (req, res) => {
    try {
        const host = activeTelnetHost;
        const resp = await sendJsonRpcOnce(host, 'discover_espnow_devices', {}, 4000);
        if (resp && resp.result) io.emit('telnet_result', resp.result);
        res.json(resp);
    } catch (e) {
        res.status(503).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Get all cached configs (Aggregator)
app.get('/api/espnow/configs', async (req, res) => {
    try {
        const host = activeTelnetHost;
        const resp = await sendJsonRpcOnce(host, 'get_all_configs', {}, 4000);
        if (resp && resp.result) io.emit('telnet_result', resp.result);
        res.json(resp);
    } catch (e) {
        res.status(503).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Refresh all cached configs (Aggregator refresh)
app.post('/api/espnow/configs/refresh', async (req, res) => {
    try {
        const host = activeTelnetHost;
        const resp = await sendJsonRpcOnce(host, 'refresh_all_configs', {}, 6000);
        if (resp && resp.result) io.emit('telnet_result', resp.result);
        res.json(resp);
    } catch (e) {
        res.status(503).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Получение статистики
app.get('/api/stats', (req, res) => {
    res.json(currentStats);
});

// API: Запуск теста
// ============================================================================
// Import configuration endpoint (legacy, импорт теперь делается на frontend)
// ============================================================================
// Примечание: Импорт реализован на frontend через существующие API endpoints
// (set_gpio_channel, set_encoder, set_stepgen_config и т.д.)
// Это более гибко и не требует реализации set_config на ESP32

// ============================================================================
// Export endpoints (LinuxCNC HAL / TwinCAT ESI)
// ============================================================================

app.post('/api/export/linuxcnc', (req, res) => {
    const { mapping } = req.body || {};
    if (!mapping || !Array.isArray(mapping)) {
        return res.status(400).json({ error: 'Invalid mapping data' });
    }

    let hal = '# LinuxCNC HAL configuration for ESP32 EtherCAT Slave\n';
    hal += '# Generated automatically from PDO mapping\n';
    hal += '# Usage: Add this to your .hal file or load via: loadrt ethercat config="path/to/this/file.hal"\n\n';

    // Group by device (assume station address = device index in chain)
    const byDevice = {};
    let deviceIdx = 0;
    const deviceOrder = [];
    for (const m of mapping) {
        if (!byDevice[m.device]) {
            byDevice[m.device] = [];
            deviceOrder.push(m.device);
        }
        byDevice[m.device].push(m);
    }

    // Generate HAL pins for each device
    for (let idx = 0; idx < deviceOrder.length; idx++) {
        const device = deviceOrder[idx];
        const signals = byDevice[device];
        const stationAddr = idx + 1; // Station address (1-based)

        hal += `# ========================================\n`;
        hal += `# Device ${stationAddr}: ${device}\n`;
        hal += `# ========================================\n\n`;

        // Outputs (Master → Slave)
        const outputs = signals.filter(s => s.direction === 'OUT');
        if (outputs.length > 0) {
            hal += `# Outputs (Master writes to Slave ${stationAddr})\n`;
            for (const sig of outputs) {
                const pinName = sig.signal.toLowerCase().replace(/[^a-z0-9]/g, '_');
                if (sig.type === 'DIGITAL_OUT' || sig.type === 'STEPGEN_OUT') {
                    hal += `setp ethercat.0.${stationAddr}.${pinName} 0  # ${sig.type} - ${sig.source}\n`;
                } else if (sig.type === 'ANALOG_OUT') {
                    hal += `setp ethercat.0.${stationAddr}.${pinName} 0.0  # ${sig.type} - ${sig.source}\n`;
                }
            }
            hal += '\n';
        }

        // Inputs (Slave → Master)
        const inputs = signals.filter(s => s.direction === 'IN');
        if (inputs.length > 0) {
            hal += `# Inputs (Slave ${stationAddr} writes to Master)\n`;
            for (const sig of inputs) {
                const pinName = sig.signal.toLowerCase().replace(/[^a-z0-9]/g, '_');
                hal += `net input:${pinName} ethercat.0.${stationAddr}.${pinName}  # ${sig.type} - ${sig.source}\n`;
            }
            hal += '\n';
        }
    }

    hal += '# Example connections:\n';
    hal += '# net estop-out <= iocontrol.0.user-enable-out\n';
    hal += '# net estop-out => ethercat.0.1.estop_in\n';
    hal += '# net step-x <= stepgen.0.step\n';
    hal += '# net step-x => ethercat.0.1.stepgen_axis0_out\n\n';

    res.setHeader('Content-Type', 'text/plain');
    res.setHeader('Content-Disposition', 'attachment; filename="ethercat_hal.hal"');
    res.send(hal);
});

app.post('/api/export/twincat', (req, res) => {
    const { mapping } = req.body || {};
    if (!mapping || !Array.isArray(mapping)) {
        return res.status(400).json({ error: 'Invalid mapping data' });
    }

    // Group by device
    const byDevice = {};
    const deviceOrder = [];
    for (const m of mapping) {
        if (!byDevice[m.device]) {
            byDevice[m.device] = [];
            deviceOrder.push(m.device);
        }
        byDevice[m.device].push(m);
    }

    let xml = '<?xml version="1.0" encoding="UTF-8"?>\n';
    xml += '<EtherCATConfig xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance" xsi:noNamespaceSchemaLocation="http://www.ethercat.org/schema/EtherCATConfig.xsd">\n';
    xml += '  <Config>\n';
    xml += '    <Groups>\n';
    xml += '      <Group>\n';
    xml += '        <Name>ESP32_Slaves</Name>\n';
    xml += '        <Type>Master</Type>\n';
    xml += '        <Devices>\n';

    let deviceIdx = 0;
    for (const device of deviceOrder) {
        const signals = byDevice[device];
        deviceIdx++;
        const stationAddr = deviceIdx;

        // Calculate PDO sizes
        const outputs = signals.filter(s => s.direction === 'OUT');
        const inputs = signals.filter(s => s.direction === 'IN');
        const outSize = Math.max(32, Math.ceil(outputs.reduce((acc, s) => Math.max(acc, s.pdo_byte + s.size_bytes), 0) / 4) * 4);
        const inSize = Math.max(32, Math.ceil(inputs.reduce((acc, s) => Math.max(acc, s.pdo_byte + s.size_bytes), 0) / 4) * 4);

        xml += `          <Device>\n`;
        xml += `            <Name>ESP32_Slave_${stationAddr}</Name>\n`;
        xml += `            <Type ProductCode="#x00000001" Revision="#x00010000" Vendor="#x00000000">ESP32 EtherCAT Slave</Type>\n`;
        xml += `            <Address>${stationAddr}</Address>\n`;
        xml += `            <Mailbox>\n`;
        xml += `              <Address>0x1000</Address>\n`;
        xml += `              <Size>128</Size>\n`;
        xml += `            </Mailbox>\n`;
        xml += `            <ProcessData>\n`;
        xml += `              <Output Size="${outSize}">\n`;
        // Sort outputs by PDO byte
        for (const sig of outputs.sort((a, b) => a.pdo_byte - b.pdo_byte)) {
            const bitLen = sig.pdo_bit !== null ? 1 : sig.size_bytes * 8;
            xml += `                <Entry Name="${sig.signal.replace(/[^a-zA-Z0-9_]/g, '_')}" Offset="${sig.pdo_byte}" BitLen="${bitLen}" DataType="${sig.type.includes('ANALOG') ? 'INT' : 'BOOL'}"/>\n`;
        }
        xml += `              </Output>\n`;
        xml += `              <Input Size="${inSize}">\n`;
        // Sort inputs by PDO byte
        for (const sig of inputs.sort((a, b) => a.pdo_byte - b.pdo_byte)) {
            const bitLen = sig.pdo_bit !== null ? 1 : sig.size_bytes * 8;
            xml += `                <Entry Name="${sig.signal.replace(/[^a-zA-Z0-9_]/g, '_')}" Offset="${sig.pdo_byte}" BitLen="${bitLen}" DataType="${sig.type.includes('ANALOG') || sig.type === 'ENCODER' ? 'INT' : 'BOOL'}"/>\n`;
        }
        xml += `              </Input>\n`;
        xml += `            </ProcessData>\n`;
        xml += `          </Device>\n`;
    }

    xml += '        </Devices>\n';
    xml += '      </Group>\n';
    xml += '    </Groups>\n';
    xml += '  </Config>\n';
    xml += '</EtherCATConfig>\n';

    res.setHeader('Content-Type', 'application/xml');
    res.setHeader('Content-Disposition', 'attachment; filename="ethercat_slave.esi.xml"');
    res.send(xml);
});

// WebSocket для real-time обновлений
io.on('connection', (socket) => {
    console.log('[WebSocket] Client connected');
    
    // Отправка текущей статистики
    socket.emit('stats_update', currentStats);
    // При использовании one-shot соединений считаем, что соединение доступно, если есть активный хост
    socket.emit('telnet_status', { 
        connected: !!activeTelnetHost, 
        host: activeTelnetHost,
        mode: 'one-shot' // Указываем режим работы
    });
    socket.emit('devices_update', { active: activeTelnetHost, devices: Array.from(devices.values()) });
    
    socket.on('disconnect', () => {
        console.log('[WebSocket] Client disconnected');
    });
});

// Периодическое обновление статистики (EtherCAT/Telnet, без теста)
// Используем one-shot соединения вместо постоянного
// Увеличена задержка до 5 секунд для снижения нагрузки
setInterval(() => {
    const host = activeTelnetHost || DEFAULT_TELNET_HOST;
    if (host) {
        // Используем one-shot соединение для запроса статистики
        sendJsonRpcOnce(host, 'get_stats', {}, 1000).then(resp => {
            if (resp && resp.result && resp.result.stats) {
                currentStats = {
                    ethercat: resp.result.stats.ethercat || currentStats.ethercat,
                    test: resp.result.stats.test || currentStats.test,
                    telnet: resp.result.stats.telnet || currentStats.telnet
                };
                io.emit('stats_update', currentStats);
            }
        }).catch(err => {
            // Игнорируем ошибки периодического обновления
        });
    }
}, 5000);

// ============================================================================
// Device Profiles API endpoints
// ============================================================================

// API: Получение списка всех профилей устройств
app.get('/api/device-profiles', (req, res) => {
    try {
        const profiles = deviceProfiles.list();
        res.json({ success: true, profiles });
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Получение информации о конкретном профиле
app.get('/api/device-profiles/:profile', (req, res) => {
    try {
        const { profile } = req.params;
        if (!deviceProfiles.has(profile)) {
            return res.status(404).json({ success: false, error: `Profile '${profile}' not found` });
        }
        const profileData = deviceProfiles.get(profile);
        res.json({ success: true, profile: profileData });
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Получение списка доступных пинов для профиля
app.get('/api/device-profiles/:profile/pins', (req, res) => {
    try {
        const { profile } = req.params;
        const { type = 'gpio' } = req.query;
        
        if (!deviceProfiles.has(profile)) {
            return res.status(404).json({ success: false, error: `Profile '${profile}' not found` });
        }
        
        const pins = deviceProfiles.getAvailablePins(profile, type);
        res.json({ success: true, pins });
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Проверка доступности пина для профиля
app.get('/api/device-profiles/:profile/pins/:pin/check', (req, res) => {
    try {
        const { profile, pin } = req.params;
        const pinNum = Number.parseInt(pin, 10);
        
        if (!deviceProfiles.has(profile)) {
            return res.status(404).json({ success: false, error: `Profile '${profile}' not found` });
        }
        
        const available = deviceProfiles.isPinAvailable(profile, pinNum, 'gpio');
        res.json({ success: true, available, pin: pinNum });
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Определение профиля устройства
app.post('/api/devices/:ip/detect-profile', async (req, res) => {
    try {
        const { ip } = req.params;
        if (!syncManager) {
            return res.status(503).json({ success: false, error: 'SyncManager not initialized' });
        }
        
        const result = await syncManager.detectDeviceProfile(ip);
        if (result.success) {
            res.json(result);
        } else {
            res.status(500).json(result);
        }
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Валидация конфигурации для устройства
app.post('/api/devices/:ip/validate-config', async (req, res) => {
    try {
        const { ip } = req.params;
        const { config } = req.body;
        
        if (!syncManager) {
            return res.status(503).json({ success: false, error: 'SyncManager not initialized' });
        }
        
        const result = await syncManager.validateConfiguration(ip, config);
        res.json(result);
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Сохранение сырого профиля конфигурации перед синхронизацией
app.post('/api/devices/:ip/profiles/save', async (req, res) => {
    try {
        const { ip } = req.params;
        const { profile_name, config } = req.body;
        
        if (!config) {
            return res.status(400).json({ success: false, error: 'config is required' });
        }
        
        const result = localStorageManager.saveProfile(ip, profile_name || 'default', config);
        res.json(result);
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Список сохраненных профилей конфигурации (для конкретного устройства или всех)
app.get('/api/devices/:ip/profiles', async (req, res) => {
    try {
        const { ip } = req.params;
        const profiles = localStorageManager.listProfiles(ip);
        res.json({ success: true, profiles });
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Список всех сохраненных профилей конфигурации
app.get('/api/profiles', async (req, res) => {
    try {
        const profiles = localStorageManager.listProfiles();
        res.json({ success: true, profiles });
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Загрузка профиля конфигурации для редактирования
app.get('/api/profiles/:filename', async (req, res) => {
    try {
        const { filename } = req.params;
        const result = localStorageManager.loadProfile(filename);
        if (result.success) {
            res.json({ success: true, profile: result.profile });
        } else {
            res.status(404).json(result);
        }
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Удаление профиля конфигурации
app.delete('/api/profiles/:filename', async (req, res) => {
    try {
        const { filename } = req.params;
        const result = localStorageManager.deleteProfile(filename);
        res.json(result);
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Управляемая синхронизация конфигурации с устройством (только по запросу)
app.post('/api/devices/:ip/sync-config', async (req, res) => {
    try {
        const { ip } = req.params;
        const { config, save_profile_before_sync = false, profile_name } = req.body;
        
        if (!syncManager) {
            return res.status(503).json({ success: false, error: 'SyncManager not initialized' });
        }
        
        // Сохраняем сырой профиль перед синхронизацией, если запрошено
        let savedProfile = null;
        if (save_profile_before_sync && config) {
            const saveResult = localStorageManager.saveProfile(ip, profile_name || 'pre_sync', config);
            if (saveResult.success) {
                savedProfile = saveResult.filename;
                console.log(`[API] Saved profile before sync: ${savedProfile}`);
            }
        }
        
        const result = await syncManager.syncConfiguration(ip, config);
        
        // Добавляем информацию о сохраненном профиле в ответ
        if (savedProfile) {
            result.saved_profile = savedProfile;
        }
        
        res.json(result);
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// API: Получение информации об устройстве (включая профиль)
app.get('/api/devices/:ip/info', async (req, res) => {
    try {
        const { ip } = req.params;
        const device = deviceRegistry.get(ip);
        
        if (!device) {
            return res.status(404).json({ success: false, error: `Device ${ip} not found in registry` });
        }
        
        res.json({ success: true, device });
    } catch (e) {
        res.status(500).json({ success: false, error: String(e?.message || e) });
    }
});

// Функция сохранения всех данных перед завершением
function saveAllData() {
    console.log('[Bridge] Saving all data before shutdown...');
    localStorageManager.saveAll(localChannels, devices, deviceRegistry);
}

// Обработчики сигналов завершения для сохранения данных
process.on('SIGINT', () => {
    console.log('\n[Bridge] Received SIGINT, shutting down gracefully...');
    saveAllData();
    process.exit(0);
});

process.on('SIGTERM', () => {
    console.log('\n[Bridge] Received SIGTERM, shutting down gracefully...');
    saveAllData();
    process.exit(0);
});

// Периодическое автосохранение (каждые 5 минут)
setInterval(() => {
    localStorageManager.saveChannels(localChannels);
    localStorageManager.saveDevices(devices);
    localStorageManager.saveRegistry(deviceRegistry);
    console.log('[Bridge] Auto-saved all data');
}, 5 * 60 * 1000); // 5 минут

// Запуск сервера
server.listen(HTTP_PORT, () => {
    console.log(`[HTTP] Server running on http://localhost:${HTTP_PORT}`);
    console.log(`[Telnet] Using persistent connection for API calls and notifications`);
    console.log(`[Telnet] Default host: ${DEFAULT_TELNET_HOST}:${TELNET_PORT}`);
    console.log(`[LocalStorage] Storage directory: ${localStorageManager.storageDir}`);
    // Устанавливаем activeTelnetHost при старте
    if (!activeTelnetHost || activeTelnetHost === '') {
        activeTelnetHost = DEFAULT_TELNET_HOST;
    }
    // Подключаемся постоянно для получения notifications и API запросов
    connectTelnet(); // Для получения notifications от ESP32 и обработки API запросов
});
