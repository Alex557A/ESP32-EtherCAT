#!/bin/bash
# =====================================================================
# HTTP-to-Telnet Bridge - start server in development mode (Linux)
# Автоматическая перезагрузка при изменении файлов
# =====================================================================

set -e

# Переход в директорию скрипта
cd "$(dirname "$0")"

# Проверка Node.js
if ! command -v node &> /dev/null; then
    echo "[ERROR] Node.js не найден."
    echo "[INFO] Установите Node.js:"
    echo "  Ubuntu/Debian: sudo apt update && sudo apt install -y nodejs npm"
    exit 1
fi

# Проверка npm
if ! command -v npm &> /dev/null; then
    echo "[ERROR] npm не найден."
    exit 1
fi

# Вывод версий
echo "[OK] node:"
node --version
echo "[OK] npm:"
npm --version
echo ""

# Проверка node_modules
if [ ! -d "node_modules" ]; then
    echo "[INFO] Установка зависимостей (npm install)..."
    npm install
    if [ $? -ne 0 ]; then
        echo "[ERROR] npm install failed."
        exit 1
    fi
    echo "[OK] Зависимости установлены."
    echo ""
fi

# Проверка nodemon
if [ ! -f "node_modules/.bin/nodemon" ]; then
    echo "[INFO] Установка nodemon..."
    npm install --save-dev nodemon
fi

HTTP_PORT=${HTTP_PORT:-3000}

# Проверка занятости порта
if command -v lsof &> /dev/null; then
    if lsof -ti:$HTTP_PORT &>/dev/null; then
        echo "[WARNING] Порт $HTTP_PORT уже занят."
        echo "[INFO] Используйте ./restart_server.sh для остановки старого процесса"
        read -p "Продолжить? (y/n) " -n 1 -r
        echo
        if [[ ! $REPLY =~ ^[Yy]$ ]]; then
            exit 1
        fi
    fi
fi

# Запуск в режиме разработки
echo "[INFO] Запуск сервера в режиме разработки (с автоперезагрузкой)..."
echo "[INFO] URL: http://localhost:$HTTP_PORT"
echo "[INFO] Режим: DEVELOPMENT (автоперезагрузка включена)"
echo "[INFO] Отслеживаемые файлы: bridge.js, public/*"
echo "[INFO] Остановка: Ctrl+C"
echo ""

# Попытка открыть браузер (опционально)
if command -v xdg-open &> /dev/null; then
    (sleep 2 && xdg-open "http://localhost:$HTTP_PORT" 2>/dev/null) &
elif command -v gnome-open &> /dev/null; then
    (sleep 2 && gnome-open "http://localhost:$HTTP_PORT" 2>/dev/null) &
fi

# Запуск через npm run dev (nodemon)
npm run dev

echo ""
echo "[INFO] Сервер остановлен."




