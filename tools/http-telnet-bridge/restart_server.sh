#!/bin/bash
# =====================================================================
# HTTP-to-Telnet Bridge - restart server (Linux)
# - kills any process LISTENING on port 3000 (or HTTP_PORT)
# - starts start_server.sh
# =====================================================================

set -e

# Переход в директорию скрипта
cd "$(dirname "$0")"

HTTP_PORT=${HTTP_PORT:-${PORT:-3000}}

echo "[INFO] Перезапуск сервера на порту $HTTP_PORT..."

# Поиск процессов на порту
PIDS=""

# Используем lsof если доступен
if command -v lsof &> /dev/null; then
    PIDS=$(lsof -ti:$HTTP_PORT 2>/dev/null || true)
# Или используем ss
elif command -v ss &> /dev/null; then
    PIDS=$(ss -tulpn 2>/dev/null | grep ":$HTTP_PORT.*LISTEN" | grep -oP 'pid=\K[0-9]+' | sort -u || true)
# Или используем netstat
elif command -v netstat &> /dev/null; then
    PIDS=$(netstat -tulpn 2>/dev/null | grep ":$HTTP_PORT.*LISTEN" | grep -oP '[0-9]+/node' | cut -d'/' -f1 | sort -u || true)
fi

if [ -n "$PIDS" ]; then
    echo "[INFO] Остановка процессов на порту $HTTP_PORT:"
    for PID in $PIDS; do
        if [ -n "$PID" ] && kill -0 "$PID" 2>/dev/null; then
            echo "  - Остановка PID $PID"
            kill -TERM "$PID" 2>/dev/null || kill -KILL "$PID" 2>/dev/null || true
        fi
    done
    
    # Ждем завершения процессов
    sleep 2
    
    # Проверяем, остались ли процессы
    REMAINING=""
    if command -v lsof &> /dev/null; then
        REMAINING=$(lsof -ti:$HTTP_PORT 2>/dev/null || true)
    fi
    
    if [ -n "$REMAINING" ]; then
        echo "[WARNING] Некоторые процессы все еще работают, принудительное завершение..."
        for PID in $REMAINING; do
            kill -KILL "$PID" 2>/dev/null || true
        done
        sleep 1
    fi
else
    echo "[INFO] Нет процессов на порту $HTTP_PORT."
fi

# Проверка, что порт свободен
if command -v lsof &> /dev/null; then
    if lsof -ti:$HTTP_PORT &>/dev/null; then
        echo "[WARNING] Порт $HTTP_PORT все еще занят. Попробуйте запустить с правами sudo."
        exit 1
    fi
fi

echo "[INFO] Запуск сервера..."
echo ""

# Запуск start_server.sh
exec ./start_server.sh




