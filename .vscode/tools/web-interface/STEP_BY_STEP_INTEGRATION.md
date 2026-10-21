# Пошаговая интеграция

## Шаг 1: Найти файл сервера

Найдите файл сервера в вашем проекте:
- `bridge.js`
- `server.js`
- `app.js`
- `index.js`
- или другой файл, который запускает HTTP сервер

## Шаг 2: Добавить API endpoint

### Вариант A: Express.js

Откройте файл сервера и найдите место, где создается Express app:

```javascript
const express = require('express');
const app = express();
```

Добавьте после создания app:

```javascript
// Добавить импорт
const { addStatusEndpoint } = require('./tools/web-interface/api-status-endpoint');

// ... ваш существующий код ...

// Добавить endpoint (после создания app, но до app.listen)
addStatusEndpoint(app);
```

### Вариант B: Другой фреймворк

Скопируйте функцию `checkESP32Connection` из `api-status-endpoint.js` и создайте свой endpoint.

## Шаг 3: Найти HTML файл

Найдите главный HTML файл вашего веб-интерфейса:
- `index.html`
- `main.html`
- `public/index.html`
- `views/index.html`
- или другой файл

## Шаг 4: Подключить CSS

В секции `<head>` HTML файла добавьте:

```html
<link rel="stylesheet" href="tools/web-interface/css/status-indicators.css">
```

**Важно:** Если файлы находятся в другом месте, измените путь:
- Если в корне: `href="/css/status-indicators.css"`
- Если в подпапке: `href="/static/css/status-indicators.css"`

## Шаг 5: Подключить JavaScript

Перед закрывающим тегом `</body>` добавьте (в правильном порядке!):

```html
<!-- 1. Сначала connection-status.js -->
<script src="tools/web-interface/js/connection-status.js"></script>

<!-- 2. Затем ui-status-indicator.js -->
<script src="tools/web-interface/js/ui-status-indicator.js"></script>

<!-- 3. Затем usb-keyboard-auto-fill.js -->
<script src="tools/web-interface/js/usb-keyboard-auto-fill.js"></script>
```

**Важно:** Порядок важен! Не меняйте порядок подключения.

## Шаг 6: Проверить ID полей формы

Убедитесь, что в вашей форме добавления USB keyboard канала есть поля с ID:

- `usb-keycode-hex` - поле для USB key code (HEX)
- `usb-button-select` - выпадающий список USB кнопок
- `modifier-lctrl`, `modifier-lshift`, `modifier-lalt`, `modifier-lgui` - левые модификаторы
- `modifier-rctrl`, `modifier-rshift`, `modifier-ralt`, `modifier-rgui` - правые модификаторы
- `pdo-byte` - поле PDO byte
- `index` - поле Index

Если ID отличаются, обновите их в `js/usb-keyboard-auto-fill.js`.

## Шаг 7: Настроить статические файлы (если нужно)

Если используете Express.js, убедитесь, что статические файлы настроены:

```javascript
app.use(express.static('public')); // или 'views'
app.use('/tools', express.static('tools')); // для доступа к /tools/...
```

## Шаг 8: Проверить работу

### 8.1. Запустить сервер

```bash
node bridge.js
# или
npm start
```

### 8.2. Открыть страницу в браузере

Откройте `http://localhost:3000` (или ваш порт)

### 8.3. Проверить консоль браузера

Нажмите F12, откройте вкладку Console. Не должно быть ошибок загрузки файлов.

### 8.4. Проверить индикатор статуса

На странице должен появиться индикатор статуса ESP32.

### 8.5. Проверить API endpoint

В консоли браузера выполните:

```javascript
fetch('/api/devices/192.168.20.1/status').then(r => r.json()).then(console.log)
```

Должен вернуться JSON с полем `connected`.

### 8.6. Протестировать автозаполнение

1. Подключите ESP32 к сети
2. Откройте форму добавления канала
3. Установите фокус на поле `usb-keycode-hex`
4. Нажмите клавишу на подключенной клавиатуре
5. Поля должны заполниться автоматически

## Шаг 9: Использовать тестовую страницу

Откройте `tools/web-interface/test-integration.html` в браузере для проверки всех компонентов.

## Решение проблем

### Проблема: Файлы не загружаются (404)

**Решение:**
1. Проверьте пути к файлам в HTML
2. Убедитесь, что статические файлы настроены в Express
3. Проверьте структуру папок
4. Используйте абсолютные пути вместо относительных

### Проблема: API endpoint не работает

**Решение:**
1. Проверьте, что `addStatusEndpoint(app)` вызван
2. Проверьте логи сервера
3. Убедитесь, что путь к `api-status-endpoint.js` правильный
4. Проверьте, что порт 23 доступен

### Проблема: Индикатор не появляется

**Решение:**
1. Проверьте консоль браузера на ошибки
2. Убедитесь, что скрипты подключены в правильном порядке
3. Проверьте, что `espConnectionStatus` доступен глобально

### Проблема: Автозаполнение не работает

**Решение:**
1. Убедитесь, что ESP32 подключен (индикатор зеленый)
2. Проверьте, что поле `usb-keycode-hex` в фокусе
3. Проверьте WebSocket соединение
4. Проверьте формат события `keyboard_key_pressed` в консоли

## Дополнительная помощь

- `QUICK_START.md` - Быстрый старт
- `INTEGRATION_GUIDE.md` - Подробное руководство
- `INTEGRATION_EXAMPLES.md` - Примеры для разных структур
- `test-integration.html` - Тестовая страница
