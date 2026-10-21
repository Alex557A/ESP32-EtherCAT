/*
 * USB HID Keyboard Module
 * Используется TinyUSB напрямую (схема из ESP32_S3_USB_test)
 * Phase 5, Week 8, Day 51-52
 */

#ifndef USB_KEYBOARD_H
#define USB_KEYBOARD_H

#include <Arduino.h>
#include "ethercat_type.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3
#include <USB.h>
#include "tusb.h"
#endif

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Constants
// ============================================================================

// USB HID Key Codes (основные клавиши)
#define USB_KEY_A           0x04
#define USB_KEY_B           0x05
#define USB_KEY_C           0x06
#define USB_KEY_D           0x07
#define USB_KEY_E           0x08
#define USB_KEY_F           0x09
#define USB_KEY_G           0x0A
#define USB_KEY_H           0x0B
#define USB_KEY_I           0x0C
#define USB_KEY_J           0x0D
#define USB_KEY_K           0x0E
#define USB_KEY_L           0x0F
#define USB_KEY_M           0x10
#define USB_KEY_N           0x11
#define USB_KEY_O           0x12
#define USB_KEY_P           0x13
#define USB_KEY_Q           0x14
#define USB_KEY_R           0x15
#define USB_KEY_S           0x16
#define USB_KEY_T           0x17
#define USB_KEY_U           0x18
#define USB_KEY_V           0x19
#define USB_KEY_W           0x1A
#define USB_KEY_X           0x1B
#define USB_KEY_Y           0x1C
#define USB_KEY_Z           0x1D
#define USB_KEY_1           0x1E
#define USB_KEY_2           0x1F
#define USB_KEY_3           0x20
#define USB_KEY_4           0x21
#define USB_KEY_5           0x22
#define USB_KEY_6           0x23
#define USB_KEY_7           0x24
#define USB_KEY_8           0x25
#define USB_KEY_9           0x26
#define USB_KEY_0           0x27
#define USB_KEY_ENTER       0x28
#define USB_KEY_ESCAPE      0x29
#define USB_KEY_BACKSPACE   0x2A
#define USB_KEY_TAB         0x2B
#define USB_KEY_SPACE       0x2C
#define USB_KEY_MINUS       0x2D
#define USB_KEY_EQUAL       0x2E
#define USB_KEY_LEFTBRACE   0x2F
#define USB_KEY_RIGHTBRACE  0x30
#define USB_KEY_BACKSLASH   0x31
#define USB_KEY_SEMICOLON   0x33
#define USB_KEY_APOSTROPHE  0x34
#define USB_KEY_GRAVE       0x35
#define USB_KEY_COMMA       0x36
#define USB_KEY_DOT         0x37
#define USB_KEY_SLASH       0x38

// Modifiers (bitmask)
#define USB_MOD_LEFT_CTRL   0x01
#define USB_MOD_LEFT_SHIFT  0x02
#define USB_MOD_LEFT_ALT    0x04
#define USB_MOD_LEFT_GUI    0x08
#define USB_MOD_RIGHT_CTRL  0x10
#define USB_MOD_RIGHT_SHIFT 0x20
#define USB_MOD_RIGHT_ALT   0x40
#define USB_MOD_RIGHT_GUI   0x80

// ============================================================================
// USB Keyboard Manager Structure
// ============================================================================

typedef struct {
    boolean initialized;                    /**< Инициализирован? */
    boolean enabled;                        /**< Включен? */
} usb_keyboard_manager_t;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Инициализация USB Keyboard Manager
 * @param manager Указатель на USB Keyboard Manager
 * @return true если успешно
 */
boolean usb_keyboard_init(usb_keyboard_manager_t* manager);

/**
 * @brief Включить/выключить USB Keyboard
 * @param manager Указатель на USB Keyboard Manager
 * @param enabled true = включить, false = выключить
 */
void usb_keyboard_set_enabled(usb_keyboard_manager_t* manager, boolean enabled);

/**
 * @brief Нажать клавишу
 * @param manager Указатель на USB Keyboard Manager
 * @param key_code USB HID key code
 * @param modifiers Modifiers (bitmask, опционально)
 * @return true если успешно
 */
boolean usb_keyboard_press_key(usb_keyboard_manager_t* manager, 
                                uint8 key_code, 
                                uint8 modifiers);

/**
 * @brief Отпустить клавишу
 * @param manager Указатель на USB Keyboard Manager
 * @param key_code USB HID key code
 * @return true если успешно
 */
boolean usb_keyboard_release_key(usb_keyboard_manager_t* manager, uint8 key_code);

/**
 * @brief Отпустить все клавиши
 * @param manager Указатель на USB Keyboard Manager
 * @return true если успешно
 */
boolean usb_keyboard_release_all(usb_keyboard_manager_t* manager);

/**
 * @brief Проверить, нажата ли клавиша
 * @param manager Указатель на USB Keyboard Manager
 * @param key_code USB HID key code
 * @return true если клавиша активна
 */
boolean usb_keyboard_is_key_pressed(usb_keyboard_manager_t* manager, uint8 key_code);

/**
 * @brief Обновить USB HID report (вызывать периодически)
 * @param manager Указатель на USB Keyboard Manager
 */
void usb_keyboard_update(usb_keyboard_manager_t* manager);

/**
 * @brief Проверить наличие USB окружения (подключен ли USB к ПК)
 * @return true если USB окружение доступно
 */
boolean usb_keyboard_is_available(void);

/**
 * @brief Тест подключения USB через проверку пинов D+ и D-
 * @param dp_pin Пин D+ (по умолчанию 20 для ESP32-S3)
 * @param dm_pin Пин D- (по умолчанию 19 для ESP32-S3)
 * @return true если USB подключен (D+ или D- имеют напряжение)
 */
boolean usb_keyboard_test_connection(uint8 dp_pin, uint8 dm_pin);

/**
 * @brief Получить карту доступных кодов клавиш
 * @param key_map Массив для сохранения кодов (должен быть размером минимум 128)
 * @param map_size Размер массива (входной) / количество записанных кодов (выходной)
 * @return true если успешно
 */
boolean usb_keyboard_get_key_map(uint8* key_map, uint8* map_size);

/**
 * @brief Получить имя клавиши по коду
 * @param key_code USB HID key code
 * @return Указатель на строку с именем клавиши или NULL
 */
const char* usb_keyboard_get_key_name(uint8 key_code);

/**
 * @brief Начать сканирование кодов клавиш с физической клавиатуры (USB Host)
 * @param duration_ms Длительность сканирования в миллисекундах (0 = бесконечно)
 * @return true если сканирование начато
 */
boolean usb_keyboard_start_scan(uint32 duration_ms);

/**
 * @brief Остановить сканирование кодов клавиш
 */
void usb_keyboard_stop_scan(void);

/**
 * @brief Проверить, активно ли сканирование
 * @return true если сканирование активно
 */
boolean usb_keyboard_is_scanning(void);

#ifdef __cplusplus
}
#endif

#endif // USB_KEYBOARD_H
