/*
 * USB Keyboard Scanner - Чтение кодов клавиш с физической клавиатуры
 * Использует USB Host режим для чтения клавиш
 */

#ifndef USB_KEYBOARD_SCANNER_H
#define USB_KEYBOARD_SCANNER_H

#include <Arduino.h>
#include "ethercat_type.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Инициализация USB Keyboard Scanner (USB Host режим)
 * @return true если успешно
 */
boolean usb_keyboard_scanner_init(void);

/**
 * @brief Обновление USB Keyboard Scanner (вызывать в loop)
 */
void usb_keyboard_scanner_task(void);

/**
 * @brief Включить/выключить сканирование
 * @param enabled true = включить, false = выключить
 */
void usb_keyboard_scanner_set_enabled(boolean enabled);

/**
 * @brief Проверить, активно ли сканирование
 * @return true если сканирование активно
 */
boolean usb_keyboard_scanner_is_enabled(void);

/**
 * @brief Установить указатель на Telnet сервер для отправки данных о нажатых клавишах
 * @param telnet_server Указатель на Telnet сервер (может быть NULL)
 */
void usb_keyboard_scanner_set_telnet_server(void* telnet_server);

#ifdef __cplusplus
}
#endif

#endif // CONFIG_IDF_TARGET_ESP32S3

#endif // USB_KEYBOARD_SCANNER_H
