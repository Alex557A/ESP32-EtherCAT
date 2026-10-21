/*
 * USB Keyboard Scanner - Чтение кодов клавиш с физической клавиатуры
 * Использует USB Host режим для чтения клавиш
 */

#include "usb_keyboard_scanner.h"
#include "ethercat_type.h"

#ifdef CONFIG_IDF_TARGET_ESP32S3

#include "EspUsbHost.h"
#include "telnet_server.h"
#include <Arduino.h>
#include <usb/usb_host.h>
#include <class/hid/hid.h>
#include <string.h>
#include <ArduinoJson.h>

// Глобальная переменная для отслеживания состояния
static boolean g_scanner_enabled = false;
static boolean g_scanner_initialized = false;
static void* g_telnet_server = NULL;  // Указатель на Telnet сервер для отправки данных

// Класс для обработки событий клавиатуры
class KeyboardScannerHandler : public EspUsbHost {
private:
    void sendKeyPressToTelnet(uint8_t keycode, uint8_t modifier) {
        if (!g_telnet_server) {
            return;
        }
        
        // Отправляем JSON-RPC уведомление (без id) через Telnet
        StaticJsonDocument<256> notification;
        notification["jsonrpc"] = "2.0";
        notification["method"] = "keyboard_key_pressed";
        JsonObject params = notification["params"].to<JsonObject>();
        params["keycode"] = keycode;
        params["modifier"] = modifier;
        
        // Формируем HEX строку для keycode
        char keycode_hex[8];
        snprintf(keycode_hex, sizeof(keycode_hex), "0x%02X", keycode);
        params["keycode_hex"] = keycode_hex;
        
        char buffer[256];
        size_t len = serializeJson(notification, buffer, sizeof(buffer));
        if (len > 0 && len < sizeof(buffer)) {
            buffer[len++] = '\r';
            buffer[len++] = '\n';
            buffer[len] = '\0';
            telnet_server_send_response((telnet_server_t*)g_telnet_server, buffer);
        }
    }
    
    void printKeyInfo(uint8_t keycode, uint8_t modifier) {
        // Отправляем данные в Telnet для веб-интерфейса
        sendKeyPressToTelnet(keycode, modifier);
        
        // Вычисляем сумму для вывода в Serial Monitor
        uint16_t sum = (uint16_t)keycode + (uint16_t)modifier;
        
        // Выводим информацию о модификаторах
        const char* mods = "";
        if (modifier != 0) {
            static char mod_buf[64];
            mod_buf[0] = '\0';
            if (modifier & 0x01) strcat(mod_buf, "LCtrl ");
            if (modifier & 0x02) strcat(mod_buf, "LShift ");
            if (modifier & 0x04) strcat(mod_buf, "LAlt ");
            if (modifier & 0x08) strcat(mod_buf, "LGUI ");
            if (modifier & 0x10) strcat(mod_buf, "RCtrl ");
            if (modifier & 0x20) strcat(mod_buf, "RShift ");
            if (modifier & 0x40) strcat(mod_buf, "RAlt ");
            if (modifier & 0x80) strcat(mod_buf, "RGUI ");
            mods = mod_buf;
        }
        
        if (modifier != 0) {
            Serial.printf("Keycode: 0x%02X | Modifier: 0x%02X (%s) | Sum: 0x%04X\n", 
                         keycode, modifier, mods, sum);
        } else {
            Serial.printf("Keycode: 0x%02X | Sum: 0x%04X\n", keycode, sum);
        }
    }

public:
    void onKeyboardKey(uint8_t ascii, uint8_t keycode, uint8_t modifier) override {
        if (!g_scanner_enabled) {
            return;
        }
        printKeyInfo(keycode, modifier);
    }

    void onKeyboard(hid_keyboard_report_t report, hid_keyboard_report_t last_report) override {
        if (!g_scanner_enabled) {
            return;
        }
        
        EspUsbHost::onKeyboard(report, last_report);
        
        // Сумма всех нажатых клавиш + модификаторы
        uint16_t total_sum = report.modifier;
        uint8_t key_count = 0;
        for (int i = 0; i < 6; i++) {
            if (report.keycode[i] != 0) {
                total_sum += report.keycode[i];
                key_count++;
            }
        }
        
        if (total_sum > 0) {
            // Отправляем данные в Telnet для веб-интерфейса (только если несколько клавиш или модификаторы)
            if ((key_count > 1 || report.modifier != 0) && g_telnet_server) {
                StaticJsonDocument<384> notification;
                notification["jsonrpc"] = "2.0";
                notification["method"] = "keyboard_keys_pressed";
                JsonObject params = notification["params"].to<JsonObject>();
                JsonArray keys = params["keys"].to<JsonArray>();
                for (int i = 0; i < 6; i++) {
                    if (report.keycode[i] != 0) {
                        keys.add(report.keycode[i]);
                    }
                }
                params["modifier"] = report.modifier;
                // Отправляем первый keycode для заполнения поля
                if (key_count > 0) {
                    for (int i = 0; i < 6; i++) {
                        if (report.keycode[i] != 0) {
                            char keycode_hex[8];
                            snprintf(keycode_hex, sizeof(keycode_hex), "0x%02X", report.keycode[i]);
                            params["keycode_hex"] = keycode_hex;
                            params["keycode"] = report.keycode[i];
                            break;
                        }
                    }
                }
                
                char buffer[384];
                size_t len = serializeJson(notification, buffer, sizeof(buffer));
                if (len > 0 && len < sizeof(buffer)) {
                    buffer[len++] = '\r';
                    buffer[len++] = '\n';
                    buffer[len] = '\0';
                    telnet_server_send_response((telnet_server_t*)g_telnet_server, buffer);
                }
            }
            
            // Выводим информацию о комбинации клавиш
            if (key_count > 1 || report.modifier != 0) {
                Serial.print("Keys: ");
                for (int i = 0; i < 6; i++) {
                    if (report.keycode[i] != 0) {
                        Serial.printf("0x%02X ", report.keycode[i]);
                    }
                }
                if (report.modifier != 0) {
                    Serial.printf("| Modifier: 0x%02X ", report.modifier);
                }
                Serial.printf("| Sum: 0x%04X\n", total_sum);
            }
        }
    }

    void onGone(const usb_host_client_event_msg_t *eventMsg) override {
        EspUsbHost::onGone(eventMsg);
    }
};

// Глобальный экземпляр обработчика
static KeyboardScannerHandler* g_keyboard_handler = NULL;

boolean usb_keyboard_scanner_init(void) {
    if (g_scanner_initialized) {
        return true;
    }
    
    g_keyboard_handler = new KeyboardScannerHandler();
    if (!g_keyboard_handler) {
        return false;
    }
    
    g_keyboard_handler->begin();
    g_keyboard_handler->setHIDLocal(HID_LOCAL_Japan_Katakana);
    
    g_scanner_initialized = true;
    g_scanner_enabled = true;
    
    return true;
}

void usb_keyboard_scanner_task(void) {
    if (!g_scanner_initialized || !g_keyboard_handler) {
        return;
    }
    
    g_keyboard_handler->task();
}

void usb_keyboard_scanner_set_enabled(boolean enabled) {
    g_scanner_enabled = enabled;
}

boolean usb_keyboard_scanner_is_enabled(void) {
    return g_scanner_enabled;
}

void usb_keyboard_scanner_set_telnet_server(void* telnet_server) {
    g_telnet_server = telnet_server;
}

#else // !CONFIG_IDF_TARGET_ESP32S3

boolean usb_keyboard_scanner_init(void) {
    return false;
}

void usb_keyboard_scanner_task(void) {
}

void usb_keyboard_scanner_set_enabled(boolean enabled) {
    (void)enabled;
}

boolean usb_keyboard_scanner_is_enabled(void) {
    return false;
}

void usb_keyboard_scanner_set_telnet_server(void* telnet_server) {
    (void)telnet_server;
}

#endif // CONFIG_IDF_TARGET_ESP32S3
