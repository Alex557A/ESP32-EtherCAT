/*
 * EtherCAT Memory Map
 * Эмуляция памяти ESC (EtherCAT Slave Controller)
 * 
 * Phase 1, Week 2, Day 8-11
 * ESP32 EtherCAT Slave v4
 */

#ifndef ETHERCAT_MEMORY_H
#define ETHERCAT_MEMORY_H

#include "ethercat_type.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Memory Map Layout
// ============================================================================

/**
 * 0x0000-0x0FFF: ESC Registers (4KB)
 *   ├── 0x0000: Type
 *   ├── 0x0010: Station Address
 *   ├── 0x0120: AL Control
 *   ├── 0x0130: AL Status
 *   ├── 0x0134: AL Status Code
 *   └── 0x0910: DC System Time
 * 
 * 0x1000-0x1FFF: I/O Memory (4KB)
 *   ├── 0x1000-0x10FF: PDO Output (Master → Slave, 256 bytes)
 *   ├── 0x1100-0x11FF: PDO Input (Slave → Master, 256 bytes)
 *   ├── 0x1200-0x12FF: Mailbox Out (Master → Slave, 256 bytes)
 *   └── 0x1300-0x13FF: Mailbox In (Slave → Master, 256 bytes)
 */

#define ETHERCAT_MEMORY_SIZE   0x2000  /**< 8KB total */
#define PDO_OUTPUT_ADDR        0x1000  /**< PDO Output начало */
#define PDO_OUTPUT_SIZE        0x0100  /**< 256 bytes */
#define PDO_INPUT_ADDR         0x1100  /**< PDO Input начало */
#define PDO_INPUT_SIZE         0x0100  /**< 256 bytes */
#define MAILBOX_OUT_ADDR       0x1200  /**< Mailbox Out (Master → Slave) */
#define MAILBOX_OUT_SIZE       0x0100  /**< 256 bytes */
#define MAILBOX_IN_ADDR        0x1300  /**< Mailbox In (Slave → Master) */
#define MAILBOX_IN_SIZE        0x0100  /**< 256 bytes */

// ============================================================================
// Memory Structure
// ============================================================================

typedef struct {
    uint8 memory[ETHERCAT_MEMORY_SIZE];  /**< Raw memory */
    
    boolean initialized;  /**< Флаг инициализации */
    
    // Callbacks для специальных регистров
    void (*on_al_control_write)(uint16 value);
    void (*on_station_address_write)(uint16 value);
    void (*on_dc_sync0_config)(uint64 start_time, uint32 cycle_time);
    
} ethercat_memory_t;

// ============================================================================
// API Functions
// ============================================================================

/**
 * @brief Инициализация memory map
 * @param mem Указатель на memory структуру
 */
void ethercat_memory_init(ethercat_memory_t* mem);

/**
 * @brief Чтение 8-bit значения
 */
uint8 ethercat_memory_read8(ethercat_memory_t* mem, uint16 addr);

/**
 * @brief Чтение 16-bit значения
 */
uint16 ethercat_memory_read16(ethercat_memory_t* mem, uint16 addr);

/**
 * @brief Чтение 32-bit значения
 */
uint32 ethercat_memory_read32(ethercat_memory_t* mem, uint16 addr);

/**
 * @brief Чтение 64-bit значения
 */
uint64 ethercat_memory_read64(ethercat_memory_t* mem, uint16 addr);

/**
 * @brief Запись 8-bit значения
 */
void ethercat_memory_write8(ethercat_memory_t* mem, uint16 addr, uint8 value);

/**
 * @brief Запись 16-bit значения
 */
void ethercat_memory_write16(ethercat_memory_t* mem, uint16 addr, uint16 value);

/**
 * @brief Запись 32-bit значения
 */
void ethercat_memory_write32(ethercat_memory_t* mem, uint16 addr, uint32 value);

/**
 * @brief Запись 64-bit значения
 */
void ethercat_memory_write64(ethercat_memory_t* mem, uint16 addr, uint64 value);

/**
 * @brief Получить указатель на PDO Output область
 */
uint8* ethercat_memory_get_pdo_output(ethercat_memory_t* mem);

/**
 * @brief Получить указатель на PDO Input область
 */
uint8* ethercat_memory_get_pdo_input(ethercat_memory_t* mem);

/**
 * @brief Получить указатель на raw memory (для frame parser)
 */
uint8* ethercat_memory_get_raw(ethercat_memory_t* mem);

#ifdef __cplusplus
}
#endif

#endif // ETHERCAT_MEMORY_H
