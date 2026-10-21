# HTTP-to-Telnet Bridge

Мост между веб-интерфейсом и Telnet сервером ESP32 EtherCAT Slave v4.

## Установка

```bash
cd tools/http-telnet-bridge
npm install
```

## Запуск

```bash
npm start
```

Или для разработки с автоперезагрузкой:

```bash
npm run dev
```

## Использование

1. Запустите bridge: `npm start`
2. Откройте браузер: `http://localhost:3000`
3. Bridge автоматически подключится к ESP32 по Telnet (`192.168.20.1:23`)

## Функции

- ✅ Real-time статистика через WebSocket
- ✅ Управление тестами (start/stop)
- ✅ Отображение EtherCAT статистики
- ✅ Отображение тестовой статистики
- ✅ Отображение latency (min/max/avg)
- ✅ Автоматическое переподключение при разрыве связи

## API Endpoints

- `GET /api/stats` - Получить текущую статистику
- `POST /api/test/start` - Запустить тест (`{ period: "1ms" | "4ms" }`)
- `POST /api/test/stop` - Остановить тест
- `POST /api/test/stats` - Запросить статистику теста

## WebSocket Events

- `stats_update` - Обновление статистики
- `telnet_status` - Статус подключения Telnet
- `telnet_response` - Ответы от Telnet

## Конфигурация

Измените в `bridge.js`:

```javascript
const TELNET_HOST = '192.168.20.1';  // IP ESP32
const TELNET_PORT = 23;              // Telnet порт
const HTTP_PORT = 3000;              // HTTP порт
```

---

**Автор:** Федя  
**Версия:** 1.0
