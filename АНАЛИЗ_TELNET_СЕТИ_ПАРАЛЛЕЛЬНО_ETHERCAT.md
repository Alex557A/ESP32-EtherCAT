# Анализ: Telnet сеть параллельно EtherCAT

**Дата:** 2026-01-25  
**Автор:** Федя  
**Статус:** Архитектурное решение

---

## Проблема

Нужно реализовать **Telnet сеть для конфигурации всех устройств** в EtherCAT сети, работающую **параллельно** с EtherCAT протоколом.

## Текущая архитектура (v4)

```
W5500 IN:  Socket 0 → MACRAW (EtherCAT RX)
W5500 OUT: Socket 0 → MACRAW (EtherCAT TX)
```

## Технические ограничения W5500

### Возможности W5500:

1. **W5500 поддерживает 8 сокетов (0-7)**
2. **Socket 0 может быть в MACRAW режиме**
3. **Socket 1-7 могут быть в TCP/UDP/IP режиме одновременно**
4. **MACRAW Socket 0 перехватывает ВСЕ Ethernet фреймы** (включая IP)

### Проблема:

Если Socket 0 в MACRAW режиме, он получает **все** Ethernet фреймы, включая TCP/IP пакеты для Socket 1. Нужна **фильтрация на уровне Socket 0**.

## Решения

### ✅ Вариант 0: Telnet через Daisy Chain (IN/OUT порты) - НОВЫЙ

**Архитектура:**
```
Сервер → ESP32-1 (W5500 IN, Socket 1) → ESP32-1 (W5500 OUT, Socket 1) → 
ESP32-2 (W5500 IN, Socket 1) → ESP32-2 (W5500 OUT, Socket 1) → ESP32-3 → ...
```

**Реализация:**

1. **W5500 IN (Socket 1):** TCP Server (LISTEN на порт 23)
   - Принимает Telnet соединения от предыдущего устройства или сервера
   - Обрабатывает команды для текущего ESP32

2. **W5500 OUT (Socket 1):** TCP Client (CONNECT к следующему устройству)
   - Подключается к следующему ESP32 (192.168.20.N+1)
   - Пересылает Telnet команды дальше по цепочке (Proxy/Forwarder)

**Преимущества:**
- ✅ Использует существующую физическую топологию (как EtherCAT)
- ✅ Не требует дополнительного оборудования (Ethernet Switch)
- ✅ Полная совместимость с EtherCAT daisy chain
- ✅ Последовательное прохождение команд через все устройства

**Недостатки:**
- ⚠️ Задержки накопления (latency) при прохождении через цепочку
- ⚠️ Если одно устройство отключено, цепочка разрывается
- ⚠️ Сложнее реализация (proxy/forwarder логика)

**Подробности:** См. `ИССЛЕДОВАНИЕ_TELNET_ДАЙЗИ_ЧЕЙН.md`

---

### ✅ Вариант 1: W5500 Socket 1 для Telnet (параллельно через Switch)

**Архитектура:**
```
W5500 IN:
  - Socket 0: MACRAW (EtherCAT, EtherType 0x88A4)
  - Socket 1: TCP/Telnet (порт 23, IP фреймы)

W5500 OUT:
  - Socket 0: MACRAW (EtherCAT TX)
  - Socket 1: TCP/Telnet (ответы)
```

**Реализация:**

1. **Socket 0 (MACRAW):**
   - Фильтрация по EtherType: пропускать только `0x88A4` (EtherCAT)
   - Остальные фреймы игнорировать

2. **Socket 1 (TCP/Telnet):**
   - Режим: TCP Server (LISTEN на порт 23)
   - IP адрес: `192.168.20.X` (статический, настраиваемый)
   - Принимает TCP/IP фреймы (EtherType 0x0800)

**Преимущества:**
- ✅ Работает параллельно с EtherCAT
- ✅ Использует существующий W5500 IN
- ✅ Минимальные изменения в архитектуре
- ✅ Удаленный доступ через Ethernet

**Недостатки:**
- ⚠️ Нужна фильтрация на Socket 0
- ⚠️ Два разных протокола на одном чипе

**Реализация фильтрации:**

```cpp
// В w5500_macraw_receive()
ec_etherheadert* eth_hdr = (ec_etherheadert*)frame;
uint16_t etype = etohs(eth_hdr->etype);

if (etype == ETH_P_ECAT) {
    // EtherCAT фрейм - обрабатываем
    return ethercat_frame_parse(...);
} else if (etype == 0x0800) {
    // IP фрейм - передаем в Socket 1 (Telnet)
    // НО: W5500 сам должен обработать это через Socket 1
    return 0;  // Игнорируем на Socket 0
}
```

**Проблема:** W5500 автоматически направляет IP фреймы в соответствующий Socket, но Socket 0 в MACRAW перехватывает их первым.

**Решение:** Настроить W5500 так, чтобы Socket 0 получал только EtherCAT фреймы. Это возможно через:
- Настройку фильтрации на уровне PHY (если поддерживается)
- Или обработку всех фреймов на Socket 0 и ручная маршрутизация

---

### ✅ Вариант 2: Отдельный W5500 для Telnet

**Архитектура:**
```
W5500 IN:  Socket 0 → MACRAW (EtherCAT RX)
W5500 OUT: Socket 0 → MACRAW (EtherCAT TX)
W5500 TELNET: Socket 0 → TCP/Telnet (порт 23)
```

**Требования:**
- Третий W5500 чип на отдельной SPI шине
- Дополнительные GPIO для CS и RST

**Преимущества:**
- ✅ Полная изоляция EtherCAT и Telnet
- ✅ Нет конфликтов
- ✅ Проще реализация

**Недостатки:**
- ❌ Дополнительный чип (стоимость, место на PCB)
- ❌ Дополнительная SPI шина или мультиплексирование

---

### ❌ Вариант 3: ESP32 WiFi для Telnet (НЕ РЕКОМЕНДУЕТСЯ)

**Проблема:** ESP32 WiFi и ESP-NOW используют один радио канал (2.4GHz). Они **не могут работать параллельно**.

**Архитектура:**
```
W5500 IN:  Socket 0 → MACRAW (EtherCAT RX)
W5500 OUT: Socket 0 → MACRAW (EtherCAT TX)
ESP32 WiFi: TCP/Telnet Server (порт 23) - КОНФЛИКТ с ESP-NOW!
```

**Почему не работает:**
- ❌ ESP32 WiFi и ESP-NOW используют один радио канал
- ❌ Невозможно одновременно использовать WiFi сервер и ESP-NOW bridge
- ❌ Конфликт ресурсов радио модуля

**Вывод:** Вариант с ESP32 WiFi **неприменим** для данной архитектуры.

---

### ✅ Вариант 3: USB Serial для Telnet (локально)

**Архитектура:**
```
W5500 IN:  Socket 0 → MACRAW (EtherCAT RX)
W5500 OUT: Socket 0 → MACRAW (EtherCAT TX)
ESP32 USB: Serial/Telnet Bridge (локально)
```

**Преимущества:**
- ✅ Простая реализация
- ✅ Работает параллельно
- ✅ Для локальной настройки

**Недостатки:**
- ❌ Только локальный доступ (USB кабель)
- ❌ Не подходит для удаленной настройки всех устройств

---

## Рекомендуемое решение: Вариант 1 (Socket 1)

### Архитектура сети:

```
                    ┌─────────────────┐
                    │  Внешний сервер │
                    │  (Компьютер)    │
                    │  192.168.20.100 │
                    └────────┬────────┘
                             │ TCP/IP (Telnet)
                             │
        ┌────────────────────┴────────────────────┐
        │         Ethernet Switch/Hub              │
        └────────────────────┬────────────────────┘
                             │
        ┌────────────────────┼────────────────────┐
        │                    │                    │
   ┌────▼────┐          ┌────▼────┐         ┌────▼────┐
   │ ESP32-1 │          │ ESP32-2 │         │ ESP32-N │
   │192.168. │          │192.168. │         │192.168. │
   │ 20.1    │          │ 20.2    │         │ 20.N    │
   └────┬────┘          └────┬────┘         └────┬────┘
        │                    │                    │
        │ EtherCAT (0x88A4)  │                    │
        └────────────────────┼────────────────────┘
                             │
                    ┌────────▼────────┐
                    │ EtherCAT Master │
                    └─────────────────┘
```

### Назначение адресов:

**TCP/IP адреса (для Telnet):**
- ESP32-1: `192.168.20.1`
- ESP32-2: `192.168.20.2`
- ESP32-N: `192.168.20.N`

**EtherCAT Station Address:**
- Назначается Master'ом при сканировании
- Может быть изменен через Telnet команду

### Протокол конфигурации:

**Telnet команды:**
```
SET_STATION_ADDRESS <0-65535>  - Установить EtherCAT Station Address
GET_STATION_ADDRESS            - Получить текущий Station Address
SET_IP_ADDRESS <x.x.x.x>      - Установить IP адрес (для Telnet)
GET_IP_ADDRESS                 - Получить IP адрес
CONFIGURE_GPIO <channel> ...   - Настроить GPIO канал
CONFIGURE_PDO <mapping> ...   - Настроить PDO mapping
SAVE_CONFIG                   - Сохранить конфигурацию
REBOOT                        - Перезагрузка
```

**JSON-RPC формат:**
```json
{
  "jsonrpc": "2.0",
  "method": "set_station_address",
  "params": {"address": 1},
  "id": 1
}
```

---

## Реализация (Вариант 1)

### 1. Модификация W5500 MACRAW драйвера

**Добавить фильтрацию на Socket 0:**

```cpp
// В w5500_macraw_receive()
uint16_t w5500_macraw_receive(w5500_macraw_t* w5500, uint8_t* frame, uint16_t max_length) {
    // ... существующий код ...
    
    // Фильтрация по EtherType
    ec_etherheadert* eth_hdr = (ec_etherheadert*)frame;
    uint16_t etype = etohs(eth_hdr->etype);
    
    if (etype != ETH_P_ECAT) {
        // Не EtherCAT фрейм - игнорируем на Socket 0
        // W5500 должен обработать его через другой Socket
        return 0;
    }
    
    // EtherCAT фрейм - обрабатываем
    return packet_size;
}
```

**Проблема:** W5500 в MACRAW режиме получает ВСЕ фреймы, включая IP. Нужно либо:
1. Настроить W5500 фильтрацию (если поддерживается)
2. Или обрабатывать все фреймы и маршрутизировать вручную

### 2. Создание Telnet сервера на Socket 1

**Новый модуль:** `w5500_telnet.h/cpp`

```cpp
typedef struct {
    w5500_tcp_t tcp;           // TCP соединение на Socket 1
    boolean connected;         // Флаг соединения
    uint8_t rx_buffer[1024];   // RX буфер
    uint16_t rx_len;          // Длина данных
} w5500_telnet_t;

boolean w5500_telnet_init(w5500_telnet_t* telnet, ...);
boolean w5500_telnet_listen(w5500_telnet_t* telnet, uint16_t port);
uint16_t w5500_telnet_receive(w5500_telnet_t* telnet, uint8_t* buf, uint16_t max_len);
boolean w5500_telnet_send(w5500_telnet_t* telnet, const uint8_t* buf, uint16_t len);
```

### 3. Интеграция в EtherCAT Stack

```cpp
typedef struct {
    w5500_macraw_t w5500_in;      // Socket 0: EtherCAT RX
    w5500_macraw_t w5500_out;     // Socket 0: EtherCAT TX
    w5500_telnet_t telnet;         // Socket 1: Telnet
    ethercat_memory_t memory;
    // ...
} ethercat_stack_t;
```

### 4. FreeRTOS Tasks

```cpp
// Task 1: EtherCAT RX (Socket 0)
void ethercat_rx_task(void* param) {
    while (1) {
        uint16_t len = w5500_macraw_receive(&stack->w5500_in, rx_buf, ...);
        if (len > 0) {
            ethercat_frame_parse(rx_buf, len, ...);
        }
        vTaskDelay(1);  // 1ms
    }
}

// Task 2: Telnet Server (Socket 1)
void telnet_task(void* param) {
    while (1) {
        if (w5500_telnet_accept(&stack->telnet)) {
            // Обработка Telnet команд
            uint16_t len = w5500_telnet_receive(&stack->telnet, buf, ...);
            if (len > 0) {
                telnet_process_command(&stack->telnet, buf, len);
            }
        }
        vTaskDelay(10);  // 10ms
    }
}
```

---

## Альтернативное решение: Два W5500 (Вариант 2)

Если Вариант 1 не работает из-за конфликтов Socket 0/1, можно использовать:

```
W5500 IN:  Socket 0 → MACRAW (EtherCAT RX)
W5500 OUT: Socket 0 → MACRAW (EtherCAT TX)
W5500 TELNET: Socket 0 → TCP/Telnet (отдельный чип)
```

**Требования:**
- Третий W5500 на SPI Bus 1 (мультиплексирование CS)
- Или отдельная SPI шина (если доступна)

---

## Рекомендация

### Для последовательной топологии (как EtherCAT):

**Рекомендуется Вариант 0 (Daisy Chain):**
1. Реализовать Telnet Server на W5500 IN (Socket 1)
2. Реализовать Telnet Client/Forwarder на W5500 OUT (Socket 1)
3. Создать Proxy/Forwarder логику для пересылки команд
4. Протестировать daisy chain топологию

**Преимущества:**
- Использует существующую физическую топологию
- Не требует дополнительного оборудования
- Полная совместимость с EtherCAT

### Для параллельной топологии (через Switch):

**Рекомендуется Вариант 1 (Socket 1, параллельно):**
1. Реализовать Telnet сервер на Socket 1 W5500 IN
2. Добавить фильтрацию EtherType на Socket 0
3. Протестировать параллельную работу через Ethernet Switch

**Если не работает:**
- Перейти на Вариант 2 (отдельный W5500)
- Или Вариант 3 (USB Serial для локальной настройки)

**Исключенные варианты:**
- ❌ ESP32 WiFi - **неприменим** из-за конфликта с ESP-NOW (один радио канал)

---

## Следующие шаги

1. ✅ Создать `w5500_telnet.h/cpp` модуль
2. ✅ Модифицировать `w5500_macraw.cpp` для фильтрации
3. ✅ Интегрировать Telnet в `ethercat_stack`
4. ✅ Реализовать JSON-RPC протокол
5. ✅ Создать команды для настройки Station Address и IP
6. ✅ Протестировать параллельную работу

---

**Примечание для Windows/Linux/macOS:**

Внешний веб-сервер (HTTP-to-Telnet bridge) должен:
- Подключаться к каждому ESP32 по IP адресу (192.168.20.1, 192.168.20.2, ...)
- Отправлять Telnet команды через TCP соединение
- Отображать статус всех устройств в сети

---

**Федя**  
*2026-01-25*
