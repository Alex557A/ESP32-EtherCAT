# Интеграция оффлайн режима и автозаполнения USB key code

## 📁 Структура файлов

```
tools/web-interface/
├── js/
│   ├── connection-status.js      ✅ Проверка статуса ESP32
│   ├── usb-keyboard-auto-fill.js ✅ Автозаполнение полей
│   └── ui-status-indicator.js    ✅ UI индикаторы
├── css/
│   └── status-indicators.css     ✅ Стили индикаторов
├── api-status-endpoint.js        ✅ API endpoint для сервера
├── html-integration-template.html ✅ Пример HTML
├── INTEGRATION_GUIDE.md          📖 Подробное руководство
├── QUICK_START.md                🚀 Быстрый старт
└── README.md                     📋 Этот файл
```

## 🎯 Что реализовано

### ✅ Функциональность

1. **Проверка статуса подключения ESP32**
   - Автоматическая проверка каждые 5 секунд
   - API endpoint `/api/devices/:ip/status`
   - Система уведомлений об изменении статуса

2. **Автозаполнение полей USB keyboard канала**
   - Работает только при подключенном ESP32
   - Автоматическое заполнение при нажатии клавиши:
     - USB key code (HEX)
     - Модификаторы (чекбоксы)
     - PDO byte
     - Index
     - Выпадающий список USB кнопки

3. **UI индикаторы**
   - Индикатор статуса подключения ESP32
   - Индикатор статуса автозаполнения
   - Подсказки для полей формы

4. **Оффлайн режим**
   - Сервер работает без подключенного ESP32
   - Ручной ввод доступен всегда
   - Сохранение конфигурации работает оффлайн

## 🚀 Быстрая интеграция

### Шаг 1: Добавить API endpoint в сервер

В файле сервера (bridge.js или server.js):

```javascript
const { addStatusEndpoint } = require('./tools/web-interface/api-status-endpoint');
addStatusEndpoint(app);
```

### Шаг 2: Подключить в HTML

В `<head>`:
```html
<link rel="stylesheet" href="tools/web-interface/css/status-indicators.css">
```

Перед `</body>` (в правильном порядке!):
```html
<script src="tools/web-interface/js/connection-status.js"></script>
<script src="tools/web-interface/js/ui-status-indicator.js"></script>
<script src="tools/web-interface/js/usb-keyboard-auto-fill.js"></script>
```

### Шаг 3: Проверить ID полей формы

Убедитесь, что в форме есть поля с ID:
- `usb-keycode-hex`
- `usb-button-select`
- `modifier-lctrl`, `modifier-lshift`, и т.д.
- `pdo-byte`
- `index`

## 📖 Документация

- **QUICK_START.md** - Быстрый старт (3 шага)
- **INTEGRATION_GUIDE.md** - Подробное руководство по интеграции
- **html-integration-template.html** - Пример HTML с формой

## 🔧 Настройка

### Изменить IP адрес ESP32

В `js/connection-status.js`:
```javascript
const response = await fetch('/api/devices/192.168.20.1/status', {
```
Замените `192.168.20.1` на нужный IP.

### Изменить интервал проверки

В `js/connection-status.js`:
```javascript
this.checkInterval = 5000; // изменить на нужное значение в мс
```

### Кастомизация стилей

Отредактируйте `css/status-indicators.css`

## ✅ Чеклист интеграции

- [ ] Добавить API endpoint в сервер
- [ ] Подключить CSS в HTML
- [ ] Подключить JavaScript в HTML (в правильном порядке)
- [ ] Проверить ID полей формы
- [ ] Протестировать онлайн режим
- [ ] Протестировать оффлайн режим
- [ ] Протестировать автозаполнение

## 🐛 Решение проблем

См. раздел "Решение проблем" в **INTEGRATION_GUIDE.md**

## 📝 Примечания

- Автозаполнение работает только когда поле `usb-keycode-hex` в фокусе
- Ручной ввод доступен всегда (онлайн и оффлайн)
- WebSocket события обрабатываются только при подключенном ESP32
- Статус проверяется каждые 5 секунд

## 🔗 Связанные документы

- `ОФФЛАЙН_РЕЖИМ_И_АВТОЗАПОЛНЕНИЕ_USB.md` - Общая документация
- `РЕАЛИЗАЦИЯ_ОФФЛАЙН_И_АВТОЗАПОЛНЕНИЯ.md` - Детальная реализация
- `ЧЕКЛИСТ_ОФФЛАЙН_И_АВТОЗАПОЛНЕНИЯ.md` - Чеклист реализации
