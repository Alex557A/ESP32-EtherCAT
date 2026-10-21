/*
 * EtherCAT Object Dictionary + CoE - Implementation
 *
 * Реализовано по ETG.1000.6 (SDO Services, SDO Information Service).
 * Сверено с эталонами: SOES (esc_coe.c/esc_coe.h), IgH (fsm_coe.c),
 * SOEM (ec_coe.c, ec_type.h).
 *
 * Phase 4, Week 8 (завершение протокола)
 * ESP32 EtherCAT Slave v4
 */

#include "ethercat_od.h"
#include "ethercat_esc.h"
#include <stddef.h>
#include <string.h>

// ============================================================================
// Смещения значений в области данных словаря (od->data)
// ============================================================================

#define OD_OFF_DEVICE_TYPE   0    /**< UINT32: тип устройства */
#define OD_OFF_DEV_NAME      4    /**< VISIBLE_STRING, 32 байта */
#define OD_OFF_HW_VERSION    36   /**< VISIBLE_STRING, 24 байта */
#define OD_OFF_SW_VERSION    60   /**< VISIBLE_STRING, 24 байта */
#define OD_OFF_IDENTITY      84   /**< [0] число, [4..19] 4 × UINT32 */
#define OD_OFF_SM_TYPE       104  /**< [0] число, [1..4] типы SM */
#define OD_OFF_RXPDO_ASSIGN  108  /**< [0] число, [2..3] индекс PDO */
#define OD_OFF_TXPDO_ASSIGN  112  /**< [0] число, [2..3] индекс PDO */
#define OD_OFF_RXPDO_MAP     116  /**< [0] число, [2..9] 2 × UINT32 */
#define OD_OFF_TXPDO_MAP     128  /**< [0] число, [2..9] 2 × UINT32 */
#define OD_OFF_DI_COUNT      140  /**< UINT8: число входных данных */
#define OD_OFF_DO_COUNT      144  /**< UINT8: число выходных данных */

/** Строки словаря */
#define OD_DEVNAME_STR   ECT_DEVICE_NAME
#define OD_HWVER_STR     "ESP32-S3 rev 1.0"
#define OD_SWVER_STR     "fw 1.0.0"

/** Тип устройства (профиль ETG: цифровые входы/выходы) */
#define OD_DEVICE_TYPE   0x00001389UL

// ============================================================================
// Таблицы записей словаря
// ============================================================================

/* 0x1000 Device Type */
static const ethercat_od_entry_t g_od_1000[] = {
    { 0x1000, 0, ECT_DTYPE_UNSIGNED32, 32, ECT_ATYPE_RO, "Device Type",
      ECT_OD_SRC_RAM, OD_OFF_DEVICE_TYPE, 0 }
};

/* 0x1008 Device Name */
static const ethercat_od_entry_t g_od_1008[] = {
    { 0x1008, 0, ECT_DTYPE_VISIBLE_STRING, (uint16)(8 * (sizeof(OD_DEVNAME_STR) - 1)),
      ECT_ATYPE_RO, "Device Name", ECT_OD_SRC_RAM, OD_OFF_DEV_NAME, 0 }
};

/* 0x1009 Hardware Version */
static const ethercat_od_entry_t g_od_1009[] = {
    { 0x1009, 0, ECT_DTYPE_VISIBLE_STRING, (uint16)(8 * (sizeof(OD_HWVER_STR) - 1)),
      ECT_ATYPE_RO, "Hardware Version", ECT_OD_SRC_RAM, OD_OFF_HW_VERSION, 0 }
};

/* 0x100A Software Version */
static const ethercat_od_entry_t g_od_100A[] = {
    { 0x100A, 0, ECT_DTYPE_VISIBLE_STRING, (uint16)(8 * (sizeof(OD_SWVER_STR) - 1)),
      ECT_ATYPE_RO, "Software Version", ECT_OD_SRC_RAM, OD_OFF_SW_VERSION, 0 }
};

/* 0x1018 Identity */
static const ethercat_od_entry_t g_od_1018[] = {
    { 0x1018, 0, ECT_DTYPE_UNSIGNED8,  8,  ECT_ATYPE_RO, "Subindex 0",
      ECT_OD_SRC_RAM, OD_OFF_IDENTITY + 0, 0 },
    { 0x1018, 1, ECT_DTYPE_UNSIGNED32, 32, ECT_ATYPE_RO, "Vendor ID",
      ECT_OD_SRC_RAM, OD_OFF_IDENTITY + 4, 0 },
    { 0x1018, 2, ECT_DTYPE_UNSIGNED32, 32, ECT_ATYPE_RO, "Product Code",
      ECT_OD_SRC_RAM, OD_OFF_IDENTITY + 8, 0 },
    { 0x1018, 3, ECT_DTYPE_UNSIGNED32, 32, ECT_ATYPE_RO, "Revision Number",
      ECT_OD_SRC_RAM, OD_OFF_IDENTITY + 12, 0 },
    { 0x1018, 4, ECT_DTYPE_UNSIGNED32, 32, ECT_ATYPE_RO, "Serial Number",
      ECT_OD_SRC_RAM, OD_OFF_IDENTITY + 16, 0 }
};

/* 0x1600 RxPDO mapping (DO) */
static const ethercat_od_entry_t g_od_1600[] = {
    { 0x1600, 0, ECT_DTYPE_UNSIGNED8,    8,  ECT_ATYPE_RW, "Subindex 0",
      ECT_OD_SRC_RAM, OD_OFF_RXPDO_MAP + 0, 0 },
    { 0x1600, 1, ECT_DTYPE_PDO_MAPPING, 32, ECT_ATYPE_RW, "DO 1..16",
      ECT_OD_SRC_RAM, OD_OFF_RXPDO_MAP + 1, 0 },
    { 0x1600, 2, ECT_DTYPE_PDO_MAPPING, 32, ECT_ATYPE_RW, "DO 17..32",
      ECT_OD_SRC_RAM, OD_OFF_RXPDO_MAP + 5, 0 }
};

/* 0x1A00 TxPDO mapping (DI) */
static const ethercat_od_entry_t g_od_1A00[] = {
    { 0x1A00, 0, ECT_DTYPE_UNSIGNED8,    8,  ECT_ATYPE_RW, "Subindex 0",
      ECT_OD_SRC_RAM, OD_OFF_TXPDO_MAP + 0, 0 },
    { 0x1A00, 1, ECT_DTYPE_PDO_MAPPING, 32, ECT_ATYPE_RW, "DI 1..16",
      ECT_OD_SRC_RAM, OD_OFF_TXPDO_MAP + 1, 0 },
    { 0x1A00, 2, ECT_DTYPE_PDO_MAPPING, 32, ECT_ATYPE_RW, "DI 17..32",
      ECT_OD_SRC_RAM, OD_OFF_TXPDO_MAP + 5, 0 }
};

    
/* 0x1C00 Sync manager communication type */
static const ethercat_od_entry_t g_od_1C00[] = {
    { 0x1C00, 0, ECT_DTYPE_UNSIGNED8, 8, ECT_ATYPE_RO, "Subindex 0",
      ECT_OD_SRC_RAM, OD_OFF_SM_TYPE + 0, 0 },
    { 0x1C00, 1, ECT_DTYPE_UNSIGNED8, 8, ECT_ATYPE_RO, "MbxOut",
      ECT_OD_SRC_RAM, OD_OFF_SM_TYPE + 1, 0 },
    { 0x1C00, 2, ECT_DTYPE_UNSIGNED8, 8, ECT_ATYPE_RO, "MbxIn",
      ECT_OD_SRC_RAM, OD_OFF_SM_TYPE + 2, 0 },
    { 0x1C00, 3, ECT_DTYPE_UNSIGNED8, 8, ECT_ATYPE_RO, "Outputs",
      ECT_OD_SRC_RAM, OD_OFF_SM_TYPE + 3, 0 },
    { 0x1C00, 4, ECT_DTYPE_UNSIGNED8, 8, ECT_ATYPE_RO, "Inputs",
      ECT_OD_SRC_RAM, OD_OFF_SM_TYPE + 4, 0 }
};

/* 0x1C12 RxPDO assign */
static const ethercat_od_entry_t g_od_1C12[] = {
    { 0x1C12, 0, ECT_DTYPE_UNSIGNED8,  8,  ECT_ATYPE_RW, "Subindex 0",
      ECT_OD_SRC_RAM, OD_OFF_RXPDO_ASSIGN + 0, 0 },
    { 0x1C12, 1, ECT_DTYPE_UNSIGNED16, 16, ECT_ATYPE_RW, "RxPDO 0x1600",
      ECT_OD_SRC_RAM, OD_OFF_RXPDO_ASSIGN + 1, 0 }
};

/* 0x1C13 TxPDO assign */
static const ethercat_od_entry_t g_od_1C13[] = {
    { 0x1C13, 0, ECT_DTYPE_UNSIGNED8,  8,  ECT_ATYPE_RW, "Subindex 0",
      ECT_OD_SRC_RAM, OD_OFF_TXPDO_ASSIGN + 0, 0 },
    { 0x1C13, 1, ECT_DTYPE_UNSIGNED16, 16, ECT_ATYPE_RW, "TxPDO 0x1A00",
      ECT_OD_SRC_RAM, OD_OFF_TXPDO_ASSIGN + 1, 0 }
};

/* 0x6000 Digital Inputs (TxPDO) */
static const ethercat_od_entry_t g_od_6000[] = {
    { 0x6000, 0, ECT_DTYPE_UNSIGNED8,  8,  ECT_ATYPE_RO, "Subindex 0",
      ECT_OD_SRC_RAM, OD_OFF_DI_COUNT, 0 },
    { 0x6000, 1, ECT_DTYPE_UNSIGNED16, 16,
      (uint16)(ECT_ATYPE_RO | ECT_ATYPE_TXPDO), "DI 1..16",
      ECT_OD_SRC_PDO_IN, 0, 0 },
    { 0x6000, 2, ECT_DTYPE_UNSIGNED16, 16,
      (uint16)(ECT_ATYPE_RO | ECT_ATYPE_TXPDO), "DI 17..32",
      ECT_OD_SRC_PDO_IN, 2, 0 }
};

/* 0x7010 Digital Outputs (RxPDO) */
static const ethercat_od_entry_t g_od_7010[] = {
    { 0x7010, 0, ECT_DTYPE_UNSIGNED8,  8,  ECT_ATYPE_RO, "Subindex 0",
      ECT_OD_SRC_RAM, OD_OFF_DO_COUNT, 0 },
    { 0x7010, 1, ECT_DTYPE_UNSIGNED16, 16,
      (uint16)(ECT_ATYPE_RO | ECT_ATYPE_Wop | ECT_ATYPE_RXPDO), "DO 1..16",
      ECT_OD_SRC_PDO_OUT, 0, 0 },
    { 0x7010, 2, ECT_DTYPE_UNSIGNED16, 16,
      (uint16)(ECT_ATYPE_RO | ECT_ATYPE_Wop | ECT_ATYPE_RXPDO), "DO 17..32",
      ECT_OD_SRC_PDO_OUT, 2, 0 }
};

// ============================================================================
// Таблица объектов (отсортирована по индексу)
// ============================================================================

static const ethercat_od_object_t g_od_objects[] = {
    { 0x1000, ECT_OTYPE_VAR,    0, "Device Type",           g_od_1000 },
    { 0x1008, ECT_OTYPE_VAR,    0, "Device Name",           g_od_1008 },
    { 0x1009, ECT_OTYPE_VAR,    0, "Hardware Version",      g_od_1009 },
    { 0x100A, ECT_OTYPE_VAR,    0, "Software Version",      g_od_100A },
    { 0x1018, ECT_OTYPE_RECORD, 4, "Identity",              g_od_1018 },
    { 0x1600, ECT_OTYPE_ARRAY,  2, "RxPDO mapping",         g_od_1600 },
    { 0x1A00, ECT_OTYPE_ARRAY,  2, "TxPDO mapping",         g_od_1A00 },
    { 0x1C00, ECT_OTYPE_ARRAY,  4, "SM communication type", g_od_1C00 },
    { 0x1C12, ECT_OTYPE_ARRAY,  1, "RxPDO assign",          g_od_1C12 },
    { 0x1C13, ECT_OTYPE_ARRAY,  1, "TxPDO assign",          g_od_1C13 },
    { 0x6000, ECT_OTYPE_ARRAY,  2, "Digital Inputs",        g_od_6000 },
    { 0x7010, ECT_OTYPE_ARRAY,  2, "Digital Outputs",       g_od_7010 }
};

#define OD_OBJECT_COUNT ((uint8)(sizeof(g_od_objects) / sizeof(g_od_objects[0])))

/** Служебная область в хвосте хранилища (для константных записей) */
#define OD_SCRATCH_OFF (ECT_OD_DATA_SIZE - 8)

// ============================================================================
// Поиск в словаре
// ============================================================================

static const ethercat_od_object_t* od_find_object(uint16 index) {
    uint8 i;

    for (i = 0; i < OD_OBJECT_COUNT; i++) {
        if (g_od_objects[i].index == index) {
            return &g_od_objects[i];
        }
        if (g_od_objects[i].index > index) {
            break;
        }
    }

    return NULL;
}

static const ethercat_od_entry_t* od_find_entry(uint16 index, uint8 subindex) {
    const ethercat_od_object_t* obj = od_find_object(index);
    uint8 i;

    if (obj == NULL || subindex > obj->maxsub) {
        return NULL;
    }

    for (i = 0; i <= obj->maxsub; i++) {
        if (obj->entries[i].subindex == subindex) {
            return &obj->entries[i];
        }
    }

    return NULL;
}

/** Размер данных записи в байтах */
static uint16 od_entry_size(const ethercat_od_entry_t* e) {
    uint16 size = (uint16)((e->bitlength + 7) / 8);

    if (size == 0) {
        size = 1;
    }

    return size;
}

/** Указатель на данные записи словаря */
static uint8* od_entry_ptr(ethercat_od_t* od, const ethercat_od_entry_t* e) {
    if (od == NULL || e == NULL) {
        return NULL;
    }

    switch (e->src) {
        case ECT_OD_SRC_PDO_IN:
            if (od->pdo_in == NULL) {
                return NULL;
            }
            return &od->pdo_in[e->off];

        case ECT_OD_SRC_PDO_OUT:
            if (od->pdo_out == NULL) {
                return NULL;
            }
            return &od->pdo_out[e->off];

        case ECT_OD_SRC_CONST:
        {
            /* Копия константы в служебную область (только для чтения) */
            uint8* p = &od->data[OD_SCRATCH_OFF];
            p[0] = (uint8)(e->value & 0xFF);
            p[1] = (uint8)((e->value >> 8) & 0xFF);
            p[2] = (uint8)((e->value >> 16) & 0xFF);
            p[3] = (uint8)((e->value >> 24) & 0xFF);
            return p;
        }

        default:
            if ((uint32)e->off + 8 > ECT_OD_DATA_SIZE) {
                return NULL;
            }
            return &od->data[e->off];
    }
}

// ============================================================================
// Доступ к значениям (little-endian)
// ============================================================================

static void od_store16(uint8* base, uint16 off, uint16 v) {
    base[off] = (uint8)(v & 0xFF);
    base[off + 1] = (uint8)((v >> 8) & 0xFF);
}

static void od_store32(uint8* base, uint16 off, uint32 v) {
    base[off] = (uint8)(v & 0xFF);
    base[off + 1] = (uint8)((v >> 8) & 0xFF);
    base[off + 2] = (uint8)((v >> 16) & 0xFF);
    base[off + 3] = (uint8)((v >> 24) & 0xFF);
}

static uint16 od_load16(const uint8* base, uint16 off) {
    return (uint16)(base[off] | (base[off + 1] << 8));
}

static uint32 od_load32(const uint8* base, uint16 off) {
    return (uint32)(base[off] | (base[off + 1] << 8) |
                    (base[off + 2] << 16) | ((uint32)base[off + 3] << 24));
}

// ============================================================================
// Инициализация словаря
// ============================================================================

void ethercat_od_init(ethercat_od_t* od, uint8* pdo_out, uint8* pdo_in) {
    if (od == NULL) {
        return;
    }

    memset(od, 0, sizeof(ethercat_od_t));

    od->pdo_out = pdo_out;
    od->pdo_in = pdo_in;
    od->object_count = OD_OBJECT_COUNT;
    od->pdo_out_size = 4;  /* 2 × UINT16 по умолчанию */
    od->pdo_in_size = 4;

    // --- Строки --------------------------------------------------------------
    od_store32(od->data, OD_OFF_DEVICE_TYPE, OD_DEVICE_TYPE);
    strncpy((char*)&od->data[OD_OFF_DEV_NAME], OD_DEVNAME_STR, 31);
    strncpy((char*)&od->data[OD_OFF_HW_VERSION], OD_HWVER_STR, 23);
    strncpy((char*)&od->data[OD_OFF_SW_VERSION], OD_SWVER_STR, 23);

    // --- 0x1018 Identity -----------------------------------------------------
    od->data[OD_OFF_IDENTITY + 0] = 4;  /* число подиндексов */
    od_store32(od->data, OD_OFF_IDENTITY + 4, (uint32)ECT_VENDOR_ID);
    od_store32(od->data, OD_OFF_IDENTITY + 8, (uint32)ECT_PRODUCT_CODE);
    od_store32(od->data, OD_OFF_IDENTITY + 12, (uint32)ECT_REVISION_NUMBER);
    od_store32(od->data, OD_OFF_IDENTITY + 16, (uint32)ECT_SERIAL_NUMBER);

    // --- 0x1C00 типы SyncManager --------------------------------------------
    od->data[OD_OFF_SM_TYPE + 0] = 4;
    od->data[OD_OFF_SM_TYPE + 1] = ECT_SII_SMTYPE_MBXOUT;
    od->data[OD_OFF_SM_TYPE + 2] = ECT_SII_SMTYPE_MBXIN;
    od->data[OD_OFF_SM_TYPE + 3] = ECT_SII_SMTYPE_OUTPUT;
    od->data[OD_OFF_SM_TYPE + 4] = ECT_SII_SMTYPE_INPUT;

    // --- 0x1C12 / 0x1C13 назначение PDO -------------------------------------
    od->data[OD_OFF_RXPDO_ASSIGN + 0] = 1;
    od_store16(od->data, OD_OFF_RXPDO_ASSIGN + 1, 0x1600);
    od->data[OD_OFF_TXPDO_ASSIGN + 0] = 1;
    od_store16(od->data, OD_OFF_TXPDO_ASSIGN + 1, 0x1A00);

    // --- 0x1600 / 0x1A00 маппинг (index << 16 | sub << 8 | бит) -------------
    od->data[OD_OFF_RXPDO_MAP + 0] = 2;
    od_store32(od->data, OD_OFF_RXPDO_MAP + 1, 0x70100110UL);
    od_store32(od->data, OD_OFF_RXPDO_MAP + 5, 0x70100210UL);
    od->data[OD_OFF_TXPDO_MAP + 0] = 2;
    od_store32(od->data, OD_OFF_TXPDO_MAP + 1, 0x60000110UL);
    od_store32(od->data, OD_OFF_TXPDO_MAP + 5, 0x60000210UL);

    // --- 0x6000 / 0x7010 число записей --------------------------------------
    od->data[OD_OFF_DI_COUNT] = 2;
    od->data[OD_OFF_DO_COUNT] = 2;

    od->upload.active = 0;
    od->download.active = 0;
    od->pdo_dirty = false;
    od->initialized = true;
}

// ============================================================================
// PDO-маппинг
// ============================================================================

/** Размер PDO (в байтах) по массиву маппинга 0x1600 / 0x1A00 */
static uint16 od_pdo_size(const ethercat_od_t* od, uint16 map_index) {
    const ethercat_od_object_t* obj = od_find_object(map_index);
    uint32 bits = 0;
    uint8 count;
    uint8 i;

    if (obj == NULL) {
        return 0;
    }

    count = od->data[obj->entries[0].off];
    if (count > obj->maxsub) {
        count = obj->maxsub;
    }

    for (i = 1; i <= count; i++) {
        uint32 map = od_load32(od->data, obj->entries[i].off);
        bits += (map & 0xFF);
    }

    return (uint16)((bits + 7) / 8);
}

void ethercat_od_apply_pdo_mapping(ethercat_od_t* od, ethercat_memory_t* mem) {
    uint8* raw;

    if (od == NULL) {
        return;
    }

    if (mem != NULL) {
        uint16 addr_out = ethercat_esc_get_pdo_address(mem, true);
        uint16 addr_in = ethercat_esc_get_pdo_address(mem, false);

        raw = ethercat_memory_get_raw(mem);

        if (raw != NULL && addr_out != 0) {
            od->pdo_out = &raw[addr_out];
        }
        if (raw != NULL && addr_in != 0) {
            od->pdo_in = &raw[addr_in];
        }
    }

    od->pdo_out_size = od_pdo_size(od, 0x1600);
    od->pdo_in_size = od_pdo_size(od, 0x1A00);
    od->pdo_dirty = false;
}

uint16 ethercat_od_get_tx_pdo_size(const ethercat_od_t* od) {
    return (od == NULL) ? 0 : od->pdo_in_size;
}

uint16 ethercat_od_get_rx_pdo_size(const ethercat_od_t* od) {
    return (od == NULL) ? 0 : od->pdo_out_size;
}

uint8 ethercat_od_get_object_count(const ethercat_od_t* od) {
    return (od == NULL) ? 0 : od->object_count;
}

void ethercat_od_set_pdo_dirty(ethercat_od_t* od) {
    if (od != NULL) {
        od->pdo_dirty = true;
    }
}

boolean ethercat_od_process_pending(ethercat_od_t* od, ethercat_memory_t* mem) {
    if (od == NULL || od->pdo_dirty == false) {
        return false;
    }

    ethercat_od_apply_pdo_mapping(od, mem);
    return true;
}

// ============================================================================
// Формирование ответа почтового ящика
// ============================================================================

typedef struct {
    uint8* buf;        /**< Начало ответа (заголовок ящика) */
    uint16 cap;        /**< Ёмкость: размер сервисных данных (байт) */
    uint16 len;        /**< Текущая длина сервисных данных */
    boolean overflow;  /**< Переполнение */
} coe_resp_t;

/** Начать ответ: заголовок CoE с указанным сервисом */
static void resp_begin(coe_resp_t* r, uint8* buf, uint16 capacity, uint16 service) {
    r->buf = buf;
    r->cap = capacity;
    r->len = ECT_MBX_COE_HEADER_SIZE;
    r->overflow = (buf == NULL) ? true : false;

    if (buf == NULL) {
        return;
    }

    /* CoE header: биты 15-12 = сервис, биты 11-0 = номер (little-endian) */
    buf[6] = 0x00;
    buf[7] = (uint8)((service << 4) & 0xF0);
}

/** Записать байт в ответ */
static void resp_u8(coe_resp_t* r, uint8 v) {
    if (r->overflow || (uint32)(r->len + 1) > r->cap) {
        r->overflow = true;
        return;
    }
    r->buf[6 + r->len] = v;
    r->len++;
}

/** Записать 16-битное значение (little-endian) */
static void resp_u16(coe_resp_t* r, uint16 v) {
    resp_u8(r, (uint8)(v & 0xFF));
    resp_u8(r, (uint8)((v >> 8) & 0xFF));
}

/** Записать 32-битное значение (little-endian) */
static void resp_u32(coe_resp_t* r, uint32 v) {
    resp_u8(r, (uint8)(v & 0xFF));
    resp_u8(r, (uint8)((v >> 8) & 0xFF));
    resp_u8(r, (uint8)((v >> 16) & 0xFF));
    resp_u8(r, (uint8)((v >> 24) & 0xFF));
}

/** Записать блок байт */
static void resp_bytes(coe_resp_t* r, const uint8* p, uint16 n) {
    uint16 i;

    if (p == NULL) {
        return;
    }

    for (i = 0; i < n; i++) {
        resp_u8(r, p[i]);
    }
}

/** Записать строку с завершающим нулём */
static void resp_string(coe_resp_t* r, const char* s) {
    if (s == NULL) {
        resp_u8(r, 0x00);
        return;
    }

    while (*s) {
        resp_u8(r, (uint8)(*s++));
    }
    resp_u8(r, 0x00);
}

/** Закончить ответ: заполнить заголовок ящика */
static void resp_finish(coe_resp_t* r, uint8 counter) {
    if (r->buf == NULL) {
        return;
    }

    r->buf[0] = (uint8)(r->len & 0xFF);
    r->buf[1] = (uint8)((r->len >> 8) & 0xFF);
    r->buf[2] = 0x00;  /* адрес */
    r->buf[3] = 0x00;
    r->buf[4] = 0x00;  /* канал/приоритет */
    /* Тип сервиса — младший полубайт, счётчик — старший (как в SOEM, SOES, IgH) */
    r->buf[5] = (uint8)((ECT_MBX_TYPE_COE & 0x0F) | (uint8)((counter & 0x0F) << 4));
}

/** Ответ-прерывание SDO (Abort) */
static void resp_abort(coe_resp_t* r, uint16 index, uint8 subindex, uint32 code) {
    resp_begin(r, r->buf, r->cap, ECT_COE_SDO_RESPONSE);
    resp_u8(r, ECT_SDO_ABORT);
    resp_u16(r, index);
    resp_u8(r, subindex);
    resp_u32(r, code);
}

// ============================================================================
// Права доступа
// ============================================================================

/** Разрешена ли запись в текущем состоянии AL */
static boolean od_write_allowed(const ethercat_od_entry_t* e, uint8 state) {
    if (e == NULL || state == EC_STATE_INIT) {
        return false;
    }

    if (state == EC_STATE_PRE_OP) {
        return ((e->flags & ECT_ATYPE_Wpre) != 0) ? true : false;
    }
    if (state == EC_STATE_SAFE_OP) {
        return ((e->flags & (ECT_ATYPE_Wpre | ECT_ATYPE_Wsafe)) != 0) ? true : false;
    }

    return ((e->flags & (ECT_ATYPE_Wpre | ECT_ATYPE_Wsafe | ECT_ATYPE_Wop)) != 0) ? true : false;
}

// ============================================================================
// Complete Access: проверка непрерывности данных объекта
// ============================================================================

/** Данные всех подиндексов объекта лежат непрерывно */
static boolean od_is_contiguous(ethercat_od_t* od, const ethercat_od_object_t* obj) {
    uint8* base = od_entry_ptr(od, &obj->entries[0]);
    uint16 off = 0;
    uint8 i;

    if (base == NULL) {
        return false;
    }

    for (i = 0; i <= obj->maxsub; i++) {
        if (od_entry_ptr(od, &obj->entries[i]) != (base + off)) {
            return false;
        }
        off = (uint16)(off + od_entry_size(&obj->entries[i]));
    }

    return true;
}

/** Суммарный размер данных объекта (complete access) */
static uint32 od_complete_size(ethercat_od_t* od, const ethercat_od_object_t* obj) {
    uint32 total = 0;
    uint8 i;

    if (od == NULL || obj == NULL) {
        return 0;
    }

    for (i = 0; i <= obj->maxsub; i++) {
        total += od_entry_size(&obj->entries[i]);
    }

    return total;
}

/** Все подиндексы объекта доступны для записи в текущем состоянии */
static boolean od_complete_writable(const ethercat_od_object_t* obj, uint8 state) {
    uint8 i;

    for (i = 0; i <= obj->maxsub; i++) {
        if (od_write_allowed(&obj->entries[i], state) == false) {
            return false;
        }
    }

    return true;
}

// ============================================================================
// SDO Upload
// ============================================================================

/** Начать сегментированную выдачу данных */
static void upload_start(ethercat_od_t* od, uint8* ptr, uint32 size) {
    od->upload.active = 1;
    od->upload.offset = 0;
    od->upload.size = size;
    od->upload.ptr = ptr;
}

/** Ответ на initiate upload request (сервис SDO) */
static void sdo_upload(ethercat_od_t* od, coe_resp_t* r,
                       uint16 index, uint8 subindex, boolean complete) {
    const ethercat_od_object_t* obj = od_find_object(index);
    const ethercat_od_entry_t* e = NULL;
    uint8* ptr = NULL;
    uint32 size = 0;

    resp_begin(r, r->buf, r->cap, ECT_COE_SDO_RESPONSE);

    if (obj == NULL) {
        od->sdo_aborts++;
        resp_abort(r, index, subindex, ECT_ABORT_NO_OBJECT);
        return;
    }

    if (complete == true) {
        if (obj->objtype != ECT_OTYPE_ARRAY && obj->objtype != ECT_OTYPE_RECORD) {
            od->sdo_aborts++;
            resp_abort(r, index, subindex, ECT_ABORT_UNSUPPORTED);
            return;
        }
        if (od_is_contiguous(od, obj) == false) {
            od->sdo_aborts++;
            resp_abort(r, index, subindex, ECT_ABORT_UNSUPPORTED);
            return;
        }
        size = od_complete_size(od, obj);
        ptr = od_entry_ptr(od, &obj->entries[0]);
    } else {
        e = od_find_entry(index, subindex);
        if (e == NULL) {
            od->sdo_aborts++;
            resp_abort(r, index, subindex, ECT_ABORT_SUBINDEX_NOT_EXIST);
            return;
        }
        size = od_entry_size(e);
        ptr = od_entry_ptr(od, e);
    }

    if (ptr == NULL) {
        od->sdo_aborts++;
        resp_abort(r, index, subindex, ECT_ABORT_UNSUPPORTED);
        return;
    }

    od->sdo_uploads++;

    if (size >= 1 && size <= 4) {
        /* Expedited: данные передаются сразу */
        uint8 n = (uint8)(4 - size);
        uint8 i;

        resp_u8(r, (uint8)(ECT_SDO_UPLOAD_RESPONSE | ECT_SDO_SIZE_INDICATOR |
                           ECT_SDO_EXPEDITED | (uint16)(n << 2) |
                           (complete ? ECT_SDO_COMPLETE_ACCESS : 0)));
        resp_u16(r, index);
        resp_u8(r, complete ? 0x00 : subindex);

        for (i = 0; i < size; i++) {
            resp_u8(r, ptr[i]);
        }
        for (i = 0; i < n; i++) {
            resp_u8(r, 0x00);
        }
        return;
    }

    /* Обычная передача: сообщаем размер, далее — сегменты */
    resp_u8(r, (uint8)(ECT_SDO_UPLOAD_RESPONSE | ECT_SDO_SIZE_INDICATOR |
                       (complete ? ECT_SDO_COMPLETE_ACCESS : 0)));
    resp_u16(r, index);
    resp_u8(r, complete ? 0x00 : subindex);
    resp_u32(r, size);

    if (size > 0) {
        upload_start(od, ptr, size);
    }
}

/** Ответ на upload segment request */
static void sdo_upload_segment(ethercat_od_t* od, coe_resp_t* r, uint8 cmd) {
    uint8 toggle = (uint8)((cmd & ECT_SDO_TOGGLE) ? 1 : 0);
    uint32 remaining;
    uint8 data_len;
    uint8 n;
    uint8 i;

    resp_begin(r, r->buf, r->cap, ECT_COE_SDO_RESPONSE);

    if (od->upload.active == 0 || od->upload.ptr == NULL ||
        od->upload.offset > od->upload.size) {
        od->upload.active = 0;
        od->sdo_aborts++;
        resp_abort(r, 0, 0, ECT_ABORT_UNSUPPORTED);
        return;
    }

    remaining = od->upload.size - od->upload.offset;
    data_len = (remaining > 7) ? 7 : (uint8)remaining;
    n = (uint8)(7 - data_len);

    resp_u8(r, (uint8)((toggle ? ECT_SDO_TOGGLE : 0) | (uint16)(n << 1) |
                       ((remaining <= 7) ? ECT_SDO_LAST_SEGMENT : 0)));

    for (i = 0; i < data_len; i++) {
        resp_u8(r, od->upload.ptr[od->upload.offset + i]);
    }

    od->upload.offset += data_len;

    if (remaining <= 7) {
        od->upload.active = 0;  /* передача завершена */
    }
}

// ============================================================================
// SDO Download
// ============================================================================

/** Побочные действия после успешной записи объекта */
static void od_after_download(ethercat_od_t* od, uint16 index) {
    /* Маппинг/назначение PDO требует пересчёта */
    if (index == 0x1600 || index == 0x1A00 || index == 0x1C12 || index == 0x1C13) {
        od->pdo_dirty = true;
    }
}

/** Ответ-подтверждение выгрузки */
static void resp_download_ok(coe_resp_t* r, uint16 index, uint8 subindex) {
    resp_u8(r, ECT_SDO_DOWNLOAD_RESPONSE);
    resp_u16(r, index);
    resp_u8(r, subindex);
    resp_u32(r, 0x00000000UL);
}

/** Обработка initiate download request */
static void sdo_download_init(ethercat_od_t* od, ethercat_memory_t* mem, coe_resp_t* r,
                              uint8 cmd, uint16 index, uint8 subindex, const uint8* d) {
    const ethercat_od_object_t* obj = od_find_object(index);
    const ethercat_od_entry_t* e = NULL;
    uint8* ptr = NULL;
    uint32 entry_size = 0;
    uint32 size;
    boolean expedited = ((cmd & ECT_SDO_EXPEDITED) != 0) ? true : false;
    boolean size_given = ((cmd & ECT_SDO_SIZE_INDICATOR) != 0) ? true : false;
    boolean complete = ((cmd & ECT_SDO_COMPLETE_ACCESS) != 0) ? true : false;
    uint8 state = (uint8)(ethercat_memory_read16(mem, ECT_REG_ALSTAT) & 0x0F);

    resp_begin(r, r->buf, r->cap, ECT_COE_SDO_RESPONSE);

    if (obj == NULL) {
        od->sdo_aborts++;
        resp_abort(r, index, subindex, ECT_ABORT_NO_OBJECT);
        return;
    }

    if (expedited == true) {
        size = (uint32)(4 - ((cmd & ECT_SDO_INVALID_SIZE_MASK) >> 2));
    } else if (size_given == true) {
        size = (uint32)(d[0] | (d[1] << 8) | (d[2] << 16) | ((uint32)d[3] << 24));
    } else {
        od->sdo_aborts++;
        resp_abort(r, index, subindex, ECT_ABORT_UNSUPPORTED);
        return;
    }

    if (complete == true) {
        if (obj->objtype != ECT_OTYPE_ARRAY) {
            od->sdo_aborts++;
            resp_abort(r, index, subindex, ECT_ABORT_UNSUPPORTED);
            return;
        }
        if (od_is_contiguous(od, obj) == false ||
            od_complete_writable(obj, state) == false) {
            od->sdo_aborts++;
            resp_abort(r, index, subindex, ECT_ABORT_UNSUPPORTED_ACCESS);
            return;
        }
        entry_size = od_complete_size(od, obj);
        ptr = od_entry_ptr(od, &obj->entries[0]);
        subindex = 0x00;
    } else {
        e = od_find_entry(index, subindex);
        if (e == NULL) {
            od->sdo_aborts++;
            resp_abort(r, index, subindex, ECT_ABORT_SUBINDEX_NOT_EXIST);
            return;
        }
        if (od_write_allowed(e, state) == false) {
            od->sdo_aborts++;
            resp_abort(r, index, subindex, ECT_ABORT_READ_ONLY);
            return;
        }
        entry_size = od_entry_size(e);
        ptr = od_entry_ptr(od, e);
    }

    if (ptr == NULL) {
        od->sdo_aborts++;
        resp_abort(r, index, subindex, ECT_ABORT_UNSUPPORTED);
        return;
    }

    if (size > entry_size) {
        od->sdo_aborts++;
        resp_abort(r, index, subindex, ECT_ABORT_INVALID_SIZE);
        return;
    }

    if (size == 0) {
        resp_download_ok(r, index, subindex);
        return;
    }

    if (expedited == true) {
        /* Данные пришли сразу */
        uint32 i;

        for (i = 0; i < size; i++) {
            ptr[i] = d[i];
        }

        od->sdo_downloads++;
        od_after_download(od, index);
        resp_download_ok(r, index, subindex);
        return;
    }

    /* Обычная передача: ждём сегменты */
    od->download.active = 1;
    od->download.offset = 0;
    od->download.size = size;
    od->download.ptr = ptr;
    od->download.index = index;
    od->download.toggle = 0;

    resp_download_ok(r, index, subindex);
}

/** Обработка download segment request */
static void sdo_download_segment(ethercat_od_t* od, coe_resp_t* r, uint8 cmd,
                                 const uint8* d, uint16 len) {
    uint8 toggle = (uint8)((cmd & ECT_SDO_TOGGLE) ? 1 : 0);
    uint8 n = (uint8)((cmd >> 1) & 0x07);
    boolean last = ((cmd & ECT_SDO_LAST_SEGMENT) != 0) ? true : false;
    uint8 count;
    uint8 i;

    resp_begin(r, r->buf, r->cap, ECT_COE_SDO_RESPONSE);

    if (od->download.active == 0 || od->download.ptr == NULL) {
        od->sdo_aborts++;
        resp_abort(r, 0, 0, ECT_ABORT_UNSUPPORTED);
        return;
    }

    count = (uint8)(7 - n);
    if (count > len) {
        count = (uint8)len;
    }
    if ((uint32)(od->download.offset + count) > od->download.size) {
        count = (uint8)(od->download.size - od->download.offset);
    }

    for (i = 0; i < count; i++) {
        od->download.ptr[od->download.offset + i] = d[i];
    }
    od->download.offset += count;

    if (last == true) {
        od->sdo_downloads++;
        od_after_download(od, od->download.index);
        od->download.active = 0;
    }

    resp_u8(r, (uint8)(ECT_SDO_DOWNLOAD_SEG_RESPONSE | (toggle ? ECT_SDO_TOGGLE : 0)));
    resp_u16(r, od->download.index);
    resp_u8(r, od->download.active ? 0x00 : 0xFF);
    resp_u32(r, 0x00000000UL);
}

// ============================================================================
// Сервис SDO Information
// ============================================================================

/** Заголовок информационного ответа (4 байта) */
static void resp_info_begin(coe_resp_t* r, uint8 opcode) {
    resp_begin(r, r->buf, r->cap, ECT_COE_SDO_INFO);
    resp_u8(r, (uint8)(opcode & 0x7F));  /* opcode, incomplete = 0 */
    resp_u8(r, 0x00);                    /* reserved */
    resp_u16(r, 0x0000);                 /* fragments left */
}

/** Ошибка сервиса SDO Information */
static void resp_info_error(coe_resp_t* r, uint32 code) {
    resp_info_begin(r, ECT_SDO_INFO_ERROR);
    resp_u32(r, code);
}

/** Get OD List response */
static void sdo_info_od_list(coe_resp_t* r, uint16 list_type) {
    uint8 count = OD_OBJECT_COUNT;
    uint8 i;

    resp_info_begin(r, ECT_SDO_INFO_OD_LIST_RES);

    if (list_type == 0x0000) {
        /* Число объектов */
        resp_u16(r, 0x0000);
        resp_u16(r, count);
        resp_u16(r, 0x0000);
        resp_u16(r, 0x0000);
        return;
    }

    /* Полный список индексов (помещается в один фрагмент) */
    resp_u16(r, 0x0001);
    for (i = 0; i < count; i++) {
        resp_u16(r, g_od_objects[i].index);
    }
}

/** Get Object Description response */
static void sdo_info_obj_desc(coe_resp_t* r, uint16 index) {
    const ethercat_od_object_t* obj = od_find_object(index);

    if (obj == NULL) {
        resp_info_error(r, ECT_ABORT_NO_OBJECT);
        return;
    }

    resp_info_begin(r, ECT_SDO_INFO_OBJ_DESC_RES);
    resp_u16(r, index);
    resp_u16(r, obj->entries[0].datatype);
    resp_u8(r, obj->maxsub);
    resp_u8(r, (uint8)(obj->objtype & 0xFF));
    resp_string(r, obj->name);
}

/** Get Entry Description response */
static void sdo_info_entry_desc(coe_resp_t* r, uint16 index, uint8 subindex) {
    const ethercat_od_entry_t* e = od_find_entry(index, subindex);

    if (e == NULL) {
        resp_info_error(r, ECT_ABORT_SUBINDEX_NOT_EXIST);
        return;
    }

    resp_info_begin(r, ECT_SDO_INFO_ENTRY_DESC_RES);
    resp_u16(r, index);
    resp_u8(r, subindex);
    resp_u8(r, (uint8)(ECT_SDO_VALUEINFO_ACCESS | ECT_SDO_VALUEINFO_OBJECT |
                      ECT_SDO_VALUEINFO_MAPPABLE));
    resp_u16(r, e->datatype);
    resp_u16(r, e->bitlength);
    resp_u16(r, e->flags);
    resp_string(r, e->name);
}

// ============================================================================
// Обработка почтового ящика (CoE)
// ============================================================================

boolean ethercat_coe_process(ethercat_od_t* od, ethercat_memory_t* mem) {
    static uint8 mbx_counter = 0;

    uint8* raw;
    uint8* mbx_out;
    uint8* mbx_in;
    uint16 sm0_addr;
    uint16 sm0_len;
    uint16 sm1_addr;
    uint16 sm1_len;
    uint16 req_len;
    uint16 service;
    uint8 service_type;
    uint8 cmd;
    uint16 index;
    uint8 subindex;
    coe_resp_t r;

    if (od == NULL || mem == NULL || od->initialized == false) {
        return false;
    }

    /* Признак заполнения SM0 (master → slave) */
    if ((ethercat_memory_read8(mem, ECT_SM0STAT) & ECT_SMSTAT_MAILBOX) == 0) {
        return false;
    }

    raw = ethercat_memory_get_raw(mem);
    if (raw == NULL) {
        return false;
    }

    sm0_addr = ethercat_memory_read16(mem, ECT_REG_SM0);
    sm0_len = ethercat_memory_read16(mem, (uint16)(ECT_REG_SM0 + 2));
    sm1_addr = ethercat_memory_read16(mem, ECT_REG_SM1);
    sm1_len = ethercat_memory_read16(mem, (uint16)(ECT_REG_SM1 + 2));

    if (sm0_len < ECT_MBX_MIN_LENGTH ||
        ((uint32)sm0_addr + sm0_len) > ETHERCAT_MEMORY_SIZE) {
        sm0_addr = MAILBOX_OUT_ADDR;
        sm0_len = ECT_MBX_SIZE;
    }
    if (sm1_len < ECT_MBX_MIN_LENGTH ||
        ((uint32)sm1_addr + sm1_len) > ETHERCAT_MEMORY_SIZE) {
        sm1_addr = MAILBOX_IN_ADDR;
        sm1_len = ECT_MBX_SIZE;
    }

    mbx_out = &raw[sm0_addr];
    mbx_in = &raw[sm1_addr];

    r.buf = mbx_in;
    r.cap = (uint16)(sm1_len - ECT_MBX_HEADER_SIZE);
    r.len = 0;
    r.overflow = false;

    service_type = (uint8)(mbx_out[5] & 0x0F);
    req_len = (uint16)(mbx_out[0] | (mbx_out[1] << 8));

    if (service_type != ECT_MBX_TYPE_COE) {
        /* Неподдерживаемый протокол: ответ ошибки почтового ящика */
        mbx_in[0] = ECT_MBX_HEADER_SIZE;
        mbx_in[1] = 0x00;
        mbx_in[2] = 0x00;
        mbx_in[3] = 0x00;
        mbx_in[4] = 0x01;                                    /* канал/приоритет */
        mbx_in[5] = (uint8)(ECT_MBX_TYPE_ERR & 0x0F);        /* тип = ошибка */
        mbx_in[6] = 0x01;                                    /* Type: SYNTAX */
        mbx_in[7] = 0x00;
        mbx_in[8] = 0x02;   /* Detail: UNSUPPORTEDPROTOCOL */
        mbx_in[9] = 0x00;
        od->mbx_errors++;
    } else if (req_len < 3) {
        /* Сервисные данные короче заголовка CoE + команды:
         * сегментные запросы допускают минимум 3 байта (CoE + команда) */
        od->mbx_errors++;
    } else {
        service = (uint16)((mbx_out[7] >> 4) & 0x0F);

        if (service == ECT_COE_SDO_REQUEST) {
            cmd = mbx_out[8];
            index = (uint16)(mbx_out[9] | (mbx_out[10] << 8));
            subindex = mbx_out[11];

            switch (cmd & ECT_SDO_CMD_MASK) {
                case ECT_SDO_UPLOAD_REQUEST:
                    sdo_upload(od, &r, index, subindex,
                               ((cmd & ECT_SDO_COMPLETE_ACCESS) != 0) ? true : false);
                    break;

                case ECT_SDO_UPLOAD_SEG_REQUEST:
                    sdo_upload_segment(od, &r, cmd);
                    break;

                case ECT_SDO_DOWNLOAD_REQUEST:
                    sdo_download_init(od, mem, &r, cmd, index, subindex, &mbx_out[12]);
                    break;

                case ECT_SDO_DOWNLOAD_SEG_REQUEST:
                {
                    uint16 data_len = (req_len > 3) ? (uint16)(req_len - 3) : 0;
                    sdo_download_segment(od, &r, cmd, &mbx_out[9], data_len);
                    break;
                }

                default:
                    /* Abort от мастера либо неизвестная команда */
                    od->upload.active = 0;
                    od->download.active = 0;
                    od->sdo_aborts++;
                    resp_abort(&r, index, subindex, ECT_ABORT_UNSUPPORTED);
                    break;
            }
        } else if (service == ECT_COE_SDO_INFO) {
            uint8 opcode = (uint8)(mbx_out[8] & 0x7F);

            if (opcode == ECT_SDO_INFO_OD_LIST_REQ) {
                uint16 list_type = (uint16)(mbx_out[12] | (mbx_out[13] << 8));
                sdo_info_od_list(&r, list_type);
            } else if (opcode == ECT_SDO_INFO_OBJ_DESC_REQ) {
                uint16 obj_index = (uint16)(mbx_out[12] | (mbx_out[13] << 8));
                sdo_info_obj_desc(&r, obj_index);
            } else if (opcode == ECT_SDO_INFO_ENTRY_DESC_REQ) {
                uint16 obj_index = (uint16)(mbx_out[12] | (mbx_out[13] << 8));
                sdo_info_entry_desc(&r, obj_index, mbx_out[14]);
            } else {
                resp_info_error(&r, ECT_ABORT_UNSUPPORTED);
            }
        } else {
            resp_abort(&r, 0, 0, ECT_ABORT_UNSUPPORTED);
            od->mbx_errors++;
        }
    }

    if (r.overflow == true) {
        od->mbx_errors++;
    }

    /* Завершить ответ: заголовок ящика + счётчик */
    resp_finish(&r, mbx_counter++);

    /* Снять признак заполнения SM0, взвести признак готовности SM1 */
    {
        uint8 stat0 = ethercat_memory_read8(mem, ECT_SM0STAT);
        uint8 stat1 = ethercat_memory_read8(mem, ECT_SM1STAT);

        ethercat_memory_write8(mem, ECT_SM0STAT, (uint8)(stat0 & ~ECT_SMSTAT_MAILBOX));
        ethercat_memory_write8(mem, ECT_SM1STAT, (uint8)(stat1 | ECT_SMSTAT_MAILBOX));
    }

    od->mbx_processed++;
    return true;
}
