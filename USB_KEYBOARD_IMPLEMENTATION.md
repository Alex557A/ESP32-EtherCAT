# USB HID Keyboard Implementation

## Важная информация

**Официальная библиотека Arduino-ESP32 `USBHIDKeyboard` НЕ РАБОТАЕТ** на ESP32-S3 в данном проекте.

## Используемое решение

Используется **TinyUSB напрямую** (встроенный в ESP32-S3 Arduino framework) с схемой инициализации из рабочего проекта:

**Рабочий проект:** `C:\Users\Alex\Desktop\EtherCAT\ESP32_S3_USB_test`

## Схема инициализации

1. **Инициализация USB:**
   ```cpp
   USB.begin();  // Инициализирует TinyUSB Device stack
   ```

2. **Инициализация USB Keyboard Manager:**
   ```cpp
   usb_keyboard_init(&manager);
   ```

3. **Периодический вызов в loop():**
   ```cpp
   tud_task();  // TinyUSB device task
   usb_keyboard_update(&manager);  // Отправка HID reports
   ```

## Отправка клавиш

Используется функция TinyUSB:
```cpp
tud_hid_keyboard_report(report_id, modifier, keycode_array);
```

Где:
- `report_id` = 0 (стандартный keyboard report)
- `modifier` = битовая маска модификаторов (Ctrl, Shift, Alt, Win)
- `keycode_array` = массив из 6 байт с кодами клавиш

## Важные ограничения

1. **USB используется ТОЛЬКО для HID клавиатуры (USB Device)**
2. **ESP32-S3 подключен к ПК ТОЛЬКО через:**
   - COM порт (UART) - для Serial Monitor
   - W5500 (Ethernet) - для EtherCAT
3. **USB НЕ используется для подключения к ПК!**

## Файлы конфигурации

- `tusb_config.h` - конфигурация TinyUSB (HID Device enabled)
- `src/usb_descriptors.cpp` - USB descriptors и callbacks для HID keyboard

## Источник

Схема инициализации основана на рабочем проекте:
- `C:\Users\Alex\Desktop\EtherCAT\ESP32_S3_USB_test\src\main.cpp`
- Используется `USB.begin()` для инициализации TinyUSB

## Примечания

- Не использовать `USBHIDKeyboard` класс из официальной библиотеки
- Использовать TinyUSB API напрямую (`tud_hid_keyboard_report`)
- Вызывать `tud_task()` периодически в `loop()`
