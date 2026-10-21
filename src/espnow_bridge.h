/*
 * ESP-NOW Bridge
 * Wireless PDO forwarding и конфигурация между ESP32 устройствами
 * 
 * Phase 5, Week 9, Day 59-63
 * ESP32 EtherCAT Slave v4
 */

#ifndef ESPNOW_BRIDGE_H
#define ESPNOW_BRIDGE_H

#include "ethercat_type.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// ESP-NOW Configuration
// ============================================================================

#define ESPNOW_MAX_PEERS 8  /**< Максимум peer устройств */
#define ESPNOW_DATA_MAX_LEN 250  /**< Максимальная длина данных ESP-NOW */

// ESP-NOW Message Types
#define ESPNOW_MSG_PDO_OUTPUT  0x01  /**< PDO Output (Master → Slave) */
#define ESPNOW_MSG_PDO_INPUT   0x02  /**< PDO Input (Slave → Master) */
#define ESPNOW_MSG_STATUS      0x03  /**< Status message */
#define ESPNOW_MSG_CONFIG_COMMAND  0x04  /**< Configuration command (JSON-RPC) */
#define ESPNOW_MSG_CONFIG_RESPONSE 0x05  /**< Configuration response (JSON-RPC) */
#define ESPNOW_MSG_CONFIG_ACK      0x06  /**< Configuration ACK */
#define ESPNOW_MSG_CONFIG_FRAGMENTED 0x07  /**< Fragmented configuration */
#define ESPNOW_MSG_DISCOVERY_REQ   0x08  /**< Discovery request */
#define ESPNOW_MSG_DISCOVERY_RESP  0x09  /**< Discovery response */
// Fragmented PDO (when len > 250)
#define ESPNOW_MSG_PDO_OUTPUT_FRAG 0x0A  /**< Fragmented PDO Output */
#define ESPNOW_MSG_PDO_INPUT_FRAG  0x0B  /**< Fragmented PDO Input */

// ESP-NOW Message Structure
typedef struct {
    uint8_t msg_type;        /**< Message type (PDO_OUTPUT, PDO_INPUT, etc.) */
    uint8_t data_len;        /**< Data length */
    uint8_t data[ESPNOW_DATA_MAX_LEN];  /**< Message data */
    uint32_t sequence;       /**< Sequence number (для отслеживания потерь) */
    uint32_t timestamp_us;   /**< Timestamp (microseconds) */
} __attribute__((packed)) espnow_message_t;

// ESP-NOW Peer Structure
typedef struct {
    uint8_t mac[6];         /**< MAC address peer устройства */
    boolean enabled;        /**< Peer активен? */
    uint32_t tx_count;     /**< Счетчик отправленных сообщений */
    uint32_t rx_count;      /**< Счетчик принятых сообщений */
    uint32_t tx_errors;     /**< Счетчик ошибок отправки */
    uint32_t rx_errors;     /**< Счетчик ошибок приема */
    uint32_t last_rx_time;  /**< Время последнего приема (ms) */
} espnow_peer_t;

// ESP-NOW Device Info (для Discovery)
typedef struct {
    uint8_t mac[6];         /**< MAC address */
    char name[32];          /**< Имя устройства */
    int8_t rssi;            /**< RSSI сигнала */
    uint32_t last_seen;     /**< Timestamp последнего контакта */
    char firmware_version[16];  /**< Версия прошивки */
    uint32_t capabilities;  /**< Битовая маска возможностей */
} espnow_device_info_t;

// ESP-NOW Bridge Structure
typedef struct {
    boolean initialized;     /**< ESP-NOW инициализирован? */
    boolean enabled;        /**< Bridge активен? */
    
    // Encryption
    uint8_t pmk[16];        /**< Primary Master Key (16 bytes) */
    uint8_t lmk[16];        /**< Local Master Key (16 bytes) */
    boolean encryption_enabled;  /**< Encryption включен? */
    
    // Peers
    espnow_peer_t peers[ESPNOW_MAX_PEERS];  /**< Массив peer устройств */
    uint8_t peer_count;     /**< Количество активных peers */
    
    // Statistics
    uint32_t total_tx;      /**< Всего отправлено сообщений */
    uint32_t total_rx;      /**< Всего принято сообщений */
    uint32_t total_tx_errors;  /**< Всего ошибок отправки */
    uint32_t total_rx_errors;  /**< Всего ошибок приема */
    uint32_t sequence_counter;  /**< Счетчик последовательности */
    
    // Callbacks
    void (*on_pdo_output_received)(const uint8_t* data, uint16_t len, const uint8_t* mac);
    void (*on_pdo_input_received)(const uint8_t* data, uint16_t len, const uint8_t* mac);
    
    // Configuration Proxy (новое)
    boolean config_proxy_enabled;  /**< Включен ли proxy режим */
    uint32_t config_timeout_ms;   /**< Timeout для конфигурационных команд */
    uint8_t config_rx_buffer[256]; /**< Буфер для ответов */
    uint16_t config_rx_len;        /**< Длина ответа */
    boolean config_response_received;  /**< Флаг получения ответа */
    uint32_t config_command_id;    /**< Текущий ID команды */
    uint32_t config_response_timeout;  /**< Timeout для ожидания ответа */
    
    // Discovery
    espnow_device_info_t discovered_devices[ESPNOW_MAX_PEERS];  /**< Обнаруженные устройства */
    uint8_t discovered_count;      /**< Количество обнаруженных устройств */
    boolean discovery_in_progress;  /**< Флаг процесса Discovery */

    // PDO fragmentation reassembly (single in-flight message)
    uint8_t pdo_frag_buffer[256];  /**< Буфер реассемблинга PDO (до 256 байт) */
    uint16_t pdo_frag_total_len;   /**< Ожидаемая длина */
    uint8_t pdo_frag_count;        /**< Количество фрагментов */
    uint32_t pdo_frag_mask;        /**< Битовая маска полученных фрагментов */
    uint32_t pdo_frag_sequence;    /**< Sequence текущего сообщения */
    uint8_t pdo_frag_msg_type;     /**< ESPNOW_MSG_PDO_OUTPUT/_INPUT */
    uint32_t pdo_frag_last_update_ms; /**< Timestamp последнего фрагмента */
    
} espnow_bridge_t;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Инициализация ESP-NOW Bridge
 * @param bridge Указатель на ESP-NOW Bridge структуру
 * @param pmk Primary Master Key (16 bytes, может быть NULL)
 * @param lmk Local Master Key (16 bytes, может быть NULL)
 * @return true если успешно
 */
boolean espnow_bridge_init(espnow_bridge_t* bridge, const uint8_t* pmk, const uint8_t* lmk);

/**
 * @brief Деинициализация ESP-NOW Bridge
 * @param bridge Указатель на ESP-NOW Bridge структуру
 */
void espnow_bridge_deinit(espnow_bridge_t* bridge);

/**
 * @brief Включение/выключение ESP-NOW Bridge
 * @param bridge Указатель на ESP-NOW Bridge структуру
 * @param enable true для включения
 * @return true если успешно
 */
boolean espnow_bridge_enable(espnow_bridge_t* bridge, boolean enable);

/**
 * @brief Добавление peer устройства
 * @param bridge Указатель на ESP-NOW Bridge структуру
 * @param mac MAC address peer устройства (6 bytes)
 * @return true если успешно
 */
boolean espnow_bridge_add_peer(espnow_bridge_t* bridge, const uint8_t* mac);

/**
 * @brief Удаление peer устройства
 * @param bridge Указатель на ESP-NOW Bridge структуру
 * @param mac MAC address peer устройства (6 bytes)
 * @return true если успешно
 */
boolean espnow_bridge_remove_peer(espnow_bridge_t* bridge, const uint8_t* mac);

/**
 * @brief Отправка PDO Output через ESP-NOW
 * @param bridge Указатель на ESP-NOW Bridge структуру
 * @param data PDO Output данные
 * @param len Длина данных
 * @param mac MAC address получателя (NULL = broadcast)
 * @return true если успешно
 */
boolean espnow_bridge_send_pdo_output(espnow_bridge_t* bridge, const uint8_t* data, uint16_t len, const uint8_t* mac);

/**
 * @brief Отправка PDO Input через ESP-NOW
 * @param bridge Указатель на ESP-NOW Bridge структуру
 * @param data PDO Input данные
 * @param len Длина данных
 * @param mac MAC address получателя (NULL = broadcast)
 * @return true если успешно
 */
boolean espnow_bridge_send_pdo_input(espnow_bridge_t* bridge, const uint8_t* data, uint16_t len, const uint8_t* mac);

/**
 * @brief Отправка конфигурационной команды через ESP-NOW (Proxy)
 * @param bridge Указатель на ESP-NOW Bridge структуру
 * @param peer_mac MAC address удаленного устройства
 * @param command_json JSON-RPC команда
 * @param command_len Длина команды
 * @param response Буфер для ответа (может быть NULL)
 * @param response_len Указатель на длину ответа (может быть NULL)
 * @param timeout_ms Timeout в миллисекундах
 * @return true если успешно
 */
boolean espnow_send_config_command(espnow_bridge_t* bridge,
                                    const uint8_t* peer_mac,
                                    const uint8_t* command_json,
                                    uint16_t command_len,
                                    uint8_t* response,
                                    uint16_t* response_len,
                                    uint32_t timeout_ms);

/**
 * @brief Broadcast Discovery запрос
 * @param bridge Указатель на ESP-NOW Bridge структуру
 * @param devices Массив для хранения информации об устройствах
 * @param device_count Указатель на количество устройств
 * @param timeout_ms Timeout в миллисекундах
 * @return true если успешно
 */
boolean espnow_broadcast_discovery(espnow_bridge_t* bridge,
                                   espnow_device_info_t* devices,
                                   uint8_t* device_count,
                                   uint32_t timeout_ms);

/**
 * @brief Обработка входящих ESP-NOW сообщений (вызывать в loop)
 * @param bridge Указатель на ESP-NOW Bridge структуру
 */
void espnow_bridge_process(espnow_bridge_t* bridge);

/**
 * @brief Получение статистики
 * @param bridge Указатель на ESP-NOW Bridge структуру
 * @param total_tx Указатель на счетчик TX (может быть NULL)
 * @param total_rx Указатель на счетчик RX (может быть NULL)
 * @param total_tx_errors Указатель на счетчик TX errors (может быть NULL)
 * @param total_rx_errors Указатель на счетчик RX errors (может быть NULL)
 */
void espnow_bridge_get_stats(espnow_bridge_t* bridge, uint32_t* total_tx, uint32_t* total_rx,
                             uint32_t* total_tx_errors, uint32_t* total_rx_errors);

#ifdef __cplusplus
}
#endif

#endif // ESPNOW_BRIDGE_H
