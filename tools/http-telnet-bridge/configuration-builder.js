/*
 * Configuration Builder
 * Построитель и валидатор конфигураций для ESP32
 * 
 * ESP32 EtherCAT Slave v4
 */

class ConfigurationBuilder {
    constructor(deviceProfile) {
        if (!deviceProfile) {
            throw new Error('Device profile is required');
        }
        this.profile = deviceProfile;
        this.config = {
            gpio_channels: [],
            encoders: [],
            stepgen: [],
            io_expansion: null,
            network: null
        };
        this.errors = [];
        this.warnings = [];
    }

    /**
     * Валидация пина
     */
    validatePin(pin, type = 'gpio') {
        if (type === 'gpio') {
            if (!this.profile.pins.gpio.includes(pin)) {
                return {
                    valid: false,
                    error: `Pin ${pin} is not available for ${this.profile.chip}`
                };
            }
        } else {
            const pinConfig = this.profile.pins[type];
            if (!pinConfig) {
                return {
                    valid: false,
                    error: `Pin type ${type} is not available for ${this.profile.chip}`
                };
            }
            const pins = Object.values(pinConfig).filter(p => p !== null && p !== undefined);
            if (!pins.includes(pin)) {
                return {
                    valid: false,
                    error: `Pin ${pin} is not available for ${type} on ${this.profile.chip}`
                };
            }
        }
        return { valid: true };
    }

    /**
     * Проверка конфликта пинов
     */
    checkPinConflict(pin, excludeIndex = -1) {
        // Проверка в GPIO каналах
        for (let i = 0; i < this.config.gpio_channels.length; i++) {
            if (i === excludeIndex) continue;
            const channel = this.config.gpio_channels[i];
            if (channel.pin === pin) {
                return {
                    conflict: true,
                    type: 'gpio_channel',
                    index: i,
                    message: `Pin ${pin} is already used in GPIO channel ${i}`
                };
            }
        }

        // Проверка в энкодерах
        for (let i = 0; i < this.config.encoders.length; i++) {
            const encoder = this.config.encoders[i];
            if (encoder.pin_a === pin || encoder.pin_b === pin) {
                return {
                    conflict: true,
                    type: 'encoder',
                    index: i,
                    message: `Pin ${pin} is already used in encoder ${i}`
                };
            }
        }

        // Проверка в StepGen
        for (let i = 0; i < this.config.stepgen.length; i++) {
            const stepgen = this.config.stepgen[i];
            if (stepgen.step_pin === pin || stepgen.dir_pin === pin) {
                return {
                    conflict: true,
                    type: 'stepgen',
                    index: i,
                    message: `Pin ${pin} is already used in StepGen axis ${i}`
                };
            }
        }

        return { conflict: false };
    }

    /**
     * Проверка ограничений устройства
     */
    checkConstraints() {
        const constraints = this.profile.constraints;
        const errors = [];

        // Проверка GPIO каналов
        if (this.config.gpio_channels.length > constraints.max_gpio_channels) {
            errors.push({
                type: 'constraint',
                field: 'gpio_channels',
                message: `Maximum ${constraints.max_gpio_channels} GPIO channels allowed, got ${this.config.gpio_channels.length}`
            });
        }

        // Проверка энкодеров
        if (this.config.encoders.length > constraints.max_encoders) {
            errors.push({
                type: 'constraint',
                field: 'encoders',
                message: `Maximum ${constraints.max_encoders} encoders allowed, got ${this.config.encoders.length}`
            });
        }

        // Проверка StepGen осей
        if (this.config.stepgen.length > constraints.max_stepgen_axes) {
            errors.push({
                type: 'constraint',
                field: 'stepgen',
                message: `Maximum ${constraints.max_stepgen_axes} StepGen axes allowed, got ${this.config.stepgen.length}`
            });
        }

        return errors;
    }

    /**
     * Добавить GPIO канал
     */
    addGPIOChannel(channel, index = -1) {
        // Валидация пина
        if (channel.pin !== undefined && channel.pin !== null) {
            const pinValidation = this.validatePin(channel.pin, 'gpio');
            if (!pinValidation.valid) {
                throw new Error(pinValidation.error);
            }

            // Проверка конфликта
            const conflict = this.checkPinConflict(channel.pin, index);
            if (conflict.conflict) {
                throw new Error(conflict.message);
            }
        }

        // Проверка периферии для USB Keyboard
        if (channel.category === 'USB_KEY' && !this.profile.peripherals.usb) {
            throw new Error(`USB Keyboard is not supported on ${this.profile.chip}`);
        }

        // Проверка периферии для GPIO_EX
        if (channel.category === 'GPIO_EX' && !this.profile.peripherals.io_expansion) {
            throw new Error(`IO Expansion (MCP23S17) is not supported on ${this.profile.chip}`);
        }

        // Добавление или обновление канала
        if (index >= 0 && index < this.config.gpio_channels.length) {
            this.config.gpio_channels[index] = { ...channel };
        } else {
            this.config.gpio_channels.push({ ...channel });
        }
    }

    /**
     * Добавить энкодер
     */
    addEncoder(encoder, index = -1) {
        // Валидация пинов
        if (encoder.pin_a !== undefined) {
            const pinValidation = this.validatePin(encoder.pin_a, 'gpio');
            if (!pinValidation.valid) {
                throw new Error(`Encoder pin_a: ${pinValidation.error}`);
            }
        }

        if (encoder.pin_b !== undefined) {
            const pinValidation = this.validatePin(encoder.pin_b, 'gpio');
            if (!pinValidation.valid) {
                throw new Error(`Encoder pin_b: ${pinValidation.error}`);
            }
        }

        // Проверка конфликтов
        if (encoder.pin_a !== undefined) {
            const conflict = this.checkPinConflict(encoder.pin_a);
            if (conflict.conflict) {
                throw new Error(`Encoder pin_a: ${conflict.message}`);
            }
        }

        if (encoder.pin_b !== undefined) {
            const conflict = this.checkPinConflict(encoder.pin_b);
            if (conflict.conflict) {
                throw new Error(`Encoder pin_b: ${conflict.message}`);
            }
        }

        // Добавление или обновление энкодера
        if (index >= 0 && index < this.config.encoders.length) {
            this.config.encoders[index] = { ...encoder };
        } else {
            this.config.encoders.push({ ...encoder });
        }
    }

    /**
     * Добавить StepGen ось
     */
    addStepGen(stepgen, index = -1) {
        // Валидация пинов
        if (stepgen.step_pin !== undefined) {
            const pinValidation = this.validatePin(stepgen.step_pin, 'gpio');
            if (!pinValidation.valid) {
                throw new Error(`StepGen step_pin: ${pinValidation.error}`);
            }
        }

        if (stepgen.dir_pin !== undefined) {
            const pinValidation = this.validatePin(stepgen.dir_pin, 'gpio');
            if (!pinValidation.valid) {
                throw new Error(`StepGen dir_pin: ${pinValidation.error}`);
            }
        }

        // Проверка конфликтов
        if (stepgen.step_pin !== undefined) {
            const conflict = this.checkPinConflict(stepgen.step_pin);
            if (conflict.conflict) {
                throw new Error(`StepGen step_pin: ${conflict.message}`);
            }
        }

        if (stepgen.dir_pin !== undefined) {
            const conflict = this.checkPinConflict(stepgen.dir_pin);
            if (conflict.conflict) {
                throw new Error(`StepGen dir_pin: ${conflict.message}`);
            }
        }

        // Добавление или обновление StepGen
        if (index >= 0 && index < this.config.stepgen.length) {
            this.config.stepgen[index] = { ...stepgen };
        } else {
            this.config.stepgen.push({ ...stepgen });
        }
    }

    /**
     * Валидация всей конфигурации
     */
    validate() {
        this.errors = [];
        this.warnings = [];

        // Проверка ограничений
        const constraintErrors = this.checkConstraints();
        this.errors.push(...constraintErrors);

        // Проверка PDO mapping конфликтов
        const pdoConflicts = this.checkPDOConflicts();
        this.errors.push(...pdoConflicts);

        return {
            valid: this.errors.length === 0,
            errors: this.errors,
            warnings: this.warnings
        };
    }

    /**
     * Проверка конфликтов PDO mapping
     */
    checkPDOConflicts() {
        const errors = [];
        const pdoMap = new Map(); // key: "byte:bit", value: channel index

        // Проверка GPIO каналов
        for (let i = 0; i < this.config.gpio_channels.length; i++) {
            const channel = this.config.gpio_channels[i];
            if (channel.pdo_byte !== undefined && channel.pdo_bit !== undefined) {
                const key = `${channel.pdo_byte}:${channel.pdo_bit}`;
                if (pdoMap.has(key)) {
                    const existingIndex = pdoMap.get(key);
                    errors.push({
                        type: 'pdo_conflict',
                        field: 'gpio_channels',
                        index: i,
                        message: `PDO byte ${channel.pdo_byte}, bit ${channel.pdo_bit} is already used by channel ${existingIndex}`
                    });
                } else {
                    pdoMap.set(key, i);
                }
            }
        }

        return errors;
    }

    /**
     * Построить конфигурацию
     */
    build() {
        const validation = this.validate();
        if (!validation.valid) {
            throw new Error(`Configuration validation failed: ${validation.errors.map(e => e.message).join(', ')}`);
        }

        return {
            profile: this.profile.chip,
            config: { ...this.config },
            timestamp: Date.now()
        };
    }

    /**
     * Загрузить конфигурацию
     */
    load(config) {
        this.config = {
            gpio_channels: config.gpio_channels || [],
            encoders: config.encoders || [],
            stepgen: config.stepgen || [],
            io_expansion: config.io_expansion || null,
            network: config.network || null
        };
    }

    /**
     * Очистить конфигурацию
     */
    clear() {
        this.config = {
            gpio_channels: [],
            encoders: [],
            stepgen: [],
            io_expansion: null,
            network: null
        };
        this.errors = [];
        this.warnings = [];
    }
}

module.exports = ConfigurationBuilder;
