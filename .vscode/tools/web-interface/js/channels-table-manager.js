/**
 * Управление таблицей каналов
 * Добавляет кнопку удаления в колонку Actions и обновляет таблицу после удаления
 */

class ChannelsTableManager {
    constructor() {
        this.channels = [];
        this.tableId = 'channels-table';
        this.init();
    }
    
    /**
     * Инициализация
     */
    init() {
        // Ждем загрузки DOM
        if (document.readyState === 'loading') {
            document.addEventListener('DOMContentLoaded', () => {
                setTimeout(() => {
                    this.setupTable();
                    this.loadChannels();
                }, 100);
            });
        } else {
            setTimeout(() => {
                this.setupTable();
                this.loadChannels();
            }, 100);
        }
        
        // Наблюдаем за изменениями DOM для динамически создаваемых таблиц
        this.observeTableChanges();
        
        // Периодическая проверка таблицы (на случай, если она создается очень поздно)
        setInterval(() => {
            const table = this.findChannelsTable();
            if (table) {
                const rows = table.querySelectorAll('tbody tr, tr:not(thead tr)');
                const rowsWithButtons = table.querySelectorAll('.delete-channel-btn').length;
                if (rows.length > 0 && rows.length !== rowsWithButtons) {
                    console.log(`[ChannelsTable] Обнаружены строки без кнопок (${rows.length} строк, ${rowsWithButtons} кнопок), обновляем...`);
                    this.setupTable();
                }
            }
        }, 2000);
    }
    
    /**
     * Настройка таблицы
     */
    setupTable() {
        // Ищем таблицу каналов
        const table = this.findChannelsTable();
        if (!table) {
            console.warn('[ChannelsTable] Таблица каналов не найдена, будет повторная попытка...');
            return false;
        }
        
        console.log('[ChannelsTable] Таблица найдена, настраиваем...');
        
        // Проверяем наличие колонки Actions
        this.ensureActionsColumn(table);
        
        // Добавляем кнопки удаления в существующие строки
        this.addDeleteButtons(table);
        
        return true;
    }
    
    /**
     * Поиск таблицы каналов
     */
    findChannelsTable() {
        // Пробуем разные селекторы
        const selectors = [
            `#${this.tableId}`,
            'table[id*="channel"]',
            'table[id*="channels"]',
            '.channels-table',
            'table.channels',
            'table[data-role="channels"]',
            'table tbody', // Любая таблица с tbody
            'table' // Любая таблица
        ];
        
        for (const selector of selectors) {
            const element = document.querySelector(selector);
            if (element) {
                const table = element.tagName === 'TABLE' ? element : element.closest('table');
                if (table) {
                    console.log(`[ChannelsTable] Таблица найдена по селектору: ${selector}`);
                    return table;
                }
            }
        }
        
        // Если не нашли, ищем любую таблицу с заголовками, содержащими "Channel" или "Канал"
        const allTables = document.querySelectorAll('table');
        for (const table of allTables) {
            const headers = table.querySelectorAll('th');
            for (const header of headers) {
                const text = header.textContent.toLowerCase();
                if (text.includes('channel') || text.includes('канал') || text.includes('name') || text.includes('имя') || 
                    text.includes('pdo') || text.includes('usb') || text.includes('index')) {
                    console.log(`[ChannelsTable] Таблица найдена по заголовку: ${text}`);
                    return table;
                }
            }
        }
        
        console.warn('[ChannelsTable] Таблица не найдена. Всего таблиц на странице:', document.querySelectorAll('table').length);
        return null;
    }
    
    /**
     * Обеспечение наличия колонки Actions
     */
    ensureActionsColumn(table) {
        const thead = table.querySelector('thead');
        if (!thead) {
            console.warn('[ChannelsTable] Таблица не имеет thead');
            return;
        }
        
        const headers = thead.querySelectorAll('th');
        let hasActions = false;
        
        // Проверяем, есть ли колонка Actions
        for (const header of headers) {
            const text = header.textContent.toLowerCase().trim();
            if (text.includes('action') || text.includes('действие') || text === 'actions' || text === 'действия') {
                hasActions = true;
                break;
            }
        }
        
        // Если колонки Actions нет, добавляем её
        if (!hasActions) {
            const headerRow = thead.querySelector('tr');
            if (headerRow) {
                const actionsHeader = document.createElement('th');
                actionsHeader.textContent = 'Actions';
                actionsHeader.className = 'actions-column';
                actionsHeader.style.width = '100px';
                actionsHeader.style.textAlign = 'center';
                headerRow.appendChild(actionsHeader);
            }
        }
    }
    
    /**
     * Добавление кнопок удаления в строки таблицы
     */
    addDeleteButtons(table) {
        const tbody = table.querySelector('tbody');
        if (!tbody) {
            console.warn('[ChannelsTable] Таблица не имеет tbody, пробуем найти строки напрямую');
            // Пробуем найти строки без tbody
            const rows = table.querySelectorAll('tr:not(thead tr)');
            if (rows.length === 0) {
                console.warn('[ChannelsTable] Строки не найдены');
                return;
            }
            this.addButtonsToRows(rows, table);
            return;
        }
        
        const rows = tbody.querySelectorAll('tr');
        console.log(`[ChannelsTable] Найдено строк: ${rows.length}`);
        this.addButtonsToRows(rows, table);
    }
    
    /**
     * Добавление кнопок в строки
     */
    addButtonsToRows(rows, table) {
        rows.forEach((row, index) => {
            // Проверяем, есть ли уже кнопка удаления
            if (row.querySelector('.delete-channel-btn')) {
                return; // Кнопка уже есть
            }
            
            // Находим или создаем ячейку Actions
            let actionsCell = row.querySelector('td.actions-cell');
            
            // Если ячейки Actions нет, проверяем последнюю ячейку
            if (!actionsCell) {
                const cells = row.querySelectorAll('td');
                const lastCell = cells[cells.length - 1];
                
                // Проверяем, является ли последняя колонка Actions
                const headers = table.querySelectorAll('thead th, thead td');
                const lastHeader = headers[headers.length - 1];
                const isActionsColumn = lastHeader && (
                    lastHeader.textContent.toLowerCase().includes('action') ||
                    lastHeader.textContent.toLowerCase().includes('действие') ||
                    lastHeader.classList.contains('actions-column')
                );
                
                if (isActionsColumn && lastCell) {
                    actionsCell = lastCell;
                    actionsCell.className = 'actions-cell';
                } else {
                    // Создаем новую ячейку Actions
                    actionsCell = document.createElement('td');
                    actionsCell.className = 'actions-cell';
                    actionsCell.style.textAlign = 'center';
                    actionsCell.style.width = '100px';
                    row.appendChild(actionsCell);
                }
            }
            
            // Создаем кнопку удаления
            const deleteBtn = document.createElement('button');
            deleteBtn.className = 'delete-channel-btn';
            deleteBtn.textContent = 'Удалить';
            deleteBtn.type = 'button';
            deleteBtn.setAttribute('data-channel-index', index);
            deleteBtn.setAttribute('data-channel-id', row.dataset.channelId || index);
            
            // Добавляем обработчик события
            deleteBtn.addEventListener('click', (e) => {
                e.preventDefault();
                e.stopPropagation();
                this.deleteChannel(row, index);
            });
            
            actionsCell.appendChild(deleteBtn);
            console.log(`[ChannelsTable] Кнопка удаления добавлена в строку ${index + 1}`);
        });
    }
    
    /**
     * Удаление канала
     */
    async deleteChannel(row, index) {
        const channelId = row.dataset.channelId || index;
        const channelName = row.querySelector('td:first-child')?.textContent || 
                           row.querySelector('td[data-name]')?.textContent || 
                           `Канал ${index + 1}`;
        
        // Подтверждение удаления
        if (!confirm(`Вы уверены, что хотите удалить канал "${channelName}"?`)) {
            return;
        }
        
        try {
            // Пробуем удалить через API
            const response = await fetch(`/api/channels/${channelId}`, {
                method: 'DELETE',
                headers: {
                    'Content-Type': 'application/json'
                }
            });
            
            if (response.ok) {
                // Успешно удалено через API
                this.removeChannelRow(row);
                this.showNotification('Канал успешно удален', 'success');
                return;
            } else {
                throw new Error(`Ошибка удаления: ${response.status} ${response.statusText}`);
            }
        } catch (error) {
            console.warn('[ChannelsTable] API недоступен, удаление локально:', error);
            
            // Удаляем локально
            this.removeChannelRow(row);
            this.showNotification('Канал удален локально', 'warning');
        }
    }
    
    /**
     * Удаление строки канала из таблицы
     */
    removeChannelRow(row) {
        // Удаляем строку
        row.style.transition = 'opacity 0.3s';
        row.style.opacity = '0';
        
        setTimeout(() => {
            row.remove();
            
            // Обновляем индексы оставшихся кнопок
            this.updateButtonIndexes();
            
            // Перерисовываем таблицу
            this.refreshTable();
            
            // Обновляем данные каналов
            this.updateChannelsData();
            
            console.log('[ChannelsTable] Строка удалена, таблица обновлена');
        }, 300);
    }
    
    /**
     * Обновление данных каналов после удаления
     */
    updateChannelsData() {
        const table = this.findChannelsTable();
        if (!table) return;
        
        const tbody = table.querySelector('tbody');
        if (!tbody) return;
        
        const rows = tbody.querySelectorAll('tr');
        this.channels = [];
        
        rows.forEach((row, index) => {
            const channel = {
                id: row.dataset.channelId || index,
                name: row.querySelector('td:first-child')?.textContent || `Канал ${index + 1}`
            };
            
            // Извлекаем данные из ячеек
            const cells = row.querySelectorAll('td');
            cells.forEach((cell, cellIndex) => {
                const header = table.querySelector(`thead th:nth-child(${cellIndex + 1})`);
                if (header) {
                    const headerText = header.textContent.toLowerCase().trim();
                    if (headerText.includes('category') || headerText.includes('категория')) {
                        channel.category = cell.textContent;
                    } else if (headerText.includes('usb') || headerText.includes('keycode')) {
                        channel.usb_keycode = cell.textContent;
                    } else if (headerText.includes('pdo byte')) {
                        channel.pdo_byte = parseInt(cell.textContent) || 0;
                    } else if (headerText.includes('index')) {
                        channel.index = parseInt(cell.textContent) || 0;
                    }
                }
            });
            
            this.channels.push(channel);
        });
    }
    
    /**
     * Обновление индексов кнопок после удаления
     */
    updateButtonIndexes() {
        const table = this.findChannelsTable();
        if (!table) return;
        
        const tbody = table.querySelector('tbody');
        if (!tbody) return;
        
        const rows = tbody.querySelectorAll('tr');
        rows.forEach((row, index) => {
            const deleteBtn = row.querySelector('.delete-channel-btn');
            if (deleteBtn) {
                deleteBtn.setAttribute('data-channel-index', index);
            }
        });
    }
    
    /**
     * Перерисовка таблицы
     */
    refreshTable() {
        const table = this.findChannelsTable();
        if (!table) return;
        
        const tbody = table.querySelector('tbody');
        if (!tbody) return;
        
        // Удаляем все кнопки удаления из ячеек Actions
        const actionsCells = table.querySelectorAll('td.actions-cell');
        actionsCells.forEach(cell => {
            const btn = cell.querySelector('.delete-channel-btn');
            if (btn) btn.remove();
        });
        
        // Добавляем кнопки заново во все строки
        this.addDeleteButtons(table);
        
        // Обновляем видимость колонок в зависимости от категории
        if (typeof usbKeyboardAutoFill !== 'undefined' && usbKeyboardAutoFill.updateChannelsTable) {
            const categorySelect = document.querySelector('select[name="category"], select[id*="category"]');
            if (categorySelect) {
                usbKeyboardAutoFill.updateChannelsTable(categorySelect.value);
            }
        }
        
        // Триггерим событие обновления таблицы
        const event = new CustomEvent('channelsTableRefreshed', { 
            detail: { channels: this.channels } 
        });
        document.dispatchEvent(event);
        
        console.log('[ChannelsTable] Таблица перерисовена');
    }
    
    /**
     * Загрузка каналов
     */
    async loadChannels() {
        try {
            const response = await fetch('/api/channels');
            if (response.ok) {
                const data = await response.json();
                this.channels = data.channels || data || [];
                this.renderTable();
            }
        } catch (error) {
            console.warn('[ChannelsTable] Не удалось загрузить каналы:', error);
            // Продолжаем работу с локальными данными
        }
    }
    
    /**
     * Отрисовка таблицы
     */
    renderTable() {
        const table = this.findChannelsTable();
        if (!table) return;
        
        const tbody = table.querySelector('tbody');
        if (!tbody) return;
        
        // Очищаем таблицу
        tbody.innerHTML = '';
        
        // Добавляем строки для каждого канала
        this.channels.forEach((channel, index) => {
            const row = this.createChannelRow(channel, index);
            tbody.appendChild(row);
        });
        
        // Добавляем кнопки удаления
        this.addDeleteButtons(table);
    }
    
    /**
     * Создание строки канала
     */
    createChannelRow(channel, index) {
        const row = document.createElement('tr');
        row.dataset.channelId = channel.id || index;
        
        // Добавляем ячейки в зависимости от типа канала
        const category = (channel.category || channel.type || '').toLowerCase();
        const isUSB = category.includes('usb') || category.includes('keyboard');
        
        // Имя канала
        const nameCell = document.createElement('td');
        nameCell.textContent = channel.name || `Канал ${index + 1}`;
        row.appendChild(nameCell);
        
        // Категория
        if (!isUSB || table.querySelector('th:contains("Категория")')) {
            const categoryCell = document.createElement('td');
            categoryCell.textContent = channel.category || channel.type || '';
            row.appendChild(categoryCell);
        }
        
        // USB key code (только для USB)
        if (isUSB) {
            const usbCodeCell = document.createElement('td');
            usbCodeCell.textContent = channel.usb_keycode || channel.keycode_hex || '';
            row.appendChild(usbCodeCell);
        }
        
        // PDO Byte
        if (channel.pdo_byte !== undefined) {
            const pdoByteCell = document.createElement('td');
            pdoByteCell.textContent = channel.pdo_byte;
            row.appendChild(pdoByteCell);
        }
        
        // PDO Bit (только для GPIO)
        if (!isUSB && channel.pdo_bit !== undefined) {
            const pdoBitCell = document.createElement('td');
            pdoBitCell.textContent = channel.pdo_bit;
            row.appendChild(pdoBitCell);
        }
        
        // Index
        if (channel.index !== undefined) {
            const indexCell = document.createElement('td');
            indexCell.textContent = channel.index;
            row.appendChild(indexCell);
        }
        
        return row;
    }
    
    /**
     * Наблюдение за изменениями таблицы
     */
    observeTableChanges() {
        const observer = new MutationObserver((mutations) => {
            let shouldRefresh = false;
            let tableFound = false;
            
            mutations.forEach((mutation) => {
                // Проверяем, появилась ли таблица
                if (mutation.addedNodes.length > 0) {
                    mutation.addedNodes.forEach((node) => {
                        if (node.nodeType === 1) {
                            // Если добавлена таблица
                            if (node.tagName === 'TABLE' || node.querySelector('table')) {
                                tableFound = true;
                            }
                            // Если добавлены строки
                            if (node.tagName === 'TR' || node.querySelector('tr')) {
                                shouldRefresh = true;
                            }
                        }
                    });
                }
                
                // Если удалены строки
                if (mutation.removedNodes.length > 0) {
                    mutation.removedNodes.forEach((node) => {
                        if (node.nodeType === 1 && (node.tagName === 'TR' || node.querySelector('tr'))) {
                            shouldRefresh = true;
                        }
                    });
                }
            });
            
            // Если таблица найдена, настраиваем её
            if (tableFound) {
                setTimeout(() => {
                    this.setupTable();
                    this.refreshTable();
                }, 100);
            }
            
            // Если нужно обновить
            if (shouldRefresh) {
                setTimeout(() => {
                    this.refreshTable();
                }, 100);
            }
        });
        
        // Наблюдаем за изменениями в body
        observer.observe(document.body, {
            childList: true,
            subtree: true
        });
        
        // Также периодически проверяем наличие таблицы (на случай, если она создается асинхронно)
        const checkInterval = setInterval(() => {
            const table = this.findChannelsTable();
            if (table && !table.dataset.channelsTableInitialized) {
                table.dataset.channelsTableInitialized = 'true';
                this.setupTable();
                this.refreshTable();
            }
        }, 1000);
        
        // Останавливаем проверку через 30 секунд
        setTimeout(() => {
            clearInterval(checkInterval);
        }, 30000);
    }
    
    /**
     * Публичный метод для обновления таблицы извне
     */
    refresh() {
        this.refreshTable();
    }
    
    /**
     * Публичный метод для добавления канала
     */
    addChannel(channel) {
        this.channels.push(channel);
        this.renderTable();
    }
    
    /**
     * Показать уведомление
     */
    showNotification(message, type = 'info') {
        // Создаем элемент уведомления
        const notification = document.createElement('div');
        notification.className = `notification notification-${type}`;
        notification.textContent = message;
        notification.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            padding: 15px 20px;
            background: ${type === 'success' ? '#4caf50' : type === 'error' ? '#f44336' : '#2196f3'};
            color: white;
            border-radius: 4px;
            box-shadow: 0 2px 5px rgba(0,0,0,0.2);
            z-index: 10000;
            animation: slideIn 0.3s ease-out;
        `;
        
        document.body.appendChild(notification);
        
        // Удаляем через 3 секунды
        setTimeout(() => {
            notification.style.animation = 'slideOut 0.3s ease-out';
            setTimeout(() => {
                notification.remove();
            }, 300);
        }, 3000);
    }
}

// Инициализация
let channelsTableManager;

function initChannelsTableManager() {
    if (!channelsTableManager) {
        channelsTableManager = new ChannelsTableManager();
        
        // Делаем доступным глобально
        if (typeof window !== 'undefined') {
            window.channelsTableManager = channelsTableManager;
        }
        
        console.log('[ChannelsTable] Модуль инициализирован');
    }
    return channelsTableManager;
}

if (document.readyState === 'loading') {
    document.addEventListener('DOMContentLoaded', () => {
        initChannelsTableManager();
    });
} else {
    initChannelsTableManager();
}

// Экспорт для использования в других модулях
if (typeof window !== 'undefined') {
    window.ChannelsTableManager = ChannelsTableManager;
    window.initChannelsTableManager = initChannelsTableManager;
}

if (typeof module !== 'undefined' && module.exports) {
    module.exports = { ChannelsTableManager, channelsTableManager, initChannelsTableManager };
}
