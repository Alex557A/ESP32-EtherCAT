/*
 * EtherCAT Stack Core - Implementation
 * 
 * Phase 1, Week 3, Day 17-18
 * ESP32 EtherCAT Slave v4
 */

#include "ethercat_stack.h"
#include "gpio_manager.h"
#include "espnow_bridge.h"
#include "mcp2515.h"
#include "stepgen.h"
#include <stddef.h>
#include <Arduino.h>
#include <string.h>
#include <esp_task_wdt.h>

// ============================================================================
// Forward Declarations
// ============================================================================

static void ethercat_rx_task(void* param);
static void ethercat_tx_task(void* param);

// ============================================================================
// Callbacks для Memory
// ============================================================================

static ethercat_stack_t* g_stack_instance = NULL;

static void on_al_control_write_callback(uint16 value) {
    if (g_stack_instance == NULL) {
        return;
    }
    
    Serial.printf("[FSM] Master requests state 0x%04X\n", value);
    
    ec_state new_state = (ec_state)(value & 0x0F);
    
    boolean success = ethercat_fsm_transition_to(&g_stack_instance->fsm, new_state);
    
    if (success) {
        Serial.printf("[FSM] Transition to 0x%04X OK\n", new_state);
    } else {
        Serial.printf("[FSM] Transition FAILED\n");
    }
}

static void on_station_address_write_callback(uint16 value) {
    Serial.printf("[CONFIG] Station Address set to 0x%04X\n", value);
}

// ============================================================================
// FreeRTOS Tasks
// ============================================================================

static void ethercat_rx_task(void* param) {
    ethercat_stack_t* stack = (ethercat_stack_t*)param;
    
    if (stack == NULL) {
        vTaskDelete(NULL);
        return;
    }
    
    Serial.println("[STACK] RX Task started");
    
    while (stack->running) {
        // Читаем фрейм (функция возвращает 0 если нет данных)
        uint16_t len = w5500_macraw_receive(&stack->w5500_in, 
                                            stack->rx_frame, 
                                            EC_MAXECATFRAME);
        
        if (len > 0) {
            // Проверяем, что это EtherCAT фрейм
            if (ethercat_frame_is_ethercat(stack->rx_frame)) {
                stack->frames_received++;
                stack->rx_frame_len = len;
                
                // Парсим и обрабатываем фрейм
                uint16_t datagrams = ethercat_frame_parse(
                    stack->rx_frame, 
                    len,
                    ethercat_memory_get_raw(&stack->memory),
                    ETHERCAT_MEMORY_SIZE
                );
                
                if (datagrams > 0) {
                    // Если TX ещё занят предыдущим фреймом — этот кадр считаем потерянным
                    if (stack->tx_frame_len != 0) {
                        stack->frames_dropped++;
                    } else {
                    // PDO ↔ GPIO mapping (Phase 2)
                    if (stack->gpio_manager != NULL) {
                        gpio_manager_t* gpio = (gpio_manager_t*)stack->gpio_manager;

                        const uint8* pdo_out = ethercat_memory_get_pdo_output(&stack->memory);
                        uint8* pdo_in = ethercat_memory_get_pdo_input(&stack->memory);

                        gpio_manager_update_outputs(gpio, pdo_out, PDO_OUTPUT_SIZE);
                        gpio_manager_update_inputs(gpio, pdo_in, PDO_INPUT_SIZE);
                    }

                    // PDO forwarding via ESP-NOW (Phase 5, Week 9)
                    if (stack->espnow_bridge != NULL && stack->espnow_pdo_tx_interval_ms > 0) {
                        uint32_t now_ms = millis();
                        if ((uint32_t)(now_ms - stack->last_espnow_pdo_tx_ms) >= (uint32_t)stack->espnow_pdo_tx_interval_ms) {
                            espnow_bridge_t* bridge = (espnow_bridge_t*)stack->espnow_bridge;
                            const uint8* pdo_out = ethercat_memory_get_pdo_output(&stack->memory);
                            // Broadcast to all peers (mac=NULL). espnow_bridge_send_pdo_output will fragment if needed.
                            (void)espnow_bridge_send_pdo_output(bridge, pdo_out, PDO_OUTPUT_SIZE, NULL);
                            stack->last_espnow_pdo_tx_ms = now_ms;
                        }
                    }

                    // PDO ↔ CAN mapping (Phase 5, Week 9) - minimal implementation
                    if (stack->can_bridge != NULL) {
                        mcp2515_t* can = (mcp2515_t*)stack->can_bridge;
                        uint32_t now_ms = millis();

                        // CAN → PDO Input (poll up to a few frames to limit latency)
                        uint8* pdo_in = ethercat_memory_get_pdo_input(&stack->memory);
                        can_frame_t rx;
                        for (int i = 0; i < 4; i++) {
                            if (!mcp2515_receive(can, &rx)) {
                                break;
                            }
                            // Minimal mapping: first 8 bytes of PDO Input = last received CAN data
                            uint8 dlc = (rx.dlc > 8) ? 8 : rx.dlc;
                            memcpy(pdo_in, rx.data, dlc);
                            if (dlc < 8) {
                                memset(pdo_in + dlc, 0, 8 - dlc);
                            }
                        }

                        // PDO Output → CAN TX (rate-limited)
                        if (stack->can_pdo_tx_interval_ms > 0 &&
                            (uint32_t)(now_ms - stack->last_can_pdo_tx_ms) >= (uint32_t)stack->can_pdo_tx_interval_ms) {
                            const uint8* pdo_out = ethercat_memory_get_pdo_output(&stack->memory);
                            can_frame_t tx;
                            memset(&tx, 0, sizeof(tx));
                            tx.id = 0x200;  // default CAN ID (can be made configurable later)
                            tx.dlc = 8;
                            memcpy(tx.data, pdo_out, 8);
                            (void)mcp2515_send(can, &tx);
                            stack->last_can_pdo_tx_ms = now_ms;
                        }
                    }

                    // StepGen (STEP/DIR) for LinuxCNC (minimal)
                    if (stack->stepgen != NULL) {
                        stepgen_t* sg = (stepgen_t*)stack->stepgen;
                        const uint8* pdo_out = ethercat_memory_get_pdo_output(&stack->memory);
                        uint8* pdo_in = ethercat_memory_get_pdo_input(&stack->memory);
                        stepgen_process_pdo(sg, pdo_out, PDO_OUTPUT_SIZE, pdo_in, PDO_INPUT_SIZE);
                    }

                    // Копируем обработанный фрейм в TX буфер и сигнализируем TX задачу
                    memcpy(stack->tx_frame, stack->rx_frame, len);
                    stack->tx_frame_len = len;

                    // Сигнализируем TX task, что фрейм обработан
                    xSemaphoreGive(stack->frame_semaphore);
                    }
                } else {
                    stack->frames_dropped++;
                }
            } else {
                // Не EtherCAT фрейм, игнорируем
                stack->frames_dropped++;
            }
        }
        
        // Минимальная задержка для сервопериода 1-4 мс
        // Небольшая задержка для сброса watchdog (1 тик = ~10ms на 100Hz)
        // Это критично для предотвращения watchdog timeout
        vTaskDelay(pdMS_TO_TICKS(1));  // 1 мс - минимальная задержка для watchdog
    }
    
    Serial.println("[STACK] RX Task stopped");
    vTaskDelete(NULL);
}

static void ethercat_tx_task(void* param) {
    ethercat_stack_t* stack = (ethercat_stack_t*)param;
    
    if (stack == NULL) {
        vTaskDelete(NULL);
        return;
    }
    
    Serial.println("[STACK] TX Task started");
    
    while (stack->running) {
        // Ждем сигнала от RX task
        if (xSemaphoreTake(stack->frame_semaphore, pdMS_TO_TICKS(100)) == pdTRUE) {
            // Отправляем обработанный фрейм обратно через W5500 OUT
            if (stack->tx_frame_len > 0) {
                boolean sent = w5500_macraw_send(&stack->w5500_out,
                                                stack->tx_frame,
                                                stack->tx_frame_len);
                
                if (sent) {
                    stack->frames_sent++;
                } else {
                    stack->frames_dropped++;
                }
                
                stack->tx_frame_len = 0;
            }
        }
    }
    
    Serial.println("[STACK] TX Task stopped");
    vTaskDelete(NULL);
}

// ============================================================================
// Инициализация
// ============================================================================

boolean ethercat_stack_init(ethercat_stack_t* stack, 
                            uint8_t cs_in, uint8_t rst_in,
                            uint8_t cs_out, uint8_t rst_out,
                            const uint8_t* mac) {
    if (stack == NULL || mac == NULL) {
        return false;
    }
    
    memset(stack, 0, sizeof(ethercat_stack_t));
    
    Serial.println("[STACK] Initializing EtherCAT Stack...");
    
    // Инициализация W5500 IN (SPI speed = 20 MHz для EtherCAT)
    if (!w5500_macraw_init(&stack->w5500_in, cs_in, rst_in, mac, 20000000)) {
        Serial.println("[STACK] ERROR: W5500 IN init failed");
        return false;
    }
    Serial.println("[STACK] ✓ W5500 IN initialized");
    
    // Открываем socket для W5500 IN
    if (!w5500_macraw_open_socket(&stack->w5500_in)) {
        Serial.println("[STACK] ERROR: W5500 IN socket open failed");
        return false;
    }
    
    // Инициализация W5500 OUT (SPI speed = 20 MHz для EtherCAT)
    if (!w5500_macraw_init(&stack->w5500_out, cs_out, rst_out, mac, 20000000)) {
        Serial.println("[STACK] ERROR: W5500 OUT init failed");
        return false;
    }
    Serial.println("[STACK] ✓ W5500 OUT initialized");
    
    // Открываем socket для W5500 OUT
    if (!w5500_macraw_open_socket(&stack->w5500_out)) {
        Serial.println("[STACK] ERROR: W5500 OUT socket open failed");
        return false;
    }
    
    // Инициализация Memory Map
    ethercat_memory_init(&stack->memory);
    Serial.println("[STACK] ✓ Memory Map initialized");
    
    // Инициализация FSM
    ethercat_fsm_init(&stack->fsm, &stack->memory);
    Serial.println("[STACK] ✓ FSM initialized");
    
    // Установка callbacks для Memory
    stack->memory.on_al_control_write = on_al_control_write_callback;
    stack->memory.on_station_address_write = on_station_address_write_callback;
    
    // Создание семафора
    stack->frame_semaphore = xSemaphoreCreateBinary();
    if (stack->frame_semaphore == NULL) {
        Serial.println("[STACK] ERROR: Failed to create semaphore");
        return false;
    }
    
    g_stack_instance = stack;
    stack->initialized = true;
    
    Serial.println("[STACK] ✓ Stack initialized successfully");
    
    return true;
}

boolean ethercat_stack_start(ethercat_stack_t* stack) {
    if (stack == NULL || !stack->initialized) {
        return false;
    }
    
    if (stack->running) {
        Serial.println("[STACK] Already running");
        return true;
    }
    
    Serial.println("[STACK] Starting EtherCAT Stack...");
    
    stack->running = true;
    
    // Создание RX Task
    xTaskCreatePinnedToCore(
        ethercat_rx_task,
        "EtherCAT_RX",
        ETHERCAT_STACK_RX_TASK_STACK_SIZE,
        stack,
        ETHERCAT_STACK_RX_TASK_PRIORITY,
        &stack->rx_task_handle,
        ETHERCAT_STACK_RX_TASK_CORE
    );
    
    if (stack->rx_task_handle == NULL) {
        Serial.println("[STACK] ERROR: Failed to create RX task");
        stack->running = false;
        return false;
    }
    Serial.println("[STACK] ✓ RX Task created");
    
    // Создание TX Task
    xTaskCreatePinnedToCore(
        ethercat_tx_task,
        "EtherCAT_TX",
        ETHERCAT_STACK_TX_TASK_STACK_SIZE,
        stack,
        ETHERCAT_STACK_TX_TASK_PRIORITY,
        &stack->tx_task_handle,
        ETHERCAT_STACK_TX_TASK_CORE
    );
    
    if (stack->tx_task_handle == NULL) {
        Serial.println("[STACK] ERROR: Failed to create TX task");
        stack->running = false;
        return false;
    }
    Serial.println("[STACK] ✓ TX Task created");
    
    Serial.println("[STACK] ✓ Stack started successfully");
    
    return true;
}

void ethercat_stack_stop(ethercat_stack_t* stack) {
    if (stack == NULL || !stack->running) {
        return;
    }
    
    Serial.println("[STACK] Stopping EtherCAT Stack...");
    
    stack->running = false;
    
    // Ждем завершения tasks
    if (stack->rx_task_handle != NULL) {
        vTaskDelete(stack->rx_task_handle);
        stack->rx_task_handle = NULL;
    }
    
    if (stack->tx_task_handle != NULL) {
        vTaskDelete(stack->tx_task_handle);
        stack->tx_task_handle = NULL;
    }
    
    // Удаление семафора
    if (stack->frame_semaphore != NULL) {
        vSemaphoreDelete(stack->frame_semaphore);
        stack->frame_semaphore = NULL;
    }
    
    Serial.println("[STACK] ✓ Stack stopped");
}

// ============================================================================
// Statistics
// ============================================================================

void ethercat_stack_get_stats(ethercat_stack_t* stack, 
                              uint32_t* rx, 
                              uint32_t* tx, 
                              uint32_t* dropped) {
    if (stack == NULL) {
        return;
    }
    
    if (rx != NULL) {
        *rx = stack->frames_received;
    }
    if (tx != NULL) {
        *tx = stack->frames_sent;
    }
    if (dropped != NULL) {
        *dropped = stack->frames_dropped;
    }
}

ec_state ethercat_stack_get_state(ethercat_stack_t* stack) {
    if (stack == NULL) {
        return EC_STATE_NONE;
    }
    
    return ethercat_fsm_get_state(&stack->fsm);
}

w5500_macraw_t* ethercat_stack_get_w5500_in(ethercat_stack_t* stack) {
    if (stack == NULL) {
        return NULL;
    }
    return &stack->w5500_in;
}

w5500_macraw_t* ethercat_stack_get_w5500_out(ethercat_stack_t* stack) {
    if (stack == NULL) {
        return NULL;
    }
    return &stack->w5500_out;
}

void ethercat_stack_set_gpio_manager(ethercat_stack_t* stack, void* gpio_manager) {
    if (stack == NULL) {
        return;
    }
    stack->gpio_manager = gpio_manager;
}

void ethercat_stack_set_espnow_bridge(ethercat_stack_t* stack, void* espnow_bridge) {
    if (stack == NULL) {
        return;
    }
    stack->espnow_bridge = espnow_bridge;
}

void ethercat_stack_set_espnow_pdo_tx_interval_ms(ethercat_stack_t* stack, uint16_t interval_ms) {
    if (stack == NULL) {
        return;
    }
    stack->espnow_pdo_tx_interval_ms = interval_ms;
}

void ethercat_stack_set_can_bridge(ethercat_stack_t* stack, void* can_bridge) {
    if (stack == NULL) {
        return;
    }
    stack->can_bridge = can_bridge;
}

void ethercat_stack_set_can_pdo_tx_interval_ms(ethercat_stack_t* stack, uint16_t interval_ms) {
    if (stack == NULL) {
        return;
    }
    stack->can_pdo_tx_interval_ms = interval_ms;
}

void ethercat_stack_set_stepgen(ethercat_stack_t* stack, void* stepgen) {
    if (stack == NULL) {
        return;
    }
    stack->stepgen = stepgen;
}
