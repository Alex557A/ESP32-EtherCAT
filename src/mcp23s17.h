/*
 * MCP23S17 SPI GPIO Expander Driver - Header
 * 
 * Phase 5, Week 8, Day 53-55
 * ESP32 EtherCAT Slave v4
 * 
 * MCP23S17 - 16-bit SPI GPIO Expander
 * - 16 GPIO портов (8 портов A, 8 портов B)
 * - Вход/выход настраивается индивидуально
 * - SPI интерфейс (до 10 MHz)
 */

#ifndef MCP23S17_H
#define MCP23S17_H

#include "ethercat_type.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Constants
// ============================================================================

#define MCP23S17_MAX_PORTS       16  /**< Максимум портов (8 портов A + 8 портов B) */
#define MCP23S17_PORT_A          0   /**< Порт A (0-7) */
#define MCP23S17_PORT_B          1   /**< Порт B (8-15) */

// MCP23S17 Register Addresses
#define MCP23S17_REG_IODIRA      0x00  // I/O Direction Register A
#define MCP23S17_REG_IODIRB      0x01  // I/O Direction Register B
#define MCP23S17_REG_IPOLA       0x02  // Input Polarity Register A
#define MCP23S17_REG_IPOLB       0x03  // Input Polarity Register B
#define MCP23S17_REG_GPINTENA    0x04  // Interrupt-on-Change Control Register A
#define MCP23S17_REG_GPINTENB    0x05  // Interrupt-on-Change Control Register B
#define MCP23S17_REG_DEFVALA     0x06  // Default Compare Register A
#define MCP23S17_REG_DEFVALB     0x07  // Default Compare Register B
#define MCP23S17_REG_INTCONA     0x08  // Interrupt Control Register A
#define MCP23S17_REG_INTCONB     0x09  // Interrupt Control Register B
#define MCP23S17_REG_IOCON       0x0A  // Configuration Register (shared)
#define MCP23S17_REG_GPPUA       0x0C  // Pull-up Resistor Register A
#define MCP23S17_REG_GPPUB       0x0D  // Pull-up Resistor Register B
#define MCP23S17_REG_INTFA       0x0E  // Interrupt Flag Register A
#define MCP23S17_REG_INTFB       0x0F  // Interrupt Flag Register B
#define MCP23S17_REG_INTCAPA     0x10  // Interrupt Capture Register A
#define MCP23S17_REG_INTCAPB     0x11  // Interrupt Capture Register B
#define MCP23S17_REG_GPIOA       0x12  // GPIO Register A
#define MCP23S17_REG_GPIOB       0x13  // GPIO Register B
#define MCP23S17_REG_OLATA       0x14  // Output Latch Register A
#define MCP23S17_REG_OLATB       0x15  // Output Latch Register B

// MCP23S17 SPI Commands
#define MCP23S17_CMD_WRITE       0x40  // Write command
#define MCP23S17_CMD_READ        0x41  // Read command

// IOCON Register bits
#define MCP23S17_IOCON_BANK      0x80  // Bank addressing mode
#define MCP23S17_IOCON_MIRROR    0x40  // INT pin mirror
#define MCP23S17_IOCON_SEQOP     0x20  // Sequential operation
#define MCP23S17_IOCON_DISSLW    0x10  // Slew rate control
#define MCP23S17_IOCON_ODR       0x08  // Open-drain output
#define MCP23S17_IOCON_INTPOL    0x04  // INT pin polarity
#define MCP23S17_IOCON_HAEN      0x08  // Hardware address enable (MCP23S17 only)

// ============================================================================
// Configuration Structure
// ============================================================================

/**
 * @brief Конфигурация MCP23S17
 */
typedef struct {
    uint8 cs_pin;              /**< Chip Select pin (SPI Bus 2) */
    boolean enabled;           /**< Включен/выключен */
    uint16 port_direction;     /**< Направление портов (0=выход, 1=вход) - 16 бит */
    uint16 port_pullup;        /**< Pull-up резисторы (0=отключен, 1=включен) - 16 бит */
    uint16 port_invert;        /**< Инверсия логики (0=нормальная, 1=инвертированная) - 16 бит */
    boolean initialized;       /**< Инициализирован ли модуль */
} mcp23s17_config_t;

/**
 * @brief Состояние MCP23S17 для edge detection
 */
typedef struct {
    mcp23s17_config_t config;
    uint16 current_state;      /**< Текущее состояние всех портов */
    uint16 previous_state;     /**< Предыдущее состояние для edge detection */
} mcp23s17_state_t;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Инициализация MCP23S17
 * @param state Указатель на структуру состояния
 * @param config Указатель на конфигурацию
 * @return true если успешно, false в противном случае
 */
boolean mcp23s17_init(mcp23s17_state_t* state, const mcp23s17_config_t* config);

/**
 * @brief Настройка направления портов
 * @param state Указатель на структуру состояния
 * @param direction Направление (0=выход, 1=вход) - 16 бит
 * @return true если успешно
 */
boolean mcp23s17_set_direction(mcp23s17_state_t* state, uint16 direction);

/**
 * @brief Настройка pull-up резисторов
 * @param state Указатель на структуру состояния
 * @param pullup Pull-up (0=отключен, 1=включен) - 16 бит
 * @return true если успешно
 */
boolean mcp23s17_set_pullup(mcp23s17_state_t* state, uint16 pullup);

/**
 * @brief Настройка инверсии логики
 * @param state Указатель на структуру состояния
 * @param invert Инверсия (0=нормальная, 1=инвертированная) - 16 бит
 * @return true если успешно
 */
boolean mcp23s17_set_invert(mcp23s17_state_t* state, uint16 invert);

/**
 * @brief Чтение состояния всех портов
 * @param state Указатель на структуру состояния
 * @return 16-bit значение состояния портов
 */
uint16 mcp23s17_read_port(mcp23s17_state_t* state);

/**
 * @brief Запись значения во все порты
 * @param state Указатель на структуру состояния
 * @param value 16-bit значение для записи
 * @return true если успешно
 */
boolean mcp23s17_write_port(mcp23s17_state_t* state, uint16 value);

/**
 * @brief Чтение состояния отдельного пина
 * @param state Указатель на структуру состояния
 * @param pin Номер пина (0-15)
 * @return true если пин установлен, false в противном случае
 */
boolean mcp23s17_read_pin(mcp23s17_state_t* state, uint8 pin);

/**
 * @brief Запись значения в отдельный пин
 * @param state Указатель на структуру состояния
 * @param pin Номер пина (0-15)
 * @param value Значение (true=HIGH, false=LOW)
 * @return true если успешно
 */
boolean mcp23s17_write_pin(mcp23s17_state_t* state, uint8 pin, boolean value);

/**
 * @brief Обновление состояния (для edge detection)
 * @param state Указатель на структуру состояния
 */
void mcp23s17_update(mcp23s17_state_t* state);

/**
 * @brief Получение edge detection для пина
 * @param state Указатель на структуру состояния
 * @param pin Номер пина (0-15)
 * @return 1 если нажатие (0→1), -1 если отпускание (1→0), 0 если нет изменений
 */
int8 mcp23s17_get_edge(mcp23s17_state_t* state, uint8 pin);

/**
 * @brief Деинициализация MCP23S17
 * @param state Указатель на структуру состояния
 */
void mcp23s17_deinit(mcp23s17_state_t* state);

/**
 * @brief Инициализация SPI Bus 2 (вызывается из main.cpp)
 * @param sck_pin SCK pin для SPI Bus 2
 * @param miso_pin MISO pin для SPI Bus 2
 * @param mosi_pin MOSI pin для SPI Bus 2
 */
void mcp23s17_spi2_init(uint8_t sck_pin, uint8_t miso_pin, uint8_t mosi_pin);

#ifdef __cplusplus
}
#endif

#endif // MCP23S17_H
