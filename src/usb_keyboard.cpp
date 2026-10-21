/*
 * USB HID Keyboard Module Implementation
 * Используется TinyUSB напрямую (схема из ESP32_S3_USB_test)
 * Phase 5, Week 8, Day 51-52
 */

#include "usb_keyboard.h"
#include "ethercat_type.h"
#include <string.h>
#include <stdio.h>

#ifdef CONFIG_IDF_TARGET_ESP32S3

#include <USB.h>
#include "tusb.h"

// ============================================================================
// Implementation
// ============================================================================

// Глобальная переменная для отслеживания инициализации USB
static boolean g_usb_initialized = false;

// Текущее состояние клавиатуры
static uint8 g_keyboard_report[8] = {0};  // [modifiers, reserved, key1, key2, key3, key4, key5, key6]

// ============================================================================
// Helper Functions
// ============================================================================

const char* usb_keyboard_get_key_name(uint8 key_code) {
    switch (key_code) {
        case USB_KEY_A: return "A";
        case USB_KEY_B: return "B";
        case USB_KEY_C: return "C";
        case USB_KEY_D: return "D";
        case USB_KEY_E: return "E";
        case USB_KEY_F: return "F";
        case USB_KEY_G: return "G";
        case USB_KEY_H: return "H";
        case USB_KEY_I: return "I";
        case USB_KEY_J: return "J";
        case USB_KEY_K: return "K";
        case USB_KEY_L: return "L";
        case USB_KEY_M: return "M";
        case USB_KEY_N: return "N";
        case USB_KEY_O: return "O";
        case USB_KEY_P: return "P";
        case USB_KEY_Q: return "Q";
        case USB_KEY_R: return "R";
        case USB_KEY_S: return "S";
        case USB_KEY_T: return "T";
        case USB_KEY_U: return "U";
        case USB_KEY_V: return "V";
        case USB_KEY_W: return "W";
        case USB_KEY_X: return "X";
        case USB_KEY_Y: return "Y";
        case USB_KEY_Z: return "Z";
        case USB_KEY_1: return "1";
        case USB_KEY_2: return "2";
        case USB_KEY_3: return "3";
        case USB_KEY_4: return "4";
        case USB_KEY_5: return "5";
        case USB_KEY_6: return "6";
        case USB_KEY_7: return "7";
        case USB_KEY_8: return "8";
        case USB_KEY_9: return "9";
        case USB_KEY_0: return "0";
        case USB_KEY_ENTER: return "Enter";
        case USB_KEY_ESCAPE: return "Escape";
        case USB_KEY_BACKSPACE: return "Backspace";
        case USB_KEY_TAB: return "Tab";
        case USB_KEY_SPACE: return "Space";
        case USB_KEY_MINUS: return "Minus";
        case USB_KEY_EQUAL: return "Equals";
        case USB_KEY_LEFTBRACE: return "LeftBrace";
        case USB_KEY_RIGHTBRACE: return "RightBrace";
        case USB_KEY_BACKSLASH: return "Backslash";
        case USB_KEY_SEMICOLON: return "Semicolon";
        case USB_KEY_APOSTROPHE: return "Apostrophe";
        case USB_KEY_GRAVE: return "Grave";
        case USB_KEY_COMMA: return "Comma";
        case USB_KEY_DOT: return "Dot";
        case USB_KEY_SLASH: return "Slash";
        default: {
            static char unknown_buf[16];
            snprintf(unknown_buf, sizeof(unknown_buf), "Key_0x%02X", key_code);
            return unknown_buf;
        }
    }
}

boolean usb_keyboard_init(usb_keyboard_manager_t* manager) {
    if (!manager) {
        return false;
    }
    
    if (!g_usb_initialized) {
        USB.begin();
        g_usb_initialized = true;
    }
    
    manager->initialized = true;
    manager->enabled = true;
    memset(g_keyboard_report, 0, sizeof(g_keyboard_report));
    
    return true;
}

void usb_keyboard_set_enabled(usb_keyboard_manager_t* manager, boolean enabled) {
    if (!manager || !manager->initialized) {
        return;
    }
    
    manager->enabled = enabled;
    
    if (!enabled) {
        // Отпустить все клавиши при выключении
        usb_keyboard_release_all(manager);
    }
}

boolean usb_keyboard_press_key(usb_keyboard_manager_t* manager, 
                                uint8 key_code, 
                                uint8 modifiers) {
    if (!manager) {
        Serial.println("[USB Keyboard] ERROR: manager is NULL");
        return false;
    }
    
    if (!manager->initialized) {
        Serial.println("[USB Keyboard] ERROR: manager not initialized");
        return false;
    }
    
    if (!manager->enabled) {
        Serial.println("[USB Keyboard] ERROR: manager not enabled");
        return false;
    }
    
    // Проверяем, не нажата ли уже эта клавиша
    for (int i = 2; i < 8; i++) {
        if (g_keyboard_report[i] == key_code) {
            // Клавиша уже нажата
            return true;
        }
    }
    
    // Ищем свободный слот для клавиши (слоты 2-7)
    for (int i = 2; i < 8; i++) {
        if (g_keyboard_report[i] == 0) {
            g_keyboard_report[i] = key_code;
            g_keyboard_report[0] |= modifiers;  // Добавляем модификаторы (накапливаем)
            
            // Выводим информацию о нажатой клавише в Serial Monitor
            const char* key_name = usb_keyboard_get_key_name(key_code);
            Serial.printf("[USB Keyboard] Key PRESSED: 0x%02X (%s)", key_code, key_name);
            if (modifiers != 0) {
                Serial.printf(" + Modifiers: 0x%02X", modifiers);
                if (modifiers & 0x01) Serial.print(" [Ctrl]");
                if (modifiers & 0x02) Serial.print(" [Shift]");
                if (modifiers & 0x04) Serial.print(" [Alt]");
                if (modifiers & 0x08) Serial.print(" [Win]");
            }
            Serial.println();
            
            // Обновляем USB HID report
            usb_keyboard_update(manager);
            
            return true;
        }
    }
    
    // Все слоты заняты
    Serial.printf("[USB Keyboard] All slots occupied, cannot press key 0x%02X\n", key_code);
    return false;
}

boolean usb_keyboard_release_key(usb_keyboard_manager_t* manager, uint8 key_code) {
    if (!manager) {
        Serial.println("[USB Keyboard] ERROR: manager is NULL");
        return false;
    }
    
    if (!manager->initialized) {
        Serial.println("[USB Keyboard] ERROR: manager not initialized");
        return false;
    }
    
    if (!manager->enabled) {
        Serial.println("[USB Keyboard] ERROR: manager not enabled");
        return false;
    }
    
    // Ищем клавишу и освобождаем слот
    boolean found = false;
    for (int i = 2; i < 8; i++) {
        if (g_keyboard_report[i] == key_code) {
            g_keyboard_report[i] = 0;
            found = true;
        }
    }
    
    if (found) {
        // Проверяем, остались ли еще нажатые клавиши
        boolean has_keys = false;
        for (int i = 2; i < 8; i++) {
            if (g_keyboard_report[i] != 0) {
                has_keys = true;
                break;
            }
        }
        
        // Если больше нет нажатых клавиш, очищаем модификаторы
        if (!has_keys) {
            g_keyboard_report[0] = 0;
        }
        
        // Выводим информацию об отпущенной клавише в Serial Monitor
        const char* key_name = usb_keyboard_get_key_name(key_code);
        Serial.printf("[USB Keyboard] Key RELEASED: 0x%02X (%s)\n", key_code, key_name);
        
        // Обновляем USB HID report
        usb_keyboard_update(manager);
        
        return true;
    }
    
    // Клавиша не найдена
    return false;
}

boolean usb_keyboard_release_all(usb_keyboard_manager_t* manager) {
    if (!manager || !manager->initialized) {
        return false;
    }
    
    // Выводим информацию об отпущенных клавишах
    boolean had_keys = false;
    for (int i = 2; i < 8; i++) {
        if (g_keyboard_report[i] != 0) {
            if (!had_keys) {
                Serial.print("[USB Keyboard] All keys RELEASED: ");
                had_keys = true;
            }
            const char* key_name = usb_keyboard_get_key_name(g_keyboard_report[i]);
            Serial.printf("0x%02X (%s) ", g_keyboard_report[i], key_name);
            g_keyboard_report[i] = 0;
        }
    }
    if (had_keys) {
        Serial.println();
    }
    g_keyboard_report[0] = 0;  // Очищаем модификаторы
    g_keyboard_report[1] = 0;   // Reserved
    
    // Обновляем USB HID report (все клавиши отпущены)
    usb_keyboard_update(manager);
    
    return true;
}

boolean usb_keyboard_is_key_pressed(usb_keyboard_manager_t* manager, uint8 key_code) {
    if (!manager || !manager->initialized) {
        return false;
    }
    
    for (int i = 2; i < 8; i++) {
        if (g_keyboard_report[i] == key_code) {
            return true;
        }
    }
    
    return false;
}

void usb_keyboard_update(usb_keyboard_manager_t* manager) {
    if (!manager || !manager->initialized || !manager->enabled) {
        return;
    }
    
    if (tud_hid_ready()) {
        uint8_t modifier = (uint8_t)g_keyboard_report[0];
        uint8_t keycode[6];
        for (int i = 0; i < 6; i++) {
            keycode[i] = (uint8_t)g_keyboard_report[i + 2];
        }
        tud_hid_keyboard_report(0, modifier, keycode);
    }
}

boolean usb_keyboard_is_available(void) {
    if (!g_usb_initialized) {
        return false;
    }
    return (boolean)tud_hid_ready();
}

boolean usb_keyboard_test_connection(uint8 dp_pin, uint8 dm_pin) {
    pinMode(dp_pin, INPUT);
    pinMode(dm_pin, INPUT);
    int dp_state = digitalRead(dp_pin);
    int dm_state = digitalRead(dm_pin);
    return (dp_state == HIGH || dm_state == HIGH);
}

boolean usb_keyboard_get_key_map(uint8* key_map, uint8* map_size) {
    if (!key_map || !map_size || *map_size < 64) {
        return false;
    }
    
    // Список основных доступных кодов клавиш
    static const uint8 available_keys[] = {
        USB_KEY_A, USB_KEY_B, USB_KEY_C, USB_KEY_D, USB_KEY_E, USB_KEY_F,
        USB_KEY_G, USB_KEY_H, USB_KEY_I, USB_KEY_J, USB_KEY_K, USB_KEY_L,
        USB_KEY_M, USB_KEY_N, USB_KEY_O, USB_KEY_P, USB_KEY_Q, USB_KEY_R,
        USB_KEY_S, USB_KEY_T, USB_KEY_U, USB_KEY_V, USB_KEY_W, USB_KEY_X,
        USB_KEY_Y, USB_KEY_Z,
        USB_KEY_1, USB_KEY_2, USB_KEY_3, USB_KEY_4, USB_KEY_5,
        USB_KEY_6, USB_KEY_7, USB_KEY_8, USB_KEY_9, USB_KEY_0,
        USB_KEY_ENTER, USB_KEY_ESCAPE, USB_KEY_BACKSPACE, USB_KEY_TAB,
        USB_KEY_SPACE, USB_KEY_MINUS, USB_KEY_EQUAL, USB_KEY_LEFTBRACE,
        USB_KEY_RIGHTBRACE, USB_KEY_BACKSLASH, USB_KEY_SEMICOLON,
        USB_KEY_APOSTROPHE, USB_KEY_GRAVE, USB_KEY_COMMA, USB_KEY_DOT,
        USB_KEY_SLASH
    };
    
    uint8 count = sizeof(available_keys) / sizeof(available_keys[0]);
    if (count > *map_size) {
        count = *map_size;
    }
    
    memcpy(key_map, available_keys, count);
    *map_size = count;
    
    return true;
}

boolean usb_keyboard_start_scan(uint32 duration_ms) {
    (void)duration_ms;
    // Реальное сканирование требует библиотеку EspUsbHost из рабочего проекта
    // C:\Users\Alex\Desktop\EtherCAT\ESP32_S3_USB_test
    // Для получения списка доступных кодов используйте Telnet команду get_usb_keyboard_map
    return false;
}

void usb_keyboard_stop_scan(void) {
}

boolean usb_keyboard_is_scanning(void) {
    return false;
}

#else // !CONFIG_IDF_TARGET_ESP32S3

// Stub implementation for non-ESP32-S3 platforms
boolean usb_keyboard_init(usb_keyboard_manager_t* manager) {
    Serial.println("[USB Keyboard] Not supported on this platform (ESP32-S3 required)");
    return false;
}

void usb_keyboard_set_enabled(usb_keyboard_manager_t* manager, boolean enabled) {
    (void)manager;
    (void)enabled;
}

boolean usb_keyboard_press_key(usb_keyboard_manager_t* manager, uint8 key_code, uint8 modifiers) {
    (void)manager;
    (void)key_code;
    (void)modifiers;
    return false;
}

boolean usb_keyboard_release_key(usb_keyboard_manager_t* manager, uint8 key_code) {
    (void)manager;
    (void)key_code;
    return false;
}

boolean usb_keyboard_release_all(usb_keyboard_manager_t* manager) {
    (void)manager;
    return false;
}

boolean usb_keyboard_is_key_pressed(usb_keyboard_manager_t* manager, uint8 key_code) {
    (void)manager;
    (void)key_code;
    return false;
}

void usb_keyboard_update(usb_keyboard_manager_t* manager) {
    (void)manager;
}

boolean usb_keyboard_is_available(void) {
    return false;
}

boolean usb_keyboard_test_connection(uint8 dp_pin, uint8 dm_pin) {
    (void)dp_pin;
    (void)dm_pin;
    return false;
}

boolean usb_keyboard_get_key_map(uint8* key_map, uint8* map_size) {
    (void)key_map;
    (void)map_size;
    return false;
}

const char* usb_keyboard_get_key_name(uint8 key_code) {
    (void)key_code;
    return "Unknown";
}

#endif // CONFIG_IDF_TARGET_ESP32S3
