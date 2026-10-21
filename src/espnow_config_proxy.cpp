/*
 * ESP-NOW Configuration Proxy - Implementation
 * 
 * Phase 5, Week 9, Day 62-63
 * ESP32 EtherCAT Slave v4
 */

#include "espnow_config_proxy.h"
#include "espnow_bridge.h"
#include <Arduino.h>
#include <string.h>

extern "C" {

// JSON-RPC команда для получения конфигурации
#define CONFIG_CMD_GET_CONFIG "{\"jsonrpc\":\"2.0\",\"method\":\"get_config\",\"params\":{},\"id\":1}"

// ============================================================================
// Initialization
// ============================================================================

boolean espnow_config_proxy_init(espnow_config_proxy_t* proxy) {
    if (proxy == NULL) {
        return false;
    }
    
    memset(proxy, 0, sizeof(espnow_config_proxy_t));
    proxy->cache_valid = false;
    proxy->cache_last_refresh = 0;
    
    return true;
}

// ============================================================================
// Cache Management
// ============================================================================

boolean espnow_config_proxy_refresh_cache(espnow_config_proxy_t* proxy,
                                          espnow_bridge_t* bridge,
                                          const espnow_device_info_t* devices,
                                          uint8_t device_count) {
    if (proxy == NULL || bridge == NULL || devices == NULL) {
        return false;
    }
    
    Serial.printf("[ESP-NOW Proxy] Refreshing cache for %d devices...\n", device_count);
    
    uint8_t success_count = 0;
    
    for (uint8_t i = 0; i < device_count; i++) {
        const espnow_device_info_t* device = &devices[i];
        
        // Отправляем команду get_config
        uint8_t response[256];
        uint16_t response_len = sizeof(response);
        
        const char* cmd = CONFIG_CMD_GET_CONFIG;
        uint16_t cmd_len = strlen(cmd);
        
        if (espnow_send_config_command(bridge, device->mac, 
                                       (const uint8_t*)cmd, cmd_len,
                                       response, &response_len, 5000)) {
            // Сохраняем в кэш
            if (espnow_config_proxy_update_cache(proxy, device->mac, 
                                                 (const char*)response, response_len)) {
                success_count++;
                Serial.printf("[ESP-NOW Proxy] ✓ Cached config for %02X:%02X:%02X:%02X:%02X:%02X\n",
                             device->mac[0], device->mac[1], device->mac[2],
                             device->mac[3], device->mac[4], device->mac[5]);
            }
        } else {
            Serial.printf("[ESP-NOW Proxy] ✗ Failed to get config from %02X:%02X:%02X:%02X:%02X:%02X\n",
                         device->mac[0], device->mac[1], device->mac[2],
                         device->mac[3], device->mac[4], device->mac[5]);
        }
        
        delay(100);  // Небольшая задержка между запросами
    }
    
    proxy->cache_last_refresh = millis();
    proxy->cache_valid = (success_count > 0);
    
    Serial.printf("[ESP-NOW Proxy] Cache refresh complete: %d/%d devices\n", 
                 success_count, device_count);
    
    return (success_count > 0);
}

boolean espnow_config_proxy_get_cached_config(espnow_config_proxy_t* proxy,
                                               const uint8_t* mac,
                                               char* config_json,
                                               uint16_t* config_len) {
    if (proxy == NULL || mac == NULL) {
        return false;
    }
    
    // Поиск в кэше
    for (uint8_t i = 0; i < proxy->cache_count; i++) {
        cached_device_config_t* cached = &proxy->device_cache[i];
        
        if (memcmp(cached->mac, mac, 6) == 0 && cached->valid) {
            // Конфигурация найдена
            if (config_json != NULL && config_len != NULL) {
                if (cached->config_len <= *config_len) {
                    memcpy(config_json, cached->config_json, cached->config_len);
                    *config_len = cached->config_len;
                    return true;
                } else {
                    // Буфер слишком мал
                    *config_len = cached->config_len;
                    return false;
                }
            }
            return true;
        }
    }
    
    // Не найдено
    if (config_len != NULL) {
        *config_len = 0;
    }
    return false;
}

boolean espnow_config_proxy_update_cache(espnow_config_proxy_t* proxy,
                                         const uint8_t* mac,
                                         const char* config_json,
                                         uint16_t config_len) {
    if (proxy == NULL || mac == NULL || config_json == NULL) {
        return false;
    }
    
    // Проверяем размер
    if (config_len >= sizeof(proxy->device_cache[0].config_json)) {
        Serial.printf("[ESP-NOW Proxy] Config too large: %d bytes\n", config_len);
        return false;
    }
    
    // Поиск существующей записи
    cached_device_config_t* cached = NULL;
    for (uint8_t i = 0; i < proxy->cache_count; i++) {
        if (memcmp(proxy->device_cache[i].mac, mac, 6) == 0) {
            cached = &proxy->device_cache[i];
            break;
        }
    }
    
    // Если не найдено, создаем новую запись
    if (cached == NULL) {
        if (proxy->cache_count >= MAX_CACHED_DEVICES) {
            Serial.println("[ESP-NOW Proxy] Cache full, cannot add new device");
            return false;
        }
        cached = &proxy->device_cache[proxy->cache_count];
        proxy->cache_count++;
    }
    
    // Обновляем данные
    memcpy(cached->mac, mac, 6);
    memcpy(cached->config_json, config_json, config_len);
    cached->config_json[config_len] = '\0';  // Null terminator
    cached->config_len = config_len;
    cached->last_update = millis();
    cached->valid = true;
    
    return true;
}

boolean espnow_config_proxy_is_cache_valid(espnow_config_proxy_t* proxy, uint32_t max_age_ms) {
    if (proxy == NULL) {
        return false;
    }
    
    if (!proxy->cache_valid) {
        return false;
    }
    
    uint32_t age = millis() - proxy->cache_last_refresh;
    return (age < max_age_ms);
}

void espnow_config_proxy_clear_cache(espnow_config_proxy_t* proxy) {
    if (proxy == NULL) {
        return;
    }
    
    memset(proxy, 0, sizeof(espnow_config_proxy_t));
    proxy->cache_valid = false;
    proxy->cache_last_refresh = 0;
    
    Serial.println("[ESP-NOW Proxy] Cache cleared");
}

boolean espnow_config_proxy_get_all_configs(espnow_config_proxy_t* proxy,
                                            char* configs_json,
                                            uint16_t* configs_len,
                                            uint16_t max_len) {
    if (proxy == NULL || configs_json == NULL || configs_len == NULL) {
        return false;
    }
    
    // Формируем JSON массив со всеми конфигурациями
    // Формат: {"devices": [{"mac": "...", "config": {...}}, ...]}
    
    uint16_t pos = 0;
    pos += snprintf(configs_json + pos, max_len - pos, "{\"devices\":[");
    
    boolean first = true;
    for (uint8_t i = 0; i < proxy->cache_count; i++) {
        cached_device_config_t* cached = &proxy->device_cache[i];
        
        if (!cached->valid) {
            continue;
        }
        
        if (!first) {
            pos += snprintf(configs_json + pos, max_len - pos, ",");
        }
        first = false;
        
        // Формируем запись устройства
        pos += snprintf(configs_json + pos, max_len - pos, 
                       "{\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\",\"config\":",
                       cached->mac[0], cached->mac[1], cached->mac[2],
                       cached->mac[3], cached->mac[4], cached->mac[5]);
        
        // Добавляем конфигурацию (уже в JSON формате)
        if (pos + cached->config_len < max_len - 2) {
            memcpy(configs_json + pos, cached->config_json, cached->config_len);
            pos += cached->config_len;
        } else {
            // Не хватает места
            *configs_len = 0;
            return false;
        }
        
        pos += snprintf(configs_json + pos, max_len - pos, "}");
    }
    
    pos += snprintf(configs_json + pos, max_len - pos, "]}");
    
    *configs_len = pos;
    return true;
}

} // extern "C"
