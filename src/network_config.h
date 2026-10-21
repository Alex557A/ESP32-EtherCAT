/*
 * Network Configuration
 * Назначение статических IP адресов для Telnet сети
 * 
 * Phase 3, Week 6, Day 36
 * ESP32 EtherCAT Slave v4
 */

#ifndef NETWORK_CONFIG_H
#define NETWORK_CONFIG_H

#include "ethercat_type.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Network Configuration Structure
// ============================================================================

typedef struct {
    uint8_t ip[4];              /**< IP адрес (192.168.20.X) */
    uint8_t subnet[4];          /**< Subnet mask (255.255.255.0) */
    uint8_t gateway[4];         /**< Gateway (192.168.20.1) */
    uint8_t next_device_ip[4];  /**< IP следующего устройства в цепочке */
    boolean auto_assigned;       /**< Флаг автоматического назначения */
} network_config_t;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Инициализация сетевой конфигурации
 * @param config Указатель на структуру конфигурации
 * @param station_address EtherCAT Station Address (для автоматического назначения)
 * @return true если успешно
 */
boolean network_config_init(network_config_t* config, uint16_t station_address);

/**
 * @brief Валидация IP адреса
 * @param ip IP адрес (4 bytes)
 * @return true если валиден
 */
boolean network_config_validate_ip(const uint8_t* ip);

/**
 * @brief Вычисление IP следующего устройства в цепочке
 * @param current_ip Текущий IP адрес
 * @param next_ip Указатель для следующего IP
 */
void network_config_calculate_next_ip(const uint8_t* current_ip, uint8_t* next_ip);

/**
 * @brief Автоматическое назначение IP на основе Station Address
 * @param config Указатель на структуру конфигурации
 * @param station_address EtherCAT Station Address
 */
void network_config_auto_assign_ip(network_config_t* config, uint16_t station_address);

#ifdef __cplusplus
}
#endif

#endif // NETWORK_CONFIG_H
