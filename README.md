# ESP32 EtherCAT Slave v4

**Реализация EtherCAT Slave на ESP32-S3 с Telnet конфигурацией**

---

## 🎯 Ключевые особенности

### Новая архитектура конфигурации

- ✅ **Telnet сервер на Socket 1 W5500** - конфигурация доступна во время работы EtherCAT
- ✅ **Внешний веб-интерфейс на компьютере** - гибкость, современные технологии
- ✅ **HTTP-to-Telnet bridge** - совместимость с существующим интерфейсом
- ✅ **Опционально USB Serial** - для локальной настройки
- ❌ **Удален веб-сервер и HTML страница из ESP32** - экономия ~40KB Flash

### Преимущества

1. **W5500 полностью для EtherCAT:**
   - Socket 0: EtherCAT (MACRAW) - реальное время
   - Socket 1: Telnet (TCP) - конфигурация
   - Оба работают параллельно

2. **Минимальные ресурсы:**
   - Нет HTML страницы (~30KB экономии)
   - Простой Telnet протокол (меньше CPU)
   - Меньше буферов памяти

3. **Доступность:**
   - Конфигурация доступна во время работы EtherCAT
   - Не нужно останавливать EtherCAT для настройки
   - Удаленное управление

---

## 📋 Документация

- **TECHNICAL_SPECIFICATION.md** - Техническое задание проекта
- **IMPLEMENTATION_CHECKLIST.md** - Чек-лист реализации
- **СРАВНИТЕЛЬНЫЙ_АНАЛИЗ_ВАРИАНТОВ_КОНФИГУРАЦИИ.md** - Анализ выбора архитектуры

---

## 🚀 Быстрый старт

### Требования

**Hardware:**
- ESP32-S3-DevKitC-1
- W5500 Ethernet module x2
- USB кабель
- Jumper wire для GPIO 12 (mode select)

**Software:**
- PlatformIO
- Node.js (для HTTP-to-Telnet bridge) или Python
- SOEM на Linux PC (для тестирования)

### Сборка

```bash
cd ESP32_EtherCAT_Slave_4
pio run
```

### Прошивка

```bash
pio run -t upload
```

### Запуск HTTP-to-Telnet Bridge

```bash
cd tools/http-telnet-bridge
npm install  # или pip install -r requirements.txt
node bridge.js  # или python bridge.py
```

### Открыть веб-интерфейс

Откройте в браузере: `http://localhost:3000`

---

## 🔧 Архитектура

```
ESP32-S3:
  - Socket 0: EtherCAT (MACRAW) - реальное время
  - Socket 1: Telnet (TCP, порт 23) - конфигурация
  
Компьютер:
  - HTTP-to-Telnet Bridge (localhost:3000)
  - Веб-интерфейс (HTML/JS)
```

---

## 📊 Статус разработки

**Текущая версия:** 4.0.0 (в разработке)

**Roadmap:** 13 недель (3 месяца)

См. **IMPLEMENTATION_CHECKLIST.md** для детального прогресса.

---

## 🔄 Отличия от v3

### Удалено:
- ❌ Веб-сервер на ESP32
- ❌ HTML страница в прошивке
- ❌ HTTP парсинг на ESP32

### Добавлено:
- ✅ Telnet сервер на Socket 1
- ✅ Внешний веб-интерфейс
- ✅ HTTP-to-Telnet bridge
- ✅ Опционально USB Serial API

### Результат:
- Экономия ~40KB Flash
- Минимальная нагрузка на ESP32
- Конфигурация доступна во время работы EtherCAT

---

## 📝 Лицензия

[Указать лицензию]

---

**Автор:** Федя  
**Дата:** 2026-01-20
