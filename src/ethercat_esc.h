/*
 * EtherCAT ESC Register Emulation
 * Эмуляция регистров ESC: идентификация, DL/AL, SyncManager, FMMU,
 * интерфейс EEPROM (SII), Distributed Clock.
 *
 * Сверено с эталонами: SOEM (src/ec_type.h), IgH EtherCAT Master
 * (master/globals.h, fsm_sii.c, fsm_slave_scan.c), SOES (soes/esc.h).
 *
 * Phase 4, Week 8 (завершение протокола)
 * ESP32 EtherCAT Slave v4
 */

#ifndef ETHERCAT_ESC_H
#define ETHERCAT_ESC_H

#include "ethercat_type.h"
#include "ethercat_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Идентификация изделия (SII, ESI/EDS, CoE 0x1018)
// ============================================================================

#define ECT_VENDOR_ID        0x00000000UL  /**< Vendor ID (внутренний, не зарег. в ETG) */
#define ECT_PRODUCT_CODE     0x00000001UL  /**< Product Code */
#define ECT_REVISION_NUMBER  0x00010000UL  /**< Revision (V1.0) */
#define ECT_SERIAL_NUMBER    0x00000001UL  /**< Serial Number */
#define ECT_DEVICE_NAME      "ESP32 EtherCAT Slave v4"
#define ECT_DEVICE_ORDER     "ESP32-ECAT-SLAVE-4"
#define ECT_DEVICE_GROUP     "ESP32 EtherCAT"
#define ECT_VENDOR_NAME      "ESP32 EtherCAT"

#define ECT_ESC_TYPE         0x01  /**< Тип ESC (0x01 = EtherCAT Slave Controller) */
#define ECT_ESC_REVISION     0x01  /**< Ревизия ESC (регистр 0x0001) */
#define ECT_ESC_BUILD        0x0001 /**< Номер сборки (регистр 0x0002) */
#define ECT_FMMU_COUNT       8     /**< Число FMMU (0x0600..0x067F) */
#define ECT_SM_COUNT         4     /**< Число SyncManager (0x0800..0x081F) */

// ============================================================================
// Адреса регистров ESC
// ============================================================================
//
// Внимание: ECT_REG_TYPE, ECT_REG_PORTDES, ECT_REG_STADR, ECT_REG_ALIAS,
// ECT_REG_ALCTL, ECT_REG_ALSTAT, ECT_REG_ALSTATCODE, ECT_REG_PDICTL,
// ECT_REG_FMMU0, ECT_REG_SM0..SM3, ECT_REG_DCSYSTIME, ECT_REG_DCSTART0 и
// ECT_REG_DCCYCLE0 уже объявлены в ethercat_type.h — здесь не повторяются.

#define ECT_REG_BUILD        0x0002  /**< Номер сборки */
#define ECT_REG_FMMU_COUNT   0x0004  /**< Число FMMU */
#define ECT_REG_SM_COUNT     0x0005  /**< Число SyncManager */
#define ECT_REG_ESCSUP       0x0008  /**< Возможности ESC (ESC Features) */

#define ECT_REG_DLCTL        0x0100  /**< DL Control */
#define ECT_REG_DLSTAT       0x0110  /**< DL Status (link/loop) */

#define ECT_REG_EEPCFG       0x0500  /**< EEPROM Configuration */
#define ECT_REG_EEPCTL       0x0502  /**< EEPROM Control/Status (word) */
#define ECT_REG_EEPSTAT      0x0502  /**< EEPROM Status (word, чтение) */
#define ECT_REG_EEPADR       0x0504  /**< EEPROM Address (word) */
#define ECT_REG_EEPDAT       0x0508  /**< EEPROM Data (8 байт) */

#define ECT_FMMU_PAGE_SIZE   0x10    /**< Размер записи FMMU */
#define ECT_SM_PAGE_SIZE     0x08    /**< Размер записи SyncManager */
#define ECT_SM0STAT          (ECT_REG_SM0 + 5)    /**< Статус SM0 (0x0805) */
#define ECT_SM1STAT          (ECT_REG_SM0 + 0x0D) /**< Статус SM1 (0x080D) */
#define ECT_SM_ENABLE_BIT    0x01    /**< Бит вкл. в регистре активации SM */
#define ECT_SMSTAT_BUFFERFULL 0x08   /**< Бит "буфер/ящик заполнен" */
#define ECT_SMSTAT_MAILBOX   0x08    /**< Бит "mailbox full" */
#define ECT_SM_MODE_MASK     0x03    /**< Биты режима работы SM */
#define ECT_SM_MODE_MAILBOX  0x02    /**< Режим mailbox (однобуферный) */
#define ECT_SM_DIR_WRITE     0x04    /**< Направление: запись (RxPDO, MbxOut) */

#define ECT_REG_DCTIME0      0x0900  /**< Время приёма порт 0 */
#define ECT_REG_DCSOF        0x0918  /**< Время приёма ECAT processing unit */
#define ECT_REG_DCSYSOFFSET  0x0920  /**< Сдвиг системного времени */
#define ECT_REG_DCSYSDELAY   0x0928  /**< Задержка распространения */
#define ECT_REG_DCSYSDIFF    0x092C  /**< Разница системного времени */
#define ECT_REG_DCTIMEFILT   0x0934  /**< Фильтр времени DC */
#define ECT_REG_DCCUC        0x0980  /**< DC Control (сброс цикл. работы) */
#define ECT_REG_DCSYNCACT    0x0981  /**< DC Activation / Status */
#define ECT_REG_DCSYNCPULSE  0x0982  /**< Длина импульса SYNC */
#define ECT_REG_DCCYCLE1     0x09A4  /**< SYNC1 Cycle Time (32 бита) */

#define ECT_DCACT_CYCLIC_OP  0x01    /**< Активировать циклическую работу */
#define ECT_DCACT_SYNC0      0x02    /**< Активировать SYNC0 */
#define ECT_DCACT_SYNC1      0x04    /**< Активировать SYNC1 */

// ============================================================================
// Интерфейс EEPROM / SII (Slave Information Interface)
// ============================================================================
//
// Протокол (сверен с IgH master/fsm_sii.c и SOEM ec_main.c):
//   1. Мастер пишет 6 байт начиная с 0x0502:
//        0x0502..0x0503 = команда (16 бит, LE): bit8 = Read, bit9 = Write
//        0x0504..0x0505 = адрес слова EEPROM
//        (IgH пишет 4 байта: команда | 0x80, адрес)
//   2. Мастер читает 0x0502..0x050B (10 байт):
//        0x0503 bit0 / bit7 = BUSY, bit5 = NACK
//        0x0508..0x050B    = данные (2 слова SII)
//   3. Слейв обязан очистить команду (0x0502 = 0x0000) после обслуживания,
//      иначе мастер видит "busy" до таймаута.
//
// Смещения SII (в словах) — по IgH master/fsm_slave_scan.c:
//   0x0004 Alias | 0x0008 VendorID | 0x000A ProductCode | 0x000C Revision
//   0x000E Serial | 0x0014..0x0017 Bootstrap Mbx | 0x0018..0x001B Std Mbx
//   0x001C Mailbox Protocols | 0x0040 Начало категорий (0xFFFF = конец)
//

#define ECT_EEPROM_CMD_READ   0x0100  /**< Команда чтения (bit8) */
#define ECT_EEPROM_CMD_WRITE  0x0200  /**< Команда записи (bit9) */
#define ECT_EEPROM_CMD_MASK   0x0300  /**< Маска команд */
#define ECT_EEPROM_ADDR_MASK  0xFFFF  /**< Маска адреса слова */

#define ECT_SII_FIRST_CATEGORY 0x0040 /**< Слово начала категорий SII */
#define ECT_SII_MAX_WORDS      512    /**< Размер образа SII (слов) */

#define ECT_SII_CAT_STRINGS   0x000A  /**< Категория Strings */
#define ECT_SII_CAT_GENERAL   0x001E  /**< Категория General (32 байта) */
#define ECT_SII_CAT_FMMU      0x0028  /**< Категория FMMU */
#define ECT_SII_CAT_SYNC      0x0029  /**< Категория SyncManager */
#define ECT_SII_CAT_TXPDO     0x0032  /**< Категория TxPDO */
#define ECT_SII_CAT_RXPDO     0x0033  /**< Категория RxPDO */
#define ECT_SII_CAT_END       0xFFFF  /**< Конец категорий */

#define ECT_SII_SMTYPE_MBXOUT 1  /**< SM0: почтовый ящик (мастер → слейв) */
#define ECT_SII_SMTYPE_MBXIN  2  /**< SM1: почтовый ящик (слейв → мастер) */
#define ECT_SII_SMTYPE_OUTPUT 3  /**< SM2: выходные данные (RxPDO) */
#define ECT_SII_SMTYPE_INPUT  4  /**< SM3: входные данные (TxPDO) */

#define ECT_SMCTRL_MBXOUT     0x26  /**< SM0: mailbox, запись, ECAT event */
#define ECT_SMCTRL_MBXIN      0x22  /**< SM1: mailbox, чтение, ECAT event */
#define ECT_SMCTRL_OUTPUT     0x24  /**< SM2: 3-буфер, запись, ECAT event */
#define ECT_SMCTRL_INPUT      0x20  /**< SM3: 3-буфер, чтение, ECAT event */

#define ECT_MBX_PROTO_COE     0x0004 /**< Бит CoE в регистре протоколов MBX */
#define ECT_MBX_PROTO_FOE     0x0008 /**< Бит FoE */
#define ECT_MBX_SIZE          128    /**< Размер почтового ящика (байт) */

// ============================================================================
// FMMU (Fieldbus Memory Management Unit)
// ============================================================================

#define ECT_FMMU_LOGICAL_START    0x00 /**< Логический адрес (16 бит) */
#define ECT_FMMU_LENGTH           0x02 /**< Длина (16 бит) */
#define ECT_FMMU_LOGICAL_STARTBIT 0x04 /**< Младший бит логического адреса */
#define ECT_FMMU_LOGICAL_STOPBIT  0x05 /**< Старший бит логического адреса */
#define ECT_FMMU_PHYSICAL_START   0x06 /**< Физический адрес (16 бит) */
#define ECT_FMMU_PHYS_STARTBIT    0x08 /**< Младший бит физического адреса */
#define ECT_FMMU_TYPE             0x09 /**< Бит 0: чтение, бит 1: запись */
#define ECT_FMMU_ACTIVATE         0x0A /**< Бит 0: активирован */

// ============================================================================
// Структура слоя ESC
// ============================================================================

typedef struct {
    uint16 sii[ECT_SII_MAX_WORDS]; /**< Образ SII (для интерфейса EEPROM) */
    uint16 sii_words;              /**< Число значащих слов в образе */
    uint16 eeprom_addr;            /**< Последний запрошенный адрес слова */
    uint32 eeprom_reads;           /**< Счётчик чтений EEPROM */
    uint32 eeprom_writes;          /**< Счётчик записей EEPROM */
    uint16 last_cmd;               /**< Последняя команда EEPROM */
    boolean initialized;           /**< Флаг инициализации */
} ethercat_esc_t;

// ============================================================================
// API
// ============================================================================

/**
 * @brief Заполнить регистры ESC и построить образ SII
 * @param esc Указатель на структуру ESC (NULL — только регистры)
 * @param mem Указатель на карту памяти
 */
void ethercat_esc_init(ethercat_esc_t* esc, ethercat_memory_t* mem);

/**
 * @brief Построить образ SII (EEPROM) в esc->sii
 * @param esc Указатель на структуру ESC
 * @return Число значащих слов SII
 */
uint16 ethercat_esc_build_sii(ethercat_esc_t* esc);

/**
 * @brief Обслужить запрос мастера к интерфейсу EEPROM (вызывать после
 *        обработки каждого принятого EtherCAT-фрейма)
 * @param esc Указатель на структуру ESC
 * @param mem Указатель на карту памяти
 * @return true если запрос был обработан
 */
boolean ethercat_esc_process_eeprom(ethercat_esc_t* esc, ethercat_memory_t* mem);

/**
 * @brief Расшифровать логический адрес через таблицу FMMU
 * @param memory Указатель на карту памяти (таблица FMMU в 0x0600)
 * @param logical Логический адрес байта
 * @param physical Указатель для физического адреса
 * @return true если найден активный FMMU, иначе false
 */
boolean ethercat_esc_fmmu_translate(const uint8* memory,
                                    uint16 logical,
                                    uint16* physical);

/**
 * @brief Получить физический адрес области PDO из настроек SyncManager
 * @param mem Указатель на карту памяти
 * @param output true = RxPDO (SM2), false = TxPDO (SM3)
 * @return Физический адрес (0 если SM не настроен)
 */
uint16 ethercat_esc_get_pdo_address(const ethercat_memory_t* mem, boolean output);

/**
 * @brief Получить длину области PDO из настроек SyncManager
 * @param mem Указатель на карту памяти
 * @param output true = RxPDO (SM2), false = TxPDO (SM3)
 * @return Длина в байтах
 */
uint16 ethercat_esc_get_pdo_length(const ethercat_memory_t* mem, boolean output);

/**
 * @brief Проверить настройки SyncManager перед переходом в OP
 * @param mem Указатель на карту памяти
 * @return 0 = OK, иначе код AL Status Code
 */
uint16 ethercat_esc_check_sm_config(const ethercat_memory_t* mem);

/**
 * @brief Обновить DL Status (link/loop по портам)
 */
void ethercat_esc_set_dl_status(ethercat_memory_t* mem, uint16 dl_status);

/**
 * @brief Обновить счётчики/регистры ошибок ESC
 * @param mem Указатель на карту памяти
 * @param frame_ok true = фрейм принят без ошибок
 */
void ethercat_esc_update_counters(ethercat_memory_t* mem, boolean frame_ok);

#ifdef __cplusplus
}
#endif

#endif // ETHERCAT_ESC_H
