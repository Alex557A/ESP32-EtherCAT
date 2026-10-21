/*
 * MCP23S17 SPI GPIO Expander Driver - Implementation
 * 
 * Phase 5, Week 8, Day 53-55
 * ESP32 EtherCAT Slave v4
 * 
 * MCP23S17 - 16-bit SPI GPIO Expander
 * - 16 GPIO портов (8 портов A, 8 портов B)
 * - Вход/выход настраивается индивидуально
 * - SPI интерфейс (до 10 MHz)
 */

#include "mcp23s17.h"
#include <Arduino.h>
#include <SPI.h>
#include <string.h>

extern "C" {

// ============================================================================
// SPI Bus 2 (для периферии)
// ============================================================================

// ESP32-S3 имеет несколько SPI интерфейсов
// Используем HSPI для второй шины (общий для MCP23S17 и MCP2515)
SPIClass* g_spi2 = NULL;  // extern для доступа из mcp2515.cpp
static SPISettings g_spi2_settings(10000000, MSBFIRST, SPI_MODE0);  // 10 MHz для периферии

// Инициализация SPI Bus 2 (вызывается из main.cpp)
void mcp23s17_spi2_init(uint8_t sck_pin, uint8_t miso_pin, uint8_t mosi_pin) {
    if (g_spi2 == NULL) {
        g_spi2 = new SPIClass(HSPI);
        g_spi2->begin(sck_pin, miso_pin, mosi_pin, -1);  // -1 = manual CS
        Serial.printf("[MCP23S17] SPI Bus 2 initialized: SCK=%d, MISO=%d, MOSI=%d\n", 
                      sck_pin, miso_pin, mosi_pin);
    }
}

// ============================================================================
// Low-level SPI Functions
// ============================================================================

static void mcp23s17_spi_begin(uint8_t cs_pin) {
    if (g_spi2 == NULL) {
        Serial.println("[MCP23S17] ERROR: SPI Bus 2 not initialized!");
        return;
    }
    digitalWrite(cs_pin, LOW);
    delayMicroseconds(1);
    g_spi2->beginTransaction(g_spi2_settings);
}

static void mcp23s17_spi_end(uint8_t cs_pin) {
    if (g_spi2 == NULL) {
        return;
    }
    g_spi2->endTransaction();
    delayMicroseconds(1);
    digitalWrite(cs_pin, HIGH);
}

static uint8_t mcp23s17_spi_transfer(uint8_t data) {
    if (g_spi2 == NULL) {
        return 0;
    }
    return g_spi2->transfer(data);
}

// ============================================================================
// Register Read/Write
// ============================================================================

static uint8_t mcp23s17_read_register(mcp23s17_state_t* state, uint8_t reg) {
    if (state == NULL || !state->config.initialized) {
        return 0;
    }
    
    mcp23s17_spi_begin(state->config.cs_pin);
    
    // MCP23S17 SPI Protocol:
    // Byte 1: Device Opcode (0x40 for write, 0x41 for read) + Hardware Address (A2, A1, A0)
    // Byte 2: Register Address
    // Byte 3: Data (for write) or dummy (for read)
    
    // Для MCP23S17 с одним устройством на шине, адрес = 0x00
    uint8_t opcode = MCP23S17_CMD_READ | 0x00;  // Read command + address 0
    
    mcp23s17_spi_transfer(opcode);
    mcp23s17_spi_transfer(reg);
    uint8_t value = mcp23s17_spi_transfer(0x00);  // Dummy byte for read
    
    mcp23s17_spi_end(state->config.cs_pin);
    
    return value;
}

static void mcp23s17_write_register(mcp23s17_state_t* state, uint8_t reg, uint8_t value) {
    if (state == NULL || !state->config.initialized) {
        return;
    }
    
    mcp23s17_spi_begin(state->config.cs_pin);
    
    // MCP23S17 SPI Protocol
    uint8_t opcode = MCP23S17_CMD_WRITE | 0x00;  // Write command + address 0
    
    mcp23s17_spi_transfer(opcode);
    mcp23s17_spi_transfer(reg);
    mcp23s17_spi_transfer(value);
    
    mcp23s17_spi_end(state->config.cs_pin);
}

// ============================================================================
// Initialization
// ============================================================================

boolean mcp23s17_init(mcp23s17_state_t* state, const mcp23s17_config_t* config) {
    if (state == NULL || config == NULL) {
        Serial.println("[MCP23S17] Init: NULL pointer");
        return false;
    }
    
    if (g_spi2 == NULL) {
        Serial.println("[MCP23S17] ERROR: SPI Bus 2 not initialized! Call mcp23s17_spi2_init() first.");
        return false;
    }
    
    memset(state, 0, sizeof(mcp23s17_state_t));
    memcpy(&state->config, config, sizeof(mcp23s17_config_t));
    
    // Настройка CS pin
    pinMode(config->cs_pin, OUTPUT);
    digitalWrite(config->cs_pin, HIGH);
    
    delay(10);  // Дать время на инициализацию
    
    // Настройка IOCON register
    // HAEN = 1 (Hardware Address Enable для MCP23S17)
    // SEQOP = 0 (Sequential operation enabled)
    // MIRROR = 0 (INT pins not mirrored)
    uint8_t iocon = MCP23S17_IOCON_HAEN;  // Hardware address enable
    mcp23s17_write_register(state, MCP23S17_REG_IOCON, iocon);
    
    // Настройка направления портов
    if (!mcp23s17_set_direction(state, config->port_direction)) {
        Serial.println("[MCP23S17] Failed to set direction");
        return false;
    }
    
    // Настройка pull-up резисторов
    if (!mcp23s17_set_pullup(state, config->port_pullup)) {
        Serial.println("[MCP23S17] Failed to set pullup");
        return false;
    }
    
    // Настройка инверсии логики
    if (!mcp23s17_set_invert(state, config->port_invert)) {
        Serial.println("[MCP23S17] Failed to set invert");
        return false;
    }
    
    // Чтение начального состояния
    state->current_state = mcp23s17_read_port(state);
    state->previous_state = state->current_state;
    
    state->config.initialized = true;
    
    Serial.printf("[MCP23S17] Initialized: CS=%d, Direction=0x%04X, Pullup=0x%04X, Invert=0x%04X\n",
                  config->cs_pin, config->port_direction, config->port_pullup, config->port_invert);
    
    return true;
}

// ============================================================================
// Configuration
// ============================================================================

boolean mcp23s17_set_direction(mcp23s17_state_t* state, uint16 direction) {
    if (state == NULL || !state->config.initialized) {
        return false;
    }
    
    // Направление: 0 = выход, 1 = вход
    uint8_t dir_a = direction & 0xFF;        // Порты A (0-7)
    uint8_t dir_b = (direction >> 8) & 0xFF; // Порты B (8-15)
    
    mcp23s17_write_register(state, MCP23S17_REG_IODIRA, dir_a);
    mcp23s17_write_register(state, MCP23S17_REG_IODIRB, dir_b);
    
    state->config.port_direction = direction;
    
    return true;
}

boolean mcp23s17_set_pullup(mcp23s17_state_t* state, uint16 pullup) {
    if (state == NULL || !state->config.initialized) {
        return false;
    }
    
    uint8_t pullup_a = pullup & 0xFF;
    uint8_t pullup_b = (pullup >> 8) & 0xFF;
    
    mcp23s17_write_register(state, MCP23S17_REG_GPPUA, pullup_a);
    mcp23s17_write_register(state, MCP23S17_REG_GPPUB, pullup_b);
    
    state->config.port_pullup = pullup;
    
    return true;
}

boolean mcp23s17_set_invert(mcp23s17_state_t* state, uint16 invert) {
    if (state == NULL || !state->config.initialized) {
        return false;
    }
    
    uint8_t invert_a = invert & 0xFF;
    uint8_t invert_b = (invert >> 8) & 0xFF;
    
    mcp23s17_write_register(state, MCP23S17_REG_IPOLA, invert_a);
    mcp23s17_write_register(state, MCP23S17_REG_IPOLB, invert_b);
    
    state->config.port_invert = invert;
    
    return true;
}

// ============================================================================
// Read/Write Operations
// ============================================================================

uint16 mcp23s17_read_port(mcp23s17_state_t* state) {
    if (state == NULL || !state->config.initialized) {
        return 0;
    }
    
    uint8_t port_a = mcp23s17_read_register(state, MCP23S17_REG_GPIOA);
    uint8_t port_b = mcp23s17_read_register(state, MCP23S17_REG_GPIOB);
    
    uint16 value = (port_b << 8) | port_a;
    
    return value;
}

boolean mcp23s17_write_port(mcp23s17_state_t* state, uint16 value) {
    if (state == NULL || !state->config.initialized) {
        return false;
    }
    
    uint8_t port_a = value & 0xFF;
    uint8_t port_b = (value >> 8) & 0xFF;
    
    mcp23s17_write_register(state, MCP23S17_REG_GPIOA, port_a);
    mcp23s17_write_register(state, MCP23S17_REG_GPIOB, port_b);
    
    return true;
}

boolean mcp23s17_read_pin(mcp23s17_state_t* state, uint8 pin) {
    if (state == NULL || !state->config.initialized || pin >= MCP23S17_MAX_PORTS) {
        return false;
    }
    
    uint16 port_value = mcp23s17_read_port(state);
    boolean pin_value = (port_value >> pin) & 0x01;
    
    // Применить инверсию, если настроена
    if ((state->config.port_invert >> pin) & 0x01) {
        pin_value = !pin_value;
    }
    
    return pin_value;
}

boolean mcp23s17_write_pin(mcp23s17_state_t* state, uint8 pin, boolean value) {
    if (state == NULL || !state->config.initialized || pin >= MCP23S17_MAX_PORTS) {
        return false;
    }
    
    // Читаем текущее состояние порта
    uint16 port_value = mcp23s17_read_port(state);
    
    // Устанавливаем/сбрасываем бит
    if (value) {
        port_value |= (1 << pin);
    } else {
        port_value &= ~(1 << pin);
    }
    
    // Записываем обратно
    return mcp23s17_write_port(state, port_value);
}

// ============================================================================
// Edge Detection
// ============================================================================

void mcp23s17_update(mcp23s17_state_t* state) {
    if (state == NULL || !state->config.initialized) {
        return;
    }
    
    // Сохраняем предыдущее состояние
    state->previous_state = state->current_state;
    
    // Читаем текущее состояние
    state->current_state = mcp23s17_read_port(state);
}

int8 mcp23s17_get_edge(mcp23s17_state_t* state, uint8 pin) {
    if (state == NULL || !state->config.initialized || pin >= MCP23S17_MAX_PORTS) {
        return 0;
    }
    
    boolean prev = (state->previous_state >> pin) & 0x01;
    boolean curr = (state->current_state >> pin) & 0x01;
    
    // Применить инверсию, если настроена
    if ((state->config.port_invert >> pin) & 0x01) {
        prev = !prev;
        curr = !curr;
    }
    
    if (curr && !prev) {
        return 1;  // Нажатие (0→1)
    } else if (!curr && prev) {
        return -1;  // Отпускание (1→0)
    }
    
    return 0;  // Нет изменений
}

// ============================================================================
// Deinitialization
// ============================================================================

void mcp23s17_deinit(mcp23s17_state_t* state) {
    if (state == NULL) {
        return;
    }
    
    state->config.initialized = false;
    state->config.enabled = false;
    
    Serial.println("[MCP23S17] Deinitialized");
}

} // extern "C"
