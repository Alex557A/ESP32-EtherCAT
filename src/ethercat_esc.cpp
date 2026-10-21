/*
 * EtherCAT ESC Register Emulation - Implementation
 *
 * Эмуляция регистров ESC: идентификация, DL/AL, SyncManager, FMMU,
 * интерфейс EEPROM (SII), Distributed Clock.
 *
 * Протоколы сверены (не скопированы) с открытыми эталонами:
 *   - SOEM        : src/ec_type.h, src/ec_main.c, src/ec_config.c
 *   - IgH Master  : master/fsm_sii.c, master/fsm_slave_scan.c, master/fsm_coe.c
 *   - SOES        : soes/esc.h, soes/esc_coe.c, soes/esc_coe.h
 *
 * Phase 4, Week 8 (завершение протокола)
 * ESP32 EtherCAT Slave v4
 */

#include "ethercat_esc.h"
#include <stddef.h>
#include <string.h>

// ============================================================================
// Таблица строк SII
// ============================================================================

#define SII_STR_GROUP   0  /**< "ESP32 EtherCAT" */
#define SII_STR_NAME    1  /**< "ESP32 EtherCAT Slave v4" */
#define SII_STR_ORDER   2  /**< "ESP32-ECAT-SLAVE-4" */

#define SII_STR_TXPDO   3  /**< "TxPDO 0x1A00" */
#define SII_STR_RXPDO   4  /**< "RxPDO 0x1600" */
#define SII_STR_DI1     5  /**< "DI 1..16" */
#define SII_STR_DI2     6  /**< "DI 17..32" */
#define SII_STR_DO1     7  /**< "DO 1..16" */
#define SII_STR_DO2     8  /**< "DO 17..32" */

static const char* const g_sii_strings[] = {
    ECT_DEVICE_GROUP,       /* 0 */
    ECT_DEVICE_NAME,        /* 1 */
    ECT_DEVICE_ORDER,       /* 2 */
    "TxPDO 0x1A00",         /* 3 */
    "RxPDO 0x1600",         /* 4 */
    "DI 1..16",             /* 5 */
    "DI 17..32",            /* 6 */
    "DO 1..16",             /* 7 */
    "DO 17..32"             /* 8 */
};

#define SII_STR_COUNT ((uint8)(sizeof(g_sii_strings) / sizeof(g_sii_strings[0])))

// ============================================================================
// Построение образа SII — служебные функции
// ============================================================================

/** Записать слово SII с проверкой границ */
static void sii_set(ethercat_esc_t* esc, uint16 idx, uint16 value) {
    if (esc == NULL || idx >= ECT_SII_MAX_WORDS) {
        return;
    }
    esc->sii[idx] = value;
}

/** Записать байт образа SII (младший/старший) */
static void sii_set_byte(ethercat_esc_t* esc, uint16 byte_idx, uint8 value) {
    uint16 word_idx = (uint16)(byte_idx / 2);

    if (esc == NULL || word_idx >= ECT_SII_MAX_WORDS) {
        return;
    }

    if ((byte_idx & 1) == 0) {
        esc->sii[word_idx] = (uint16)((esc->sii[word_idx] & 0xFF00) | value);
    } else {
        esc->sii[word_idx] = (uint16)((esc->sii[word_idx] & 0x00FF) | (value << 8));
    }
}

/** Категория SII: записать заголовок (тип + место под размер) */
static uint16 sii_cat_begin(ethercat_esc_t* esc, uint16 word_idx, uint16 type) {
    sii_set(esc, word_idx, (uint16)(type & 0x7FFF));
    sii_set(esc, (uint16)(word_idx + 1), 0);  /* размер дозаполняется */
    return (uint16)(word_idx + 2);
}

/** Категория SII: записать размер (в словах, включая 2 слова заголовка) */
static void sii_cat_end(ethercat_esc_t* esc, uint16 word_idx, uint16 word_end) {
    sii_set(esc, (uint16)(word_idx + 1), (uint16)(word_end - word_idx));
}

/**
 * Строки SII: записать каталог строк, начиная с байтового смещения byte_off.
 * @param data_words Указатель для числа слов, занятых данными категории
 * @return Байтовое смещение за концом каталога строк
 */
static uint16 sii_build_strings(ethercat_esc_t* esc, uint16 byte_off, uint16* data_words) {
    uint16 start_word = (uint16)(byte_off / 2);
    uint16 b = byte_off;
    uint8 i;

    sii_set_byte(esc, b++, SII_STR_COUNT);

    for (i = 0; i < SII_STR_COUNT; i++) {
        const char* s = g_sii_strings[i];
        uint8 len = (uint8)strlen(s);

        sii_set_byte(esc, b++, len);

        while (*s) {
            sii_set_byte(esc, b++, (uint8)(*s++));
        }

        /* Строка занимает чётное число байт (длина + паддинг) */
        if ((len & 1) == 0) {
            sii_set_byte(esc, b++, 0);
        }
    }

    if (data_words != NULL) {
        *data_words = (uint16)(((b + 1) / 2) - start_word);
    }
    return b;
}

uint16 ethercat_esc_build_sii(ethercat_esc_t* esc) {
    uint16 word;

    if (esc == NULL) {
        return 0;
    }

    memset(esc->sii, 0x00, sizeof(esc->sii));

    // ------------------------------------------------------------------------
    // Слова 0x00..0x3F: служебные данные SII (не категории)
    // ------------------------------------------------------------------------
    sii_set(esc, 0x0000, 0x0000);  /* PDI Control / Configuration */
    sii_set(esc, 0x0001, 0x0000);  /* PDI Configuration */
    sii_set(esc, 0x0002, 0x0000);  /* Sync Impulse Length */
    sii_set(esc, 0x0004, 0x0000);  /* Configured Station Alias */

    sii_set(esc, 0x0008, (uint16)(ECT_VENDOR_ID & 0xFFFF));
    sii_set(esc, 0x0009, (uint16)((ECT_VENDOR_ID >> 16) & 0xFFFF));
    sii_set(esc, 0x000A, (uint16)(ECT_PRODUCT_CODE & 0xFFFF));
    sii_set(esc, 0x000B, (uint16)((ECT_PRODUCT_CODE >> 16) & 0xFFFF));
    sii_set(esc, 0x000C, (uint16)(ECT_REVISION_NUMBER & 0xFFFF));
    sii_set(esc, 0x000D, (uint16)((ECT_REVISION_NUMBER >> 16) & 0xFFFF));
    sii_set(esc, 0x000E, (uint16)(ECT_SERIAL_NUMBER & 0xFFFF));
    sii_set(esc, 0x000F, (uint16)((ECT_SERIAL_NUMBER >> 16) & 0xFFFF));

    /* Bootstrap mailbox — не используется */
    sii_set(esc, 0x0014, 0x0000);
    sii_set(esc, 0x0015, 0x0000);
    sii_set(esc, 0x0016, 0x0000);
    sii_set(esc, 0x0017, 0x0000);

    /* Standard mailbox: SM0 = 0x1200, SM1 = 0x1300 */
    sii_set(esc, 0x0018, MAILBOX_OUT_ADDR);
    sii_set(esc, 0x0019, ECT_MBX_SIZE);
    sii_set(esc, 0x001A, MAILBOX_IN_ADDR);
    sii_set(esc, 0x001B, ECT_MBX_SIZE);

    /* Поддерживаемые протоколы почтового ящика: CoE */
    sii_set(esc, 0x001C, ECT_MBX_PROTO_COE);

    // ------------------------------------------------------------------------
    // Категории (со слова 0x0040)
    // ------------------------------------------------------------------------
    word = ECT_SII_FIRST_CATEGORY;

    /* --- Категория Strings ------------------------------------------------ */
    {
        uint16 hdr = word;
        uint16 data_words = 0;
        uint16 byte_off = (uint16)((hdr + 2) * 2);

        word = sii_cat_begin(esc, hdr, ECT_SII_CAT_STRINGS);
        (void)sii_build_strings(esc, byte_off, &data_words);
        word = (uint16)(word + data_words);
        sii_cat_end(esc, hdr, word);
    }

    /* --- Категория General (32 байта) ------------------------------------- */
    {
        uint16 hdr = word;
        uint16 b = (uint16)((hdr + 2) * 2);

        word = sii_cat_begin(esc, hdr, ECT_SII_CAT_GENERAL);

        sii_set_byte(esc, (uint16)(b + 0x00), SII_STR_GROUP);  /* группа */
        sii_set_byte(esc, (uint16)(b + 0x01), SII_STR_NAME);   /* образ */
        sii_set_byte(esc, (uint16)(b + 0x02), SII_STR_ORDER);  /* заказ */
        sii_set_byte(esc, (uint16)(b + 0x03), SII_STR_NAME);   /* имя */
        sii_set_byte(esc, (uint16)(b + 0x04), 0x55);  /* физ. слой: 4×100BASE-TX */
        /* CoE details: SDO, SDO Info, PDO Assign, PDO Config,
         * Upload at startup, Complete Access */
        sii_set_byte(esc, (uint16)(b + 0x05), 0x3F);
        /* General Flags: bit0 = SafeOp */
        sii_set_byte(esc, (uint16)(b + 0x0B), 0x01);
        sii_set_byte(esc, (uint16)(b + 0x0C), 0xC8);  /* ток 200 мА */
        sii_set_byte(esc, (uint16)(b + 0x0D), 0x00);

        word = (uint16)(word + 16);  /* 32 байта = 16 слов */
        sii_cat_end(esc, hdr, word);
    }

    /* --- Категория SyncManager (4 записи по 8 байт) ------------------------ */
    {
        uint16 hdr = word;
        uint16 b = (uint16)((hdr + 2) * 2);

        word = sii_cat_begin(esc, hdr, ECT_SII_CAT_SYNC);

        /* SM0: почтовый ящик (мастер → слейв) */
        sii_set_byte(esc, (uint16)(b + 0), (uint8)(MAILBOX_OUT_ADDR & 0xFF));
        sii_set_byte(esc, (uint16)(b + 1), (uint8)((MAILBOX_OUT_ADDR >> 8) & 0xFF));
        sii_set_byte(esc, (uint16)(b + 2), (uint8)(ECT_MBX_SIZE & 0xFF));
        sii_set_byte(esc, (uint16)(b + 3), (uint8)((ECT_MBX_SIZE >> 8) & 0xFF));
        sii_set_byte(esc, (uint16)(b + 4), ECT_SMCTRL_MBXOUT);
        sii_set_byte(esc, (uint16)(b + 6), 0x01);
        sii_set_byte(esc, (uint16)(b + 7), ECT_SII_SMTYPE_MBXOUT);
        b = (uint16)(b + 8);

        /* SM1: почтовый ящик (слейв → мастер) */
        sii_set_byte(esc, (uint16)(b + 0), (uint8)(MAILBOX_IN_ADDR & 0xFF));
        sii_set_byte(esc, (uint16)(b + 1), (uint8)((MAILBOX_IN_ADDR >> 8) & 0xFF));
        sii_set_byte(esc, (uint16)(b + 2), (uint8)(ECT_MBX_SIZE & 0xFF));
        sii_set_byte(esc, (uint16)(b + 3), (uint8)((ECT_MBX_SIZE >> 8) & 0xFF));
        sii_set_byte(esc, (uint16)(b + 4), ECT_SMCTRL_MBXIN);
        sii_set_byte(esc, (uint16)(b + 6), 0x01);
        sii_set_byte(esc, (uint16)(b + 7), ECT_SII_SMTYPE_MBXIN);
        b = (uint16)(b + 8);

        /* SM2: выходные данные (RxPDO) */
        sii_set_byte(esc, (uint16)(b + 0), (uint8)(PDO_OUTPUT_ADDR & 0xFF));
        sii_set_byte(esc, (uint16)(b + 1), (uint8)((PDO_OUTPUT_ADDR >> 8) & 0xFF));
        sii_set_byte(esc, (uint16)(b + 2), (uint8)(PDO_OUTPUT_SIZE & 0xFF));
        sii_set_byte(esc, (uint16)(b + 3), (uint8)((PDO_OUTPUT_SIZE >> 8) & 0xFF));
        sii_set_byte(esc, (uint16)(b + 4), ECT_SMCTRL_OUTPUT);
        sii_set_byte(esc, (uint16)(b + 6), 0x01);
        sii_set_byte(esc, (uint16)(b + 7), ECT_SII_SMTYPE_OUTPUT);
        b = (uint16)(b + 8);

        /* SM3: входные данные (TxPDO) */
        sii_set_byte(esc, (uint16)(b + 0), (uint8)(PDO_INPUT_ADDR & 0xFF));
        sii_set_byte(esc, (uint16)(b + 1), (uint8)((PDO_INPUT_ADDR >> 8) & 0xFF));
        sii_set_byte(esc, (uint16)(b + 2), (uint8)(PDO_INPUT_SIZE & 0xFF));
        sii_set_byte(esc, (uint16)(b + 3), (uint8)((PDO_INPUT_SIZE >> 8) & 0xFF));
        sii_set_byte(esc, (uint16)(b + 4), ECT_SMCTRL_INPUT);
        sii_set_byte(esc, (uint16)(b + 6), 0x01);
        sii_set_byte(esc, (uint16)(b + 7), ECT_SII_SMTYPE_INPUT);

        word = (uint16)(word + 16);  /* 4 × 8 байт = 16 слов */
        sii_cat_end(esc, hdr, word);
    }

    /* --- Категория TxPDO (0x1A00 → 0x6000:01, 0x6000:02) ------------------ */
    {
        uint16 hdr = word;
        uint16 b = (uint16)((hdr + 2) * 2);
        uint8 e;

        word = sii_cat_begin(esc, hdr, ECT_SII_CAT_TXPDO);

        /* Заголовок PDO (8 байт): индекс, число записей, SyncManager,
         * DC-Sync (0), индекс имени, флаги, резерв */
        sii_set_byte(esc, (uint16)(b + 0), 0x00);
        sii_set_byte(esc, (uint16)(b + 1), 0x1A);
        sii_set_byte(esc, (uint16)(b + 2), 2);
        sii_set_byte(esc, (uint16)(b + 3), 3);
        sii_set_byte(esc, (uint16)(b + 4), 0);
        sii_set_byte(esc, (uint16)(b + 5), SII_STR_TXPDO);
        b = (uint16)(b + 8);

        /* Записи PDO (по 8 байт): индекс, подиндекс, имя, тип, бит, флаги */
        for (e = 0; e < 2; e++) {
            sii_set_byte(esc, (uint16)(b + 0), 0x00);
            sii_set_byte(esc, (uint16)(b + 1), 0x60);
            sii_set_byte(esc, (uint16)(b + 2), (uint8)(e + 1));
            sii_set_byte(esc, (uint16)(b + 3), (uint8)((e == 0) ? SII_STR_DI1 : SII_STR_DI2));
            sii_set_byte(esc, (uint16)(b + 4), 0x06);  /* UINT16 */
            sii_set_byte(esc, (uint16)(b + 5), 16);    /* 16 бит */
            b = (uint16)(b + 8);
        }

        word = (uint16)(word + 12);  /* 8 + 2×8 = 24 байта = 12 слов */
        sii_cat_end(esc, hdr, word);
    }

    /* --- Категория RxPDO (0x1600 → 0x7010:01, 0x7010:02) ------------------ */
    {
        uint16 hdr = word;
        uint16 b = (uint16)((hdr + 2) * 2);
        uint8 e;

        word = sii_cat_begin(esc, hdr, ECT_SII_CAT_RXPDO);

        sii_set_byte(esc, (uint16)(b + 0), 0x00);
        sii_set_byte(esc, (uint16)(b + 1), 0x16);
        sii_set_byte(esc, (uint16)(b + 2), 2);
        sii_set_byte(esc, (uint16)(b + 3), 2);
        sii_set_byte(esc, (uint16)(b + 4), 0);
        sii_set_byte(esc, (uint16)(b + 5), SII_STR_RXPDO);
        b = (uint16)(b + 8);

        for (e = 0; e < 2; e++) {
            sii_set_byte(esc, (uint16)(b + 0), 0x10);
            sii_set_byte(esc, (uint16)(b + 1), 0x70);
            sii_set_byte(esc, (uint16)(b + 2), (uint8)(e + 1));
            sii_set_byte(esc, (uint16)(b + 3), (uint8)((e == 0) ? SII_STR_DO1 : SII_STR_DO2));
            sii_set_byte(esc, (uint16)(b + 4), 0x06);  /* UINT16 */
            sii_set_byte(esc, (uint16)(b + 5), 16);    /* 16 бит */
            b = (uint16)(b + 8);
        }

        word = (uint16)(word + 12);  /* 24 байта = 12 слов */
        sii_cat_end(esc, hdr, word);
    }

    /* --- Терминатор категорий -------------------------------------------- */
    sii_set(esc, word, ECT_SII_CAT_END);
    word++;

    esc->sii_words = word;
    esc->eeprom_addr = 0;
    esc->eeprom_reads = 0;
    esc->eeprom_writes = 0;
    esc->last_cmd = 0;
    esc->initialized = true;

    return word;
}

// ============================================================================
// Инициализация регистров ESC
// ============================================================================

/** Записать конфигурацию SyncManager n (8 байт) */
static void esc_sm_write(ethercat_memory_t* mem, uint8 n, uint16 phys, uint16 length,
                         uint8 control, uint8 activate) {
    uint16 base = (uint16)(ECT_REG_SM0 + (n * ECT_SM_PAGE_SIZE));

    ethercat_memory_write16(mem, base, phys);
    ethercat_memory_write16(mem, (uint16)(base + 2), length);
    ethercat_memory_write8(mem, (uint16)(base + 4), control);
    ethercat_memory_write8(mem, (uint16)(base + 5), 0x00);  /* статус */
    ethercat_memory_write8(mem, (uint16)(base + 6), activate);
    ethercat_memory_write8(mem, (uint16)(base + 7), 0x00);  /* PDI control */
}

void ethercat_esc_init(ethercat_esc_t* esc, ethercat_memory_t* mem) {
    uint16 i;

    if (mem == NULL) {
        return;
    }

    // --- Идентификация ESC ---------------------------------------------------
    ethercat_memory_write8(mem, ECT_REG_TYPE, ECT_ESC_TYPE);
    ethercat_memory_write8(mem, (uint16)(ECT_REG_TYPE + 1), ECT_ESC_REVISION);
    ethercat_memory_write16(mem, ECT_REG_BUILD, ECT_ESC_BUILD);
    ethercat_memory_write8(mem, ECT_REG_FMMU_COUNT, ECT_FMMU_COUNT);
    ethercat_memory_write8(mem, ECT_REG_SM_COUNT, ECT_SM_COUNT);
    /* Дескриптор портов: 2 порта MII (по 2 бита на порт, 0b11 = MII) */
    ethercat_memory_write8(mem, ECT_REG_PORTDES, 0x0F);
    /* Возможности: bit2 = поддержка DC, bit3 = 64-битный диапазон DC */
    ethercat_memory_write8(mem, ECT_REG_ESCSUP, 0x0C);
    ethercat_memory_write8(mem, (uint16)(ECT_REG_ESCSUP + 1), 0x00);

    // --- Адрес станции / алиас ----------------------------------------------
    ethercat_memory_write16(mem, ECT_REG_STADR, 0x0000);
    ethercat_memory_write16(mem, ECT_REG_ALIAS, 0x0000);

    // --- DL / AL -------------------------------------------------------------
    ethercat_memory_write8(mem, ECT_REG_DLCTL, 0x00);
    /* Оба порта: link up (биты 4-5), loop closed (биты 8,10),
     * signal detected (биты 9,11) */
    ethercat_memory_write16(mem, ECT_REG_DLSTAT, 0x0F30);
    ethercat_memory_write16(mem, ECT_REG_ALCTL, 0x0000);
    ethercat_memory_write16(mem, ECT_REG_ALSTAT, EC_STATE_INIT);
    ethercat_memory_write16(mem, ECT_REG_ALSTATCODE, EC_ALSTATUSCODE_NOERROR);
    ethercat_memory_write8(mem, ECT_REG_PDICTL, 0x00);

    // --- Интерфейс EEPROM ----------------------------------------------------
    ethercat_memory_write8(mem, ECT_REG_EEPCFG, 0x00);  /* владелец — мастер */
    ethercat_memory_write16(mem, ECT_REG_EEPCTL, 0x0000);  /* команда/статус */
    ethercat_memory_write32(mem, ECT_REG_EEPADR, 0x00000000);
    for (i = 0; i < 8; i++) {
        ethercat_memory_write8(mem, (uint16)(ECT_REG_EEPDAT + i), 0x00);
    }

    // --- FMMU: все записи неактивны -----------------------------------------
    for (i = 0; i < (uint16)(ECT_FMMU_COUNT * ECT_FMMU_PAGE_SIZE); i++) {
        ethercat_memory_write8(mem, (uint16)(ECT_REG_FMMU0 + i), 0x00);
    }

    // --- SyncManager: конфигурация по умолчанию ------------------------------
    esc_sm_write(mem, 0, MAILBOX_OUT_ADDR, ECT_MBX_SIZE, ECT_SMCTRL_MBXOUT, ECT_SM_ENABLE_BIT);
    esc_sm_write(mem, 1, MAILBOX_IN_ADDR, ECT_MBX_SIZE, ECT_SMCTRL_MBXIN, ECT_SM_ENABLE_BIT);
    esc_sm_write(mem, 2, PDO_OUTPUT_ADDR, PDO_OUTPUT_SIZE, ECT_SMCTRL_OUTPUT, ECT_SM_ENABLE_BIT);
    esc_sm_write(mem, 3, PDO_INPUT_ADDR, PDO_INPUT_SIZE, ECT_SMCTRL_INPUT, ECT_SM_ENABLE_BIT);

    // --- Distributed Clock ---------------------------------------------------
    ethercat_memory_write32(mem, ECT_REG_DCTIME0, 0x00000000);
    ethercat_memory_write64(mem, ECT_REG_DCSYSTIME, 0);
    ethercat_memory_write32(mem, ECT_REG_DCSOF, 0x00000000);
    ethercat_memory_write64(mem, ECT_REG_DCSYSOFFSET, 0);
    ethercat_memory_write32(mem, ECT_REG_DCSYSDELAY, 0x00000000);
    ethercat_memory_write32(mem, ECT_REG_DCSYSDIFF, 0x00000000);
    ethercat_memory_write8(mem, ECT_REG_DCCUC, 0x00);
    ethercat_memory_write8(mem, ECT_REG_DCSYNCACT, 0x00);
    ethercat_memory_write16(mem, ECT_REG_DCSYNCPULSE, 0x0000);
    ethercat_memory_write64(mem, ECT_REG_DCSTART0, 0);
    ethercat_memory_write32(mem, ECT_REG_DCCYCLE0, 0);
    ethercat_memory_write32(mem, ECT_REG_DCCYCLE1, 0);

    // --- Образ SII -----------------------------------------------------------
    if (esc != NULL) {
        (void)ethercat_esc_build_sii(esc);
    }
}

// ============================================================================
// Обслуживание интерфейса EEPROM (чтение SII мастером)
// ============================================================================

boolean ethercat_esc_process_eeprom(ethercat_esc_t* esc, ethercat_memory_t* mem) {
    uint16 cmd;
    uint16 addr;
    uint8 b;

    if (esc == NULL || mem == NULL || !esc->initialized) {
        return false;
    }

    cmd = ethercat_memory_read16(mem, ECT_REG_EEPCTL);

    if ((cmd & ECT_EEPROM_CMD_MASK) == 0) {
        return false;  /* команды нет */
    }

    addr = (uint16)(ethercat_memory_read16(mem, ECT_REG_EEPADR) & ECT_EEPROM_ADDR_MASK);
    esc->last_cmd = cmd;
    esc->eeprom_addr = addr;

    if ((cmd & ECT_EEPROM_CMD_READ) != 0) {
        /* Отдать 2 слова (4 байта) SII */
        for (b = 0; b < 4; b++) {
            uint16 word_idx = (uint16)(addr + (b / 2));
            uint16 value = 0;
            uint8 out;

            if (word_idx < esc->sii_words) {
                value = esc->sii[word_idx];
            }

            out = (uint8)((value >> ((b & 1) * 8)) & 0xFF);
            ethercat_memory_write8(mem, (uint16)(ECT_REG_EEPDAT + b), out);
        }
        esc->eeprom_reads++;
    } else {
        /* Запись EEPROM: для SII не поддерживается — данные игнорируются */
        esc->eeprom_writes++;
    }

    /* Снять команду: очистить биты busy/nack/read — иначе мастер
     * будет ждать до таймаута */
    ethercat_memory_write16(mem, ECT_REG_EEPCTL, 0x0000);

    return true;
}

// ============================================================================
// FMMU: трансляция логических адресов в физические
// ============================================================================

boolean ethercat_esc_fmmu_translate(const uint8* memory,
                                    uint16 logical,
                                    uint16* physical) {
    uint8 i;

    if (memory == NULL) {
        return false;
    }

    for (i = 0; i < ECT_FMMU_COUNT; i++) {
        uint16 base = (uint16)(ECT_REG_FMMU0 + (i * ECT_FMMU_PAGE_SIZE));
        uint8 activate = memory[base + ECT_FMMU_ACTIVATE];
        uint16 start;
        uint16 length;
        uint16 phys;

        if ((activate & 0x01) == 0) {
            continue;  /* FMMU выключен */
        }

        length = (uint16)(memory[base + 2] | (memory[base + 3] << 8));
        if (length == 0) {
            continue;
        }

        start = (uint16)(memory[base] | (memory[base + 1] << 8));
        if ((logical < start) || (logical >= (uint16)(start + length))) {
            continue;
        }

        phys = (uint16)(memory[base + 6] | (memory[base + 7] << 8));

        if (physical != NULL) {
            *physical = (uint16)(phys + (logical - start));
        }
        return true;
    }

    return false;
}

// ============================================================================
// Доступ к областям PDO через настройки SyncManager
// ============================================================================

uint16 ethercat_esc_get_pdo_address(const ethercat_memory_t* mem, boolean output) {
    uint16 base;
    uint16 addr;

    if (mem == NULL) {
        return 0;
    }

    base = (uint16)(ECT_REG_SM0 + ((output ? 2 : 3) * ECT_SM_PAGE_SIZE));
    addr = (uint16)(mem->memory[base] | (mem->memory[base + 1] << 8));

    if (addr >= ETHERCAT_MEMORY_SIZE) {
        return 0;  /* вне карты памяти — использовать значение по умолчанию */
    }

    return addr;
}

uint16 ethercat_esc_get_pdo_length(const ethercat_memory_t* mem, boolean output) {
    uint16 base;
    uint16 addr;
    uint16 length;

    if (mem == NULL) {
        return 0;
    }

    base = (uint16)(ECT_REG_SM0 + ((output ? 2 : 3) * ECT_SM_PAGE_SIZE));
    addr = (uint16)(mem->memory[base] | (mem->memory[base + 1] << 8));
    length = (uint16)(mem->memory[base + 2] | (mem->memory[base + 3] << 8));

    if (addr >= ETHERCAT_MEMORY_SIZE) {
        return 0;
    }

    if ((uint32)addr + length > ETHERCAT_MEMORY_SIZE) {
        length = (uint16)(ETHERCAT_MEMORY_SIZE - addr);
    }

    return length;
}

// ============================================================================
// Проверка настроек SyncManager (условие перехода в OP)
// ============================================================================

/** Проверить, что область SM n лежит внутри карты памяти */
static boolean esc_sm_area_valid(const ethercat_memory_t* mem, uint8 n) {
    uint16 base = (uint16)(ECT_REG_SM0 + (n * ECT_SM_PAGE_SIZE));
    uint16 addr = (uint16)(mem->memory[base] | (mem->memory[base + 1] << 8));
    uint16 length = (uint16)(mem->memory[base + 2] | (mem->memory[base + 3] << 8));
    uint8 activate = mem->memory[base + 6];

    if ((activate & ECT_SM_ENABLE_BIT) == 0) {
        return false;  /* SM не активирован мастером */
    }

    if (addr >= ETHERCAT_MEMORY_SIZE) {
        return false;
    }

    if ((uint32)addr + length > ETHERCAT_MEMORY_SIZE) {
        return false;
    }

    return true;
}

uint16 ethercat_esc_check_sm_config(const ethercat_memory_t* mem) {
    if (mem == NULL) {
        return EC_ALSTATUSCODE_UNSPECIFIED;
    }

    /* SM0/SM1 обязаны быть в режиме mailbox (0b10) */
    if ((mem->memory[ECT_REG_SM0 + 4] & ECT_SM_MODE_MASK) != ECT_SM_MODE_MAILBOX) {
        return EC_ALSTATUSCODE_INVALIDSM;
    }
    if ((mem->memory[ECT_REG_SM0 + ECT_SM_PAGE_SIZE + 4] & ECT_SM_MODE_MASK)
            != ECT_SM_MODE_MAILBOX) {
        return EC_ALSTATUSCODE_INVALIDSM;
    }

    /* Области выходных (SM2) и входных (SM3) данных должны быть корректны */
    if (esc_sm_area_valid(mem, 2) == false) {
        return EC_ALSTATUSCODE_INVALIDOUTPUT;
    }
    if (esc_sm_area_valid(mem, 3) == false) {
        return EC_ALSTATUSCODE_INVALIDINPUT;
    }

    return EC_ALSTATUSCODE_NOERROR;
}

// ============================================================================
// DL Status и счётчики
// ============================================================================

void ethercat_esc_set_dl_status(ethercat_memory_t* mem, uint16 dl_status) {
    if (mem == NULL) {
        return;
    }
    ethercat_memory_write16(mem, ECT_REG_DLSTAT, dl_status);
}

void ethercat_esc_update_counters(ethercat_memory_t* mem, boolean frame_ok) {
    if (mem == NULL) {
        return;
    }

    if (frame_ok == false) {
        /* Счётчик ошибок приёма порта 0 (0x0300), с насыщением */
        uint8 errs = ethercat_memory_read8(mem, 0x0300);
        if (errs < 0xFF) {
            ethercat_memory_write8(mem, 0x0300, (uint8)(errs + 1));
        }
    }
}

