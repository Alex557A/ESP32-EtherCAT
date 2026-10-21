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

    /* Проверка настроек SyncManager перед переходом в SAFE_OP / OP */
    if (new_state == EC_STATE_SAFE_OP || new_state == EC_STATE_OPERATIONAL) {
        uint16 code = ethercat_esc_check_sm_config(&g_stack_instance->memory);

        if (code != EC_ALSTATUSCODE_NOERROR) {
            Serial.printf("[FSM] SM config check FAILED: 0x%04X\n", code);
            ethercat_memory_write16(&g_stack_instance->memory, ECT_REG_ALSTAT,
                                    (uint16)(new_state | EC_STATE_ERROR));
            ethercat_memory_write16(&g_stack_instance->memory, ECT_REG_ALSTATCODE, code);
            return;
        }

        /* Перед OP применяем PDO-маппинг (области и размеры PDO) */
        (void)ethercat_od_process_pending(&g_stack_instance->od,
                                          &g_stack_instance->memory);
    }
    
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
// Хук изменений карты памяти (почтовый ящик, SII/EEPROM)
// ============================================================================

/** Диапазоны адресов пересекаются */
static boolean addr_overlap(uint16 a_start, uint16 a_len, uint16 b_start, uint16 b_len) {
    uint32 a_end = (uint32)a_start + a_len;
    uint32 b_end = (uint32)b_start + b_len;

    return ((a_start < b_end) && (b_start < a_end)) ? true : false;
}

static void on_memory_change_callback(void* ctx, uint16 addr, uint16 length, uint8 command) {
    ethercat_stack_t* stack = (ethercat_stack_t*)ctx;
    uint16 sm0_addr;
    uint16 sm0_len;

    (void)command;

    if (stack == NULL) {
        return;
    }

    /* Запись в регистры интерфейса EEPROM → обслужить запрос SII */
    if (addr_overlap(addr, length, ECT_REG_EEPCFG, 0x000C) == true) {
        (void)ethercat_esc_process_eeprom(&stack->esc, &stack->memory);
    }

    /* Запись в буфер почтового ящика SM0 → взвести признак заполнения */
    sm0_addr = ethercat_memory_read16(&stack->memory, ECT_REG_SM0);
    sm0_len = ethercat_memory_read16(&stack->memory, (uint16)(ECT_REG_SM0 + 2));

    if (sm0_len < ECT_MBX_MIN_LENGTH ||
        ((uint32)sm0_addr + sm0_len) > ETHERCAT_MEMORY_SIZE) {
        sm0_addr = MAILBOX_OUT_ADDR;
        sm0_len = ECT_MBX_SIZE;
    }

    if (addr_overlap(addr, length, sm0_addr, sm0_len) == true) {
        uint8 stat0 = ethercat_memory_read8(&stack->memory, ECT_SM0STAT);
        uint8 stat1 = ethercat_memory_read8(&stack->memory, ECT_SM1STAT);

        /* Пришёл новый запрос: взвести признак заполнения SM0 и снять
         * признак готовности ответа SM1 (предыдущий ответ считается
         * принятым мастером) */
        ethercat_memory_write8(&stack->memory, ECT_SM0STAT,
                               (uint8)(stat0 | ECT_SMSTAT_MAILBOX));
        ethercat_memory_write8(&stack->memory, ECT_SM1STAT,
                               (uint8)(stat1 & ~ECT_SMSTAT_MAILBOX));
    }
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
                    /* --- Обслуживание ESC: SII/EEPROM и почтовый ящик CoE ------- */
                    ethercat_esc_update_counters(&stack->memory, true);
                    (void)ethercat_esc_process_eeprom(&stack->esc, &stack->memory);

                    while (ethercat_coe_process(&stack->od, &stack->memory)) {
                        /* Обрабатываем все поступившие запросы CoE */
                    }

                    if (ethercat_od_process_pending(&stack->od, &stack->memory) == true) {
                        Serial.printf("[CoE] PDO mapping: RxPDO=%u B, TxPDO=%u B\n",
                                      (unsigned)ethercat_od_get_rx_pdo_size(&stack->od),
                                      (unsigned)ethercat_od_get_tx_pdo_size(&stack->od));
                    }

                    /* --- Distributed Clock -------------------------------------- */
                    ethercat_dc_update(&stack->dc, &stack->memory);

                    if (ethercat_dc_check_sync(&stack->dc, &stack->memory) > 0) {
                        /* Импульс SYNC0: защёлкнуть входы и применить выходы */
                        if (stack->gpio_manager != NULL) {
                            gpio_manager_t* gpio_sync = (gpio_manager_t*)stack->gpio_manager;
                            uint8* raw_sync = ethercat_memory_get_raw(&stack->memory);

                            if (raw_sync != NULL) {
                                gpio_manager_update_inputs(gpio_sync,
                                    &raw_sync[ethercat_esc_get_pdo_address(&stack->memory, false)],
                                    PDO_INPUT_SIZE);
                            }
                        }
                    }

                    // Если TX ещё занят предыдущим фреймом — этот кадр считаем потерянным
                    if (stack->tx_frame_len != 0) {
                        stack->frames_dropped++;
                    } else {
                    // PDO ↔ GPIO mapping (Phase 2)
                    if (stack->gpio_manager != NULL) {
                        gpio_manager_t* gpio = (gpio_manager_t*)stack->gpio_manager;
                        uint8* raw = ethercat_memory_get_raw(&stack->memory);
                        uint16 out_addr = ethercat_esc_get_pdo_address(&stack->memory, true);
                        uint16 in_addr = ethercat_esc_get_pdo_address(&stack->memory, false);
                        uint16 out_len = ethercat_esc_get_pdo_length(&stack->memory, true);
                        uint16 in_len = ethercat_esc_get_pdo_length(&stack->memory, false);

                        /* Области PDO берём из настроек SyncManager (мастер),
                         * с откатом на значения по умолчанию */
                        if (out_addr == 0 || out_len == 0) {
                            out_addr = PDO_OUTPUT_ADDR;
                            out_len = PDO_OUTPUT_SIZE;
                        }
                        if (in_addr == 0 || in_len == 0) {
                            in_addr = PDO_INPUT_ADDR;
                            in_len = PDO_INPUT_SIZE;
                        }

                        gpio_manager_update_outputs(gpio, &raw[out_addr], out_len);
                        gpio_manager_update_inputs(gpio, &raw[in_addr], in_len);
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
    
    // Инициализация регистров ESC и образа SII
    ethercat_esc_init(&stack->esc, &stack->memory);
    Serial.printf("[STACK] ✓ ESC registers + SII (%u слов)\n", stack->esc.sii_words);

    // Инициализация словаря объектов (CoE)
    ethercat_od_init(&stack->od,
                     ethercat_memory_get_pdo_output(&stack->memory),
                     ethercat_memory_get_pdo_input(&stack->memory));
    (void)ethercat_od_apply_pdo_mapping(&stack->od, &stack->memory);
    Serial.printf("[STACK] ✓ Object Dictionary: %u объектов\n",
                  (unsigned)ethercat_od_get_object_count(&stack->od));

    // Инициализация Distributed Clock
    ethercat_dc_init(&stack->dc);
    Serial.println("[STACK] ✓ Distributed Clock initialized");

    // Хук изменений памяти: почтовый ящик CoE и интерфейс SII/EEPROM
    ethercat_frame_set_mem_change_hook(on_memory_change_callback, stack);
    Serial.println("[STACK] ✓ Memory change hook installed");
    
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

ethercat_od_t* ethercat_stack_get_od(ethercat_stack_t* stack) {
    if (stack == NULL) {
        return NULL;
    }
    return &stack->od;
}

ethercat_dc_t* ethercat_stack_get_dc(ethercat_stack_t* stack) {
    if (stack == NULL) {
        return NULL;
    }
    return &stack->dc;
}

ethercat_esc_t* ethercat_stack_get_esc(ethercat_stack_t* stack) {
    if (stack == NULL) {
        return NULL;
    }
    return &stack->esc;
}
