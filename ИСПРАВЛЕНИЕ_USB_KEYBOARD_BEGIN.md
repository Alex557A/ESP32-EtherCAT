# Исправление ошибки USB Keyboard: TX Semaphore is NULL

**Дата:** 2026-01-27  
**Автор:** Федя  
**Проблема:** Ошибка `SendReport(): TX Semaphore is NULL. You must call USBHID::begin() before you can send reports`

---

## 🔍 Проблема

При тестировании USB Keyboard возникала ошибка:
```
[E][USBHID.cpp:335] SendReport(): TX Semaphore is NULL. You must call USBHID::begin() before you can send reports
```

**Причина:** После создания объекта `USBHIDKeyboard` не был вызван метод `begin()`, который инициализирует внутренние семафоры и структуры для отправки HID отчетов.

---

## ✅ Решение

Добавлен вызов `keyboard->begin()` в функцию `usb_keyboard_init()` после создания объекта.

### Изменения в `src/usb_keyboard.cpp`:

**До:**
```cpp
// Создание USB HID Keyboard объекта
manager->keyboard = new USBHIDKeyboard();
if (!manager->keyboard) {
    Serial.println("[USB Keyboard] Failed to create USBHIDKeyboard");
    g_usb_initialized = false;
    return false;
}

// Инициализация структуры
manager->initialized = true;
// ...
```

**После:**
```cpp
// Создание USB HID Keyboard объекта
manager->keyboard = new USBHIDKeyboard();
if (!manager->keyboard) {
    Serial.println("[USB Keyboard] Failed to create USBHIDKeyboard");
    g_usb_initialized = false;
    return false;
}

// ВАЖНО: Вызов begin() для инициализации USBHIDKeyboard
// Без этого вызова SendReport() будет выдавать ошибку "TX Semaphore is NULL"
manager->keyboard->begin();

// Небольшая задержка для завершения инициализации
delay(50);

// Инициализация структуры
manager->initialized = true;
// ...
```

---

## 🧪 Тестирование

После исправления:

1. **Перепрошейте ESP32** с обновленным кодом
2. **Запустите тест:** `usb test` в Serial Monitor
3. **Проверьте вывод:** Ошибки "TX Semaphore is NULL" должны исчезнуть
4. **Проверьте на ПК:** Клавиши должны нажиматься в текстовом редакторе

---

## 📊 Ожидаемый результат

**После исправления тест должен показывать:**
```
[USB Test] 1/10: Тест клавиши A (0x04)... PRESSED ✓ RELEASED
[USB Test] 2/10: Тест клавиши B (0x05)... PRESSED ✓ RELEASED
...
========================================
Результаты: 10 успешно, 0 ошибок из 10 тестов
========================================
```

**Без ошибок "TX Semaphore is NULL"!**

---

## 🔧 Технические детали

### Почему нужен `begin()`?

Метод `USBHIDKeyboard::begin()` выполняет:
1. Инициализацию внутренних семафоров для синхронизации
2. Регистрацию HID дескриптора в USB стеке
3. Подготовку буферов для отправки HID отчетов

Без вызова `begin()` объект создан, но не готов к отправке отчетов, что приводит к ошибке при вызове `press()` или `releaseAll()`.

### Порядок инициализации

Правильный порядок:
1. `USB.begin()` - инициализация USB стека
2. `new USBHIDKeyboard()` - создание объекта
3. `keyboard->begin()` - инициализация HID Keyboard
4. Готово к использованию

---

## ✅ Статус

- [x] Проблема идентифицирована
- [x] Исправление реализовано
- [x] Код проверен линтером
- [ ] Требуется тестирование на реальном оборудовании

---

## 🔧 Дополнительное исправление: "not ready"

**Проблема:** После исправления появилась новая ошибка:
```
[E][USBHID.cpp:346] SendReport(): not ready
```

**Причина:** USB HID устройство еще не готово к отправке отчетов (не подключено к хосту или не распознано).

**Решение:**
1. Добавлена проверка `USB.ready()` перед отправкой отчетов в `usb_keyboard_update()`
2. Добавлено ожидание готовности USB при инициализации (до 5 секунд)
3. Улучшена функция `usb_keyboard_is_available()` - теперь проверяет `USB.ready()`

**Изменения:**
- `usb_keyboard_update()`: проверка `USB.ready()` перед отправкой
- `usb_keyboard_init()`: ожидание готовности USB с таймаутом 5 секунд
- `usb_keyboard_is_available()`: возвращает `USB.ready()` вместо просто `g_usb_initialized`

---

**Примечание:** После перепрошивки ESP32 с исправленным кодом ошибки должны исчезнуть, и USB Keyboard должен работать корректно, когда USB подключен к ПК.
