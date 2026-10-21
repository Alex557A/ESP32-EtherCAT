/*
 * EtherCAT FSM (Finite State Machine)
 * Управление переходами состояний Slave
 * 
 * Phase 1, Week 2, Day 12-14
 * ESP32 EtherCAT Slave v4
 */

#ifndef ETHERCAT_FSM_H
#define ETHERCAT_FSM_H

#include "ethercat_type.h"
#include "ethercat_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// FSM Structure
// ============================================================================

typedef struct {
    ec_state current_state;          /**< Текущее состояние */
    boolean initialized;            /**< Флаг инициализации */
    ethercat_memory_t* memory;       /**< Указатель на memory map */
    
} ethercat_fsm_t;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Инициализация FSM
 * @param fsm Указатель на FSM структуру
 * @param mem Указатель на memory map
 */
void ethercat_fsm_init(ethercat_fsm_t* fsm, ethercat_memory_t* mem);

/**
 * @brief Переход в новое состояние
 * @param fsm Указатель на FSM структуру
 * @param new_state Новое состояние
 * @return true если переход успешен
 */
boolean ethercat_fsm_transition_to(ethercat_fsm_t* fsm, ec_state new_state);

/**
 * @brief Получить текущее состояние
 * @param fsm Указатель на FSM структуру
 * @return Текущее состояние
 */
ec_state ethercat_fsm_get_state(ethercat_fsm_t* fsm);

/**
 * @brief Проверка валидности перехода
 * @param from Исходное состояние
 * @param to Целевое состояние
 * @return true если переход валиден
 */
boolean ethercat_fsm_is_valid_transition(ec_state from, ec_state to);

#ifdef __cplusplus
}
#endif

#endif // ETHERCAT_FSM_H
