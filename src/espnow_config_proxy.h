/*
 * ESP-NOW Configuration Proxy
 * Кэширование и управление конфигурациями удаленных ESP-NOW устройств
 * 
 * Phase 5, Week 9, Day 62-63
 * ESP32 EtherCAT Slave v4
 */

#ifndef ESPNOW_CONFIG_PROXY_H
#define ESPNOW_CONFIG_PROXY_H

#include "espnow_bridge.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration Cache
// ============================================================================

#define MAX_CACHED_DEVICES 16  /**< Максимальное количество кэшированных устройств */

/** Структура кэшированной конфигурации устройства */
typedef struct {
    uint8_t mac[6];              /**< MAC адрес устройства */
    uint32_t last_update;        /**< Timestamp последнего обновления (ms) */
    boolean valid;              /**< Флаг валидности кэша */
    char config_json[512];       /**< Кэшированная конфигурация (JSON) */
    uint16_t config_len;        /**< Длина конфигурации */
} cached_device_config_t;

/** Структура конфигурационного proxy */
typedef struct {
    cached_device_config_t device_cache[MAX_CACHED_DEVICES];  /**< Кэш конфигураций */
    uint8_t cache_count;        /**< Количество устройств в кэше */
    uint32_t cache_last_refresh;  /**< Timestamp последнего обновления кэша */
    boolean cache_valid;        /**< Флаг валидности всего кэша */
} espnow_config_proxy_t;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Инициализация конфигурационного proxy
 * @param proxy Указатель на структуру proxy
 * @return true если успешно
 */
boolean espnow_config_proxy_init(espnow_config_proxy_t* proxy);

/**
 * @brief Обновление кэша конфигураций для всех устройств
 * @param proxy Указатель на структуру proxy
 * @param bridge Указатель на ESP-NOW Bridge
 * @param devices Массив устройств для обновления
 * @param device_count Количество устройств
 * @return true если успешно
 */
boolean espnow_config_proxy_refresh_cache(espnow_config_proxy_t* proxy,
                                          espnow_bridge_t* bridge,
                                          const espnow_device_info_t* devices,
                                          uint8_t device_count);

/**
 * @brief Получение конфигурации из кэша
 * @param proxy Указатель на структуру proxy
 * @param mac MAC адрес устройства
 * @param config_json Буфер для конфигурации (может быть NULL)
 * @param config_len Указатель на длину конфигурации (может быть NULL)
 * @return true если конфигурация найдена и валидна
 */
boolean espnow_config_proxy_get_cached_config(espnow_config_proxy_t* proxy,
                                               const uint8_t* mac,
                                               char* config_json,
                                               uint16_t* config_len);

/**
 * @brief Обновление конфигурации в кэше
 * @param proxy Указатель на структуру proxy
 * @param mac MAC адрес устройства
 * @param config_json JSON конфигурация
 * @param config_len Длина конфигурации
 * @return true если успешно
 */
boolean espnow_config_proxy_update_cache(espnow_config_proxy_t* proxy,
                                         const uint8_t* mac,
                                         const char* config_json,
                                         uint16_t config_len);

/**
 * @brief Проверка валидности кэша (по timestamp)
 * @param proxy Указатель на структуру proxy
 * @param max_age_ms Максимальный возраст кэша в миллисекундах
 * @return true если кэш валиден
 */
boolean espnow_config_proxy_is_cache_valid(espnow_config_proxy_t* proxy, uint32_t max_age_ms);

/**
 * @brief Очистка кэша
 * @param proxy Указатель на структуру proxy
 */
void espnow_config_proxy_clear_cache(espnow_config_proxy_t* proxy);

/**
 * @brief Получение всех конфигураций из кэша
 * @param proxy Указатель на структуру proxy
 * @param configs_json Буфер для JSON со всеми конфигурациями
 * @param configs_len Указатель на длину JSON
 * @param max_len Максимальная длина буфера
 * @return true если успешно
 */
boolean espnow_config_proxy_get_all_configs(espnow_config_proxy_t* proxy,
                                            char* configs_json,
                                            uint16_t* configs_len,
                                            uint16_t max_len);

#ifdef __cplusplus
}
#endif

#endif // ESPNOW_CONFIG_PROXY_H
