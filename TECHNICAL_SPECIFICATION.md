# ESP32 EtherCAT Slave v4 - Техническое задание

**Дата:** 2026-01-27  
**Версия:** 4.1 (актуализация: сохранение конфигурации в Flash, исправления Telnet соединений, очередь запросов, индикатор статуса)  
**Статус:** В процессе (документ обновляется по факту реализации)

---

## Содержание

1. [Введение](#1-введение)
2. [Архитектура системы](#2-архитектура-системы)
3. [Модули системы](#3-модули-системы)
4. [Конфигурация через Telnet](#4-конфигурация-через-telnet)
5. [Внешний веб-интерфейс](#5-внешний-веб-интерфейс)
6. [Roadmap разработки](#6-roadmap-разработки)
7. [Критерии приемки](#7-критерии-приемки)

---

## 1. Введение

### 1.1 Цель проекта

Разработать **полнофункциональный EtherCAT Slave** на базе ESP32-S3 с поддержкой:
- ✅ EtherCAT протокол (ETG.1000)
- ✅ Real-time PDO обмен (<100μs latency)
- ✅ CoE (CANopen over EtherCAT)
- ✅ Distributed Clock синхронизация
- ✅ До 32 I/O каналов (digital/analog)
- ✅ До 8 энкодеров
- ✅ StepGen (STEP/DIR) для LinuxCNC (до 4 осей, EtherCAT PDO)
- ✅ USB HID Keyboard (ESP32-S3) - управление ПК через EtherCAT
  - Используется TinyUSB напрямую (НЕ официальная библиотека USBHIDKeyboard)
  - Схема инициализации из рабочего проекта: `C:\Users\Alex\Desktop\EtherCAT\ESP32_S3_USB_test`
  - См. `USB_KEYBOARD_IMPLEMENTATION.md` для деталей
- ✅ IO Expansion через MCP23S17 (16 GPIO, вход/выход) - расширение I/O каналов
- ✅ CAN bus мост (для VFD)
- ✅ ESP-NOW беспроводной мост
- ✅ **Telnet конфигурация** (вместо веб-сервера на ESP32)
- ✅ **Внешний веб-интерфейс** на компьютере
- ✅ **Сохранение конфигурации в Flash** (автоматическое при изменении, загрузка при старте)
- ✅ **Индикатор статуса Telnet соединения** (в веб-интерфейсе)

### 1.2 Ключевые отличия от v3

**Новая архитектура конфигурации:**
- ❌ **Удален веб-сервер и HTML страница из ESP32** (экономия ~30KB Flash)
- ✅ **Telnet сервер на Socket 1 W5500** (работает параллельно с EtherCAT)
- ✅ **Внешний веб-интерфейс на компьютере** (гибкость, современные технологии)
- ✅ **HTTP-to-Telnet bridge** (совместимость с существующим интерфейсом)
- ✅ **Опционально USB Serial** (для локальной настройки)

**Преимущества:**
- W5500 полностью используется для EtherCAT в реальном времени
- Конфигурация доступна во время работы EtherCAT
- Минимальная нагрузка на ESP32
- Удаленное управление
- Экономия памяти
- Конфигурация сохраняется в Flash и не теряется при перезагрузке
- Автоматическая загрузка конфигурации при старте ESP32

### 1.3 Целевое применение

**CNC станки и промышленная автоматизация:**
- Фрезерные станки (3-5 осей)
- Токарные станки
- Лазерные граверы
- 3D принтеры (промышленные)
- Роботы-манипуляторы

### 1.4 Hardware

**ESP32-S3:**
- CPU: Dual-core Xtensa LX7 @ 240MHz
- RAM: 512KB SRAM + 8MB PSRAM
- Flash: 8MB
- GPIO: 45 pins (32 usable for I/O)
- USB: USB OTG (для Keyboard и Serial)

**W5500 Ethernet (x2):**
- IN: MACRAW mode для EtherCAT (Socket 0)
- OUT: MACRAW mode для EtherCAT (Socket 0)
- Socket 1: TCP/Telnet для конфигурации (порт 23)
- SPI @ 40MHz с DMA

**Опционально:**
- MCP2515 (CAN controller) - на SPI Bus 2
- ESP32 дополнительный (ESP-NOW)
- MCP23S17 (IO Expander) - на SPI Bus 2, 16 GPIO (вход/выход)

**Важно - ограничения:**
- ❌ **ESP32 WiFi НЕ используется** для конфигурации (конфликт с ESP-NOW)
- ✅ ESP-NOW и WiFi используют один радио канал (2.4GHz) и не могут работать параллельно
- ✅ Конфигурация только через W5500 Socket 1 (Telnet) или USB Serial

---

## 2. Архитектура системы

### 2.1 Режимы работы

**OPERATIONAL Mode (GPIO 12 = HIGH):**
- ✅ W5500 Socket 0: EtherCAT (MACRAW mode) - реальное время
- ✅ W5500 Socket 1: Telnet (TCP mode, порт 23) - конфигурация
- ✅ Оба работают параллельно и независимо
- ✅ Конфигурация доступна во время работы EtherCAT

**CONFIG Mode (GPIO 12 = LOW):**
- ✅ W5500 Socket 0: TCP/IP (для обратной совместимости, опционально)
- ✅ W5500 Socket 1: Telnet (TCP mode, порт 23) - конфигурация
- ✅ USB Serial: опционально для локальной настройки

### 2.2 Общая архитектура

```
┌─────────────────────────────────────────────────────────────┐
│                    ESP32-S3                                │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐  │
│  │  W5500 IN/OUT                                       │  │
│  │                                                      │  │
│  │  ┌──────────────┐         ┌──────────────┐         │  │
│  │  │ Socket 0     │         │ Socket 1     │         │  │
│  │  │ MACRAW       │         │ TCP/Telnet   │         │  │
│  │  │ EtherCAT     │         │ Port: 23     │         │  │
│  │  │              │         │              │         │  │
│  │  │ Real-time    │         │ Config API   │         │  │
│  │  │ PDO обмен    │         │              │         │  │
│  │  └──────┬───────┘         └──────┬───────┘         │  │
│  │         │                         │                 │  │
│  │  ┌──────▼───────┐         ┌──────▼───────┐         │  │
│  │  │ EtherCAT     │         │ Telnet Server │         │  │
│  │  │ Stack        │         │ Handler       │         │  │
│  │  │              │         │              │         │  │
│  │  │ FreeRTOS     │         │ JSON-RPC      │         │  │
│  │  │ Tasks        │         │ Parser        │         │  │
│  │  └──────────────┘         └──────┬───────┘         │  │
│  │                                   │                 │  │
│  │                          ┌────────▼────────┐        │  │
│  │                          │  API Handlers   │        │  │
│  │                          │  (существующие) │        │  │
│  │                          └─────────────────┘        │  │
│  └─────────────────────────────────────────────────────┘  │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐  │
│  │  USB Serial (опционально)                           │  │
│  │  - Локальная настройка                              │  │
│  │  - Отладка                                          │  │
│  └─────────────────────────────────────────────────────┘  │
└─────────────────────────────────────────────────────────────┘
                       │
                       │ Ethernet (TCP/IP)
                       │
┌──────────────────────▼──────────────────────────────────────┐
│                    Компьютер                                │
│                                                             │
│  ┌─────────────────────────────────────────────────────┐  │
│  │  Внешний веб-интерфейс (HTML/JS)                    │  │
│  │  - Настройка конфигурации                           │  │
│  │  - Генерация ESI/HAL                               │  │
│  │  - Экспорт/импорт конфигураций                      │  │
│  └───────────────────┬─────────────────────────────────┘  │
│                      │                                      │
│  ┌───────────────────▼─────────────────────────────────┐  │
│  │  HTTP-to-Telnet Bridge (Node.js/Python)             │  │
│  │  - HTTP сервер (localhost:3000)                     │  │
│  │  - Telnet клиент (ESP32:23)                         │  │
│  │  - Преобразование HTTP ↔ Telnet                     │  │
│  └───────────────────┬─────────────────────────────────┘  │
│                      │                                      │
│                      │ Telnet (TCP, порт 23)                │
└──────────────────────┼──────────────────────────────────────┘
```

### 2.3 W5500 Socket Architecture

**W5500 имеет 8 независимых сокетов:**

- **Socket 0:** MACRAW mode для EtherCAT (реальное время)
- **Socket 1:** TCP mode для Telnet конфигурации (порт 23)
- **Socket 2-7:** Свободны для будущего использования

**Ключевой момент:**
- Сокеты работают независимо и параллельно
- Socket 0 (EtherCAT) не влияет на Socket 1 (Telnet)
- Минимальная нагрузка на CPU

---

## 3. Модули системы

### 3.1 Core EtherCAT Stack

**Файлы:** `src/ethercat_*.h`, `src/ethercat_*.cpp`

**Модули:**
- `ethercat_type.h` - Типы данных, структуры, регистры
- `ethercat_frame.h/cpp` - Парсинг EtherCAT фреймов
- `ethercat_memory.h/cpp` - Memory map (8KB)
- `ethercat_fsm.h/cpp` - State Machine (INIT→PREOP→SAFEOP→OP)
- `ethercat_stack.h/cpp` - Главный стек (FreeRTOS tasks)

**Функциональность:**
- Парсинг EtherCAT фреймов (APRD, APWR, LRW)
- Обработка FSM переходов
- PDO обмен (Input/Output)
- CoE (CANopen over EtherCAT)
- Distributed Clock синхронизация

### 3.2 W5500 Drivers

**Файлы:** `src/w5500_*.h`, `src/w5500_*.cpp`

**Модули:**
- `w5500_macraw.h/cpp` - MACRAW mode для EtherCAT (Socket 0)
- `w5500_tcp.h/cpp` - TCP mode для Telnet (Socket 1)

**Функциональность:**
- Инициализация W5500
- Управление сокетами
- Прием/передача данных
- Параллельная работа нескольких сокетов

**Обработка TCP соединений (v4.1):**

W5500 поддерживает только одно соединение на сокет одновременно. Реализована корректная обработка состояний TCP:

**Состояния сокета:**
- `LISTEN` (0x14) - ожидание соединения
- `SYN_RECV` (0x15) - получен SYN, требуется ACCEPT
- `ESTABLISHED` (0x17) - соединение установлено
- `CLOSE_WAIT` (0x1C) - получен FIN, требуется CLOSE
- `CLOSED` (0x00) - сокет закрыт

**Обработка соединений:**
- При получении `SYN_RECV` → отправка команды `W5500_Sn_CR_ACCEPT` → переход в `ESTABLISHED`
- При получении `CLOSE_WAIT` → отправка команды `W5500_Sn_CR_CLOSE` → переход в `CLOSED`
- При состоянии `CLOSED` → автоматическое открытие сокета (`W5500_Sn_CR_OPEN`) и возврат в `LISTEN`
- Проверка статуса `ESTABLISHED` перед отправкой данных
- Повторные попытки отправки (до 10 попыток с задержкой 1ms) при временных ошибках

**Команды W5500:**
- `W5500_Sn_CR_OPEN` - открыть сокет
- `W5500_Sn_CR_LISTEN` - начать прослушивание
- `W5500_Sn_CR_ACCEPT` - принять соединение (для SYN_RECV)
- `W5500_Sn_CR_CLOSE` - закрыть соединение
- `W5500_Sn_CR_RECV` - получить данные

### 3.3 Network Configuration

**Файлы:** `src/network_config.h`, `src/network_config.cpp`

**Функциональность:**
- Назначение статических IP адресов для Telnet сети
- Автоматическое назначение на основе EtherCAT Station Address
- Сохранение/загрузка сетевой конфигурации в NVS
- Валидация IP адресов
- Определение следующего устройства в daisy chain

**Стратегия назначения IP (комбинированный подход):**

1. **При первом запуске:**
   - Автоматическое назначение: `IP = 192.168.20.X`, где `X = EtherCAT Station Address`
   - Если Station Address = 0 или > 254, использовать адрес по умолчанию: `192.168.20.1`

2. **При наличии сохраненной конфигурации (NVS):**
   - Загрузить IP адрес из NVS
   - Использовать сохраненный адрес

3. **При ручной настройке:**
   - Пользователь может изменить IP через Telnet команду `set_ip_address`
   - Новый адрес сохраняется в NVS

4. **Определение следующего устройства:**
   - Автоматически: `next_ip = current_ip + 1`
   - Или через конфигурацию: `set_next_device_ip`

**API:**
```cpp
// Инициализация сетевой конфигурации
void initialize_network_config(network_config_t* config);

// Сохранение/загрузка из NVS
boolean config_storage_save_network_config(network_config_t* config);
boolean config_storage_load_network_config(network_config_t* config);

// Валидация IP адреса
boolean validate_ip_address(uint8_t* ip);

// Вычисление IP следующего устройства
void calculate_next_device_ip(uint8_t* current_ip, uint8_t* next_ip);
```

**Структура данных:**
```cpp
typedef struct {
    uint8_t ip[4];              // IP адрес (192.168.20.X)
    uint8_t subnet[4];          // Subnet mask (255.255.255.0)
    uint8_t gateway[4];         // Gateway (192.168.20.1)
    uint8_t next_device_ip[4];  // IP следующего устройства в цепочке
    boolean auto_assigned;       // Флаг автоматического назначения
} network_config_t;
```

**Валидация IP адресов:**
- IP должен быть в подсети `192.168.20.0/24`
- Последний октет должен быть в диапазоне 1-254
- Проверка конфликтов адресов (опционально, через ARP)

**Подробности:** См. `ИССЛЕДОВАНИЕ_TELNET_ДАЙЗИ_ЧЕЙН.md`, раздел 2.2

### 3.4 Telnet Configuration Server

**Файлы:** `src/telnet_server.h`, `src/telnet_server.cpp`

**Функциональность:**
- Telnet сервер на Socket 1 W5500 (порт 23)
- JSON-RPC протокол через Telnet
- Парсинг команд и формирование ответов
- Переиспользование существующих API handlers
- Поддержка daisy chain топологии (опционально)
- **Обработка TCP соединений с автоматическим возвратом в LISTEN**
- **Команда ACCEPT для принятия новых соединений**
- **Улучшенная обработка состояний W5500 сокета**

**API:**
```cpp
// Инициализация
boolean telnet_server_init(telnet_server_t* server, w5500_tcp_t* w5500);

// Обработка соединений (вызывать в loop)
void telnet_server_process(telnet_server_t* server);

// Отправка ответа
boolean telnet_server_send_response(telnet_server_t* server, const char* response);

// Отправка ошибки
boolean telnet_server_send_error(telnet_server_t* server, const char* error, int64_t id);
```

**Протокол:**
- Команда: `{"jsonrpc":"2.0","id":1,"method":"get_config","params":{}}`
- Ответ: `{"id":1,"success":true,"result":{...}}`
- Уведомление: `{"jsonrpc":"2.0","method":"keyboard_key_pressed","params":{...}}`

**Обработка TCP соединений:**

W5500 поддерживает только одно соединение на сокет одновременно. Реализована корректная обработка состояний:
- `SYN_RECV` → отправка команды `ACCEPT` → переход в `ESTABLISHED`
- `CLOSE_WAIT` → отправка команды `CLOSE` → переход в `CLOSED`
- `CLOSED` → автоматическое открытие сокета и возврат в `LISTEN`
- Проверка статуса `ESTABLISHED` перед отправкой данных
- Автоматическое восстановление после разрыва соединения

**Команды Telnet:**
- `get_system_info` - информация о системе
- `get_network_config` - конфигурация сети
- `get_gpio_channels` - список GPIO каналов
- `get_encoders` - список энкодеров
- `set_gpio_channel` - создание/обновление канала
- `delete_gpio_channel` - удаление канала
- `set_encoder` - создание/обновление энкодера
- `delete_encoder` - удаление энкодера
- `get_config` - полная конфигурация устройства
- `save_config` - явное сохранение конфигурации в Flash
- `get_io_expansion_config` - конфигурация MCP23S17
- `set_io_expansion_config` - сохранение конфигурации MCP23S17
- `get_espnow_status` - статус ESP-NOW bridge
- `get_stats` - статистика EtherCAT и Telnet

### 3.5 USB Serial API (опционально)

**Файлы:** `src/serial_api.h`, `src/serial_api.cpp`

**Функциональность:**
- Serial command handler через USB
- JSON-RPC протокол через Serial
- Переиспользование существующих API handlers

**Использование:**
- Локальная настройка
- Отладка
- Когда нет сетевого подключения

### 3.6 GPIO Manager

**Файлы:** `src/gpio_manager.h`, `src/gpio_manager.cpp`

**Функциональность:**
- Управление I/O каналами (до 32)
- Управление энкодерами (до 8)
- USB HID Keyboard поддержка
- MCP23S17 IO Expansion поддержка
- PDO mapping
- **Сохранение конфигурации в Flash (автоматическое)**
- **Загрузка конфигурации из Flash при старте**

**Сохранение конфигурации в постоянную память:**

Конфигурация автоматически сохраняется в Flash через ESP32 Preferences API при каждом изменении:
- При добавлении/изменении/удалении канала → автоматическое сохранение
- При добавлении/изменении/удалении энкодера → автоматическое сохранение
- При инициализации MCP23S17 → автоматическое сохранение

**API сохранения:**
```cpp
// Сохранение конфигурации в Flash
boolean gpio_manager_save_to_flash(gpio_manager_t* manager);

// Загрузка конфигурации из Flash
boolean gpio_manager_load_from_flash(gpio_manager_t* manager);
```

**Хранение данных:**
- Namespace: `"gpio_cfg"` (ESP32 Preferences API)
- Сохраняются: каналы, энкодеры, конфигурация MCP23S17
- Данные сохраняются в NVS (Non-Volatile Storage) Flash памяти
- Автоматическая загрузка при инициализации GPIO Manager

**Преимущества:**
- Конфигурация сохраняется после перезагрузки ESP32
- Не требуется повторная синхронизация после перезагрузки
- Работает автономно после первоначальной настройки

### 3.7 Configuration Storage

**Файлы:** `src/config_storage.h`, `src/config_storage.cpp`

**Функциональность:**
- NVS storage для конфигурации
- JSON экспорт/импорт
- Валидация конфигурации
- Multi-profile поддержка

### 3.8 API Handlers

**Файлы:** `src/api_handlers.h`, `src/api_handlers.cpp`

**Функциональность:**
- Обработка команд конфигурации
- GPIO channels management
- Encoders management
- PDO mapping
- IO Expansion configuration
- ESP-NOW configuration
- EtherCAT device configuration

**Используются:**
- Telnet сервером (JSON-RPC)
- USB Serial API (JSON-RPC)
- HTTP-to-Telnet bridge (через Telnet)

### 3.9 ESP-NOW Configuration Proxy

**Файлы:** `src/espnow_config_proxy.h`, `src/espnow_config_proxy.cpp`

**Функциональность:**
- Пересылка Telnet команд на удаленные ESP32 устройства через ESP-NOW
- Discovery протокол для поиска всех ESP-NOW устройств
- Кэширование конфигураций удаленных устройств
- Обработка ответов от удаленных устройств
- Гибридный подход: Proxy для записи, Aggregator для чтения

**Архитектура (Гибридный подход):**

1. **Proxy режим (для изменения конфигураций):**
   - Прямая пересылка JSON-RPC команд на указанное устройство
   - Ожидание ответа с timeout (по умолчанию 5 секунд)
   - Подтверждение получения ответа
   - Автоматическое обновление кэша после успешного изменения

2. **Aggregator режим (для чтения конфигураций):**
   - Сбор конфигураций всех устройств в кэш
   - Быстрое чтение из кэша (без задержек ESP-NOW)
   - Периодическое обновление кэша через ESP-NOW
   - Валидация актуальности кэша (timestamp)

3. **Discovery протокол:**
   - Broadcast запросы через ESP-NOW (`ESPNOW_MSG_DISCOVERY_REQ`)
   - Сбор ответов от всех устройств (`ESPNOW_MSG_DISCOVERY_RESP`)
   - Информация об устройстве: MAC, имя, версия прошивки, RSSI
   - Автоматическое добавление в список peers

**Протокол ESP-NOW для конфигурации:**

**Типы сообщений:**
- `ESPNOW_MSG_CONFIG_COMMAND` (0x04) - JSON-RPC команда
- `ESPNOW_MSG_CONFIG_RESPONSE` (0x05) - JSON-RPC ответ
- `ESPNOW_MSG_CONFIG_ACK` (0x06) - Подтверждение получения
- `ESPNOW_MSG_CONFIG_FRAGMENTED` (0x07) - Фрагментированная конфигурация
- `ESPNOW_MSG_DISCOVERY_REQ` (0x08) - Discovery запрос
- `ESPNOW_MSG_DISCOVERY_RESP` (0x09) - Discovery ответ

**Формат пакета:**
```
[msg_type:1][data_len:1][data:data_len][sequence:4][timestamp_us:4]
```

**Ограничения:**
- Максимальный размер пакета: 250 bytes (ограничение ESP-NOW)
- Для больших конфигураций: фрагментация на несколько пакетов
- Timeout для конфигурационных команд: 5 секунд (настраивается)

**Структура кэша:**
```cpp
typedef struct {
    uint8_t mac[6];              // MAC адрес устройства
    uint32_t last_update;        // Timestamp последнего обновления
    boolean valid;              // Флаг валидности кэша
    char config_json[512];       // Кэшированная конфигурация (JSON)
    uint16_t config_len;        // Длина конфигурации
} cached_device_config_t;
```

**Стратегия использования:**
- **Чтение:** `get_all_configs` → возвращает из кэша (быстро)
- **Обновление кэша:** `refresh_all_configs` → обновляет через ESP-NOW
- **Изменение:** `set_config` с `target` → Proxy режим (прямая пересылка)
- **Discovery:** `discover_espnow_devices` → broadcast запрос

**Telnet команды:**
```json
// Discovery всех ESP-NOW устройств
{
  "jsonrpc": "2.0",
  "method": "discover_espnow_devices",
  "params": {},
  "id": 1
}

// Получить конфигурацию удаленного устройства
{
  "jsonrpc": "2.0",
  "method": "get_config",
  "params": {
    "target": "remote_1",
    "peer_mac": "AA:BB:CC:DD:EE:FF"
  },
  "id": 2
}

// Получить конфигурации всех устройств (из кэша)
{
  "jsonrpc": "2.0",
  "method": "get_all_configs",
  "params": {},
  "id": 3
}
```

**Реализация:**

1. **Расширение ESP-NOW Bridge:**
   - Добавление полей для конфигурационного proxy в `espnow_bridge_t`
   - Функция `espnow_send_config_command()` для отправки команд
   - Функция `espnow_broadcast_discovery()` для поиска устройств
   - Обработка конфигурационных сообщений в `espnow_on_data_recv()`

2. **ESP-NOW Config Proxy модуль:**
   - Структура кэша конфигураций
   - Функции управления кэшем (`refresh_config_cache()`, `get_cached_config()`)
   - Интеграция с Telnet Server для обработки параметра `target`

3. **Интеграция с Telnet Server:**
   - Поддержка параметра `target` в JSON-RPC командах
   - Автоматическое определение режима (Proxy/Aggregator)
   - Обработка timeout и ошибок ESP-NOW

**Подробности:** См. `ИССЛЕДОВАНИЕ_КОНФИГУРАЦИЯ_ESP_NOW_УСТРОЙСТВ.md`, раздел "Гибридный подход (Proxy + Aggregator)"

### 3.10 StepGen (LinuxCNC STEP/DIR)

**Файлы:** `src/stepgen.h`, `src/stepgen.cpp`

**Функциональность:**
- Генерация STEP/DIR для шаговых приводов (до 4 осей) по EtherCAT PDO
- Режимы: VELOCITY (cmd = Hz), POSITION (cmd = absolute steps)
- POSITION: P-регулятор \(target_hz = error_steps * pos_gain_hz_per_step\) + `pos_deadband_steps`
- Ограничение ускорения (через `max_accel_units_s2 * steps_per_unit`)
- Ограничение частоты: `max_step_hz` и минимальный период `step_time_us + step_space_us`
- Задержка смены направления: `dir_setup_us`
- Backend генерации STEP: **MCPWM** (основной), **LEDC** (fallback)

**PDO mapping (v2, 8 байт на ось):**
- OUT: `[enable:1][mode:1][cmd:int32 LE][reserved:2]`
- IN:  `[fb_vel_hz:int32 LE][fb_pos_steps:int32 LE]`

---

## 4. Конфигурация через Telnet

### 4.1 Протокол обмена

**Формат команды (JSON-RPC):**
```json
{
  "id": 12345,
  "method": "get_config",
  "params": {}
}
```

**Формат ответа:**
```json
{
  "id": 12345,
  "success": true,
  "result": {
    "gpio_channels": [...],
    "encoders": [...],
    "io_expansion": {...}
  }
}
```

**Обработка ошибок:**
```json
{
  "id": 12345,
  "success": false,
  "error": "Invalid method"
}
```

### 4.2 Доступные методы

**Configuration:**
- `get_config` - Экспорт конфигурации
- `set_config` - Импорт конфигурации (не реализовано)
- `save_config` - Явное сохранение конфигурации в Flash
- `reset_config` - Сброс конфигурации (не реализовано)

**GPIO Channels:**
- `get_gpio_channels` - Список каналов
- `set_gpio_channel` - Добавить/изменить канал
- `delete_gpio_channel` - Удалить канал

**Encoders:**
- `get_encoders` - Список энкодеров
- `set_encoder` - Добавить/изменить энкодер
- `delete_encoder` - Удалить энкодер
- `reset_encoder` - Сбросить счетчик

**StepGen:**
- `get_stepgen_config` - Список осей и параметры StepGen
- `set_stepgen_config` - Установить параметры оси

**Testing:**
- `test_usb_keyboard` - Тест USB клавиатуры (прямое нажатие/отпускание клавиши)
  - Параметры: `key_code` (uint8), `modifiers` (uint8, опционально), `duration_ms` (uint16, опционально, 0 = только нажать)
  - Пример: `{"method": "test_usb_keyboard", "params": {"key_code": 4, "modifiers": 0, "duration_ms": 100}}`
- `validate_usb_keyboard` - Health check всех USB keyboard каналов (санитизация перед использованием)
  - Параметры: `test_enabled` (boolean, опционально, false = только проверка конфигурации), `test_duration_ms` (uint16, опционально, 50)
  - Возвращает: массив результатов для каждого настроенного USB keyboard канала
  - Пример: `{"method": "validate_usb_keyboard", "params": {"test_enabled": true, "test_duration_ms": 50}}`

**USB Keyboard инициализация и проверка окружения:**
- USB Keyboard автоматически инициализируется при старте GPIO Manager (если ESP32-S3)
- Функция `usb_keyboard_is_available()` проверяет наличие USB окружения (подключение к ПК)
- Функция `usb_keyboard_test_connection(dp_pin, dm_pin)` тестирует подключение USB через проверку пинов D+ и D-
  - ESP32-S3: D+ = GPIO 21, D- = GPIO 19
  - Проверяет наличие напряжения на пинах при подключении к ПК
  - Выводит детальную информацию в Serial Monitor
- Статус USB окружения выводится в Serial Monitor при инициализации
- Позволяет пользователю добавлять USB keyboard каналы через веб-интерфейс сразу после запуска

**IO Expansion:**
- `get_io_expansion_config` - Конфигурация MCP23S17
- `set_io_expansion_config` - Сохранить конфигурацию MCP23S17

**ESP-NOW:**
- `get_espnow_config` - Конфигурация ESP-NOW (локальная)
- `set_espnow_config` - Сохранить конфигурацию ESP-NOW (локальная)
- `discover_espnow_devices` - Найти все ESP-NOW устройства
- `get_espnow_status` - Статус ESP-NOW bridge
- `get_config` - Получить конфигурацию (локальную или удаленную через `target`)
- `set_config` - Сохранить конфигурацию (локальную или удаленную через `target`)
- `get_all_configs` - Получить конфигурации всех устройств (из кэша)
- `refresh_all_configs` - Обновить кэш конфигураций всех устройств

**EtherCAT Device:**
- `get_ethercat_device` - Параметры устройства
- `set_ethercat_device` - Сохранить параметры

**System:**
- `get_status` - Статус системы
- `get_system_info` - Системная информация
- `reboot` - Перезагрузка

**Network Configuration (Telnet сеть / Daisy Chain):**
- `get_network_config` - Текущая сетевая конфигурация (ip/subnet/gateway/next_device_ip)
- `set_next_device_ip` - Установить IP следующего устройства в цепочке

*(Остальные методы `set_ip_address`, `auto_assign_ip`, `reset_network_config` — запланированы, см. Roadmap)*

### 4.3 Назначение статических IP адресов (для Daisy Chain)

**Рекомендуемый подход: Комбинированный**

1. **При первом запуске:**
   - Автоматическое назначение: `IP = 192.168.20.X`, где `X = EtherCAT Station Address`
   - Если Station Address = 0 или > 254, использовать адрес по умолчанию: `192.168.20.1`

2. **При наличии сохраненной конфигурации (NVS):**
   - Загрузить IP адрес из NVS
   - Использовать сохраненный адрес

3. **При ручной настройке:**
   - Пользователь может изменить IP через Telnet команду `set_ip_address`
   - Новый адрес сохраняется в NVS

4. **Определение следующего устройства:**
   - Автоматически: `next_ip = current_ip + 1`
   - Или через конфигурацию: `set_next_device_ip`

**Валидация IP адресов:**
- IP должен быть в подсети `192.168.20.0/24`
- Последний октет должен быть в диапазоне 1-254
- Проверка конфликтов адресов (опционально, через ARP)

**Сохранение в NVS:**
- Структура: `nvs_network_config_t` (ip, subnet, gateway, next_device_ip, station_address)
- Magic number: `0xDEADBEEF` для проверки валидности
- Функции: `config_storage_save_network_config()`, `config_storage_load_network_config()`

**Подробности:** См. `ИССЛЕДОВАНИЕ_TELNET_ДАЙЗИ_ЧЕЙН.md`, раздел 2.2

### 4.4 Реализация на ESP32

**Инициализация:**
```cpp
// В main.cpp, OPERATIONAL mode
network_config_t network_config;  // Сетевая конфигурация
w5500_tcp_t g_telnet_tcp;         // W5500 TCP для Telnet (Socket 1)
telnet_server_t g_telnet_server;  // Telnet сервер

void setup() {
    // ... инициализация EtherCAT Stack (Socket 0) ...
    
    // Инициализация сетевой конфигурации
    initialize_network_config(&network_config);
    
    // Инициализация Telnet сервера (Socket 1)
    w5500_tcp_init(&g_telnet_tcp, 
                   W5500_CS_IN,
                   W5500_RST_IN,
                   MAC_ADDRESS,
                   network_config.ip,        // Назначенный IP
                   network_config.subnet,
                   network_config.gateway,
                   20000000);
    
    w5500_tcp_listen(&g_telnet_tcp, 23);  // Telnet порт
    
    telnet_server_init(&g_telnet_server, &g_telnet_tcp);
    
    // Для Daisy Chain: инициализация W5500 OUT (Socket 1) как TCP Client
    if (network_config.next_device_ip[0] != 0) {
        w5500_tcp_init(&g_telnet_tcp_out, 
                       W5500_CS_OUT,
                       W5500_RST_OUT,
                       MAC_ADDRESS,
                       network_config.ip,
                       network_config.subnet,
                       network_config.gateway,
                       20000000);
    }
}
```

**Обработка:**
```cpp
void loop() {
    // Обработка EtherCAT (Socket 0)
    ethercat_stack_process(&g_ethercat_stack);
    
    // Обработка Telnet (Socket 1) - параллельно
    telnet_server_process(&g_telnet_server);
}
```

---

## 5. Внешний веб-интерфейс

### 5.1 Архитектура

**Компоненты:**
1. **Веб-интерфейс (HTML/JS)** - пользовательский интерфейс
2. **HTTP-to-Telnet Bridge** - преобразование HTTP ↔ Telnet (основной метод)
3. **Telnet клиент** - связь с ESP32
4. **EtherCAT Master Library** - опционально, для прямого доступа через EtherCAT (SOEM/TwinCAT)

**Два варианта доступа:**

**Вариант A: Telnet (рекомендуется, универсальный)**
```
Внешний веб-сервер → HTTP-to-Telnet Bridge → Telnet/TCP → ESP32 Slave
```

**Вариант B: EtherCAT Master (опционально, для производительности)**
```
Внешний веб-сервер → EtherCAT Master (SOEM/TwinCAT) → EtherCAT → ESP32 Slave
```

### 5.2 HTTP-to-Telnet Bridge (Новая архитектура)

**Технологии:**
- Node.js + Express.js (рекомендуется)
- Python + Flask (альтернатива)

**Новая концепция:**
Сервер имеет свою карту пинов ESP32 и окружения периферии для каждого варианта микроконтроллера. Конфигурации создаются локально на сервере и синхронизируются с ESP32 по требованию через Telnet.

**Очередь запросов:**
- Запросы к ESP32 выполняются последовательно через очередь
- Задержка 150ms между запросами для предотвращения конфликтов с одним сокетом W5500
- Автоматическая обработка очереди с повторными попытками при ошибках

**Улучшенный парсинг JSON:**
- Поиск полного JSON объекта по балансу скобок вместо разбора по строкам
- Обработка накопленных данных от предыдущих соединений
- Автоматическая очистка буфера при превышении размера

**Индикатор статуса Telnet соединения:**
- Периодическая проверка соединения каждые 5 секунд
- Автоматическое обновление при смене устройства
- Отображение статуса "Telnet Connected" / "Telnet Disconnected" в веб-интерфейсе

**Компоненты:**

1. **Device Profiles (Профили устройств):**
   - Карта пинов для каждого варианта ESP32 (ESP32-S3, ESP32, ESP32-C3)
   - Информация о доступной периферии (USB, WiFi, CAN, IO Expansion)
   - Ограничения устройства (max GPIO channels, encoders, etc.)

2. **Device Registry (Реестр устройств):**
   - Хранение информации о подключенных устройствах
   - Автоматическое определение типа устройства при подключении
   - Связь IP адреса с профилем устройства

3. **Configuration Builder (Построитель конфигураций):**
   - Локальное создание конфигураций без подключения к ESP32
   - Валидация пинов на стороне сервера
   - Проверка конфликтов и ограничений

4. **Sync Mechanism (Механизм синхронизации):**
   - Отправка конфигурации на ESP32 по команде
   - Валидация перед синхронизацией
   - Отслеживание результатов синхронизации

**Пример (Node.js):**
```javascript
const express = require('express');
const { DeviceProfiles, DeviceRegistry, ConfigurationBuilder } = require('./device-manager');

const app = express();
app.use(express.static('public'));
app.use(express.json());

// Device Profiles
const profiles = new DeviceProfiles();
profiles.load('device-profiles.json');

// Device Registry
const registry = new DeviceRegistry();

// Получение профилей устройств
app.get('/api/device-profiles', (req, res) => {
    res.json({ profiles: profiles.list() });
});

// Получение профиля устройства
app.get('/api/device-profiles/:profile', (req, res) => {
    const profile = profiles.get(req.params.profile);
    res.json(profile);
});

// Создание конфигурации для профиля
app.post('/api/configurations', (req, res) => {
    const { profile, config } = req.body;
    const builder = new ConfigurationBuilder(profiles.get(profile));
    // Валидация и построение конфигурации
    const validatedConfig = builder.build(config);
    res.json({ id: 'config-123', profile, config: validatedConfig });
});

// Определение типа устройства
app.post('/api/devices/:ip/detect-profile', async (req, res) => {
    const systemInfo = await sendJsonRpcOnce(req.params.ip, 'get_system_info');
    const profile = profiles.detect(systemInfo);
    registry.setProfile(req.params.ip, profile);
    res.json({ profile, systemInfo });
});

// Синхронизация конфигурации с устройством
app.post('/api/devices/:ip/sync-config', async (req, res) => {
    const { configId } = req.body;
    const config = getConfiguration(configId);
    const device = registry.get(req.params.ip);
    
    // Валидация против профиля устройства
    const profile = profiles.get(device.profile);
    validateConfiguration(config, profile);
    
    // Синхронизация
    const results = await syncConfigurationToDevice(req.params.ip, config);
    res.json(results);
});

app.listen(3000);
```

**Преимущества новой архитектуры:**
- ✅ Локальное создание конфигураций без подключения к ESP32
- ✅ Валидация пинов на стороне сервера
- ✅ Поддержка разных вариантов ESP32
- ✅ Улучшенный UX (автозаполнение, валидация в реальном времени)
- ✅ Работа без постоянного подключения к устройству
- ✅ Универсальный (работает на любой платформе)
- ✅ Не требует специальных драйверов
- ✅ Не конфликтует с EtherCAT Master

**Подробности:** См. `АНАЛИЗ_НОВОЙ_АРХИТЕКТУРЫ_СЕРВЕРА.md`

### 5.2.1 Device Profiles (Профили устройств)

**Концепция:**
Сервер хранит карты пинов и информацию о периферии для каждого варианта ESP32, позволяя создавать конфигурации локально без подключения к устройству.

**Структура профиля:**
```javascript
{
  chip: 'ESP32-S3',           // Название чипа
  variant: 'esp32s3',          // Вариант для PlatformIO
  pins: {
    gpio: [0, 1, 2, ...],     // Доступные GPIO пины
    spi1: { sck, miso, mosi }, // SPI Bus 1 (EtherCAT)
    spi2: { sck, miso, mosi }, // SPI Bus 2 (Peripherals)
    w5500_in: { cs, rst },     // W5500 IN
    w5500_out: { cs, rst },    // W5500 OUT
    usb: { dp, dm } | null,    // USB (если доступен)
    can: { cs, int },          // CAN (MCP2515)
    mcp23s17: { cs }           // IO Expansion
  },
  peripherals: {
    usb: boolean,              // USB HID Keyboard support
    wifi: boolean,              // WiFi support
    bluetooth: boolean,         // Bluetooth support
    ethernet: boolean,          // W5500 support
    can: boolean,               // MCP2515 support
    io_expansion: boolean       // MCP23S17 support
  },
  constraints: {
    max_gpio_channels: number,  // Максимум GPIO каналов
    max_encoders: number,       // Максимум энкодеров
    max_stepgen_axes: number,   // Максимум осей StepGen
    max_analog_inputs: number,  // Максимум аналоговых входов
    max_analog_outputs: number // Максимум аналоговых выходов
  }
}
```

**Поддерживаемые профили:**
- **ESP32-S3:** Полная поддержка (USB, WiFi, Bluetooth, Ethernet, CAN, IO Expansion)
- **ESP32:** Без USB HID Keyboard (нет USB OTG)
- **ESP32-C3:** Ограниченная поддержка (меньше GPIO, нет Bluetooth)

### 5.2.2 Device Registry (Реестр устройств)

**Назначение:**
Хранение информации о подключенных устройствах и их профилях.

**Структура записи:**
```javascript
{
  ip: '192.168.20.1',
  profile: 'ESP32-S3',         // Определяется при подключении
  systemInfo: {                // Информация с ESP32
    chip_model: 'ESP32-S3',
    chip_revision: 0,
    cpu_freq_mhz: 240,
    free_heap: 200000,
    // ...
  },
  lastSeen: Date.now(),        // Время последнего подключения
  lastSynced: Date.now(),      // Время последней синхронизации
  config: { ... },             // Локальная конфигурация
  status: 'connected'          // 'disconnected' | 'connected' | 'syncing' | 'synced'
}
```

**Автоматическое определение профиля:**
При подключении к устройству сервер запрашивает системную информацию (`get_system_info`) и определяет тип ESP32 по `chip_model`.

### 5.2.3 Configuration Builder (Построитель конфигураций)

**Назначение:**
Локальное создание и валидация конфигураций без подключения к ESP32.

**Функциональность:**
- Валидация пинов против профиля устройства
- Проверка конфликтов пинов (один пин используется дважды)
- Проверка ограничений устройства (max channels, etc.)
- Проверка PDO mapping (конфликты byte/bit)
- Генерация валидной конфигурации

**Пример использования:**
```javascript
const builder = new ConfigurationBuilder(deviceProfile);

// Добавление GPIO канала с валидацией
try {
  builder.addGPIOChannel({
    pin: 36,
    category: 'GPIO_ESP',
    direction: 'input',
    // ...
  });
} catch (error) {
  // Пин недоступен или конфликт
  console.error(error.message);
}

// Построение конфигурации
const config = builder.build();
```

### 5.2.4 Sync Mechanism (Механизм синхронизации)

**Процесс синхронизации:**

1. **Валидация конфигурации:**
   - Проверка против профиля устройства
   - Проверка доступности пинов
   - Проверка конфликтов

2. **Отправка на ESP32:**
   - Каждый канал отправляется отдельной JSON-RPC командой
   - Отслеживание успешных и неудачных операций

3. **Результаты синхронизации:**
   ```javascript
   {
     success: true,
     synced: 10,        // Количество успешно синхронизированных
     errors: 0,         // Количество ошибок
     results: [         // Детальные результаты
       { index: 0, success: true },
       { index: 1, success: true, error: 'Pin conflict' },
       // ...
     ]
   }
   ```

**Workflow:**
```
1. Пользователь создает конфигурацию локально
   ↓
2. Сервер валидирует конфигурацию
   ↓
3. Пользователь подключается к ESP32
   ↓
4. Сервер определяет профиль устройства
   ↓
5. Сервер сравнивает конфигурацию с профилем
   ↓
6. Пользователь нажимает "Синхронизировать"
   ↓
7. Сервер отправляет конфигурацию на ESP32
   ↓
8. Отображаются результаты синхронизации
```

### 5.3 EtherCAT Master для веб-сервера (опционально)

**Описание:**
Внешний веб-сервер может использовать **EtherCAT Master библиотеку** (SOEM/TwinCAT) для прямого доступа к ESP32 Slave через EtherCAT протокол, минуя Telnet.

**Архитектура:**
```
Внешний веб-сервер
    │
    ├─[HTTP]─→ EtherCAT Master Library (SOEM/TwinCAT)
    │              │
    │              └─[EtherCAT/CoE]─→ ESP32 Slave (Socket 0)
```

**Требования:**
- **Linux:** SOEM library (open-source)
- **Windows:** TwinCAT 3 Runtime (коммерческая лицензия) или SOEM через виртуальную машину
- **macOS:** Не поддерживается (только Telnet)

**Реализация:**
- Создание **Object Dictionary** в ESP32 для конфигурации через CoE
- Использование **SDO Upload/Download** для чтения/записи конфигурации
- Node.js/Python bindings для SOEM/TwinCAT

**Преимущества:**
- ✅ Прямой доступ к EtherCAT протоколу
- ✅ Высокая производительность
- ✅ Стандартный протокол (CoE/SDO)
- ✅ Может работать с несколькими Slave одновременно

**Недостатки:**
- ⚠️ Требует специальный драйвер/библиотеку
- ⚠️ Платформенная зависимость (Linux/Windows)
- ⚠️ Может конфликтовать с основным EtherCAT Master
- ⚠️ Сложнее реализация (нужен Object Dictionary)

**Гибридный подход (рекомендуется):**
- Попытка использовать EtherCAT Master (если доступен)
- Fallback на Telnet (если Master недоступен)
- Автоматическое переключение

**Подробности:** См. `ИССЛЕДОВАНИЕ_ETHERCAT_MASTER_ДЛЯ_ВЕБ_СЕРВЕРА.md`

### 5.4 Веб-интерфейс

**Технологии:**
- HTML/CSS/JavaScript (простой вариант)
- React/Vue/Angular (продвинутый вариант)
- Electron (настольное приложение)

**Функциональность (актуально в реализации):**
- **Device Profile Selection (новое):**
  - Выбор типа ESP32 при создании конфигурации (ESP32-S3, ESP32, ESP32-C3)
  - Автоматическое определение типа устройства при подключении
  - Адаптация интерфейса под возможности устройства
- Multi-device: выбор активного устройства (Telnet host) + сканирование цепочки Telnet (daisy chain scan)
- Tabs/секции:
  - **System** (системная информация, ESP-NOW статус, **импорт конфигурации**)
  - **Device config / Network** (в т.ч. `next_device_ip`)
  - **IO каналы** (GPIO_ESP / GPIO_EX / USB_KEY; digital/analog; invert; PDO mapping)
    - Упрощенный интерфейс с динамическим скрытием полей
    - Чекбоксы вместо текстовых полей (Enabled, Invert)
    - Выпадающий список для выбора существующих каналов
    - **Выбор пинов из профиля устройства (новое):**
      - Автозаполнение доступных пинов из выбранного профиля
      - Подсветка недоступных пинов (серым цветом)
      - Предупреждения о конфликтах пинов в реальном времени
      - Валидация перед сохранением
    - Для USB Keyboard: выпадающий список доступных кнопок с понятными названиями
    - Чекбоксы для модификаторов USB (Ctrl, Shift, Alt, Win/GUI)
    - Автоматическое заполнение полей при выборе канала
  - **Энкодеры** (GPIO/GPIO_EX каналы + счетчики)
  - **StepGen** (настройка STEP/DIR, режимы, лимиты)
  - **Маппинг** (предпросмотр PDO маппинга всех устройств + валидация + экспорт)
- Настройка IO Expansion (MCP23S17)

**Вкладка "Маппинг":**
- Сводная таблица всех сигналов (IO/Encoders/StepGen) по всем устройствам
- Валидация: проверка PDO overlaps (byte/bit), превышение размера PDO, некорректные типы
- Экспорт:
  - **LinuxCNC HAL** (`.hal` файл с HAL pin names и примерами подключений)
  - **TwinCAT ESI** (`.esi.xml` файл для импорта в TwinCAT)
  - **Mapping CSV** (таблица маппинга для документации)

**Импорт конфигурации (System tab):**
- Загрузка JSON файла конфигурации (формат `get_config`)
- Применение ко всем устройствам в системе (multi-device)
- Импорт включает: GPIO channels, Encoders, StepGen, Network settings
- Отображение прогресса и результатов импорта

**Тест USB клавиатуры (System tab):**
- Прямое тестирование USB клавиатуры без настройки GPIO каналов
- Параметры: Key Code (HEX), Modifiers (HEX), Duration (ms)
- Быстрые кнопки для тестирования: A, Enter, Escape
- Отправка команды на выбранное устройство через Telnet API

**Health Check USB клавиатуры (санитизация):**
- Проверка всех настроенных USB keyboard каналов перед использованием

**Автоматическое заполнение полей при добавлении USB Keyboard канала:**
- При нажатии клавиши на физической клавиатуре автоматически заполняются:
  - USB key code (HEX) - keycode нажатой клавиши
  - USB кнопка (выпадающий список) - автоматически выбирается соответствующая запись
  - Модификаторы (чекбоксы) - автоматически отмечаются при нажатии с модификаторами
  - Index - автоматически заполняется следующим свободным индексом
  - PDO byte - автоматически заполняется следующим свободным byte для USB каналов
- Поле Name заполняется вручную
- См. `ДОБАВЛЕНИЕ_USB_KEYBOARD_КАНАЛОВ.md` для подробной инструкции

**Двухэтапное сохранение каналов (локальное хранилище + синхронизация с ESP32):**
- **Этап 1 - Локальное сохранение:**
  - При нажатии "Apply" в веб-интерфейсе канал сохраняется в локальное хранилище на сервере `bridge.js` (Map `localChannels`)
  - Каналы хранятся в памяти Node.js сервера по IP устройства
  - Позволяет редактировать, обновлять и удалять каналы без обращения к ESP32
  - Визуальная таблица каналов отображает данные из локального хранилища
- **Этап 2 - Синхронизация с ESP32:**
  - Кнопка "💾 Сохранить на ESP32" отправляет все локальные каналы на ESP32
  - Каждый канал отправляется через JSON-RPC команду `set_gpio_channel`
  - Отображается прогресс синхронизации и результаты (успешно/ошибки)
  - После синхронизации каналы доступны для использования в EtherCAT PDO
- **Преимущества:**
  - Быстрое редактирование без задержек сети
  - Возможность подготовить полную конфигурацию перед записью
  - Защита от потери данных при сетевых сбоях
  - Явный контроль момента записи в память ESP32
- **API endpoints:**
  - `POST /api/devices/:ip/gpio/channel` - сохранение в локальное хранилище
  - `GET /api/devices/:ip/gpio/channels/local` - получение локальных каналов
  - `POST /api/devices/:ip/gpio/channels/sync` - синхронизация с ESP32
  - `POST /api/devices/:ip/gpio/channel/delete/local` - удаление из локального хранилища
- Два режима:
  - **Проверка конфигурации**: валидация настроек (USB инициализирован, key_code валидный)
  - **Проверка + тест**: дополнительно отправляет тестовое нажатие для каждого канала
- Отображает таблицу результатов с детальной информацией по каждому каналу
- Используется для санитизации перед запуском production системы

**Автоматическая инициализация USB Keyboard:**
- USB Keyboard инициализируется автоматически при старте GPIO Manager (если ESP32-S3)
- Проверка USB окружения (подключение к ПК) при инициализации
- Вывод статуса в Serial Monitor:
  - Статус инициализации USB Keyboard
  - Статус USB окружения (доступно/недоступно)
  - Список всех настроенных USB keyboard каналов с параметрами
- Позволяет пользователю добавлять USB keyboard каналы через веб-интерфейс сразу после запуска

**Следующие крупные функции (план):**
- Настройка ESP-NOW (Discovery + Proxy/Aggregator)
- Экспорт конфигураций (JSON) + NVS persistence

**API вызовы (реализация):**
- Базовый URL: `http://localhost:3000/api/*`
- **Device Profiles (новое):**
  - `GET /api/device-profiles` → список доступных профилей
  - `GET /api/device-profiles/:profile` → детали профиля (пины, периферия, ограничения)
  - `POST /api/devices/:ip/detect-profile` → автоматическое определение типа устройства
- **Configurations (новое):**
  - `POST /api/configurations` → создание конфигурации для профиля
  - `GET /api/configurations/:id` → получение конфигурации
  - `POST /api/configurations/:id/validate` → валидация конфигурации
  - `POST /api/devices/:ip/sync-config` → синхронизация конфигурации с устройством
- Multi-device слой:
  - `GET /api/devices`, `POST /api/devices/active`, `POST /api/devices/scan`
  - device-specific: `/api/devices/:ip/*` (GPIO/Encoders/StepGen/Network/ESP-NOW)
- Export endpoints:
  - `POST /api/export/linuxcnc` (body: `{mapping: [...]}`) → `.hal` файл
  - `POST /api/export/twincat` (body: `{mapping: [...]}`) → `.esi.xml` файл
- Bridge преобразует HTTP → Telnet JSON-RPC → HTTP (и Socket.IO для статуса/результатов)

---

## 6. Roadmap разработки

### Phase 1: Core EtherCAT (3 недели)
- EtherCAT типы данных
- Frame parser
- Memory map
- FSM (State Machine)
- W5500 MACRAW driver
- EtherCAT Stack core

### Phase 2: GPIO & I/O (2 недели)
- GPIO Manager
- Digital/Analog I/O
- Encoders
- PDO mapping

### Phase 3: Telnet Configuration (1 неделя)
- Network Configuration (назначение статических IP адресов)
- Telnet сервер на Socket 1
- JSON-RPC протокол
- API handlers интеграция
- Поддержка daisy chain топологии (опционально)
- Тестирование параллельной работы с EtherCAT

### Phase 4: External Web Interface (1 неделя)
- HTTP-to-Telnet bridge (Node.js/Python)
- Веб-интерфейс (HTML/JS)
- Интеграция с существующим UI кодом
- Тестирование

### Phase 5: Advanced Features (3 недели)
- USB HID Keyboard
- IO Expansion (MCP23S17)
- CAN Bridge
- ESP-NOW Bridge
- CoE (CANopen over EtherCAT)
- Distributed Clock

### Phase 6: Testing & Optimization (2 недели)
- Unit tests
- Integration tests
- Performance optimization
- 24/7 stability tests

### Phase 7: Documentation & Release (1 неделя)
- User documentation
- Developer documentation
- Release preparation

**Итого:** 13 недель (3 месяца)

---

## 7. Критерии приемки

### Must Have (обязательно)

**EtherCAT:**
- [ ] SOEM обнаруживает Slave
- [ ] FSM переходы работают (INIT→PREOP→SAFEOP→OP)
- [ ] PDO обмен стабилен (1ms cycle)
- [ ] Latency < 100μs
- [ ] Jitter < 10μs

**I/O:**
- [ ] Digital I/O (32 каналов)
- [ ] Analog I/O (8 каналов)
- [ ] Encoders (8 штук)
- [ ] IO Expansion через MCP23S17 (16 GPIO)

**Configuration:**
- [ ] Telnet сервер работает на Socket 1
- [ ] Конфигурация доступна во время работы EtherCAT
- [ ] Внешний веб-интерфейс работает
- [ ] HTTP-to-Telnet bridge функционирует
- [ ] NVS сохраняет конфигурацию
- [ ] Назначение статических IP адресов работает
- [ ] Автоматическое назначение IP на основе Station Address
- [ ] Сохранение/загрузка сетевой конфигурации из NVS
- [ ] Telnet команды для управления IP адресами (`get_ip_address`, `set_ip_address`, `set_next_device_ip`)
- [ ] Поддержка daisy chain топологии (опционально)

**Performance:**
- [ ] CPU load < 70%
- [ ] Heap < 100KB
- [ ] 24/7 uptime
- [ ] Параллельная работа EtherCAT и Telnet без конфликтов

### Should Have (желательно)

- [ ] USB Serial API (опционально)
- [ ] DC (Distributed Clock)
- [ ] CoE (Mailbox + SDO)
- [ ] CAN Bridge (VFD control)
- [ ] ESP-NOW Bridge (wireless I/O)
- [ ] Конфигурация удаленных ESP-NOW устройств через Telnet (proxy/aggregator)
- [ ] Discovery протокол для поиска ESP-NOW устройств
- [ ] Кэширование конфигураций удаленных устройств
- [ ] EtherCAT Master для веб-сервера (опционально, SOEM/TwinCAT)
- [ ] Object Dictionary для конфигурации через CoE/SDO (опционально)
- [ ] Гибридный подход: EtherCAT Master + Telnet fallback (опционально)

### Compatibility

- [ ] SOEM (Linux/Windows)
- [ ] TwinCAT 3 (Beckhoff)
- [ ] CODESYS
- [ ] LinuxCNC HAL

---

## 8. Преимущества новой архитектуры

### По сравнению с v3:

1. **Экономия памяти:**
   - Удален веб-сервер (~10KB Flash)
   - Удалена HTML страница (~30KB Flash)
   - Итого: ~40KB экономии

2. **Производительность:**
   - W5500 полностью для EtherCAT (Socket 0)
   - Telnet на отдельном сокете (Socket 1)
   - Нет конфликтов ресурсов

3. **Гибкость:**
   - Внешний веб-интерфейс на компьютере
   - Можно использовать современные технологии
   - Легко обновлять интерфейс без перепрошивки ESP32

4. **Доступность:**
   - Конфигурация доступна во время работы EtherCAT
   - Не нужно останавливать EtherCAT для настройки
   - Удаленное управление

---

**Примечание:** Это техническое задание для проекта ESP32_EtherCAT_Slave_4 с новой архитектурой на основе Telnet протокола. Для деталей реализации см. `IMPLEMENTATION_CHECKLIST.md`.
