/*
 * ESP32 EtherCAT Slave v4 - Main
 * 
 * Phase 1, Week 3, Day 19
 * ESP32 EtherCAT Slave v4
 */

#include <Arduino.h>
#include "ethercat_stack.h"
#include "w5500_tcp.h"
#include "network_config.h"
#include "telnet_server.h"
#include "ethercat_test.h"
#include "espnow_bridge.h"
#include "espnow_config_proxy.h"
#include "gpio_manager.h"
#include "mcp23s17.h"
#include "mcp2515.h"
#include "stepgen.h"
#include <SPI.h>

#ifdef CONFIG_IDF_TARGET_ESP32S3
#include <USB.h>
#include "tusb.h"
#include "usb_keyboard_scanner.h"
#endif

// ============================================================================
// Pin Definitions (из предыдущего проекта)
// ============================================================================

// SPI Bus 1 (Основная) - EtherCAT
#define SPI1_SCK           36  // SPI Clock
#define SPI1_MISO          37  // SPI MISO
#define SPI1_MOSI          35  // SPI MOSI

// SPI Bus 2 (Периферия) - MCP23S17 / MCP2515 (CAN)
// IMPORTANT: Пины нужно выставить под вашу разводку.
// Эти значения выбраны как "безопасные по умолчанию" (не пересекаются с SPI1/W5500 CS/RST).
#define SPI2_SCK           17
#define SPI2_MISO          16
#define SPI2_MOSI          15

// CAN (MCP2515) pins (SPI Bus 2)
// IMPORTANT: выставить под вашу разводку.
#define MCP2515_CS         18
#define MCP2515_INT        0xFF  // 0xFF = не используем INT pin (polling)
#define CAN_BITRATE        500000

// W5500 IN (EtherCAT RX)
#define W5500_CS_IN        40  // W5500 IN Chip Select
#define W5500_RST_IN       39  // W5500 IN Reset

// W5500 OUT (EtherCAT TX)
#define W5500_CS_OUT       38  // W5500 OUT Chip Select
#define W5500_RST_OUT      41  // W5500 OUT Reset

// SPI Bus (shared для обоих W5500)
#define SPI_FREQ 20000000  // 20 MHz

// ============================================================================
// MAC Address
// ============================================================================

// MAC адрес по умолчанию (можно изменить через конфигурацию)
static const uint8_t DEFAULT_MAC[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};

// ============================================================================
// Global Variables
// ============================================================================

static ethercat_stack_t g_ethercat_stack;

// Telnet Server (работает параллельно с EtherCAT)
static w5500_tcp_t g_telnet_w5500;
network_config_t g_network_config;  // extern для доступа из telnet_server.cpp
static telnet_server_t g_telnet_server;
static boolean g_telnet_server_running = false;

// GPIO Manager (Phase 2/5)
static gpio_manager_t g_gpio_manager;

// StepGen (LinuxCNC STEP/DIR) - Phase (future)
static stepgen_t g_stepgen;
static boolean g_stepgen_enabled = true;

// CAN Bridge (Phase 5, Week 9)
static mcp2515_t g_can;
static boolean g_can_enabled = false;  // включить когда подключен MCP2515

// ESP-NOW (Phase 5, Week 9)
static espnow_bridge_t g_espnow_bridge;
static espnow_config_proxy_t g_espnow_proxy;
static boolean g_espnow_enabled = true;

// Test Module (для симуляции EtherCAT фреймов)
static boolean g_test_enabled = true;  // Установить в true для включения теста

// ============================================================================
// Setup
// ============================================================================

void setup() {
    // Инициализация Serial
    Serial.begin(115200);
    delay(1000);
    
    Serial.println();
    Serial.println("========================================");
    Serial.println("ESP32 EtherCAT Slave v4");
    Serial.println("========================================");
    Serial.println();
    
    // Инициализация SPI Bus 1 (EtherCAT)
    Serial.println("[MAIN] Initializing SPI Bus 1 (EtherCAT)...");
    SPI.begin(SPI1_SCK, SPI1_MISO, SPI1_MOSI, -1);  // -1 = manual CS
    SPI.setFrequency(SPI_FREQ);
    SPI.setDataMode(SPI_MODE0);
    SPI.setBitOrder(MSBFIRST);
    Serial.printf("[MAIN] ✓ SPI Bus 1 initialized: SCK=%d, MISO=%d, MOSI=%d\n", 
                  SPI1_SCK, SPI1_MISO, SPI1_MOSI);
    Serial.println();

    // ====================================================================
    // SPI Bus 2 (Peripherals: MCP23S17 / MCP2515)
    // ====================================================================
    Serial.println("[MAIN] Initializing SPI Bus 2 (Peripherals)...");
    mcp23s17_spi2_init(SPI2_SCK, SPI2_MISO, SPI2_MOSI);
    Serial.println();

    // ====================================================================
    // CAN Bridge (MCP2515) - optional
    // ====================================================================
    if (g_can_enabled) {
        Serial.println("[MAIN] Initializing CAN (MCP2515)...");
        if (mcp2515_init(&g_can, MCP2515_CS, MCP2515_INT, CAN_BITRATE)) {
            Serial.println("[MAIN] ✓ CAN (MCP2515) initialized");
        } else {
            Serial.println("[MAIN] ERROR: Failed to initialize CAN (MCP2515)");
        }
        Serial.println();
    }

    #ifdef CONFIG_IDF_TARGET_ESP32S3
    // USB HID Keyboard инициализируется в GPIO Manager
    #endif

    // ====================================================================
    // GPIO Manager
    // ====================================================================
    Serial.println("[MAIN] Initializing GPIO Manager...");
    if (gpio_manager_init(&g_gpio_manager)) {
        Serial.println("[MAIN] ✓ GPIO Manager initialized");
    } else {
        Serial.println("[MAIN] ERROR: Failed to initialize GPIO Manager");
    }
    Serial.println();
    
    #ifdef CONFIG_IDF_TARGET_ESP32S3
    usb_keyboard_scanner_init();
    // Передаем указатель на Telnet сервер для отправки данных о нажатых клавишах
    usb_keyboard_scanner_set_telnet_server(&g_telnet_server);
    #endif
    

    // ====================================================================
    // StepGen (STEP/DIR) - optional scaffolding
    // ====================================================================
    if (g_stepgen_enabled) {
        Serial.println("[MAIN] Initializing StepGen...");
        if (stepgen_init(&g_stepgen)) {
            Serial.println("[MAIN] ✓ StepGen initialized");
        } else {
            Serial.println("[MAIN] ERROR: Failed to initialize StepGen");
        }
        Serial.println();
    }

    // ====================================================================
    // ESP-NOW Bridge (WiFi STA) - Phase 5
    // ====================================================================
    if (g_espnow_enabled) {
        Serial.println("[MAIN] Initializing ESP-NOW Bridge...");
        if (espnow_bridge_init(&g_espnow_bridge, NULL, NULL)) {
            Serial.println("[MAIN] ✓ ESP-NOW Bridge initialized");
            espnow_config_proxy_init(&g_espnow_proxy);
            Serial.println("[MAIN] ✓ ESP-NOW Config Proxy initialized");
        } else {
            Serial.println("[MAIN] ERROR: Failed to initialize ESP-NOW Bridge");
        }
        Serial.println();
    }
    
    // ====================================================================
    // Инициализация EtherCAT Stack (Socket 0 на W5500_IN/OUT)
    // ====================================================================
    Serial.println("[MAIN] Initializing EtherCAT Stack...");
    if (ethercat_stack_init(&g_ethercat_stack,
                           W5500_CS_IN, W5500_RST_IN,
                           W5500_CS_OUT, W5500_RST_OUT,
                           DEFAULT_MAC)) {
        Serial.println("[MAIN] ✓ EtherCAT Stack initialized");

        // Подключаем GPIO manager к PDO (Phase 2)
        ethercat_stack_set_gpio_manager(&g_ethercat_stack, &g_gpio_manager);

        // PDO forwarding через ESP-NOW (Phase 5, Week 9)
        if (g_espnow_enabled && g_espnow_bridge.initialized) {
            ethercat_stack_set_espnow_bridge(&g_ethercat_stack, &g_espnow_bridge);
            // По умолчанию выключено (0ms), чтобы не влиять на 1ms EtherCAT цикл.
            // Включайте при необходимости (например 5..20ms).
            ethercat_stack_set_espnow_pdo_tx_interval_ms(&g_ethercat_stack, 0);
        } else {
            ethercat_stack_set_espnow_bridge(&g_ethercat_stack, NULL);
            ethercat_stack_set_espnow_pdo_tx_interval_ms(&g_ethercat_stack, 0);
        }

        // CAN bridge (PDO ↔ CAN) - optional
        if (g_can_enabled && g_can.initialized) {
            ethercat_stack_set_can_bridge(&g_ethercat_stack, &g_can);
            ethercat_stack_set_can_pdo_tx_interval_ms(&g_ethercat_stack, 10); // 100 Hz default
        } else {
            ethercat_stack_set_can_bridge(&g_ethercat_stack, NULL);
            ethercat_stack_set_can_pdo_tx_interval_ms(&g_ethercat_stack, 0);
        }

        // StepGen (STEP/DIR) - optional
        ethercat_stack_set_stepgen(&g_ethercat_stack, g_stepgen_enabled ? &g_stepgen : NULL);
        
        // Запуск EtherCAT Stack
        if (ethercat_stack_start(&g_ethercat_stack)) {
            Serial.println("[MAIN] ✓ EtherCAT Stack started");
        } else {
            Serial.println("[MAIN] ERROR: Failed to start EtherCAT Stack");
        }
    } else {
        Serial.println("[MAIN] ERROR: Failed to initialize EtherCAT Stack");
    }
    
    Serial.println();
    
    // ====================================================================
    // Инициализация Telnet Server (Socket 1 на W5500_IN, параллельно с EtherCAT)
    // ====================================================================
    Serial.println("[MAIN] Initializing Network Configuration...");
    uint16_t station_address = 1;  // По умолчанию, можно получить из EtherCAT memory
    if (network_config_init(&g_network_config, station_address)) {
        Serial.println("[MAIN] ✓ Network Configuration initialized");
    } else {
        Serial.println("[MAIN] ERROR: Network Configuration init failed");
    }
    
    // Инициализация W5500 TCP для Telnet (Socket 1 на W5500_IN)
    // Socket 0 используется для EtherCAT, Socket 1 для Telnet - работают параллельно
    Serial.println("[MAIN] Initializing W5500 TCP for Telnet (Socket 1 on W5500_IN)...");
    const uint8_t telnet_mac[6] = {0x02, 0x00, 0x00, 0x00, 0x00, 0x01};  // Тот же MAC что и для EtherCAT
    
    if (w5500_tcp_init(&g_telnet_w5500,
                      W5500_CS_IN, W5500_RST_IN,
                      telnet_mac,
                      g_network_config.ip,
                      g_network_config.subnet,
                      g_network_config.gateway,
                      20000000)) {
        Serial.println("[MAIN] ✓ W5500 TCP initialized for Telnet");
        
        // Инициализация Telnet Server
        Serial.println("[MAIN] Initializing Telnet Server...");
        if (telnet_server_init(&g_telnet_server, &g_telnet_w5500, 
                              &g_ethercat_stack, NULL, &g_gpio_manager,
                              g_stepgen_enabled ? &g_stepgen : NULL,
                              g_espnow_enabled ? &g_espnow_bridge : NULL,
                              g_espnow_enabled ? &g_espnow_proxy : NULL)) {
            Serial.println("[MAIN] ✓ Telnet Server initialized");
            
            // Запуск Telnet Server
            if (telnet_server_start(&g_telnet_server)) {
                Serial.println("[MAIN] ✓ Telnet Server started");
                g_telnet_server_running = true;
            } else {
                Serial.println("[MAIN] ERROR: Failed to start Telnet Server");
            }
        } else {
            Serial.println("[MAIN] ERROR: Failed to initialize Telnet Server");
        }
    } else {
        Serial.println("[MAIN] ERROR: Failed to initialize W5500 TCP for Telnet");
    }
    
    Serial.println();
    Serial.println("[MAIN] EtherCAT Stack: Socket 0 (MACRAW mode)");
    Serial.println("[MAIN] Telnet Server: Socket 1 (TCP mode, port 23)");
    Serial.printf("[MAIN] Telnet server listening on %d.%d.%d.%d:23\n",
                 g_network_config.ip[0], g_network_config.ip[1],
                 g_network_config.ip[2], g_network_config.ip[3]);
    Serial.println("[MAIN] Connect with: telnet <IP> 23");
    Serial.println();
    Serial.println("[MAIN] Waiting for EtherCAT Master...");
    Serial.println("[MAIN] Use SOEM 'slaveinfo' to detect this device");
    
    // ====================================================================
    // Инициализация Test Module (для симуляции EtherCAT фреймов)
    // ====================================================================
    // Test Module отключен - больше не используется
    // if (g_test_enabled) { ... }
    
    Serial.println();
    Serial.println("========================================");
}

// ============================================================================
// Loop
// ============================================================================

void loop() {
    // EtherCAT обработка происходит в FreeRTOS tasks
    // Здесь обрабатываем Telnet Server и периодические задачи
    
    // Обработка Telnet Server
    if (g_telnet_server_running) {
        telnet_server_process(&g_telnet_server);
    }

    // ESP-NOW processing (callbacks) + future periodic tasks
    if (g_espnow_enabled) {
        espnow_bridge_process(&g_espnow_bridge);
    }
    
    // Обработка Test Module
    if (g_test_enabled) {
        ethercat_test_process();
        
        // Обработка Serial команд для теста
        if (Serial.available() > 0) {
            String cmd = Serial.readStringUntil('\n');
            cmd.trim();
            
            if (cmd == "test start" || cmd == "test start 1ms") {
                ethercat_test_config_t config;
                config.enabled = true;
                config.frame_interval_us = 1000;  // 1ms между фреймами - сервопериод 1 мс
                config.test_duration_sec = 30;    // 30 секунд
                config.frame_type = 0;            // APRD
                config.measure_latency = true;
                config.parallel_telnet = g_telnet_server_running;
                config.loopback_mode = true;      // Loopback для теста без Master
                
                Serial.println("[TEST] Starting test with 1ms servo period (loopback mode)");
                ethercat_test_start(&config);
            } else if (cmd == "test start 4ms") {
                ethercat_test_config_t config;
                config.enabled = true;
                config.frame_interval_us = 4000;  // 4ms между фреймами - сервопериод 4 мс
                config.test_duration_sec = 30;    // 30 секунд
                config.frame_type = 0;            // APRD
                config.measure_latency = true;
                config.parallel_telnet = g_telnet_server_running;
                config.loopback_mode = true;      // Loopback для теста без Master
                
                Serial.println("[TEST] Starting test with 4ms servo period (loopback mode)");
                ethercat_test_start(&config);
            } else if (cmd == "test stop") {
                ethercat_test_stop();
            } else if (cmd == "test stats") {
                ethercat_test_stats_t stats;
                ethercat_test_get_stats(&stats);
                
                Serial.println();
                Serial.println("========================================");
                Serial.println("EtherCAT Test Statistics");
                Serial.println("========================================");
                Serial.printf("Frames sent: %lu\n", stats.frames_sent);
                Serial.printf("Frames received: %lu\n", stats.frames_received);
                Serial.printf("Frames dropped: %lu\n", stats.frames_dropped);
                Serial.printf("Telnet commands: %lu\n", stats.telnet_commands);
                
                if (stats.latency_samples > 0) {
                    uint32_t avg_latency = stats.total_latency_us / stats.latency_samples;
                    Serial.printf("Latency - Min: %lu us, Max: %lu us, Avg: %lu us\n",
                                stats.min_latency_us, stats.max_latency_us, avg_latency);
                }
                
                Serial.printf("Test duration: %lu ms\n", stats.test_duration_ms);
                Serial.printf("CPU load: %lu%%\n", stats.cpu_load_percent);
                Serial.printf("Heap free (min): %lu bytes\n", stats.heap_free_min);
                Serial.println("========================================");
                Serial.println();
            } else if (cmd == "usb test" || cmd == "test usb") {
                // Тест USB Keyboard с подробным выводом
                // USB keyboard test functions removed - use working TinyUSB implementation
                Serial.println("[MAIN] USB keyboard test functions removed");
            } else if (cmd.startsWith("button test ")) {
                // Простой тест физической кнопки: "button test <pin>"
                // Пример: "button test 14" - тест кнопки на GPIO 14
                int pin = cmd.substring(12).toInt();
                if (pin > 0 && pin < 48) {
                    Serial.println();
                    Serial.println("========================================");
                    Serial.printf("Button Test on GPIO %d\n", pin);
                    Serial.println("========================================");
                    Serial.println("Press the button to see output in Serial Monitor");
                    Serial.println("Press any key in Serial Monitor to stop");
                    Serial.println("----------------------------------------");
                    
                    pinMode(pin, INPUT_PULLUP);
                    int last_state = digitalRead(pin);
                    uint32_t last_press_time = 0;
                    
                    while (Serial.available() == 0) {
                        int current_state = digitalRead(pin);
                        
                        // Обнаружение нажатия (переход с HIGH на LOW для INPUT_PULLUP)
                        if (last_state == HIGH && current_state == LOW) {
                            uint32_t now = millis();
                            // Защита от дребезга контактов (debounce)
                            if (now - last_press_time > 50) {
                                Serial.printf("[BUTTON] GPIO %d PRESSED at %lu ms\n", pin, now);
                                last_press_time = now;
                            }
                        }
                        // Обнаружение отпускания (переход с LOW на HIGH)
                        else if (last_state == LOW && current_state == HIGH) {
                            uint32_t now = millis();
                            if (now - last_press_time > 50) {
                                Serial.printf("[BUTTON] GPIO %d RELEASED at %lu ms\n", pin, now);
                            }
                        }
                        
                        last_state = current_state;
                        delay(10);  // Небольшая задержка
                    }
                    
                    // Очищаем буфер Serial
                    while (Serial.available() > 0) {
                        Serial.read();
                    }
                    
                    Serial.println("----------------------------------------");
                    Serial.println("Button test stopped");
                    Serial.println("========================================");
                    Serial.println();
                } else {
                    Serial.printf("ERROR: Invalid pin number %d. Use: button test <pin>\n", pin);
                    Serial.println("Example: button test 14");
                }
            }
        }
    }
    
    // Вывод статистики каждые 10 секунд
    static uint32_t last_stats_time = 0;
    uint32_t now = millis();
    
    if (now - last_stats_time > 10000) {
        last_stats_time = now;
        
        uint32_t rx = 0, tx = 0, dropped = 0;
        ethercat_stack_get_stats(&g_ethercat_stack, &rx, &tx, &dropped);
        
        ec_state state = ethercat_stack_get_state(&g_ethercat_stack);
        const char* state_str = "UNKNOWN";
        switch (state) {
            case EC_STATE_INIT: state_str = "INIT"; break;
            case EC_STATE_PRE_OP: state_str = "PRE_OP"; break;
            case EC_STATE_SAFE_OP: state_str = "SAFE_OP"; break;
            case EC_STATE_OPERATIONAL: state_str = "OPERATIONAL"; break;
            default: break;
        }
        
        // Убрали вывод статистики в Serial Monitor (засоряет вывод, статистика доступна через Telnet API)
        // Serial.printf("[STATS] State: %s, RX: %lu, TX: %lu, Dropped: %lu",
        //              state_str, rx, tx, dropped);
        // if (g_telnet_server_running) {
        //     Serial.printf(", Telnet: running");
        // }
        // Serial.println();
    }
    
    #ifdef CONFIG_IDF_TARGET_ESP32S3
    // tud_task();  // Отключено - используется USB Host Scanner
    usb_keyboard_scanner_task();
    #endif
    
    // Минимальная задержка для снижения нагрузки на CPU
    // Для сервопериода 1-4 мс критично минимизировать задержки
    delay(1);  // 1 мс вместо 10 мс для сервоприводов
}
