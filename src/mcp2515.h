/*
 * MCP2515 CAN Controller Driver
 * SPI интерфейс для MCP2515 (SPI Bus 2)
 * 
 * Phase 5, Week 9, Day 57-58
 * ESP32 EtherCAT Slave v4
 */

#ifndef MCP2515_H
#define MCP2515_H

#include "ethercat_type.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// MCP2515 Registers
// ============================================================================

// CAN Control Registers
#define MCP2515_REG_CNF1    0x2A
#define MCP2515_REG_CNF2    0x29
#define MCP2515_REG_CNF3    0x28
#define MCP2515_REG_CANCTRL 0x0F
#define MCP2515_REG_CANSTAT 0x0E
#define MCP2515_REG_CANINTF 0x2C
#define MCP2515_REG_CANINTE 0x2B

// CAN Control Register bits
#define MCP2515_CANCTRL_REQOP_MASK  0xE0
#define MCP2515_CANCTRL_REQOP_NORMAL 0x00
#define MCP2515_CANCTRL_REQOP_SLEEP  0x20
#define MCP2515_CANCTRL_REQOP_LOOPBACK 0x40
#define MCP2515_CANCTRL_REQOP_LISTEN  0x60
#define MCP2515_CANCTRL_REQOP_CONFIG  0x80

// CAN Interrupt Flags
#define MCP2515_CANINTF_RX0IF  0x01
#define MCP2515_CANINTF_RX1IF  0x02
#define MCP2515_CANINTF_TX0IF  0x04
#define MCP2515_CANINTF_TX1IF  0x08
#define MCP2515_CANINTF_TX2IF  0x10
#define MCP2515_CANINTF_ERRIF  0x20
#define MCP2515_CANINTF_WAKIF  0x40
#define MCP2515_CANINTF_MERRF  0x80

// TX Buffer Registers
#define MCP2515_REG_TXB0CTRL  0x30
#define MCP2515_REG_TXB0SIDH  0x31
#define MCP2515_REG_TXB0DLC   0x35
#define MCP2515_REG_TXB0D0    0x36

// RX Buffer Registers
#define MCP2515_REG_RXB0CTRL  0x60
#define MCP2515_REG_RXB0SIDH  0x61
#define MCP2515_REG_RXB0DLC   0x65
#define MCP2515_REG_RXB0D0    0x66

// SPI Commands
#define MCP2515_CMD_RESET     0xC0
#define MCP2515_CMD_READ      0x03
#define MCP2515_CMD_WRITE     0x02
#define MCP2515_CMD_RTS       0x80  // Request To Send
#define MCP2515_CMD_READ_STATUS 0xA0
#define MCP2515_CMD_RX_STATUS   0xB0
#define MCP2515_CMD_BIT_MODIFY   0x05

// ============================================================================
// CAN Frame Structure
// ============================================================================

typedef struct {
    uint32 id;        /**< CAN ID (11-bit или 29-bit) */
    uint8 rtr;        /**< Remote Transmission Request */
    uint8 ide;        /**< Extended ID flag */
    uint8 dlc;        /**< Data Length Code (0-8) */
    uint8 data[8];    /**< CAN data */
} can_frame_t;

// ============================================================================
// MCP2515 Structure
// ============================================================================

typedef struct {
    boolean initialized;   /**< Драйвер инициализирован? */
    uint8 cs_pin;         /**< Chip Select pin */
    uint8 int_pin;        /**< Interrupt pin */
    uint32 bitrate;       /**< CAN bitrate (bps) */
    boolean loopback_mode;  /**< Loopback mode для тестирования */
    
    // Statistics
    uint32 tx_count;      /**< Счетчик отправленных фреймов */
    uint32 rx_count;      /**< Счетчик принятых фреймов */
    uint32 tx_errors;     /**< Счетчик ошибок передачи */
    uint32 rx_errors;     /**< Счетчик ошибок приема */
    
} mcp2515_t;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Инициализация MCP2515
 * @param can Указатель на MCP2515 структуру
 * @param cs_pin Chip Select pin (SPI Bus 2)
 * @param int_pin Interrupt pin
 * @param bitrate CAN bitrate (bps)
 * @return true если успешно
 */
boolean mcp2515_init(mcp2515_t* can, uint8 cs_pin, uint8 int_pin, uint32 bitrate);

/**
 * @brief Настройка CAN bitrate
 * @param can Указатель на MCP2515 структуру
 * @param bitrate CAN bitrate (bps)
 * @return true если успешно
 */
boolean mcp2515_set_bitrate(mcp2515_t* can, uint32 bitrate);

/**
 * @brief Включение/выключение loopback mode
 * @param can Указатель на MCP2515 структуру
 * @param enable true для включения loopback
 * @return true если успешно
 */
boolean mcp2515_set_loopback(mcp2515_t* can, boolean enable);

/**
 * @brief Отправка CAN фрейма
 * @param can Указатель на MCP2515 структуру
 * @param frame Указатель на CAN фрейм
 * @return true если успешно
 */
boolean mcp2515_send(mcp2515_t* can, const can_frame_t* frame);

/**
 * @brief Прием CAN фрейма
 * @param can Указатель на MCP2515 структуру
 * @param frame Указатель на буфер для CAN фрейма
 * @return true если фрейм принят
 */
boolean mcp2515_receive(mcp2515_t* can, can_frame_t* frame);

/**
 * @brief Проверка наличия принятых фреймов
 * @param can Указатель на MCP2515 структуру
 * @return true если есть фреймы для чтения
 */
boolean mcp2515_available(mcp2515_t* can);

/**
 * @brief Получение статистики
 * @param can Указатель на MCP2515 структуру
 * @param tx_count Указатель на счетчик TX (может быть NULL)
 * @param rx_count Указатель на счетчик RX (может быть NULL)
 * @param tx_errors Указатель на счетчик TX errors (может быть NULL)
 * @param rx_errors Указатель на счетчик RX errors (может быть NULL)
 */
void mcp2515_get_stats(mcp2515_t* can, uint32* tx_count, uint32* rx_count,
                      uint32* tx_errors, uint32* rx_errors);

#ifdef __cplusplus
}
#endif

#endif // MCP2515_H
