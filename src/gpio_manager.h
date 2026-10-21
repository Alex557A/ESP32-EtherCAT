/*
 * GPIO Manager
 * Управление Digital/Analog I/O и Encoders через PDO
 * 
 * Phase 5, Week 8, Day 51-56
 * ESP32 EtherCAT Slave v4
 */

#ifndef GPIO_MANAGER_H
#define GPIO_MANAGER_H

#include "ethercat_type.h"
#include "usb_keyboard.h"
#include "mcp23s17.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration Limits
// ============================================================================

#define GPIO_MAX_CHANNELS    32   /**< Максимум I/O каналов */
#define GPIO_MAX_ENCODERS    8    /**< Максимум энкодеров */

// ============================================================================
// I/O Types
// ============================================================================

typedef enum {
    GPIO_TYPE_DISABLED = 0,       /**< Отключен */
    GPIO_TYPE_DIGITAL_IN = 1,     /**< Цифровой вход */
    GPIO_TYPE_DIGITAL_OUT = 2,    /**< Цифровой выход */
    GPIO_TYPE_ANALOG_IN = 3,      /**< Аналоговый вход (ADC) */
    GPIO_TYPE_ANALOG_OUT = 4,     /**< Аналоговый выход (DAC/PWM) */
    GPIO_TYPE_USB_KEYBOARD = 5,   /**< USB HID Keyboard (ESP32-S3 only) */
    GPIO_TYPE_MCP23S17 = 6        /**< MCP23S17 IO Expansion */
} gpio_type_t;

// ============================================================================
// I/O Channel Structure
// ============================================================================

typedef struct {
    boolean enabled;              /**< Включен/выключен */
    gpio_type_t type;             /**< Тип канала */
    uint8 gpio_pin;               /**< Номер GPIO pin (для USB Keyboard и MCP23S17 не используется) */
    uint8 pdo_byte;               /**< PDO byte offset */
    uint8 pdo_bit;                /**< PDO bit offset (для digital) */
    boolean invert;               /**< Инверсия сигнала */
    char name[16];                /**< Имя канала */
    char comment[128];            /**< Комментарий */
    
    // USB Keyboard специфичные поля (если type == GPIO_TYPE_USB_KEYBOARD)
    uint8 usb_key_code;           /**< USB HID key code */
    uint8 usb_modifiers;          /**< Modifiers (bitmask: Ctrl, Alt, Shift, Win) */
    
    // MCP23S17 специфичные поля (если type == GPIO_TYPE_MCP23S17)
    uint8 mcp23s17_port;         /**< Порт MCP23S17 (0-15) */
    
    // LEDC/PWM специфичные поля (если type == GPIO_TYPE_ANALOG_OUT)
    uint8 ledc_channel;          /**< LEDC канал для PWM (0-7) */
} gpio_channel_t;

// ============================================================================
// Encoder Structure
// ============================================================================

typedef struct {
    boolean enabled;              /**< Включен/выключен */
    uint8 pin_a;                  /**< GPIO для сигнала A */
    uint8 pin_b;                  /**< GPIO для сигнала B */
    uint8 pdo_byte_start;         /**< PDO byte offset (4 bytes для int32) */
    int32 counter;                /**< Текущий счетчик */
    char name[16];                /**< Имя энкодера */
    char comment[128];            /**< Комментарий */
} gpio_encoder_t;

// ============================================================================
// GPIO Manager Structure
// ============================================================================

typedef struct {
    // I/O Channels
    gpio_channel_t channels[GPIO_MAX_CHANNELS];
    uint8 channel_count;
    
    // Encoders
    gpio_encoder_t encoders[GPIO_MAX_ENCODERS];
    uint8 encoder_count;
    
    // IO Expansion через MCP23S17
    mcp23s17_state_t mcp23s17;    /**< MCP23S17 состояние */
    boolean mcp23s17_enabled;     /**< MCP23S17 включен? */
    
    // Statistics
    uint32 update_count;          /**< Счетчик обновлений */
    uint32 errors;                /**< Счетчик ошибок */
    
} gpio_manager_t;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Инициализация GPIO Manager
 * @param manager Указатель на GPIO Manager структуру
 * @return true если успешно
 */
boolean gpio_manager_init(gpio_manager_t* manager);

/**
 * @brief Добавить I/O канал
 * @param manager Указатель на GPIO Manager
 * @param channel Указатель на конфигурацию канала
 * @return Index канала или -1 если ошибка
 */
int8 gpio_manager_add_channel(gpio_manager_t* manager, const gpio_channel_t* channel);

/**
 * @brief Создать или обновить I/O канал по индексу
 * @param manager Указатель на GPIO Manager
 * @param index Индекс (0..GPIO_MAX_CHANNELS-1). Если index == channel_count, добавит новый
 * @param channel Указатель на конфигурацию канала
 * @return true если успешно
 */
boolean gpio_manager_set_channel(gpio_manager_t* manager, uint8 index, const gpio_channel_t* channel);

/**
 * @brief Удалить I/O канал по индексу (сдвиг массива)
 * @param manager Указатель на GPIO Manager
 * @param index Индекс канала
 * @return true если успешно
 */
boolean gpio_manager_delete_channel(gpio_manager_t* manager, uint8 index);

/**
 * @brief Добавить энкодер
 * @param manager Указатель на GPIO Manager
 * @param encoder Указатель на конфигурацию энкодера
 * @return Index энкодера или -1 если ошибка
 */
int8 gpio_manager_add_encoder(gpio_manager_t* manager, const gpio_encoder_t* encoder);

/**
 * @brief Создать или обновить энкодер по индексу
 * @param manager Указатель на GPIO Manager
 * @param index Индекс (0..GPIO_MAX_ENCODERS-1). Если index == encoder_count, добавит новый
 * @param encoder Указатель на конфигурацию энкодера
 * @return true если успешно
 */
boolean gpio_manager_set_encoder(gpio_manager_t* manager, uint8 index, const gpio_encoder_t* encoder);

/**
 * @brief Удалить энкодер по индексу (сдвиг массива)
 * @param manager Указатель на GPIO Manager
 * @param index Индекс энкодера
 * @return true если успешно
 */
boolean gpio_manager_delete_encoder(gpio_manager_t* manager, uint8 index);

/**
 * @brief Обновить outputs из PDO
 * @param manager Указатель на GPIO Manager
 * @param pdo_output Указатель на PDO Output буфер (Master → Slave)
 * @param size Размер PDO Output
 */
void gpio_manager_update_outputs(gpio_manager_t* manager, 
                                  const uint8* pdo_output, 
                                  uint16 size);

/**
 * @brief Обновить inputs в PDO
 * @param manager Указатель на GPIO Manager
 * @param pdo_input Указатель на PDO Input буфер (Slave → Master)
 * @param size Размер PDO Input
 */
void gpio_manager_update_inputs(gpio_manager_t* manager, 
                                 uint8* pdo_input, 
                                 uint16 size);

/**
 * @brief Инициализация MCP23S17 IO Expansion
 * @param manager Указатель на GPIO Manager
 * @param cs_pin Chip Select pin для MCP23S17
 * @param port_direction Направление портов (0=выход, 1=вход) - 16 бит
 * @param port_pullup Pull-up резисторы (0=отключен, 1=включен) - 16 бит
 * @param port_invert Инверсия логики (0=нормальная, 1=инвертированная) - 16 бит
 * @return true если успешно
 */
boolean gpio_manager_init_mcp23s17(gpio_manager_t* manager,
                                    uint8 cs_pin,
                                    uint16 port_direction,
                                    uint16 port_pullup,
                                    uint16 port_invert);

/**
 * @brief Обновление MCP23S17 (чтение/запись портов)
 * @param manager Указатель на GPIO Manager
 */
void gpio_manager_update_mcp23s17(gpio_manager_t* manager);

/**
 * @brief Сохранение конфигурации GPIO Manager в Flash (EEPROM)
 * @param manager Указатель на GPIO Manager
 * @return true если успешно
 */
boolean gpio_manager_save_to_flash(gpio_manager_t* manager);

/**
 * @brief Загрузка конфигурации GPIO Manager из Flash (EEPROM)
 * @param manager Указатель на GPIO Manager
 * @return true если успешно загружена конфигурация
 */
boolean gpio_manager_load_from_flash(gpio_manager_t* manager);

/**
 * @brief Установить digital output
 * @param manager Указатель на GPIO Manager
 * @param channel_idx Index канала
 * @param value true = HIGH, false = LOW
 */
void gpio_manager_set_digital_output(gpio_manager_t* manager, 
                                      uint8 channel_idx, 
                                      boolean value);

/**
 * @brief Прочитать digital input
 * @param manager Указатель на GPIO Manager
 * @param channel_idx Index канала
 * @return true = HIGH, false = LOW
 */
boolean gpio_manager_get_digital_input(gpio_manager_t* manager, 
                                        uint8 channel_idx);

/**
 * @brief Установить analog output (DAC/PWM)
 * @param manager Указатель на GPIO Manager
 * @param channel_idx Index канала
 * @param value Значение (0-255 для DAC, 0-1023 для PWM)
 */
void gpio_manager_set_analog_output(gpio_manager_t* manager, 
                                     uint8 channel_idx, 
                                     uint16 value);

/**
 * @brief Прочитать analog input (ADC)
 * @param manager Указатель на GPIO Manager
 * @param channel_idx Index канала
 * @return Значение (0-4095 для 12-bit ADC)
 */
uint16 gpio_manager_get_analog_input(gpio_manager_t* manager, 
                                      uint8 channel_idx);

// USB keyboard test functions removed - use working TinyUSB implementation

/**
 * @brief Тест клавиатуры на GPIO пинах (PS/2 или матричная)
 * @param manager Указатель на GPIO Manager
 * @param data_pin GPIO пин для данных (например, 14)
 * @param clock_pin GPIO пин для тактового сигнала (например, 13)
 * @return true если тест успешен
 * 
 * Читает нажатия клавиш с клавиатуры, подключенной к указанным пинам,
 * и выводит информацию о нажатиях в Serial Monitor.
 */
boolean gpio_manager_test_keyboard_gpio(gpio_manager_t* manager, uint8 data_pin, uint8 clock_pin);

/**
 * @brief Получить значение энкодера
 * @param manager Указатель на GPIO Manager
 * @param encoder_idx Index энкодера
 * @return Текущий счетчик
 */
int32 gpio_manager_get_encoder_count(gpio_manager_t* manager, 
                                      uint8 encoder_idx);

/**
 * @brief Сбросить энкодер
 * @param manager Указатель на GPIO Manager
 * @param encoder_idx Index энкодера
 */
void gpio_manager_reset_encoder(gpio_manager_t* manager, 
                                 uint8 encoder_idx);

/**
 * @brief Получить статистику
 * @param manager Указатель на GPIO Manager
 * @param updates Указатель для счетчика обновлений (может быть NULL)
 * @param errors Указатель для счетчика ошибок (может быть NULL)
 */
void gpio_manager_get_stats(gpio_manager_t* manager, 
                            uint32* updates, 
                            uint32* errors);

#ifdef __cplusplus
}
#endif

#endif // GPIO_MANAGER_H
