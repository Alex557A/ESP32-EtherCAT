# Примеры интеграции для разных структур проектов

## Вариант 1: Express.js сервер (bridge.js)

### Структура проекта:
```
project/
├── bridge.js (или server.js)
├── package.json
└── public/ (или views/)
    └── index.html
```

### Интеграция в bridge.js:

```javascript
const express = require('express');
const app = express();
const http = require('http').createServer(app);
const io = require('socket.io')(http);

// ... ваш существующий код ...

// ===== ДОБАВИТЬ ЭТО =====
// Импорт функции для добавления endpoint
const { addStatusEndpoint } = require('./tools/web-interface/api-status-endpoint');

// Добавить endpoint для проверки статуса ESP32
addStatusEndpoint(app);
// ========================

// ... остальной код ...

const PORT = process.env.PORT || 3000;
http.listen(PORT, () => {
    console.log(`Server running on http://localhost:${PORT}`);
});
```

### Интеграция в HTML (public/index.html или views/index.html):

```html
<!DOCTYPE html>
<html>
<head>
    <meta charset="UTF-8">
    <title>ESP32 EtherCAT Configuration</title>
    
    <!-- Добавить CSS -->
    <link rel="stylesheet" href="/tools/web-interface/css/status-indicators.css">
    
    <!-- Ваши существующие стили -->
</head>
<body>
    <!-- Ваш существующий контент -->
    
    <!-- Добавить JavaScript (перед закрывающим </body>) -->
    <script src="/tools/web-interface/js/connection-status.js"></script>
    <script src="/tools/web-interface/js/ui-status-indicator.js"></script>
    <script src="/tools/web-interface/js/usb-keyboard-auto-fill.js"></script>
    
    <!-- Ваши существующие скрипты -->
</body>
</html>
```

### Настройка статических файлов в Express:

```javascript
// В bridge.js добавить:
app.use(express.static('public')); // или 'views', в зависимости от структуры
app.use('/tools', express.static('tools')); // для доступа к файлам через /tools/...
```

## Вариант 2: Node.js HTTP сервер без Express

### Интеграция в server.js:

```javascript
const http = require('http');
const fs = require('fs');
const path = require('path');
const { checkESP32Connection } = require('./tools/web-interface/api-status-endpoint');

const server = http.createServer((req, res) => {
    // ... ваш существующий код обработки запросов ...
    
    // Добавить обработку endpoint для проверки статуса
    if (req.method === 'GET' && req.url.startsWith('/api/devices/')) {
        const match = req.url.match(/\/api\/devices\/([^\/]+)\/status/);
        if (match) {
            const ip = match[1];
            checkESP32Connection(ip).then(connected => {
                res.writeHead(200, { 'Content-Type': 'application/json' });
                res.end(JSON.stringify({
                    connected,
                    lastSeen: connected ? new Date().toISOString() : null,
                    latency: null,
                    ip
                }));
            });
            return;
        }
    }
    
    // ... остальная обработка запросов ...
});

server.listen(3000, () => {
    console.log('Server running on http://localhost:3000');
});
```

## Вариант 3: Сервер с существующим telnet клиентом

Если у вас уже есть telnet клиент, используйте альтернативную функцию:

```javascript
const { addStatusEndpointWithTelnetClient } = require('./tools/web-interface/api-status-endpoint');

// Где telnetClient - ваш существующий telnet клиент
addStatusEndpointWithTelnetClient(app, telnetClient);
```

## Вариант 4: Интеграция в существующий WebSocket обработчик

Если WebSocket уже настроен, убедитесь, что события `keyboard_key_pressed` передаются:

```javascript
// В вашем WebSocket обработчике
io.on('connection', (socket) => {
    // ... ваш существующий код ...
    
    // Убедитесь, что события keyboard_key_pressed передаются клиентам
    // Например, если получаете от ESP32:
    telnetClient.on('data', (data) => {
        try {
            const message = JSON.parse(data.toString());
            if (message.method === 'keyboard_key_pressed') {
                // Передать всем подключенным клиентам
                io.emit('message', JSON.stringify(message));
            }
        } catch (e) {
            // Обработка других сообщений
        }
    });
});
```

## Вариант 5: Если файлы находятся в другом месте

### Если структура проекта другая:

```javascript
// В bridge.js изменить путь:
const { addStatusEndpoint } = require('../tools/web-interface/api-status-endpoint');
// или
const { addStatusEndpoint } = require('../../tools/web-interface/api-status-endpoint');
```

### В HTML изменить пути:

```html
<!-- Если файлы в корне проекта -->
<link rel="stylesheet" href="/css/status-indicators.css">
<script src="/js/connection-status.js"></script>

<!-- Если файлы в подпапке -->
<link rel="stylesheet" href="/static/css/status-indicators.css">
<script src="/static/js/connection-status.js"></script>

<!-- Если используете относительные пути -->
<link rel="stylesheet" href="../tools/web-interface/css/status-indicators.css">
<script src="../tools/web-interface/js/connection-status.js"></script>
```

## Проверка интеграции

### 1. Проверить API endpoint:

```bash
curl http://localhost:3000/api/devices/192.168.20.1/status
```

Ожидаемый ответ:
```json
{
  "connected": true,
  "lastSeen": "2024-01-15T10:30:00.000Z",
  "latency": null,
  "ip": "192.168.20.1",
  "port": 23
}
```

### 2. Проверить в браузере:

1. Откройте консоль разработчика (F12)
2. Проверьте, что нет ошибок загрузки файлов
3. Проверьте, что индикатор статуса появился
4. Проверьте Network tab - должен быть запрос к `/api/devices/192.168.20.1/status`

### 3. Проверить автозаполнение:

1. Подключите ESP32
2. Откройте форму добавления канала
3. Установите фокус на поле `usb-keycode-hex`
4. Нажмите клавишу на подключенной клавиатуре
5. Поля должны заполниться автоматически

## Решение проблем

### Проблема: Файлы не загружаются (404)

**Решение:**
- Проверьте пути к файлам в HTML
- Убедитесь, что статические файлы настроены в Express
- Проверьте структуру папок

### Проблема: API endpoint не работает (404)

**Решение:**
- Убедитесь, что `addStatusEndpoint(app)` вызван после создания `app`
- Проверьте, что путь к `api-status-endpoint.js` правильный
- Проверьте логи сервера на ошибки

### Проблема: Индикатор не появляется

**Решение:**
- Проверьте консоль браузера на ошибки JavaScript
- Убедитесь, что скрипты подключены в правильном порядке
- Проверьте, что `espConnectionStatus` доступен глобально

### Проблема: Автозаполнение не работает

**Решение:**
- Убедитесь, что ESP32 подключен (индикатор зеленый)
- Проверьте, что WebSocket передает события `keyboard_key_pressed`
- Проверьте, что поле `usb-keycode-hex` в фокусе
- Проверьте формат события в консоли браузера

## Дополнительная настройка

### Изменить IP адрес по умолчанию

В `js/connection-status.js`:
```javascript
// Заменить хардкод на переменную
const ESP32_IP = window.ESP32_IP || '192.168.20.1';
const response = await fetch(`/api/devices/${ESP32_IP}/status`, {
```

Или передать через конфигурацию:
```html
<script>
    window.ESP32_IP = '192.168.20.1'; // или получить из конфигурации
</script>
<script src="js/connection-status.js"></script>
```

### Изменить интервал проверки

В `js/connection-status.js`:
```javascript
this.checkInterval = 5000; // изменить на нужное значение
```

### Кастомизация стилей

Отредактируйте `css/status-indicators.css` для изменения внешнего вида.
