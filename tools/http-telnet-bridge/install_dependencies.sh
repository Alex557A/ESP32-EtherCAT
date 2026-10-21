#!/bin/bash
# =====================================================================
# HTTP-to-Telnet Bridge - install dependencies (Linux)
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

echo "[INFO] Установка зависимостей..."
npm install

if [ $? -eq 0 ]; then
    echo "[OK] Зависимости успешно установлены."
else
    echo "[ERROR] Ошибка при установке зависимостей."
    exit 1
fi




