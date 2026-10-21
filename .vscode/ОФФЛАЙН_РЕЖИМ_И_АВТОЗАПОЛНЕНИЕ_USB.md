# Оффлайн режим и автозаполнение USB key code

## Требования

1. **Сервер должен работать оффлайн** - без подключенного ESP32
2. **Автозаполнение USB key code (HEX) и модификаторов** доступно только когда ESP32 подключен (онлайн)
3. **Ручное заполнение** доступно всегда (онлайн и оффлайн)

## Архитектура

### Статус подключения ESP32

Сервер отслеживает статус подключения ESP32 через:
- Telnet соединение (192.168.20.1:23)
- WebSocket для real-time событий
- Периодические проверки через API `/api/devices/{ip}/status`

### Логика автозаполнения

```javascript
// Псевдокод логики автозаполнения
function handleKeyboardKeyPress(event) {
    const isESPOnline = checkESPConnectionStatus();
    
    if (isESPOnline && event.method === 'keyboard_key_pressed') {
        // Автозаполнение доступно только онлайн
        autoFillUSBKeyCode(event.params.keycode_hex);
        autoFillModifiers(event.params.modifier);
        autoFillPDOByte(event.params.keycode);
        autoFillIndex(event.params.keycode);
    } else if (!isESPOnline) {
        // Оффлайн режим - только ручное заполнение
        console.log('ESP offline - manual input only');
    }
}
```

## Реализация

### 1. Проверка статуса подключения

```javascript
// tools/web-interface/js/connection-status.js
let espConnectionStatus = {
    isConnected: false,
    lastCheck: null,
    checkInterval: 5000, // 5 секунд
    
    async check() {
        try {
            const response = await fetch(`/api/devices/192.168.20.1/status`);
            this.isConnected = response.ok;
            this.lastCheck = Date.now();
            return this.isConnected;
        } catch (error) {
            this.isConnected = false;
            this.lastCheck = Date.now();
            return false;
        }
    },
    
    startMonitoring() {
        this.check(); // Первая проверка
        setInterval(() => this.check(), this.checkInterval);
    }
};
```

### 2. Обработка keyboard_key_pressed (только онлайн)

```javascript
// tools/web-interface/js/usb-keyboard-auto-fill.js
function setupKeyboardAutoFill() {
    // WebSocket обработчик
    if (window.ws && window.ws.readyState === WebSocket.OPEN) {
        window.ws.addEventListener('message', (event) => {
            try {
                const data = JSON.parse(event.data);
                
                // Проверяем статус подключения перед автозаполнением
                if (data.method === 'keyboard_key_pressed' && 
                    espConnectionStatus.isConnected) {
                    
                    handleAutoFill(data.params);
                }
            } catch (error) {
                console.error('Error parsing keyboard event:', error);
            }
        });
    }
}

function handleAutoFill(params) {
    const { keycode, modifier, keycode_hex } = params;
    
    // Автозаполнение только если ESP онлайн
    if (!espConnectionStatus.isConnected) {
        console.warn('ESP offline - auto-fill disabled');
        return;
    }
    
    // Находим активное поле USB key code
    const usbKeyField = document.getElementById('usb-keycode-hex');
    if (usbKeyField && document.activeElement === usbKeyField) {
        usbKeyField.value = keycode_hex;
        
        // Автозаполнение модификаторов
        updateModifierCheckboxes(modifier);
        
        // Автозаполнение PDO byte
        const pdoByteField = document.getElementById('pdo-byte');
        if (pdoByteField) {
            pdoByteField.value = keycode;
        }
        
        // Автозаполнение Index
        const indexField = document.getElementById('index');
        if (indexField) {
            indexField.value = keycode;
        }
        
        // Обновление выпадающего списка USB кнопки
        updateUSBButtonDropdown(keycode_hex);
    }
}

function updateModifierCheckboxes(modifier) {
    // LCtrl = 0x01, LShift = 0x02, LAlt = 0x04, LGui = 0x08
    // RCtrl = 0x10, RShift = 0x20, RAlt = 0x40, RGui = 0x80
    
    const checkboxes = {
        'modifier-lctrl': (modifier & 0x01) !== 0,
        'modifier-lshift': (modifier & 0x02) !== 0,
        'modifier-lalt': (modifier & 0x04) !== 0,
        'modifier-lgui': (modifier & 0x08) !== 0,
        'modifier-rctrl': (modifier & 0x10) !== 0,
        'modifier-rshift': (modifier & 0x20) !== 0,
        'modifier-ralt': (modifier & 0x40) !== 0,
        'modifier-rgui': (modifier & 0x80) !== 0
    };
    
    Object.entries(checkboxes).forEach(([id, checked]) => {
        const checkbox = document.getElementById(id);
        if (checkbox) {
            checkbox.checked = checked;
        }
    });
}

function updateUSBButtonDropdown(keycodeHex) {
    const dropdown = document.getElementById('usb-button-select');
    if (dropdown) {
        // Находим опцию с соответствующим keycode
        const option = Array.from(dropdown.options).find(
            opt => opt.value === keycodeHex || opt.dataset.keycode === keycodeHex
        );
        if (option) {
            dropdown.value = option.value;
        }
    }
}
```

### 3. UI индикация статуса

```javascript
// tools/web-interface/js/ui-status-indicator.js
function updateConnectionIndicator() {
    const indicator = document.getElementById('esp-connection-indicator');
    const autoFillStatus = document.getElementById('auto-fill-status');
    
    if (espConnectionStatus.isConnected) {
        indicator.className = 'status-indicator online';
        indicator.textContent = 'ESP32 Online';
        if (autoFillStatus) {
            autoFillStatus.textContent = 'Автозаполнение: Включено';
            autoFillStatus.className = 'auto-fill-enabled';
        }
    } else {
        indicator.className = 'status-indicator offline';
        indicator.textContent = 'ESP32 Offline';
        if (autoFillStatus) {
            autoFillStatus.textContent = 'Автозаполнение: Отключено (ESP offline)';
            autoFillStatus.className = 'auto-fill-disabled';
        }
    }
}

// Обновление индикатора при изменении статуса
espConnectionStatus.onStatusChange = updateConnectionIndicator;
```

### 4. Блокировка автозаполнения в оффлайн режиме

```javascript
// tools/web-interface/js/form-validation.js
function setupFormValidation() {
    const usbKeyField = document.getElementById('usb-keycode-hex');
    
    if (usbKeyField) {
        // Показываем подсказку о статусе
        usbKeyField.addEventListener('focus', () => {
            if (!espConnectionStatus.isConnected) {
                showTooltip(usbKeyField, 
                    'ESP32 offline - введите key code вручную');
            } else {
                showTooltip(usbKeyField, 
                    'Нажмите клавишу на подключенной клавиатуре для автозаполнения');
            }
        });
        
        // Разрешаем ручной ввод всегда
        usbKeyField.addEventListener('input', () => {
            // Ручной ввод разрешен в любом режиме
        });
    }
}
```

## API endpoints

### GET /api/devices/{ip}/status

Проверка статуса подключения ESP32.

**Response:**
```json
{
  "connected": true,
  "lastSeen": "2024-01-15T10:30:00Z",
  "latency": 15
}
```

### WebSocket: keyboard_key_pressed

Событие отправляется только когда ESP32 подключен и клавиатура активна.

**Event:**
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

## Чеклист реализации

- [x] Добавлена проверка статуса подключения ESP32
- [x] Автозаполнение USB key code только при подключенном ESP
- [x] Автозаполнение модификаторов только при подключенном ESP
- [x] Ручной ввод доступен всегда (онлайн и оффлайн)
- [x] UI индикация статуса подключения
- [x] UI индикация статуса автозаполнения
- [x] Подсказки для пользователя о режиме работы
- [x] Сервер работает оффлайн (без ESP)
- [x] WebSocket события обрабатываются только при подключенном ESP

## Тестирование

### Тест 1: Онлайн режим
1. Подключить ESP32 к сети
2. Открыть форму добавления USB keyboard канала
3. Нажать клавишу на подключенной клавиатуре
4. **Ожидаемый результат:** Поля заполняются автоматически

### Тест 2: Оффлайн режим
1. Отключить ESP32 от сети
2. Открыть форму добавления USB keyboard канала
3. Попытаться нажать клавишу
4. **Ожидаемый результат:** Автозаполнение не работает, но ручной ввод доступен

### Тест 3: Переход онлайн/оффлайн
1. Начать заполнение формы в оффлайн режиме
2. Подключить ESP32
3. Нажать клавишу
4. **Ожидаемый результат:** Автозаполнение начинает работать

## Примечания

- Статус подключения проверяется каждые 5 секунд
- WebSocket события обрабатываются только при активном подключении
- Ручной ввод всегда доступен независимо от статуса подключения
- Индикатор статуса обновляется в реальном времени
