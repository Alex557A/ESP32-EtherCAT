# Руководство по интеграции оффлайн режима и автозаполнения

## Шаг 1: Добавить API endpoint в сервер

### Если используете Express.js (bridge.js или server.js):

```javascript
const { addStatusEndpoint } = require('./api-status-endpoint');

// После создания app
addStatusEndpoint(app);
```

### Если используете другой фреймворк:

Скопируйте функцию `checkESP32Connection` и `addStatusEndpoint` из `api-status-endpoint.js` и адаптируйте под ваш фреймворк.

### Пример для Express:

```javascript
// В bridge.js или server.js
const express = require('express');
const app = express();

// ... ваш существующий код ...

// Добавить endpoint для проверки статуса
const { addStatusEndpoint } = require('./tools/web-interface/api-status-endpoint');
addStatusEndpoint(app);
```

## Шаг 2: Подключить CSS в HTML

В `<head>` вашего HTML файла добавьте:

```html
<link rel="stylesheet" href="tools/web-interface/css/status-indicators.css">
```

Или если файлы находятся в другом месте, укажите правильный путь.

## Шаг 3: Подключить JavaScript в HTML

Перед закрывающим тегом `</body>` добавьте (в правильном порядке!):

```html
<!-- 1. Сначала connection-status.js -->
<script src="tools/web-interface/js/connection-status.js"></script>

<!-- 2. Затем ui-status-indicator.js -->
<script src="tools/web-interface/js/ui-status-indicator.js"></script>

<!-- 3. Затем usb-keyboard-auto-fill.js -->
<script src="tools/web-interface/js/usb-keyboard-auto-fill.js"></script>
```

## Шаг 4: Убедиться в наличии полей формы

Убедитесь, что в вашей форме добавления USB keyboard канала есть поля с правильными ID:

- `usb-keycode-hex` - поле для USB key code (HEX)
- `usb-button-select` - выпадающий список USB кнопок
- `modifier-lctrl`, `modifier-lshift`, и т.д. - чекбоксы модификаторов
- `pdo-byte` - поле PDO byte
- `index` - поле Index

Если ID полей отличаются, обновите их в `usb-keyboard-auto-fill.js`.

## Шаг 5: Настроить WebSocket

Убедитесь, что WebSocket настроен для передачи событий `keyboard_key_pressed` от ESP32.

Событие должно иметь формат:

```json
{
  "jsonrpc": "2.0",
  "method": "keyboard_key_pressed",
  "params": {
    "keycode": 96,
    "modifier": 0,
    "keycode_hex": "0x60"
  }
}
```

## Шаг 6: Проверка работы

1. Откройте страницу в браузере
2. Проверьте консоль браузера на ошибки
3. Убедитесь, что индикатор статуса ESP32 отображается
4. Подключите ESP32 - индикатор должен стать зеленым
5. Откройте форму добавления канала
6. Установите фокус на поле USB key code
7. Нажмите клавишу на подключенной клавиатуре
8. Поля должны заполниться автоматически

## Решение проблем

### Индикатор не отображается

- Проверьте, что скрипты подключены в правильном порядке
- Проверьте консоль браузера на ошибки JavaScript
- Убедитесь, что пути к файлам правильные

### Автозаполнение не работает

- Убедитесь, что ESP32 подключен (индикатор зеленый)
- Проверьте, что поле USB key code в фокусе
- Проверьте WebSocket соединение в консоли браузера
- Убедитесь, что события `keyboard_key_pressed` приходят от ESP32
- Проверьте формат события в консоли браузера

### API endpoint не работает

- Проверьте, что endpoint добавлен в сервер
- Проверьте логи сервера на ошибки
- Убедитесь, что порт 23 доступен для проверки подключения
- Проверьте, что IP адрес ESP32 правильный (по умолчанию 192.168.20.1)

### Ошибки в консоли браузера

- Убедитесь, что все скрипты загружены
- Проверьте порядок подключения скриптов
- Убедитесь, что `espConnectionStatus` доступен глобально

## Кастомизация

### Изменить интервал проверки статуса

В `connection-status.js` измените:

```javascript
this.checkInterval = 5000; // изменить на нужное значение в мс
```

### Изменить IP адрес ESP32

В `connection-status.js` измените:

```javascript
const response = await fetch('/api/devices/192.168.20.1/status', {
```

Или сделайте IP адрес настраиваемым через переменную.

### Изменить стили индикаторов

Отредактируйте `css/status-indicators.css` для изменения внешнего вида индикаторов.

## Дополнительная информация

- `ОФФЛАЙН_РЕЖИМ_И_АВТОЗАПОЛНЕНИЕ_USB.md` - общая документация
- `РЕАЛИЗАЦИЯ_ОФФЛАЙН_И_АВТОЗАПОЛНЕНИЯ.md` - детальная реализация
- `ЧЕКЛИСТ_ОФФЛАЙН_И_АВТОЗАПОЛНЕНИЯ.md` - чеклист реализации
