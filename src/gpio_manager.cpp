/*
 * GPIO Manager - Implementation
 * 
 * Phase 5, Week 8, Day 51-56
 * ESP32 EtherCAT Slave v4
 */

#include "gpio_manager.h"
#include "usb_keyboard.h"
#include "mcp23s17.h"
#include <stddef.h>
#include <Arduino.h>
#include <string.h>
#include <Preferences.h>

// ============================================================================
// LEDC PWM (Analog Output) — указ о выделении канала аналоговым выходам
// ============================================================================

#define GPIO_PWM_FREQ          5000   /**< Частота PWM, Гц */
#define GPIO_PWM_RESOLUTION    10     /**< Разрядность PWM (значение 0-1023) */
#define GPIO_PWM_MAX_DUTY      ((1u << GPIO_PWM_RESOLUTION) - 1u)  /**< Максимальный duty */
#define GPIO_ANALOG_MAX_VALUE  1023   /**< Максимальное значение analog output */
#define GPIO_LEDC_MAX_CHANNELS 8      /**< Количество LEDC каналов у ESP32-S3 */

/**
 * @brief Сия функция раздаёт LEDC-каналы аналоговым выходам, дабы не ведали они свары меж собой
 * @param manager Указатель на GPIO Manager
 * @param idx Индекс канала
 * @return Номер LEDC канала (0..GPIO_LEDC_MAX_CHANNELS-1) или -1 если нет свободных
 *
 * Канал определяется как порядковый номер включённого analog-out канала,
 * что гарантирует уникальность без глобального состояния.
 */
static int8 gpio_manager_pick_ledc(gpio_manager_t* manager, uint8 idx) {
    if (manager == NULL || idx >= GPIO_MAX_CHANNELS) {
        return -1;
    }
    int8 slot = -1;
    for (uint8 i = 0; i <= idx; i++) {
        const gpio_channel_t* c = &manager->channels[i];
        if (c->enabled && c->type == GPIO_TYPE_ANALOG_OUT) {
            slot++;
        }
    }
    if (slot < 0 || slot >= GPIO_LEDC_MAX_CHANNELS) {
        return -1;
    }
    return slot;
}

// ============================================================================
// Encoder ISR Data
// ============================================================================

// Глобальный массив для ISR (нужен доступ из прерываний)
static volatile int32 g_encoder_counters[GPIO_MAX_ENCODERS];
static volatile uint8 g_encoder_last_state[GPIO_MAX_ENCODERS];
static volatile uint8 g_encoder_pin_a[GPIO_MAX_ENCODERS];
static volatile uint8 g_encoder_pin_b[GPIO_MAX_ENCODERS];

// Флаги: держит ли караул (ISR) тот или иной энкодер (трогать лишь из основного контекста)
static bool g_encoder_isr_active[GPIO_MAX_ENCODERS];

// USB Keyboard Manager (глобальный, один на всю систему)
static usb_keyboard_manager_t g_usb_keyboard_manager;
static boolean g_usb_keyboard_initialized = false;

// Предыдущие состояния для edge detection USB Keyboard каналов
static boolean g_usb_keyboard_prev_states[GPIO_MAX_CHANNELS];

// ============================================================================
// Encoder ISR Handlers
// ============================================================================

static void IRAM_ATTR encoder_isr_0() {
    uint8 pin_a = g_encoder_pin_a[0];
    uint8 pin_b = g_encoder_pin_b[0];
    uint8 pin_a_state = digitalRead(pin_a);
    uint8 pin_b_state = digitalRead(pin_b);
    uint8 state = (pin_a_state << 1) | pin_b_state;
    uint8 last = g_encoder_last_state[0];
    
    // Quadrature decoder
    if ((last == 0b00 && state == 0b01) || 
        (last == 0b01 && state == 0b11) ||
        (last == 0b11 && state == 0b10) ||
        (last == 0b10 && state == 0b00)) {
        g_encoder_counters[0]++;
    } else if ((last == 0b00 && state == 0b10) || 
               (last == 0b10 && state == 0b11) ||
               (last == 0b11 && state == 0b01) ||
               (last == 0b01 && state == 0b00)) {
        g_encoder_counters[0]--;
    }
    
    g_encoder_last_state[0] = state;
}

// Сии караульные (ISR) приставлены к энкодерам 1..7; энкодер 0 имеет своего
// стража выше — encoder_isr_0. attachInterrupt() аргументов не передаёт, оттого
// всякому энкодеру надобен отдельный обработчик со своим индексом.
#define GPIO_DEFINE_ENCODER_ISR(n)                                            \
    static void IRAM_ATTR encoder_isr_##n() {                                 \
        uint8 pin_a = g_encoder_pin_a[n];                                     \
        uint8 pin_b = g_encoder_pin_b[n];                                     \
        uint8 state = (digitalRead(pin_a) << 1) | digitalRead(pin_b);         \
        uint8 last = g_encoder_last_state[n];                                 \
        if ((last == 0b00 && state == 0b01) ||                                \
            (last == 0b01 && state == 0b11) ||                                \
            (last == 0b11 && state == 0b10) ||                                \
            (last == 0b10 && state == 0b00)) {                                \
            g_encoder_counters[n]++;                                          \
        } else if ((last == 0b00 && state == 0b10) ||                         \
                   (last == 0b10 && state == 0b11) ||                         \
                   (last == 0b11 && state == 0b01) ||                         \
                   (last == 0b01 && state == 0b00)) {                         \
            g_encoder_counters[n]--;                                          \
        }                                                                     \
        g_encoder_last_state[n] = state;                                      \
    }

GPIO_DEFINE_ENCODER_ISR(1)
GPIO_DEFINE_ENCODER_ISR(2)
GPIO_DEFINE_ENCODER_ISR(3)
GPIO_DEFINE_ENCODER_ISR(4)
GPIO_DEFINE_ENCODER_ISR(5)
GPIO_DEFINE_ENCODER_ISR(6)
GPIO_DEFINE_ENCODER_ISR(7)

// Таблица указателей на ISR (индекс = номер энкодера).
// ВНИМАНИЕ: размер должен совпадать с GPIO_MAX_ENCODERS.
static void (*const g_encoder_isr_table[GPIO_MAX_ENCODERS])() = {
    encoder_isr_0, encoder_isr_1, encoder_isr_2, encoder_isr_3,
    encoder_isr_4, encoder_isr_5, encoder_isr_6, encoder_isr_7
};

/**
 * @brief Сия функция ставит караул (прерывание) на энкодер
 * @param idx Индекс энкодера
 */
static void gpio_manager_attach_encoder_isr(uint8 idx) {
    if (idx >= GPIO_MAX_ENCODERS) {
        return;
    }
    attachInterrupt(digitalPinToInterrupt(g_encoder_pin_a[idx]),
                    g_encoder_isr_table[idx], CHANGE);
    attachInterrupt(digitalPinToInterrupt(g_encoder_pin_b[idx]),
                    g_encoder_isr_table[idx], CHANGE);
    g_encoder_isr_active[idx] = true;
}

/**
 * @brief Сия функция снимает караул (прерывание) с энкодера
 * @param idx Индекс энкодера
 */
static void gpio_manager_detach_encoder_isr(uint8 idx) {
    if (idx >= GPIO_MAX_ENCODERS || !g_encoder_isr_active[idx]) {
        return;
    }
    detachInterrupt(digitalPinToInterrupt(g_encoder_pin_a[idx]));
    detachInterrupt(digitalPinToInterrupt(g_encoder_pin_b[idx]));
    g_encoder_isr_active[idx] = false;
}

// ============================================================================
// Инициализация
// ============================================================================

boolean gpio_manager_init(gpio_manager_t* manager) {
    if (manager == NULL) {
        return false;
    }
    
    Serial.println("[GPIO] Initializing...");
    
    // Очистка структуры
    memset(manager, 0, sizeof(gpio_manager_t));
    
    // Инициализация счетчиков энкодеров и снятие прежних караулов (ISR)
    for (int i = 0; i < GPIO_MAX_ENCODERS; i++) {
        g_encoder_counters[i] = 0;
        g_encoder_last_state[i] = 0;
        g_encoder_pin_a[i] = 0;
        g_encoder_pin_b[i] = 0;
        g_encoder_isr_active[i] = false;
    }
    
    // Инициализация USB Keyboard предыдущих состояний
    for (int i = 0; i < GPIO_MAX_CHANNELS; i++) {
        g_usb_keyboard_prev_states[i] = false;
    }
    
    // USB Keyboard будет инициализирован при первом использовании или при наличии активных каналов
    g_usb_keyboard_initialized = false;
    
    // Инициализация MCP23S17 (по умолчанию выключен)
    manager->mcp23s17_enabled = false;
    memset(&manager->mcp23s17, 0, sizeof(mcp23s17_state_t));
    
    Serial.println("[GPIO] Init OK");
    
    // Загрузка конфигурации из Flash (если есть)
    gpio_manager_load_from_flash(manager);
    
    // USB Keyboard инициализация отключена - используется USB Host Scanner для чтения клавиш
    // Раскомментируйте, если нужен USB Device режим (отправка клавиш)
    #ifdef CONFIG_IDF_TARGET_ESP32S3
    /*
    if (usb_keyboard_init(&g_usb_keyboard_manager)) {
        g_usb_keyboard_initialized = true;
        Serial.println("[GPIO] USB Keyboard: ✓ инициализирован");
    } else {
        Serial.println("[GPIO] USB Keyboard: ✗ ошибка инициализации");
    }
    */
    #endif
    
    return true;
}

// ============================================================================
// Persistent Storage (Flash/EEPROM через Preferences API)
// ============================================================================

static Preferences g_preferences;
static const char* PREF_NAMESPACE = "gpio_cfg";

/**
 * @brief Сохранение конфигурации GPIO Manager в Flash
 * @param manager Указатель на GPIO Manager
 * @return true если успешно
 */
boolean gpio_manager_save_to_flash(gpio_manager_t* manager) {
    if (manager == NULL) {
        return false;
    }
    
    if (!g_preferences.begin(PREF_NAMESPACE, false)) {
        Serial.println("[GPIO] ERROR: Failed to open preferences for writing");
        return false;
    }
    
    // Сохранение счетчиков
    g_preferences.putUChar("ch_count", manager->channel_count);
    g_preferences.putUChar("enc_count", manager->encoder_count);
    g_preferences.putBool("mcp_enabled", manager->mcp23s17_enabled);
    
    // Сохранение каналов
    if (manager->channel_count > 0) {
        size_t channels_size = sizeof(gpio_channel_t) * manager->channel_count;
        g_preferences.putBytes("channels", manager->channels, channels_size);
        Serial.printf("[GPIO] Saved %d channels to Flash (%d bytes)\n", manager->channel_count, channels_size);
    }
    
    // Сохранение энкодеров
    if (manager->encoder_count > 0) {
        size_t encoders_size = sizeof(gpio_encoder_t) * manager->encoder_count;
        g_preferences.putBytes("encoders", manager->encoders, encoders_size);
        Serial.printf("[GPIO] Saved %d encoders to Flash (%d bytes)\n", manager->encoder_count, encoders_size);
    }
    
    // Сохранение конфигурации MCP23S17
    if (manager->mcp23s17_enabled) {
        g_preferences.putUChar("mcp_cs", manager->mcp23s17.config.cs_pin);
        g_preferences.putUShort("mcp_dir", manager->mcp23s17.config.port_direction);
        g_preferences.putUShort("mcp_pullup", manager->mcp23s17.config.port_pullup);
        g_preferences.putUShort("mcp_invert", manager->mcp23s17.config.port_invert);
    }
    
    g_preferences.end();
    Serial.println("[GPIO] Configuration saved to Flash");
    return true;
}

// Forward declarations для внутренних функций
static void gpio_manager_apply_channel_hw(gpio_manager_t* manager, uint8 idx, gpio_channel_t* ch);
static void gpio_manager_rebuild_encoders_isr(gpio_manager_t* manager);

/**
 * @brief Загрузка конфигурации GPIO Manager из Flash
 * @param manager Указатель на GPIO Manager
 * @return true если успешно загружена конфигурация
 */
boolean gpio_manager_load_from_flash(gpio_manager_t* manager) {
    if (manager == NULL) {
        return false;
    }
    
    if (!g_preferences.begin(PREF_NAMESPACE, true)) {
        Serial.println("[GPIO] No saved configuration in Flash (first boot)");
        return false;
    }
    
    // Загрузка счетчиков
    uint8_t ch_count = g_preferences.getUChar("ch_count", 0);
    uint8_t enc_count = g_preferences.getUChar("enc_count", 0);
    boolean mcp_enabled = g_preferences.getBool("mcp_enabled", false);
    
    if (ch_count == 0 && enc_count == 0 && !mcp_enabled) {
        Serial.println("[GPIO] No saved configuration found in Flash");
        g_preferences.end();
        return false;
    }
    
    Serial.printf("[GPIO] Loading configuration from Flash: %d channels, %d encoders\n", ch_count, enc_count);
    
    // Загрузка каналов
    if (ch_count > 0 && ch_count <= GPIO_MAX_CHANNELS) {
        size_t channels_size = sizeof(gpio_channel_t) * ch_count;
        size_t bytes_read = g_preferences.getBytes("channels", manager->channels, channels_size);
        if (bytes_read == channels_size) {
            manager->channel_count = ch_count;
            Serial.printf("[GPIO] Loaded %d channels from Flash\n", ch_count);
            
            // Применение конфигурации к аппаратуре
            for (uint8_t i = 0; i < ch_count; i++) {
                gpio_manager_apply_channel_hw(manager, i, &manager->channels[i]);
            }
        } else {
            Serial.printf("[GPIO] ERROR: Failed to load channels (read %d bytes, expected %d)\n", bytes_read, channels_size);
            manager->channel_count = 0;
        }
    }
    
    // Загрузка энкодеров
    if (enc_count > 0 && enc_count <= GPIO_MAX_ENCODERS) {
        size_t encoders_size = sizeof(gpio_encoder_t) * enc_count;
        size_t bytes_read = g_preferences.getBytes("encoders", manager->encoders, encoders_size);
        if (bytes_read == encoders_size) {
            manager->encoder_count = enc_count;
            Serial.printf("[GPIO] Loaded %d encoders from Flash\n", enc_count);
            
            // Применение конфигурации к аппаратуре
            for (uint8_t i = 0; i < enc_count; i++) {
                if (manager->encoders[i].enabled) {
                    pinMode(manager->encoders[i].pin_a, INPUT_PULLUP);
                    pinMode(manager->encoders[i].pin_b, INPUT_PULLUP);
                }
            }
            gpio_manager_rebuild_encoders_isr(manager);
        } else {
            Serial.printf("[GPIO] ERROR: Failed to load encoders (read %d bytes, expected %d)\n", bytes_read, encoders_size);
            manager->encoder_count = 0;
        }
    }
    
    // Загрузка конфигурации MCP23S17
    if (mcp_enabled) {
        uint8_t mcp_cs = g_preferences.getUChar("mcp_cs", 0);
        uint16_t mcp_dir = g_preferences.getUShort("mcp_dir", 0xFFFF);
        uint16_t mcp_pullup = g_preferences.getUShort("mcp_pullup", 0);
        uint16_t mcp_invert = g_preferences.getUShort("mcp_invert", 0);
        
        if (gpio_manager_init_mcp23s17(manager, mcp_cs, mcp_dir, mcp_pullup, mcp_invert)) {
            Serial.println("[GPIO] MCP23S17 configuration loaded from Flash");
        } else {
            Serial.println("[GPIO] ERROR: Failed to initialize MCP23S17 from saved config");
        }
    }
    
    g_preferences.end();
    Serial.println("[GPIO] Configuration loaded from Flash successfully");
    return true;
}

// ============================================================================
// Add Channel/Encoder
// ============================================================================

static void gpio_manager_apply_channel_hw(gpio_manager_t* manager, uint8 idx, gpio_channel_t* ch) {
    if (ch == NULL || !ch->enabled) {
        return;
    }
    switch (ch->type) {
        case GPIO_TYPE_DIGITAL_IN:
            pinMode(ch->gpio_pin, INPUT);
            Serial.printf("[GPIO] Channel %d: Digital IN (GPIO %d)\n", idx, ch->gpio_pin);
            break;
        case GPIO_TYPE_DIGITAL_OUT:
            pinMode(ch->gpio_pin, OUTPUT);
            digitalWrite(ch->gpio_pin, LOW);
            Serial.printf("[GPIO] Channel %d: Digital OUT (GPIO %d)\n", idx, ch->gpio_pin);
            break;
        case GPIO_TYPE_ANALOG_IN:
            pinMode(ch->gpio_pin, INPUT);
            Serial.printf("[GPIO] Channel %d: Analog IN (GPIO %d)\n", idx, ch->gpio_pin);
            break;
        case GPIO_TYPE_ANALOG_OUT: {
            // ESP32-S3 лишён DAC, посему обращаемся к LEDC/PWM
            int8 ledc = gpio_manager_pick_ledc(manager, idx);
            if (ledc < 0) {
                Serial.printf("[GPIO] Channel %d: Analog OUT (GPIO %d) - ERROR: нет свободного LEDC канала\n",
                              idx, ch->gpio_pin);
                break;
            }
            ch->ledc_channel = (uint8)ledc;
            ledcSetup(ch->ledc_channel, GPIO_PWM_FREQ, GPIO_PWM_RESOLUTION);
            ledcAttachPin(ch->gpio_pin, ch->ledc_channel);
            ledcWrite(ch->ledc_channel, 0);
            Serial.printf("[GPIO] Channel %d: Analog OUT (GPIO %d, PWM ch=%d, %dHz, %d-bit)\n",
                          idx, ch->gpio_pin, ch->ledc_channel, GPIO_PWM_FREQ, GPIO_PWM_RESOLUTION);
            break;
        }
        case GPIO_TYPE_USB_KEYBOARD:
            Serial.printf("[GPIO] Channel %d: USB Keyboard (Key=0x%02X, Modifiers=0x%02X)\n",
                          idx, ch->usb_key_code, ch->usb_modifiers);
            break;
        case GPIO_TYPE_MCP23S17:
            Serial.printf("[GPIO] Channel %d: MCP23S17 (Port %d)\n", idx, ch->mcp23s17_port);
            break;
        default:
            break;
    }
}

int8 gpio_manager_add_channel(gpio_manager_t* manager, const gpio_channel_t* channel) {
    if (manager == NULL || channel == NULL) {
        return -1;
    }
    
    if (manager->channel_count >= GPIO_MAX_CHANNELS) {
        Serial.println("[GPIO] Channel limit reached");
        return -1;
    }
    
    uint8 idx = manager->channel_count;
    
    // Копирование конфигурации
    memcpy(&manager->channels[idx], channel, sizeof(gpio_channel_t));
    
    gpio_manager_apply_channel_hw(manager, idx, &manager->channels[idx]);
    
    manager->channel_count++;
    
    // USB keyboard уже инициализирован при старте, просто выводим информацию о добавленном канале
    #ifdef CONFIG_IDF_TARGET_ESP32S3
    if (channel->enabled && channel->type == GPIO_TYPE_USB_KEYBOARD) {
        if (g_usb_keyboard_initialized) {
            Serial.printf("[GPIO] USB Keyboard канал [%d] добавлен: Key=0x%02X, Modifiers=0x%02X, PDO byte=%d, bit=%d\n",
                         idx, channel->usb_key_code, channel->usb_modifiers, channel->pdo_byte, channel->pdo_bit);
        } else {
            Serial.printf("[GPIO] USB Keyboard канал [%d] добавлен, но USB Keyboard не инициализирован!\n", idx);
        }
    }
    #endif
    
    // Автоматическое сохранение конфигурации в Flash после добавления
    gpio_manager_save_to_flash(manager);
    
    return idx;
}

boolean gpio_manager_set_channel(gpio_manager_t* manager, uint8 index, const gpio_channel_t* channel) {
    if (manager == NULL || channel == NULL) {
        return false;
    }
    if (index > manager->channel_count) {
        return false;
    }
    if (index >= GPIO_MAX_CHANNELS) {
        return false;
    }

    if (index == manager->channel_count) {
        return gpio_manager_add_channel(manager, channel) >= 0;
    }

    memcpy(&manager->channels[index], channel, sizeof(gpio_channel_t));
    gpio_manager_apply_channel_hw(manager, index, &manager->channels[index]);
    
    // Автоматическое сохранение конфигурации в Flash после изменения
    gpio_manager_save_to_flash(manager);

    // reset USB keyboard previous states after reindexing/update
    for (int i = 0; i < GPIO_MAX_CHANNELS; i++) {
        g_usb_keyboard_prev_states[i] = false;
    }
    
    // USB keyboard уже инициализирован при старте, просто выводим информацию об установленном канале
    #ifdef CONFIG_IDF_TARGET_ESP32S3
    if (channel->enabled && channel->type == GPIO_TYPE_USB_KEYBOARD) {
        if (g_usb_keyboard_initialized) {
            Serial.printf("[GPIO] USB Keyboard канал [%d] установлен: Key=0x%02X, Modifiers=0x%02X, PDO byte=%d, bit=%d\n",
                         index, channel->usb_key_code, channel->usb_modifiers, channel->pdo_byte, channel->pdo_bit);
        } else {
            Serial.printf("[GPIO] USB Keyboard канал [%d] установлен, но USB Keyboard не инициализирован!\n", index);
        }
    }
    #endif
    
    return true;
}

boolean gpio_manager_delete_channel(gpio_manager_t* manager, uint8 index) {
    if (manager == NULL) {
        return false;
    }
    if (index >= manager->channel_count) {
        return false;
    }
    for (uint8 i = index; i + 1 < manager->channel_count; i++) {
        manager->channels[i] = manager->channels[i + 1];
    }
    memset(&manager->channels[manager->channel_count - 1], 0, sizeof(gpio_channel_t));
    manager->channel_count--;
    
    // Автоматическое сохранение конфигурации в Flash после удаления
    gpio_manager_save_to_flash(manager);

    // reset USB keyboard previous states after reindexing
    for (int i = 0; i < GPIO_MAX_CHANNELS; i++) {
        g_usb_keyboard_prev_states[i] = false;
    }
    return true;
}

static void gpio_manager_rebuild_encoders_isr(gpio_manager_t* manager) {
    if (manager == NULL) {
        return;
    }

    // Снимаем прежние караулы (прерывания) со всех энкодеров
    for (uint8 i = 0; i < GPIO_MAX_ENCODERS; i++) {
        gpio_manager_detach_encoder_isr(i);
    }

    // Очищаем привязки пинов, дабы не осталось следов прежних назначений
    for (int i = 0; i < GPIO_MAX_ENCODERS; i++) {
        g_encoder_pin_a[i] = 0;
        g_encoder_pin_b[i] = 0;
        g_encoder_last_state[i] = 0;
    }

    // Ставим караул на всякий включённый энкодер
    for (uint8 i = 0; i < manager->encoder_count && i < GPIO_MAX_ENCODERS; i++) {
        if (!manager->encoders[i].enabled) {
            continue;
        }
        g_encoder_pin_a[i] = manager->encoders[i].pin_a;
        g_encoder_pin_b[i] = manager->encoders[i].pin_b;
        g_encoder_last_state[i] = (digitalRead(g_encoder_pin_a[i]) << 1) |
                                   digitalRead(g_encoder_pin_b[i]);
        gpio_manager_attach_encoder_isr(i);
    }
}

int8 gpio_manager_add_encoder(gpio_manager_t* manager, const gpio_encoder_t* encoder) {
    if (manager == NULL || encoder == NULL) {
        return -1;
    }
    
    if (manager->encoder_count >= GPIO_MAX_ENCODERS) {
        Serial.println("[GPIO] Encoder limit reached");
        return -1;
    }
    
    uint8 idx = manager->encoder_count;
    
    // Копирование конфигурации
    memcpy(&manager->encoders[idx], encoder, sizeof(gpio_encoder_t));
    
    // Инициализация encoder pins
    if (encoder->enabled) {
        pinMode(encoder->pin_a, INPUT_PULLUP);
        pinMode(encoder->pin_b, INPUT_PULLUP);
        
        // Сохранение pins для ISR
        g_encoder_pin_a[idx] = encoder->pin_a;
        g_encoder_pin_b[idx] = encoder->pin_b;
        g_encoder_last_state[idx] = (digitalRead(encoder->pin_a) << 1) |
                                     digitalRead(encoder->pin_b);
        
        // Ставим караул на сей энкодер
        gpio_manager_attach_encoder_isr(idx);
        
        Serial.printf("[GPIO] Encoder %d: A=%d, B=%d\n", 
                     idx, encoder->pin_a, encoder->pin_b);
    }
    
    manager->encoder_count++;
    
    // Автоматическое сохранение конфигурации в Flash после добавления
    gpio_manager_save_to_flash(manager);
    
    return idx;
}

boolean gpio_manager_set_encoder(gpio_manager_t* manager, uint8 index, const gpio_encoder_t* encoder) {
    if (manager == NULL || encoder == NULL) {
        return false;
    }
    if (index > manager->encoder_count) {
        return false;
    }
    if (index >= GPIO_MAX_ENCODERS) {
        return false;
    }

    if (index == manager->encoder_count) {
        return gpio_manager_add_encoder(manager, encoder) >= 0;
    }

    memcpy(&manager->encoders[index], encoder, sizeof(gpio_encoder_t));
    if (encoder->enabled) {
        pinMode(encoder->pin_a, INPUT_PULLUP);
        pinMode(encoder->pin_b, INPUT_PULLUP);
    }
    gpio_manager_rebuild_encoders_isr(manager);
    
    // Автоматическое сохранение конфигурации в Flash после изменения
    gpio_manager_save_to_flash(manager);
    
    return true;
}

boolean gpio_manager_delete_encoder(gpio_manager_t* manager, uint8 index) {
    if (manager == NULL) {
        return false;
    }
    if (index >= manager->encoder_count) {
        return false;
    }

    // Снимаем караул со всех энкодеров, ибо после переиндексации расстановка изменится
    for (uint8 i = 0; i < GPIO_MAX_ENCODERS; i++) {
        gpio_manager_detach_encoder_isr(i);
    }

    for (uint8 i = index; i + 1 < manager->encoder_count; i++) {
        manager->encoders[i] = manager->encoders[i + 1];
        g_encoder_counters[i] = g_encoder_counters[i + 1];
    }
    memset(&manager->encoders[manager->encoder_count - 1], 0, sizeof(gpio_encoder_t));
    g_encoder_counters[manager->encoder_count - 1] = 0;
    manager->encoder_count--;

    gpio_manager_rebuild_encoders_isr(manager);
    
    // Автоматическое сохранение конфигурации в Flash после удаления
    gpio_manager_save_to_flash(manager);
    
    return true;
}

// ============================================================================
// Update Outputs (PDO → GPIO)
// ============================================================================

void gpio_manager_update_outputs(gpio_manager_t* manager, 
                                  const uint8* pdo_output, 
                                  uint16 size) {
    if (manager == NULL || pdo_output == NULL) {
        return;
    }
    
    for (uint8 i = 0; i < manager->channel_count; i++) {
        gpio_channel_t* ch = &manager->channels[i];
        
        if (!ch->enabled) {
            continue;
        }
        
        if (ch->type == GPIO_TYPE_DIGITAL_OUT) {
            // Digital output: извлечь бит из PDO
            if (ch->pdo_byte < size) {
                uint8 byte_val = pdo_output[ch->pdo_byte];
                boolean bit_val = (byte_val >> ch->pdo_bit) & 0x01;
                
                if (ch->invert) {
                    bit_val = !bit_val;
                }
                
                digitalWrite(ch->gpio_pin, bit_val);
            }
        }
        else if (ch->type == GPIO_TYPE_ANALOG_OUT) {
            // Аналоговый выход: извлекаем 16-битное значение и обращаем оное в скважность PWM
            if (ch->pdo_byte + 1 < size) {
                uint16 value = pdo_output[ch->pdo_byte] | 
                               (pdo_output[ch->pdo_byte + 1] << 8);
                
                uint32 duty = ((uint32)value * GPIO_PWM_MAX_DUTY) / GPIO_ANALOG_MAX_VALUE;
                if (duty > GPIO_PWM_MAX_DUTY) {
                    duty = GPIO_PWM_MAX_DUTY;
                }
                if (ch->invert) {
                    duty = GPIO_PWM_MAX_DUTY - duty;
                }
                ledcWrite(ch->ledc_channel, duty);
            }
        }
        else if (ch->type == GPIO_TYPE_USB_KEYBOARD) {
            // USB Keyboard: edge detection для отправки клавиш
            if (ch->pdo_byte < size) {
                // Инициализация USB Keyboard при первом использовании
                if (!g_usb_keyboard_initialized) {
                    if (usb_keyboard_init(&g_usb_keyboard_manager)) {
                        g_usb_keyboard_initialized = true;
                        Serial.println("[GPIO] USB Keyboard initialized (first use)");
                        // Инициализация предыдущих состояний
                        for (uint8 j = 0; j < GPIO_MAX_CHANNELS; j++) {
                            g_usb_keyboard_prev_states[j] = false;
                        }
                    } else {
                        Serial.println("[GPIO] ERROR: USB Keyboard init failed");
                        continue;  // Пропускаем этот канал
                    }
                }
                
                if (g_usb_keyboard_initialized) {
                    uint8 byte_val = pdo_output[ch->pdo_byte];
                    boolean bit_val = (byte_val >> ch->pdo_bit) & 0x01;
                    
                    if (ch->invert) {
                        bit_val = !bit_val;
                    }
                    
                    // Edge detection: отслеживание изменений состояния
                    boolean prev = g_usb_keyboard_prev_states[i];
                    
                    if (bit_val == 1 && prev == 0) {
                        // Нажатие клавиши (0→1)
                        usb_keyboard_press_key(&g_usb_keyboard_manager, 
                                               ch->usb_key_code, 
                                               ch->usb_modifiers);
                    } else if (bit_val == 0 && prev == 1) {
                        // Отпускание клавиши (1→0)
                        usb_keyboard_release_key(&g_usb_keyboard_manager, 
                                                 ch->usb_key_code);
                    }
                    
                    g_usb_keyboard_prev_states[i] = bit_val;
                }
            }
        }
        else if (ch->type == GPIO_TYPE_MCP23S17) {
            // MCP23S17: определить направление из настроек порта
            if (ch->pdo_byte < size && manager->mcp23s17_enabled && 
                ch->mcp23s17_port < MCP23S17_MAX_PORTS) {
                
                // Проверяем direction порта из настроек (0=выход, 1=вход)
                boolean is_input = (manager->mcp23s17.config.port_direction >> ch->mcp23s17_port) & 0x01;
                
                if (!is_input) {
                    // Порт настроен как выход - записываем бит в порт MCP23S17
                    uint8 byte_val = pdo_output[ch->pdo_byte];
                    boolean bit_val = (byte_val >> ch->pdo_bit) & 0x01;
                    
                    // Используем invert из настроек порта
                    boolean port_invert = (manager->mcp23s17.config.port_invert >> ch->mcp23s17_port) & 0x01;
                    if (port_invert) {
                        bit_val = !bit_val;
                    }
                    
                    mcp23s17_write_pin(&manager->mcp23s17, ch->mcp23s17_port, bit_val);
                }
            }
        }
    }
    
    manager->update_count++;
    
    // Обновление MCP23S17 (для edge detection входов)
    if (manager->mcp23s17_enabled) {
        gpio_manager_update_mcp23s17(manager);
    }
}

// ============================================================================
// Update Inputs (GPIO → PDO)
// ============================================================================

void gpio_manager_update_inputs(gpio_manager_t* manager, 
                                 uint8* pdo_input, 
                                 uint16 size) {
    if (manager == NULL || pdo_input == NULL) {
        return;
    }
    
    // Digital/Analog Inputs
    for (uint8 i = 0; i < manager->channel_count; i++) {
        gpio_channel_t* ch = &manager->channels[i];
        
        if (!ch->enabled) {
            continue;
        }
        
        if (ch->type == GPIO_TYPE_DIGITAL_IN) {
            // Digital input: записать бит в PDO
            if (ch->pdo_byte < size) {
                boolean value = digitalRead(ch->gpio_pin);
                
                if (ch->invert) {
                    value = !value;
                }
                
                if (value) {
                    pdo_input[ch->pdo_byte] |= (1 << ch->pdo_bit);
                } else {
                    pdo_input[ch->pdo_byte] &= ~(1 << ch->pdo_bit);
                }
            }
        }
        else if (ch->type == GPIO_TYPE_ANALOG_IN) {
            // Analog input: записать 16-bit значение
            if (ch->pdo_byte + 1 < size) {
                uint16 value = analogRead(ch->gpio_pin);  // 0-4095 (12-bit)
                
                pdo_input[ch->pdo_byte] = value & 0xFF;
                pdo_input[ch->pdo_byte + 1] = (value >> 8) & 0xFF;
            }
        }
        else if (ch->type == GPIO_TYPE_MCP23S17) {
            // MCP23S17: определить направление из настроек порта
            if (ch->pdo_byte < size && manager->mcp23s17_enabled && 
                ch->mcp23s17_port < MCP23S17_MAX_PORTS) {
                
                // Проверяем direction порта из настроек (0=выход, 1=вход)
                boolean is_input = (manager->mcp23s17.config.port_direction >> ch->mcp23s17_port) & 0x01;
                
                if (is_input) {
                    // Порт настроен как вход - читаем бит из порта MCP23S17
                    boolean value = mcp23s17_read_pin(&manager->mcp23s17, ch->mcp23s17_port);
                    
                    // Используем invert из настроек порта
                    boolean port_invert = (manager->mcp23s17.config.port_invert >> ch->mcp23s17_port) & 0x01;
                    if (port_invert) {
                        value = !value;
                    }
                    
                    if (value) {
                        pdo_input[ch->pdo_byte] |= (1 << ch->pdo_bit);
                    } else {
                        pdo_input[ch->pdo_byte] &= ~(1 << ch->pdo_bit);
                    }
                }
            }
        }
    }
    
    // Encoders
    for (uint8 i = 0; i < manager->encoder_count; i++) {
        gpio_encoder_t* enc = &manager->encoders[i];
        
        if (!enc->enabled) {
            continue;
        }
        
        // Читаем counter из ISR
        int32 count = g_encoder_counters[i];
        
        // Записываем 32-bit значение в PDO (little-endian)
        if (enc->pdo_byte_start + 3 < size) {
            pdo_input[enc->pdo_byte_start] = count & 0xFF;
            pdo_input[enc->pdo_byte_start + 1] = (count >> 8) & 0xFF;
            pdo_input[enc->pdo_byte_start + 2] = (count >> 16) & 0xFF;
            pdo_input[enc->pdo_byte_start + 3] = (count >> 24) & 0xFF;
        }
    }
}

// ============================================================================
// Direct Access Functions
// ============================================================================

void gpio_manager_set_digital_output(gpio_manager_t* manager, 
                                      uint8 channel_idx, 
                                      boolean value) {
    if (manager == NULL || channel_idx >= manager->channel_count) {
        return;
    }
    
    gpio_channel_t* ch = &manager->channels[channel_idx];
    
    if (ch->enabled && ch->type == GPIO_TYPE_DIGITAL_OUT) {
        if (ch->invert) {
            value = !value;
        }
        digitalWrite(ch->gpio_pin, value);
    }
}

boolean gpio_manager_get_digital_input(gpio_manager_t* manager, 
                                        uint8 channel_idx) {
    if (manager == NULL || channel_idx >= manager->channel_count) {
        return false;
    }
    
    gpio_channel_t* ch = &manager->channels[channel_idx];
    
    if (ch->enabled && ch->type == GPIO_TYPE_DIGITAL_IN) {
        boolean value = digitalRead(ch->gpio_pin);
        
        if (ch->invert) {
            value = !value;
        }
        
        return value;
    }
    
    return false;
}

void gpio_manager_set_analog_output(gpio_manager_t* manager, 
                                     uint8 channel_idx, 
                                     uint16 value) {
    if (manager == NULL || channel_idx >= manager->channel_count) {
        return;
    }
    
    gpio_channel_t* ch = &manager->channels[channel_idx];
    
    if (ch->enabled && ch->type == GPIO_TYPE_ANALOG_OUT) {
        // ESP32-S3 лишён DAC: значение 0-1023 обращаем в скважность PWM
        uint32 duty = ((uint32)value * GPIO_PWM_MAX_DUTY) / GPIO_ANALOG_MAX_VALUE;
        if (duty > GPIO_PWM_MAX_DUTY) {
            duty = GPIO_PWM_MAX_DUTY;
        }
        if (ch->invert) {
            duty = GPIO_PWM_MAX_DUTY - duty;
        }
        ledcWrite(ch->ledc_channel, duty);
    }
}

uint16 gpio_manager_get_analog_input(gpio_manager_t* manager, 
                                      uint8 channel_idx) {
    if (manager == NULL || channel_idx >= manager->channel_count) {
        return 0;
    }
    
    gpio_channel_t* ch = &manager->channels[channel_idx];
    
    if (ch->enabled && ch->type == GPIO_TYPE_ANALOG_IN) {
        return analogRead(ch->gpio_pin);  // 0-4095 (12-bit)
    }
    
    return 0;
}

int32 gpio_manager_get_encoder_count(gpio_manager_t* manager, 
                                      uint8 encoder_idx) {
    if (manager == NULL || encoder_idx >= manager->encoder_count) {
        return 0;
    }
    
    return g_encoder_counters[encoder_idx];
}

void gpio_manager_reset_encoder(gpio_manager_t* manager, 
                                 uint8 encoder_idx) {
    if (manager == NULL || encoder_idx >= manager->encoder_count) {
        return;
    }
    
    g_encoder_counters[encoder_idx] = 0;
}

// ============================================================================
// Statistics
// ============================================================================

void gpio_manager_get_stats(gpio_manager_t* manager, 
                            uint32* updates, 
                            uint32* errors) {
    if (manager == NULL) {
        return;
    }
    
    if (updates != NULL) {
        *updates = manager->update_count;
    }
    
    if (errors != NULL) {
        *errors = manager->errors;
    }
}

// ============================================================================
// MCP23S17 IO Expansion
// ============================================================================

boolean gpio_manager_init_mcp23s17(gpio_manager_t* manager,
                                    uint8 cs_pin,
                                    uint16 port_direction,
                                    uint16 port_pullup,
                                    uint16 port_invert) {
    if (manager == NULL) {
        Serial.println("[GPIO] MCP23S17 init: NULL manager");
        return false;
    }
    
    // Настройка конфигурации MCP23S17
    mcp23s17_config_t config;
    config.cs_pin = cs_pin;
    config.enabled = true;
    config.port_direction = port_direction;
    config.port_pullup = port_pullup;
    config.port_invert = port_invert;
    config.initialized = false;
    
    // Инициализация MCP23S17
    if (!mcp23s17_init(&manager->mcp23s17, &config)) {
        Serial.println("[GPIO] MCP23S17 init: Failed to initialize");
        return false;
    }
    
    manager->mcp23s17_enabled = true;
    
    Serial.printf("[GPIO] MCP23S17 initialized: CS=%d, Direction=0x%04X, Pullup=0x%04X, Invert=0x%04X\n",
                  cs_pin, port_direction, port_pullup, port_invert);
    
    // Автоматическое сохранение конфигурации в Flash после инициализации MCP23S17
    gpio_manager_save_to_flash(manager);
    
    return true;
}

void gpio_manager_update_mcp23s17(gpio_manager_t* manager) {
    if (manager == NULL || !manager->mcp23s17_enabled) {
        return;
    }
    
    // Обновление состояния MCP23S17 (для edge detection входов)
    mcp23s17_update(&manager->mcp23s17);
}

// USB keyboard test functions removed - use working TinyUSB implementation

boolean gpio_manager_test_keyboard_gpio(gpio_manager_t* manager, uint8 data_pin, uint8 clock_pin) {
    (void)manager;  // Пока не используется
    
    Serial.println();
    Serial.println("========================================");
    Serial.println("GPIO Keyboard Test");
    Serial.println("========================================");
    Serial.printf("Data pin: GPIO %d\n", data_pin);
    Serial.printf("Clock pin: GPIO %d\n", clock_pin);
    Serial.println();
    Serial.println("Reading keyboard pins (press keys to test)...");
    Serial.println("Press Ctrl+C to stop");
    Serial.println("----------------------------------------");
    
    // Настраиваем пины как входы с подтяжкой вверх
    pinMode(data_pin, INPUT_PULLUP);
    pinMode(clock_pin, INPUT_PULLUP);
    
    uint32_t start_time = millis();
    uint32_t last_data_change = 0;
    uint32_t last_clock_change = 0;
    int last_data_state = digitalRead(data_pin);
    int last_clock_state = digitalRead(clock_pin);
    uint32_t data_transitions = 0;
    uint32_t clock_transitions = 0;
    
    Serial.printf("Initial state - Data: %s, Clock: %s\n",
                  last_data_state == HIGH ? "HIGH" : "LOW",
                  last_clock_state == HIGH ? "HIGH" : "LOW");
    Serial.println();
    
    // Читаем пины в течение 10 секунд
    while (millis() - start_time < 10000) {
        int data_state = digitalRead(data_pin);
        int clock_state = digitalRead(clock_pin);
        
        // Отслеживаем изменения на data пине
        if (data_state != last_data_state) {
            last_data_change = millis();
            data_transitions++;
            Serial.printf("[%lu ms] Data pin changed: %s -> %s\n",
                         last_data_change - start_time,
                         last_data_state == HIGH ? "HIGH" : "LOW",
                         data_state == HIGH ? "HIGH" : "LOW");
            last_data_state = data_state;
        }
        
        // Отслеживаем изменения на clock пине
        if (clock_state != last_clock_state) {
            last_clock_change = millis();
            clock_transitions++;
            Serial.printf("[%lu ms] Clock pin changed: %s -> %s\n",
                         last_clock_change - start_time,
                         last_clock_state == HIGH ? "HIGH" : "LOW",
                         clock_state == HIGH ? "HIGH" : "LOW");
            last_clock_state = clock_state;
        }
        
        delay(10);  // Небольшая задержка для снижения нагрузки
    }
    
    Serial.println("----------------------------------------");
    Serial.println("Test Results:");
    Serial.printf("  Data pin transitions: %lu\n", data_transitions);
    Serial.printf("  Clock pin transitions: %lu\n", clock_transitions);
    Serial.printf("  Final state - Data: %s, Clock: %s\n",
                  last_data_state == HIGH ? "HIGH" : "LOW",
                  last_clock_state == HIGH ? "HIGH" : "LOW");
    
    if (data_transitions > 0 || clock_transitions > 0) {
        Serial.println("✓ Keyboard activity detected!");
    } else {
        Serial.println("⚠ No keyboard activity detected (check connections)");
    }
    
    Serial.println("========================================");
    Serial.println();
    
    return (data_transitions > 0 || clock_transitions > 0);
}

// USB keyboard validation function removed - use working TinyUSB implementation
