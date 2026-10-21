/*
 * EtherCAT Stack Core
 * Интеграция всех модулей с FreeRTOS tasks
 * 
 * Phase 1, Week 3, Day 17-18
 * ESP32 EtherCAT Slave v4
 */

#ifndef ETHERCAT_STACK_H
#define ETHERCAT_STACK_H

#include "ethercat_type.h"
#include "ethercat_frame.h"
#include "ethercat_memory.h"
#include "ethercat_fsm.h"
#include "w5500_macraw.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/semphr.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Configuration
// ============================================================================

#define ETHERCAT_STACK_RX_TASK_PRIORITY     5  /**< Высокий приоритет для RX */
#define ETHERCAT_STACK_TX_TASK_PRIORITY     5  /**< Высокий приоритет для TX */
#define ETHERCAT_STACK_RX_TASK_STACK_SIZE   4096
#define ETHERCAT_STACK_TX_TASK_STACK_SIZE   4096
#define ETHERCAT_STACK_RX_TASK_CORE         0  /**< Pinned to core 0 */
#define ETHERCAT_STACK_TX_TASK_CORE         0  /**< Pinned to core 0 */

// ============================================================================
// EtherCAT Stack Structure
// ============================================================================

typedef struct {
    // Modules
    w5500_macraw_t w5500_in;           /**< W5500 IN (RX) */
    w5500_macraw_t w5500_out;          /**< W5500 OUT (TX) */
    ethercat_memory_t memory;          /**< Memory Map */
    ethercat_fsm_t fsm;                /**< State Machine */

    // Optional integrations
    void* gpio_manager;                /**< gpio_manager_t* (PDO ↔ GPIO) */
    void* espnow_bridge;               /**< espnow_bridge_t* (PDO forwarding) */
    uint16_t espnow_pdo_tx_interval_ms; /**< Минимальный интервал TX PDO (ms), 0=off */
    uint32_t last_espnow_pdo_tx_ms;    /**< Timestamp последней отправки PDO (ms) */

    void* can_bridge;                  /**< mcp2515_t* (CAN bridge) */
    uint16_t can_pdo_tx_interval_ms;   /**< Интервал CAN TX из PDO (ms), 0=off */
    uint32_t last_can_pdo_tx_ms;       /**< Timestamp последней CAN отправки (ms) */

    void* stepgen;                     /**< stepgen_t* (STEP/DIR generator for LinuxCNC) */
    
    // FreeRTOS
    TaskHandle_t rx_task_handle;       /**< RX Task handle */
    TaskHandle_t tx_task_handle;       /**< TX Task handle */
    SemaphoreHandle_t frame_semaphore; /**< Семафор для синхронизации RX→TX */
    
    // Frame buffers
    uint8_t rx_frame[EC_MAXECATFRAME]; /**< RX buffer */
    uint16_t rx_frame_len;             /**< Длина RX фрейма (debug) */
    uint8_t tx_frame[EC_MAXECATFRAME]; /**< TX buffer */
    uint16_t tx_frame_len;             /**< Длина TX фрейма */
    
    // Statistics
    uint32_t frames_received;          /**< Счетчик принятых фреймов */
    uint32_t frames_sent;              /**< Счетчик отправленных фреймов */
    uint32_t frames_dropped;           /**< Счетчик потерянных фреймов */
    
    // Status
    boolean initialized;               /**< Флаг инициализации */
    boolean running;                   /**< Флаг работы */
    
} ethercat_stack_t;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Инициализация EtherCAT Stack
 * @param stack Указатель на stack структуру
 * @param cs_in CS pin для W5500 IN
 * @param rst_in RST pin для W5500 IN
 * @param cs_out CS pin для W5500 OUT
 * @param rst_out RST pin для W5500 OUT
 * @param mac MAC address (6 bytes)
 * @return true если успешно
 */
boolean ethercat_stack_init(ethercat_stack_t* stack, 
                            uint8_t cs_in, uint8_t rst_in,
                            uint8_t cs_out, uint8_t rst_out,
                            const uint8_t* mac);

/**
 * @brief Запуск EtherCAT Stack (создание tasks)
 * @param stack Указатель на stack структуру
 * @return true если успешно
 */
boolean ethercat_stack_start(ethercat_stack_t* stack);

/**
 * @brief Остановка EtherCAT Stack
 * @param stack Указатель на stack структуру
 */
void ethercat_stack_stop(ethercat_stack_t* stack);

/**
 * @brief Получить статистику
 * @param stack Указатель на stack структуру
 * @param rx Указатель для RX счетчика (может быть NULL)
 * @param tx Указатель для TX счетчика (может быть NULL)
 * @param dropped Указатель для Dropped счетчика (может быть NULL)
 */
void ethercat_stack_get_stats(ethercat_stack_t* stack, 
                              uint32_t* rx, 
                              uint32_t* tx, 
                              uint32_t* dropped);

/**
 * @brief Получить текущее состояние FSM
 * @param stack Указатель на stack структуру
 * @return Текущее состояние
 */
ec_state ethercat_stack_get_state(ethercat_stack_t* stack);

/**
 * @brief Получить указатель на W5500 IN (для тестирования)
 * @param stack Указатель на stack структуру
 * @return Указатель на w5500_in
 */
w5500_macraw_t* ethercat_stack_get_w5500_in(ethercat_stack_t* stack);

/**
 * @brief Получить указатель на W5500 OUT (для тестирования)
 * @param stack Указатель на stack структуру
 * @return Указатель на w5500_out
 */
w5500_macraw_t* ethercat_stack_get_w5500_out(ethercat_stack_t* stack);

/**
 * @brief Подключить GPIO Manager для PDO ↔ GPIO
 * @param stack Указатель на stack структуру
 * @param gpio_manager Указатель на gpio_manager_t (может быть NULL)
 */
void ethercat_stack_set_gpio_manager(ethercat_stack_t* stack, void* gpio_manager);

/**
 * @brief Подключить ESP-NOW Bridge для PDO forwarding
 * @param stack Указатель на stack структуру
 * @param espnow_bridge Указатель на espnow_bridge_t (может быть NULL)
 */
void ethercat_stack_set_espnow_bridge(ethercat_stack_t* stack, void* espnow_bridge);

/**
 * @brief Настроить частоту отправки PDO по ESP-NOW
 * @param stack Указатель на stack структуру
 * @param interval_ms Интервал в миллисекундах (0 = выключить)
 */
void ethercat_stack_set_espnow_pdo_tx_interval_ms(ethercat_stack_t* stack, uint16_t interval_ms);

/**
 * @brief Подключить CAN bridge (MCP2515) для PDO ↔ CAN
 * @param stack Указатель на stack структуру
 * @param can_bridge Указатель на mcp2515_t (может быть NULL)
 */
void ethercat_stack_set_can_bridge(ethercat_stack_t* stack, void* can_bridge);

/**
 * @brief Настроить частоту отправки CAN кадров из PDO
 * @param stack Указатель на stack структуру
 * @param interval_ms Интервал в миллисекундах (0 = выключить)
 */
void ethercat_stack_set_can_pdo_tx_interval_ms(ethercat_stack_t* stack, uint16_t interval_ms);

/**
 * @brief Подключить StepGen модуль (STEP/DIR)
 * @param stack Указатель на stack структуру
 * @param stepgen Указатель на stepgen_t (может быть NULL)
 */
void ethercat_stack_set_stepgen(ethercat_stack_t* stack, void* stepgen);

#ifdef __cplusplus
}
#endif

#endif // ETHERCAT_STACK_H
