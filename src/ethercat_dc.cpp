/*
 * EtherCAT Distributed Clock - Implementation
 *
 * Phase 4, Week 8 (завершение протокола)
 * ESP32 EtherCAT Slave v4
 */

#include "ethercat_dc.h"
#include "ethercat_esc.h"
#include <esp_timer.h>
#include <stddef.h>

// ============================================================================
// Инициализация
// ============================================================================

void ethercat_dc_init(ethercat_dc_t* dc) {
    if (dc == NULL) {
        return;
    }

    dc->offset_ns = 0;
    dc->next_sync0_ns = 0;
    dc->last_sync0_ns = 0;
    dc->last_error_ns = 0;
    dc->sync0_cycle_ns = 0;
    dc->sync1_cycle_ns = 0;
    dc->sync_count = 0;
    dc->missed = 0;
    dc->activation = 0;
    dc->mode = ECT_DC_MODE_FREERUN;
    dc->sync0_active = false;
    dc->initialized = true;
}

/** Локальное время в наносекундах (отсчёт от запуска ESP32) */
static uint64 dc_local_time_ns(void) {
    return (uint64)esp_timer_get_time() * 1000ULL;
}

/** Системное время DC с учётом сдвига */
static uint64 dc_sys_time_ns(const ethercat_dc_t* dc) {
    return dc->offset_ns + dc_local_time_ns();
}

// ============================================================================
// Циклическое обновление
// ============================================================================

void ethercat_dc_update(ethercat_dc_t* dc, ethercat_memory_t* mem) {
    uint64 now;
    uint8 activation;
    uint32 cycle;

    if (dc == NULL || mem == NULL || dc->initialized == false) {
        return;
    }

    /* Сдвиг системного времени (задаётся мастером) */
    dc->offset_ns = ethercat_memory_read64(mem, ECT_REG_DCSYSOFFSET);

    now = dc_sys_time_ns(dc);

    /* Системное время (0x0910) и время приёма ECAT (0x0918) */
    ethercat_memory_write64(mem, ECT_REG_DCSYSTIME, now);
    ethercat_memory_write32(mem, ECT_REG_DCSOF, (uint32)(now & 0xFFFFFFFFULL));

    /* Регистр активации (0x0981) и периоды */
    activation = ethercat_memory_read8(mem, ECT_REG_DCSYNCACT);
    dc->activation = activation;

    cycle = ethercat_memory_read32(mem, ECT_REG_DCCYCLE0);
    dc->sync0_cycle_ns = cycle;
    dc->sync1_cycle_ns = ethercat_memory_read32(mem, ECT_REG_DCCYCLE1);

    if ((activation & ECT_DCACT_SYNC0) != 0 && cycle > 0) {
        if (dc->sync0_active == false) {
            uint64 start_time = ethercat_memory_read64(mem, ECT_REG_DCSTART0);

            dc->sync0_active = true;
            dc->mode = ECT_DC_MODE_SYNC0;
            dc->missed = 0;
            dc->last_error_ns = 0;
            dc->next_sync0_ns = (start_time != 0) ? start_time : now;

            /* Стартовое время в прошлом — сдвигаем к будущему */
            while (dc->next_sync0_ns < now) {
                dc->next_sync0_ns += cycle;
            }
        }
    } else if ((activation & ECT_DCACT_SYNC0) == 0) {
        if (dc->sync0_active == true) {
            /* Мастер отключил SYNC0 */
            dc->sync0_active = false;
            dc->mode = ECT_DC_MODE_FREERUN;
            dc->next_sync0_ns = 0;
            dc->missed = 0;
            dc->last_error_ns = 0;
        } else if (dc->mode != ECT_DC_MODE_FREERUN) {
            dc->mode = ECT_DC_MODE_FREERUN;
        }
    }

    /* Разница системного времени (0x092C) */
    if (dc->sync0_active == true && dc->next_sync0_ns != 0) {
        int64 diff = (int64)now - (int64)dc->next_sync0_ns;
        ethercat_memory_write32(mem, ECT_REG_DCSYSDIFF, (uint32)(diff & 0xFFFFFFFFLL));
    } else {
        ethercat_memory_write32(mem, ECT_REG_DCSYSDIFF, 0x00000000);
    }
}

// ============================================================================
// Проверка импульса SYNC0
// ============================================================================

uint8 ethercat_dc_check_sync(ethercat_dc_t* dc, ethercat_memory_t* mem) {
    uint64 now;
    uint8 edges = 0;

    (void)mem;

    if (dc == NULL || dc->initialized == false) {
        return 0;
    }

    if (dc->sync0_active == false || dc->next_sync0_ns == 0 ||
        dc->sync0_cycle_ns == 0) {
        return 0;
    }

    now = dc_sys_time_ns(dc);

    if (now < dc->next_sync0_ns) {
        /* Импульс ещё не наступил: оцениваем ошибку синхронизации */
        dc->last_error_ns = (int64)dc->next_sync0_ns - (int64)now;
        return 0;
    }

    /* Импульс (или несколько) наступил */
    while (now >= dc->next_sync0_ns) {
        dc->last_sync0_ns = dc->next_sync0_ns;
        dc->next_sync0_ns += dc->sync0_cycle_ns;
        dc->sync_count++;
        edges++;

        if (edges > ECT_DC_MAX_MISSED) {
            /* Слишком много пропущенных импульсов: расписание подтягивается
             * к текущему времени, синхронизация считается потерянной */
            dc->missed += (uint8)(edges - 1);
            dc->next_sync0_ns = now + dc->sync0_cycle_ns;
            dc->mode = ECT_DC_MODE_LOST;
            break;
        }
    }

    dc->last_error_ns = (int64)now - (int64)dc->last_sync0_ns;

    if (dc->last_error_ns < 0) {
        dc->last_error_ns = -dc->last_error_ns;
    }

    if (dc->mode == ECT_DC_MODE_LOST &&
        dc->last_error_ns <= ECT_DC_SYNC_TOLERANCE_NS) {
        dc->mode = ECT_DC_MODE_SYNC0;
    }

    return edges;
}

// ============================================================================
// Геттеры
// ============================================================================

ethercat_dc_mode_t ethercat_dc_get_mode(const ethercat_dc_t* dc) {
    return (dc == NULL) ? ECT_DC_MODE_FREERUN : (ethercat_dc_mode_t)dc->mode;
}

uint32 ethercat_dc_get_cycle_ns(const ethercat_dc_t* dc) {
    return (dc == NULL) ? 0 : dc->sync0_cycle_ns;
}

int64 ethercat_dc_get_error_ns(const ethercat_dc_t* dc) {
    return (dc == NULL) ? 0 : dc->last_error_ns;
}

uint64 ethercat_dc_get_time_ns(const ethercat_dc_t* dc) {
    if (dc == NULL) {
        return 0;
    }
    return dc_sys_time_ns(dc);
}
