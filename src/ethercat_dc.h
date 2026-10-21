/*
 * EtherCAT Distributed Clock (DC)
 * Системное время, SYNC0/SYNC1, синхронизация циклического обмена.
 *
 * Регистры сверены с эталонами: SOEM (src/ec_type.h, ec_config.c —
 * ecx_dcsync0), SOES (soes/esc.h), IgH (master/fsm_slave_scan.c).
 *
 * Phase 4, Week 8 (завершение протокола)
 * ESP32 EtherCAT Slave v4
 */

#ifndef ETHERCAT_DC_H
#define ETHERCAT_DC_H

#include "ethercat_type.h"
#include "ethercat_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Режимы синхронизации
// ============================================================================

typedef enum {
    ECT_DC_MODE_FREERUN = 0,  /**< Free-Run: обмен без синхронизации */
    ECT_DC_MODE_SYNC0 = 1,    /**< DC-Synchron: синхронизация по SYNC0 */
    ECT_DC_MODE_SYNC1 = 2,    /**< DC-Synchron по SYNC1 */
    ECT_DC_MODE_LOST = 3      /**< Синхронизация потеряна */
} ethercat_dc_mode_t;

/** Допустимое отклонение синхронизации (нс) */
#define ECT_DC_SYNC_TOLERANCE_NS  1000000LL   /* 1 мс */
/** Максимум пропущенных импульсов до признака потери синхронизации */
#define ECT_DC_MAX_MISSED         3

// ============================================================================
// Структура модуля DC
// ============================================================================

typedef struct {
    uint64 offset_ns;          /**< Сдвиг системного времени (реестр 0x0920) */
    uint64 next_sync0_ns;      /**< Время следующего импульса SYNC0 */
    uint64 last_sync0_ns;      /**< Время последнего импульса */
    int64  last_error_ns;      /**< Последняя ошибка синхронизации */
    uint32 sync0_cycle_ns;     /**< Период SYNC0 (нс) */
    uint32 sync1_cycle_ns;     /**< Период SYNC1 (нс) */
    uint32 sync_count;         /**< Число выданных импульсов SYNC0 */
    uint32 missed;             /**< Пропущенные импульсы */
    uint8  activation;         /**< Регистр активации (0x0981) */
    uint8  mode;               /**< Текущий режим (ethercat_dc_mode_t) */
    boolean sync0_active;      /**< SYNC0 активирован мастером */
    boolean initialized;       /**< Флаг инициализации */
} ethercat_dc_t;

// ============================================================================
// API
// ============================================================================

/**
 * @brief Инициализация модуля DC
 * @param dc Указатель на структуру DC
 */
void ethercat_dc_init(ethercat_dc_t* dc);

/**
 * @brief Циклическое обновление: системное время, времена приёма портов
 * @param dc Указатель на структуру DC
 * @param mem Указатель на карту памяти
 */
void ethercat_dc_update(ethercat_dc_t* dc, ethercat_memory_t* mem);

/**
 * @brief Проверить наступление импульса SYNC0
 *
 * Вызывать в цикле обработки фреймов. При наступлении импульса
 * рекомендуется «защёлкнуть» входы и применить выходы (DC-Synchron).
 *
 * @param dc Указатель на структуру DC
 * @param mem Указатель на карту памяти
 * @return Число прошедших импульсов SYNC0 (0, если импульса не было)
 */
uint8 ethercat_dc_check_sync(ethercat_dc_t* dc, ethercat_memory_t* mem);

/**
 * @brief Текущий режим синхронизации
 */
ethercat_dc_mode_t ethercat_dc_get_mode(const ethercat_dc_t* dc);

/**
 * @brief Период SYNC0 в наносекундах (0 = Free-Run)
 */
uint32 ethercat_dc_get_cycle_ns(const ethercat_dc_t* dc);

/**
 * @brief Ошибка синхронизации в наносекундах
 */
int64 ethercat_dc_get_error_ns(const ethercat_dc_t* dc);

/**
 * @brief Получить текущее системное время DC (нс)
 */
uint64 ethercat_dc_get_time_ns(const ethercat_dc_t* dc);

#ifdef __cplusplus
}
#endif

#endif // ETHERCAT_DC_H
