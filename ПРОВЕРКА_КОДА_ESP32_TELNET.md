# Проверка кода ESP32 для Telnet сервера

## Проблема

Bridge.js не может подключиться к ESP32:
- `Connection timeout` - таймаут подключения
- `ECONNREFUSED` - соединение отклонено

## Проверка кода ESP32

### 1. Инициализация Telnet сервера (main.cpp)

**Код инициализации (строки 242-296):**

```cpp
// Инициализация Network Configuration
Serial.println("[MAIN] Initializing Network Configuration...");
uint16_t station_address = 1;  // По умолчанию
if (network_config_init(&g_network_config, station_address)) {
    Serial.println("[MAIN] ✓ Network Configuration initialized");
} else {
    Serial.println("[MAIN] ERROR: Network Configuration init failed");
}

// Инициализация W5500 TCP для Telnet
Serial.println("[MAIN] Initializing W5500 TCP for Telnet (Socket 1 on W5500_IN)...");
if (w5500_tcp_init(&g_telnet_w5500, ...)) {
    Serial.println("[MAIN] ✓ W5500 TCP initialized for Telnet");
    
    // Инициализация Telnet Server
    Serial.println("[MAIN] Initializing Telnet Server...");
    if (telnet_server_init(&g_telnet_server, ...)) {
        Serial.println("[MAIN] ✓ Telnet Server initialized");
        
        // Запуск Telnet Server
        if (telnet_server_start(&g_telnet_server)) {
            Serial.println("[MAIN] ✓ Telnet Server started");
            g_telnet_server_running = true;
        } else {
            Serial.println("[MAIN] ERROR: Failed to start Telnet Server");
        }
    } else {
        Serial.println("[MAIN] ERROR: Failed to initialize Telnet Server");
    }
} else {
    Serial.println("[MAIN] ERROR: Failed to initialize W5500 TCP for Telnet");
}
```

### 2. Что проверить в Serial Monitor

**Откройте Serial Monitor (115200 baud) и проверьте вывод:**

#### Успешная инициализация должна показать:

```
[MAIN] Initializing Network Configuration...
[Network] IP: 192.168.20.1
[Network] Next device IP: 192.168.20.2
[MAIN] ✓ Network Configuration initialized
[MAIN] Initializing W5500 TCP for Telnet (Socket 1 on W5500_IN)...
[W5500 TCP] Initializing W5500...
[W5500 TCP] Read IP: 192.168.20.1
[MAIN] ✓ W5500 TCP initialized for Telnet
[MAIN] Initializing Telnet Server...
[MAIN] ✓ Telnet Server initialized
[Telnet] Starting server on port 23...
[W5500 TCP] Opening socket 1 in TCP mode (port 23)...
[W5500 TCP] Current socket 1 status: 0x00
[W5500 TCP] Socket opened (INIT)
[W5500 TCP] Socket LISTENING SUCCESS
[Telnet] ✓ Server started, waiting for connections...
[MAIN] ✓ Telnet Server started
[MAIN] Telnet Server: Socket 1 (TCP mode, port 23)
[MAIN] Telnet server listening on 192.168.20.1:23
```

#### Если видите ошибки:

**Ошибка 1: `[MAIN] ERROR: Network Configuration init failed`**
- Проблема: Не удалось инициализировать сетевую конфигурацию
- Решение: Проверьте код `network_config_init()`

**Ошибка 2: `[MAIN] ERROR: Failed to initialize W5500 TCP for Telnet`**
- Проблема: W5500 не инициализируется
- Возможные причины:
  - W5500 не подключен или неправильные пины
  - SPI не инициализирован
  - Проблемы с питанием W5500
- Решение: Проверьте подключение W5500 и пины

**Ошибка 3: `[MAIN] ERROR: Failed to initialize Telnet Server`**
- Проблема: Telnet сервер не инициализируется
- Решение: Проверьте код `telnet_server_init()`

**Ошибка 4: `[MAIN] ERROR: Failed to start Telnet Server`**
- Проблема: Telnet сервер не запускается
- Возможные причины:
  - Socket 1 не может перейти в режим LISTEN
  - Конфликт с Socket 0 (EtherCAT)
  - Проблемы с W5500
- Решение: Проверьте логи W5500 TCP

**Ошибка 5: `[W5500 TCP] Socket open FAILED` или `[W5500 TCP] Listen FAILED`**
- Проблема: W5500 не может открыть socket или перейти в режим LISTEN
- Возможные причины:
  - Socket уже занят
  - Проблемы с W5500
  - Неправильная конфигурация W5500
- Решение: Проверьте статус socket и конфигурацию W5500

### 3. Проверка IP адреса

**В Serial Monitor должно быть:**
```
[Network] IP: 192.168.20.1
[MAIN] Telnet server listening on 192.168.20.1:23
```

**Если IP адрес другой:**
- Запишите реальный IP адрес
- Обновите IP адрес в bridge.js или веб-интерфейсе

### 4. Проверка обработки Telnet сервера в loop()

**Код обработки (строки 320-321):**

```cpp
// Обработка Telnet Server
if (g_telnet_server_running) {
    telnet_server_process(&g_telnet_server);
}
```

**Важно:** `telnet_server_process()` должен вызываться в `loop()` для обработки входящих соединений.

### 5. Проверка функции w5500_tcp_listen()

**Код (w5500_tcp.cpp, строки 219-318):**

Функция должна:
1. Проверить инициализацию W5500
2. Закрыть socket если открыт
3. Установить TCP mode
4. Установить порт (23)
5. Выполнить команду OPEN
6. Проверить статус INIT
7. Выполнить команду LISTEN
8. Проверить статус LISTEN

**Если видите ошибки:**
- `Socket open FAILED` - проверьте статус socket после OPEN
- `Listen FAILED` - проверьте статус socket после LISTEN

### 6. Проверка функции telnet_server_start()

**Код (telnet_server.cpp, строки 222-244):**

Функция должна:
1. Проверить инициализацию сервера
2. Проверить, что сервер не запущен
3. Вызвать `w5500_tcp_listen()` для порта 23
4. Установить флаг `running = true`

## Диагностика

### Шаг 1: Проверьте Serial Monitor

1. Откройте Serial Monitor (115200 baud)
2. Перезагрузите ESP32 (кнопка RESET)
3. Найдите все сообщения об инициализации
4. Запишите все ошибки

### Шаг 2: Проверьте IP адрес

1. Найдите строку `[Network] IP: X.X.X.X`
2. Запишите IP адрес
3. Убедитесь, что он в подсети 192.168.20.0/24

### Шаг 3: Проверьте статус Telnet сервера

1. Найдите строку `[MAIN] ✓ Telnet Server started` или ошибку
2. Если ошибка - запишите точное сообщение
3. Проверьте логи W5500 TCP

### Шаг 4: Проверьте подключение W5500

1. Проверьте физическое подключение:
   - CS pin (GPIO 40)
   - RST pin (GPIO 39)
   - SPI пины (GPIO 36, 37, 35)
   - Питание W5500
   - Кабель Ethernet подключен

2. Проверьте логи W5500:
   - `[W5500 TCP] Initializing W5500...`
   - `[W5500 TCP] Read IP: X.X.X.X`

### Шаг 5: Проверьте сетевые настройки на ПК

1. Проверьте IP адрес ПК:
   ```powershell
   ipconfig
   ```
   
2. Должен быть в подсети 192.168.20.X (где X ≠ IP ESP32)

3. Если нет - настройте:
   ```powershell
   netsh interface ip set address "Ethernet" static 192.168.20.100 255.255.255.0 192.168.20.1
   ```

## Возможные проблемы и решения

### Проблема 1: ESP32 не запускается

**Симптомы:**
- Нет вывода в Serial Monitor
- ESP32 не мигает светодиодом

**Решение:**
- Проверьте питание ESP32
- Проверьте подключение USB кабеля
- Перепрошейте прошивку

### Проблема 2: W5500 не инициализируется

**Симптомы:**
- `[MAIN] ERROR: Failed to initialize W5500 TCP for Telnet`
- Нет логов W5500

**Решение:**
- Проверьте подключение W5500
- Проверьте пины CS, RST, SPI
- Проверьте питание W5500
- Проверьте кабель Ethernet

### Проблема 3: Socket не может перейти в режим LISTEN

**Симптомы:**
- `[W5500 TCP] Listen FAILED, status: 0xXX`
- Socket остается в статусе INIT

**Решение:**
- Проверьте, что Socket 0 (EtherCAT) не конфликтует
- Проверьте конфигурацию W5500
- Попробуйте увеличить задержки в `w5500_tcp_listen()`

### Проблема 4: IP адрес неправильный

**Симптомы:**
- IP адрес не в подсети 192.168.20.0/24
- Bridge.js подключается к другому адресу

**Решение:**
- Проверьте `station_address` в main.cpp
- Обновите IP адрес в bridge.js
- Проверьте функцию `network_config_auto_assign_ip()`

## Тестирование

### Тест 1: Проверка инициализации

1. Откройте Serial Monitor
2. Перезагрузите ESP32
3. Проверьте, что все инициализации прошли успешно
4. Запишите IP адрес

### Тест 2: Проверка Telnet сервера

1. Убедитесь, что видите: `[MAIN] ✓ Telnet Server started`
2. Убедитесь, что видите: `[MAIN] Telnet server listening on X.X.X.X:23`
3. Запишите IP адрес

### Тест 3: Прямое подключение через telnet

1. Откройте командную строку
2. Выполните:
   ```bash
   telnet 192.168.20.X 23
   ```
   (Замените X на реальный IP адрес из Serial Monitor)

3. Если подключение успешно:
   - Telnet сервер работает
   - Проблема в IP адресе в bridge.js

4. Если не подключается:
   - Проверьте Serial Monitor на ошибки
   - Проверьте сетевые настройки
   - Проверьте физическое подключение

## Следующие шаги

1. **Откройте Serial Monitor** и перезагрузите ESP32
2. **Запишите все сообщения** об инициализации
3. **Найдите IP адрес** из строки `[Network] IP: X.X.X.X`
4. **Проверьте статус Telnet сервера** - должен быть `✓ Telnet Server started`
5. **Если есть ошибки** - запишите точные сообщения
6. **Обновите IP адрес** в bridge.js если отличается от 192.168.20.1

## Что предоставить для помощи

Если проблема не решена, предоставьте:

1. **Вывод Serial Monitor** (первые 100 строк после загрузки)
2. **IP адрес** из Serial Monitor
3. **Статус Telnet сервера** (запущен или ошибка)
4. **Логи W5500 TCP** (если есть)
5. **Результат теста** прямого подключения через telnet
