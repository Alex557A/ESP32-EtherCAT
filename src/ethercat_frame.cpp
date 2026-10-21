/*
 * EtherCAT Frame Parser - Implementation
 * 
 * Phase 1, Week 1, Day 3-7
 * ESP32 EtherCAT Slave v4
 */

#include "ethercat_frame.h"
#include "ethercat_esc.h"
#include <stddef.h>
#include <string.h>

// ============================================================================
// Проверка EtherType
// ============================================================================

boolean ethercat_frame_is_ethercat(uint8* frame) {
    if (frame == NULL) {
        return false;
    }
    
    ec_etherheadert* eth_hdr = (ec_etherheadert*)frame;
    return (etohs(eth_hdr->etype) == ETH_P_ECAT);
}

// ============================================================================
// Increment Working Counter
// ============================================================================

void ethercat_frame_increment_wkc(ec_comt* datagram, uint16 dlength) {
    // WKC находится сразу после данных
    uint8* data = (uint8*)(datagram + 1);  // Пропустить header
    uint16* wkc_ptr = (uint16*)&data[dlength];
    
    // Читаем текущий WKC
    uint16 wkc = etohs(*wkc_ptr);
    
    // Инкрементируем
    wkc++;
    
    // Записываем обратно
    *wkc_ptr = htoes(wkc);
}

// ============================================================================
// Хук изменений карты памяти
// ============================================================================

static ec_mem_change_cb_t g_mem_change_cb = NULL;
static void* g_mem_change_ctx = NULL;

void ethercat_frame_set_mem_change_hook(ec_mem_change_cb_t cb, void* ctx) {
    g_mem_change_cb = cb;
    g_mem_change_ctx = ctx;
}

/** Уведомить хук о записи в память */
static void notify_mem_change(uint16 addr, uint16 length, uint8 command) {
    if (g_mem_change_cb != NULL) {
        g_mem_change_cb(g_mem_change_ctx, addr, length, command);
    }
}

/** true для команд с логической адресацией (адрес переводится через FMMU) */
static boolean is_logical_command(uint8 command) {
    return (command == EC_CMD_LRD || command == EC_CMD_LWR ||
            command == EC_CMD_LRW) ? true : false;
}

/**
 * Физический адрес для байта с адресом addr.
 * Для логического доступа адрес переводится через таблицу FMMU;
 * если ни один FMMU не подошёл — применяется прямая адресация
 * (обратная совместимость с прежним поведением).
 */
static uint16 phys_addr(uint8* memory, uint16 memory_size, uint16 addr, boolean logical) {
    uint16 phys;

    if (logical == true) {
        if (ethercat_esc_fmmu_translate(memory, addr, &phys) == true) {
            if (phys < memory_size) {
                return phys;
            }
        }
    }

    return addr;
}

// ============================================================================
// Обработка команды
// ============================================================================

void ethercat_frame_process_command(ec_comt* datagram, 
                                     uint8* memory, 
                                     uint16 memory_size) {
    uint16 ADO;
    uint16 dlength;
    uint8* data;
    boolean is_read = false;
    boolean is_write = false;
    boolean logical;
    uint16 i;

    if (datagram == NULL || memory == NULL) {
        return;
    }
    
    ADO = etohs(datagram->ADO);
    dlength = etohs(datagram->dlength) & 0x7FF;  // Маска EC_DATAGRAMFOLLOWS
    data = (uint8*)(datagram + 1);
    
    // Определение направления обмена
    switch (datagram->command) {
        case EC_CMD_APRD:  // Auto Increment Read
        case EC_CMD_FPRD:  // Configured Address Read
        case EC_CMD_BRD:   // Broadcast Read
        case EC_CMD_LRD:   // Logical Read
            is_read = true;
            break;

        case EC_CMD_APWR:  // Auto Increment Write
        case EC_CMD_FPWR:  // Configured Address Write
        case EC_CMD_BWR:   // Broadcast Write
        case EC_CMD_LWR:   // Logical Write
            is_write = true;
            break;

        case EC_CMD_APRW:  // Auto Increment Read-Write
        case EC_CMD_FPRW:  // Configured Address Read-Write
        case EC_CMD_BRW:   // Broadcast Read-Write
        case EC_CMD_LRW:   // Logical Read-Write (самая важная!)
            is_read = true;
            is_write = true;
            break;

        case EC_CMD_NOP:   // No Operation
            ethercat_frame_increment_wkc(datagram, 0);
            return;

        default:
            // Неизвестная команда - не инкрементируем WKC
            return;
    }

    // Проверка размера данных
    if (dlength > EC_MAXLRWDATA) {
        return;
    }

    logical = is_logical_command(datagram->command);

    // Для прямой адресации проверяем границы области целиком
    if (logical == false && (ADO + dlength) > memory_size) {
        return;  // Out of bounds
    }

    // Побайтный обмен: чтение выполняется ДО записи (для LRW/APRW/FPRW)
    for (i = 0; i < dlength; i++) {
        uint16 p = phys_addr(memory, memory_size, (uint16)(ADO + i), logical);

        if (p >= memory_size) {
            continue;
        }

        if (is_read == true) {
            data[i] = memory[p];
        }

        if (is_write == true) {
            memory[p] = data[i];
        }
    }

    if (is_write == true && logical == false) {
        notify_mem_change(ADO, dlength, datagram->command);
    }

    ethercat_frame_increment_wkc(datagram, dlength);
}

// ============================================================================
// Парсинг фрейма
// ============================================================================

uint16 ethercat_frame_parse(uint8* frame, uint16 length, 
                             uint8* memory, uint16 memory_size) {
    if (frame == NULL || memory == NULL || length < ETH_HEADERSIZE + EC_HEADERSIZE) {
        return 0;
    }
    
    // Проверка EtherType
    if (!ethercat_frame_is_ethercat(frame)) {
        return 0;
    }
    
    // Пропускаем Ethernet header
    uint8* ecat_data = frame + ETH_HEADERSIZE;
    uint16 ecat_length = length - ETH_HEADERSIZE;
    
    uint16 datagram_count = 0;
    uint16 offset = 0;
    
    // Парсим все datagrams в фрейме
    while (offset + EC_HEADERSIZE <= ecat_length) {
        ec_comt* datagram = (ec_comt*)(ecat_data + offset);
        
        // Получаем длину datagram
        uint16 elength = etohs(datagram->elength);
        uint16 dlength = etohs(datagram->dlength) & 0x7FF;
        
        // Проверка границ
        uint16 datagram_total_size = EC_HEADERSIZE + dlength + EC_WKCSIZE;
        if (offset + datagram_total_size > ecat_length) {
            break;  // Неполный datagram
        }
        
        // Обрабатываем команду
        ethercat_frame_process_command(datagram, memory, memory_size);
        
        datagram_count++;
        
        // Проверяем флаг "datagram follows"
        if (!(etohs(datagram->dlength) & EC_DATAGRAMFOLLOWS)) {
            break;  // Последний datagram
        }
        
        // Переходим к следующему datagram
        offset += datagram_total_size;
    }
    
    return datagram_count;
}
