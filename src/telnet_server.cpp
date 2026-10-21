/*
 * Telnet Configuration Server - Implementation
 * 
 * Phase 3, Week 6, Day 37-38
 * ESP32 EtherCAT Slave v4
 */

#include "telnet_server.h"
#include "ethercat_test.h"
#include "ethercat_stack.h"
#include "network_config.h"
#include "gpio_manager.h"
#include "espnow_bridge.h"
#include "espnow_config_proxy.h"
#include "stepgen.h"
#include "usb_keyboard.h"
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <ArduinoJson.h>

// ============================================================================
// C++ Helper Functions (must be outside extern "C" for templates)
// ============================================================================

// Статические буферы для экономии стека (вне стека функций)
// Увеличен размер буфера для больших ответов (get_gpio_channels, get_encoders)
static char g_json_send_buffer[4096];
static char g_espnow_configs_buffer[512];  // Уменьшен с 1024 для экономии
static char g_espnow_cmd_json_buffer[256];
static char g_espnow_out_buffer[300];
static char g_ip_buffer[20], g_subnet_buffer[20], g_gw_buffer[20], g_nextip_buffer[20];  // Для IP адресов
static char g_mac_str_buffer[20];  // Для MAC адресов

template<typename T>
static boolean send_json_doc(telnet_server_t* server, const T& doc) {
    // Используем статический буфер вместо локального для экономии стека
    // Размер буфера: 4096 байт (достаточно для больших ответов get_gpio_channels, get_encoders)
    // Для очень больших ответов можно использовать chunked отправку
    size_t n = serializeJson(doc, g_json_send_buffer, sizeof(g_json_send_buffer));
    if (n == 0 || n >= sizeof(g_json_send_buffer) - 3) {
        // Используем оператор | с int64_t литералом для поддержки больших ID
        // ArduinoJson автоматически преобразует типы
        int64_t see_id = doc["id"] | (int64_t)1;
        Serial.printf("[Telnet] ERROR: Response too large: %zu bytes (max %zu)\n", n, sizeof(g_json_send_buffer) - 3);
        return telnet_server_send_error(server, "response_too_large", see_id);
    }
    g_json_send_buffer[n++] = '\r';
    g_json_send_buffer[n++] = '\n';
    g_json_send_buffer[n] = '\0';
    Serial.printf("[Telnet] Prepared response: %zu bytes\n", n);
    boolean sent = telnet_server_send_response(server, g_json_send_buffer);
    if (!sent) {
        Serial.println("[Telnet] ERROR: send_json_doc failed to send response");
    }
    return sent;
}

extern "C" {

// ============================================================================
// Helpers
// ============================================================================

static boolean parse_mac_string(const char* s, uint8_t mac[6]) {
    if (s == NULL || mac == NULL) {
        return false;
    }
    // Accept formats: "AA:BB:CC:DD:EE:FF" or "AA-BB-CC-DD-EE-FF"
    int values[6];
    char sep = ':';
    if (strchr(s, '-') != NULL) {
        sep = '-';
    }
    int n = sscanf(s, "%x%c%x%c%x%c%x%c%x%c%x",
                   &values[0], &sep,
                   &values[1], &sep,
                   &values[2], &sep,
                   &values[3], &sep,
                   &values[4], &sep,
                   &values[5]);
    if (n != 11) {
        return false;
    }
    for (int i = 0; i < 6; i++) {
        if (values[i] < 0 || values[i] > 255) return false;
        mac[i] = (uint8_t)values[i];
    }
    return true;
}

static boolean parse_ipv4_string(const char* s, uint8_t ip[4]) {
    if (s == NULL || ip == NULL) {
        return false;
    }
    int a=0,b=0,c=0,d=0;
    int n = sscanf(s, "%d.%d.%d.%d", &a, &b, &c, &d);
    if (n != 4) {
        return false;
    }
    if (a < 0 || a > 255 || b < 0 || b > 255 || c < 0 || c > 255 || d < 0 || d > 255) {
        return false;
    }
    ip[0] = (uint8_t)a;
    ip[1] = (uint8_t)b;
    ip[2] = (uint8_t)c;
    ip[3] = (uint8_t)d;
    return true;
}

/**
 * @brief Парсинг HEX строки в uint8_t
 * @param s Строка (может быть "0x04", "04", "4" или число)
 * @param out Результат
 * @return true если успешно
 */
static boolean parse_hex_uint8(const char* s, uint8_t* out) {
    if (s == NULL || out == NULL) {
        return false;
    }
    
    // Если это число (не строка), ArduinoJson уже распарсил
    // Проверяем, является ли это строкой
    if (s[0] == '\0') {
        return false;
    }
    
    // Пробуем распарсить как HEX (0x04, 0x4, 04, 4)
    unsigned int value = 0;
    int n = 0;
    
    // Пропускаем "0x" или "0X" если есть
    const char* start = s;
    if (s[0] == '0' && (s[1] == 'x' || s[1] == 'X')) {
        start = s + 2;
    }
    
    // Парсим HEX
    n = sscanf(start, "%x", &value);
    if (n == 1 && value <= 255) {
        *out = (uint8_t)value;
        return true;
    }
    
    // Если не получилось, пробуем как десятичное число
    n = sscanf(s, "%u", &value);
    if (n == 1 && value <= 255) {
        *out = (uint8_t)value;
        return true;
    }
    
    return false;
}

/**
 * @brief Безопасное извлечение uint8 из JSON параметра (поддерживает HEX строки)
 * @param params JSON объект
 * @param key1 Первый ключ для поиска
 * @param key2 Второй ключ для поиска (опционально)
 * @param default_val Значение по умолчанию
 * @return Извлеченное значение
 */
static uint8_t get_uint8_from_json(JsonObject params, const char* key1, const char* key2, uint8_t default_val) {
    // Сначала пробуем как число
    if (params[key1].is<int>() || params[key1].is<unsigned int>()) {
        int val = params[key1] | default_val;
        if (val >= 0 && val <= 255) {
            return (uint8_t)val;
        }
    }
    
    // Пробуем второй ключ как число
    if (key2 != NULL) {
        if (params[key2].is<int>() || params[key2].is<unsigned int>()) {
            int val = params[key2] | default_val;
            if (val >= 0 && val <= 255) {
                return (uint8_t)val;
            }
        }
    }
    
    // Пробуем как строку (HEX)
    if (params[key1].is<const char*>()) {
        uint8_t val = default_val;
        if (parse_hex_uint8(params[key1].as<const char*>(), &val)) {
            return val;
        }
    }
    
    // Пробуем второй ключ как строку (HEX)
    if (key2 != NULL && params[key2].is<const char*>()) {
        uint8_t val = default_val;
        if (parse_hex_uint8(params[key2].as<const char*>(), &val)) {
            return val;
        }
    }
    
    return default_val;
}
// ============================================================================
// Импорт и сброс полной конфигурации (JSON-RPC: set_config / reset_config)
// ============================================================================

/**
 * Импорт конфигурации (формат команды get_config).
 * Применяет каналы I/O, энкодеры и настройки модуля расширения,
 * при необходимости сохраняет конфигурацию во Flash.
 */
static boolean telnet_import_config(telnet_server_t* server, JsonObject params, int req_id) {
    gpio_manager_t* gpio = (gpio_manager_t*)server->gpio_manager;
    JsonObject cfg = params["config"].is<JsonObject>() ? params["config"].as<JsonObject>() : params;

    uint16 ch_applied = 0;
    uint16 ch_failed = 0;
    uint16 enc_applied = 0;
    uint16 enc_failed = 0;

    // --- Каналы I/O (полная замена начиная с индекса 0) ---
    if (cfg["gpio_channels"].is<JsonArray>()) {
        uint8 idx = 0;
        for (JsonObject o : cfg["gpio_channels"].as<JsonArray>()) {
            gpio_channel_t ch;
            memset(&ch, 0, sizeof(ch));
            ch.enabled = (boolean)(o["enabled"] | true);
            ch.type = (gpio_type_t)(int)(o["type"] | 0);
            ch.gpio_pin = (uint8)(o["gpio_pin"] | 0);
            ch.mcp23s17_port = (uint8)(o["mcp23s17_port"] | 0);
            ch.usb_key_code = get_uint8_from_json(o, "usb_key_code", "key_code", 0);
            ch.usb_modifiers = get_uint8_from_json(o, "usb_modifiers", "modifiers", 0);
            ch.pdo_byte = (uint8)(o["pdo_byte"] | 0);
            ch.pdo_bit = (uint8)(o["pdo_bit"] | 0);
            ch.invert = (boolean)(o["invert"] | false);
            const char* cname = o["name"] | "";
            if (cname && cname[0]) {
                strncpy(ch.name, cname, sizeof(ch.name) - 1);
            }
            if (gpio_manager_set_channel(gpio, idx, &ch)) {
                ch_applied++;
            } else {
                ch_failed++;
            }
            idx++;
        }
        // Лишние каналы удаляем с конца
        while (gpio->channel_count > idx) {
            (void)gpio_manager_delete_channel(gpio, (uint8)(gpio->channel_count - 1));
        }
    }

    // --- Энкодеры ---
    if (cfg["encoders"].is<JsonArray>()) {
        uint8 idx = 0;
        for (JsonObject o : cfg["encoders"].as<JsonArray>()) {
            gpio_encoder_t enc;
            memset(&enc, 0, sizeof(enc));
            enc.enabled = (boolean)(o["enabled"] | true);
            enc.pin_a = (uint8)(o["pin_a"] | 0);
            enc.pin_b = (uint8)(o["pin_b"] | 0);
            enc.pdo_byte_start = (uint8)(o["pdo_byte_start"] | 0);
            const char* ename = o["name"] | "";
            if (ename && ename[0]) {
                strncpy(enc.name, ename, sizeof(enc.name) - 1);
            }
            if (gpio_manager_set_encoder(gpio, idx, &enc)) {
                enc_applied++;
            } else {
                enc_failed++;
            }
            idx++;
        }
        while (gpio->encoder_count > idx) {
            (void)gpio_manager_delete_encoder(gpio, (uint8)(gpio->encoder_count - 1));
        }
    }

    // --- Модуль расширения MCP23S17 ---
    boolean io_ok = true;
    if (cfg["io_expansion"].is<JsonObject>()) {
        JsonObject io = cfg["io_expansion"].as<JsonObject>();
        boolean enabled = io["enabled"] | false;
        if (!enabled) {
            gpio->mcp23s17_enabled = false;
        } else {
            uint8 cs_pin = (uint8)(io["cs_pin"] | gpio->mcp23s17.config.cs_pin);
            uint16 port_direction = (uint16)(io["port_direction"] | (uint32)gpio->mcp23s17.config.port_direction);
            uint16 port_pullup = (uint16)(io["port_pullup"] | (uint32)gpio->mcp23s17.config.port_pullup);
            uint16 port_invert = (uint16)(io["port_invert"] | (uint32)gpio->mcp23s17.config.port_invert);
            io_ok = gpio_manager_init_mcp23s17(gpio, cs_pin, port_direction, port_pullup, port_invert);
        }
    }

    // --- Сохранение во Flash ---
    boolean save_needed = params["save"] | true;
    boolean saved = true;
    if (save_needed) {
        saved = gpio_manager_save_to_flash(gpio);
    }

    StaticJsonDocument<384> resp;
    resp["id"] = req_id;
    resp["success"] = (boolean)((ch_failed == 0) && (enc_failed == 0));
    JsonObject result = resp["result"].to<JsonObject>();
    result["channels_applied"] = ch_applied;
    result["channels_failed"] = ch_failed;
    result["encoders_applied"] = enc_applied;
    result["encoders_failed"] = enc_failed;
    result["channels"] = gpio->channel_count;
    result["encoders"] = gpio->encoder_count;
    result["io_expansion_ok"] = io_ok;
    result["saved"] = saved;

    return send_json_doc(server, resp);
}

/**
 * Сброс конфигурации к значениям по умолчанию (пустой список каналов/энкодеров).
 */
static boolean telnet_reset_config(telnet_server_t* server, boolean save_needed, int req_id) {
    gpio_manager_t* gpio = (gpio_manager_t*)server->gpio_manager;

    // Удаляем все каналы и энкодеры (с конца массива)
    while (gpio->channel_count > 0) {
        (void)gpio_manager_delete_channel(gpio, (uint8)(gpio->channel_count - 1));
    }
    while (gpio->encoder_count > 0) {
        (void)gpio_manager_delete_encoder(gpio, (uint8)(gpio->encoder_count - 1));
    }
    gpio->mcp23s17_enabled = false;

    boolean saved = true;
    if (save_needed) {
        saved = gpio_manager_save_to_flash(gpio);
    }

    StaticJsonDocument<320> resp;
    resp["id"] = req_id;
    resp["success"] = saved;
    JsonObject result = resp["result"].to<JsonObject>();
    result["message"] = "Configuration reset to defaults";
    result["channels"] = gpio->channel_count;
    result["encoders"] = gpio->encoder_count;
    result["saved"] = saved;

    return send_json_doc(server, resp);
}



// ============================================================================
// Initialization
// ============================================================================

boolean telnet_server_init(telnet_server_t* server, w5500_tcp_t* w5500, 
                          void* ethercat_stack, void* test_stats, void* gpio_manager,
                          void* stepgen,
                          void* espnow_bridge, void* espnow_config_proxy) {
    if (server == NULL || w5500 == NULL) {
        return false;
    }
    
    memset(server, 0, sizeof(telnet_server_t));
    
    server->w5500 = w5500;
    server->ethercat_stack = ethercat_stack;
    server->test_stats = test_stats;
    server->gpio_manager = gpio_manager;
    server->stepgen = stepgen;
    server->espnow_bridge = espnow_bridge;
    server->espnow_config_proxy = espnow_config_proxy;
    server->initialized = true;
    server->running = false;
    
    Serial.println("[Telnet] Server initialized");
    
    return true;
}

boolean telnet_server_start(telnet_server_t* server) {
    if (server == NULL || !server->initialized || server->w5500 == NULL) {
        return false;
    }
    
    if (server->running) {
        Serial.println("[Telnet] Server already running");
        return true;
    }
    
    Serial.println("[Telnet] Starting server on port 23...");
    
    // Открыть TCP socket в режиме LISTEN на порту 23
    if (!w5500_tcp_listen(server->w5500, 23)) {
        Serial.println("[Telnet] ERROR: Failed to start listening");
        return false;
    }
    
    server->running = true;
    Serial.println("[Telnet] ✓ Server started, waiting for connections...");
    
    return true;
}

// ============================================================================
// Processing
// ============================================================================

void telnet_server_process(telnet_server_t* server) {
    if (server == NULL || !server->running || server->w5500 == NULL) {
        return;
    }
    
    boolean new_conn = w5500_tcp_check_connection(server->w5500);
    boolean connected = w5500_tcp_accept(server->w5500);

    if (new_conn && connected) {
        server->connections_count++;
        Serial.printf("[Telnet] New connection (#%lu)\n", server->connections_count);
    }

    // Проверка текущего соединения
    if (connected) {
        // Проверка наличия данных
        uint16_t available = w5500_tcp_available(server->w5500);
        
        if (available > 0) {
            Serial.printf("[Telnet] Data available: %d bytes\n", available);
            // Читаем данные
            uint16_t len = w5500_tcp_recv(server->w5500, 
                                         server->rx_buffer, 
                                         TELNET_SERVER_BUFFER_SIZE - 1);
            
            if (len > 0) {
                server->rx_buffer[len] = '\0';  // Null terminator
                server->rx_len = len;
                
                // Убираем \r\n из конца команды и обрезаем до первого \r или \n
                char* cmd = (char*)server->rx_buffer;
                // Находим первый \r или \n и обрезаем там
                for (uint16_t i = 0; i < len; i++) {
                    if (cmd[i] == '\r' || cmd[i] == '\n') {
                        cmd[i] = '\0';
                        len = i;
                        break;
                    }
                }
                // Убираем пробелы в конце
                while (len > 0 && cmd[len-1] == ' ') {
                    cmd[len-1] = '\0';
                    len--;
                }
                
                // Обработка команд
                Serial.printf("[Telnet] Received %d bytes: '%.200s'\n", len, cmd);

                // Parse as JSON-RPC if it looks like JSON
                bool is_json = (cmd[0] == '{');
                int64_t req_id = 1;  // Используем int64_t для поддержки больших ID
                const char* method = cmd;
                const char* target = NULL;
                const char* peer_mac_str = NULL;
                const char* period_str = NULL;

                StaticJsonDocument<384> req;
                if (is_json) {
                    DeserializationError derr = deserializeJson(req, cmd);
                    if (derr) {
                        (void)telnet_server_send_error(server, "invalid_json", 1);
                        server->commands_processed++;
                        return;
                    }
                    // Правильное чтение ID с поддержкой больших чисел
                    // В ArduinoJson v7 большие числа могут быть как int64_t или как строки
                    if (!req["id"].isNull()) {
                        // Сначала пробуем получить как строку (для очень больших чисел)
                        if (req["id"].is<const char*>()) {
                            const char* id_str = req["id"].as<const char*>();
                            req_id = strtoll(id_str, NULL, 10);
                            if (req_id == 0 && strcmp(id_str, "0") != 0) {
                                req_id = 1;  // Если не удалось распарсить, используем 1
                            }
                        } else {
                            // Пробуем получить как число - ArduinoJson автоматически преобразует
                            // Используем оператор | для получения значения с дефолтом
                            req_id = req["id"] | (int64_t)1;
                        }
                    } else {
                        req_id = 1;  // По умолчанию, если ID отсутствует
                    }
                    method = req["method"] | "";
                    JsonObject params = req["params"].is<JsonObject>() ? req["params"].as<JsonObject>() : JsonObject();
                    target = params["target"].is<const char*>() ? params["target"].as<const char*>() : NULL;
                    peer_mac_str = params["peer_mac"].is<const char*>() ? params["peer_mac"].as<const char*>() : NULL;
                    period_str = params["period"].is<const char*>() ? params["period"].as<const char*>() : NULL;
                }

                // Handle commands
                if (strcmp(method, "get_stats") == 0 || strcmp(method, "test stats") == 0 || strcmp(method, "test_stats") == 0 || strstr(method, "test stats") != NULL) {
                    ethercat_test_stats_t test_stats;
                    ethercat_test_get_stats(&test_stats);

                    uint32_t ec_rx = 0, ec_tx = 0, ec_dropped = 0;
                    const char* ec_state_str = "INIT";
                    if (server->ethercat_stack != NULL) {
                        ethercat_stack_t* stack = (ethercat_stack_t*)server->ethercat_stack;
                        ethercat_stack_get_stats(stack, &ec_rx, &ec_tx, &ec_dropped);
                        ec_state ec_state = ethercat_stack_get_state(stack);
                        switch (ec_state) {
                            case EC_STATE_INIT: ec_state_str = "INIT"; break;
                            case EC_STATE_PRE_OP: ec_state_str = "PRE_OP"; break;
                            case EC_STATE_SAFE_OP: ec_state_str = "SAFE_OP"; break;
                            case EC_STATE_OPERATIONAL: ec_state_str = "OPERATIONAL"; break;
                            default: break;
                        }
                    }

                    uint32_t avg_latency = 0;
                    if (test_stats.latency_samples > 0) {
                        avg_latency = (uint32_t)(test_stats.total_latency_us / test_stats.latency_samples);
                    }

                    StaticJsonDocument<512> resp;  // Уменьшено для экономии стека
                    resp["id"] = req_id;
                    resp["success"] = true;
                    JsonObject result = resp["result"].to<JsonObject>();
                    JsonObject stats = result["stats"].to<JsonObject>();

                    JsonObject ec = stats["ethercat"].to<JsonObject>();
                    ec["state"] = ec_state_str;
                    ec["rx"] = ec_rx;
                    ec["tx"] = ec_tx;
                    ec["dropped"] = ec_dropped;

                    JsonObject t = stats["test"].to<JsonObject>();
                    t["frames_sent"] = test_stats.frames_sent;
                    t["frames_received"] = test_stats.frames_received;
                    t["frames_dropped"] = test_stats.frames_dropped;
                    t["min_latency_us"] = test_stats.min_latency_us;
                    t["max_latency_us"] = test_stats.max_latency_us;
                    t["avg_latency_us"] = avg_latency;
                    t["latency_samples"] = test_stats.latency_samples;
                    t["test_duration_ms"] = test_stats.test_duration_ms;

                    JsonObject tel = stats["telnet"].to<JsonObject>();
                    tel["running"] = server->running;
                    tel["connections_count"] = server->connections_count;
                    tel["commands_processed"] = server->commands_processed;

                    boolean sent = send_json_doc(server, resp);
                    if (!sent) {
                        Serial.println("[Telnet] ERROR: Failed to send get_stats response");
                    }
                } else if (strncmp(method, "test start", 10) == 0 || strcmp(method, "test_start") == 0) {
                    uint32_t period_us = 4000;
                    if ((period_str && strcmp(period_str, "1ms") == 0) || strstr(method, "1ms") != NULL) {
                        period_us = 1000;
                    }

                    ethercat_test_config_t config;
                    config.enabled = true;
                    config.frame_interval_us = period_us;
                    config.test_duration_sec = 30;
                    config.frame_type = 0;
                    config.measure_latency = true;
                    config.parallel_telnet = server->running;
                    config.loopback_mode = true;

                    boolean started = ethercat_test_start(&config);

                    StaticJsonDocument<256> resp;
                    resp["id"] = req_id;
                    resp["success"] = started;
                    JsonObject result = resp["result"].to<JsonObject>();
                    result["message"] = started ? "test started" : "failed to start";
                    result["period_us"] = period_us;
                    (void)send_json_doc(server, resp);
                } else if (strcmp(method, "test stop") == 0 || strcmp(method, "test_stop") == 0) {
                    ethercat_test_stop();
                    StaticJsonDocument<192> resp;
                    resp["id"] = req_id;
                    resp["success"] = true;
                    JsonObject result = resp["result"].to<JsonObject>();
                    result["message"] = "test stopped";
                    (void)send_json_doc(server, resp);
                } else if (strcmp(method, "get_system_info") == 0) {
                    StaticJsonDocument<256> resp;
                    resp["id"] = req_id;
                    resp["success"] = true;
                    JsonObject result = resp["result"].to<JsonObject>();
                    JsonObject sys = result["system"].to<JsonObject>();
                    sys["chip_model"] = "ESP32-S3";
                    sys["chip_revision"] = ESP.getChipRevision();
                    sys["cpu_freq_mhz"] = ESP.getCpuFreqMHz();
                    sys["free_heap"] = ESP.getFreeHeap();
                    sys["uptime_ms"] = millis();
                    boolean sent = send_json_doc(server, resp);
                    if (!sent) {
                        Serial.println("[Telnet] ERROR: Failed to send get_system_info response");
                    }
                } else if (strcmp(method, "get_network_config") == 0) {
                    extern network_config_t g_network_config;
                    StaticJsonDocument<320> resp;
                    resp["id"] = req_id;
                    resp["success"] = true;
                    JsonObject result = resp["result"].to<JsonObject>();
                    JsonObject net = result["network"].to<JsonObject>();
                    // Используем статические буферы вместо локальных для экономии стека
                    snprintf(g_ip_buffer, sizeof(g_ip_buffer), "%d.%d.%d.%d", g_network_config.ip[0], g_network_config.ip[1], g_network_config.ip[2], g_network_config.ip[3]);
                    snprintf(g_subnet_buffer, sizeof(g_subnet_buffer), "%d.%d.%d.%d", g_network_config.subnet[0], g_network_config.subnet[1], g_network_config.subnet[2], g_network_config.subnet[3]);
                    snprintf(g_gw_buffer, sizeof(g_gw_buffer), "%d.%d.%d.%d", g_network_config.gateway[0], g_network_config.gateway[1], g_network_config.gateway[2], g_network_config.gateway[3]);
                    snprintf(g_nextip_buffer, sizeof(g_nextip_buffer), "%d.%d.%d.%d", g_network_config.next_device_ip[0], g_network_config.next_device_ip[1], g_network_config.next_device_ip[2], g_network_config.next_device_ip[3]);
                    net["ip"] = g_ip_buffer;
                    net["subnet"] = g_subnet_buffer;
                    net["gateway"] = g_gw_buffer;
                    net["next_device_ip"] = g_nextip_buffer;
                    boolean sent = send_json_doc(server, resp);
                    if (!sent) {
                        Serial.println("[Telnet] ERROR: Failed to send get_network_config response");
                    }
                } else if (strcmp(method, "set_next_device_ip") == 0) {
                    extern network_config_t g_network_config;
                    if (!is_json) {
                        (void)telnet_server_send_error(server, "set_next_device_ip_requires_jsonrpc", req_id);
                    } else {
                        JsonObject params = req["params"].is<JsonObject>() ? req["params"].as<JsonObject>() : JsonObject();
                        const char* next_ip_str = NULL;
                        if (params["next_device_ip"].is<const char*>()) {
                            next_ip_str = params["next_device_ip"].as<const char*>();
                        } else if (params["ip"].is<const char*>()) {
                            next_ip_str = params["ip"].as<const char*>();
                        }
                        uint8_t ipb[4];
                        boolean ok = parse_ipv4_string(next_ip_str, ipb);
                        if (ok) {
                            memcpy(g_network_config.next_device_ip, ipb, 4);
                        }

                        StaticJsonDocument<384> resp;
                        resp["id"] = req_id;
                        resp["success"] = ok;
                        JsonObject result = resp["result"].to<JsonObject>();
                        JsonObject net = result["network"].to<JsonObject>();
                        // Используем статический буфер вместо локального для экономии стека
                        snprintf(g_nextip_buffer, sizeof(g_nextip_buffer), "%d.%d.%d.%d", g_network_config.next_device_ip[0], g_network_config.next_device_ip[1], g_network_config.next_device_ip[2], g_network_config.next_device_ip[3]);
                        net["next_device_ip"] = g_nextip_buffer;
                        (void)send_json_doc(server, resp);
                    }
                } else if (strcmp(method, "get_io_expansion_config") == 0) {
                    StaticJsonDocument<320> resp;
                    resp["id"] = req_id;
                    resp["success"] = true;
                    JsonObject result = resp["result"].to<JsonObject>();
                    JsonObject io = result["io_expansion"].to<JsonObject>();
                    if (server->gpio_manager) {
                        gpio_manager_t* gpio = (gpio_manager_t*)server->gpio_manager;
                        io["enabled"] = gpio->mcp23s17_enabled;
                        if (gpio->mcp23s17_enabled) {
                            io["cs_pin"] = gpio->mcp23s17.config.cs_pin;
                            io["port_direction"] = gpio->mcp23s17.config.port_direction;
                            io["port_pullup"] = gpio->mcp23s17.config.port_pullup;
                            io["port_invert"] = gpio->mcp23s17.config.port_invert;
                            io["current_state"] = gpio->mcp23s17.current_state;
                        }
                    } else {
                        resp["success"] = false;
                        resp["error"] = "gpio_manager_not_available";
                    }
                    (void)send_json_doc(server, resp);
                } else if (strcmp(method, "set_io_expansion_config") == 0) {
                    if (!server->gpio_manager) {
                        (void)telnet_server_send_error(server, "gpio_manager_not_available", req_id);
                    } else if (!is_json) {
                        (void)telnet_server_send_error(server, "set_io_expansion_config_requires_jsonrpc", req_id);
                    } else {
                        gpio_manager_t* gpio = (gpio_manager_t*)server->gpio_manager;
                        JsonObject params = req["params"].is<JsonObject>() ? req["params"].as<JsonObject>() : JsonObject();

                        boolean enabled = params["enabled"] | true;
                        int cs_pin = params["cs_pin"] | (int)(gpio->mcp23s17.config.cs_pin);
                        uint16 port_direction = (uint16)(params["port_direction"] | (uint32)gpio->mcp23s17.config.port_direction);
                        uint16 port_pullup = (uint16)(params["port_pullup"] | (uint32)gpio->mcp23s17.config.port_pullup);
                        uint16 port_invert = (uint16)(params["port_invert"] | (uint32)gpio->mcp23s17.config.port_invert);

                        boolean ok = true;
                        if (!enabled) {
                            gpio->mcp23s17_enabled = false;
                        } else {
                            ok = gpio_manager_init_mcp23s17(gpio, (uint8)cs_pin, port_direction, port_pullup, port_invert);
                        }

                        StaticJsonDocument<320> resp;
                        resp["id"] = req_id;
                        resp["success"] = ok;
                        JsonObject result = resp["result"].to<JsonObject>();
                        JsonObject io = result["io_expansion"].to<JsonObject>();
                        io["enabled"] = gpio->mcp23s17_enabled;
                        if (gpio->mcp23s17_enabled) {
                            io["cs_pin"] = gpio->mcp23s17.config.cs_pin;
                            io["port_direction"] = gpio->mcp23s17.config.port_direction;
                            io["port_pullup"] = gpio->mcp23s17.config.port_pullup;
                            io["port_invert"] = gpio->mcp23s17.config.port_invert;
                            io["current_state"] = gpio->mcp23s17.current_state;
                        }
                        if (!ok) {
                            resp["error"] = "mcp23s17_init_failed";
                        }
                        (void)send_json_doc(server, resp);
                    }
                } else if (strcmp(method, "get_stepgen_config") == 0) {
                    StaticJsonDocument<1536> resp;
                    resp["id"] = req_id;
                    resp["success"] = (server->stepgen != NULL);
                    if (!server->stepgen) {
                        resp["error"] = "stepgen_not_available";
                        (void)send_json_doc(server, resp);
                    } else {
                        stepgen_t* sg = (stepgen_t*)server->stepgen;
                        JsonObject result = resp["result"].to<JsonObject>();
                        JsonArray axes = result["axes"].to<JsonArray>();
                        for (uint8_t i = 0; i < STEPGEN_MAX_AXES; i++) {
                            const stepgen_axis_config_t* c = stepgen_get_axis_config(sg, i);
                            JsonObject a = axes.add<JsonObject>();
                            a["axis"] = i;
                            if (c) {
                                a["enabled"] = c->enabled;
                                a["step_pin"] = c->step_pin;
                                a["dir_pin"] = c->dir_pin;
                                a["invert_dir"] = c->invert_dir;
                                a["ledc_channel"] = c->ledc_channel;
                                a["resolution_bits"] = c->ledc_resolution_bits;
                                a["step_time_us"] = c->step_time_us;
                                a["step_space_us"] = c->step_space_us;
                                a["dir_setup_us"] = c->dir_setup_us;
                                a["dir_hold_us"] = c->dir_hold_us;
                                a["steps_per_unit"] = c->steps_per_unit;
                                a["max_accel_units_s2"] = c->max_accel_units_s2;
                                a["max_step_hz"] = c->max_step_hz;
                                a["pos_gain_hz_per_step"] = c->pos_gain_hz_per_step;
                                a["pos_deadband_steps"] = c->pos_deadband_steps;
                            }
                        }
                        (void)send_json_doc(server, resp);
                    }
                } else if (strcmp(method, "set_stepgen_config") == 0) {
                    if (!server->stepgen) {
                        (void)telnet_server_send_error(server, "stepgen_not_available", req_id);
                    } else if (!is_json) {
                        (void)telnet_server_send_error(server, "set_stepgen_config_requires_jsonrpc", req_id);
                    } else {
                        stepgen_t* sg = (stepgen_t*)server->stepgen;
                        JsonObject params = req["params"].is<JsonObject>() ? req["params"].as<JsonObject>() : JsonObject();
                        int axis = params["axis"] | params["index"] | 0;
                        if (axis < 0 || axis >= STEPGEN_MAX_AXES) {
                            (void)telnet_server_send_error(server, "invalid_axis", req_id);
                        } else {
                            const stepgen_axis_config_t* cur = stepgen_get_axis_config(sg, (uint8)axis);
                            stepgen_axis_config_t cfg = cur ? *cur : stepgen_axis_config_t{};

                            cfg.enabled = (boolean)(params["enabled"] | cfg.enabled);
                            cfg.step_pin = (uint8)(params["step_pin"] | (int)cfg.step_pin);
                            cfg.dir_pin = (uint8)(params["dir_pin"] | (int)cfg.dir_pin);
                            cfg.invert_dir = (boolean)(params["invert_dir"] | cfg.invert_dir);
                            cfg.ledc_channel = (uint8)(params["ledc_channel"] | (int)cfg.ledc_channel);
                            cfg.ledc_resolution_bits = (uint8)(params["resolution_bits"] | (int)cfg.ledc_resolution_bits);
                            cfg.step_time_us = (uint16)(params["step_time_us"] | (uint32)cfg.step_time_us);
                            cfg.step_space_us = (uint16)(params["step_space_us"] | (uint32)cfg.step_space_us);
                            cfg.dir_setup_us = (uint16)(params["dir_setup_us"] | (uint32)cfg.dir_setup_us);
                            cfg.dir_hold_us = (uint16)(params["dir_hold_us"] | (uint32)cfg.dir_hold_us);
                            cfg.steps_per_unit = (float)(params["steps_per_unit"] | cfg.steps_per_unit);
                            cfg.max_accel_units_s2 = (float)(params["max_accel_units_s2"] | cfg.max_accel_units_s2);
                            cfg.max_step_hz = (uint32)(params["max_step_hz"] | (uint32)cfg.max_step_hz);
                            cfg.pos_gain_hz_per_step = (float)(params["pos_gain_hz_per_step"] | cfg.pos_gain_hz_per_step);
                            cfg.pos_deadband_steps = (uint32)(params["pos_deadband_steps"] | (uint32)cfg.pos_deadband_steps);

                            boolean ok = stepgen_set_axis_config(sg, (uint8)axis, &cfg);

                            StaticJsonDocument<384> resp;
                            resp["id"] = req_id;
                            resp["success"] = ok;
                            JsonObject result = resp["result"].to<JsonObject>();
                            result["axis"] = axis;
                            (void)send_json_doc(server, resp);
                        }
                    }
                } else if (strcmp(method, "discover_espnow_devices") == 0) {
                    espnow_bridge_t* bridge = (espnow_bridge_t*)server->espnow_bridge;
                    StaticJsonDocument<896> resp;
                    resp["id"] = req_id;
                    resp["success"] = false;

                    if (!bridge) {
                        resp["error"] = "espnow_not_available";
                        (void)send_json_doc(server, resp);
                    } else {
                        espnow_device_info_t devices[ESPNOW_MAX_PEERS];
                        uint8_t count = ESPNOW_MAX_PEERS;
                        boolean ok = espnow_broadcast_discovery(bridge, devices, &count, 5000);
                        resp["success"] = ok;
                        JsonObject result = resp["result"].to<JsonObject>();
                        JsonArray arr = result["devices"].to<JsonArray>();
                        for (uint8_t i = 0; i < count; i++) {
                            // Используем статический буфер вместо локального для экономии стека
                            snprintf(g_mac_str_buffer, sizeof(g_mac_str_buffer), "%02X:%02X:%02X:%02X:%02X:%02X",
                                     devices[i].mac[0], devices[i].mac[1], devices[i].mac[2],
                                     devices[i].mac[3], devices[i].mac[4], devices[i].mac[5]);
                            JsonObject d = arr.add<JsonObject>();
                            d["mac"] = g_mac_str_buffer;
                            d["name"] = devices[i].name;
                            d["rssi"] = devices[i].rssi;
                            d["last_seen"] = devices[i].last_seen;
                            d["firmware_version"] = devices[i].firmware_version;
                            d["capabilities"] = devices[i].capabilities;
                        }
                        result["count"] = count;
                        (void)send_json_doc(server, resp);
                    }
                } else if (strcmp(method, "get_espnow_status") == 0) {
                    espnow_bridge_t* bridge = (espnow_bridge_t*)server->espnow_bridge;
                    StaticJsonDocument<384> resp;
                    resp["id"] = req_id;
                    resp["success"] = (bridge != NULL);
                    if (!bridge) {
                        resp["error"] = "espnow_not_available";
                        (void)send_json_doc(server, resp);
                    } else {
                        uint32_t tx=0, rx=0, txe=0, rxe=0;
                        espnow_bridge_get_stats(bridge, &tx, &rx, &txe, &rxe);
                        JsonObject result = resp["result"].to<JsonObject>();
                        JsonObject st = result["espnow"].to<JsonObject>();
                        st["enabled"] = bridge->enabled;
                        st["initialized"] = bridge->initialized;
                        st["encryption"] = bridge->encryption_enabled;
                        st["peer_count"] = bridge->peer_count;
                        st["total_tx"] = tx;
                        st["total_rx"] = rx;
                        st["total_tx_errors"] = txe;
                        st["total_rx_errors"] = rxe;
                        (void)send_json_doc(server, resp);
                    }
                } else if (strcmp(method, "get_all_configs") == 0) {
                    espnow_config_proxy_t* proxy = (espnow_config_proxy_t*)server->espnow_config_proxy;
                    StaticJsonDocument<256> resp;
                    resp["id"] = req_id;
                    resp["success"] = false;
                    if (!proxy) {
                        resp["error"] = "espnow_proxy_not_available";
                        (void)send_json_doc(server, resp);
                    } else {
                        // Используем статический буфер вместо локального для экономии стека
                        uint16_t out_len = 0;
                        boolean ok = espnow_config_proxy_get_all_configs(proxy, g_espnow_configs_buffer, &out_len, sizeof(g_espnow_configs_buffer));
                        resp["success"] = ok;
                        JsonObject result = resp["result"].to<JsonObject>();
                        if (ok) {
                            g_espnow_configs_buffer[out_len] = '\0';
                            result["configs_json"] = g_espnow_configs_buffer;
                        }
                        (void)send_json_doc(server, resp);
                    }
                } else if (strcmp(method, "refresh_all_configs") == 0) {
                    espnow_bridge_t* bridge = (espnow_bridge_t*)server->espnow_bridge;
                    espnow_config_proxy_t* proxy = (espnow_config_proxy_t*)server->espnow_config_proxy;
                    StaticJsonDocument<320> resp;
                    resp["id"] = req_id;
                    resp["success"] = false;
                    if (!bridge || !proxy) {
                        resp["error"] = "espnow_not_available";
                        (void)send_json_doc(server, resp);
                    } else {
                        espnow_device_info_t devices[ESPNOW_MAX_PEERS];
                        uint8_t count = ESPNOW_MAX_PEERS;
                        boolean disc_ok = espnow_broadcast_discovery(bridge, devices, &count, 5000);
                        boolean ref_ok = false;
                        if (disc_ok && count > 0) {
                            ref_ok = espnow_config_proxy_refresh_cache(proxy, bridge, devices, count);
                        }
                        resp["success"] = (disc_ok && ref_ok);
                        JsonObject result = resp["result"].to<JsonObject>();
                        result["discovered"] = count;
                        result["discovery_ok"] = disc_ok;
                        result["refresh_ok"] = ref_ok;
                        (void)send_json_doc(server, resp);
                    }
                } else if (strcmp(method, "get_config") == 0 && (target != NULL || peer_mac_str != NULL)) {
                    // Proxy get_config to remote device
                    espnow_bridge_t* bridge = (espnow_bridge_t*)server->espnow_bridge;
                    if (!bridge) {
                        (void)telnet_server_send_error(server, "espnow_not_available", req_id);
                    } else {
                        uint8_t mac[6];
                        const char* mac_s = peer_mac_str;
                        if (!mac_s && target) {
                            mac_s = target;
                        }
                        if (!parse_mac_string(mac_s, mac)) {
                            (void)telnet_server_send_error(server, "invalid_peer_mac", req_id);
                        } else {
                            // Forward JSON-RPC if provided, else build minimal JSON
                            // Используем статический буфер вместо локального для экономии стека
                            const char* fwd = cmd;
                            if (!is_json) {
                                snprintf(g_espnow_cmd_json_buffer, sizeof(g_espnow_cmd_json_buffer),
                                         "{\"jsonrpc\":\"2.0\",\"method\":\"get_config\",\"params\":{},\"id\":%d}",
                                         req_id);
                                fwd = g_espnow_cmd_json_buffer;
                            }
                            uint8_t resp_buf[256];
                            uint16_t resp_len = sizeof(resp_buf);
                            boolean ok = espnow_send_config_command(bridge, mac, (const uint8_t*)fwd, (uint16_t)strlen(fwd),
                                                                    resp_buf, &resp_len, 5000);
                            if (!ok) {
                                (void)telnet_server_send_error(server, "espnow_timeout", req_id);
                            } else {
                                // Send raw JSON from remote as-is (append CRLF)
                                // Используем статический буфер вместо локального для экономии стека
                                uint16_t n = (resp_len < (sizeof(g_espnow_out_buffer) - 3)) ? resp_len : (sizeof(g_espnow_out_buffer) - 3);
                                memcpy(g_espnow_out_buffer, resp_buf, n);
                                g_espnow_out_buffer[n++] = '\r';
                                g_espnow_out_buffer[n++] = '\n';
                                g_espnow_out_buffer[n] = '\0';
                                (void)telnet_server_send_response(server, g_espnow_out_buffer);
                            }
                        }
                    }
                } else if (strcmp(method, "set_config") == 0 && (target != NULL || peer_mac_str != NULL)) {
                    // Proxy set_config to remote device (требует JSON-RPC вход)
                    espnow_bridge_t* bridge = (espnow_bridge_t*)server->espnow_bridge;
                    if (!bridge) {
                        (void)telnet_server_send_error(server, "espnow_not_available", req_id);
                    } else if (!is_json) {
                        (void)telnet_server_send_error(server, "set_config_requires_jsonrpc", req_id);
                    } else {
                        uint8_t mac[6];
                        const char* mac_s = peer_mac_str;
                        if (!mac_s && target) {
                            mac_s = target;
                        }
                        if (!parse_mac_string(mac_s, mac)) {
                            (void)telnet_server_send_error(server, "invalid_peer_mac", req_id);
                        } else {
                            uint8_t resp_buf[256];
                            uint16_t resp_len = sizeof(resp_buf);
                            boolean ok = espnow_send_config_command(bridge, mac, (const uint8_t*)cmd, (uint16_t)strlen(cmd),
                                                                    resp_buf, &resp_len, 5000);
                            if (!ok) {
                                (void)telnet_server_send_error(server, "espnow_timeout", req_id);
                            } else {
                                char out[300];
                                uint16_t n = (resp_len < (sizeof(out) - 3)) ? resp_len : (sizeof(out) - 3);
                                memcpy(out, resp_buf, n);
                                out[n++] = '\r';
                                out[n++] = '\n';
                                out[n] = '\0';
                                (void)telnet_server_send_response(server, out);
                            }
                        }
                    }
                } else if (strcmp(method, "get_config") == 0) {
                    Serial.println("[Telnet] Processing get_config command (local)");
                    // Local get_config (export) - используем увеличенный размер для больших конфигураций
                    // Оптимизировано: только основные поля, без комментариев
                    StaticJsonDocument<4096> resp;
                    resp["id"] = req_id;
                    resp["success"] = true;
                    JsonObject result = resp["result"].to<JsonObject>();
                    JsonObject cfg = result["config"].to<JsonObject>();
                    cfg["vendor_id"] = 0;
                    cfg["product_code"] = 1;
                    cfg["revision"] = 65536;
                    cfg["device_name"] = "ESP32 EtherCAT Slave";
                    cfg["vendor_name"] = "ESP32 EtherCAT";
                    // GPIO channels + Encoders
                    if (server->gpio_manager) {
                        gpio_manager_t* gpio = (gpio_manager_t*)server->gpio_manager;

                        JsonArray chArr = cfg["gpio_channels"].to<JsonArray>();
                        for (uint8 i = 0; i < gpio->channel_count; i++) {
                            const gpio_channel_t* ch = &gpio->channels[i];
                            JsonObject o = chArr.add<JsonObject>();
                            o["index"] = i;
                            o["enabled"] = ch->enabled;
                            o["type"] = (int)ch->type;
                            o["gpio_pin"] = ch->gpio_pin;
                            o["mcp23s17_port"] = ch->mcp23s17_port;
                            o["usb_key_code"] = ch->usb_key_code;
                            o["usb_modifiers"] = ch->usb_modifiers;
                            o["pdo_byte"] = ch->pdo_byte;
                            o["pdo_bit"] = ch->pdo_bit;
                            o["invert"] = ch->invert;
                            if (ch->name[0]) o["name"] = ch->name;
                        }
                        cfg["gpio_channel_count"] = gpio->channel_count;

                        JsonArray encArr = cfg["encoders"].to<JsonArray>();
                        for (uint8 i = 0; i < gpio->encoder_count; i++) {
                            const gpio_encoder_t* enc = &gpio->encoders[i];
                            JsonObject o = encArr.add<JsonObject>();
                            o["index"] = i;
                            o["enabled"] = enc->enabled;
                            o["pin_a"] = enc->pin_a;
                            o["pin_b"] = enc->pin_b;
                            o["pdo_byte_start"] = enc->pdo_byte_start;
                            o["counter"] = gpio_manager_get_encoder_count(gpio, i);
                            if (enc->name[0]) o["name"] = enc->name;
                        }
                        cfg["encoder_count"] = gpio->encoder_count;

                        // IO Expansion (MCP23S17)
                        JsonObject io = cfg["io_expansion"].to<JsonObject>();
                        io["enabled"] = gpio->mcp23s17_enabled;
                        if (gpio->mcp23s17_enabled) {
                            io["cs_pin"] = gpio->mcp23s17.config.cs_pin;
                            io["port_direction"] = gpio->mcp23s17.config.port_direction;
                            io["port_pullup"] = gpio->mcp23s17.config.port_pullup;
                            io["port_invert"] = gpio->mcp23s17.config.port_invert;
                        }
                    } else {
                        (void)cfg["gpio_channels"].to<JsonArray>();
                        (void)cfg["encoders"].to<JsonArray>();
                    }

                    // StepGen axes
                    if (server->stepgen) {
                        stepgen_t* sg = (stepgen_t*)server->stepgen;
                        JsonObject sgo = cfg["stepgen"].to<JsonObject>();
                        JsonArray axes = sgo["axes"].to<JsonArray>();
                        for (uint8_t i = 0; i < STEPGEN_MAX_AXES; i++) {
                            const stepgen_axis_config_t* c = stepgen_get_axis_config(sg, i);
                            JsonObject a = axes.add<JsonObject>();
                            a["axis"] = i;
                            if (c) {
                                a["enabled"] = c->enabled;
                                a["step_pin"] = c->step_pin;
                                a["dir_pin"] = c->dir_pin;
                                a["invert_dir"] = c->invert_dir;
                                a["ledc_channel"] = c->ledc_channel;
                                a["resolution_bits"] = c->ledc_resolution_bits;
                                a["step_time_us"] = c->step_time_us;
                                a["step_space_us"] = c->step_space_us;
                                a["dir_setup_us"] = c->dir_setup_us;
                                a["dir_hold_us"] = c->dir_hold_us;
                                a["steps_per_unit"] = c->steps_per_unit;
                                a["max_accel_units_s2"] = c->max_accel_units_s2;
                                a["max_step_hz"] = c->max_step_hz;
                                a["pos_gain_hz_per_step"] = c->pos_gain_hz_per_step;
                                a["pos_deadband_steps"] = c->pos_deadband_steps;
                            }
                        }
                    }
                    boolean sent = send_json_doc(server, resp);
                    if (!sent) {
                        Serial.println("[Telnet] ERROR: Failed to send get_config response");
                    } else {
                        Serial.println("[Telnet] get_config response sent successfully");
                    }
                } else if (strcmp(method, "set_config") == 0) {
                    // Импорт полной конфигурации (формат get_config)
                    Serial.println("[Telnet] Processing set_config command (local import)");
                    if (!server->gpio_manager) {
                        (void)telnet_server_send_error(server, "gpio_manager_not_available", req_id);
                    } else if (!is_json) {
                        (void)telnet_server_send_error(server, "set_config_requires_jsonrpc", req_id);
                    } else {
                        JsonObject params = req["params"].is<JsonObject>() ? req["params"].as<JsonObject>() : JsonObject();
                        (void)telnet_import_config(server, params, req_id);
                    }
                } else if (strcmp(method, "reset_config") == 0) {
                    // Сброс конфигурации к значениям по умолчанию
                    Serial.println("[Telnet] Processing reset_config command");
                    if (!server->gpio_manager) {
                        (void)telnet_server_send_error(server, "gpio_manager_not_available", req_id);
                    } else {
                        JsonObject params = req["params"].is<JsonObject>() ? req["params"].as<JsonObject>() : JsonObject();
                        boolean save_needed = params["save"] | true;
                        (void)telnet_reset_config(server, save_needed, req_id);
                    }
                } else if (strcmp(method, "set_gpio_channel") == 0) {
                    if (!server->gpio_manager) {
                        (void)telnet_server_send_error(server, "gpio_manager_not_available", req_id);
                    } else if (!is_json) {
                        (void)telnet_server_send_error(server, "set_gpio_channel_requires_jsonrpc", req_id);
                    } else {
                        gpio_manager_t* gpio = (gpio_manager_t*)server->gpio_manager;
                        JsonObject params = req["params"].is<JsonObject>() ? req["params"].as<JsonObject>() : JsonObject();

                        int idx = params["index"] | -1;
                        if (idx < 0) {
                            idx = (int)gpio->channel_count;
                        }

                        gpio_channel_t ch;
                        memset(&ch, 0, sizeof(ch));
                        ch.enabled = (boolean)(params["enabled"] | true);
                        ch.invert = (boolean)(params["invert"] | false);
                        ch.pdo_byte = (uint8)(params["pdo_byte"] | 0);
                        ch.pdo_bit = (uint8)(params["pdo_bit"] | 0);

                        const char* name = params["name"] | "";
                        if (name && name[0]) {
                            strncpy(ch.name, name, sizeof(ch.name) - 1);
                        }

                        int type = params["type"] | -1;
                        if (type < 0) {
                            // Derive from category/class/direction (strings)
                            const char* category = params["category"] | params["source"] | "GPIO_ESP";
                            const char* sclass = params["class"] | params["signal_class"] | "DIGITAL";
                            const char* dir = params["direction"] | "IN";

                            if (strcmp(category, "USB") == 0 || strcmp(category, "USB_KEY") == 0) {
                                type = GPIO_TYPE_USB_KEYBOARD;
                            } else if (strcmp(category, "GPIO_EX") == 0 || strcmp(category, "GPIO_Ex") == 0 || strcmp(category, "GPIO_EX") == 0) {
                                type = GPIO_TYPE_MCP23S17;
                            } else {
                                // GPIO_ESP
                                if ((strcmp(sclass, "ANALOG") == 0) || (strcmp(sclass, "analog") == 0)) {
                                    type = (strcmp(dir, "OUT") == 0 || strcmp(dir, "out") == 0) ? GPIO_TYPE_ANALOG_OUT : GPIO_TYPE_ANALOG_IN;
                                } else {
                                    type = (strcmp(dir, "OUT") == 0 || strcmp(dir, "out") == 0) ? GPIO_TYPE_DIGITAL_OUT : GPIO_TYPE_DIGITAL_IN;
                                }
                            }
                        }
                        ch.type = (gpio_type_t)type;

                        // type-specific fields
                        ch.gpio_pin = (uint8)(params["gpio_pin"] | params["pin"] | 0);
                        ch.mcp23s17_port = (uint8)(params["mcp23s17_port"] | params["port"] | 0);
                        
                        // USB key code и modifiers - поддерживаем HEX строки ("0x04") и числа
                        ch.usb_key_code = get_uint8_from_json(params, "usb_key_code", "key_code", 0);
                        ch.usb_modifiers = get_uint8_from_json(params, "usb_modifiers", "modifiers", 0);
                        
                        // Валидация для USB Keyboard каналов
                        if (ch.type == GPIO_TYPE_USB_KEYBOARD) {
                            // USB key code должен быть валидным (не 0, если канал включен)
                            if (ch.enabled && ch.usb_key_code == 0) {
                                (void)telnet_server_send_error(server, "invalid_usb_key_code", req_id);
                                server->commands_processed++;
                                return;
                            }
                            // Модификаторы валидны в диапазоне 0-15 (4 бита)
                            if (ch.usb_modifiers > 0x0F) {
                                (void)telnet_server_send_error(server, "invalid_usb_modifiers", req_id);
                                server->commands_processed++;
                                return;
                            }
                        }

                        boolean ok = gpio_manager_set_channel(gpio, (uint8)idx, &ch);

                        StaticJsonDocument<512> resp;
                        resp["id"] = req_id;
                        resp["success"] = ok;
                        JsonObject result = resp["result"].to<JsonObject>();
                        result["index"] = idx;
                        result["count"] = gpio->channel_count;
                        (void)send_json_doc(server, resp);
                    }
                } else if (strcmp(method, "delete_gpio_channel") == 0) {
                    if (!server->gpio_manager) {
                        (void)telnet_server_send_error(server, "gpio_manager_not_available", req_id);
                    } else if (!is_json) {
                        (void)telnet_server_send_error(server, "delete_gpio_channel_requires_jsonrpc", req_id);
                    } else {
                        gpio_manager_t* gpio = (gpio_manager_t*)server->gpio_manager;
                        JsonObject params = req["params"].is<JsonObject>() ? req["params"].as<JsonObject>() : JsonObject();
                        int idx = params["index"] | -1;
                        boolean ok = (idx >= 0) ? gpio_manager_delete_channel(gpio, (uint8)idx) : false;

                        StaticJsonDocument<320> resp;
                        resp["id"] = req_id;
                        resp["success"] = ok;
                        JsonObject result = resp["result"].to<JsonObject>();
                        result["index"] = idx;
                        result["count"] = gpio->channel_count;
                        (void)send_json_doc(server, resp);
                    }
                } else if (strcmp(method, "set_encoder") == 0) {
                    if (!server->gpio_manager) {
                        (void)telnet_server_send_error(server, "gpio_manager_not_available", req_id);
                    } else if (!is_json) {
                        (void)telnet_server_send_error(server, "set_encoder_requires_jsonrpc", req_id);
                    } else {
                        gpio_manager_t* gpio = (gpio_manager_t*)server->gpio_manager;
                        JsonObject params = req["params"].is<JsonObject>() ? req["params"].as<JsonObject>() : JsonObject();

                        int idx = params["index"] | -1;
                        if (idx < 0) {
                            idx = (int)gpio->encoder_count;
                        }

                        gpio_encoder_t enc;
                        memset(&enc, 0, sizeof(enc));
                        enc.enabled = (boolean)(params["enabled"] | true);
                        enc.pin_a = (uint8)(params["pin_a"] | 0);
                        enc.pin_b = (uint8)(params["pin_b"] | 0);
                        enc.pdo_byte_start = (uint8)(params["pdo_byte_start"] | params["pdo_byte"] | 0);
                        const char* name = params["name"] | "";
                        if (name && name[0]) {
                            strncpy(enc.name, name, sizeof(enc.name) - 1);
                        }

                        boolean ok = gpio_manager_set_encoder(gpio, (uint8)idx, &enc);

                        StaticJsonDocument<384> resp;
                        resp["id"] = req_id;
                        resp["success"] = ok;
                        JsonObject result = resp["result"].to<JsonObject>();
                        result["index"] = idx;
                        result["count"] = gpio->encoder_count;
                        (void)send_json_doc(server, resp);
                    }
                } else if (strcmp(method, "delete_encoder") == 0) {
                    if (!server->gpio_manager) {
                        (void)telnet_server_send_error(server, "gpio_manager_not_available", req_id);
                    } else if (!is_json) {
                        (void)telnet_server_send_error(server, "delete_encoder_requires_jsonrpc", req_id);
                    } else {
                        gpio_manager_t* gpio = (gpio_manager_t*)server->gpio_manager;
                        JsonObject params = req["params"].is<JsonObject>() ? req["params"].as<JsonObject>() : JsonObject();
                        int idx = params["index"] | -1;
                        boolean ok = (idx >= 0) ? gpio_manager_delete_encoder(gpio, (uint8)idx) : false;

                        StaticJsonDocument<320> resp;
                        resp["id"] = req_id;
                        resp["success"] = ok;
                        JsonObject result = resp["result"].to<JsonObject>();
                        result["index"] = idx;
                        result["count"] = gpio->encoder_count;
                        (void)send_json_doc(server, resp);
                    }
                } else if (strcmp(method, "reset_encoder") == 0) {
                    if (!server->gpio_manager) {
                        (void)telnet_server_send_error(server, "gpio_manager_not_available", req_id);
                    } else if (!is_json) {
                        (void)telnet_server_send_error(server, "reset_encoder_requires_jsonrpc", req_id);
                    } else {
                        gpio_manager_t* gpio = (gpio_manager_t*)server->gpio_manager;
                        JsonObject params = req["params"].is<JsonObject>() ? req["params"].as<JsonObject>() : JsonObject();
                        int idx = params["index"] | -1;
                        boolean ok = false;
                        if (idx >= 0 && idx < (int)gpio->encoder_count) {
                            gpio_manager_reset_encoder(gpio, (uint8)idx);
                            ok = true;
                        }

                        StaticJsonDocument<320> resp;
                        resp["id"] = req_id;
                        resp["success"] = ok;
                        JsonObject result = resp["result"].to<JsonObject>();
                        result["index"] = idx;
                        (void)send_json_doc(server, resp);
                    }
                // USB keyboard test methods removed - use working TinyUSB implementation
                } else if (strcmp(method, "get_gpio_channels") == 0) {
                    Serial.println("[Telnet] Processing get_gpio_channels command");
                    if (!server->gpio_manager) {
                        Serial.println("[Telnet] ERROR: gpio_manager not available");
                        (void)telnet_server_send_error(server, "gpio_manager_not_available", req_id);
                    } else {
                        gpio_manager_t* gpio = (gpio_manager_t*)server->gpio_manager;
                        Serial.printf("[Telnet] GPIO channels count: %d\n", gpio->channel_count);
                        // Уменьшен размер для экономии стека
                        StaticJsonDocument<2048> resp;
                        resp["id"] = req_id;
                        resp["success"] = true;
                        JsonObject result = resp["result"].to<JsonObject>();
                        JsonArray arr = result["channels"].to<JsonArray>();

                        for (uint8 i = 0; i < gpio->channel_count; i++) {
                            const gpio_channel_t* ch = &gpio->channels[i];
                            JsonObject o = arr.add<JsonObject>();
                            o["index"] = i;
                            o["enabled"] = ch->enabled;
                            o["type"] = (int)ch->type;
                            o["gpio_pin"] = ch->gpio_pin;
                            o["mcp23s17_port"] = ch->mcp23s17_port;
                            o["usb_key_code"] = ch->usb_key_code;
                            o["usb_modifiers"] = ch->usb_modifiers;
                            o["pdo_byte"] = ch->pdo_byte;
                            o["pdo_bit"] = ch->pdo_bit;
                            o["invert"] = ch->invert;
                            if (ch->name[0]) o["name"] = ch->name;
                        }
                        result["count"] = gpio->channel_count;
                        boolean sent = send_json_doc(server, resp);
                        if (!sent) {
                            Serial.println("[Telnet] ERROR: Failed to send get_gpio_channels response");
                        }
                    }
                } else if (strcmp(method, "get_encoders") == 0) {
                    Serial.println("[Telnet] Processing get_encoders command");
                    if (!server->gpio_manager) {
                        Serial.println("[Telnet] ERROR: gpio_manager not available");
                        (void)telnet_server_send_error(server, "gpio_manager_not_available", req_id);
                    } else {
                        gpio_manager_t* gpio = (gpio_manager_t*)server->gpio_manager;
                        Serial.printf("[Telnet] Encoders count: %d\n", gpio->encoder_count);
                        // Уменьшен размер для экономии стека
                        StaticJsonDocument<1536> resp;
                        resp["id"] = req_id;
                        resp["success"] = true;
                        JsonObject result = resp["result"].to<JsonObject>();
                        JsonArray arr = result["encoders"].to<JsonArray>();

                        for (uint8 i = 0; i < gpio->encoder_count; i++) {
                            const gpio_encoder_t* enc = &gpio->encoders[i];
                            JsonObject o = arr.add<JsonObject>();
                            o["index"] = i;
                            o["enabled"] = enc->enabled;
                            o["pin_a"] = enc->pin_a;
                            o["pin_b"] = enc->pin_b;
                            o["pdo_byte_start"] = enc->pdo_byte_start;
                            o["counter"] = gpio_manager_get_encoder_count(gpio, i);
                            if (enc->name[0]) o["name"] = enc->name;
                        }
                        result["count"] = gpio->encoder_count;
                        boolean sent = send_json_doc(server, resp);
                        if (!sent) {
                            Serial.println("[Telnet] ERROR: Failed to send get_encoders response");
                        }
                    }
                } else if (strcmp(method, "scan_usb_keyboard") == 0) {
                    #ifdef CONFIG_IDF_TARGET_ESP32S3
                    JsonObject params = req["params"].is<JsonObject>() ? req["params"].as<JsonObject>() : JsonObject();
                    uint32_t duration_ms = params["duration_ms"] | 10000;  // По умолчанию 10 секунд
                    boolean started = usb_keyboard_start_scan(duration_ms);
                    StaticJsonDocument<256> resp;
                    resp["id"] = req_id;
                    resp["success"] = started;
                    if (started) {
                        resp["result"]["message"] = "Сканирование начато";
                        resp["result"]["duration_ms"] = duration_ms;
                    } else {
                        resp["error"] = "Не удалось начать сканирование";
                    }
                    (void)send_json_doc(server, resp);
                    #else
                    StaticJsonDocument<256> resp;
                    resp["id"] = req_id;
                    resp["success"] = false;
                    resp["error"] = "USB Keyboard scanner not supported on this platform";
                    (void)send_json_doc(server, resp);
                    #endif
                } else if (strcmp(method, "get_usb_keyboard_map") == 0) {
                    #ifdef CONFIG_IDF_TARGET_ESP32S3
                    StaticJsonDocument<2048> resp;
                    resp["id"] = req_id;
                    resp["success"] = true;
                    JsonObject result = resp["result"].to<JsonObject>();
                    JsonArray keys = result["keys"].to<JsonArray>();
                    
                    uint8 key_map[128];
                    uint8 map_size = sizeof(key_map);
                    if (usb_keyboard_get_key_map(key_map, &map_size)) {
                        for (uint8 i = 0; i < map_size; i++) {
                            JsonObject key = keys.add<JsonObject>();
                            key["code"] = key_map[i];
                            key["name"] = usb_keyboard_get_key_name(key_map[i]);
                        }
                        result["count"] = map_size;
                    } else {
                        resp["success"] = false;
                        resp["error"] = "Failed to get keyboard map";
                    }
                    (void)send_json_doc(server, resp);
                    #else
                    StaticJsonDocument<256> resp;
                    resp["id"] = req_id;
                    resp["success"] = false;
                    resp["error"] = "USB Keyboard not supported on this platform";
                    (void)send_json_doc(server, resp);
                    #endif
                } else if (strcmp(method, "save_config") == 0) {
                    // Явное сохранение конфигурации в Flash
                    Serial.println("[Telnet] Processing save_config command");
                    if (!server->gpio_manager) {
                        (void)telnet_server_send_error(server, "gpio_manager_not_available", req_id);
                    } else {
                        gpio_manager_t* gpio = (gpio_manager_t*)server->gpio_manager;
                        boolean saved = gpio_manager_save_to_flash(gpio);
                        StaticJsonDocument<256> resp;
                        resp["id"] = req_id;
                        resp["success"] = saved;
                        JsonObject result = resp["result"].to<JsonObject>();
                        result["message"] = saved ? "Configuration saved to Flash" : "Failed to save configuration";
                        result["channels"] = gpio->channel_count;
                        result["encoders"] = gpio->encoder_count;
                        boolean sent = send_json_doc(server, resp);
                        if (!sent) {
                            Serial.println("[Telnet] ERROR: Failed to send save_config response");
                        }
                    }
                } else {
                    StaticJsonDocument<256> resp;
                    resp["id"] = req_id;
                    resp["success"] = true;
                    JsonObject result = resp["result"].to<JsonObject>();
                    result["echo"] = cmd;
                    result["message"] = "Command not implemented yet";
                    (void)send_json_doc(server, resp);
                }

                server->commands_processed++;
            }
        }
    }
}

// ============================================================================
// Response Sending
// ============================================================================

boolean telnet_server_send_response(telnet_server_t* server, const char* response) {
    if (server == NULL || !server->running || server->w5500 == NULL || response == NULL) {
        Serial.println("[Telnet] ERROR: send_response - invalid parameters");
        return false;
    }
    
    if (!server->w5500->connected) {
        Serial.println("[Telnet] ERROR: send_response - not connected");
        return false;
    }
    
    uint16_t len = strlen(response);
    
    if (len == 0) {
        Serial.println("[Telnet] ERROR: send_response - empty response");
        return false;
    }
    
    Serial.printf("[Telnet] Sending response: %d bytes\n", len);
    
    // Пытаемся отправить с повторными попытками (максимум 3 попытки)
    boolean sent = false;
    for (int retry = 0; retry < 3 && !sent; retry++) {
        sent = w5500_tcp_send(server->w5500, (const uint8_t*)response, len);
        if (!sent && retry < 2) {
            Serial.printf("[Telnet] Send failed, retry %d/3\n", retry + 1);
            delay(10);  // Ждем 10ms перед повторной попыткой
        }
    }
    
    if (sent) {
        Serial.println("[Telnet] Response sent successfully");
    } else {
        Serial.println("[Telnet] ERROR: Failed to send response after 3 attempts");
        server->errors_count++;
    }
    
    return sent;
}

// Статический буфер для отправки ошибок (вне стека функции)
static char g_error_response_buffer[256];

boolean telnet_server_send_error(telnet_server_t* server, const char* error_message, int64_t id) {
    if (server == NULL || error_message == NULL) {
        return false;
    }
    
    // Используем статический буфер вместо локального для экономии стека
    // Используем %lld для int64_t
    int n = snprintf(g_error_response_buffer, sizeof(g_error_response_buffer),
            "{\"id\":%lld,\"success\":false,\"error\":\"%s\"}\r\n",
            (long long)id, error_message);
    
    if (n < 0 || n >= (int)sizeof(g_error_response_buffer)) {
        // Fallback: простой ответ без форматирования
        const char* simple = "{\"id\":1,\"success\":false,\"error\":\"unknown\"}\r\n";
        return telnet_server_send_response(server, simple);
    }
    
    return telnet_server_send_response(server, g_error_response_buffer);
}

} // extern "C"
