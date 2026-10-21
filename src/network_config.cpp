/*
 * Network Configuration - Implementation
 * 
 * Phase 3, Week 6, Day 36
 * ESP32 EtherCAT Slave v4
 */

#include "network_config.h"
#include <Arduino.h>
#include <string.h>

extern "C" {

// ============================================================================
// Default Network Settings
// ============================================================================

#define NETWORK_BASE_IP_0    192
#define NETWORK_BASE_IP_1    168
#define NETWORK_BASE_IP_2    20
#define NETWORK_DEFAULT_IP   1
#define NETWORK_DEFAULT_SUBNET_0  255
#define NETWORK_DEFAULT_SUBNET_1  255
#define NETWORK_DEFAULT_SUBNET_2  255
#define NETWORK_DEFAULT_SUBNET_3  0
#define NETWORK_DEFAULT_GATEWAY_0 192
#define NETWORK_DEFAULT_GATEWAY_1 168
#define NETWORK_DEFAULT_GATEWAY_2 20
#define NETWORK_DEFAULT_GATEWAY_3 1

// ============================================================================
// Initialization
// ============================================================================

boolean network_config_init(network_config_t* config, uint16_t station_address) {
    if (config == NULL) {
        return false;
    }
    
    memset(config, 0, sizeof(network_config_t));
    
    // Установка значений по умолчанию
    config->subnet[0] = NETWORK_DEFAULT_SUBNET_0;
    config->subnet[1] = NETWORK_DEFAULT_SUBNET_1;
    config->subnet[2] = NETWORK_DEFAULT_SUBNET_2;
    config->subnet[3] = NETWORK_DEFAULT_SUBNET_3;
    
    config->gateway[0] = NETWORK_DEFAULT_GATEWAY_0;
    config->gateway[1] = NETWORK_DEFAULT_GATEWAY_1;
    config->gateway[2] = NETWORK_DEFAULT_GATEWAY_2;
    config->gateway[3] = NETWORK_DEFAULT_GATEWAY_3;
    
    // Автоматическое назначение IP на основе Station Address
    network_config_auto_assign_ip(config, station_address);
    
    // Вычисление next_device_ip
    network_config_calculate_next_ip(config->ip, config->next_device_ip);
    
    Serial.printf("[Network] IP: %d.%d.%d.%d\n", 
                  config->ip[0], config->ip[1], config->ip[2], config->ip[3]);
    Serial.printf("[Network] Next device IP: %d.%d.%d.%d\n",
                  config->next_device_ip[0], config->next_device_ip[1],
                  config->next_device_ip[2], config->next_device_ip[3]);
    
    return true;
}

// ============================================================================
// Validation
// ============================================================================

boolean network_config_validate_ip(const uint8_t* ip) {
    if (ip == NULL) {
        return false;
    }
    
    // Проверка подсети 192.168.20.0/24
    if (ip[0] != NETWORK_BASE_IP_0 || 
        ip[1] != NETWORK_BASE_IP_1 || 
        ip[2] != NETWORK_BASE_IP_2) {
        return false;
    }
    
    // Последний октет должен быть в диапазоне 1-254
    if (ip[3] == 0 || ip[3] == 255) {
        return false;
    }
    
    return true;
}

// ============================================================================
// IP Assignment
// ============================================================================

void network_config_auto_assign_ip(network_config_t* config, uint16_t station_address) {
    if (config == NULL) {
        return;
    }
    
    config->ip[0] = NETWORK_BASE_IP_0;
    config->ip[1] = NETWORK_BASE_IP_1;
    config->ip[2] = NETWORK_BASE_IP_2;
    
    // IP = 192.168.20.X, где X = Station Address
    // Если Station Address = 0 или > 254, использовать адрес по умолчанию
    if (station_address > 0 && station_address <= 254) {
        config->ip[3] = (uint8_t)station_address;
        config->auto_assigned = true;
    } else {
        config->ip[3] = NETWORK_DEFAULT_IP;
        config->auto_assigned = false;
    }
}

void network_config_calculate_next_ip(const uint8_t* current_ip, uint8_t* next_ip) {
    if (current_ip == NULL || next_ip == NULL) {
        return;
    }
    
    memcpy(next_ip, current_ip, 4);
    
    // next_ip = current_ip + 1
    if (next_ip[3] < 254) {
        next_ip[3]++;
    } else {
        // Если достигнут максимум, возвращаемся к 1
        next_ip[3] = 1;
    }
}

} // extern "C"
