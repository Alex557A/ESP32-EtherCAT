/*
 * Device Profiles
 * Профили устройств ESP32 с картами пинов и периферии
 * 
 * ESP32 EtherCAT Slave v4
 */

class DeviceProfiles {
    constructor() {
        this.profiles = new Map();
        this.loadDefaultProfiles();
    }

    /**
     * Загрузка профилей по умолчанию
     */
    loadDefaultProfiles() {
        // ESP32-S3 Profile
        this.profiles.set('ESP32-S3', {
            chip: 'ESP32-S3',
            variant: 'esp32s3',
            pins: {
                // GPIO pins (ESP32-S3)
                gpio: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48],
                // SPI Bus 1 (EtherCAT)
                spi1: { sck: 36, miso: 37, mosi: 35 },
                // SPI Bus 2 (Peripherals)
                spi2: { sck: 17, miso: 16, mosi: 15 },
                // W5500 IN (EtherCAT RX)
                w5500_in: { cs: 40, rst: 39 },
                // W5500 OUT (EtherCAT TX)
                w5500_out: { cs: 38, rst: 41 },
                // USB (ESP32-S3 only)
                usb: { dp: 20, dm: 19 },
                // CAN (MCP2515)
                can: { cs: 18, int: null },
                // MCP23S17 (IO Expansion)
                mcp23s17: { cs: 14 }
            },
            peripherals: {
                usb: true,        // USB HID Keyboard support
                wifi: true,       // WiFi support
                bluetooth: true,  // Bluetooth support
                ethernet: true,   // W5500 support
                can: true,        // MCP2515 support
                io_expansion: true // MCP23S17 support
            },
            constraints: {
                max_gpio_channels: 32,
                max_encoders: 8,
                max_stepgen_axes: 4,
                max_analog_inputs: 8,
                max_analog_outputs: 2
            }
        });

        // ESP32 Profile
        this.profiles.set('ESP32', {
            chip: 'ESP32',
            variant: 'esp32',
            pins: {
                // GPIO pins (ESP32)
                gpio: [0, 1, 2, 3, 4, 5, 12, 13, 14, 15, 16, 17, 18, 19, 21, 22, 23, 25, 26, 27, 32, 33],
                // SPI Bus 1 (EtherCAT)
                spi1: { sck: 18, miso: 19, mosi: 23 },
                // SPI Bus 2 (Peripherals)
                spi2: { sck: 14, miso: 12, mosi: 13 },
                // W5500 IN (EtherCAT RX)
                w5500_in: { cs: 5, rst: 4 },
                // W5500 OUT (EtherCAT TX)
                w5500_out: { cs: 2, rst: 15 },
                // USB (ESP32 не имеет USB OTG)
                usb: null,
                // CAN (MCP2515)
                can: { cs: 16, int: null },
                // MCP23S17 (IO Expansion)
                mcp23s17: { cs: 17 }
            },
            peripherals: {
                usb: false,       // Нет USB HID Keyboard
                wifi: true,
                bluetooth: true,
                ethernet: true,
                can: true,
                io_expansion: true
            },
            constraints: {
                max_gpio_channels: 24,
                max_encoders: 6,
                max_stepgen_axes: 4,
                max_analog_inputs: 6,
                max_analog_outputs: 2
            }
        });

        // ESP32-C3 Profile
        this.profiles.set('ESP32-C3', {
            chip: 'ESP32-C3',
            variant: 'esp32c3',
            pins: {
                // GPIO pins (ESP32-C3 - меньше пинов)
                gpio: [0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21],
                // SPI Bus 1 (EtherCAT)
                spi1: { sck: 10, miso: 9, mosi: 8 },
                // SPI Bus 2 (Peripherals)
                spi2: { sck: 2, miso: 3, mosi: 4 },
                // W5500 IN (EtherCAT RX)
                w5500_in: { cs: 5, rst: 6 },
                // W5500 OUT (EtherCAT TX)
                w5500_out: { cs: 7, rst: 8 },
                // USB (ESP32-C3 имеет USB)
                usb: { dp: 19, dm: 18 },
                // CAN (MCP2515)
                can: { cs: 1, int: null },
                // MCP23S17 (IO Expansion)
                mcp23s17: { cs: 0 }
            },
            peripherals: {
                usb: true,
                wifi: true,
                bluetooth: false,  // Нет Bluetooth
                ethernet: true,
                can: true,
                io_expansion: true
            },
            constraints: {
                max_gpio_channels: 20,
                max_encoders: 4,
                max_stepgen_axes: 2,
                max_analog_inputs: 4,
                max_analog_outputs: 1
            }
        });
    }

    /**
     * Получить список всех профилей
     */
    list() {
        return Array.from(this.profiles.keys());
    }

    /**
     * Получить профиль по имени
     */
    get(profileName) {
        return this.profiles.get(profileName);
    }

    /**
     * Проверить существование профиля
     */
    has(profileName) {
        return this.profiles.has(profileName);
    }

    /**
     * Определить профиль по системной информации ESP32
     */
    detect(systemInfo) {
        if (!systemInfo || !systemInfo.chip_model) {
            return null;
        }

        const chipModel = systemInfo.chip_model.toUpperCase();
        
        if (chipModel.includes('ESP32-S3')) {
            return 'ESP32-S3';
        } else if (chipModel.includes('ESP32-C3')) {
            return 'ESP32-C3';
        } else if (chipModel.includes('ESP32')) {
            return 'ESP32';
        }

        return null;
    }

    /**
     * Проверить доступность пина для профиля
     */
    isPinAvailable(profileName, pin, type = 'gpio') {
        const profile = this.get(profileName);
        if (!profile) {
            return false;
        }

        if (type === 'gpio') {
            return profile.pins.gpio.includes(pin);
        }

        // Для других типов проверяем конкретные пины
        const pinConfig = profile.pins[type];
        if (!pinConfig) {
            return false;
        }

        // Проверяем все пины в конфигурации
        return Object.values(pinConfig).includes(pin);
    }

    /**
     * Получить все доступные пины для типа
     */
    getAvailablePins(profileName, type = 'gpio') {
        const profile = this.get(profileName);
        if (!profile) {
            return [];
        }

        if (type === 'gpio') {
            return [...profile.pins.gpio];
        }

        const pinConfig = profile.pins[type];
        if (!pinConfig) {
            return [];
        }

        return Object.values(pinConfig).filter(pin => pin !== null && pin !== undefined);
    }

    /**
     * Проверить доступность периферии
     */
    hasPeripheral(profileName, peripheral) {
        const profile = this.get(profileName);
        if (!profile) {
            return false;
        }

        return profile.peripherals[peripheral] === true;
    }

    /**
     * Получить ограничения профиля
     */
    getConstraints(profileName) {
        const profile = this.get(profileName);
        if (!profile) {
            return null;
        }

        return { ...profile.constraints };
    }
}

module.exports = DeviceProfiles;
