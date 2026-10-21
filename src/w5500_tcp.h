/*
 * W5500 TCP/IP Driver для Telnet Server
 * 
 * Phase 3, Week 6, Day 37-38
 * ESP32 EtherCAT Slave v4
 * 
 * Используется Socket 1 для Telnet (параллельно с Socket 0 для EtherCAT)
 */

#ifndef W5500_TCP_H
#define W5500_TCP_H

#include "ethercat_type.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// W5500 TCP Register Definitions
// ============================================================================

// Socket Mode Register values
#define W5500_Sn_MR_TCP     0x01  /**< TCP mode */
#define W5500_Sn_MR_UDP     0x02  /**< UDP mode */

// Socket Command Register values
#define W5500_Sn_CR_OPEN    0x01  /**< Open socket */
#define W5500_Sn_CR_LISTEN  0x02  /**< Listen (TCP server) */
#define W5500_Sn_CR_CONNECT 0x04  /**< Connect (TCP client) */
#define W5500_Sn_CR_ACCEPT  0x05  /**< Accept connection (TCP server) */
#define W5500_Sn_CR_DISCON  0x08  /**< Disconnect */
#define W5500_Sn_CR_CLOSE   0x10  /**< Close socket */

// Socket Status Register values
#define W5500_Sn_SR_INIT        0x13  /**< TCP initialized */
#define W5500_Sn_SR_LISTEN      0x14  /**< TCP listening */
#define W5500_Sn_SR_SYN_RECV    0x15  /**< TCP SYN received (waiting for ACCEPT) */
#define W5500_Sn_SR_ESTABLISHED 0x17  /**< TCP connection established */
#define W5500_Sn_SR_CLOSE_WAIT  0x1C  /**< TCP connection closing */
#define W5500_Sn_SR_CLOSED      0x00  /**< Socket closed */

// Socket Interrupt Register bits
#define W5500_Sn_IR_CON     0x01  /**< Connection established */
#define W5500_Sn_IR_DISCON  0x02  /**< Disconnected */
#define W5500_Sn_IR_RECV    0x04  /**< Data received */
#define W5500_Sn_IR_TIMEOUT 0x08  /**< Timeout */
#define W5500_Sn_IR_SENDOK  0x10  /**< Send completed */

// Socket Register offsets (Sn_*). Адрес — внутри блока регистров сокета,
// а выбор Socket N происходит через BSB (см. W5500_BSB_S1_* ниже).
#define W5500_Sn_MR         0x0000  /**< Socket n Mode Register */
#define W5500_Sn_CR         0x0001  /**< Socket n Command Register */
#define W5500_Sn_IR         0x0002  /**< Socket n Interrupt Register */
#define W5500_Sn_SR         0x0003  /**< Socket n Status Register */
#define W5500_Sn_PORT       0x0004  /**< Socket n Port (2 bytes) */
#define W5500_Sn_TX_FSR     0x0020  /**< Socket n TX Free Size */
#define W5500_Sn_TX_WR      0x0024  /**< Socket n TX Write Pointer */
#define W5500_Sn_RX_RSR     0x0026  /**< Socket n RX Received Size */
#define W5500_Sn_RX_RD      0x0028  /**< Socket n RX Read Pointer */

// Socket Buffer BSB (Block Select Bits)
// Формула для W5500:
// - BSB_REG = 0x01 + socket*4
// - BSB_TX  = 0x02 + socket*4
// - BSB_RX  = 0x03 + socket*4
#define W5500_BSB_S1_REG    0x05  /**< Socket 1 registers */
#define W5500_BSB_S1_TX     0x06  /**< Socket 1 TX buffer */
#define W5500_BSB_S1_RX     0x07  /**< Socket 1 RX buffer */

// ============================================================================
// W5500 TCP Structure
// ============================================================================

typedef struct {
    uint8_t cs_pin;           /**< Chip Select pin */
    uint8_t socket;           /**< Socket number (1 для Telnet) */
    uint32_t spi_speed;       /**< SPI clock speed (Hz) */
    boolean initialized;      /**< Флаг инициализации */
    boolean listening;        /**< Флаг состояния LISTEN */
    boolean connected;        /**< Флаг соединения */
    uint8_t mac[6];           /**< MAC address */
    uint8_t ip[4];            /**< IP address */
    uint8_t subnet[4];        /**< Subnet mask */
    uint8_t gateway[4];       /**< Gateway address */
    uint16_t port;            /**< Local port */
    
} w5500_tcp_t;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Инициализация W5500 в TCP/IP режиме
 * @param w5500 Указатель на W5500 TCP структуру
 * @param cs_pin Chip Select pin
 * @param rst_pin Reset pin (255 если не используется)
 * @param mac MAC address (6 bytes)
 * @param ip IP address (4 bytes)
 * @param subnet Subnet mask (4 bytes)
 * @param gateway Gateway address (4 bytes)
 * @param spi_speed SPI speed в Hz
 * @return true если успешно
 */
boolean w5500_tcp_init(w5500_tcp_t* w5500, 
                       uint8_t cs_pin, 
                       uint8_t rst_pin,
                       const uint8_t* mac, 
                       const uint8_t* ip,
                       const uint8_t* subnet,
                       const uint8_t* gateway,
                       uint32_t spi_speed);

/**
 * @brief Открыть TCP socket в режиме сервера (LISTEN)
 * @param w5500 Указатель на W5500 TCP структуру
 * @param port Локальный порт (например, 23 для Telnet)
 * @return true если успешно
 */
boolean w5500_tcp_listen(w5500_tcp_t* w5500, uint16_t port);

/**
 * @brief Проверить наличие входящего соединения
 * @param w5500 Указатель на W5500 TCP структуру
 * @return true если есть новое соединение
 */
boolean w5500_tcp_check_connection(w5500_tcp_t* w5500);

/**
 * @brief Принять входящее соединение
 * @param w5500 Указатель на W5500 TCP структуру
 * @return true если соединение установлено
 */
boolean w5500_tcp_accept(w5500_tcp_t* w5500);

/**
 * @brief Проверить наличие данных для чтения
 * @param w5500 Указатель на W5500 TCP структуру
 * @return Количество байт доступных для чтения
 */
uint16_t w5500_tcp_available(w5500_tcp_t* w5500);

/**
 * @brief Прочитать данные из TCP socket
 * @param w5500 Указатель на W5500 TCP структуру
 * @param buffer Буфер для данных
 * @param max_len Максимальная длина буфера
 * @return Количество прочитанных байт
 */
uint16_t w5500_tcp_recv(w5500_tcp_t* w5500, uint8_t* buffer, uint16_t max_len);

/**
 * @brief Отправить данные через TCP socket
 * @param w5500 Указатель на W5500 TCP структуру
 * @param data Данные для отправки
 * @param len Длина данных
 * @return true если успешно
 */
boolean w5500_tcp_send(w5500_tcp_t* w5500, const uint8_t* data, uint16_t len);

/**
 * @brief Закрыть TCP соединение
 * @param w5500 Указатель на W5500 TCP структуру
 */
void w5500_tcp_disconnect(w5500_tcp_t* w5500);

/**
 * @brief Закрыть TCP socket
 * @param w5500 Указатель на W5500 TCP структуру
 */
void w5500_tcp_close(w5500_tcp_t* w5500);

#ifdef __cplusplus
}
#endif

#endif // W5500_TCP_H
