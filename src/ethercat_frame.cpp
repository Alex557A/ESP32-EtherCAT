/*
 * EtherCAT Frame Parser - Implementation
 * 
 * Phase 1, Week 1, Day 3-7
 * ESP32 EtherCAT Slave v4
 */

#include "ethercat_frame.h"
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
// Обработка команды
// ============================================================================

void ethercat_frame_process_command(ec_comt* datagram, 
                                     uint8* memory, 
                                     uint16 memory_size) {
    if (datagram == NULL || memory == NULL) {
        return;
    }
    
    uint16 ADO = etohs(datagram->ADO);
    uint16 dlength = etohs(datagram->dlength) & 0x7FF;  // Маска EC_DATAGRAMFOLLOWS
    uint8* data = (uint8*)(datagram + 1);
    
    // Проверка границ memory
    if (ADO + dlength > memory_size) {
        return;  // Out of bounds
    }
    
    switch (datagram->command) {
        case EC_CMD_APRD:  // Auto Increment Read
        case EC_CMD_FPRD:  // Configured Address Read
        case EC_CMD_BRD:   // Broadcast Read
        case EC_CMD_LRD:   // Logical Read
            // Read: копируем из memory в datagram
            memcpy(data, &memory[ADO], dlength);
            ethercat_frame_increment_wkc(datagram, dlength);
            break;
            
        case EC_CMD_APWR:  // Auto Increment Write
        case EC_CMD_FPWR:  // Configured Address Write
        case EC_CMD_BWR:   // Broadcast Write
        case EC_CMD_LWR:   // Logical Write
            // Write: копируем из datagram в memory
            memcpy(&memory[ADO], data, dlength);
            ethercat_frame_increment_wkc(datagram, dlength);
            break;
            
        case EC_CMD_LRW:   // Logical Read-Write (самая важная!)
        {
            // LRW: swap данных (read current + write new)
            uint8 temp[EC_MAXLRWDATA];
            
            // Проверка размера
            if (dlength > EC_MAXLRWDATA) {
                return;
            }
            
            // 1. Читаем текущие данные из memory
            memcpy(temp, &memory[ADO], dlength);
            
            // 2. Пишем новые данные в memory
            memcpy(&memory[ADO], data, dlength);
            
            // 3. Возвращаем старые данные в datagram
            memcpy(data, temp, dlength);
            
            ethercat_frame_increment_wkc(datagram, dlength);
            break;
        }
        
        case EC_CMD_APRW:  // Auto Increment Read-Write
        case EC_CMD_FPRW:  // Configured Address Read-Write
        case EC_CMD_BRW:   // Broadcast Read-Write
        {
            // Read-Write: аналогично LRW
            uint8 temp[EC_MAXLRWDATA];
            
            if (dlength > EC_MAXLRWDATA) {
                return;
            }
            
            memcpy(temp, &memory[ADO], dlength);
            memcpy(&memory[ADO], data, dlength);
            memcpy(data, temp, dlength);
            
            ethercat_frame_increment_wkc(datagram, dlength);
            break;
        }
        
        case EC_CMD_NOP:   // No Operation
            // NOP: просто инкрементируем WKC
            ethercat_frame_increment_wkc(datagram, 0);
            break;
        
        default:
            // Неизвестная команда - не инкрементируем WKC
            break;
    }
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
