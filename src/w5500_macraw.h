/*
 * W5500 MACRAW Driver
 * Raw Ethernet frame processing для EtherCAT
 * 
 * Phase 1, Week 3, Day 15-16
 * ESP32 EtherCAT Slave v4
 */

#ifndef W5500_MACRAW_H
#define W5500_MACRAW_H

#include "ethercat_type.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// W5500 Register Definitions
// ============================================================================

// Common Registers
#define W5500_MR        0x0000  /**< Mode Register */
#define W5500_GAR       0x0001  /**< Gateway Address */
#define W5500_SUBR      0x0005  /**< Subnet Mask */
#define W5500_SHAR      0x0009  /**< Source Hardware Address (MAC) */
#define W5500_SIPR      0x000F  /**< Source IP Address */
#define W5500_PHYCFGR   0x002E  /**< PHY Configuration */

// Socket Registers (per socket, base 0x0000 + offset)
#define W5500_Sn_MR     0x0000  /**< Socket n Mode Register */
#define W5500_Sn_CR     0x0001  /**< Socket n Command Register */
#define W5500_Sn_IR     0x0002  /**< Socket n Interrupt Register */
#define W5500_Sn_SR     0x0003  /**< Socket n Status Register */
#define W5500_Sn_RXBUF_SIZE 0x001E  /**< Socket n RX Buffer Size */
#define W5500_Sn_TXBUF_SIZE 0x001F  /**< Socket n TX Buffer Size */
#define W5500_Sn_TX_FSR 0x0020  /**< Socket n TX Free Size */
#define W5500_Sn_TX_WR  0x0024  /**< Socket n TX Write Pointer */
#define W5500_Sn_RX_RSR 0x0026  /**< Socket n RX Received Size */
#define W5500_Sn_RX_RD  0x0028  /**< Socket n RX Read Pointer */

// Socket Mode Register values
#define W5500_Sn_MR_MACRAW  0x04  /**< MACRAW mode */

// Socket Command Register values
#define W5500_Sn_CR_OPEN    0x01  /**< Open socket */
#define W5500_Sn_CR_CLOSE   0x10  /**< Close socket */
#define W5500_Sn_CR_SEND    0x20  /**< Send data */
#define W5500_Sn_CR_RECV    0x40  /**< Receive data */

// Socket Status Register values
#define W5500_Sn_SR_CLOSED  0x00  /**< Closed */
#define W5500_Sn_SR_MACRAW  0x42  /**< MACRAW mode */

// Block Select Bits (BSB) - 5-bit values, будут сдвинуты << 3 в control byte
#define W5500_BSB_COMMON    0x00  /**< Common registers (BSB=0b00000) */
#define W5500_BSB_S0_REG    0x01  /**< Socket 0 registers (BSB=0b00001) */
#define W5500_BSB_S0_TX     0x02  /**< Socket 0 TX buffer (BSB=0b00010) */
#define W5500_BSB_S0_RX     0x03  /**< Socket 0 RX buffer (BSB=0b00011) */

// SPI Frame Format
#define W5500_SPI_READ      0x00  /**< Read operation */
#define W5500_SPI_WRITE     0x04  /**< Write operation */

// ============================================================================
// W5500 MACRAW Structure
// ============================================================================

typedef struct {
    uint8_t cs_pin;           /**< Chip Select pin */
    uint8_t socket;           /**< Socket number (обычно 0) */
    uint32_t spi_speed;       /**< SPI clock speed (Hz) */
    boolean initialized;      /**< Флаг инициализации */
    uint8_t mac[6];           /**< MAC address */
    
} w5500_macraw_t;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Инициализация W5500 в MACRAW режиме
 * @param w5500 Указатель на W5500 структуру
 * @param cs_pin Chip Select pin
 * @param rst_pin Reset pin (255 = не используется)
 * @param mac MAC address (6 bytes)
 * @param spi_speed SPI speed в Hz (обычно 40000000)
 * @return true если успешно
 */
boolean w5500_macraw_init(w5500_macraw_t* w5500, uint8_t cs_pin, uint8_t rst_pin,
                          const uint8_t* mac, uint32_t spi_speed);

/**
 * @brief Открыть socket в MACRAW режиме
 * @param w5500 Указатель на W5500 структуру
 * @return true если успешно
 */
boolean w5500_macraw_open_socket(w5500_macraw_t* w5500);

/**
 * @brief Закрыть socket
 * @param w5500 Указатель на W5500 структуру
 */
void w5500_macraw_close_socket(w5500_macraw_t* w5500);

/**
 * @brief Отправить Ethernet фрейм
 * @param w5500 Указатель на W5500 структуру
 * @param frame Указатель на фрейм
 * @param length Длина фрейма
 * @return true если успешно
 */
boolean w5500_macraw_send(w5500_macraw_t* w5500, const uint8_t* frame, uint16_t length);

/**
 * @brief Принять Ethernet фрейм
 * @param w5500 Указатель на W5500 структуру
 * @param frame Буфер для фрейма
 * @param max_length Максимальная длина буфера
 * @return Длина принятого фрейма (0 если нет данных)
 */
uint16_t w5500_macraw_receive(w5500_macraw_t* w5500, uint8_t* frame, uint16_t max_length);

#ifdef __cplusplus
}
#endif

#endif // W5500_MACRAW_H
