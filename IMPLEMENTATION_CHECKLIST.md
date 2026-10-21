# ESP32 EtherCAT Slave v4 - Чек-лист реализации

**Версия:** 4.1 (сохранение конфигурации в Flash, исправления Telnet соединений, очередь запросов, индикатор статуса)  
**Roadmap:** 13 недель (3 месяца)  
**Формат:** Последовательная реализация с чек-боксами

---

## 📋 Как использовать

1. Выполняй задачи **строго последовательно**
2. Отмечай ✅ после завершения каждой задачи
3. Тестируй после каждого Phase
4. Не переходи к следующему Phase без завершения предыдущего

---

## Phase 1: Core EtherCAT (3 недели)

### Week 1: Types & Frame Parser

#### День 1-2: SOEM типы данных
- [ ] Создать `src/ethercat_type.h`
  - [ ] Базовые типы (uint8, uint16, uint32, etc.)
  - [ ] EtherCAT States enum (EC_STATE_INIT, PRE_OP, SAFE_OP, OP)
  - [ ] EtherCAT Commands enum (APRD, APWR, LRD, LWR, LRW)
  - [ ] Register definitions (ECT_REG_ALCTL, ALSTAT, etc.)
  - [ ] AL Status Codes (NOERROR, INVALIDSTATE, etc.)
  - [ ] Packed structures (ec_etherheadert, ec_comt)
  - [ ] Endianness macros (htoes, etohs)
- [ ] Unit test: проверить размеры структур (sizeof)

#### День 3-4: Frame Parser основа
- [ ] Создать `src/ethercat_frame.h`
  - [ ] Class EtherCATFrame declaration
  - [ ] Methods: parse(), processCommand(), incrementWKC()
- [ ] Создать `src/ethercat_frame.cpp`
  - [ ] Реализовать isEtherCATFrame() (проверка EtherType 0x88A4)
  - [ ] Реализовать parse() (цикл по datagrams)
  - [ ] Реализовать processCommand() (switch по command)
  - [ ] Реализовать incrementWKC()
- [ ] Unit test: parse один APRD datagram
- [ ] Unit test: parse один APWR datagram

#### День 5: Multiple Datagrams
- [ ] Доработать parse() для multiple datagrams
  - [ ] Проверка EC_DATAGRAMFOLLOWS flag
  - [ ] Переход к следующему datagram
- [ ] Unit test: parse 3 datagrams в одном фрейме
- [ ] Unit test: WKC increment для каждого datagram

#### День 6-7: LRW команда (самая важная)
- [ ] Реализовать LRW (Logical Read-Write) в processCommand()
  - [ ] Read current data из memory
  - [ ] Write new data в memory
  - [ ] Return old data в datagram
- [ ] Unit test: LRW корректно swap данных
- [ ] Unit test: WKC increment после LRW

**Week 1 Done?** ✅ Frame parser парсит APRD, APWR, LRW и инкрементирует WKC

---

### Week 2: Memory Map & FSM

#### День 8-9: Memory Map структура
- [ ] Создать `src/ethercat_memory.h`
  - [ ] Class EtherCATMemory declaration
  - [ ] uint8_t memory[0x2000] (8KB)
  - [ ] Methods: read8/16/32, write8/16/32
- [ ] Создать `src/ethercat_memory.cpp`
  - [ ] Реализовать read8/16/32 (little-endian)
  - [ ] Реализовать write8/16/32
  - [ ] Инициализация defaults в конструкторе
- [ ] Unit test: read/write 8/16/32-bit values

#### День 10-11: ESC Registers
- [ ] Заполнить ESC registers в memory:
  - [ ] 0x0000: Type = 0x01
  - [ ] 0x0010: Station Address (из config)
  - [ ] 0x0120: AL Control = 0x0000
  - [ ] 0x0130: AL Status = 0x0001 (INIT)
  - [ ] 0x0134: AL Status Code = 0x0000
- [ ] Реализовать onALControlWrite() callback
- [ ] Unit test: запись в AL Control вызывает callback

#### День 12-13: FSM (State Machine)
- [ ] Создать `src/ethercat_fsm.h`
  - [ ] Class EtherCATFSM declaration
  - [ ] ec_state current_state
  - [ ] Method: transitionTo(new_state)
- [ ] Создать `src/ethercat_fsm.cpp`
  - [ ] Реализовать transitionTo() с проверкой валидности
  - [ ] Callbacks: onEnterInit(), onEnterPreOp(), onEnterSafeOp(), onEnterOperational()
- [ ] Unit test: INIT → PRE_OP
- [ ] Unit test: PRE_OP → SAFE_OP
- [ ] Unit test: SAFE_OP → OPERATIONAL
- [ ] Unit test: Invalid transition (INIT → OPERATIONAL) rejected

#### День 14: Integration Memory + FSM
- [ ] Связать EtherCATMemory::onALControlWrite() с EtherCATFSM::transitionTo()
- [ ] Обновлять AL Status register при переходе состояний
- [ ] Обновлять AL Status Code при ошибках
- [ ] Integration test: запись в AL Control меняет состояние

**Week 2 Done?** ✅ Memory map работает, FSM переключает состояния

---

### Week 3: W5500 MACRAW & Stack Integration

#### День 15-16: W5500 MACRAW driver
- [ ] Создать `src/w5500_macraw.h`
  - [ ] Class W5500MACRAW declaration
  - [ ] Methods: init(), openSocket(), available(), recv(), send()
- [ ] Создать `src/w5500_macraw.cpp`
  - [ ] Реализовать init() (SPI setup)
  - [ ] Реализовать openSocket() (Sn_MR = MACRAW mode, Socket 0)
  - [ ] Реализовать available() (check Sn_RX_RSR)
  - [ ] Реализовать recv() (read from RX buffer)
  - [ ] Реализовать send() (write to TX buffer)
- [ ] Unit test: W5500 init успешен
- [ ] Unit test: Socket 0 открывается в MACRAW mode

#### День 17-18: EtherCAT Stack Core
- [ ] Создать `src/ethercat_stack.h`
  - [ ] Class EtherCATStack declaration
  - [ ] Members: w5500_in, w5500_out, memory, fsm
  - [ ] Methods: init(), start(), stop(), process()
- [ ] Создать `src/ethercat_stack.cpp`
  - [ ] Реализовать init() (инициализация всех модулей)
  - [ ] Реализовать start() (создать FreeRTOS tasks)
  - [ ] Реализовать rxTask() (прием фреймов)
  - [ ] Реализовать txTask() (отправка ответов)
- [ ] Поместить rxTask() и txTask() в IRAM (IRAM_ATTR)

#### День 19: Main.cpp интеграция
- [ ] Обновить `src/main.cpp`:
  - [ ] Добавить MODE_SELECT (GPIO 12)
  - [ ] OPERATIONAL mode: EtherCATStack (Socket 0, MACRAW)
- [ ] Compile test: проект компилируется без ошибок
- [ ] Flash test: прошивка загружается на ESP32

#### День 20-21: SOEM Testing
- [ ] Установить SOEM на Linux PC
- [ ] Запустить `slaveinfo eth0`
- [ ] Проверить: ESP32 обнаруживается как Slave
- [ ] Проверить: Type = 0x01
- [ ] Проверить: Station Address корректный
- [ ] Запустить `simple_ng eth0`
- [ ] Проверить: FSM переходы (INIT → PRE_OP → SAFE_OP → OP)
- [ ] Проверить: WKC корректный

**Week 3 Done?** ✅ SOEM обнаруживает ESP32 Slave и переводит в OPERATIONAL

---

## Phase 2: GPIO & PDO (2 недели)

### Week 4: GPIO Manager

#### День 22-23: GPIO Manager структура
- [ ] Создать `src/gpio_manager.h`
  - [ ] Class GPIOManager declaration
  - [ ] Arrays: io_channel_t channels[32], encoder_config_t encoders[8]
  - [ ] Methods: init(), updateOutputs(), updateInputs()
- [ ] Создать `src/gpio_manager.cpp`
  - [ ] Реализовать init() (загрузка конфигурации из config_storage)
  - [ ] Инициализация GPIO pins (pinMode)

#### День 24-25: Digital I/O
- [ ] Реализовать updateOutputs() для digital outputs:
  - [ ] Извлечь бит из pdo_byte/pdo_bit
  - [ ] Применить invert если нужно
  - [ ] digitalWrite()
- [ ] Реализовать updateInputs() для digital inputs:
  - [ ] digitalRead()
  - [ ] Применить invert
  - [ ] Записать бит в pdo_byte/pdo_bit
- [ ] Unit test: digital output включается/выключается
- [ ] Unit test: digital input читается корректно

#### День 26-27: Analog I/O
- [ ] Реализовать updateOutputs() для analog outputs:
  - [ ] Извлечь 16-bit значение из PDO
  - [ ] dacWrite() (0-255)
- [ ] Реализовать updateInputs() для analog inputs:
  - [ ] analogRead() (12-bit)
  - [ ] Записать 16-bit значение в PDO
- [ ] Unit test: analog output DAC работает
- [ ] Unit test: analog input ADC читается

#### День 28: Encoders
- [ ] Добавить encoder handling:
  - [ ] attachInterrupt() на pin_a и pin_b
  - [ ] Increment/decrement counter в ISR
  - [ ] Записать int32_t counter в PDO

#### День 28b: Сохранение конфигурации в Flash (v4.1)
- [x] Интеграция ESP32 Preferences API:
  - [x] Добавить `#include <Preferences.h>` в `gpio_manager.cpp`
  - [x] Объявить `static Preferences g_preferences` и namespace `"gpio_cfg"`
- [x] Реализовать `gpio_manager_save_to_flash()`:
  - [x] Сохранение количества каналов и энкодеров
  - [x] Сохранение массивов каналов и энкодеров (бинарные данные)
  - [x] Сохранение конфигурации MCP23S17
  - [x] Автоматический вызов после каждого изменения конфигурации
- [x] Реализовать `gpio_manager_load_from_flash()`:
  - [x] Загрузка количества каналов и энкодеров
  - [x] Загрузка массивов каналов и энкодеров
  - [x] Восстановление конфигурации MCP23S17
  - [x] Применение конфигурации к аппаратуре (pinMode, ISR, MCP23S17 init)
- [x] Интеграция в `gpio_manager_init()`:
  - [x] Вызов `gpio_manager_load_from_flash()` после инициализации
- [x] Автоматическое сохранение при изменениях:
  - [x] `gpio_manager_set_channel()` → `gpio_manager_save_to_flash()`
  - [x] `gpio_manager_delete_channel()` → `gpio_manager_save_to_flash()`
  - [x] `gpio_manager_add_encoder()` → `gpio_manager_save_to_flash()`
  - [x] `gpio_manager_set_encoder()` → `gpio_manager_save_to_flash()`
  - [x] `gpio_manager_delete_encoder()` → `gpio_manager_save_to_flash()`
  - [x] `gpio_manager_init_mcp23s17()` → `gpio_manager_save_to_flash()`
- [x] Telnet команда `save_config`:
  - [x] Обработчик в `telnet_server.cpp`
  - [x] Вызов `gpio_manager_save_to_flash()`
- [x] Тестирование:
  - [x] Создание конфигурации → перезагрузка → проверка сохранения
- [ ] Unit test: encoder считает импульсы
- [ ] Unit test: encoder counter в PDO корректный

**Week 4 Done?** ✅ GPIO Manager управляет I/O через PDO

---

### Week 5: PDO Integration

#### День 29-30: PDO Memory Areas
- [ ] Выделить в EtherCATMemory:
  - [ ] 0x1000-0x10FF: PDO Output (Master → Slave, 256 bytes)
  - [ ] 0x1100-0x11FF: PDO Input (Slave → Master, 256 bytes)
- [ ] Добавить в EtherCATStack:
  - [ ] updateInputPDO() вызывает gpio.updateInputs()
  - [ ] applyOutputPDO() вызывает gpio.updateOutputs()
- [ ] Вызывать updateInputPDO()/applyOutputPDO() в rxTask()

#### День 31-32: Latency Optimization
- [ ] Оптимизировать rxTask():
  - [ ] Использовать DMA для SPI
  - [ ] Минимизировать memcpy
  - [ ] Избегать Serial.print в hot path
- [ ] Измерить latency (GPIO toggle → PDO → GPIO toggle):
  - [ ] Target: < 100μs
  - [ ] Critical: < 200μs
- [ ] Оптимизация если latency > 100μs

#### День 33-34: PDO Testing с SOEM
- [ ] Создать test program на PC:
  - [ ] Master пишет pattern в PDO Output
  - [ ] Master читает PDO Input
  - [ ] Проверяет loopback
- [ ] Test: digital outputs включаются/выключаются
- [ ] Test: digital inputs читаются корректно
- [ ] Test: analog I/O работает
- [ ] Test: encoders считают
- [ ] Test: latency < 100μs

#### День 35: Performance Metrics
- [ ] Измерить CPU load (< 70%)
- [ ] Измерить heap usage (< 100KB)
- [ ] Измерить jitter (< 10μs)
- [ ] Документировать результаты

**Week 5 Done?** ✅ PDO обмен работает с latency < 100μs

---

## Phase 3: Telnet Configuration (1 неделя) 🆕

### Week 6: Telnet Server на ESP32

#### День 35b: W5500 TCP исправления (v4.1)
- [x] Исправления обработки TCP соединений:
  - [x] Добавить определения состояний: `W5500_Sn_SR_SYN_RECV`, `W5500_Sn_CR_ACCEPT`
  - [x] Обработка состояния `SYN_RECV` с командой `ACCEPT` в `w5500_tcp_accept()`
  - [x] Автоматический возврат в `LISTEN` после `CLOSE_WAIT`/`CLOSED`
  - [x] Проверка статуса `ESTABLISHED` перед отправкой в `w5500_tcp_send()`
  - [x] Повторные попытки отправки (до 10 попыток с задержкой 1ms)
  - [x] Задержка после команды `RECV` для стабилизации
- [x] Unit test: TCP соединения устанавливаются корректно
- [x] Unit test: Автоматическое восстановление после разрыва соединения

#### День 36: Network Configuration (статические IP адреса)
- [ ] Создать `src/network_config.h`:
  - [ ] Структура `network_config_t` (ip, subnet, gateway, next_device_ip)
  - [ ] Структура `nvs_network_config_t` для NVS
  - [ ] API функции (init, save, load, validate)
- [ ] Создать `src/network_config.cpp`:
  - [ ] Реализовать `initialize_network_config()`:
    - [ ] Попытка загрузки из NVS
    - [ ] Автоматическое назначение на основе Station Address
    - [ ] Адрес по умолчанию (192.168.20.1)
    - [ ] Вычисление next_device_ip
  - [ ] Реализовать `config_storage_save_network_config()`
  - [ ] Реализовать `config_storage_load_network_config()`
  - [ ] Реализовать `validate_ip_address()`
  - [ ] Реализовать `calculate_next_device_ip()`
- [ ] Unit test: автоматическое назначение IP на основе Station Address
- [ ] Unit test: сохранение/загрузка из NVS
- [ ] Unit test: валидация IP адресов

#### День 37-38: Telnet Server Core
- [x] Создать `src/telnet_server.h`:
  - [x] Структура `telnet_server_t`
  - [x] API функции (init, process, send_response, send_error)
- [x] Создать `src/telnet_server.cpp`:
  - [x] Инициализация Telnet сервера
  - [x] Использование Socket 1 W5500 (TCP mode, порт 23)
  - [x] Инициализация с назначенным IP адресом из network_config
  - [x] Обработка входящих соединений
  - [x] Чтение команд (JSON-RPC)
  - [x] Отправка ответов
- [x] Исправления TCP соединений (v4.1):
  - [x] Обработка состояния `SYN_RECV` с командой `ACCEPT`
  - [x] Автоматический возврат в `LISTEN` после `CLOSE_WAIT`/`CLOSED`
  - [x] Проверка статуса `ESTABLISHED` перед отправкой данных
  - [x] Повторные попытки при временных ошибках отправки
- [x] Unit test: Telnet сервер принимает соединения

#### День 39-40: JSON-RPC Parser
- [x] Реализовать JSON-RPC парсинг:
  - [x] Парсинг команды (method, id, params)
  - [x] Формирование ответа (id, success, result/error)
  - [x] Обработка ошибок парсинга
- [x] Интеграция с API handlers:
  - [x] Вызов существующих API handlers
  - [x] Преобразование результатов в JSON
- [x] Улучшения (v4.1):
  - [x] Поддержка `int64_t` для ID запросов
  - [x] Улучшенная обработка ID (fallback на 1)
  - [x] Детальное логирование отправки ответов
  - [x] Проверка успешности отправки через `send_json_doc`
- [x] Unit test: парсинг JSON-RPC команд
- [x] Unit test: формирование JSON-RPC ответов

#### День 41-42: API Handlers Integration
- [ ] Создать `src/api_handlers.h/.cpp`:
  - [ ] Вынести API handlers из web_server.cpp
  - [ ] Универсальные функции (не зависят от HTTP)
  - [ ] Поддержка JSON-RPC протокола
- [ ] Интеграция с Telnet сервером:
  - [ ] Вызов handlers из Telnet сервера
  - [ ] Обработка всех методов (get_config, set_config, etc.)
- [ ] Unit test: все API методы работают через Telnet

#### День 43: Network API Handlers
- [ ] Добавить методы в Telnet API:
  - [ ] `get_ip_address` - Получить текущий IP
  - [ ] `set_ip_address` - Установить IP адрес
  - [ ] `get_next_device_ip` - Получить IP следующего устройства
  - [ ] `set_next_device_ip` - Установить IP следующего устройства
  - [ ] `auto_assign_ip` - Автоматическое назначение IP
  - [ ] `reset_network_config` - Сброс сетевых настроек
- [ ] Интеграция с `network_config.cpp`:
  - [ ] Вызов функций назначения IP
  - [ ] Валидация параметров
  - [ ] Сохранение в NVS после изменения
- [ ] Unit test: все сетевые API методы работают через Telnet

#### День 44: Daisy Chain Support (опционально)
- [ ] Создать `src/telnet_daisy_chain.h/.cpp`:
  - [ ] Структура `telnet_daisy_chain_t` (tcp_in, tcp_out, next_device_ip)
  - [ ] Функция `telnet_daisy_chain_process()`:
    - [ ] Обработка входящего соединения на W5500 IN (Socket 1)
    - [ ] Подключение к следующему устройству на W5500 OUT (Socket 1)
    - [ ] Пересылка команд дальше по цепочке
    - [ ] Обработка команд для текущего устройства
- [ ] Интеграция в `ethercat_stack`:
  - [ ] FreeRTOS task для обработки daisy chain
  - [ ] Параллельная работа с EtherCAT
- [ ] Unit test: пересылка команд по цепочке

#### День 45: Parallel Operation Testing
- [x] Тест параллельной работы:
  - [x] Socket 0: EtherCAT (MACRAW) работает
  - [x] Socket 1: Telnet (TCP) работает одновременно
  - [x] Нет конфликтов ресурсов
- [x] Performance test:
  - [x] Latency EtherCAT не изменилась
  - [x] CPU load приемлемый
  - [x] Heap usage стабилен
  - [x] **Сервопериод 4 мс: 100% гарантия доставки (0 потерь из 7,493 фреймов)**
  - [x] **Сервопериод 1 мс: 99.63% гарантия доставки (46 потерь из 12,259 фреймов)**
- [x] Integration test: Telnet конфигурация во время работы EtherCAT
- [x] Документирование результатов тестирования производительности

**Week 6 Done?** ✅ Telnet сервер работает параллельно с EtherCAT

---

## Phase 4: External Web Interface (1 неделя) 🆕

### Week 7: HTTP-to-Telnet Bridge

#### День 43-44: Telnet Client Library
- [x] Создать `tools/http-telnet-bridge/bridge.js`:
  - [x] Telnet клиент (Node.js net.Socket)
  - [x] Методы: connect(), sendCommand()
  - [x] JSON-RPC протокол
  - [x] Обработка ошибок и таймаутов
  - [x] WebSocket для real-time обновлений
- [x] Unit test: Telnet клиент подключается к ESP32
- [x] Unit test: Telnet клиент отправляет команды

#### День 45-46: HTTP-to-Telnet Bridge
- [x] Создать `tools/http-telnet-bridge/`:
  - [x] `bridge.js` (Node.js + Express)
  - [x] HTTP сервер (localhost:3000)
  - [x] Telnet клиент (ESP32:23)
  - [x] Прокси для API запросов
  - [x] Преобразование HTTP → Telnet → HTTP
  - [x] WebSocket для real-time обновлений
- [x] Очередь запросов (v4.1):
  - [x] Реализовать очередь запросов (`requestQueue`, `isProcessingQueue`)
  - [x] Последовательное выполнение запросов с задержкой 150ms
  - [x] Предотвращение конфликтов с одним сокетом W5500
- [x] Улучшенный парсинг JSON (v4.1):
  - [x] Поиск полного JSON объекта по балансу скобок
  - [x] Обработка накопленных данных от предыдущих соединений
  - [x] Автоматическая очистка буфера при превышении размера
- [x] Индикатор статуса Telnet (v4.1):
  - [x] Периодическая проверка соединения каждые 5 секунд
  - [x] Автоматическое обновление при смене устройства
  - [x] Отображение статуса в веб-интерфейсе
  - [x] Emit событий `telnet_status` через Socket.IO
  - [ ] **Device Profiles модуль:**
    - [ ] `device-profiles.js` - профили устройств
    - [ ] `device-registry.js` - реестр устройств
    - [ ] `configuration-builder.js` - построитель конфигураций
    - [ ] `sync-manager.js` - менеджер синхронизации
- [x] API endpoints:
  - [x] GET /api/stats → telnet test stats
  - [x] POST /api/test/start → telnet test start
  - [x] POST /api/test/stop → telnet test stop
  - [x] Multi-device:
    - [x] GET /api/devices
    - [x] POST /api/devices/active
    - [x] POST /api/devices/scan
  - [x] Device API (`/api/devices/:ip/*`):
    - [x] GET /api/devices/:ip/system/info → telnet get_system_info
    - [x] GET /api/devices/:ip/network/config → telnet get_network_config
    - [x] POST /api/devices/:ip/network/next → telnet set_next_device_ip
    - [x] GPIO channels:
      - [x] GET /api/devices/:ip/gpio/channels → telnet get_gpio_channels
      - [x] POST /api/devices/:ip/gpio/channel → telnet set_gpio_channel
      - [x] POST /api/devices/:ip/gpio/channel/delete → telnet delete_gpio_channel
    - [x] Encoders:
      - [x] GET /api/devices/:ip/encoders → telnet get_encoders
      - [x] POST /api/devices/:ip/encoder → telnet set_encoder
      - [x] POST /api/devices/:ip/encoder/delete → telnet delete_encoder
      - [x] POST /api/devices/:ip/encoder/reset → telnet reset_encoder
    - [x] StepGen:
      - [x] GET /api/devices/:ip/stepgen/config → telnet get_stepgen_config
      - [x] POST /api/devices/:ip/stepgen/config → telnet set_stepgen_config
- [x] Unit test: Bridge преобразует HTTP в Telnet

#### День 47-48: Device Profiles & Configuration Builder (Новая архитектура)
- [ ] Создать `tools/http-telnet-bridge/device-profiles.js`:
  - [ ] Структура DeviceProfile (chip, variant, pins, peripherals, constraints)
  - [ ] Профили для ESP32-S3, ESP32, ESP32-C3
  - [ ] Карты пинов для каждого профиля (GPIO, SPI, W5500, USB, CAN, MCP23S17)
  - [ ] Информация о периферии (USB, WiFi, Bluetooth, Ethernet, CAN, IO Expansion)
  - [ ] Ограничения (max GPIO channels, encoders, stepgen axes, etc.)
- [ ] Создать `tools/http-telnet-bridge/device-registry.js`:
  - [ ] Класс DeviceRegistry для хранения информации о устройствах
  - [ ] Связь IP адреса с профилем устройства
  - [ ] Автоматическое определение типа устройства при подключении
  - [ ] Хранение системной информации и статуса синхронизации
- [ ] Создать `tools/http-telnet-bridge/configuration-builder.js`:
  - [ ] Класс ConfigurationBuilder для построения конфигураций
  - [ ] Валидация пинов против профиля устройства
  - [ ] Проверка конфликтов пинов
  - [ ] Проверка ограничений устройства
  - [ ] Генерация конфигурации
- [ ] Интеграция в `bridge.js`:
  - [ ] Инициализация DeviceProfiles, DeviceRegistry, ConfigurationBuilder
  - [ ] API endpoints для работы с профилями
  - [ ] API endpoints для создания и валидации конфигураций
- [ ] Unit test: Device Profiles загружаются корректно
- [ ] Unit test: Валидация пинов работает
- [ ] Unit test: Configuration Builder создает валидные конфигурации

#### День 49-50: External Web Interface
- [x] Создать `tools/http-telnet-bridge/public/index.html`:
  - [x] Базовый веб-интерфейс с Socket.IO
  - [x] Real-time обновление статистики
  - [x] Отображение системной информации
  - [x] Отображение сетевой конфигурации
  - [x] Управление тестами (start/stop)
  - [x] API calls через HTTP-to-Telnet bridge
- [x] Функциональность (базовая):
  - [x] Статистика EtherCAT (State, RX, TX, Dropped)
  - [x] Статистика тестов (Sent, Received, Dropped, Latency)
  - [x] Системная информация (Chip, CPU, Heap, Uptime)
  - [x] Сетевая конфигурация (IP, Subnet, Gateway)
- [x] Multi-device: выбор активного устройства + scan chain (Telnet)
- [x] Настройка IO каналов (GPIO_ESP/GPIO_EX/USB_KEY) через Telnet API (без NVS)
  - [x] Упрощенный интерфейс с динамическим скрытием полей
  - [x] Чекбоксы вместо текстовых полей (Enabled, Invert)
  - [x] Выпадающий список для выбора существующих каналов
  - [x] Для USB Keyboard: выпадающий список доступных кнопок с понятными названиями
  - [x] Чекбоксы для модификаторов USB (Ctrl, Shift, Alt, Win/GUI)
  - [x] Автоматическое заполнение полей при выборе канала
  - [x] Динамическое скрытие нерелевантных полей в зависимости от категории
  - [x] **Двухэтапное сохранение каналов:**
    - [x] Локальное хранилище на сервере bridge.js (Map `localChannels`)
    - [x] API для сохранения в локальное хранилище (`POST /api/devices/:ip/gpio/channel`)
    - [x] API для получения локальных каналов (`GET /api/devices/:ip/gpio/channels/local`)
    - [x] API для синхронизации с ESP32 (`POST /api/devices/:ip/gpio/channels/sync`)
    - [x] API для удаления из локального хранилища (`POST /api/devices/:ip/gpio/channel/delete/local`)
    - [x] Визуальная таблица каналов с данными из локального хранилища
    - [x] Кнопки "Загрузить локальные каналы", "Загрузить с ESP32", "💾 Сохранить на ESP32"
    - [x] Информационное сообщение о режиме работы
  - [ ] **Device Profiles & Configuration Builder (новая архитектура):**
    - [ ] Выбор типа ESP32 в веб-интерфейсе (ESP32-S3, ESP32, ESP32-C3)
    - [ ] Автозаполнение доступных пинов из профиля устройства
    - [ ] Валидация пинов в реальном времени (подсветка недоступных)
    - [ ] Предупреждения о конфликтах пинов
    - [ ] Адаптация интерфейса под возможности устройства (скрытие недоступных функций)
    - [ ] Автоматическое определение типа устройства при подключении
    - [ ] Сравнение профиля с реальным устройством перед синхронизацией
- [x] Настройка энкодеров через Telnet API (без NVS)
- [x] Настройка StepGen (осевые параметры, Kp/deadband, timing)
  - [ ] Настройка IO Expansion (TODO)
- [ ] Настройка ESP-NOW (Discovery/Proxy/Aggregator) (TODO)
  - [ ] Настройка EtherCAT устройства (TODO)
- [x] Вкладка "Маппинг":
  - [x] Сводная таблица PDO маппинга всех устройств (IO/Encoders/StepGen)
  - [x] Валидация overlaps/ошибок (byte/bit conflicts, size checks)
  - [x] Экспорт LinuxCNC HAL (`.hal` файл)
  - [x] Экспорт TwinCAT ESI (`.esi.xml` файл)
  - [x] Экспорт Mapping CSV (для документации)
- [x] Импорт конфигурации (System tab):
  - [x] Загрузка JSON файла конфигурации
  - [x] Применение ко всем устройствам (GPIO/Encoders/StepGen/Network)
  - [x] Отображение прогресса и результатов
- [ ] Экспорт конфигураций (JSON) + NVS persistence (TODO)
- [x] Integration test: веб-интерфейс работает через Bridge
- [x] Индикатор статуса Telnet соединения (v4.1):
  - [x] Функция `updateConnectionStatus()` для управления UI элементами
  - [x] Функция `checkConnectionStatus()` для периодической проверки (каждые 5 секунд)
  - [x] Автоматический запуск проверки при выборе устройства
  - [x] Обработка ошибок соединения с обновлением статуса
  - [x] Очистка интервала при отключении Socket.IO

#### День 49: EtherCAT Master Support (опционально)
- [ ] Исследовать возможность использования EtherCAT Master:
  - [ ] SOEM для Linux
  - [ ] TwinCAT ADS для Windows
  - [ ] Создать Node.js/Python bindings
- [ ] Реализовать Object Dictionary в ESP32:
  - [ ] Добавить Custom Objects для конфигурации (0x2000-0x2004)
  - [ ] Реализовать SDO Upload/Download для всех объектов
  - [ ] Синхронизация с NVS
- [ ] Создать гибридный Configuration API:
  - [ ] Попытка использования EtherCAT Master (если доступен)
  - [ ] Fallback на Telnet (если Master недоступен)
  - [ ] Автоматическое переключение
- [ ] Unit test: EtherCAT Master доступ работает (если доступен)
- [ ] Unit test: Fallback на Telnet работает

#### День 50: Documentation
- [ ] Создать `tools/README.md`:
  - [ ] Инструкция по установке (Node.js/Python)
  - [ ] Инструкция по запуску Bridge
  - [ ] Инструкция по использованию веб-интерфейса
  - [ ] Troubleshooting
- [ ] Создать примеры конфигураций

**Week 7 Done?** ✅ Внешний веб-интерфейс работает через HTTP-to-Telnet bridge

---

## Phase 5: Advanced Features (3 недели)

### Week 8: USB HID Keyboard & IO Expansion

#### День 51-52: USB HID Keyboard
- [x] Создать `src/usb_keyboard.h/.cpp`:
  - [x] Инициализация USB HID Keyboard
  - [x] Поддержка основных клавиш
  - [x] Edge detection для триггеров (структура готова)
- [x] Интеграция в GPIO Manager:
  - [x] Обработка USB Keyboard каналов (edge detection)
  - [x] Управление через PDO
  - [x] Модуль gpio_manager создан
- [x] Тест USB клавиатуры:
  - [x] Функция `gpio_manager_test_usb_keyboard()` в GPIO Manager
  - [x] Telnet JSON-RPC метод `test_usb_keyboard`
  - [x] Backend endpoint `/api/devices/:ip/test/usb-keyboard`
  - [x] UI для тестирования на вкладке System (key code, modifiers, duration)
  - [ ] Unit test: автоматическая проверка USB HID событий (требует дополнительного ПО на ПК)
- [x] Health Check USB клавиатуры (санитизация):
  - [x] Функция `gpio_manager_validate_usb_keyboard_channels()` в GPIO Manager
  - [x] Telnet JSON-RPC метод `validate_usb_keyboard`
  - [x] Backend endpoint `/api/devices/:ip/validate/usb-keyboard`
  - [x] UI для health check на вкладке System (проверка конфигурации + опциональный тест)
  - [x] Автоматическая инициализация USB Keyboard при старте GPIO Manager
  - [x] Функция `usb_keyboard_is_available()` для проверки USB окружения
  - [x] Функция `usb_keyboard_test_connection(dp_pin, dm_pin)` для тестирования подключения USB через пины
    - [x] Проверка пинов D+ (GPIO 21) и D- (GPIO 19) на ESP32-S3
    - [x] Вывод детальной информации в Serial Monitor
  - [x] Вывод статуса USB окружения в Serial Monitor при инициализации
  - [x] Вывод списка всех настроенных USB keyboard каналов при старте
  - [ ] Опционально: автоматический health check при старте (если включен в конфигурации)

#### День 53-55: IO Expansion (MCP23S17)
- [x] Конфигурация двух SPI шин:
  - [x] SPI Bus 1: EtherCAT (W5500) - уже используется
  - [x] SPI Bus 2: Периферия (MCP23S17, CAN) - функция mcp23s17_spi2_init() готова
- [x] Создать `src/mcp23s17.h/.cpp`:
  - [x] Драйвер MCP23S17
  - [x] Настройка портов (направление, pull-up, инверсия)
  - [x] Чтение/запись портов
  - [x] Edge detection для триггеров
- [x] Интеграция в GPIO Manager:
  - [x] Консолидированный тип GPIO_TYPE_MCP23S17
  - [x] Обработка каналов MCP23S17 (вход/выход через настройки порта)
  - [x] Модуль gpio_manager создан
- [ ] Unit test: MCP23S17 работает

#### День 56: Configuration API для IO Expansion
- [x] Добавить методы в Telnet API:
  - [x] get_io_expansion_config (реализовано)
  - [ ] set_io_expansion_config (заглушка, требуется парсинг JSON-RPC)
- [x] Добавить методы в HTTP-to-Telnet Bridge:
  - [x] GET /api/io-expansion/config
  - [x] POST /api/io-expansion/config
- [x] Обновить веб-интерфейс:
  - [x] Секция "IO Expansion (MCP23S17)" добавлена
  - [x] Отображение конфигурации (enabled, cs_pin, direction, pullup, invert, state)
  - [ ] Настройка портов MCP23S17 (требуется форма редактирования)
- [ ] Unit test: конфигурация IO Expansion работает
- [x] GPIO Manager создан с поддержкой USB Keyboard и MCP23S17

**Week 8 Done?** ✅ USB Keyboard и IO Expansion работают

#### День 56b-56c: StepGen (LinuxCNC STEP/DIR) 🆕
- [x] Создать `src/stepgen.h/.cpp`:
  - [x] PDO mapping v2 (8 bytes/axis)
  - [x] VELOCITY/POSITION режимы (POSITION: Kp + deadband)
  - [x] Accel limiting + dir_setup_us
  - [x] Backend: MCPWM (основной), LEDC fallback
- [x] Интеграция в EtherCAT Stack:
  - [x] `stepgen_process_pdo()` вызывается в RX path
- [x] Telnet JSON-RPC API:
  - [x] get_stepgen_config / set_stepgen_config
- [x] External Web UI:
  - [x] вкладка StepGen + формы параметров

---

### Week 9: CAN Bridge & ESP-NOW

#### День 57-58: CAN Bridge
- [x] Создать `src/mcp2515.h/.cpp`:
  - [x] Драйвер MCP2515
  - [x] CAN TX/RX
  - [x] Использует SPI Bus 2 (общий с MCP23S17)
- [ ] Интеграция в EtherCAT Stack:
  - [ ] PDO → CAN mapping
  - [ ] CAN → PDO mapping
- [ ] Unit test: CAN Bridge работает

#### День 59-61: ESP-NOW Bridge
- [x] Создать `src/espnow_bridge.h/.cpp`:
  - [x] Инициализация ESP-NOW
  - [x] Encryption (PMK, LMK) — базовая поддержка (ключи/флаги)
  - [x] Peer management
  - [x] PDO forwarding + fragmentation/reassembly
- [x] Интеграция в EtherCAT Stack:
  - [x] PDO forwarding через ESP-NOW (rate-limited interval)
- [ ] Unit test: ESP-NOW Bridge работает (радио стенд)

#### День 62: ESP-NOW Configuration Proxy (Гибридный подход)
- [x] Реализовано в `src/espnow_bridge.*`:
  - [x] Типы сообщений CONFIG/DISCOVERY (+ fragmented PDO)
  - [x] Proxy send (`espnow_send_config_command`) + Discovery (`espnow_broadcast_discovery`)
  - [x] Поля состояния proxy/discovery/reassembly в `espnow_bridge_t`
- [x] Реализовано в `src/espnow_config_proxy.*`:
  - [x] Кэш конфигураций устройств + refresh/get/update/clear
- [x] Минимальная интеграция в Telnet API:
  - [x] `discover_espnow_devices`, `get_espnow_status`
  - [x] `get_all_configs`, `refresh_all_configs`
- [x] Proxy `get_config`/`set_config` с `target`/`peer_mac` (минимум)
- [ ] TODO: расширить буфер/фрагментацию для больших config_response (>256 bytes)
- [ ] Unit tests: радио стенд (discovery, proxy, cache)

#### День 63: Configuration API для Bridges (Гибридный подход)
- [x] Telnet API (минимум) добавлено:
  - [x] `discover_espnow_devices`, `get_espnow_status`
  - [x] `get_all_configs`, `refresh_all_configs`
- [x] HTTP-to-Telnet Bridge:
  - [x] Добавить endpoints для ESP-NOW (`/api/devices/:ip/espnow/*`)
- [x] Обновить веб-интерфейс:
  - [x] Секция ESP-NOW (Discovery/Status/Configs) в System tab (accordion)
  - [x] Кнопки: Discover / Refresh cache
  - [x] Просмотр списка устройств + кэша конфигураций
- [ ] TODO: `get_espnow_config` / `set_espnow_config`
- [x] Proxy `get_config`/`set_config` с `target`/`peer_mac` (минимум)
- [ ] TODO: расширить буфер/фрагментацию для больших config_response (>256 bytes)
- [ ] Unit tests: радио стенд (Discovery + Aggregator + Proxy)

**Week 9 Done?** ✅ CAN и ESP-NOW Bridges работают

---

### Week 10: CoE & Distributed Clock

#### День 63-65: CoE (CANopen over EtherCAT)
- [ ] Создать `src/ethercat_coe.h/.cpp`:
  - [ ] Mailbox protocol
  - [ ] SDO Upload/Download
  - [ ] Object Dictionary
- [ ] Интеграция в EtherCAT Stack:
  - [ ] Обработка CoE datagrams
- [ ] Unit test: CoE работает

#### День 66-68: Distributed Clock
- [ ] Создать `src/dc_sync0.h/.cpp`:
  - [ ] DC System Time
  - [ ] SYNC0 timer
  - [ ] PDO sync with DC
- [ ] Интеграция в EtherCAT Stack:
  - [ ] DC синхронизация
- [ ] Unit test: DC работает, jitter < 10μs

#### День 69-70: ESI & HAL Generation
- [ ] Создать `src/esi_generator.h/.cpp`:
  - [ ] Генерация ESI XML
  - [ ] Генерация HAL конфигурации
- [ ] Добавить методы в Telnet API:
  - [ ] generate_esi (platform parameter)
  - [ ] generate_hal (slave_index parameter)
- [ ] Обновить веб-интерфейс:
  - [ ] Кнопки "Download ESI" и "Download HAL"
- [ ] Unit test: ESI и HAL генерируются корректно

**Week 10 Done?** ✅ CoE и DC работают

---

## Phase 6: USB Serial (опционально) 🆕

### Week 11: USB Serial API

#### День 71-72: Serial Command Handler
- [ ] Создать `src/serial_api.h/.cpp`:
  - [ ] Serial command handler
  - [ ] JSON-RPC протокол через Serial
  - [ ] Переиспользование API handlers
- [ ] Интеграция в main.cpp:
  - [ ] Обработка Serial команд в loop()
- [ ] Unit test: Serial API работает

#### День 73-74: Serial-to-HTTP Bridge
- [ ] Создать `tools/serial-http-bridge/`:
  - [ ] Serial порт клиент
  - [ ] HTTP сервер (localhost:3001)
  - [ ] Преобразование HTTP → Serial → HTTP
- [ ] Обновить веб-интерфейс:
  - [ ] Поддержка выбора интерфейса (Telnet/Serial)
- [ ] Unit test: Serial Bridge работает

#### День 75: Documentation
- [ ] Обновить документацию:
  - [ ] Инструкция по использованию USB Serial
  - [ ] Сравнение Telnet vs Serial
- [ ] Создать примеры использования

**Week 11 Done?** ✅ USB Serial API работает (опционально)

---

## Phase 7: Testing & Optimization (2 недели)

### Week 12: Stability Testing

#### День 76-77: Unit Tests
- [ ] Frame parser tests (10 tests)
- [ ] Memory map tests (15 tests)
- [ ] FSM tests (8 tests)
- [ ] GPIO Manager tests (20 tests)
- [ ] Telnet Server tests (10 tests)
- [ ] Все тесты проходят

#### День 78-79: Integration Tests
- [ ] SOEM simple_ng test
- [ ] SOEM slaveinfo test
- [ ] TwinCAT scan test
- [ ] LinuxCNC config test
- [ ] Telnet конфигурация во время работы EtherCAT
- [ ] Все тесты проходят

#### День 80-81: Performance Tests
- [ ] Latency measurement (< 100μs)
- [ ] Jitter measurement (< 10μs)
- [ ] CPU profiling (< 70%)
- [ ] Memory leak detection
- [ ] Все метрики в пределах целевых значений

#### День 82-84: 24/7 Stability
- [ ] Запустить ESP32 на 24 часа:
  - [ ] PDO обмен непрерывен
  - [ ] Telnet конфигурация доступна
  - [ ] Без memory leaks
  - [ ] Без crashes
- [ ] Мониторинг:
  - [ ] CPU load
  - [ ] Heap usage
  - [ ] Temperature
- [ ] Log analysis: нет ошибок

**Week 12 Done?** ✅ 24/7 стабильность, performance targets достигнуты

---

### Week 13: Documentation & Release

#### День 85-86: User Documentation
- [ ] Написать USER_GUIDE.md:
  - [ ] Hardware setup
  - [ ] Установка и запуск HTTP-to-Telnet bridge
  - [ ] Использование веб-интерфейса
  - [ ] Конфигурация через Telnet
  - [ ] TwinCAT integration
  - [ ] Troubleshooting
- [ ] Создать video tutorial (опционально)

#### День 87-88: Developer Documentation
- [ ] Обновить README.md
- [ ] API Reference для всех модулей
- [ ] Doxygen comments в коде
- [ ] Architecture diagrams
- [ ] Описание Telnet протокола

#### День 89-90: Release Preparation
- [ ] Git tag: v4.0.0
- [ ] GitHub Release notes
- [ ] Binary releases (.bin files)
- [ ] Example configurations
- [ ] Инструкция по миграции с v3 на v4

#### День 91: Production Ready ✅
- [ ] **Final checklist:**
  - [ ] Все Unit tests passed
  - [ ] Все Integration tests passed
  - [ ] Performance targets met
  - [ ] 24/7 stability confirmed
  - [ ] Documentation complete
  - [ ] Release published

**Week 13 Done?** ✅ Production Release v4.0.0 🎉

---

## Итоговые Критерии Успеха

### Must Have (обязательно) ✓

**EtherCAT:**
- [ ] SOEM обнаруживает Slave
- [ ] FSM переходы работают
- [ ] PDO обмен стабилен (1ms cycle)
- [ ] Latency < 100μs
- [ ] Jitter < 10μs

**I/O:**
- [ ] Digital I/O (32 каналов)
- [ ] Analog I/O (8 каналов)
- [ ] Encoders (8 штук)
- [ ] USB HID Keyboard
- [ ] IO Expansion через MCP23S17 (16 GPIO)

**Configuration:**
- [ ] Telnet сервер работает на Socket 1
- [ ] Конфигурация доступна во время работы EtherCAT
- [ ] Внешний веб-интерфейс работает
- [ ] HTTP-to-Telnet bridge функционирует
- [ ] NVS сохраняет конфигурацию
- [ ] Параллельная работа EtherCAT и Telnet без конфликтов
- [ ] Назначение статических IP адресов работает
- [ ] Автоматическое назначение IP на основе Station Address
- [ ] Сохранение/загрузка сетевой конфигурации из NVS
- [ ] Telnet команды для управления IP адресами
- [ ] Поддержка daisy chain топологии (опционально)

### Should Have (желательно) ⚠️

- [ ] USB Serial API (опционально)
- [ ] DC (Distributed Clock)
- [ ] CoE (Mailbox + SDO)
- [ ] CAN Bridge (VFD control)
- [ ] ESP-NOW Bridge (wireless I/O)
- [ ] Конфигурация удаленных ESP-NOW устройств через Telnet
- [ ] Discovery протокол для поиска ESP-NOW устройств
- [ ] Кэширование конфигураций удаленных устройств
- [ ] EtherCAT Master для веб-сервера (опционально, SOEM/TwinCAT)
- [ ] Object Dictionary для конфигурации через CoE/SDO (опционально)
- [ ] Гибридный подход: EtherCAT Master + Telnet fallback (опционально)

### Performance ✓

- [ ] Latency < 100μs
- [ ] Jitter < 10μs
- [ ] CPU load < 70%
- [ ] Heap < 100KB
- [ ] 24/7 uptime
- [ ] Параллельная работа EtherCAT и Telnet без деградации производительности

### Compatibility ✓

- [ ] SOEM (Linux/Windows)
- [ ] TwinCAT 3 (Beckhoff)
- [ ] CODESYS
- [ ] LinuxCNC HAL

---

## Прогресс Tracking

**Текущий Phase:** ___________  
**Текущая Week:** ___________  
**Завершено задач:** ___ / 250  
**Процент:** ____%  

**Дата начала:** ___________  
**Ожидаемая дата завершения:** ___________  
**Фактическая дата завершения:** ___________  

---

## Примечания

- ✅ = Задача завершена
- ⚠️ = Задача в процессе
- ❌ = Задача заблокирована
- 📝 = Требует документации
- 🐛 = Найден баг, требует исправления

**Удачи в разработке!** 🚀
