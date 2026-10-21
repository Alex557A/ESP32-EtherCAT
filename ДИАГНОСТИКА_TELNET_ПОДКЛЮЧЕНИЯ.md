# Диагностика проблемы подключения Telnet

**Дата:** 2026-01-27  
**Автор:** Федя  
**Ошибка:** `[Telnet] Error: connect ECONNREFUSED 192.168.20.1:23`

---

## 🔍 Анализ проблемы

Ошибка `ECONNREFUSED` означает, что соединение было отклонено. Возможные причины:

1. **ESP32 не запущен или не прошит**
2. **Telnet сервер не запустился на ESP32**
3. **Неправильный IP адрес** (192.168.20.1 может быть неверным)
4. **W5500 не инициализирован** или кабель не подключен
5. **Сетевые настройки не совпадают** (подсеть, gateway)

---

## 📋 Шаги диагностики

### Шаг 1: Проверка Serial Monitor ESP32

**Откройте Serial Monitor (115200 baud) и проверьте вывод при запуске:**

Должны быть видны следующие сообщения:

```
[MAIN] Initializing Network Configuration...
[Network] IP: 192.168.20.X
[Network] Next device IP: 192.168.20.Y
[MAIN] ✓ Network Configuration initialized
[MAIN] Initializing W5500 TCP for Telnet (Socket 1 on W5500_IN)...
[MAIN] ✓ W5500 TCP initialized for Telnet
[MAIN] Initializing Telnet Server...
[MAIN] ✓ Telnet Server initialized
[MAIN] Starting server on port 23...
[Telnet] ✓ Server started, waiting for connections...
[MAIN] ✓ Telnet Server started
[MAIN] Telnet server listening on 192.168.20.X:23
```

**Важно:** Запишите реальный IP адрес из вывода `[Network] IP: 192.168.20.X`

---

### Шаг 2: Проверка IP адреса

**IP адрес назначается автоматически на основе Station Address:**

- Если `station_address = 1` → IP = `192.168.20.1`
- Если `station_address = 2` → IP = `192.168.20.2`
- Если `station_address = 0` или `> 254` → IP = `192.168.20.1` (по умолчанию)

**В коде (main.cpp строка 229):**
```cpp
uint16_t station_address = 1;  // По умолчанию
```

**Проверьте в Serial Monitor, какой IP адрес реально назначен!**

---

### Шаг 3: Проверка ошибок инициализации

**Если видите ошибки в Serial Monitor:**

```
[MAIN] ERROR: Network Configuration init failed
[MAIN] ERROR: Failed to initialize W5500 TCP for Telnet
[MAIN] ERROR: Failed to initialize Telnet Server
[MAIN] ERROR: Failed to start Telnet Server
```

**Возможные причины:**
- W5500 не подключен или неправильные пины
- SPI не инициализирован
- Проблемы с сетью (кабель не подключен)

---

### Шаг 4: Проверка сетевых настроек на ПК

**Настройте сетевой интерфейс на ПК для подключения к ESP32:**

**Windows:**
```powershell
# Проверьте текущие настройки
ipconfig

# Настройте статический IP (если нужно)
netsh interface ip set address "Ethernet" static 192.168.20.100 255.255.255.0 192.168.20.1
```

**Linux:**
```bash
# Проверьте текущие настройки
ip addr show

# Настройте статический IP (если нужно)
sudo ip addr add 192.168.20.100/24 dev eth0
sudo ip route add default via 192.168.20.1
```

**macOS:**
```bash
# Настройте через System Preferences → Network
# Или через командную строку:
sudo ifconfig en0 192.168.20.100 netmask 255.255.255.0
```

---

### Шаг 5: Проверка подключения через telnet

**Попробуйте подключиться напрямую через telnet:**

```bash
# Windows
telnet 192.168.20.X 23

# Linux/macOS
telnet 192.168.20.X 23
```

**Если подключение успешно:**
- Telnet сервер работает
- Проблема в HTTP-to-Telnet Bridge (неправильный IP адрес)

**Если подключение не удается:**
- Проверьте, что ESP32 запущен
- Проверьте IP адрес в Serial Monitor
- Проверьте сетевые настройки

---

### Шаг 6: Проверка HTTP-to-Telnet Bridge

**Проверьте настройки в `tools/http-telnet-bridge/bridge.js`:**

```javascript
// Проверьте, какой IP адрес используется
const TELNET_HOST = process.env.TELNET_HOST || '192.168.20.1';
const TELNET_PORT = process.env.TELNET_PORT || 23;
```

**Или проверьте активное устройство в веб-интерфейсе:**
- Откройте http://localhost:3000
- Проверьте, какой IP адрес выбран как активное устройство
- Убедитесь, что IP адрес совпадает с адресом из Serial Monitor

---

## 🔧 Решения

### Решение 1: Обновить IP адрес в HTTP-to-Telnet Bridge

**Если IP адрес ESP32 отличается от 192.168.20.1:**

1. **Через веб-интерфейс:**
   - Откройте http://localhost:3000
   - В разделе "Devices" добавьте правильный IP адрес
   - Выберите его как активное устройство

2. **Через переменные окружения:**
   ```bash
   # Windows
   set TELNET_HOST=192.168.20.X
   
   # Linux/macOS
   export TELNET_HOST=192.168.20.X
   ```

3. **Через файл конфигурации:**
   - Отредактируйте `tools/http-telnet-bridge/bridge.js`
   - Измените значение `TELNET_HOST`

---

### Решение 2: Проверить инициализацию W5500

**Если W5500 не инициализируется:**

1. **Проверьте подключение:**
   - Кабель Ethernet подключен к W5500_IN
   - SPI пины подключены правильно
   - CS и RST пины подключены

2. **Проверьте пины в `main.cpp`:**
   ```cpp
   #define W5500_CS_IN        40
   #define W5500_RST_IN       39
   #define SPI1_SCK           36
   #define SPI1_MISO          37
   #define SPI1_MOSI          35
   ```

3. **Проверьте SPI инициализацию:**
   - SPI должен быть инициализирован до W5500
   - Частота SPI: 20 MHz

---

### Решение 3: Проверить сетевые настройки

**Если сеть не работает:**

1. **Проверьте подсеть:**
   - ESP32: `192.168.20.X/24`
   - ПК: `192.168.20.Y/24` (где Y ≠ X)

2. **Проверьте gateway:**
   - ESP32: `192.168.20.1`
   - ПК: `192.168.20.1` (или другой gateway)

3. **Проверьте физическое подключение:**
   - Кабель Ethernet подключен
   - Индикаторы на W5500 горят (если есть)
   - Нет повреждений кабеля

---

### Решение 4: Перезапустить Telnet сервер

**Если Telnet сервер не запустился:**

1. **Перезагрузите ESP32:**
   - Нажмите кнопку RESET на ESP32
   - Или перепрошейте прошивку

2. **Проверьте Serial Monitor:**
   - Убедитесь, что все инициализации прошли успешно
   - Проверьте, что Telnet сервер запустился

---

## 🧪 Тест подключения

**Создайте простой тест для проверки подключения:**

```javascript
// test_telnet_connection.js
const net = require('net');

const HOST = '192.168.20.1';  // Замените на реальный IP из Serial Monitor
const PORT = 23;

const client = new net.Socket();

client.connect(PORT, HOST, () => {
    console.log(`✓ Connected to ${HOST}:${PORT}`);
    
    // Отправить тестовую команду
    const cmd = JSON.stringify({
        jsonrpc: "2.0",
        method: "get_system_info",
        params: {},
        id: 1
    }) + '\r\n';
    
    client.write(cmd);
});

client.on('data', (data) => {
    console.log('Received:', data.toString());
    client.destroy();
});

client.on('error', (err) => {
    console.error('Error:', err.message);
});

client.on('close', () => {
    console.log('Connection closed');
});
```

**Запустите тест:**
```bash
node test_telnet_connection.js
```

---

## 📊 Чек-лист диагностики

- [ ] ESP32 прошит и запущен
- [ ] Serial Monitor открыт (115200 baud)
- [ ] Видны сообщения инициализации в Serial Monitor
- [ ] IP адрес из Serial Monitor записан
- [ ] Telnet сервер успешно запустился (видно в Serial Monitor)
- [ ] Сетевой интерфейс на ПК настроен (192.168.20.X/24)
- [ ] Кабель Ethernet подключен к W5500_IN
- [ ] IP адрес в HTTP-to-Telnet Bridge совпадает с IP из Serial Monitor
- [ ] Прямое подключение через telnet работает (если возможно)

---

## 🎯 Следующие шаги

1. **Проверьте Serial Monitor** - найдите реальный IP адрес ESP32
2. **Обновите IP адрес** в HTTP-to-Telnet Bridge или веб-интерфейсе
3. **Проверьте сетевые настройки** на ПК
4. **Попробуйте подключиться** снова

**Если проблема не решена:**
- Проверьте логи HTTP-to-Telnet Bridge
- Проверьте, что W5500 правильно инициализирован
- Проверьте физическое подключение кабеля

---

**Примечание:** IP адрес `192.168.20.1` - это адрес по умолчанию. Реальный IP адрес может быть другим в зависимости от Station Address. Всегда проверяйте Serial Monitor для получения точного IP адреса!
