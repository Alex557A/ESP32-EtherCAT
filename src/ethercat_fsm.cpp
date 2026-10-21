/*
 * EtherCAT FSM - Implementation
 * 
 * Phase 1, Week 2, Day 12-14
 * ESP32 EtherCAT Slave v4
 */

#include "ethercat_fsm.h"
#include <stddef.h>

// ============================================================================
// Инициализация
// ============================================================================

void ethercat_fsm_init(ethercat_fsm_t* fsm, ethercat_memory_t* mem) {
    if (fsm == NULL || mem == NULL) {
        return;
    }
    
    fsm->current_state = EC_STATE_INIT;
    fsm->memory = mem;
    fsm->initialized = true;
    
    // Установить состояние в memory
    ethercat_memory_write16(mem, ECT_REG_ALSTAT, EC_STATE_INIT);
    ethercat_memory_write16(mem, ECT_REG_ALSTATCODE, EC_ALSTATUSCODE_NOERROR);
}

// ============================================================================
// Проверка валидности перехода
// ============================================================================

boolean ethercat_fsm_is_valid_transition(ec_state from, ec_state to) {
    // Переходы согласно ETG.1000
    
    // В INIT можно из любого состояния
    if (to == EC_STATE_INIT) {
        return true;
    }
    
    switch (from) {
        case EC_STATE_INIT:
            // INIT → PRE_OP
            return (to == EC_STATE_PRE_OP);
            
        case EC_STATE_PRE_OP:
            // PRE_OP → INIT или PRE_OP → SAFE_OP
            return (to == EC_STATE_INIT || to == EC_STATE_SAFE_OP);
            
        case EC_STATE_SAFE_OP:
            // SAFE_OP → INIT, PRE_OP или OPERATIONAL
            return (to == EC_STATE_INIT || 
                    to == EC_STATE_PRE_OP || 
                    to == EC_STATE_OPERATIONAL);
            
        case EC_STATE_OPERATIONAL:
            // OPERATIONAL → INIT, PRE_OP или SAFE_OP
            return (to == EC_STATE_INIT || 
                    to == EC_STATE_PRE_OP || 
                    to == EC_STATE_SAFE_OP);
            
        default:
            return false;
    }
}

// ============================================================================
// Переход состояний
// ============================================================================

boolean ethercat_fsm_transition_to(ethercat_fsm_t* fsm, ec_state new_state) {
    if (fsm == NULL || fsm->memory == NULL) {
        return false;
    }
    
    // Проверка валидности перехода
    if (!ethercat_fsm_is_valid_transition(fsm->current_state, new_state)) {
        // Инвалидный переход - установить ошибку
        ethercat_memory_write16(fsm->memory, ECT_REG_ALSTAT, 
                                fsm->current_state | EC_STATE_ERROR);
        ethercat_memory_write16(fsm->memory, ECT_REG_ALSTATCODE, 
                                EC_ALSTATUSCODE_INVALIDSTATE);
        return false;
    }
    
    // Валидный переход
    fsm->current_state = new_state;
    
    // Обновить регистры в memory
    ethercat_memory_write16(fsm->memory, ECT_REG_ALSTAT, new_state);
    ethercat_memory_write16(fsm->memory, ECT_REG_ALSTATCODE, 
                            EC_ALSTATUSCODE_NOERROR);
    
    return true;
}

// ============================================================================
// Getter
// ============================================================================

ec_state ethercat_fsm_get_state(ethercat_fsm_t* fsm) {
    if (fsm == NULL || !fsm->initialized) {
        return EC_STATE_NONE;
    }
    
    return fsm->current_state;
}
