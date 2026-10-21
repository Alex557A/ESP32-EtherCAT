# ESP32 EtherCAT Slave v4
## Модуль дискретного ввода-вывода с интерфейсом EtherCAT (ESP32-ECAT-SLAVE-4)

[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)

> ## ⚠️ Проект в разработке. Код НЕ ИСПЫТАН на реальном оборудовании
>
> - Прошивка **собирается** (PlatformIO, `esp32-s3-devkitc-1`), однако обмен с
>   настоящим ведущим устройством (SOEM / IgH EtherCAT Master / TwinCAT /
>   LinuxCNC) **не проверялся**: переходы INIT → PREOP → SAFEOP → OP, чтение
>   входов, запись выходов и SDO-чтение словаря на реальном стенде не
>   подтверждены.
> - Публикуется «как есть», ради обмена опытом и продолжения работ.
>   **Ведутся обновления** — их перечень в разделе «Дорожная карта» ниже и в
>   [CHANGELOG.md](CHANGELOG.md).
> - Никакая часть сего проекта не является гарантией работоспособности; для
>   ответственных применений требуется самостоятельная проверка.
>
> **О товарных знаках.** `EtherCAT®` — зарегистрированный товарный знак
> Beckhoff Automation GmbH, права на имя и правила маркировки ведёт EtherCAT
> Technology Group (ETG). Сей проект — **независимая экспериментальная
> реализация**, не связанная с Beckhoff Automation GmbH и ETG, **не
> сертифицированная** и не проходившая испытаний на соответствие (Conformance
> Test). Vendor ID `0x00000000` и Product Code `0x00000001` — **тестовые**;
> ETG их не выдавала. Логотипы ETG/EtherCAT в проекте не используются.

**Версия:** 0.1.0-alpha (предварительная редакция)  
**Платформа:** ESP32-S3-DevKitC-1, 2 × W5500 (MACRAW), Arduino-framework + PlatformIO  
**Автор:** [Alex557A](https://github.com/Alex557A)

---

## 1. Что это такое

Модуль дискретного ввода-вывода (DI/DO) с интерфейсом EtherCAT. Ведомое
устройство (SubDevice / слейв) реализовано полностью программно на ESP32-S3:
эмуляция регистров ESC, SII/EEPROM, FMMU, SyncManager, словарь объектов CoE,
SDO, PDO-маппинг и Distributed Clock.

Полный паспорт изделия (характеристики, схема подключения, карта регистров,
карта памяти, гарантия) — [`docs/ПАСПОРТ_ESP32_EtherCAT_Slave_v4.md`](docs/ПАСПОРТ_ESP32_EtherCAT_Slave_v4.md).

## 2. Состояние работ

### 2.1. Реализовано в коде (сборка проходит)

| Узел | Свиток | Что сделано |
|---|---|---|
| Эмуляция ESC | `src/ethercat_esc.*` | Регистры `0x0000…0x0FFF`, образ SII + интерфейс EEPROM `0x0500`, 8 FMMU, 4 SyncManager, статусы DL/AL, счётчики, проверка настроек SM |
| Кадры и датаграммы | `src/ethercat_frame.*` | APRD/APWR/FPRD/FPWR/BRD/BWR/LRD/LWR/LRW, трансляция логических адресов через FMMU, обработчик изменений памяти |
| Словарь объектов, CoE | `src/ethercat_od.*` | `0x1000`, `0x1008`–`0x100A`, `0x1018`, `0x1600`, `0x1A00`, `0x1C00`, `0x1C12/13`, `0x6000`, `0x7010`; SDO upload/download (в том числе сегментированные), Complete Access, SDO Information |
| Distributed Clock | `src/ethercat_dc.*` | Системное время, сдвиг, задержка, SYNC0/SYNC1, режимы Free-Run и DC-Synchron |
| Конечный автомат, стек | `src/ethercat_fsm.*`, `src/ethercat_stack.*` | INIT → PREOP → SAFEOP → OP, почтовый ящик, работа со SM, привязка PDO-областей |
| Telnet-конфигурация | `src/telnet_server.*` | JSON-RPC: `get_config`, `set_config`, `reset_config` (каналы, энкодеры, MCP23S17, сохранение во Flash) |
| ESI/EDS | `esi/ESP32_EtherCAT_Slave_v4.xml` | Описание для TwinCAT / SOEM / IgH |
| Дополнительно | `src/usb_keyboard*`, `mcp23s17.*`, `mcp2515.*`, `stepgen.*`, `espnow_*` | USB-HID клавиатура, расширитель GPIO, CAN, генератор STEP/DIR, радиоканал ESP-NOW |

### 2.2. Не проверено и заведомо ограничено

| Ограничение | Суть |
|---|---|
| Обмен с ведущим | Переходы состояний и обмен процессами на реальном мастере не проверялись |
| Точность DC | SYNC0 выдаётся программным таймером ESP32; допуск планировщика FreeRTOS |
| FMMU | Только побайтная трансляция; побитовые отображения не поддерживаются |
| SII/EEPROM | Запись игнорируется (интерфейс отдаёт чтение) |
| SDO Information | Ответ не фрагментируется (рассчитан на словарь в один почтовый кадр) |
| Идентификаторы | Vendor ID/Product Code — тестовые, не зарегистрированы в ETG |

## 3. Требования и сборка

**Аппаратное обеспечение**

| Компонент | Примечание |
|---|---|
| ESP32-S3-DevKitC-1 (16 МБ Flash, PSRAM) | основной модуль |
| 2 × W5500 (Ethernet, SPI) | Socket 0 — EtherCAT (MACRAW), Socket 1 — Telnet |
| MCP23S17 (SPI) | расширитель дискретных каналов |
| MCP2515 (SPI) | шина CAN (по желанию) |
| Источник 24 В | питание модуля |

**Программное обеспечение**

- [PlatformIO](https://platformio.org/) — CLI либо расширение к VS Code;
- framework Arduino для ESP32 (подтягивается сам);
- `ArduinoJson` — из реестра PlatformIO; `EspUsbHost` — из `lib/EspUsbHost`
  (вкоммичена, внешние пути не нужны).

```bash
pio run                # сборка
pio run -t upload      # прошивка
pio device monitor     # монитор 115200 бод
```

На чистом клоне никаких правок не требуется. Проверочная сборка:
`[SUCCESS]`, Flash ≈ 27 %, RAM ≈ 28 %.

## 4. Структура репозитория

```
├── src/                        исходники прошивки
│   ├── ethercat_esc.*          эмуляция регистров ESC, SII/EEPROM, FMMU, SM
│   ├── ethercat_frame.*        кадры EtherCAT и датаграммы
│   ├── ethercat_od.*           словарь объектов CoE, SDO, PDO-маппинг
│   ├── ethercat_dc.*           Distributed Clock (SYNC0/SYNC1)
│   ├── ethercat_fsm.*          конечный автомат состояний
│   ├── ethercat_stack.*        связка модулей, обработка почтового ящика
│   ├── telnet_server.*         конфигурация по Telnet (JSON-RPC)
│   ├── gpio_manager.*          каналы ввода-вывода, энкодеры
│   └── ...                     W5500, MCP23S17, MCP2515, USB, ESP-NOW, STEP/DIR
├── lib/EspUsbHost/             вендорная библиотека USB-Host (MIT, v1.0.2)
├── esi/                        ESI/EDS-описание устройства
├── docs/                       паспорт изделия
├── tools/http-telnet-bridge/   мост HTTP→Telnet и веб-интерфейс (Node.js)
├── scripts/                    вспомогательные скрипты сборки
├── platformio.ini              конфигурация сборки
├── THIRD_PARTY_NOTICES.md      уведомления о сторонних компонентах
└── CHANGELOG.md                журнал изменений
```

## 5. Документация

| Документ | О чём |
|---|---|
| [docs/ПАСПОРТ_ESP32_EtherCAT_Slave_v4.md](docs/ПАСПОРТ_ESP32_EtherCAT_Slave_v4.md) | Паспорт изделия: характеристики, разъёмы, схемы, карты регистров и памяти, гарантия |
| [esi/ESP32_EtherCAT_Slave_v4.xml](esi/ESP32_EtherCAT_Slave_v4.xml) | Описание устройства для TwinCAT / SOEM / IgH |
| [CHANGELOG.md](CHANGELOG.md) | Журнал изменений и известные ограничения |
| [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) | Сторонние компоненты, лицензии, оговорки о товарных знаках |

## 6. Дорожная карта (что в разработке)

1. Проверка обмена с реальным ведущим: SOEM / IgH EtherCAT Master / TwinCAT / LinuxCNC.
2. Побитовое отображение FMMU; запись в SII/EEPROM.
3. Фрагментация ответов сервиса SDO Information.
4. Аппаратная синхронизация SYNC0 (уход от программного таймера ESP32).
5. Обновление `lib/EspUsbHost` с 1.0.2 до 2.x (API изменился).
6. Сертификационные шаги: членство в ETG (бесплатно), собственный Vendor ID
   (выдаётся ETG), прогон Conformance Test Tool, внесение ESI в Product Guide.

## 7. Лицензия и правовые оговорки

- Код проекта — **MIT**, см. [LICENSE](LICENSE).
- Сторонние компоненты и их условия — [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md).
- `EtherCAT®` — зарегистрированный товарный знак Beckhoff Automation GmbH.
  Проект не связан с Beckhoff Automation GmbH и EtherCAT Technology Group,
  не сертифицирован и испытаний на соответствие не проходил.
  `Vendor ID 0x00000000` и `Product Code 0x00000001` — тестовые значения.
- Реализация протокола написана самостоятельно по открытой спецификации;
  код GPL-проектов (SOEM, IgH EtherCAT Master, SOES) не заимствован.

## 8. Связь

Вопросы, замечания и предложения — чрез раздел
[Issues](https://github.com/Alex557A/ESP32-EtherCAT/issues) репозитория.
Персональные контакты в публичной документации намеренно не публикуются.
