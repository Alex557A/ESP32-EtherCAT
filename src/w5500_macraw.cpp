/*
 * W5500 MACRAW Driver - Implementation
 * 
 * Phase 1, Week 3, Day 15-16
 * ESP32 EtherCAT Slave v4
 */

#include "w5500_macraw.h"
#include <stddef.h>
#include <Arduino.h>
#include <SPI.h>
#include <string.h>

// SPI speed for W5500 MACRAW access (defaults to 20MHz).
// Важно: НЕ вызываем SPI.begin() здесь, чтобы не сбросить пины,
// настроенные в `main.cpp` через `SPI.begin(SCK, MISO, MOSI, ...)`.
static uint32_t g_w5500_macraw_spi_speed_hz = 20000000;

// ============================================================================
// Low-level SPI Functions
// ============================================================================

static void w5500_write_reg(uint8_t cs_pin, uint16_t addr, uint8_t bsb, uint8_t data) {
    digitalWrite(cs_pin, LOW);
    
    SPISettings spi_settings(g_w5500_macraw_spi_speed_hz, MSBFIRST, SPI_MODE0);
    SPI.beginTransaction(spi_settings);
    
    // Address phase
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    
    // Control phase (BSB + Write)
    SPI.transfer((bsb << 3) | W5500_SPI_WRITE);
    
    // Data phase
    SPI.transfer(data);
    
    SPI.endTransaction();
    
    digitalWrite(cs_pin, HIGH);
}

static uint8_t w5500_read_reg(uint8_t cs_pin, uint16_t addr, uint8_t bsb) {
    digitalWrite(cs_pin, LOW);
    
    SPISettings spi_settings(g_w5500_macraw_spi_speed_hz, MSBFIRST, SPI_MODE0);
    SPI.beginTransaction(spi_settings);
    
    // Address phase
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    
    // Control phase (BSB + Read)
    SPI.transfer((bsb << 3) | W5500_SPI_READ);
    
    // Data phase
    uint8_t data = SPI.transfer(0x00);
    
    SPI.endTransaction();
    
    digitalWrite(cs_pin, HIGH);
    
    return data;
}

static void w5500_write_buf(uint8_t cs_pin, uint16_t addr, uint8_t bsb, 
                     const uint8_t* buf, uint16_t len) {
    digitalWrite(cs_pin, LOW);
    
    SPISettings spi_settings(g_w5500_macraw_spi_speed_hz, MSBFIRST, SPI_MODE0);
    SPI.beginTransaction(spi_settings);
    
    // Address phase
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    
    // Control phase
    SPI.transfer((bsb << 3) | W5500_SPI_WRITE);
    
    // Data phase
    for (uint16_t i = 0; i < len; i++) {
        SPI.transfer(buf[i]);
    }
    
    SPI.endTransaction();
    
    digitalWrite(cs_pin, HIGH);
}

static void w5500_read_buf(uint8_t cs_pin, uint16_t addr, uint8_t bsb, 
                    uint8_t* buf, uint16_t len) {
    digitalWrite(cs_pin, LOW);
    
    SPISettings spi_settings(g_w5500_macraw_spi_speed_hz, MSBFIRST, SPI_MODE0);
    SPI.beginTransaction(spi_settings);
    
    // Address phase
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    
    // Control phase
    SPI.transfer((bsb << 3) | W5500_SPI_READ);
    
    // Data phase
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = SPI.transfer(0x00);
    }
    
    SPI.endTransaction();
    
    digitalWrite(cs_pin, HIGH);
}

static uint16_t w5500_read_reg16(uint8_t cs_pin, uint16_t addr, uint8_t bsb) {
    uint16_t value = w5500_read_reg(cs_pin, addr, bsb) << 8;
    value |= w5500_read_reg(cs_pin, addr + 1, bsb);
    return value;
}

static void w5500_write_reg16(uint8_t cs_pin, uint16_t addr, uint8_t bsb, uint16_t value) {
    w5500_write_reg(cs_pin, addr, bsb, (value >> 8) & 0xFF);
    w5500_write_reg(cs_pin, addr + 1, bsb, value & 0xFF);
}

// ============================================================================
// Инициализация
// ============================================================================

boolean w5500_macraw_init(w5500_macraw_t* w5500, uint8_t cs_pin, uint8_t rst_pin,
                          const uint8_t* mac, uint32_t spi_speed) {
    if (w5500 == NULL || mac == NULL) {
        return false;
    }
    
    w5500->cs_pin = cs_pin;
    w5500->socket = 0;  // Используем socket 0
    w5500->spi_speed = spi_speed;
    g_w5500_macraw_spi_speed_hz = (spi_speed > 0) ? spi_speed : 20000000;
    w5500->initialized = false;
    memcpy(w5500->mac, mac, 6);
    
    Serial.printf("[W5500] Init CS=%d, RST=%d\n", cs_pin, rst_pin);
    
    // Инициализация GPIO
    pinMode(cs_pin, OUTPUT);
    digitalWrite(cs_pin, HIGH);
    
    // Hardware Reset
    if (rst_pin != 255) {
        pinMode(rst_pin, OUTPUT);
        digitalWrite(rst_pin, LOW);
        delayMicroseconds(500);  // Min 500μs reset pulse
        digitalWrite(rst_pin, HIGH);
        delay(2);  // PLL lock time ~2ms
        Serial.println("[W5500] Hardware reset done");
    }
    
    // Настройка Mode Register: Force ARP=0, PPPOE=0, Ping Block=0
    w5500_write_reg(cs_pin, W5500_MR, W5500_BSB_COMMON, 0x00);
    delayMicroseconds(10);
    
    // Записываем MAC address
    Serial.printf("[W5500] Writing MAC: %02X:%02X:%02X:%02X:%02X:%02X\n",
                  mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);
    
    for (int i = 0; i < 6; i++) {
        w5500_write_reg(cs_pin, W5500_SHAR + i, W5500_BSB_COMMON, mac[i]);
    }
    
    delayMicroseconds(10);
    
    Serial.println("[W5500] Init OK");
    w5500->initialized = true;
    
    return true;
}

// ============================================================================
// Socket Operations
// ============================================================================

boolean w5500_macraw_open_socket(w5500_macraw_t* w5500) {
    if (w5500 == NULL || !w5500->initialized) {
        return false;
    }
    
    uint8_t cs = w5500->cs_pin;
    uint8_t sn = w5500->socket;
    uint8_t bsb = W5500_BSB_S0_REG + (sn * 4);  // Socket N Register BSB = 1 + N*4
    
    Serial.printf("[W5500] Opening socket %d in MACRAW mode...\n", sn);
    
    // Установить MACRAW mode
    w5500_write_reg(cs, W5500_Sn_MR, bsb, W5500_Sn_MR_MACRAW);
    delayMicroseconds(10);
    
    // Команда OPEN
    w5500_write_reg(cs, W5500_Sn_CR, bsb, W5500_Sn_CR_OPEN);
    delayMicroseconds(10);
    
    // Ждем выполнения команды (CR должен стать 0x00)
    for (int i = 0; i < 100; i++) {
        uint8_t cr = w5500_read_reg(cs, W5500_Sn_CR, bsb);
        if (cr == 0x00) {
            break;
        }
        delay(1);
    }
    
    // Проверка статуса
    uint8_t status = w5500_read_reg(cs, W5500_Sn_SR, bsb);
    
    if (status == W5500_Sn_SR_MACRAW) {
        Serial.println("[W5500] Socket opened in MACRAW mode SUCCESS");
        return true;
    } else {
        Serial.printf("[W5500] Socket open FAILED, status: 0x%02X\n", status);
        return false;
    }
}

void w5500_macraw_close_socket(w5500_macraw_t* w5500) {
    if (w5500 == NULL || !w5500->initialized) {
        return;
    }
    
    uint8_t cs = w5500->cs_pin;
    uint8_t sn = w5500->socket;
    uint8_t bsb = W5500_BSB_S0_REG + (sn * 4);
    
    // Команда CLOSE
    w5500_write_reg(cs, W5500_Sn_CR, bsb, W5500_Sn_CR_CLOSE);
    
    delay(1);
}

// ============================================================================
// RX Functions
// ============================================================================

uint16_t w5500_macraw_receive(w5500_macraw_t* w5500, uint8_t* frame, uint16_t max_length) {
    if (w5500 == NULL || !w5500->initialized || frame == NULL) {
        return 0;
    }
    
    uint8_t cs = w5500->cs_pin;
    uint8_t sn = w5500->socket;
    uint8_t bsb_reg = W5500_BSB_S0_REG + (sn * 4);
    uint8_t bsb_rx = W5500_BSB_S0_RX + (sn * 4);
    
    // Читаем Sn_RX_RSR (Received Size)
    uint16_t rx_size = w5500_read_reg16(cs, W5500_Sn_RX_RSR, bsb_reg);
    
    if (rx_size == 0) {
        return 0;
    }
    
    // Читаем RX Read Pointer
    uint16_t rx_rd = w5500_read_reg16(cs, W5500_Sn_RX_RD, bsb_reg);
    
    // Читаем packet header (2 bytes = packet size)
    uint8_t size_buf[2];
    w5500_read_buf(cs, rx_rd, bsb_rx, size_buf, 2);
    uint16_t packet_size = (size_buf[0] << 8) | size_buf[1];
    
    // Проверка размера
    if (packet_size == 0 || packet_size > max_length || packet_size > rx_size - 2) {
        return 0;
    }
    
    // Читаем packet data
    w5500_read_buf(cs, rx_rd + 2, bsb_rx, frame, packet_size);
    
    // Обновляем RX Read Pointer
    rx_rd += 2 + packet_size;
    w5500_write_reg16(cs, W5500_Sn_RX_RD, bsb_reg, rx_rd);
    
    // Команда RECV
    w5500_write_reg(cs, W5500_Sn_CR, bsb_reg, W5500_Sn_CR_RECV);
    
    return packet_size;
}

// ============================================================================
// TX Functions
// ============================================================================

boolean w5500_macraw_send(w5500_macraw_t* w5500, const uint8_t* frame, uint16_t length) {
    if (w5500 == NULL || !w5500->initialized || frame == NULL || length == 0) {
        return false;
    }
    
    uint8_t cs = w5500->cs_pin;
    uint8_t sn = w5500->socket;
    uint8_t bsb_reg = W5500_BSB_S0_REG + (sn * 4);
    uint8_t bsb_tx = W5500_BSB_S0_TX + (sn * 4);
    
    // Проверка свободного места в TX буфере
    uint16_t tx_free = w5500_read_reg16(cs, W5500_Sn_TX_FSR, bsb_reg);
    if (tx_free < length) {
        return false;  // Недостаточно места
    }
    
    // Читаем TX Write Pointer
    uint16_t tx_wr = w5500_read_reg16(cs, W5500_Sn_TX_WR, bsb_reg);
    
    // Записываем данные в TX буфер
    w5500_write_buf(cs, tx_wr, bsb_tx, frame, length);
    
    // Обновляем TX Write Pointer
    tx_wr += length;
    w5500_write_reg16(cs, W5500_Sn_TX_WR, bsb_reg, tx_wr);
    
    // Команда SEND
    w5500_write_reg(cs, W5500_Sn_CR, bsb_reg, W5500_Sn_CR_SEND);
    
    return true;
}
