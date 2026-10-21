/*
 * EtherCAT Test Functions - Implementation
 * 
 * Phase 3, Week 6 - Performance Testing
 * ESP32 EtherCAT Slave v4
 */

#include "ethercat_test.h"
#include "ethercat_frame.h"
#include "ethercat_stack.h"
#include "ethercat_memory.h"
#include <Arduino.h>
#include <string.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

extern "C" {

// ============================================================================
// Global Variables
// ============================================================================

static w5500_macraw_t* g_w5500_in = NULL;
static w5500_macraw_t* g_w5500_out = NULL;
static void* g_ethercat_stack = NULL;  // Для loopback режима
static ethercat_test_config_t g_test_config;
static ethercat_test_stats_t g_test_stats;
static boolean g_test_running = false;
static TaskHandle_t g_test_task_handle = NULL;

// ============================================================================
// Test Frame Generation
// ============================================================================

uint16_t ethercat_test_create_frame(uint8_t* frame, uint8_t frame_type, 
                                    uint16_t address, uint16_t data) {
    if (frame == NULL) {
        return 0;
    }
    
    // Ethernet Header (14 bytes)
    // Destination MAC: Broadcast (для теста)
    frame[0] = 0xFF; frame[1] = 0xFF; frame[2] = 0xFF;
    frame[3] = 0xFF; frame[4] = 0xFF; frame[5] = 0xFF;
    
    // Source MAC: ESP32
    frame[6] = 0x02; frame[7] = 0x00; frame[8] = 0x00;
    frame[9] = 0x00; frame[10] = 0x00; frame[11] = 0x01;
    
    // EtherType: EtherCAT (0x88A4)
    frame[12] = 0x88;
    frame[13] = 0xA4;
    
    // EtherCAT Datagram Header (10 bytes) - ec_comt структура начинается сразу после Ethernet header
    uint16_t offset = ETH_HEADERSIZE;  // 14 bytes
    
    ec_comt* datagram = (ec_comt*)(frame + offset);
    uint16_t dlength = 0;
    uint16_t data_len = 2;  // 2 байта данных для чтения/записи
    
    // Заполняем структуру datagram
    switch (frame_type) {
        case 0: {  // APRD (Auto Increment Physical Read)
            datagram->command = EC_CMD_APRD;  // 0x02
            datagram->index = 0;
            datagram->ADP = htoes((uint16_t)(address & 0xFFFF));  // Младшие 16 бит адреса
            datagram->ADO = htoes((uint16_t)(address >> 16));     // Старшие 16 бит адреса
            datagram->dlength = htoes(data_len);  // Длина данных (2 байта)
            datagram->irpt = htoes(0);  // Interrupt
            
            dlength = data_len;
            break;
        }
        case 1: {  // APWR (Auto Increment Physical Write)
            datagram->command = EC_CMD_APWR;  // 0x03
            datagram->index = 0;
            datagram->ADP = htoes((uint16_t)(address & 0xFFFF));
            datagram->ADO = htoes((uint16_t)(address >> 16));
            datagram->dlength = htoes(data_len);
            datagram->irpt = htoes(0);
            
            // Данные для записи (после header)
            uint8_t* data_ptr = (uint8_t*)(datagram + 1);
            data_ptr[0] = (data >> 0) & 0xFF;
            data_ptr[1] = (data >> 8) & 0xFF;
            
            dlength = data_len;
            break;
        }
        case 2: {  // LRW (Logical Read Write)
            datagram->command = EC_CMD_LRW;  // 0x0B
            datagram->index = 0;
            datagram->ADP = htoes((uint16_t)(address & 0xFFFF));
            datagram->ADO = htoes((uint16_t)(address >> 16));
            datagram->dlength = htoes(data_len);
            datagram->irpt = htoes(0);
            
            // Данные для записи (после header)
            uint8_t* data_ptr = (uint8_t*)(datagram + 1);
            data_ptr[0] = (data >> 0) & 0xFF;
            data_ptr[1] = (data >> 8) & 0xFF;
            
            dlength = data_len;
            break;
        }
        default:
            return 0;
    }
    
    // Устанавливаем elength в начале структуры ec_comt
    // elength = размер datagram header (10) + данные (dlength) + WKC (2)
    uint16_t elength = EC_HEADERSIZE + dlength + EC_WKCSIZE;
    datagram->elength = htoes(elength);
    
    // Добавить WKC (Working Counter) в конце datagram
    uint16_t* wkc_ptr = (uint16_t*)(frame + offset + EC_HEADERSIZE + dlength);
    *wkc_ptr = htoes(0);  // Начальное значение WKC = 0
    
    // Общая длина фрейма: Ethernet header (14) + datagram header (10) + данные (dlength) + WKC (2)
    return ETH_HEADERSIZE + EC_HEADERSIZE + dlength + EC_WKCSIZE;
}

// ============================================================================
// Test Task (FreeRTOS)
// ============================================================================

static void ethercat_test_task(void* param) {
    uint8_t tx_frame[64];
    uint8_t rx_frame[64];
    uint32_t last_frame_time = 0;
    uint32_t frame_counter = 0;
    
    Serial.println("[TEST] Test task started");
    
    while (g_test_running) {
        uint32_t now = micros();
        
        // Отправка фрейма с заданным интервалом
        if (now - last_frame_time >= g_test_config.frame_interval_us) {
            last_frame_time = now;
            
            // Создать тестовый фрейм
            uint16_t address = 0x0000;  // Type register
            uint16_t data = 0x0001;
            uint16_t frame_len = ethercat_test_create_frame(tx_frame, 
                                                           g_test_config.frame_type,
                                                           address, 
                                                           data);
            
            if (frame_len > 0) {
                uint32_t send_start = micros();
                
                // Отладка первых фреймов
                static uint32_t debug_counter = 0;
                if (debug_counter < 3) {
                    Serial.printf("[TEST] DEBUG: frame_len=%d, loopback=%d, stack=%p\n", 
                                frame_len, g_test_config.loopback_mode, g_ethercat_stack);
                    Serial.printf("[TEST] DEBUG: EtherType=0x%02X%02X\n", tx_frame[12], tx_frame[13]);
                    debug_counter++;
                }
                
                if (g_test_config.loopback_mode && g_ethercat_stack != NULL) {
                    // ВНУТРЕННИЙ LOOPBACK: обрабатываем фрейм напрямую через EtherCAT Stack
                    // Это имитирует получение фрейма от Master без физического loopback
                    ethercat_stack_t* stack = (ethercat_stack_t*)g_ethercat_stack;
                    
                    // Проверяем, что это EtherCAT фрейм
                    boolean is_ethercat = ethercat_frame_is_ethercat(tx_frame);
                    if (debug_counter <= 3) {
                        Serial.printf("[TEST] DEBUG: is_ethercat=%d\n", is_ethercat);
                    }
                    
                    if (is_ethercat) {
                        // Парсим и обрабатываем фрейм напрямую (как в RX task)
                        uint16_t datagrams = ethercat_frame_parse(
                            tx_frame,
                            frame_len,
                            ethercat_memory_get_raw(&stack->memory),
                            ETHERCAT_MEMORY_SIZE
                        );
                        
                        if (datagrams > 0) {
                            // Фрейм обработан успешно, теперь отправляем ответ
                            if (w5500_macraw_send(g_w5500_out, tx_frame, frame_len)) {
                                g_test_stats.frames_sent++;
                                frame_counter++;
                                
                                // Измеряем latency (внутренняя обработка + отправка)
                                if (g_test_config.measure_latency) {
                                    uint32_t latency = micros() - send_start;
                                    g_test_stats.frames_received++;
                                    
                                    if (g_test_stats.latency_samples == 0) {
                                        g_test_stats.min_latency_us = latency;
                                        g_test_stats.max_latency_us = latency;
                                    } else {
                                        if (latency < g_test_stats.min_latency_us) {
                                            g_test_stats.min_latency_us = latency;
                                        }
                                        if (latency > g_test_stats.max_latency_us) {
                                            g_test_stats.max_latency_us = latency;
                                        }
                                    }
                                    g_test_stats.total_latency_us += latency;
                                    g_test_stats.latency_samples++;
                                }
                            } else {
                                // Отладка: почему не отправляется
                                if (frame_counter < 5) {
                                    Serial.printf("[TEST] DEBUG: w5500_macraw_send failed (frame %lu)\n", frame_counter);
                                }
                                g_test_stats.frames_dropped++;
                            }
                        } else {
                            // Отладка: почему не парсится
                            if (frame_counter < 5) {
                                Serial.printf("[TEST] DEBUG: parse returned 0 datagrams (frame %lu)\n", frame_counter);
                            }
                            g_test_stats.frames_dropped++;
                        }
                    } else {
                        // Отладка: почему не EtherCAT фрейм
                        if (frame_counter < 5) {
                            Serial.printf("[TEST] DEBUG: Not EtherCAT frame (frame_len=%d, EtherType=0x%02X%02X)\n", 
                                        frame_len, tx_frame[12], tx_frame[13]);
                        }
                        g_test_stats.frames_dropped++;
                    }
                } else {
                    // ОБЫЧНЫЙ РЕЖИМ: отправка через сеть
                    if (g_w5500_out != NULL) {
                        if (w5500_macraw_send(g_w5500_out, tx_frame, frame_len)) {
                            g_test_stats.frames_sent++;
                            frame_counter++;
                            
                            // Измерение задержки (если включено)
                            if (g_test_config.measure_latency) {
                                uint32_t latency = 0;
                                
                                if (g_test_config.loopback_mode) {
                                    // Внешний loopback: ждем ответ через сеть (нужен Ethernet switch)
                                    uint32_t timeout_start = micros();
                                    uint16_t rx_len = 0;
                                    uint32_t timeout_us = 2000;  // 2 мс таймаут
                                    
                                    while ((micros() - timeout_start) < timeout_us) {
                                        rx_len = w5500_macraw_receive(g_w5500_in, 
                                                                      rx_frame, 
                                                                      sizeof(rx_frame));
                                        if (rx_len > 0 && ethercat_frame_is_ethercat(rx_frame)) {
                                            latency = micros() - send_start;
                                            g_test_stats.frames_received++;
                                            
                                            if (g_test_stats.latency_samples == 0) {
                                                g_test_stats.min_latency_us = latency;
                                                g_test_stats.max_latency_us = latency;
                                            } else {
                                                if (latency < g_test_stats.min_latency_us) {
                                                    g_test_stats.min_latency_us = latency;
                                                }
                                                if (latency > g_test_stats.max_latency_us) {
                                                    g_test_stats.max_latency_us = latency;
                                                }
                                            }
                                            g_test_stats.total_latency_us += latency;
                                            g_test_stats.latency_samples++;
                                            break;
                                        }
                                        vTaskDelay(pdMS_TO_TICKS(0));  // Yield
                                    }
                                    
                                    if (rx_len == 0) {
                                        g_test_stats.frames_dropped++;
                                    }
                                } else {
                                    // Обычный режим: просто измеряем время отправки
                                    latency = micros() - send_start;
                                    g_test_stats.frames_received++;  // Считаем как успешно отправленные
                                    
                                    if (g_test_stats.latency_samples == 0) {
                                        g_test_stats.min_latency_us = latency;
                                        g_test_stats.max_latency_us = latency;
                                    } else {
                                        if (latency < g_test_stats.min_latency_us) {
                                            g_test_stats.min_latency_us = latency;
                                        }
                                        if (latency > g_test_stats.max_latency_us) {
                                            g_test_stats.max_latency_us = latency;
                                        }
                                    }
                                    g_test_stats.total_latency_us += latency;
                                    g_test_stats.latency_samples++;
                                }
                            }
                        } else {
                            g_test_stats.frames_dropped++;
                        }
                    }
                }
            }
            
            // Проверка длительности теста
            uint32_t elapsed = millis() - g_test_stats.start_time_ms;
            if (elapsed >= (g_test_config.test_duration_sec * 1000)) {
                g_test_running = false;
                Serial.println("[TEST] Test duration reached, stopping...");
                break;
            }
            
            // Вывод прогресса каждые 1000 фреймов
            if (frame_counter % 1000 == 0) {
                Serial.printf("[TEST] Sent: %lu frames, Elapsed: %lu ms\n",
                             frame_counter, elapsed);
            }
        }
        
        // Минимальная задержка для снижения нагрузки
        // Для сервопериода 1-4 мс критично минимизировать задержки
        vTaskDelay(pdMS_TO_TICKS(0));  // 0 мс - только yield CPU
    }
    
    Serial.println("[TEST] Test task stopped");
    g_test_task_handle = NULL;
    vTaskDelete(NULL);
}

// ============================================================================
// API Functions
// ============================================================================

boolean ethercat_test_init(w5500_macraw_t* w5500_in, w5500_macraw_t* w5500_out, void* ethercat_stack) {
    if (w5500_in == NULL || w5500_out == NULL) {
        return false;
    }
    
    g_w5500_in = w5500_in;
    g_w5500_out = w5500_out;
    g_ethercat_stack = ethercat_stack;
    
    memset(&g_test_config, 0, sizeof(g_test_config));
    memset(&g_test_stats, 0, sizeof(g_test_stats));
    
    Serial.println("[TEST] Test module initialized");
    if (ethercat_stack != NULL) {
        Serial.println("[TEST] EtherCAT Stack available for loopback");
    }
    
    return true;
}

boolean ethercat_test_start(const ethercat_test_config_t* config) {
    if (config == NULL) {
        return false;
    }
    
    if (g_test_running) {
        Serial.println("[TEST] Test already running");
        return false;
    }
    
    memcpy(&g_test_config, config, sizeof(ethercat_test_config_t));
    
    // Сброс статистики
    memset(&g_test_stats, 0, sizeof(g_test_stats));
    g_test_stats.start_time_ms = millis();
    g_test_stats.test_duration_ms = config->test_duration_sec * 1000;
    
    g_test_running = true;
    
    Serial.println("[TEST] Starting test...");
    Serial.printf("[TEST] Frame interval: %lu us\n", config->frame_interval_us);
    Serial.printf("[TEST] Duration: %lu seconds\n", config->test_duration_sec);
    Serial.printf("[TEST] Frame type: %d\n", config->frame_type);
    Serial.printf("[TEST] Measure latency: %s\n", config->measure_latency ? "YES" : "NO");
    Serial.printf("[TEST] Parallel Telnet: %s\n", config->parallel_telnet ? "YES" : "NO");
    Serial.printf("[TEST] Loopback mode: %s\n", config->loopback_mode ? "YES" : "NO");
    if (!config->loopback_mode) {
        Serial.println("[TEST] WARNING: Loopback mode OFF - frames will be lost without Master!");
    }
    
    // Создать FreeRTOS task для теста
    xTaskCreate(ethercat_test_task,
                "EtherCAT_Test",
                4096,  // Stack size
                NULL,
                5,  // Priority (ниже чем EtherCAT tasks)
                &g_test_task_handle);
    
    if (g_test_task_handle == NULL) {
        Serial.println("[TEST] ERROR: Failed to create test task");
        g_test_running = false;
        return false;
    }
    
    return true;
}

void ethercat_test_stop(void) {
    if (!g_test_running) {
        return;
    }
    
    Serial.println("[TEST] Stopping test...");
    g_test_running = false;
    
    // Подождать завершения task
    if (g_test_task_handle != NULL) {
        vTaskDelay(pdMS_TO_TICKS(100));
        g_test_task_handle = NULL;
    }
    
    Serial.println("[TEST] Test stopped");
}

void ethercat_test_process(void) {
    // Обработка происходит в FreeRTOS task
    // Здесь можно добавить дополнительную логику если нужно
}

void ethercat_test_get_stats(ethercat_test_stats_t* stats) {
    if (stats == NULL) {
        return;
    }
    
    memcpy(stats, &g_test_stats, sizeof(ethercat_test_stats_t));
    
    // Обновить текущее время
    stats->test_duration_ms = millis() - stats->start_time_ms;
}

void ethercat_test_reset_stats(void) {
    memset(&g_test_stats, 0, sizeof(g_test_stats));
    Serial.println("[TEST] Statistics reset");
}

} // extern "C"
