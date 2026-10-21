/*
 * EtherCAT Memory Map - Implementation
 * 
 * Phase 1, Week 2, Day 8-11
 * ESP32 EtherCAT Slave v4
 */

#include "ethercat_memory.h"
#include <esp_timer.h>
#include <stddef.h>
#include <string.h>

// ============================================================================
// Инициализация
// ============================================================================

void ethercat_memory_init(ethercat_memory_t* mem) {
    if (mem == NULL) {
        return;
    }
    
    // Очистка всей памяти
    memset(mem->memory, 0, ETHERCAT_MEMORY_SIZE);
    
    // Инициализация callbacks
    mem->on_al_control_write = NULL;
    mem->on_station_address_write = NULL;
    mem->on_dc_sync0_config = NULL;
    
    // ========================================================================
    // Инициализация ESC Registers defaults
    // ========================================================================
    
    // 0x0000: Type = 0x01 (EtherCAT Slave)
    ethercat_memory_write8(mem, ECT_REG_TYPE, 0x01);
    
    // 0x0010: Station Address = 0 (будет установлен Master'ом)
    ethercat_memory_write16(mem, ECT_REG_STADR, 0x0000);
    
    // 0x0120: AL Control = 0x0000 (INIT requested)
    ethercat_memory_write16(mem, ECT_REG_ALCTL, 0x0000);
    
    // 0x0130: AL Status = 0x0001 (INIT state)
    ethercat_memory_write16(mem, ECT_REG_ALSTAT, EC_STATE_INIT);
    
    // 0x0134: AL Status Code = 0x0000 (No error)
    ethercat_memory_write16(mem, ECT_REG_ALSTATCODE, EC_ALSTATUSCODE_NOERROR);
    
    // ========================================================================
    // DC Registers Initialization
    // ========================================================================
    
    // 0x0910: DC System Time = 0 (будет обновляться через update_dc_system_time)
    ethercat_memory_write64(mem, ECT_REG_DCSYSTIME, 0);
    
    // 0x0990: DC SYNC0 Start Time = 0 (64-bit, будет установлен Master'ом)
    ethercat_memory_write64(mem, ECT_REG_DCSTART0, 0);
    
    // 0x09A0: DC SYNC0 Cycle Time = 0 (32-bit, будет установлен Master'ом)
    ethercat_memory_write32(mem, ECT_REG_DCCYCLE0, 0);
    
    // Установка флага инициализации
    mem->initialized = true;
}

// ============================================================================
// Read Functions
// ============================================================================

uint8 ethercat_memory_read8(ethercat_memory_t* mem, uint16 addr) {
    if (mem == NULL || addr >= ETHERCAT_MEMORY_SIZE) {
        return 0;
    }
    
    return mem->memory[addr];
}

uint16 ethercat_memory_read16(ethercat_memory_t* mem, uint16 addr) {
    if (mem == NULL || addr + 1 >= ETHERCAT_MEMORY_SIZE) {
        return 0;
    }
    
    // Little-endian
    uint16 value = mem->memory[addr] | (mem->memory[addr + 1] << 8);
    return value;
}

uint32 ethercat_memory_read32(ethercat_memory_t* mem, uint16 addr) {
    if (mem == NULL || addr + 3 >= ETHERCAT_MEMORY_SIZE) {
        return 0;
    }
    
    // Little-endian
    uint32 value = mem->memory[addr] 
                 | (mem->memory[addr + 1] << 8)
                 | (mem->memory[addr + 2] << 16)
                 | (mem->memory[addr + 3] << 24);
    return value;
}

uint64 ethercat_memory_read64(ethercat_memory_t* mem, uint16 addr) {
    if (mem == NULL || addr + 7 >= ETHERCAT_MEMORY_SIZE) {
        return 0;
    }
    
    // Little-endian
    uint64 value = (uint64)mem->memory[addr]
                 | ((uint64)mem->memory[addr + 1] << 8)
                 | ((uint64)mem->memory[addr + 2] << 16)
                 | ((uint64)mem->memory[addr + 3] << 24)
                 | ((uint64)mem->memory[addr + 4] << 32)
                 | ((uint64)mem->memory[addr + 5] << 40)
                 | ((uint64)mem->memory[addr + 6] << 48)
                 | ((uint64)mem->memory[addr + 7] << 56);
    return value;
}

// ============================================================================
// Write Functions (с callbacks)
// ============================================================================

void ethercat_memory_write8(ethercat_memory_t* mem, uint16 addr, uint8 value) {
    if (mem == NULL || addr >= ETHERCAT_MEMORY_SIZE) {
        return;
    }
    
    mem->memory[addr] = value;
}

void ethercat_memory_write16(ethercat_memory_t* mem, uint16 addr, uint16 value) {
    if (mem == NULL || addr + 1 >= ETHERCAT_MEMORY_SIZE) {
        return;
    }
    
    // Little-endian
    mem->memory[addr] = value & 0xFF;
    mem->memory[addr + 1] = (value >> 8) & 0xFF;
    
    // Callbacks для специальных регистров
    if (addr == ECT_REG_ALCTL && mem->on_al_control_write != NULL) {
        mem->on_al_control_write(value);
    }
    
    if (addr == ECT_REG_STADR && mem->on_station_address_write != NULL) {
        mem->on_station_address_write(value);
    }
}

void ethercat_memory_write32(ethercat_memory_t* mem, uint16 addr, uint32 value) {
    if (mem == NULL || addr + 3 >= ETHERCAT_MEMORY_SIZE) {
        return;
    }
    
    // Little-endian
    mem->memory[addr] = value & 0xFF;
    mem->memory[addr + 1] = (value >> 8) & 0xFF;
    mem->memory[addr + 2] = (value >> 16) & 0xFF;
    mem->memory[addr + 3] = (value >> 24) & 0xFF;
    
    // DC SYNC0 Cycle Time callback
    if (addr == ECT_REG_DCCYCLE0 && mem->on_dc_sync0_config != NULL) {
        uint64 start_time = ethercat_memory_read64(mem, ECT_REG_DCSTART0);
        mem->on_dc_sync0_config(start_time, value);
    }
}

void ethercat_memory_write64(ethercat_memory_t* mem, uint16 addr, uint64 value) {
    if (mem == NULL || addr + 7 >= ETHERCAT_MEMORY_SIZE) {
        return;
    }
    
    // Little-endian
    mem->memory[addr] = value & 0xFF;
    mem->memory[addr + 1] = (value >> 8) & 0xFF;
    mem->memory[addr + 2] = (value >> 16) & 0xFF;
    mem->memory[addr + 3] = (value >> 24) & 0xFF;
    mem->memory[addr + 4] = (value >> 32) & 0xFF;
    mem->memory[addr + 5] = (value >> 40) & 0xFF;
    mem->memory[addr + 6] = (value >> 48) & 0xFF;
    mem->memory[addr + 7] = (value >> 56) & 0xFF;
    
    // DC SYNC0 Start Time callback
    if (addr == ECT_REG_DCSTART0 && mem->on_dc_sync0_config != NULL) {
        uint32 cycle_time = ethercat_memory_read32(mem, ECT_REG_DCCYCLE0);
        mem->on_dc_sync0_config(value, cycle_time);
    }
}

// ============================================================================
// DC System Time Update
// ============================================================================

// Статическая переменная для хранения начального времени
static int64_t g_dc_time_offset_us = 0;
static boolean g_dc_time_initialized = false;

void ethercat_memory_update_dc_system_time(ethercat_memory_t* mem) {
    if (mem == NULL) {
        return;
    }
    
    // ESP32 esp_timer_get_time() возвращает время в микросекундах с момента загрузки
    int64_t time_us = esp_timer_get_time();
    
    // Инициализация offset при первом вызове
    if (!g_dc_time_initialized) {
        g_dc_time_offset_us = time_us;
        g_dc_time_initialized = true;
    }
    
    // Вычисляем относительное время (с момента инициализации)
    int64_t relative_time_us = time_us - g_dc_time_offset_us;
    
    // Конвертируем в наносекунды (умножаем на 1000)
    uint64 time_ns = (uint64)relative_time_us * 1000ULL;
    
    // Записываем в DC System Time register (0x0910)
    ethercat_memory_write64(mem, ECT_REG_DCSYSTIME, time_ns);
}

// ============================================================================
// Helper Functions
// ============================================================================

uint8* ethercat_memory_get_pdo_output(ethercat_memory_t* mem) {
    if (mem == NULL) {
        return NULL;
    }
    return &mem->memory[PDO_OUTPUT_ADDR];
}

uint8* ethercat_memory_get_pdo_input(ethercat_memory_t* mem) {
    if (mem == NULL) {
        return NULL;
    }
    return &mem->memory[PDO_INPUT_ADDR];
}

uint8* ethercat_memory_get_raw(ethercat_memory_t* mem) {
    if (mem == NULL) {
        return NULL;
    }
    return mem->memory;
}
