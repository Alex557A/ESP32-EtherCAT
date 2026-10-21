#!/bin/bash
# =====================================================================
# HTTP-to-Telnet Bridge - start server (Linux)
# =====================================================================

set -e

# Переход в директорию скрипта
cd "$(dirname "$0")"

# Проверка Node.js
if ! command -v node &> /dev/null; then
    echo "[ERROR] Node.js не найден."
    echo "[INFO] Установите Node.js:"
    echo "  Ubuntu/Debian: sudo apt update && sudo apt install -y nodejs npm"
    echo "  Или скачайте с: https://nodejs.org/"
    exit 1
fi

# Проверка npm
if ! command -v npm &> /dev/null; then
    echo "[ERROR] npm не найден."
    echo "[INFO] Установите npm вместе с Node.js"
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

# Проверка занятости порта
HTTP_PORT=${HTTP_PORT:-3000}
if command -v netstat &> /dev/null; then
    if netstat -tuln 2>/dev/null | grep -q ":$HTTP_PORT.*LISTEN"; then
        echo "[ERROR] Порт $HTTP_PORT уже занят."
        echo "[INFO] Закройте другой сервер или измените HTTP_PORT в bridge.js"
        exit 1
    fi
elif command -v ss &> /dev/null; then
    if ss -tuln 2>/dev/null | grep -q ":$HTTP_PORT.*LISTEN"; then
        echo "[ERROR] Порт $HTTP_PORT уже занят."
        echo "[INFO] Закройте другой сервер или измените HTTP_PORT в bridge.js"
        exit 1
    fi
fi

# Запуск сервера
echo "[INFO] Запуск сервера..."
echo "[INFO] URL: http://localhost:$HTTP_PORT"
echo "[INFO] Остановка: Ctrl+C"
echo ""

# Попытка открыть браузер (опционально)
if command -v xdg-open &> /dev/null; then
    (sleep 2 && xdg-open "http://localhost:$HTTP_PORT" 2>/dev/null) &
elif command -v gnome-open &> /dev/null; then
    (sleep 2 && gnome-open "http://localhost:$HTTP_PORT" 2>/dev/null) &
fi

# Запуск через npm
npm start

echo ""
echo "[INFO] Сервер остановлен."




