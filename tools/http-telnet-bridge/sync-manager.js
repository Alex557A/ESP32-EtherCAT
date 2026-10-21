/*
 * Sync Manager
 * Менеджер синхронизации конфигураций с ESP32
 * 
 * ESP32 EtherCAT Slave v4
 */

class SyncManager {
    constructor(deviceProfiles, deviceRegistry, sendJsonRpcOnce) {
        this.deviceProfiles = deviceProfiles;
        this.deviceRegistry = deviceRegistry;
        this.sendJsonRpcOnce = sendJsonRpcOnce;
    }

    /**
     * Определить профиль устройства
     */
    async detectDeviceProfile(ip) {
        try {
            const systemInfo = await this.sendJsonRpcOnce(ip, 'get_system_info', {}, 3000);
            if (systemInfo && systemInfo.success && systemInfo.result) {
                const profile = this.deviceProfiles.detect(systemInfo.result);
                if (profile) {
                    this.deviceRegistry.setProfile(ip, profile);
                    this.deviceRegistry.setSystemInfo(ip, systemInfo.result);
                    return { success: true, profile, systemInfo: systemInfo.result };
                }
            }
            return { success: false, error: 'Could not detect device profile' };
        } catch (error) {
            return { success: false, error: error.message };
        }
    }

    /**
     * Валидация конфигурации против профиля устройства
     */
    validateConfiguration(ip, config) {
        const device = this.deviceRegistry.get(ip);
        if (!device) {
            return { valid: false, error: `Device ${ip} not found in registry` };
        }

        if (!device.profile) {
            return { valid: false, error: `Device profile not set for ${ip}` };
        }

        const profile = this.deviceProfiles.get(device.profile);
        if (!profile) {
            return { valid: false, error: `Profile ${device.profile} not found` };
        }

        // Создаем builder для валидации
        const ConfigurationBuilder = require('./configuration-builder');
        const builder = new ConfigurationBuilder(profile);
        builder.load(config);

        const validation = builder.validate();
        return validation;
    }

    /**
     * Синхронизация конфигурации с устройством
     */
    async syncConfiguration(ip, config) {
        const device = this.deviceRegistry.get(ip);
        if (!device) {
            throw new Error(`Device ${ip} not found in registry`);
        }

        // Устанавливаем статус синхронизации
        this.deviceRegistry.setStatus(ip, 'syncing');

        try {
            // Валидация конфигурации
            const validation = this.validateConfiguration(ip, config);
            if (!validation.valid) {
                this.deviceRegistry.setStatus(ip, 'disconnected');
                return {
                    success: false,
                    error: 'Configuration validation failed',
                    validationErrors: validation.errors
                };
            }

            const results = [];
            let successCount = 0;
            let errorCount = 0;

            // Синхронизация GPIO каналов
            if (config.gpio_channels && config.gpio_channels.length > 0) {
                for (const channel of config.gpio_channels) {
                    try {
                        const resp = await this.sendJsonRpcOnce(ip, 'set_gpio_channel', channel, 5000);
                        if (resp.success) {
                            successCount++;
                            results.push({
                                type: 'gpio_channel',
                                index: channel.index,
                                success: true
                            });
                        } else {
                            errorCount++;
                            results.push({
                                type: 'gpio_channel',
                                index: channel.index,
                                success: false,
                                error: resp.error || 'Unknown error'
                            });
                        }
                    } catch (error) {
                        errorCount++;
                        results.push({
                            type: 'gpio_channel',
                            index: channel.index,
                            success: false,
                            error: error.message
                        });
                    }
                }
            }

            // Синхронизация энкодеров
            if (config.encoders && config.encoders.length > 0) {
                for (const encoder of config.encoders) {
                    try {
                        const resp = await this.sendJsonRpcOnce(ip, 'set_encoder', encoder, 3000);
                        if (resp.success) {
                            successCount++;
                            results.push({
                                type: 'encoder',
                                index: encoder.index,
                                success: true
                            });
                        } else {
                            errorCount++;
                            results.push({
                                type: 'encoder',
                                index: encoder.index,
                                success: false,
                                error: resp.error || 'Unknown error'
                            });
                        }
                    } catch (error) {
                        errorCount++;
                        results.push({
                            type: 'encoder',
                            index: encoder.index,
                            success: false,
                            error: error.message
                        });
                    }
                }
            }

            // Синхронизация StepGen
            if (config.stepgen && config.stepgen.length > 0) {
                for (const stepgen of config.stepgen) {
                    try {
                        const resp = await this.sendJsonRpcOnce(ip, 'set_stepgen_config', stepgen, 3000);
                        if (resp.success) {
                            successCount++;
                            results.push({
                                type: 'stepgen',
                                index: stepgen.axis,
                                success: true
                            });
                        } else {
                            errorCount++;
                            results.push({
                                type: 'stepgen',
                                index: stepgen.axis,
                                success: false,
                                error: resp.error || 'Unknown error'
                            });
                        }
                    } catch (error) {
                        errorCount++;
                        results.push({
                            type: 'stepgen',
                            index: stepgen.axis,
                            success: false,
                            error: error.message
                        });
                    }
                }
            }

            // Обновление статуса
            if (errorCount === 0) {
                this.deviceRegistry.setStatus(ip, 'synced');
                this.deviceRegistry.markSynced(ip);
                this.deviceRegistry.setConfig(ip, config);
            } else {
                this.deviceRegistry.setStatus(ip, 'connected');
            }

            return {
                success: errorCount === 0,
                synced: successCount,
                errors: errorCount,
                results: results
            };

        } catch (error) {
            this.deviceRegistry.setStatus(ip, 'disconnected');
            return {
                success: false,
                error: error.message
            };
        }
    }
}

module.exports = SyncManager;
