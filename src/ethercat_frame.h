/*
 * EtherCAT Frame Parser
 * Парсинг и обработка EtherCAT фреймов
 * 
 * Phase 1, Week 1, Day 3-4
 * ESP32 EtherCAT Slave v4
 */

#ifndef ETHERCAT_FRAME_H
#define ETHERCAT_FRAME_H

#include "ethercat_type.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Проверка, является ли фрейм EtherCAT
 * @param frame Указатель на Ethernet фрейм
 * @return true если EtherType == 0x88A4
 */
boolean ethercat_frame_is_ethercat(uint8* frame);

/**
 * @brief Парсинг EtherCAT фрейма
 * @param frame Указатель на Ethernet фрейм
 * @param length Длина фрейма
 * @param memory Указатель на memory map
 * @param memory_size Размер memory map
 * @return Количество обработанных datagrams
 */
uint16 ethercat_frame_parse(uint8* frame, uint16 length, 
                             uint8* memory, uint16 memory_size);

/**
 * @brief Обработка одного datagram
 * @param datagram Указатель на EtherCAT datagram header
 * @param memory Указатель на memory map
 * @param memory_size Размер memory map
 */
void ethercat_frame_process_command(ec_comt* datagram, 
                                     uint8* memory, 
                                     uint16 memory_size);

/**
 * Хук изменения карты памяти: вызывается при каждой записи в память
 * (для обслуживания почтового ящика CoE и интерфейса SII/EEPROM).
 *
 * @param ctx Произвольный контекст (передаётся в хук)
 * @param addr Адрес начала изменённой области (физический)
 * @param length Число байт
 * @param command Команда EtherCAT, вызвавшая запись
 */
typedef void (*ec_mem_change_cb_t)(void* ctx, uint16 addr, uint16 length, uint8 command);

/**
 * @brief Установить хук изменений карты памяти
 * @param cb Функция-хук (NULL — снять)
 * @param ctx Контекст хука
 */
void ethercat_frame_set_mem_change_hook(ec_mem_change_cb_t cb, void* ctx);

/**
 * @brief Инкремент Working Counter
 * @param datagram Указатель на EtherCAT datagram header
 * @param dlength Длина данных (без header и WKC)
 */
void ethercat_frame_increment_wkc(ec_comt* datagram, uint16 dlength);

#ifdef __cplusplus
}
#endif

#endif // ETHERCAT_FRAME_H
