/*
 * EtherCAT Object Dictionary + CoE (SDO)
 * Словарь объектов слейва, сервисы SDO (upload/download, в т.ч.
 * сегментированные), SDO Information (OD List / Object / Entry Description),
 * применение PDO-маппинга.
 *
 * Протоколы сверены с эталонами: SOES (soes/esc_coe.c, esc_coe.h),
 * IgH EtherCAT Master (master/fsm_coe.c), SOEM (src/ec_coe.c, ec_type.h).
 *
 * Phase 4, Week 8 (завершение протокола)
 * ESP32 EtherCAT Slave v4
 */

#ifndef ETHERCAT_OD_H
#define ETHERCAT_OD_H

#include "ethercat_type.h"
#include "ethercat_memory.h"

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Объекты и типы данных
// ============================================================================

#define ECT_OTYPE_DOMAIN      0x0002  /**< Domain (область памяти) */
#define ECT_OTYPE_DEFTYPE     0x0005  /**< Deftype */
#define ECT_OTYPE_DEFSTRUCT   0x0006  /**< Defstruct */
#define ECT_OTYPE_VAR         0x0007  /**< Переменная */
#define ECT_OTYPE_ARRAY       0x0008  /**< Массив */
#define ECT_OTYPE_RECORD      0x0009  /**< Запись (структура) */

#define ECT_DTYPE_BOOLEAN         0x0001
#define ECT_DTYPE_INTEGER8        0x0002
#define ECT_DTYPE_INTEGER16       0x0003
#define ECT_DTYPE_INTEGER32       0x0004
#define ECT_DTYPE_UNSIGNED8       0x0005
#define ECT_DTYPE_UNSIGNED16      0x0006
#define ECT_DTYPE_UNSIGNED32      0x0007
#define ECT_DTYPE_REAL32          0x0008
#define ECT_DTYPE_VISIBLE_STRING  0x0009
#define ECT_DTYPE_OCTET_STRING    0x000A
#define ECT_DTYPE_UNICODE_STRING  0x000B
#define ECT_DTYPE_INTEGER64       0x0015
#define ECT_DTYPE_UNSIGNED64      0x001B
#define ECT_DTYPE_PDO_MAPPING     0x0021
#define ECT_DTYPE_IDENTITY        0x0023
#define ECT_DTYPE_BIT1            0x0030

// Права доступа (биты как в ETG.1000.6 и SII)
#define ECT_ATYPE_Rpre       0x0001  /**< Чтение в PREOP */
#define ECT_ATYPE_Rsafe      0x0002  /**< Чтение в SAFEOP */
#define ECT_ATYPE_Rop        0x0004  /**< Чтение в OP */
#define ECT_ATYPE_Wpre       0x0008  /**< Запись в PREOP */
#define ECT_ATYPE_Wsafe      0x0010  /**< Запись в SAFEOP */
#define ECT_ATYPE_Wop        0x0020  /**< Запись в OP */
#define ECT_ATYPE_RXPDO      0x0040  /**< Входит в RxPDO */
#define ECT_ATYPE_TXPDO      0x0080  /**< Входит в TxPDO */

#define ECT_ATYPE_RO   (ECT_ATYPE_Rpre | ECT_ATYPE_Rsafe | ECT_ATYPE_Rop)
#define ECT_ATYPE_WO   (ECT_ATYPE_Wpre | ECT_ATYPE_Wsafe | ECT_ATYPE_Wop)
#define ECT_ATYPE_RW   (ECT_ATYPE_RO | ECT_ATYPE_WO)

// ============================================================================
// CoE: сервисы, команды, коды прерываний
// ============================================================================

#define ECT_COE_SDO_REQUEST   0x02  /**< Сервис: SDO Request */
#define ECT_COE_SDO_RESPONSE  0x03  /**< Сервис: SDO Response */
#define ECT_COE_SDO_INFO      0x08  /**< Сервис: SDO Information */

#define ECT_SDO_CMD_MASK              0xE0
#define ECT_SDO_UPLOAD_REQUEST        0x40  /**< Initiate upload request */
#define ECT_SDO_UPLOAD_RESPONSE       0x40  /**< Initiate upload response */
#define ECT_SDO_DOWNLOAD_REQUEST      0x20  /**< Initiate download request */
#define ECT_SDO_DOWNLOAD_RESPONSE     0x60  /**< Initiate download response */
#define ECT_SDO_UPLOAD_SEG_REQUEST    0x60  /**< Upload segment request */
#define ECT_SDO_UPLOAD_SEG_RESPONSE   0x00  /**< Upload segment response */
#define ECT_SDO_DOWNLOAD_SEG_REQUEST  0x00  /**< Download segment request */
#define ECT_SDO_DOWNLOAD_SEG_RESPONSE 0x20  /**< Download segment response */
#define ECT_SDO_ABORT                 0x80  /**< Abort transfer */
#define ECT_SDO_SIZE_INDICATOR        0x01  /**< Размер указан */
#define ECT_SDO_EXPEDITED             0x02  /**< Expedited (до 4 байт) */
#define ECT_SDO_TOGGLE                0x10  /**< Бит toggle */
#define ECT_SDO_COMPLETE_ACCESS       0x10  /**< Complete access */
#define ECT_SDO_LAST_SEGMENT          0x01  /**< Последний сегмент */
#define ECT_SDO_INVALID_SIZE_MASK     0x0C  /**< Биты размера (n) */

// Opcodes сервиса SDO Information
#define ECT_SDO_INFO_OD_LIST_REQ     0x01  /**< Get OD List request */
#define ECT_SDO_INFO_OD_LIST_RES     0x02  /**< Get OD List response */
#define ECT_SDO_INFO_OBJ_DESC_REQ    0x03  /**< Get Object Description request */
#define ECT_SDO_INFO_OBJ_DESC_RES    0x04  /**< Get Object Description response */
#define ECT_SDO_INFO_ENTRY_DESC_REQ  0x05  /**< Get Entry Description request */
#define ECT_SDO_INFO_ENTRY_DESC_RES  0x06  /**< Get Entry Description response */
#define ECT_SDO_INFO_ERROR           0x07  /**< SDO Info Error */
#define ECT_SDO_INFO_INCOMPLETE      0x80  /**< Бит "фрагмент не последний" */

#define ECT_SDO_VALUEINFO_ACCESS     0x01
#define ECT_SDO_VALUEINFO_OBJECT     0x02
#define ECT_SDO_VALUEINFO_MAPPABLE   0x04

// Коды прерываний (abort codes)
#define ECT_ABORT_NOTOGGLE           0x05030000UL
#define ECT_ABORT_TIMEOUT            0x05040000UL
#define ECT_ABORT_UNSUPPORTED        0x05040001UL
#define ECT_ABORT_INVALID_SIZE       0x05040004UL
#define ECT_ABORT_OUT_OF_MEMORY      0x05040005UL
#define ECT_ABORT_UNSUPPORTED_ACCESS 0x06010000UL
#define ECT_ABORT_WRITE_ONLY         0x06010001UL
#define ECT_ABORT_READ_ONLY          0x06010002UL
#define ECT_ABORT_NO_OBJECT          0x06020000UL
#define ECT_ABORT_SUBINDEX_NOT_EXIST 0x06090011UL
#define ECT_ABORT_VALUE_TOO_HIGH     0x06090031UL
#define ECT_ABORT_VALUE_TOO_LOW      0x06090032UL
#define ECT_ABORT_GENERAL_ERROR      0x08000000UL
#define ECT_ABORT_DATA_TRANSFER      0x08000020UL
#define ECT_ABORT_NOT_IN_STATE       0x08000022UL

// Почтовый ящик
#define ECT_MBX_TYPE_COE       0x03  /**< Тип сервиса CoE в заголовке ящика */
#define ECT_MBX_TYPE_ERR       0x00  /**< Ошибка почтового ящика */
#define ECT_MBX_HEADER_SIZE    6     /**< Размер заголовка ящика */
#define ECT_MBX_COE_HEADER_SIZE 2    /**< Размер заголовка CoE */
#define ECT_MBX_MIN_LENGTH     8     /**< Минимальная длина сервисных данных */

// ============================================================================
// Хранилище словаря
// ============================================================================

#define ECT_OD_DATA_SIZE    320  /**< Размер области значений словаря (байт) */
#define ECT_OD_MAX_OBJECTS  16   /**< Максимум объектов в словаре */

/** Источник данных записи словаря */
typedef enum {
    ECT_OD_SRC_RAM = 0,   /**< Значение в области данных словаря */
    ECT_OD_SRC_CONST,     /**< Константа (только чтение) */
    ECT_OD_SRC_PDO_IN,    /**< Область входных данных (TxPDO) */
    ECT_OD_SRC_PDO_OUT    /**< Область выходных данных (RxPDO) */
} ethercat_od_src_t;

/** Описание записи словаря (объект + подиндекс) */
typedef struct {
    uint16 index;        /**< Индекс объекта */
    uint8 subindex;      /**< Подиндекс */
    uint16 datatype;     /**< Тип данных (ECT_DTYPE_*) */
    uint16 bitlength;    /**< Длина в битах */
    uint16 flags;        /**< Права доступа (ECT_ATYPE_*) */
    const char* name;    /**< Имя (для SDO Info и ESI) */
    uint8 src;           /**< Источник данных */
    uint16 off;          /**< Смещение в источнике */
    uint32 value;        /**< Значение для ECT_OD_SRC_CONST */
} ethercat_od_entry_t;

/** Описание объекта словаря */
typedef struct {
    uint16 index;        /**< Индекс */
    uint16 objtype;      /**< Тип объекта (ECT_OTYPE_*) */
    uint8 maxsub;        /**< Число подиндексов (без учёта нулевого) */
    const char* name;    /**< Имя объекта */
    const ethercat_od_entry_t* entries; /**< Массив записей (maxsub + 1) */
} ethercat_od_object_t;

/** Состояние сегментированного SDO-обмена */
typedef struct {
    uint8 active;          /**< Обмен активен */
    uint8 complete_access; /**< Complete Access */
    uint8 toggle;          /**< Ожидаемый бит toggle */
    uint16 index;          /**< Индекс объекта */
    uint32 offset;         /**< Текущее смещение (байт) */
    uint32 size;           /**< Размер данных (байт) */
    uint8* ptr;            /**< Указатель на данные */
} ethercat_od_transfer_t;

/** Словарь объектов слейва (экземпляр) */
typedef struct {
    uint8 data[ECT_OD_DATA_SIZE];        /**< Значения объектов */
    uint8 object_count;                  /**< Число объектов */
    uint8* pdo_in;                       /**< Область входных данных */
    uint8* pdo_out;                      /**< Область выходных данных */
    uint16 pdo_in_size;                  /**< Размер TxPDO по маппингу (байт) */
    uint16 pdo_out_size;                 /**< Размер RxPDO по маппингу (байт) */
    ethercat_od_transfer_t upload;       /**< Текущий upload */
    ethercat_od_transfer_t download;     /**< Текущий download */
    boolean pdo_dirty;                   /**< Нужно применить PDO-маппинг */
    uint32 sdo_uploads;                  /**< Счётчик загрузок */
    uint32 sdo_downloads;                /**< Счётчик выгрузок */
    uint32 sdo_aborts;                   /**< Счётчик прерываний */
    uint32 mbx_processed;                /**< Счётчик обработанных ящиков */
    uint32 mbx_errors;                   /**< Счётчик ошибок ящика */
    boolean initialized;                 /**< Флаг инициализации */
} ethercat_od_t;

// ============================================================================
// API
// ============================================================================

/**
 * @brief Инициализация словаря объектов (значения по умолчанию)
 * @param od Указатель на словарь
 * @param pdo_out Область выходных данных (RxPDO)
 * @param pdo_in Область входных данных (TxPDO)
 */
void ethercat_od_init(ethercat_od_t* od, uint8* pdo_out, uint8* pdo_in);

/**
 * @brief Обработать запрос в почтовом ящике (CoE) и сформировать ответ
 *
 * Вызывать после обработки принятого ЭКС-фрейма. Функция проверяет
 * признак "ящик заполнен" в SM0, разбирает CoE-сервис, формирует ответ
 * в области SM1 и обновляет признаки заполнения SM0/SM1.
 *
 * @param od Указатель на словарь
 * @param mem Указатель на карту памяти
 * @return true если запрос был обработан
 */
boolean ethercat_coe_process(ethercat_od_t* od, ethercat_memory_t* mem);

/**
 * @brief Применить PDO-маппинг (пересчитать размеры и указатели PDO)
 * @param od Указатель на словарь
 * @param mem Указатель на карту памяти
 */
void ethercat_od_apply_pdo_mapping(ethercat_od_t* od, ethercat_memory_t* mem);

/**
 * @brief Размер TxPDO (входы) по текущему маппингу, байт
 */
uint16 ethercat_od_get_tx_pdo_size(const ethercat_od_t* od);

/**
 * @brief Размер RxPDO (выходы) по текущему маппингу, байт
 */
uint16 ethercat_od_get_rx_pdo_size(const ethercat_od_t* od);

/**
 * @brief Число объектов в словаре
 */
uint8 ethercat_od_get_object_count(const ethercat_od_t* od);

/**
 * @brief Установить признак необходимости применения PDO-маппинга
 */
void ethercat_od_set_pdo_dirty(ethercat_od_t* od);

/**
 * @brief Проверить и при необходимости применить PDO-маппинг
 * @return true если маппинг был применён
 */
boolean ethercat_od_process_pending(ethercat_od_t* od, ethercat_memory_t* mem);

#ifdef __cplusplus
}
#endif

#endif // ETHERCAT_OD_H
