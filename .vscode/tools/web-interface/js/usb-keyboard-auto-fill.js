/**
 * Автозаполнение полей USB keyboard канала
 * Работает только когда ESP32 подключен
 * 
 * PDO Byte - байт в Process Data Object (EtherCAT), содержит keycode для USB клавиатуры
 * PDO Bit - бит в PDO Byte, используется для GPIO каналов, НЕ нужен для USB клавиатуры
 */

class USBKeyboardAutoFill {
    constructor() {
        this.isEnabled = false;
        this.setupWebSocket();
        this.setupFormListeners();
        
        // Слушаем изменения статуса подключения
        if (typeof espConnectionStatus !== 'undefined') {
            espConnectionStatus.addListener((isConnected) => {
                this.updateAutoFillStatus(isConnected);
            });
        }
    }
    
    /**
     * Настройка WebSocket для получения событий клавиатуры
     */
    setupWebSocket() {
        // Используем существующий WebSocket или подключаемся к новому
        if (window.ws) {
            window.ws.addEventListener('message', (event) => this.handleWebSocketMessage(event));
        } else {
            // Подключаемся к WebSocket при его создании
            const checkWS = setInterval(() => {
                if (window.ws) {
                    window.ws.addEventListener('message', (event) => this.handleWebSocketMessage(event));
                    clearInterval(checkWS);
                }
            }, 100);
        }
    }
    
    /**
     * Обработка сообщений WebSocket
     */
    handleWebSocketMessage(event) {
        try {
            const data = JSON.parse(event.data);
            
            // Обрабатываем только события keyboard_key_pressed
            if (data.method === 'keyboard_key_pressed' && 
                data.params && 
                typeof espConnectionStatus !== 'undefined' &&
                espConnectionStatus.isConnected) {
                
                this.autoFillFields(data.params);
            }
        } catch (error) {
            // Игнорируем ошибки парсинга (могут быть другие сообщения)
        }
    }
    
    /**
     * Автозаполнение полей формы
     */
    autoFillFields(params) {
        const { keycode, modifier, keycode_hex } = params;
        
        // Проверяем, что ESP онлайн
        if (typeof espConnectionStatus === 'undefined' || !espConnectionStatus.isConnected) {
            console.warn('ESP offline - auto-fill disabled');
            return;
        }
        
        // Проверяем, что поле USB key code активно (в фокусе)
        const usbKeyField = document.getElementById('usb-keycode-hex');
        if (!usbKeyField || document.activeElement !== usbKeyField) {
            return; // Поле не в фокусе - не заполняем
        }
        
        // Заполняем USB key code (HEX)
        usbKeyField.value = keycode_hex;
        
        // Обновляем выпадающий список USB кнопки
        this.updateUSBButtonDropdown(keycode_hex);
        
        // Заполняем модификаторы
        this.updateModifierCheckboxes(modifier);
        
        // Заполняем PDO byte (keycode для USB клавиатуры)
        // PDO Byte = keycode нажатой клавиши (0-255)
        // Ищем поле PDO Byte разными способами
        let pdoByteField = document.getElementById('pdo-byte');
        if (!pdoByteField) {
            pdoByteField = document.querySelector('input[name="pdo-byte"], input[name="pdo_byte"], input[id*="pdo"], input[placeholder*="PDO"]');
        }
        if (pdoByteField) {
            pdoByteField.value = keycode;
            pdoByteField.dispatchEvent(new Event('input', { bubbles: true }));
            pdoByteField.dispatchEvent(new Event('change', { bubbles: true }));
            console.log(`[USB AutoFill] PDO Byte заполнен: ${keycode} (поле: ${pdoByteField.id || pdoByteField.name})`);
        } else {
            console.warn('[USB AutoFill] Поле pdo-byte не найдено. Доступные поля:', 
                Array.from(document.querySelectorAll('input[type="number"], input[type="text"]'))
                    .map(inp => inp.id || inp.name || inp.placeholder)
                    .filter(Boolean)
            );
        }
        
        // Заполняем Index (keycode для USB клавиатуры)
        const indexField = document.getElementById('index');
        if (indexField) {
            indexField.value = keycode;
            indexField.dispatchEvent(new Event('input', { bubbles: true }));
            console.log(`[USB AutoFill] Index заполнен: ${keycode}`);
        } else {
            console.warn('[USB AutoFill] Поле index не найдено');
        }
        
        // Триггерим событие изменения для валидации
        usbKeyField.dispatchEvent(new Event('input', { bubbles: true }));
        console.log(`[USB AutoFill] Поля заполнены: keycode=${keycode}, hex=${keycode_hex}, modifier=${modifier}`);
    }
    
    /**
     * Проверка, что выбрана категория USB клавиатуры
     */
    isUSBKeyboardCategory() {
        // Ищем селектор категории канала
        const categorySelect = document.querySelector('select[name="category"], select[id*="category"], select[id*="channel-type"], select[name*="type"]');
        if (categorySelect && categorySelect.value) {
            const category = categorySelect.value.toLowerCase();
            return category.includes('usb') || category.includes('keyboard') || category === 'usb_keyboard';
        }
        
        // Если селектора нет, проверяем наличие полей USB (если поле видимо - значит категория USB)
        const usbKeyField = document.getElementById('usb-keycode-hex');
        if (usbKeyField) {
            const parent = usbKeyField.closest('.form-group, .field-wrapper, tr, .channel-form');
            return parent && parent.offsetParent !== null && usbKeyField.offsetParent !== null;
        }
        
        return false;
    }
    
    /**
     * Обновление чекбоксов модификаторов
     */
    updateModifierCheckboxes(modifier) {
        const modifierMap = {
            'modifier-lctrl': 0x01,
            'modifier-lshift': 0x02,
            'modifier-lalt': 0x04,
            'modifier-lgui': 0x08,
            'modifier-rctrl': 0x10,
            'modifier-rshift': 0x20,
            'modifier-ralt': 0x40,
            'modifier-rgui': 0x80
        };
        
        Object.entries(modifierMap).forEach(([id, bit]) => {
            const checkbox = document.getElementById(id);
            if (checkbox) {
                checkbox.checked = (modifier & bit) !== 0;
                checkbox.dispatchEvent(new Event('change', { bubbles: true }));
            }
        });
    }
    
    /**
     * Обновление выпадающего списка USB кнопки
     */
    updateUSBButtonDropdown(keycodeHex) {
        const dropdown = document.getElementById('usb-button-select');
        if (!dropdown) return;
        
        // Ищем опцию с соответствующим keycode
        const option = Array.from(dropdown.options).find(
            opt => opt.value === keycodeHex || 
                   opt.dataset.keycode === keycodeHex ||
                   opt.text.includes(keycodeHex)
        );
        
        if (option) {
            dropdown.value = option.value;
            dropdown.dispatchEvent(new Event('change', { bubbles: true }));
        }
    }
    
    /**
     * Настройка слушателей формы
     */
    setupFormListeners() {
        // Используем делегирование событий для динамически создаваемых форм
        document.addEventListener('focus', (event) => {
            if (event.target.id === 'usb-keycode-hex') {
                this.showFieldTooltip(event.target);
            }
        }, true);
        
        document.addEventListener('blur', (event) => {
            if (event.target.id === 'usb-keycode-hex') {
                this.hideFieldTooltip(event.target);
            }
        }, true);
        
        // Слушаем изменение категории канала для скрытия/показа полей
        document.addEventListener('change', (event) => {
            const target = event.target;
            if (target.matches('select[name="category"], select[id*="category"], select[id*="channel-type"]')) {
                this.updateFieldsVisibility(target.value);
            }
        }, true);
        
        // Инициализация видимости полей при загрузке
        if (document.readyState === 'loading') {
            document.addEventListener('DOMContentLoaded', () => {
                this.initFieldsVisibility();
                // Также обновляем при динамическом изменении DOM
                this.observeDOMChanges();
            });
        } else {
            this.initFieldsVisibility();
            this.observeDOMChanges();
        }
    }
    
    /**
     * Наблюдение за изменениями DOM для обновления видимости полей
     */
    observeDOMChanges() {
        // Используем MutationObserver для отслеживания изменений в таблице
        const observer = new MutationObserver(() => {
            const categorySelect = document.querySelector('select[name="category"], select[id*="category"], select[id*="channel-type"]');
            if (categorySelect) {
                this.updateFieldsVisibility(categorySelect.value);
            }
        });
        
        // Наблюдаем за изменениями в body
        observer.observe(document.body, {
            childList: true,
            subtree: true
        });
    }
    
    /**
     * Инициализация видимости полей
     */
    initFieldsVisibility() {
        const categorySelect = document.querySelector('select[name="category"], select[id*="category"], select[id*="channel-type"]');
        if (categorySelect) {
            this.updateFieldsVisibility(categorySelect.value);
        }
    }
    
    /**
     * Обновление видимости полей в зависимости от категории
     */
    updateFieldsVisibility(category) {
        const categoryLower = category ? category.toLowerCase() : '';
        const isUSB = categoryLower.includes('usb') || categoryLower.includes('keyboard') || categoryLower === 'usb_keyboard';
        
        // Поле PDO Bit - скрываем для USB клавиатуры
        const pdoBitField = document.getElementById('pdo-bit');
        const pdoBitLabel = document.querySelector('label[for="pdo-bit"]');
        if (pdoBitField) {
            if (isUSB) {
                pdoBitField.style.display = 'none';
                const parent = pdoBitField.closest('.field-wrapper, .form-group, tr, .form-row');
                if (parent) parent.style.display = 'none';
            } else {
                pdoBitField.style.display = '';
                const parent = pdoBitField.closest('.field-wrapper, .form-group, tr, .form-row');
                if (parent) parent.style.removeProperty('display');
            }
        }
        if (pdoBitLabel) {
            pdoBitLabel.style.display = isUSB ? 'none' : '';
        }
        
        // Поле PDO Byte - показываем для USB клавиатуры
        const pdoByteField = document.getElementById('pdo-byte');
        const pdoByteLabel = document.querySelector('label[for="pdo-byte"]');
        if (pdoByteField) {
            if (isUSB) {
                pdoByteField.style.display = '';
                const parent = pdoByteField.closest('.field-wrapper, .form-group, tr, .form-row');
                if (parent) parent.style.removeProperty('display');
            }
        }
        if (pdoByteLabel && isUSB) {
            pdoByteLabel.style.display = '';
        }
        
        // Обновляем таблицу каналов
        this.updateChannelsTable(category);
        
        // Обновляем таблицу через менеджер таблицы, если он доступен
        if (typeof channelsTableManager !== 'undefined' && channelsTableManager.refreshTable) {
            channelsTableManager.refreshTable();
        }
    }
    
    /**
     * Обновление таблицы каналов - скрытие колонки PDO Bit для USB
     */
    updateChannelsTable(category) {
        const categoryLower = category ? category.toLowerCase() : '';
        const isUSB = categoryLower.includes('usb') || categoryLower.includes('keyboard') || categoryLower === 'usb_keyboard';
        
        // Ищем таблицу каналов
        const tables = document.querySelectorAll('table[id*="channel"], table[id*="channels"], .channels-table, table');
        
        tables.forEach(table => {
            // Ищем заголовок колонки PDO Bit
            const headers = table.querySelectorAll('th, thead td');
            headers.forEach((header, index) {
                const text = header.textContent.trim().toLowerCase();
                if (text.includes('pdo bit') || text.includes('pdo-bit')) {
                    // Скрываем/показываем заголовок
                    if (isUSB) {
                        header.style.display = 'none';
                    } else {
                        header.style.display = '';
                    }
                    
                    // Скрываем/показываем ячейки в этой колонке
                    const rows = table.querySelectorAll('tbody tr, tbody td');
                    rows.forEach(row => {
                        if (row.tagName === 'TD') {
                            // Это ячейка, нужно найти правильную колонку
                            const cells = row.parentElement.querySelectorAll('td');
                            const cellIndex = Array.from(cells).indexOf(row);
                            if (cellIndex === index) {
                                row.style.display = isUSB ? 'none' : '';
                            }
                        } else if (row.tagName === 'TR') {
                            // Это строка, скрываем ячейку по индексу
                            const cell = row.querySelectorAll('td')[index];
                            if (cell) {
                                cell.style.display = isUSB ? 'none' : '';
                            }
                        }
                    });
                }
            });
        });
    }
    
    /**
     * Показать подсказку для поля
     */
    showFieldTooltip(field) {
        let tooltip = field.parentElement.querySelector('.field-tooltip');
        if (!tooltip) {
            tooltip = document.createElement('div');
            tooltip.className = 'field-tooltip';
            field.parentElement.style.position = 'relative';
            field.parentElement.appendChild(tooltip);
        }
        
        if (typeof espConnectionStatus !== 'undefined' && espConnectionStatus.isConnected) {
            tooltip.textContent = 'Нажмите клавишу на подключенной клавиатуре для автозаполнения';
            tooltip.className = 'field-tooltip tooltip-online';
        } else {
            tooltip.textContent = 'ESP32 offline - введите key code вручную';
            tooltip.className = 'field-tooltip tooltip-offline';
        }
        tooltip.style.display = 'block';
    }
    
    /**
     * Скрыть подсказку
     */
    hideFieldTooltip(field) {
        const tooltip = field.parentElement.querySelector('.field-tooltip');
        if (tooltip) {
            tooltip.style.display = 'none';
        }
    }
    
    /**
     * Обновление статуса автозаполнения
     */
    updateAutoFillStatus(isConnected) {
        this.isEnabled = isConnected;
        
        // Обновляем UI индикатор
        const indicator = document.getElementById('auto-fill-status-indicator');
        if (indicator) {
            if (isConnected) {
                indicator.textContent = 'Автозаполнение: Включено';
                indicator.className = 'status-badge status-online';
            } else {
                indicator.textContent = 'Автозаполнение: Отключено (ESP offline)';
                indicator.className = 'status-badge status-offline';
            }
        }
    }
}

// Инициализация при загрузке страницы
let usbKeyboardAutoFill;
if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => {
        usbKeyboardAutoFill = new USBKeyboardAutoFill();
    });
} else {
    usbKeyboardAutoFill = new USBKeyboardAutoFill();
}

// Экспорт для использования в других модулях
if (typeof module !== 'undefined' && module.exports) {
    module.exports = { USBKeyboardAutoFill, usbKeyboardAutoFill };
}
