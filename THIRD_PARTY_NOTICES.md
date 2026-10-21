# Сторонние компоненты и уведомления

Настоящий свиток — для честного писарского отчёта о том, какими чужими
трудами проект пользуется, и на каких условиях.

## 1. Собственная реализация протокола EtherCAT

Вся реализация протокола EtherCAT в сем проекте (`src/ethercat_esc.*`,
`ethercat_frame.*`, `ethercat_od.*`, `ethercat_dc.*`, `ethercat_stack.*`,
`ethercat_fsm.*`, `ethercat_memory.*`, `ethercat_type.h`) **написана
самостоятельно** по открытой спецификации ETG (IEC 61158 / IEC 61784) и
общедоступным описаниям форматов кадров.

В шапках сих свитков указано, что регистры и форматы **сверялись** с открытыми
эталонными реализациями — SOEM (`ec_type.h`, `ec_main.c`, `ec_coe.c`),
IgH EtherCAT Master (`fsm_coe.c`, `fsm_sii.c`, `fsm_slave_scan.c`, `mailbox.c`),
SOES (`esc.h`, `esc_coe.c`). Сие означает сверку **фактов**: адресов регистров,
кодов команд, разрядности полей. Код GPL-проектов (SOEM — GPLv2 c исключением
для линковки, IgH — GPLv2, SOES — GPLv2) **не копировался**, ибо GPL-код
налагал бы на весь проект несовместимые требования.

## 2. Сторонние библиотеки, включённые в сборку

| Библиотека | Версия | Лицензия | Правообладатель | Как используется |
|---|---|---|---|---|
| **EspUsbHost** | 1.0.2 | MIT | TANAKA Masayuki | USB-Host для чтения клавиатуры; вкоммичена в `lib/EspUsbHost` (текст лицензии — `lib/EspUsbHost/LICENSE`) |
| **ArduinoJson** | 7.x | MIT | Benoit Blanchon | Разбор и сборка JSON в Telnet JSON-RPC и ESP-NOW |
| **ESP-IDF / Arduino-ESP32** | по framework | Apache-2.0, LGPL-2.1, MIT | Espressif Systems | Базовый framework, драйверы, WiFi/USB |
| **TinyUSB** | по framework | MIT | hathach и соавторы | USB-дескрипторы и HID-стек |

## 3. Зависимости служебного инструмента (не входят в прошивку)

`tools/http-telnet-bridge` — мост HTTP→Telnet на Node.js:

| Пакет | Лицензия |
|---|---|
| express | MIT |
| socket.io | MIT |
| nodemon (только для разработки) | MIT |

Фронтенд (`public/index.html`) сторонних скриптов с внешних CDN не подтягивает:
`socket.io.js` отдаёт сам мост.

## 4. Материалы, намеренно НЕ включённые в репозиторий

- `docs/China_Devices/` — руководство пользователя стороннего изделия
  (GDK EDS-32x) в формате PDF с производными текстовыми файлами. Чужие
  авторские права: в публичный репозиторий не выкладывается (см. `.gitignore`).

## 5. О товарных знаках и сертификации

1. **EtherCAT®** — зарегистрированный товарный знак Beckhoff Automation GmbH;
   технология EtherCAT разработана Beckhoff Automation, права на использование
   имени и правила маркировки ведёт EtherCAT Technology Group (ETG).
2. Сей проект — **независимая экспериментальная реализация**, не связанная с
   Beckhoff Automation GmbH и ETG, **не проходившая** испытаний на соответствие
   (Conformance Test) и **не имеющая** сертификата «EtherCAT Conformance Tested».
3. Идентификаторы в коде — **тестовые**: Vendor ID `0x00000000`,
   Product Code `0x00000001`. Для изделий, поступающих на рынок, ETG требует
   членства изготовителя и собственного Vendor ID (выдаётся бесплатно).
4. Логотипы ETG/EtherCAT в проекте не используются: их применение возможно лишь
   по правилам ETG (ETG.9001 EtherCAT Marking Rules).
