/*
 * Device Registry
 * Реестр устройств ESP32 с информацией о профилях и конфигурациях
 * 
 * ESP32 EtherCAT Slave v4
 */

class DeviceRegistry {
    constructor() {
        this.devices = new Map();
    }

    /**
     * Добавить или обновить устройство
     */
    set(ip, deviceInfo) {
        const existing = this.devices.get(ip);
        this.devices.set(ip, {
            ip: ip,
            profile: deviceInfo.profile || existing?.profile || null,
            systemInfo: deviceInfo.systemInfo || existing?.systemInfo || null,
            lastSeen: Date.now(),
            lastSynced: deviceInfo.lastSynced || existing?.lastSynced || null,
            config: deviceInfo.config || existing?.config || null,
            status: deviceInfo.status || existing?.status || 'disconnected'
        });
    }

    /**
     * Получить информацию об устройстве
     */
    get(ip) {
        return this.devices.get(ip);
    }

    /**
     * Проверить существование устройства
     */
    has(ip) {
        return this.devices.has(ip);
    }

    /**
     * Установить профиль устройства
     */
    setProfile(ip, profile) {
        const device = this.devices.get(ip);
        if (device) {
            device.profile = profile;
            device.lastSeen = Date.now();
        } else {
            this.set(ip, { profile });
        }
    }

    /**
     * Установить системную информацию
     */
    setSystemInfo(ip, systemInfo) {
        const device = this.devices.get(ip);
        if (device) {
            device.systemInfo = systemInfo;
            device.lastSeen = Date.now();
        } else {
            this.set(ip, { systemInfo });
        }
    }

    /**
     * Установить конфигурацию
     */
    setConfig(ip, config) {
        const device = this.devices.get(ip);
        if (device) {
            device.config = config;
        } else {
            this.set(ip, { config });
        }
    }

    /**
     * Обновить статус устройства
     */
    setStatus(ip, status) {
        const device = this.devices.get(ip);
        if (device) {
            device.status = status;
            device.lastSeen = Date.now();
        } else {
            this.set(ip, { status });
        }
    }

    /**
     * Отметить время синхронизации
     */
    markSynced(ip) {
        const device = this.devices.get(ip);
        if (device) {
            device.lastSynced = Date.now();
            device.status = 'synced';
        }
    }

    /**
     * Получить все устройства
     */
    getAll() {
        return Array.from(this.devices.values());
    }

    /**
     * Получить все IP адреса
     */
    getAllIPs() {
        return Array.from(this.devices.keys());
    }

    /**
     * Удалить устройство
     */
    remove(ip) {
        return this.devices.delete(ip);
    }

    /**
     * Очистить реестр
     */
    clear() {
        this.devices.clear();
    }

    /**
     * Получить устройства по профилю
     */
    getByProfile(profile) {
        return this.getAll().filter(device => device.profile === profile);
    }

    /**
     * Получить устройства по статусу
     */
    getByStatus(status) {
        return this.getAll().filter(device => device.status === status);
    }
}

module.exports = DeviceRegistry;
