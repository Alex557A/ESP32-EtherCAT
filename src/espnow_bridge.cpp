/*
 * ESP-NOW Bridge - Implementation
 * Wireless PDO forwarding и конфигурация между ESP32 устройствами
 * 
 * Phase 5, Week 9, Day 59-63
 * ESP32 EtherCAT Slave v4
 */

#include "espnow_bridge.h"
#include <Arduino.h>
#include <esp_now.h>
#include <WiFi.h>
#include <esp_mac.h>
#include <string.h>
#include <ArduinoJson.h>

extern "C" {

// ============================================================================
// ESP-NOW Callbacks (static)
// ============================================================================

static espnow_bridge_t* g_espnow_bridge = NULL;

// Forward declarations (C++ requires prototypes before use)
static boolean espnow_send_message_with_sequence(espnow_bridge_t* bridge, uint8_t msg_type,
                                                 const uint8_t* data, uint16_t len, const uint8_t* mac,
                                                 uint32_t sequence, uint32_t timestamp_us);
static boolean espnow_send_message(espnow_bridge_t* bridge, uint8_t msg_type,
                                   const uint8_t* data, uint16_t len, const uint8_t* mac);

static void espnow_on_data_sent(const uint8_t* mac_addr, esp_now_send_status_t status) {
    if (g_espnow_bridge == NULL) {
        return;
    }
    
    // Поиск peer по MAC
    for (uint8_t i = 0; i < g_espnow_bridge->peer_count; i++) {
        if (memcmp(g_espnow_bridge->peers[i].mac, mac_addr, 6) == 0) {
            if (status == ESP_NOW_SEND_SUCCESS) {
                g_espnow_bridge->peers[i].tx_count++;
                g_espnow_bridge->total_tx++;
            } else {
                g_espnow_bridge->peers[i].tx_errors++;
                g_espnow_bridge->total_tx_errors++;
                
                // Проверяем наличие peer при ошибке
                if (!esp_now_is_peer_exist(mac_addr)) {
                    g_espnow_bridge->peers[i].enabled = false;
                    Serial.printf("[ESP-NOW] Peer removed: %02X:%02X:%02X:%02X:%02X:%02X\n",
                                  mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
                }
            }
            break;
        }
    }
}

static void espnow_on_data_recv(const uint8_t* mac_addr, const uint8_t* data, int len) {
    if (g_espnow_bridge == NULL) {
        return;
    }
    
    // Минимальный размер сообщения: msg_type(1) + data_len(1) + data(минимум 0) + sequence(4) + timestamp_us(4) = 10 bytes
    if (len < 10) {
        Serial.printf("[ESP-NOW] ERROR: Message too short: %d bytes\n", len);
        return;
    }
    
    // Поиск или создание peer
    espnow_peer_t* peer = NULL;
    for (uint8_t i = 0; i < g_espnow_bridge->peer_count; i++) {
        if (memcmp(g_espnow_bridge->peers[i].mac, mac_addr, 6) == 0) {
            peer = &g_espnow_bridge->peers[i];
            break;
        }
    }
    
    if (peer == NULL && g_espnow_bridge->peer_count < ESPNOW_MAX_PEERS) {
        // Добавляем нового peer
        peer = &g_espnow_bridge->peers[g_espnow_bridge->peer_count];
        memcpy(peer->mac, mac_addr, 6);
        peer->enabled = true;
        peer->tx_count = 0;
        peer->rx_count = 0;
        peer->tx_errors = 0;
        peer->rx_errors = 0;
        g_espnow_bridge->peer_count++;
    }
    
    if (peer != NULL) {
        peer->rx_count++;
        peer->last_rx_time = millis();
        g_espnow_bridge->total_rx++;
    } else {
        g_espnow_bridge->total_rx_errors++;
        Serial.printf("[ESP-NOW] Received from unknown peer: %02X:%02X:%02X:%02X:%02X:%02X\n",
                     mac_addr[0], mac_addr[1], mac_addr[2], mac_addr[3], mac_addr[4], mac_addr[5]);
        return;
    }
    
    // Парсинг сообщения
    uint8_t msg_type = data[0];
    uint8_t data_len = data[1];
    
    // Проверяем, что сообщение содержит достаточно данных
    uint16_t expected_len = 1 + 1 + data_len + 4 + 4;
    if (len < expected_len) {
        Serial.printf("[ESP-NOW] ERROR: Message incomplete: len=%d, expected=%d (data_len=%d)\n",
                     len, expected_len, data_len);
        return;
    }
    
    // Извлекаем данные из сообщения
    const uint8_t* msg_data = (const uint8_t*)(data + 2);  // Пропускаем msg_type и data_len
    uint32_t sequence = 0;
    uint32_t timestamp_us = 0;
    memcpy(&sequence, data + 2 + data_len, 4);
    memcpy(&timestamp_us, data + 2 + data_len + 4, 4);
    
    // Обработка различных типов сообщений
    if (msg_type == ESPNOW_MSG_PDO_OUTPUT) {
        // PDO Output получен
        if (g_espnow_bridge->on_pdo_output_received != NULL) {
            g_espnow_bridge->on_pdo_output_received(msg_data, data_len, mac_addr);
        }
    } else if (msg_type == ESPNOW_MSG_PDO_INPUT) {
        // PDO Input данные
        if (g_espnow_bridge->on_pdo_input_received != NULL) {
            g_espnow_bridge->on_pdo_input_received(msg_data, data_len, mac_addr);
        }
    } else if (msg_type == ESPNOW_MSG_PDO_OUTPUT_FRAG || msg_type == ESPNOW_MSG_PDO_INPUT_FRAG) {
        // Fragmented PDO: [total_len:2 LE][frag_idx:1][frag_count:1][payload...]
        if (data_len < 4) {
            return;
        }

        uint16_t total_len = (uint16_t)msg_data[0] | ((uint16_t)msg_data[1] << 8);
        uint8_t frag_idx = msg_data[2];
        uint8_t frag_count = msg_data[3];

        const uint8_t* payload = msg_data + 4;
        uint16_t payload_len = (uint16_t)(data_len - 4);

        const uint16_t max_payload = (ESPNOW_DATA_MAX_LEN - 4); // 246 bytes per fragment payload

        if (frag_count == 0 || frag_count > 32 || frag_idx >= frag_count) {
            return;
        }
        if (total_len == 0 || total_len > sizeof(g_espnow_bridge->pdo_frag_buffer)) {
            return;
        }

        uint16_t offset = (uint16_t)frag_idx * max_payload;
        if ((uint32_t)offset + (uint32_t)payload_len > (uint32_t)total_len) {
            return;
        }

        // Reset reassembly if new sequence/type or timeout
        uint32_t now_ms = millis();
        if (g_espnow_bridge->pdo_frag_sequence != sequence ||
            g_espnow_bridge->pdo_frag_msg_type != (msg_type == ESPNOW_MSG_PDO_OUTPUT_FRAG ? ESPNOW_MSG_PDO_OUTPUT : ESPNOW_MSG_PDO_INPUT) ||
            (g_espnow_bridge->pdo_frag_last_update_ms != 0 && (uint32_t)(now_ms - g_espnow_bridge->pdo_frag_last_update_ms) > 1000)) {
            g_espnow_bridge->pdo_frag_sequence = sequence;
            g_espnow_bridge->pdo_frag_total_len = total_len;
            g_espnow_bridge->pdo_frag_count = frag_count;
            g_espnow_bridge->pdo_frag_mask = 0;
            g_espnow_bridge->pdo_frag_msg_type = (msg_type == ESPNOW_MSG_PDO_OUTPUT_FRAG ? ESPNOW_MSG_PDO_OUTPUT : ESPNOW_MSG_PDO_INPUT);
        }

        // Validate consistency for current message
        if (g_espnow_bridge->pdo_frag_total_len != total_len || g_espnow_bridge->pdo_frag_count != frag_count) {
            // Inconsistent fragments, drop
            g_espnow_bridge->pdo_frag_mask = 0;
            g_espnow_bridge->pdo_frag_total_len = total_len;
            g_espnow_bridge->pdo_frag_count = frag_count;
        }

        if (payload_len > 0) {
            memcpy(g_espnow_bridge->pdo_frag_buffer + offset, payload, payload_len);
        }
        g_espnow_bridge->pdo_frag_mask |= (1UL << frag_idx);
        g_espnow_bridge->pdo_frag_last_update_ms = now_ms;

        uint32_t all_mask = (frag_count >= 32) ? 0xFFFFFFFFUL : ((1UL << frag_count) - 1UL);
        if ((g_espnow_bridge->pdo_frag_mask & all_mask) == all_mask) {
            // Reassembled
            if (g_espnow_bridge->pdo_frag_msg_type == ESPNOW_MSG_PDO_OUTPUT) {
                if (g_espnow_bridge->on_pdo_output_received != NULL) {
                    g_espnow_bridge->on_pdo_output_received(g_espnow_bridge->pdo_frag_buffer,
                                                           g_espnow_bridge->pdo_frag_total_len,
                                                           mac_addr);
                }
            } else {
                if (g_espnow_bridge->on_pdo_input_received != NULL) {
                    g_espnow_bridge->on_pdo_input_received(g_espnow_bridge->pdo_frag_buffer,
                                                          g_espnow_bridge->pdo_frag_total_len,
                                                          mac_addr);
                }
            }

            // Clear state
            g_espnow_bridge->pdo_frag_mask = 0;
            g_espnow_bridge->pdo_frag_total_len = 0;
            g_espnow_bridge->pdo_frag_count = 0;
            g_espnow_bridge->pdo_frag_last_update_ms = 0;
        }
    } else if (msg_type == ESPNOW_MSG_CONFIG_RESPONSE) {
        // Конфигурационный ответ получен
        if (g_espnow_bridge->config_proxy_enabled) {
            // Проверяем, что это ответ на нашу команду
            if (data_len <= sizeof(g_espnow_bridge->config_rx_buffer)) {
                memcpy(g_espnow_bridge->config_rx_buffer, msg_data, data_len);
                g_espnow_bridge->config_rx_len = data_len;
                g_espnow_bridge->config_response_received = true;
            }
        }
    } else if (msg_type == ESPNOW_MSG_CONFIG_COMMAND) {
        // Конфигурационная команда (на удаленном устройстве)
        // msg_data = JSON-RPC (не обязательно null-terminated)
        if (data_len == 0 || data_len > ESPNOW_DATA_MAX_LEN) {
            return;
        }

        // Копируем JSON в локальный буфер
        char json_buf[ESPNOW_DATA_MAX_LEN + 1];
        memcpy(json_buf, msg_data, data_len);
        json_buf[data_len] = '\0';

        StaticJsonDocument<384> req;
        DeserializationError err = deserializeJson(req, json_buf);

        StaticJsonDocument<384> resp;
        resp["jsonrpc"] = "2.0";
        resp["id"] = req.containsKey("id") ? req["id"] : 1;

        if (err) {
            resp["success"] = false;
            resp["error"] = "invalid_json";
        } else {
            const char* method = req["method"] | "";
            // Минимальная реализация: поддерживаем get_config, остальное — заглушки
            if (strcmp(method, "get_config") == 0) {
                resp["success"] = true;
                JsonObject result = resp["result"].to<JsonObject>();
                JsonObject cfg = result["config"].to<JsonObject>();
                cfg["vendor_id"] = 0;
                cfg["product_code"] = 1;
                cfg["revision"] = 65536;
                cfg["device_name"] = "ESP32 EtherCAT Slave";
                cfg["vendor_name"] = "ESP32 EtherCAT";
            } else {
                resp["success"] = false;
                resp["error"] = "method_not_implemented";
            }
        }

        char out_buf[ESPNOW_DATA_MAX_LEN + 1];
        size_t out_len = serializeJson(resp, out_buf, sizeof(out_buf));
        if (out_len > 0 && out_len <= ESPNOW_DATA_MAX_LEN) {
            // Отправляем ответ инициатору
            (void)espnow_send_message(g_espnow_bridge, ESPNOW_MSG_CONFIG_RESPONSE,
                                      (const uint8_t*)out_buf, (uint16_t)out_len, mac_addr);
        } else {
            // Если не помещается — сообщаем об ошибке компактно
            const char* small_err = "{\"jsonrpc\":\"2.0\",\"id\":1,\"success\":false,\"error\":\"resp_too_large\"}";
            (void)espnow_send_message(g_espnow_bridge, ESPNOW_MSG_CONFIG_RESPONSE,
                                      (const uint8_t*)small_err, (uint16_t)strlen(small_err), mac_addr);
        }
    } else if (msg_type == ESPNOW_MSG_DISCOVERY_RESP) {
        // Discovery ответ получен
        if (g_espnow_bridge->discovery_in_progress) {
            // Парсим информацию об устройстве
            if (data_len >= sizeof(espnow_device_info_t)) {
                espnow_device_info_t* device_info = (espnow_device_info_t*)msg_data;
                
                // Проверяем, не добавлено ли уже это устройство
                boolean found = false;
                for (uint8_t i = 0; i < g_espnow_bridge->discovered_count; i++) {
                    if (memcmp(g_espnow_bridge->discovered_devices[i].mac, device_info->mac, 6) == 0) {
                        found = true;
                        // Обновляем информацию
                        memcpy(&g_espnow_bridge->discovered_devices[i], device_info, sizeof(espnow_device_info_t));
                        break;
                    }
                }
                
                if (!found && g_espnow_bridge->discovered_count < ESPNOW_MAX_PEERS) {
                    memcpy(&g_espnow_bridge->discovered_devices[g_espnow_bridge->discovered_count], 
                           device_info, sizeof(espnow_device_info_t));
                    g_espnow_bridge->discovered_count++;
                }
            }
        }
    } else if (msg_type == ESPNOW_MSG_DISCOVERY_REQ) {
        // Discovery запрос — отвечаем информацией об устройстве
        espnow_device_info_t info;
        memset(&info, 0, sizeof(info));

        uint8_t my_mac[6];
        esp_read_mac(my_mac, ESP_MAC_WIFI_STA);
        memcpy(info.mac, my_mac, 6);
        strncpy(info.name, "ESP32-ECAT", sizeof(info.name) - 1);
        info.rssi = 0;
        info.last_seen = millis();
        strncpy(info.firmware_version, "v4", sizeof(info.firmware_version) - 1);
        info.capabilities = 0;

        (void)espnow_send_message(g_espnow_bridge, ESPNOW_MSG_DISCOVERY_RESP,
                                  (const uint8_t*)&info, (uint16_t)sizeof(info), mac_addr);
    }
}

// ============================================================================
// Initialization
// ============================================================================

boolean espnow_bridge_init(espnow_bridge_t* bridge, const uint8_t* pmk, const uint8_t* lmk) {
    if (bridge == NULL) {
        return false;
    }
    
    memset(bridge, 0, sizeof(espnow_bridge_t));
    
    // Сохранение ключей
    if (pmk != NULL) {
        memcpy(bridge->pmk, pmk, 16);
        bridge->encryption_enabled = true;
    }
    if (lmk != NULL) {
        memcpy(bridge->lmk, lmk, 16);
        bridge->encryption_enabled = true;
    }
    
    // Инициализация WiFi (ESP-NOW требует WiFi)
    WiFi.mode(WIFI_STA);
    WiFi.disconnect();
    
    // Инициализация ESP-NOW
    esp_err_t init_result = esp_now_init();
    if (init_result != ESP_OK) {
        Serial.printf("[ESP-NOW] Failed to initialize ESP-NOW! Error: %d\n", init_result);
        return false;
    }
    
    // Регистрация callbacks
    esp_now_register_send_cb(espnow_on_data_sent);
    esp_now_register_recv_cb(espnow_on_data_recv);
    
    // Настройка encryption (если включено)
    if (bridge->encryption_enabled && pmk != NULL) {
        esp_now_set_pmk((uint8_t*)pmk);
    }
    
    // Инициализация полей конфигурационного proxy
    bridge->config_proxy_enabled = true;
    bridge->config_timeout_ms = 5000;  // 5 секунд по умолчанию
    bridge->config_response_received = false;
    bridge->config_rx_len = 0;
    bridge->discovery_in_progress = false;
    bridge->discovered_count = 0;
    
    g_espnow_bridge = bridge;
    bridge->initialized = true;
    bridge->enabled = true;
    
    Serial.println("[ESP-NOW] Bridge initialized");
    Serial.printf("  Encryption: %s\n", bridge->encryption_enabled ? "ON" : "OFF");
    
    // Получаем MAC адрес
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    Serial.printf("  MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return true;
}

void espnow_bridge_deinit(espnow_bridge_t* bridge) {
    if (bridge == NULL || !bridge->initialized) {
        return;
    }
    
    // Удаление всех peers
    for (uint8_t i = 0; i < bridge->peer_count; i++) {
        esp_now_del_peer(bridge->peers[i].mac);
    }
    
    // Деинициализация ESP-NOW
    esp_now_deinit();
    
    bridge->initialized = false;
    bridge->enabled = false;
    g_espnow_bridge = NULL;
    
    Serial.println("[ESP-NOW] Bridge deinitialized");
}

boolean espnow_bridge_enable(espnow_bridge_t* bridge, boolean enable) {
    if (bridge == NULL || !bridge->initialized) {
        return false;
    }
    
    bridge->enabled = enable;
    return true;
}

// ============================================================================
// Peer Management
// ============================================================================

boolean espnow_bridge_add_peer(espnow_bridge_t* bridge, const uint8_t* mac) {
    if (bridge == NULL || !bridge->initialized || mac == NULL) {
        return false;
    }
    
    // Проверка, не добавлен ли уже
    for (uint8_t i = 0; i < bridge->peer_count; i++) {
        if (memcmp(bridge->peers[i].mac, mac, 6) == 0) {
            return true;  // Уже добавлен
        }
    }
    
    // Проверка лимита
    if (bridge->peer_count >= ESPNOW_MAX_PEERS) {
        Serial.println("[ESP-NOW] Maximum peers reached");
        return false;
    }
    
    // Добавление peer в ESP-NOW
    esp_now_peer_info_t peer_info;
    memset(&peer_info, 0, sizeof(peer_info));
    memcpy(peer_info.peer_addr, mac, 6);
    
    int8_t wifi_channel = WiFi.channel();
    if (wifi_channel <= 0) {
        wifi_channel = 1;
    }
    peer_info.channel = wifi_channel;
    peer_info.encrypt = bridge->encryption_enabled;
    
    if (bridge->encryption_enabled) {
        memcpy(peer_info.lmk, bridge->lmk, 16);
    }
    
    esp_err_t add_result = esp_now_add_peer(&peer_info);
    if (add_result != ESP_OK) {
        Serial.printf("[ESP-NOW] Failed to add peer: %d\n", add_result);
        return false;
    }
    
    // Добавление в наш список
    espnow_peer_t* peer = &bridge->peers[bridge->peer_count];
    memcpy(peer->mac, mac, 6);
    peer->enabled = true;
    peer->tx_count = 0;
    peer->rx_count = 0;
    peer->tx_errors = 0;
    peer->rx_errors = 0;
    bridge->peer_count++;
    
    Serial.printf("[ESP-NOW] Peer added: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    return true;
}

boolean espnow_bridge_remove_peer(espnow_bridge_t* bridge, const uint8_t* mac) {
    if (bridge == NULL || !bridge->initialized || mac == NULL) {
        return false;
    }
    
    // Удаление из ESP-NOW
    esp_now_del_peer(mac);
    
    // Удаление из нашего списка
    for (uint8_t i = 0; i < bridge->peer_count; i++) {
        if (memcmp(bridge->peers[i].mac, mac, 6) == 0) {
            // Сдвигаем остальные элементы
            for (uint8_t j = i; j < bridge->peer_count - 1; j++) {
                bridge->peers[j] = bridge->peers[j + 1];
            }
            bridge->peer_count--;
            return true;
        }
    }
    
    return false;
}

// ============================================================================
// PDO Forwarding
// ============================================================================

static boolean espnow_send_message_with_sequence(espnow_bridge_t* bridge, uint8_t msg_type,
                                                 const uint8_t* data, uint16_t len, const uint8_t* mac,
                                                 uint32_t sequence, uint32_t timestamp_us) {
    if (bridge == NULL || !bridge->initialized || !bridge->enabled) {
        return false;
    }
    if (len > 0 && data == NULL) {
        return false;
    }
    
    // Формируем пакет: msg_type(1) + data_len(1) + data(len) + sequence(4) + timestamp_us(4)
    uint16_t packet_len = 1 + 1 + len + 4 + 4;
    if (packet_len > ESPNOW_DATA_MAX_LEN + 10) {
        Serial.printf("[ESP-NOW] Message too large: %d bytes\n", packet_len);
        return false;
    }
    
    uint8_t packet[ESPNOW_DATA_MAX_LEN + 10];
    packet[0] = msg_type;
    packet[1] = len;
    if (len > 0) {
        memcpy(packet + 2, data, len);
    }
    
    memcpy(packet + 2 + len, &sequence, 4);
    
    memcpy(packet + 2 + len + 4, &timestamp_us, 4);
    
    esp_err_t result = esp_now_send(mac, packet, packet_len);
    return (result == ESP_OK);
}

static boolean espnow_send_message(espnow_bridge_t* bridge, uint8_t msg_type,
                                   const uint8_t* data, uint16_t len, const uint8_t* mac) {
    if (bridge == NULL) {
        return false;
    }
    uint32_t seq = bridge->sequence_counter++;
    uint32_t ts = micros();
    return espnow_send_message_with_sequence(bridge, msg_type, data, len, mac, seq, ts);
}

boolean espnow_bridge_send_pdo_output(espnow_bridge_t* bridge, const uint8_t* data, uint16_t len, const uint8_t* mac) {
    if (bridge == NULL) {
        return false;
    }
    if (len <= ESPNOW_DATA_MAX_LEN) {
        return espnow_send_message(bridge, ESPNOW_MSG_PDO_OUTPUT, data, len, mac);
    }

    // Fragment into [total_len:2][frag_idx:1][frag_count:1][payload...]
    const uint16_t max_payload = (ESPNOW_DATA_MAX_LEN - 4); // 246
    uint8_t frag_count = (uint8_t)((len + max_payload - 1) / max_payload);
    if (frag_count == 0 || frag_count > 32 || len > 0xFFFF) {
        return false;
    }

    uint32_t seq = bridge->sequence_counter++;
    uint32_t ts = micros();

    boolean ok = true;
    for (uint8_t frag_idx = 0; frag_idx < frag_count; frag_idx++) {
        uint16_t offset = (uint16_t)frag_idx * max_payload;
        uint16_t chunk_len = (uint16_t)((len > offset) ? (len - offset) : 0);
        if (chunk_len > max_payload) {
            chunk_len = max_payload;
        }

        uint8_t frag_buf[ESPNOW_DATA_MAX_LEN];
        frag_buf[0] = (uint8_t)(len & 0xFF);
        frag_buf[1] = (uint8_t)((len >> 8) & 0xFF);
        frag_buf[2] = frag_idx;
        frag_buf[3] = frag_count;
        if (chunk_len > 0) {
            memcpy(frag_buf + 4, data + offset, chunk_len);
        }

        ok = ok && espnow_send_message_with_sequence(bridge, ESPNOW_MSG_PDO_OUTPUT_FRAG,
                                                     frag_buf, (uint16_t)(4 + chunk_len),
                                                     mac, seq, ts);
    }
    return ok;
}

boolean espnow_bridge_send_pdo_input(espnow_bridge_t* bridge, const uint8_t* data, uint16_t len, const uint8_t* mac) {
    if (bridge == NULL) {
        return false;
    }
    if (len <= ESPNOW_DATA_MAX_LEN) {
        return espnow_send_message(bridge, ESPNOW_MSG_PDO_INPUT, data, len, mac);
    }

    const uint16_t max_payload = (ESPNOW_DATA_MAX_LEN - 4); // 246
    uint8_t frag_count = (uint8_t)((len + max_payload - 1) / max_payload);
    if (frag_count == 0 || frag_count > 32 || len > 0xFFFF) {
        return false;
    }

    uint32_t seq = bridge->sequence_counter++;
    uint32_t ts = micros();

    boolean ok = true;
    for (uint8_t frag_idx = 0; frag_idx < frag_count; frag_idx++) {
        uint16_t offset = (uint16_t)frag_idx * max_payload;
        uint16_t chunk_len = (uint16_t)((len > offset) ? (len - offset) : 0);
        if (chunk_len > max_payload) {
            chunk_len = max_payload;
        }

        uint8_t frag_buf[ESPNOW_DATA_MAX_LEN];
        frag_buf[0] = (uint8_t)(len & 0xFF);
        frag_buf[1] = (uint8_t)((len >> 8) & 0xFF);
        frag_buf[2] = frag_idx;
        frag_buf[3] = frag_count;
        if (chunk_len > 0) {
            memcpy(frag_buf + 4, data + offset, chunk_len);
        }

        ok = ok && espnow_send_message_with_sequence(bridge, ESPNOW_MSG_PDO_INPUT_FRAG,
                                                     frag_buf, (uint16_t)(4 + chunk_len),
                                                     mac, seq, ts);
    }
    return ok;
}

// ============================================================================
// Configuration Proxy Functions
// ============================================================================

boolean espnow_send_config_command(espnow_bridge_t* bridge,
                                    const uint8_t* peer_mac,
                                    const uint8_t* command_json,
                                    uint16_t command_len,
                                    uint8_t* response,
                                    uint16_t* response_len,
                                    uint32_t timeout_ms) {
    if (bridge == NULL || !bridge->initialized || !bridge->config_proxy_enabled || 
        peer_mac == NULL || command_json == NULL) {
        return false;
    }
    
    // Проверяем размер команды
    if (command_len > ESPNOW_DATA_MAX_LEN) {
        Serial.printf("[ESP-NOW] Config command too large: %d bytes (max %d)\n", 
                     command_len, ESPNOW_DATA_MAX_LEN);
        return false;
    }
    
    // Сбрасываем флаги
    bridge->config_response_received = false;
    bridge->config_rx_len = 0;
    bridge->config_command_id = millis();  // Простой ID на основе времени
    
    // Отправляем команду
    if (!espnow_send_message(bridge, ESPNOW_MSG_CONFIG_COMMAND, command_json, command_len, peer_mac)) {
        return false;
    }
    
    // Устанавливаем timeout
    uint32_t start_time = millis();
    bridge->config_response_timeout = start_time + timeout_ms;
    
    // Ожидаем ответ
    while (millis() < bridge->config_response_timeout) {
        if (bridge->config_response_received) {
            // Ответ получен
            if (response != NULL && response_len != NULL) {
                if (bridge->config_rx_len <= *response_len) {
                    memcpy(response, bridge->config_rx_buffer, bridge->config_rx_len);
                    *response_len = bridge->config_rx_len;
                    return true;
                } else {
                    Serial.printf("[ESP-NOW] Response buffer too small: %d < %d\n",
                                 *response_len, bridge->config_rx_len);
                    return false;
                }
            }
            return true;
        }
        delay(10);  // Небольшая задержка
    }
    
    // Timeout
    Serial.println("[ESP-NOW] Config command timeout");
    return false;
}

boolean espnow_broadcast_discovery(espnow_bridge_t* bridge,
                                   espnow_device_info_t* devices,
                                   uint8_t* device_count,
                                   uint32_t timeout_ms) {
    if (bridge == NULL || !bridge->initialized || devices == NULL || device_count == NULL) {
        return false;
    }
    
    // Сбрасываем список обнаруженных устройств
    bridge->discovered_count = 0;
    bridge->discovery_in_progress = true;
    
    // Отправляем broadcast Discovery запрос (в формате espnow_send_message)
    // mac=NULL → broadcast
    if (!espnow_send_message(bridge, ESPNOW_MSG_DISCOVERY_REQ, NULL, 0, NULL)) {
        bridge->discovery_in_progress = false;
        return false;
    }
    
    // Ожидаем ответы
    uint32_t start_time = millis();
    while (millis() < start_time + timeout_ms) {
        delay(50);  // Даем время на получение ответов
    }
    
    bridge->discovery_in_progress = false;
    
    // Копируем результаты
    if (bridge->discovered_count > 0) {
        uint8_t count = (bridge->discovered_count < *device_count) ? 
                        bridge->discovered_count : *device_count;
        memcpy(devices, bridge->discovered_devices, count * sizeof(espnow_device_info_t));
        *device_count = count;
        return true;
    }
    
    *device_count = 0;
    return false;
}

// ============================================================================
// Process Loop
// ============================================================================

void espnow_bridge_process(espnow_bridge_t* bridge) {
    // Обработка происходит в callbacks
    // Здесь можно добавить периодические задачи, если нужно
    (void)bridge;
}

// ============================================================================
// Statistics
// ============================================================================

void espnow_bridge_get_stats(espnow_bridge_t* bridge, uint32_t* total_tx, uint32_t* total_rx,
                             uint32_t* total_tx_errors, uint32_t* total_rx_errors) {
    if (bridge == NULL) {
        return;
    }
    
    if (total_tx != NULL) {
        *total_tx = bridge->total_tx;
    }
    if (total_rx != NULL) {
        *total_rx = bridge->total_rx;
    }
    if (total_tx_errors != NULL) {
        *total_tx_errors = bridge->total_tx_errors;
    }
    if (total_rx_errors != NULL) {
        *total_rx_errors = bridge->total_rx_errors;
    }
}

} // extern "C"
