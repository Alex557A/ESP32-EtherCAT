/*
 * W5500 TCP/IP Driver - Implementation
 * 
 * Phase 3, Week 6, Day 37-38
 * ESP32 EtherCAT Slave v4
 * 
 * Используется Socket 1 для Telnet (параллельно с Socket 0 для EtherCAT)
 */

#include "w5500_tcp.h"
#include "w5500_macraw.h"  // Используем константы
#include <Arduino.h>
#include <SPI.h>
#include <string.h>

extern "C" {

// SPI speed for W5500 TCP access (defaults to 20MHz).
// NOTE: SPI bus/pins должны быть инициализированы на уровне приложения (main.cpp).
static uint32_t g_w5500_tcp_spi_speed_hz = 20000000;

// Low-level функции (копируем из w5500_macraw.cpp, но для Socket 1)
static void w5500_tcp_write_reg(uint8_t cs_pin, uint16_t addr, uint8_t bsb, uint8_t data) {
    digitalWrite(cs_pin, LOW);
    
    SPISettings spi_settings(g_w5500_tcp_spi_speed_hz, MSBFIRST, SPI_MODE0);
    SPI.beginTransaction(spi_settings);
    
    // Address phase
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    
    // Control phase (BSB + Write)
    SPI.transfer((bsb << 3) | 0x04);  // 0x04 = Write
    
    // Data phase
    SPI.transfer(data);
    
    SPI.endTransaction();
    digitalWrite(cs_pin, HIGH);
}

static uint8_t w5500_tcp_read_reg(uint8_t cs_pin, uint16_t addr, uint8_t bsb) {
    digitalWrite(cs_pin, LOW);
    
    SPISettings spi_settings(g_w5500_tcp_spi_speed_hz, MSBFIRST, SPI_MODE0);
    SPI.beginTransaction(spi_settings);
    
    // Address phase
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    
    // Control phase (BSB + Read)
    SPI.transfer((bsb << 3) | 0x00);  // 0x00 = Read
    
    // Data phase
    uint8_t data = SPI.transfer(0x00);
    
    SPI.endTransaction();
    digitalWrite(cs_pin, HIGH);
    
    return data;
}

static uint16_t w5500_tcp_read_reg16(uint8_t cs_pin, uint16_t addr, uint8_t bsb) {
    uint16_t value = w5500_tcp_read_reg(cs_pin, addr, bsb) << 8;
    value |= w5500_tcp_read_reg(cs_pin, addr + 1, bsb);
    return value;
}

static void w5500_tcp_write_reg16(uint8_t cs_pin, uint16_t addr, uint8_t bsb, uint16_t value) {
    w5500_tcp_write_reg(cs_pin, addr, bsb, (value >> 8) & 0xFF);
    w5500_tcp_write_reg(cs_pin, addr + 1, bsb, value & 0xFF);
}

static void w5500_tcp_read_buf(uint8_t cs_pin, uint16_t addr, uint8_t bsb, uint8_t* buf, uint16_t len) {
    digitalWrite(cs_pin, LOW);
    
    SPISettings spi_settings(g_w5500_tcp_spi_speed_hz, MSBFIRST, SPI_MODE0);
    SPI.beginTransaction(spi_settings);
    
    // Address phase
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    
    // Control phase
    SPI.transfer((bsb << 3) | 0x00);  // Read
    
    // Data phase
    for (uint16_t i = 0; i < len; i++) {
        buf[i] = SPI.transfer(0x00);
    }
    
    SPI.endTransaction();
    digitalWrite(cs_pin, HIGH);
}

static void w5500_tcp_write_buf(uint8_t cs_pin, uint16_t addr, uint8_t bsb, const uint8_t* buf, uint16_t len) {
    digitalWrite(cs_pin, LOW);
    
    SPISettings spi_settings(g_w5500_tcp_spi_speed_hz, MSBFIRST, SPI_MODE0);
    SPI.beginTransaction(spi_settings);
    
    // Address phase
    SPI.transfer((addr >> 8) & 0xFF);
    SPI.transfer(addr & 0xFF);
    
    // Control phase
    SPI.transfer((bsb << 3) | 0x04);  // Write
    
    // Data phase
    for (uint16_t i = 0; i < len; i++) {
        SPI.transfer(buf[i]);
    }
    
    SPI.endTransaction();
    digitalWrite(cs_pin, HIGH);
}

// ============================================================================
// Инициализация
// ============================================================================

boolean w5500_tcp_init(w5500_tcp_t* w5500, 
                       uint8_t cs_pin, 
                       uint8_t rst_pin,
                       const uint8_t* mac, 
                       const uint8_t* ip,
                       const uint8_t* subnet,
                       const uint8_t* gateway,
                       uint32_t spi_speed) {
    if (w5500 == NULL || mac == NULL || ip == NULL || subnet == NULL || gateway == NULL) {
        return false;
    }
    
    w5500->cs_pin = cs_pin;
    w5500->socket = 1;  // Socket 1 для Telnet
    w5500->spi_speed = spi_speed;
    w5500->initialized = false;
    w5500->listening = false;
    w5500->connected = false;
    memcpy(w5500->mac, mac, 6);
    memcpy(w5500->ip, ip, 4);
    memcpy(w5500->subnet, subnet, 4);
    memcpy(w5500->gateway, gateway, 4);
    
    // Используем скорость из параметра для всех транзакций этого драйвера.
    // (Ожидается, что EtherCAT MACRAW и Telnet TCP используют одинаковую частоту.)
    g_w5500_tcp_spi_speed_hz = (spi_speed > 0) ? spi_speed : 20000000;
    
    // Serial.printf("[W5500 TCP] Init CS=%d, RST=%d (Socket 1)\n", cs_pin, rst_pin);
    // Serial.printf("[W5500 TCP] IP: %d.%d.%d.%d\n", ip[0], ip[1], ip[2], ip[3]);
    
    // Инициализация GPIO
    pinMode(cs_pin, OUTPUT);
    digitalWrite(cs_pin, HIGH);
    
    // Hardware Reset (если используется)
    if (rst_pin != 255) {
        pinMode(rst_pin, OUTPUT);
        digitalWrite(rst_pin, LOW);
        delayMicroseconds(500);
        digitalWrite(rst_pin, HIGH);
        delay(2);
        // Serial.println("[W5500 TCP] Hardware reset done");
    }
    
    // Software Reset (Common Register)
    w5500_tcp_write_reg(cs_pin, 0x0000, 0x00, 0x80);  // MR RST bit (BSB_COMMON = 0x00)
    delay(1);
    
    // Проверка версии чипа
    uint8_t version = w5500_tcp_read_reg(cs_pin, 0x0039, 0x00);  // BSB_COMMON = 0x00
    // Serial.printf("[W5500 TCP] Chip version: 0x%02X\n", version);
    
    // Настройка сети (Common Registers, BSB = 0x00)
    // Serial.println("[W5500 TCP] Configuring network...");
    
    // MAC address (0x0009-0x000E)
    for (int i = 0; i < 6; i++) {
        w5500_tcp_write_reg(cs_pin, 0x0009 + i, 0x00, mac[i]);
    }
    
    // Gateway (0x0001-0x0004)
    for (int i = 0; i < 4; i++) {
        w5500_tcp_write_reg(cs_pin, 0x0001 + i, 0x00, gateway[i]);
    }
    
    // Subnet mask (0x0005-0x0008)
    for (int i = 0; i < 4; i++) {
        w5500_tcp_write_reg(cs_pin, 0x0005 + i, 0x00, subnet[i]);
    }
    
    // IP address (0x000F-0x0012)
    for (int i = 0; i < 4; i++) {
        w5500_tcp_write_reg(cs_pin, 0x000F + i, 0x00, ip[i]);
    }
    
    delayMicroseconds(10);
    
    // Проверка настроек
    uint8_t read_ip[4];
    for (int i = 0; i < 4; i++) {
        read_ip[i] = w5500_tcp_read_reg(cs_pin, 0x000F + i, 0x00);
    }
    Serial.printf("[W5500 TCP] Read IP: %d.%d.%d.%d\n", 
                  read_ip[0], read_ip[1], read_ip[2], read_ip[3]);
    
    w5500->initialized = true;
    Serial.println("[W5500 TCP] Init OK");
    
    return true;
}

// ============================================================================
// TCP Server Operations
// ============================================================================

boolean w5500_tcp_listen(w5500_tcp_t* w5500, uint16_t port) {
    if (w5500 == NULL || !w5500->initialized) {
        return false;
    }
    
    uint8_t cs = w5500->cs_pin;
    uint8_t sn = w5500->socket;  // Socket 1
    uint8_t bsb = W5500_BSB_S1_REG;
    
    w5500->port = port;
    
    Serial.printf("[W5500 TCP] Opening socket %d in TCP mode (port %d)...\n", sn, port);
    
    // Проверить текущий статус socket
    uint8_t current_status = w5500_tcp_read_reg(cs, W5500_Sn_SR, bsb);
    Serial.printf("[W5500 TCP] Current socket %d status: 0x%02X\n", sn, current_status);
    
    // Закрыть socket если открыт (не CLOSED)
    if (current_status != W5500_Sn_SR_CLOSED) {
        // Serial.printf("[W5500 TCP] Closing socket %d (current status: 0x%02X)...\n", sn, current_status);
        w5500_tcp_write_reg(cs, W5500_Sn_CR, bsb, W5500_Sn_CR_CLOSE);
        delay(10);  // Увеличена задержка для закрытия
        
        // Ждем закрытия
        for (int i = 0; i < 100; i++) {
            uint8_t cr = w5500_tcp_read_reg(cs, W5500_Sn_CR, bsb);
            if (cr == 0x00) {
                break;
            }
            delay(1);
        }
        
        // Проверка, что socket закрыт
        current_status = w5500_tcp_read_reg(cs, W5500_Sn_SR, bsb);
        if (current_status != W5500_Sn_SR_CLOSED) {
            // Serial.printf("[W5500 TCP] WARNING: Socket %d not closed, status: 0x%02X\n", sn, current_status);
        }
    }
    
    delay(5);  // Дополнительная задержка после закрытия
    
    // Установить TCP mode
    w5500_tcp_write_reg(cs, W5500_Sn_MR, bsb, W5500_Sn_MR_TCP);
    delay(1);
    
    // Установить порт (Sn_PORT = 0x0004-0x0005)
    w5500_tcp_write_reg(cs, W5500_Sn_PORT, bsb, (port >> 8) & 0xFF);
    w5500_tcp_write_reg(cs, W5500_Sn_PORT + 1, bsb, port & 0xFF);
    delay(1);
    
    // Команда OPEN (Sn_CR = 0x0001)
    w5500_tcp_write_reg(cs, W5500_Sn_CR, bsb, 0x01);  // OPEN
    delay(1);
    
    // Ждем выполнения команды
    for (int i = 0; i < 200; i++) {  // Увеличено до 200ms
        uint8_t cr = w5500_tcp_read_reg(cs, W5500_Sn_CR, bsb);
        if (cr == 0x00) {
            break;
        }
        delay(1);
    }
    
    // Проверка статуса (Sn_SR = 0x0003)
    uint8_t status = w5500_tcp_read_reg(cs, W5500_Sn_SR, bsb);
    // Serial.printf("[W5500 TCP] Socket %d status after OPEN: 0x%02X\n", sn, status);
    
    if (status != W5500_Sn_SR_INIT) {
        Serial.printf("[W5500 TCP] Socket open FAILED, expected INIT (0x%02X), got: 0x%02X\n", 
                     W5500_Sn_SR_INIT, status);
        return false;
    }
    
    // Serial.println("[W5500 TCP] Socket opened (INIT)");
    
    // Команда LISTEN
    w5500_tcp_write_reg(cs, W5500_Sn_CR, bsb, W5500_Sn_CR_LISTEN);
    delayMicroseconds(10);
    
    // Ждем выполнения команды
    for (int i = 0; i < 100; i++) {
        uint8_t cr = w5500_tcp_read_reg(cs, W5500_Sn_CR, bsb);
        if (cr == 0x00) {
            break;
        }
        delay(1);
    }
    
    // Проверка статуса
    status = w5500_tcp_read_reg(cs, W5500_Sn_SR, bsb);
    Serial.printf("[W5500 TCP] Socket %d status after LISTEN: 0x%02X (expected 0x%02X)\n", 
                 sn, status, W5500_Sn_SR_LISTEN);
    if (status != W5500_Sn_SR_LISTEN) {
        Serial.printf("[W5500 TCP] Listen FAILED, status: 0x%02X\n", status);
        return false;
    }
    
    w5500->listening = true;
    Serial.println("[W5500 TCP] Socket LISTENING SUCCESS");
    
    return true;
}

boolean w5500_tcp_check_connection(w5500_tcp_t* w5500) {
    if (w5500 == NULL || !w5500->listening) {
        return false;
    }
    
    uint8_t cs = w5500->cs_pin;
    uint8_t bsb = W5500_BSB_S1_REG;
    
    // Проверка interrupt register (Sn_IR = 0x0002)
    uint8_t ir = w5500_tcp_read_reg(cs, W5500_Sn_IR, bsb);
    
    if (ir & W5500_Sn_IR_CON) {
        // Новое соединение
        w5500_tcp_write_reg(cs, W5500_Sn_IR, bsb, W5500_Sn_IR_CON);  // Clear interrupt
        return true;
    }
    
    return false;
}

boolean w5500_tcp_accept(w5500_tcp_t* w5500) {
    if (w5500 == NULL || !w5500->listening) {
        return false;
    }
    
    uint8_t cs = w5500->cs_pin;
    uint8_t bsb = W5500_BSB_S1_REG;
    
    // Проверка статуса
    uint8_t status = w5500_tcp_read_reg(cs, W5500_Sn_SR, bsb);
    
    // Если пришло новое соединение (SYN_RECV), нужно отправить команду ACCEPT
    if (status == W5500_Sn_SR_SYN_RECV) {
        // Отправляем команду ACCEPT для принятия соединения
        w5500_tcp_write_reg(cs, W5500_Sn_CR, bsb, W5500_Sn_CR_ACCEPT);
        delay(1);
        
        // Ждем выполнения команды
        for (int i = 0; i < 100; i++) {
            uint8_t cr = w5500_tcp_read_reg(cs, W5500_Sn_CR, bsb);
            if (cr == 0x00) {
                break;
            }
            delay(1);
        }
        
        // Проверяем, что соединение установлено
        status = w5500_tcp_read_reg(cs, W5500_Sn_SR, bsb);
        if (status == W5500_Sn_SR_ESTABLISHED) {
            w5500->connected = true;
            Serial.println("[W5500 TCP] Connection accepted and established!");
            return true;
        } else {
            Serial.printf("[W5500 TCP] ACCEPT failed, status: 0x%02X\n", status);
            return false;
        }
    }
    
    if (status == W5500_Sn_SR_ESTABLISHED) {
        if (!w5500->connected) {
            Serial.println("[W5500 TCP] Client connected!");
            w5500->connected = true;
        }
        return true;
    } else if (status == W5500_Sn_SR_CLOSE_WAIT) {
        // Клиент инициировал закрытие, нужно закрыть с нашей стороны
        if (w5500->connected) {
            Serial.println("[W5500 TCP] Client initiated close (CLOSE_WAIT), closing socket...");
            w5500->connected = false;
        }
        // Отправляем команду CLOSE для завершения закрытия
        w5500_tcp_write_reg(cs, W5500_Sn_CR, bsb, W5500_Sn_CR_CLOSE);
        delay(10);
        // Ждем закрытия
        for (int i = 0; i < 100; i++) {
            uint8_t cr = w5500_tcp_read_reg(cs, W5500_Sn_CR, bsb);
            if (cr == 0x00) {
                break;
            }
            delay(1);
        }
        return false;
    } else if (status == W5500_Sn_SR_CLOSED) {
        if (w5500->connected) {
            Serial.println("[W5500 TCP] Client disconnected (CLOSED)");
            w5500->connected = false;
        }
        // Если сокет закрыт, но мы должны слушать, переоткрываем
        if (w5500->listening) {
            Serial.println("[W5500 TCP] Reopening socket and returning to LISTEN...");
            // Переоткрываем сокет и возвращаемся в LISTEN
            w5500_tcp_write_reg(cs, W5500_Sn_MR, bsb, W5500_Sn_MR_TCP);
            delay(1);
            w5500_tcp_write_reg(cs, W5500_Sn_PORT, bsb, (w5500->port >> 8) & 0xFF);
            w5500_tcp_write_reg(cs, W5500_Sn_PORT + 1, bsb, w5500->port & 0xFF);
            delay(1);
            w5500_tcp_write_reg(cs, W5500_Sn_CR, bsb, W5500_Sn_CR_OPEN);
            delay(10);
            // Ждем INIT
            for (int i = 0; i < 100; i++) {
                status = w5500_tcp_read_reg(cs, W5500_Sn_SR, bsb);
                if (status == W5500_Sn_SR_INIT) {
                    break;
                }
                delay(1);
            }
            if (status == W5500_Sn_SR_INIT) {
                // Возвращаемся в LISTEN
                w5500_tcp_write_reg(cs, W5500_Sn_CR, bsb, W5500_Sn_CR_LISTEN);
                delay(10);
                status = w5500_tcp_read_reg(cs, W5500_Sn_SR, bsb);
                if (status == W5500_Sn_SR_LISTEN) {
                    Serial.println("[W5500 TCP] Socket returned to LISTEN mode");
                } else {
                    Serial.printf("[W5500 TCP] Failed to return to LISTEN, status: 0x%02X\n", status);
                }
            } else {
                Serial.printf("[W5500 TCP] Failed to reopen socket, status: 0x%02X\n", status);
            }
        }
        return false;
    } else if (status == W5500_Sn_SR_LISTEN) {
        // Сокет в режиме LISTEN, но не подключен
        if (w5500->connected) {
            w5500->connected = false;
        }
        return false;
    }
    
    return w5500->connected;
}

uint16_t w5500_tcp_available(w5500_tcp_t* w5500) {
    if (w5500 == NULL || !w5500->connected) {
        return 0;
    }
    
    uint8_t cs = w5500->cs_pin;
    uint8_t bsb = W5500_BSB_S1_REG;
    
    // Sn_RX_RSR (0x0026-0x0027) - Received Size
    uint16_t available = w5500_tcp_read_reg16(cs, W5500_Sn_RX_RSR, bsb);
    
    return available;
}

uint16_t w5500_tcp_recv(w5500_tcp_t* w5500, uint8_t* buffer, uint16_t max_len) {
    if (w5500 == NULL || !w5500->connected || buffer == NULL || max_len == 0) {
        return 0;
    }
    
    uint16_t available = w5500_tcp_available(w5500);
    if (available == 0) {
        return 0;
    }
    
    uint8_t cs = w5500->cs_pin;
    uint8_t bsb_reg = W5500_BSB_S1_REG;
    uint8_t bsb_rx = W5500_BSB_S1_RX;
    
    // Ограничить чтение max_len
    uint16_t to_read = (available > max_len) ? max_len : available;
    
    // Прочитать RX Read Pointer (Sn_RX_RD = 0x0028-0x0029)
    uint16_t rx_rd = w5500_tcp_read_reg16(cs, W5500_Sn_RX_RD, bsb_reg);
    
    // Прочитать данные из RX buffer
    w5500_tcp_read_buf(cs, rx_rd, bsb_rx, buffer, to_read);
    
    // Обновить RX Read Pointer
    rx_rd += to_read;
    w5500_tcp_write_reg16(cs, W5500_Sn_RX_RD, bsb_reg, rx_rd);
    
    // Команда RECV (Sn_CR = 0x40)
    w5500_tcp_write_reg(cs, W5500_Sn_CR, bsb_reg, 0x40);  // RECV
    delay(1);
    
    // Ждем выполнения команды RECV
    for (int i = 0; i < 50; i++) {
        uint8_t cr = w5500_tcp_read_reg(cs, W5500_Sn_CR, bsb_reg);
        if (cr == 0x00) {
            break;
        }
        delay(1);
    }
    
    return to_read;
}

boolean w5500_tcp_send(w5500_tcp_t* w5500, const uint8_t* data, uint16_t len) {
    if (w5500 == NULL || !w5500->connected || data == NULL || len == 0) {
        Serial.printf("[W5500 TCP] Send failed: w5500=%p, connected=%d, data=%p, len=%d\n", 
                     w5500, w5500 ? w5500->connected : 0, data, len);
        return false;
    }
    
    uint8_t cs = w5500->cs_pin;
    uint8_t bsb_reg = W5500_BSB_S1_REG;
    uint8_t bsb_tx = W5500_BSB_S1_TX;
    
    // Проверка статуса соединения перед отправкой
    // Даем небольшую задержку для стабилизации статуса после чтения данных
    uint8_t status = w5500_tcp_read_reg(cs, W5500_Sn_SR, bsb_reg);
    if (status != W5500_Sn_SR_ESTABLISHED) {
        // Если статус не ESTABLISHED, ждем немного и проверяем снова (максимум 10ms)
        for (int wait = 0; wait < 10; wait++) {
            delay(1);
            status = w5500_tcp_read_reg(cs, W5500_Sn_SR, bsb_reg);
            if (status == W5500_Sn_SR_ESTABLISHED) {
                break;
            }
        }
        if (status != W5500_Sn_SR_ESTABLISHED) {
            Serial.printf("[W5500 TCP] Send failed: not ESTABLISHED, status=0x%02X\n", status);
            return false;
        }
    }
    
    // Ждем освобождения TX buffer (максимум 100ms)
    uint16_t tx_free = 0;
    for (int wait = 0; wait < 100; wait++) {
        tx_free = w5500_tcp_read_reg16(cs, W5500_Sn_TX_FSR, bsb_reg);
        if (tx_free >= len) {
            break;
        }
        delay(1);  // Ждем 1ms перед следующей проверкой
    }
    
    if (tx_free < len) {
        Serial.printf("[W5500 TCP] TX buffer full: %d < %d\n", tx_free, len);
        return false;
    }
    
    // Прочитать TX Write Pointer (Sn_TX_WR = 0x0024-0x0025)
    uint16_t tx_wr = w5500_tcp_read_reg16(cs, W5500_Sn_TX_WR, bsb_reg);
    
    // Записать данные в TX buffer
    w5500_tcp_write_buf(cs, tx_wr, bsb_tx, data, len);
    
    // Обновить TX Write Pointer
    tx_wr += len;
    w5500_tcp_write_reg16(cs, W5500_Sn_TX_WR, bsb_reg, tx_wr);
    
    // Команда SEND (Sn_CR = 0x20)
    w5500_tcp_write_reg(cs, W5500_Sn_CR, bsb_reg, 0x20);  // SEND
    
    // Ждем завершения отправки
    for (int i = 0; i < 200; i++) {
        uint8_t cr = w5500_tcp_read_reg(cs, W5500_Sn_CR, bsb_reg);
        if (cr == 0x00) {
            delay(5);  // Дополнительная задержка для завершения TCP отправки
            Serial.printf("[W5500 TCP] Data sent successfully: %d bytes\n", len);
            return true;
        }
        delay(1);
    }
    
    Serial.println("[W5500 TCP] Send timeout!");
    return false;
}

void w5500_tcp_disconnect(w5500_tcp_t* w5500) {
    if (w5500 == NULL || !w5500->connected) {
        return;
    }
    
    uint8_t cs = w5500->cs_pin;
    uint8_t bsb = W5500_BSB_S1_REG;
    
    // Команда DISCON
    w5500_tcp_write_reg(cs, W5500_Sn_CR, bsb, W5500_Sn_CR_DISCON);
    delay(10);
    
    w5500->connected = false;
    Serial.println("[W5500 TCP] Disconnected");
}

void w5500_tcp_close(w5500_tcp_t* w5500) {
    if (w5500 == NULL) {
        return;
    }
    
    uint8_t cs = w5500->cs_pin;
    uint8_t bsb = W5500_BSB_S1_REG;
    
    // Команда CLOSE
    w5500_tcp_write_reg(cs, W5500_Sn_CR, bsb, W5500_Sn_CR_CLOSE);
    delay(10);
    
    w5500->listening = false;
    w5500->connected = false;
    Serial.println("[W5500 TCP] Socket closed");
}

} // extern "C"
