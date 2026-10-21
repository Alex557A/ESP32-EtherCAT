/*
 * Telnet Configuration Server
 * JSON-RPC протокол через Telnet
 * 
 * Phase 3, Week 6, Day 37-38
 * ESP32 EtherCAT Slave v4
 */

#ifndef TELNET_SERVER_H
#define TELNET_SERVER_H

#include "ethercat_type.h"
#include "w5500_tcp.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Telnet Server Structure
// ============================================================================

#define TELNET_SERVER_BUFFER_SIZE 512  /**< Размер буфера для команд */

// Используем void* для избежания циклических зависимостей
// ethercat_stack_t и ethercat_test_stats_t определены в других заголовочных файлах

typedef struct {
    w5500_tcp_t* w5500;                /**< Указатель на W5500 TCP */
    void* ethercat_stack;               /**< Указатель на EtherCAT Stack (для статистики) */
    void* test_stats;                   /**< Указатель на тестовую статистику */
    void* gpio_manager;                 /**< Указатель на GPIO Manager (для конфигурации) */
    void* stepgen;                      /**< Указатель на StepGen (STEP/DIR конфигурация) */
    void* espnow_bridge;                /**< Указатель на ESP-NOW Bridge (для proxy/discovery) */
    void* espnow_config_proxy;          /**< Указатель на ESP-NOW Config Proxy (кэш конфигураций) */
    boolean initialized;               /**< Флаг инициализации */
    boolean running;                   /**< Флаг работы */
    
    // Command buffer
    uint8_t rx_buffer[TELNET_SERVER_BUFFER_SIZE];  /**< Буфер для входящих команд */
    uint16_t rx_len;                   /**< Длина принятых данных */
    
    // Statistics
    uint32_t connections_count;        /**< Счетчик соединений */
    uint32_t commands_processed;        /**< Счетчик обработанных команд */
    uint32_t errors_count;             /**< Счетчик ошибок */
    
} telnet_server_t;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Инициализация Telnet сервера
 * @param server Указатель на Telnet сервер
 * @param w5500 Указатель на W5500 TCP (Socket 1)
 * @param ethercat_stack Указатель на EtherCAT Stack (для статистики, может быть NULL)
 * @param test_stats Указатель на тестовую статистику (может быть NULL)
 * @param gpio_manager Указатель на GPIO Manager (для конфигурации, может быть NULL)
 * @return true если успешно
 */
boolean telnet_server_init(telnet_server_t* server, w5500_tcp_t* w5500, 
                          void* ethercat_stack, void* test_stats, void* gpio_manager,
                          void* stepgen,
                          void* espnow_bridge, void* espnow_config_proxy);

/**
 * @brief Запуск Telnet сервера (начать прослушивание порта 23)
 * @param server Указатель на Telnet сервер
 * @return true если успешно
 */
boolean telnet_server_start(telnet_server_t* server);

/**
 * @brief Обработка Telnet сервера (вызывать в loop)
 * @param server Указатель на Telnet сервер
 */
void telnet_server_process(telnet_server_t* server);

/**
 * @brief Отправка ответа клиенту
 * @param server Указатель на Telnet сервер
 * @param response JSON-RPC ответ
 * @return true если успешно
 */
boolean telnet_server_send_response(telnet_server_t* server, const char* response);

/**
 * @brief Отправка ошибки клиенту
 * @param server Указатель на Telnet сервер
 * @param error_message Сообщение об ошибке
 * @param id ID запроса (для JSON-RPC) - int64_t для поддержки больших ID
 * @return true если успешно
 */
boolean telnet_server_send_error(telnet_server_t* server, const char* error_message, int64_t id);

#ifdef __cplusplus
}
#endif

#endif // TELNET_SERVER_H
