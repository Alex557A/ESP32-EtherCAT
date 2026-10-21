/*
 * Local Storage Manager
 * Сохранение и восстановление локальных настроек профиля конфигурации
 * 
 * ESP32 EtherCAT Slave v4
 */

const fs = require('fs');
const path = require('path');

class LocalStorageManager {
    constructor(storageDir = './storage') {
        this.storageDir = storageDir;
        this.channelsFile = path.join(storageDir, 'channels.json');
        this.devicesFile = path.join(storageDir, 'devices.json');
        this.registryFile = path.join(storageDir, 'registry.json');
        this.profilesDir = path.join(storageDir, 'profiles');
        
        // Создаем директории для хранения, если их нет
        this.ensureStorageDir();
        this.ensureProfilesDir();
    }

    /**
     * Создать директорию для хранения, если её нет
     */
    ensureStorageDir() {
        try {
            if (!fs.existsSync(this.storageDir)) {
                fs.mkdirSync(this.storageDir, { recursive: true });
                console.log(`[LocalStorage] Created storage directory: ${this.storageDir}`);
            }
        } catch (error) {
            console.error(`[LocalStorage] Failed to create storage directory:`, error.message);
        }
    }

    /**
     * Создать директорию для профилей конфигурации
     */
    ensureProfilesDir() {
        try {
            if (!fs.existsSync(this.profilesDir)) {
                fs.mkdirSync(this.profilesDir, { recursive: true });
                console.log(`[LocalStorage] Created profiles directory: ${this.profilesDir}`);
            }
        } catch (error) {
            console.error(`[LocalStorage] Failed to create profiles directory:`, error.message);
        }
    }

    /**
     * Сохранить локальные каналы GPIO
     */
    saveChannels(localChannelsMap) {
        try {
            const data = {};
            for (const [ip, channels] of localChannelsMap.entries()) {
                data[ip] = channels;
            }
            
            fs.writeFileSync(this.channelsFile, JSON.stringify(data, null, 2), 'utf8');
            console.log(`[LocalStorage] Saved channels for ${Object.keys(data).length} device(s)`);
            return true;
        } catch (error) {
            console.error(`[LocalStorage] Failed to save channels:`, error.message);
            return false;
        }
    }

    /**
     * Загрузить локальные каналы GPIO
     */
    loadChannels() {
        try {
            if (!fs.existsSync(this.channelsFile)) {
                console.log(`[LocalStorage] Channels file not found, starting with empty storage`);
                return new Map();
            }

            const data = JSON.parse(fs.readFileSync(this.channelsFile, 'utf8'));
            const map = new Map();
            
            for (const [ip, channels] of Object.entries(data)) {
                map.set(ip, channels);
            }
            
            console.log(`[LocalStorage] Loaded channels for ${map.size} device(s)`);
            return map;
        } catch (error) {
            console.error(`[LocalStorage] Failed to load channels:`, error.message);
            return new Map();
        }
    }

    /**
     * Сохранить информацию об устройствах
     */
    saveDevices(devicesMap) {
        try {
            const data = {};
            for (const [ip, device] of devicesMap.entries()) {
                // Сохраняем только необходимые данные (исключаем функции и временные поля)
                data[ip] = {
                    host: device.host,
                    system: device.system || {},
                    network: device.network || {},
                    lastSeen: device.lastSeen || Date.now()
                };
            }
            
            fs.writeFileSync(this.devicesFile, JSON.stringify(data, null, 2), 'utf8');
            console.log(`[LocalStorage] Saved ${Object.keys(data).length} device(s)`);
            return true;
        } catch (error) {
            console.error(`[LocalStorage] Failed to save devices:`, error.message);
            return false;
        }
    }

    /**
     * Загрузить информацию об устройствах
     */
    loadDevices() {
        try {
            if (!fs.existsSync(this.devicesFile)) {
                console.log(`[LocalStorage] Devices file not found, starting with empty storage`);
                return new Map();
            }

            const data = JSON.parse(fs.readFileSync(this.devicesFile, 'utf8'));
            const map = new Map();
            
            for (const [ip, device] of Object.entries(data)) {
                map.set(ip, device);
            }
            
            console.log(`[LocalStorage] Loaded ${map.size} device(s)`);
            return map;
        } catch (error) {
            console.error(`[LocalStorage] Failed to load devices:`, error.message);
            return new Map();
        }
    }

    /**
     * Сохранить реестр устройств (DeviceRegistry)
     */
    saveRegistry(deviceRegistry) {
        try {
            const allDevices = deviceRegistry.getAll();
            const data = {};
            
            for (const device of allDevices) {
                data[device.ip] = {
                    ip: device.ip,
                    profile: device.profile || null,
                    systemInfo: device.systemInfo || null,
                    lastSeen: device.lastSeen || Date.now(),
                    lastSynced: device.lastSynced || null,
                    config: device.config || null,
                    status: device.status || 'disconnected'
                };
            }
            
            fs.writeFileSync(this.registryFile, JSON.stringify(data, null, 2), 'utf8');
            console.log(`[LocalStorage] Saved registry for ${Object.keys(data).length} device(s)`);
            return true;
        } catch (error) {
            console.error(`[LocalStorage] Failed to save registry:`, error.message);
            return false;
        }
    }

    /**
     * Загрузить реестр устройств (DeviceRegistry)
     */
    loadRegistry(deviceRegistry) {
        try {
            if (!fs.existsSync(this.registryFile)) {
                console.log(`[LocalStorage] Registry file not found, starting with empty registry`);
                return;
            }

            const data = JSON.parse(fs.readFileSync(this.registryFile, 'utf8'));
            
            for (const [ip, deviceInfo] of Object.entries(data)) {
                deviceRegistry.set(ip, deviceInfo);
            }
            
            console.log(`[LocalStorage] Loaded registry for ${Object.keys(data).length} device(s)`);
        } catch (error) {
            console.error(`[LocalStorage] Failed to load registry:`, error.message);
        }
    }

    /**
     * Сохранить все данные (channels, devices, registry)
     */
    saveAll(localChannelsMap, devicesMap, deviceRegistry) {
        const results = {
            channels: this.saveChannels(localChannelsMap),
            devices: this.saveDevices(devicesMap),
            registry: this.saveRegistry(deviceRegistry)
        };
        
        const allSuccess = Object.values(results).every(r => r === true);
        if (allSuccess) {
            console.log(`[LocalStorage] All data saved successfully`);
        } else {
            console.warn(`[LocalStorage] Some data failed to save:`, results);
        }
        
        return allSuccess;
    }

    /**
     * Загрузить все данные
     */
    loadAll(deviceRegistry) {
        const channels = this.loadChannels();
        const devices = this.loadDevices();
        this.loadRegistry(deviceRegistry);
        
        return { channels, devices };
    }

    /**
     * Очистить все сохраненные данные
     */
    clearAll() {
        try {
            const files = [this.channelsFile, this.devicesFile, this.registryFile];
            let cleared = 0;
            
            for (const file of files) {
                if (fs.existsSync(file)) {
                    fs.unlinkSync(file);
                    cleared++;
                }
            }
            
            console.log(`[LocalStorage] Cleared ${cleared} file(s)`);
            return true;
        } catch (error) {
            console.error(`[LocalStorage] Failed to clear storage:`, error.message);
            return false;
        }
    }

    /**
     * Сохранить сырой профиль конфигурации перед синхронизацией
     */
    saveProfile(deviceIp, profileName, config) {
        try {
            this.ensureProfilesDir();
            
            // Создаем имя файла: deviceIp_profileName_timestamp.json
            const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
            const safeName = (profileName || 'default').replace(/[^a-zA-Z0-9_-]/g, '_');
            const filename = `${deviceIp}_${safeName}_${timestamp}.json`;
            const filepath = path.join(this.profilesDir, filename);
            
            const profileData = {
                device_ip: deviceIp,
                profile_name: profileName || 'default',
                created_at: new Date().toISOString(),
                config: config
            };
            
            fs.writeFileSync(filepath, JSON.stringify(profileData, null, 2), 'utf8');
            console.log(`[LocalStorage] Saved profile: ${filename}`);
            
            return {
                success: true,
                filename: filename,
                filepath: filepath
            };
        } catch (error) {
            console.error(`[LocalStorage] Failed to save profile:`, error.message);
            return {
                success: false,
                error: error.message
            };
        }
    }

    /**
     * Получить список всех сохраненных профилей
     */
    listProfiles(deviceIp = null) {
        try {
            this.ensureProfilesDir();
            
            if (!fs.existsSync(this.profilesDir)) {
                return [];
            }
            
            const files = fs.readdirSync(this.profilesDir)
                .filter(f => f.endsWith('.json'))
                .map(filename => {
                    const filepath = path.join(this.profilesDir, filename);
                    try {
                        const data = JSON.parse(fs.readFileSync(filepath, 'utf8'));
                        return {
                            filename: filename,
                            filepath: filepath,
                            device_ip: data.device_ip,
                            profile_name: data.profile_name,
                            created_at: data.created_at,
                            size: fs.statSync(filepath).size
                        };
                    } catch (e) {
                        console.warn(`[LocalStorage] Failed to parse profile ${filename}:`, e.message);
                        return null;
                    }
                })
                .filter(p => p !== null);
            
            // Фильтруем по deviceIp если указан
            if (deviceIp) {
                return files.filter(p => p.device_ip === deviceIp);
            }
            
            return files.sort((a, b) => new Date(b.created_at) - new Date(a.created_at)); // Новые сначала
        } catch (error) {
            console.error(`[LocalStorage] Failed to list profiles:`, error.message);
            return [];
        }
    }

    /**
     * Загрузить профиль конфигурации по имени файла
     */
    loadProfile(filename) {
        try {
            const filepath = path.join(this.profilesDir, filename);
            
            if (!fs.existsSync(filepath)) {
                return {
                    success: false,
                    error: 'Profile file not found'
                };
            }
            
            const data = JSON.parse(fs.readFileSync(filepath, 'utf8'));
            
            return {
                success: true,
                profile: data
            };
        } catch (error) {
            console.error(`[LocalStorage] Failed to load profile:`, error.message);
            return {
                success: false,
                error: error.message
            };
        }
    }

    /**
     * Удалить профиль конфигурации
     */
    deleteProfile(filename) {
        try {
            const filepath = path.join(this.profilesDir, filename);
            
            if (!fs.existsSync(filepath)) {
                return {
                    success: false,
                    error: 'Profile file not found'
                };
            }
            
            fs.unlinkSync(filepath);
            console.log(`[LocalStorage] Deleted profile: ${filename}`);
            
            return {
                success: true
            };
        } catch (error) {
            console.error(`[LocalStorage] Failed to delete profile:`, error.message);
            return {
                success: false,
                error: error.message
            };
        }
    }
}

module.exports = LocalStorageManager;
