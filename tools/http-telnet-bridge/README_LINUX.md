# Инструкция по запуску HTTP-to-Telnet Bridge в Linux

## Быстрый запуск

### Вариант 1: Использование ярлыка на рабочем столе (самый простой)

1. Создайте ярлыки (один раз):
   ```bash
   cd /media/alex/One/EtherCAT/ESP32_EtherCAT_Slave_4/tools/http-telnet-bridge
   ./create_desktop_shortcuts.sh
   ```

2. Двойной клик по ярлыку на рабочем столе:
   - **ESP32_Bridge_Start.desktop** - обычный запуск
   - **ESP32_Bridge_Dev.desktop** - режим разработки (автоперезагрузка)

### Вариант 2: Использование скрипта

1. Перейдите в директорию:
   ```bash
   cd /media/alex/One/EtherCAT/ESP32_EtherCAT_Slave_4/tools/http-telnet-bridge
   ```

2. Сделайте скрипт исполняемым (только первый раз):
   ```bash
   chmod +x start_server.sh
   ```

3. Запустите сервер:
   ```bash
   ./start_server.sh
   ```

### Вариант 3: Ручной запуск

1. Перейдите в директорию:
   ```bash
   cd /media/alex/One/EtherCAT/ESP32_EtherCAT_Slave_4/tools/http-telnet-bridge
   ```

2. Установите зависимости (только первый раз):
   ```bash
   npm install
   ```

3. Запустите сервер:
   ```bash
   npm start
   ```

## Первый запуск

### Шаг 1: Установка Node.js

Если Node.js не установлен, установите его:

**Ubuntu/Debian:**
```bash
sudo apt update
sudo apt install -y nodejs npm
```

**Проверка установки:**
```bash
node --version  # Должна быть версия 14 или выше
npm --version
```

**Альтернативный способ (через NodeSource):**
```bash
curl -fsSL https://deb.nodesource.com/setup_20.x | sudo -E bash -
sudo apt install -y nodejs
```

### Шаг 2: Установка зависимостей

```bash
cd /media/alex/One/EtherCAT/ESP32_EtherCAT_Slave_4/tools/http-telnet-bridge
chmod +x install_dependencies.sh
./install_dependencies.sh
```

Или вручную:
```bash
npm install
```

### Шаг 3: Запуск сервера

```bash
chmod +x start_server.sh
./start_server.sh
```

Или вручную:
```bash
npm start
```

## Использование

1. После запуска сервер будет доступен по адресу: `http://localhost:3000`
2. Bridge автоматически подключится к ESP32 по Telnet (`192.168.20.1:23`)
3. Откройте браузер и перейдите на `http://localhost:3000`

## Перезапуск сервера

**Вариант 1: Через ярлык**
- Двойной клик по **ESP32_Bridge_Restart.desktop** на рабочем столе

**Вариант 2: Через скрипт**
```bash
./restart_server.sh
```

Скрипт автоматически найдет и остановит процесс на порту 3000, затем запустит сервер заново.

## Остановка сервера

Нажмите **Ctrl+C** в терминале, где запущен сервер.

## Настройка

### Изменение порта HTTP

Установите переменную окружения перед запуском:
```bash
HTTP_PORT=3001 ./start_server.sh
```

Или измените в `bridge.js`:
```javascript
const HTTP_PORT = Number.parseInt(process.env.HTTP_PORT || process.env.PORT || '3000', 10);
```

### Изменение IP адреса ESP32

Отредактируйте файл `bridge.js`:
```javascript
const DEFAULT_TELNET_HOST = process.env.TELNET_HOST || '192.168.20.1';
```

Или установите переменную окружения:
```bash
TELNET_HOST=192.168.1.100 ./start_server.sh
```

## Возможные проблемы

### "Node.js не найден" или "node: команда не найдена"

**Решение:** Установите Node.js:
```bash
sudo apt update
sudo apt install -y nodejs npm
```

### "npm не распознается как команда"

**Решение:** npm должен устанавливаться вместе с Node.js. Переустановите:
```bash
sudo apt remove nodejs npm
sudo apt update
sudo apt install -y nodejs npm
```

### "Port 3000 already in use"

**Решение:** Порт занят другим приложением
- Измените порт: `HTTP_PORT=3001 ./start_server.sh`
- Или найдите и закройте процесс, использующий порт:
  ```bash
  sudo lsof -i :3000
  # или
  sudo netstat -tulpn | grep :3000
  ```

### "Cannot connect to Telnet"

**Решение:**
- Проверьте, что ESP32 запущен и доступен в сети
- Проверьте IP адрес ESP32 в `bridge.js` или через `TELNET_HOST`
- Убедитесь, что Telnet Server на ESP32 активен
- Проверьте сетевое подключение:
  ```bash
  ping 192.168.20.1
  telnet 192.168.20.1 23
  ```

### "Permission denied" при запуске скрипта

**Решение:** Сделайте скрипт исполняемым:
```bash
chmod +x start_server.sh
chmod +x install_dependencies.sh
chmod +x restart_server.sh
chmod +x create_desktop_shortcuts.sh
```

### Ярлыки на рабочем столе не запускаются

**Решение:** В некоторых системах нужно разрешить выполнение .desktop файлов:
1. Правой кнопкой мыши на ярлык -> **Свойства** (Properties)
2. Вкладка **Разрешения** (Permissions)
3. Отметьте **Разрешить выполнение файла как программы** (Allow executing file as program)
4. Закройте окно свойств

Или через терминал:
```bash
chmod +x ~/Desktop/ESP32_Bridge_Start.desktop
chmod +x ~/Desktop/ESP32_Bridge_Restart.desktop
chmod +x ~/Desktop/ESP32_Bridge_Dev.desktop
```

## Разработка с автоперезагрузкой

Для разработки используйте режим с автоматической перезагрузкой при изменении файлов.

**Вариант 1: Через ярлык (рекомендуется)**
- Двойной клик по **ESP32_Bridge_Dev.desktop** на рабочем столе

**Вариант 2: Через скрипт**
```bash
./start_dev.sh
```

**Вариант 3: Через npm**
```bash
npm run dev
```

**Что отслеживается:**
- `bridge.js` - основной файл сервера
- Файлы в папке `public/` (HTML, CSS, JS)

**Преимущества режима разработки:**
- Автоматическая перезагрузка при сохранении изменений
- Не нужно вручную перезапускать сервер
- Быстрая итерация при отладке
- Удобно для разработки веб-интерфейса

**Когда использовать:**
- При разработке и изменении кода
- При изменении веб-интерфейса
- Для быстрой отладки

**Когда НЕ использовать:**
- В продакшене (используйте `start_server.sh`)
- Если нужна стабильная работа без перезагрузок

Требуется установленный `nodemon` (устанавливается автоматически через `npm install`).

## Структура файлов

```
tools/http-telnet-bridge/
├── bridge.js                  ← Основной файл сервера
├── package.json               ← Зависимости Node.js
├── start_server.sh            ← Скрипт запуска (Linux)
├── start_dev.sh               ← Скрипт запуска в режиме разработки (автоперезагрузка)
├── restart_server.sh          ← Скрипт перезапуска (Linux)
├── install_dependencies.sh    ← Скрипт установки зависимостей
├── create_desktop_shortcuts.sh ← Скрипт создания ярлыков
├── nodemon.json                ← Конфигурация nodemon для автоперезагрузки
├── public/
│   └── index.html             ← Веб-интерфейс
└── README_LINUX.md            ← Эта инструкция
```

## API Endpoints

- `GET /api/stats` - Получить текущую статистику
- `GET /api/devices` - Список устройств
- `POST /api/devices/scan` - Сканирование цепочки устройств
- `POST /api/devices/active` - Установить активное устройство
- `GET /api/devices/:ip/gpio/channels` - Получить GPIO каналы
- И другие (см. `bridge.js`)

## WebSocket Events

- `stats_update` - Обновление статистики
- `telnet_status` - Статус подключения Telnet
- `telnet_response` - Ответы от Telnet
- `devices_update` - Обновление списка устройств

---

**Автор:** Федя  
**Версия:** 1.0  
**Платформа:** Linux (Ubuntu/Debian и другие дистрибутивы)

