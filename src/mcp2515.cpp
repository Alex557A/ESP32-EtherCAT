/*
 * MCP2515 CAN Controller Driver - Implementation
 * 
 * Phase 5, Week 9, Day 57-58
 * ESP32 EtherCAT Slave v4
 * 
 * Использует SPI Bus 2 (HSPI) для периферии
 */

#include "mcp2515.h"
#include "mcp23s17.h"  // Для доступа к g_spi2
#include <Arduino.h>
#include <SPI.h>
#include <string.h>

extern "C" {

// Используем SPI Bus 2 из mcp23s17.cpp (общий для периферии)
extern SPIClass* g_spi2;

// ============================================================================
// SPI Communication (SPI Bus 2)
// ============================================================================

static SPISettings g_mcp2515_spi_settings(10000000, MSBFIRST, SPI_MODE0);  // 10 MHz

static void mcp2515_spi_begin(uint8 cs_pin) {
    if (g_spi2 == NULL) {
        Serial.println("[MCP2515] ERROR: SPI Bus 2 not initialized!");
        return;
    }
    digitalWrite(cs_pin, LOW);
    delayMicroseconds(1);
    g_spi2->beginTransaction(g_mcp2515_spi_settings);
}

static void mcp2515_spi_end(uint8 cs_pin) {
    if (g_spi2 == NULL) {
        return;
    }
    g_spi2->endTransaction();
    delayMicroseconds(1);
    digitalWrite(cs_pin, HIGH);
}

static uint8 mcp2515_spi_transfer(uint8 data) {
    if (g_spi2 == NULL) {
        return 0;
    }
    return g_spi2->transfer(data);
}

static uint8 mcp2515_read_register(uint8 cs_pin, uint8 reg) {
    mcp2515_spi_begin(cs_pin);
    mcp2515_spi_transfer(MCP2515_CMD_READ);
    mcp2515_spi_transfer(reg);
    uint8 value = mcp2515_spi_transfer(0x00);
    mcp2515_spi_end(cs_pin);
    return value;
}

static void mcp2515_write_register(uint8 cs_pin, uint8 reg, uint8 value) {
    mcp2515_spi_begin(cs_pin);
    mcp2515_spi_transfer(MCP2515_CMD_WRITE);
    mcp2515_spi_transfer(reg);
    mcp2515_spi_transfer(value);
    mcp2515_spi_end(cs_pin);
}

static void mcp2515_reset(uint8 cs_pin) {
    mcp2515_spi_begin(cs_pin);
    mcp2515_spi_transfer(MCP2515_CMD_RESET);
    mcp2515_spi_end(cs_pin);
    delay(10);  // Wait for reset
}

// ============================================================================
// Bitrate Configuration
// ============================================================================

static boolean mcp2515_configure_bitrate(uint8 cs_pin, uint32 bitrate) {
    // MCP2515 bitrate configuration
    // CNF1, CNF2, CNF3 registers для настройки bitrate
    
    uint8 cnf1, cnf2, cnf3;
    
    // Стандартные bitrate конфигурации
    if (bitrate == 125000) {
        // 125 kbps @ 16MHz
        cnf1 = 0x03;  // BRP = 4
        cnf2 = 0xB1;  // PS1 = 6, PS2 = 2
        cnf3 = 0x85;  // PS2 = 2
    } else if (bitrate == 250000) {
        // 250 kbps
        cnf1 = 0x01;  // BRP = 2
        cnf2 = 0xB1;
        cnf3 = 0x85;
    } else if (bitrate == 500000) {
        // 500 kbps (default)
        cnf1 = 0x00;  // BRP = 1
        cnf2 = 0xB1;
        cnf3 = 0x85;
    } else if (bitrate == 1000000) {
        // 1 Mbps
        cnf1 = 0x00;
        cnf2 = 0x90;  // PS1 = 3, PS2 = 1
        cnf3 = 0x82;
    } else {
        // Default: 500 kbps
        cnf1 = 0x00;
        cnf2 = 0xB1;
        cnf3 = 0x85;
    }
    
    // Запись конфигурации
    mcp2515_write_register(cs_pin, MCP2515_REG_CNF1, cnf1);
    mcp2515_write_register(cs_pin, MCP2515_REG_CNF2, cnf2);
    mcp2515_write_register(cs_pin, MCP2515_REG_CNF3, cnf3);
    
    return true;
}

// ============================================================================
// Initialization
// ============================================================================

boolean mcp2515_init(mcp2515_t* can, uint8 cs_pin, uint8 int_pin, uint32 bitrate) {
    if (can == NULL) {
        return false;
    }
    
    if (g_spi2 == NULL) {
        Serial.println("[MCP2515] ERROR: SPI Bus 2 not initialized! Call mcp23s17_spi2_init() first.");
        return false;
    }
    
    memset(can, 0, sizeof(mcp2515_t));
    
    can->cs_pin = cs_pin;
    can->int_pin = int_pin;
    can->bitrate = bitrate;
    can->loopback_mode = false;
    
    // Настройка SPI pins
    pinMode(cs_pin, OUTPUT);
    digitalWrite(cs_pin, HIGH);
    
    if (int_pin != 0xFF) {
        pinMode(int_pin, INPUT);
    }
    
    // Reset MCP2515
    mcp2515_reset(cs_pin);
    
    // Переход в CONFIG mode
    mcp2515_write_register(cs_pin, MCP2515_REG_CANCTRL, MCP2515_CANCTRL_REQOP_CONFIG);
    delay(10);
    
    // Проверка, что мы в CONFIG mode
    uint8 canstat = mcp2515_read_register(cs_pin, MCP2515_REG_CANSTAT);
    if ((canstat & MCP2515_CANCTRL_REQOP_MASK) != MCP2515_CANCTRL_REQOP_CONFIG) {
        Serial.printf("[MCP2515] Failed to enter CONFIG mode (status: 0x%02X)\n", canstat);
        return false;
    }
    
    // Настройка bitrate
    if (!mcp2515_configure_bitrate(cs_pin, bitrate)) {
        Serial.println("[MCP2515] Failed to configure bitrate");
        return false;
    }
    
    // Настройка фильтров (принимать все сообщения)
    // RXB0CTRL: BUKT=1 (rollover), RXM=0 (accept all)
    mcp2515_write_register(cs_pin, MCP2515_REG_RXB0CTRL, 0x60);
    
    // Включение прерываний
    mcp2515_write_register(cs_pin, MCP2515_REG_CANINTE, 
                          MCP2515_CANINTF_RX0IF | MCP2515_CANINTF_RX1IF);
    
    // Переход в NORMAL mode
    mcp2515_write_register(cs_pin, MCP2515_REG_CANCTRL, MCP2515_CANCTRL_REQOP_NORMAL);
    delay(10);
    
    // Проверка, что мы в NORMAL mode
    canstat = mcp2515_read_register(cs_pin, MCP2515_REG_CANSTAT);
    if ((canstat & MCP2515_CANCTRL_REQOP_MASK) != MCP2515_CANCTRL_REQOP_NORMAL) {
        Serial.printf("[MCP2515] Failed to enter NORMAL mode (status: 0x%02X)\n", canstat);
        return false;
    }
    
    can->initialized = true;
    
    Serial.printf("[MCP2515] Initialized: CS=%d, INT=%d, Bitrate=%u bps\n",
                  cs_pin, int_pin, bitrate);
    
    return true;
}

// ============================================================================
// Configuration
// ============================================================================

boolean mcp2515_set_bitrate(mcp2515_t* can, uint32 bitrate) {
    if (can == NULL || !can->initialized) {
        return false;
    }
    
    // Переход в CONFIG mode
    mcp2515_write_register(can->cs_pin, MCP2515_REG_CANCTRL, MCP2515_CANCTRL_REQOP_CONFIG);
    delay(10);
    
    // Настройка bitrate
    if (!mcp2515_configure_bitrate(can->cs_pin, bitrate)) {
        return false;
    }
    
    // Переход в NORMAL mode
    uint8 mode = can->loopback_mode ? MCP2515_CANCTRL_REQOP_LOOPBACK : MCP2515_CANCTRL_REQOP_NORMAL;
    mcp2515_write_register(can->cs_pin, MCP2515_REG_CANCTRL, mode);
    delay(10);
    
    can->bitrate = bitrate;
    
    Serial.printf("[MCP2515] Bitrate set to %u bps\n", bitrate);
    
    return true;
}

boolean mcp2515_set_loopback(mcp2515_t* can, boolean enable) {
    if (can == NULL || !can->initialized) {
        return false;
    }
    
    // Переход в CONFIG mode
    mcp2515_write_register(can->cs_pin, MCP2515_REG_CANCTRL, MCP2515_CANCTRL_REQOP_CONFIG);
    delay(10);
    
    // Переход в LOOPBACK или NORMAL mode
    uint8 mode = enable ? MCP2515_CANCTRL_REQOP_LOOPBACK : MCP2515_CANCTRL_REQOP_NORMAL;
    mcp2515_write_register(can->cs_pin, MCP2515_REG_CANCTRL, mode);
    delay(10);
    
    can->loopback_mode = enable;
    
    Serial.printf("[MCP2515] Loopback mode: %s\n", enable ? "ON" : "OFF");
    
    return true;
}

// ============================================================================
// TX/RX Functions
// ============================================================================

boolean mcp2515_send(mcp2515_t* can, const can_frame_t* frame) {
    if (can == NULL || !can->initialized || frame == NULL) {
        return false;
    }
    
    if (frame->dlc > 8) {
        return false;  // Invalid DLC
    }
    
    // Проверка доступности TX buffer (TXB0)
    uint8 txb0ctrl = mcp2515_read_register(can->cs_pin, MCP2515_REG_TXB0CTRL);
    if (txb0ctrl & 0x08) {
        // TX buffer занят
        can->tx_errors++;
        return false;
    }
    
    // Запись ID (11-bit standard)
    uint16 sid = frame->id & 0x7FF;
    mcp2515_write_register(can->cs_pin, MCP2515_REG_TXB0SIDH, (sid >> 3) & 0xFF);
    uint8 sidl = (sid << 5) & 0xE0;
    if (frame->rtr) sidl |= 0x40;  // RTR bit
    mcp2515_write_register(can->cs_pin, MCP2515_REG_TXB0SIDH + 1, sidl);
    
    // Запись DLC
    mcp2515_write_register(can->cs_pin, MCP2515_REG_TXB0DLC, frame->dlc & 0x0F);
    
    // Запись данных
    for (uint8 i = 0; i < frame->dlc; i++) {
        mcp2515_write_register(can->cs_pin, MCP2515_REG_TXB0D0 + i, frame->data[i]);
    }
    
    // Запрос отправки (RTS TXB0)
    mcp2515_spi_begin(can->cs_pin);
    mcp2515_spi_transfer(MCP2515_CMD_RTS | 0x01);  // RTS TXB0
    mcp2515_spi_end(can->cs_pin);
    
    can->tx_count++;
    
    return true;
}

boolean mcp2515_receive(mcp2515_t* can, can_frame_t* frame) {
    if (can == NULL || !can->initialized || frame == NULL) {
        return false;
    }
    
    // Проверка наличия фреймов
    uint8 intf = mcp2515_read_register(can->cs_pin, MCP2515_REG_CANINTF);
    
    if (!(intf & MCP2515_CANINTF_RX0IF)) {
        return false;  // Нет фреймов
    }
    
    // Чтение ID (11-bit standard)
    uint8 sidh = mcp2515_read_register(can->cs_pin, MCP2515_REG_RXB0SIDH);
    uint8 sidl = mcp2515_read_register(can->cs_pin, MCP2515_REG_RXB0SIDH + 1);
    frame->id = ((uint16)sidh << 3) | (sidl >> 5);
    
    // Чтение DLC
    uint8 rxb0dlc = mcp2515_read_register(can->cs_pin, MCP2515_REG_RXB0DLC);
    frame->dlc = rxb0dlc & 0x0F;
    frame->rtr = (rxb0dlc & 0x40) ? 1 : 0;
    frame->ide = (rxb0dlc & 0x08) ? 1 : 0;
    
    // Чтение данных
    for (uint8 i = 0; i < frame->dlc && i < 8; i++) {
        frame->data[i] = mcp2515_read_register(can->cs_pin, MCP2515_REG_RXB0D0 + i);
    }
    
    // Очистка флага прерывания
    mcp2515_write_register(can->cs_pin, MCP2515_REG_CANINTF, 
                          intf & ~MCP2515_CANINTF_RX0IF);
    
    can->rx_count++;
    
    return true;
}

boolean mcp2515_available(mcp2515_t* can) {
    if (can == NULL || !can->initialized) {
        return false;
    }
    
    uint8 intf = mcp2515_read_register(can->cs_pin, MCP2515_REG_CANINTF);
    return (intf & (MCP2515_CANINTF_RX0IF | MCP2515_CANINTF_RX1IF)) != 0;
}

void mcp2515_get_stats(mcp2515_t* can, uint32* tx_count, uint32* rx_count,
                      uint32* tx_errors, uint32* rx_errors) {
    if (can == NULL || !can->initialized) {
        return;
    }
    
    if (tx_count != NULL) *tx_count = can->tx_count;
    if (rx_count != NULL) *rx_count = can->rx_count;
    if (tx_errors != NULL) *tx_errors = can->tx_errors;
    if (rx_errors != NULL) *rx_errors = can->rx_errors;
}

} // extern "C"
