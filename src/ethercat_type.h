/*
 * EtherCAT типы данных
 * На основе SOEM ec_type.h для полной совместимости
 * 
 * Phase 1, Week 1, Day 1-2
 * ESP32 EtherCAT Slave v4
 */

#ifndef ETHERCAT_TYPE_H
#define ETHERCAT_TYPE_H

#include <stdint.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// ============================================================================
// Базовые типы (для совместимости с SOEM)
// ============================================================================

typedef uint8_t uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;
typedef int8_t int8;
typedef int16_t int16;
typedef int32_t int32;
typedef int64_t int64;
typedef bool boolean;

// ============================================================================
// Константы EtherCAT
// ============================================================================

/** EtherType для EtherCAT */
#define ETH_P_ECAT 0x88A4

/** Максимальный размер EtherCAT фрейма */
#define EC_MAXECATFRAME 1518

/** Максимальный размер данных в LRW фрейме */
#define EC_MAXLRWDATA   1486

/** Размер Ethernet header */
#define ETH_HEADERSIZE  14

/** Размер EtherCAT header */
#define EC_HEADERSIZE   10

/** Размер Working Counter */
#define EC_WKCSIZE      2

/** Флаг "следующий datagram" в dlength */
#define EC_DATAGRAMFOLLOWS (1 << 15)

/** EtherCAT тип в elength */
#define EC_ECATTYPE     0x1000

// ============================================================================
// EtherCAT States (из SOEM)
// ============================================================================

/** Возможные состояния EtherCAT Slave */
typedef enum {
   /** Нет валидного состояния */
   EC_STATE_NONE = 0x00,
   /** Init state */
   EC_STATE_INIT = 0x01,
   /** Pre-operational */
   EC_STATE_PRE_OP = 0x02,
   /** Boot state */
   EC_STATE_BOOT = 0x03,
   /** Safe-operational */
   EC_STATE_SAFE_OP = 0x04,
   /** Operational */
   EC_STATE_OPERATIONAL = 0x08,
   /** Error или ACK error */
   EC_STATE_ACK = 0x10,
   EC_STATE_ERROR = 0x10
} ec_state;

// ============================================================================
// EtherCAT Commands (из SOEM)
// ============================================================================

/** Типы EtherCAT команд */
typedef enum {
   /** No operation */
   EC_CMD_NOP = 0x00,
   /** Auto Increment Read */
   EC_CMD_APRD = 0x01,
   /** Auto Increment Write */
   EC_CMD_APWR = 0x02,
   /** Auto Increment Read Write */
   EC_CMD_APRW = 0x03,
   /** Configured Address Read */
   EC_CMD_FPRD = 0x04,
   /** Configured Address Write */
   EC_CMD_FPWR = 0x05,
   EC_CMD_FPRW = 0x06,
   /** Broadcast Read */
   EC_CMD_BRD = 0x07,
   /** Broadcast Write */
   EC_CMD_BWR = 0x08,
   EC_CMD_BRW = 0x09,
   /** Logical Read */
   EC_CMD_LRD = 0x0A,
   /** Logical Write */
   EC_CMD_LWR = 0x0B,
   /** Logical Read-Write */
   EC_CMD_LRW = 0x0C
} ec_cmdtype;

// ============================================================================
// EtherCAT Registers (из SOEM)
// ============================================================================

enum {
   ECT_REG_TYPE = 0x0000,
   ECT_REG_PORTDES = 0x0007,
   ECT_REG_STADR = 0x0010,      // Station Address
   ECT_REG_ALIAS = 0x0012,
   ECT_REG_ALCTL = 0x0120,      // AL Control
   ECT_REG_ALSTAT = 0x0130,     // AL Status
   ECT_REG_ALSTATCODE = 0x0134, // AL Status Code
   ECT_REG_PDICTL = 0x0140,
   ECT_REG_FMMU0 = 0x0600,
   ECT_REG_SM0 = 0x0800,        // SyncManager 0
   ECT_REG_SM1 = 0x0808,
   ECT_REG_SM2 = 0x0810,
   ECT_REG_SM3 = 0x0818,
   ECT_REG_DCSYSTIME = 0x0910,  // DC System Time
   ECT_REG_DCSTART0 = 0x0990,   // SYNC0 Start Time
   ECT_REG_DCCYCLE0 = 0x09A0    // SYNC0 Cycle Time
};

// ============================================================================
// AL Status Codes (из SOEM)
// ============================================================================

#define EC_ALSTATUSCODE_NOERROR       0x0000
#define EC_ALSTATUSCODE_UNSPECIFIED   0x0001
#define EC_ALSTATUSCODE_INVALIDSTATE  0x0011
#define EC_ALSTATUSCODE_INVALIDSM     0x0016
#define EC_ALSTATUSCODE_INVALIDOUTPUT 0x001A
#define EC_ALSTATUSCODE_INVALIDINPUT  0x001B

// ============================================================================
// Endianness (ESP32 = little endian)
// ============================================================================

// Network byte order (big-endian) <-> Host byte order (little-endian)
// Ethernet фреймы всегда в network byte order (big-endian)
// ESP32 - little-endian, поэтому нужно преобразование

static inline uint16_t swap16(uint16_t x) {
    return ((x & 0xFF) << 8) | ((x >> 8) & 0xFF);
}

static inline uint32_t swap32(uint32_t x) {
    return ((x & 0xFF) << 24) | ((x & 0xFF00) << 8) | ((x >> 8) & 0xFF00) | ((x >> 24) & 0xFF);
}

// Host to Ethernet (Short/Long) - преобразуем из little-endian в big-endian
#define htoes(A)  swap16(A)
#define htoel(A)  swap32(A)

// Ethernet to Host (Short/Long) - преобразуем из big-endian в little-endian
#define etohs(A)  swap16(A)
#define etohl(A)  swap32(A)

// ============================================================================
// Packed structures
// ============================================================================

#define PACKED __attribute__((packed))

// ============================================================================
// Ethernet Header
// ============================================================================

typedef struct PACKED {
   uint16 da0, da1, da2;  // Destination MAC
   uint16 sa0, sa1, sa2;  // Source MAC
   uint16 etype;          // 0x88A4
} ec_etherheadert;

// ============================================================================
// EtherCAT Datagram Header
// ============================================================================

typedef struct PACKED {
   uint16 elength;
   uint8 command;
   uint8 index;
   uint16 ADP;  // Address Position
   uint16 ADO;  // Address Offset
   uint16 dlength;
   uint16 irpt;
} ec_comt;

#ifdef __cplusplus
}
#endif

#endif // ETHERCAT_TYPE_H
