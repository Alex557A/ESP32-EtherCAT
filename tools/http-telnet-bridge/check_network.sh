#!/bin/bash
# =====================================================================
# Проверка сетевой конфигурации для EtherCAT + Telnet
# =====================================================================

ETHERCAT_INTERFACE="${ETHERCAT_INTERFACE:-enp3s0}"
ESP32_IP="${ESP32_IP:-192.168.20.1}"

echo "=========================================="
echo "Проверка сетевой конфигурации"
echo "=========================================="
echo ""

# Проверка интерфейса
echo "[1] Проверка интерфейса $ETHERCAT_INTERFACE:"
if ip link show "$ETHERCAT_INTERFACE" &>/dev/null; then
    STATE=$(ip link show "$ETHERCAT_INTERFACE" | grep -oP 'state \K\w+')
    echo "  ✓ Интерфейс существует (state: $STATE)"
    
    IP=$(ip addr show "$ETHERCAT_INTERFACE" | grep -oP 'inet \K[\d.]+' || echo "не настроен")
    if [ "$IP" != "не настроен" ]; then
        echo "  ✓ IP адрес: $IP"
        
        # Проверка сети
        if echo "$IP" | grep -q "^192\.168\.20\."; then
            echo "  ✓ IP в правильной сети (192.168.20.0/24)"
        else
            echo "  ⚠ IP не в сети 192.168.20.0/24"
        fi
    else
        echo "  ✗ IP адрес не настроен"
    fi
else
    echo "  ✗ Интерфейс не найден"
fi

echo ""

# Проверка маршрутов
echo "[2] Проверка маршрутов к сети 192.168.20.0/24:"
ROUTES=$(ip route | grep "192.168.20.0/24" || echo "")
if [ -n "$ROUTES" ]; then
    echo "  ✓ Маршруты найдены:"
    echo "$ROUTES" | sed 's/^/    /'
else
    echo "  ✗ Маршруты не найдены"
fi

echo ""

# Проверка доступности ESP32
echo "[3] Проверка доступности ESP32 ($ESP32_IP):"
if ping -c 1 -W 2 "$ESP32_IP" &>/dev/null; then
    echo "  ✓ ESP32 доступен по ping"
else
    echo "  ✗ ESP32 недоступен по ping"
fi

echo ""

# Проверка Telnet порта
echo "[4] Проверка Telnet порта (23):"
if command -v nc &>/dev/null; then
    if timeout 2 nc -z "$ESP32_IP" 23 2>/dev/null; then
        echo "  ✓ Telnet порт 23 открыт"
    else
        echo "  ✗ Telnet порт 23 недоступен"
    fi
else
    echo "  ⚠ netcat не установлен (установите: sudo apt install netcat)"
fi

echo ""

# Проверка работы bridge сервера
echo "[5] Проверка HTTP-to-Telnet Bridge:"
if curl -s http://localhost:3000 &>/dev/null; then
    echo "  ✓ HTTP сервер работает на порту 3000"
else
    echo "  ✗ HTTP сервер не отвечает на порту 3000"
fi

echo ""
echo "=========================================="
echo "Рекомендации:"
echo "=========================================="
echo ""

# Даем рекомендации
if ! ip addr show "$ETHERCAT_INTERFACE" | grep -q "inet.*192\.168\.20\."; then
    echo "1. Настройте IP адрес на интерфейсе $ETHERCAT_INTERFACE:"
    echo "   sudo ./setup_network.sh"
    echo ""
fi

if ! ping -c 1 -W 2 "$ESP32_IP" &>/dev/null; then
    echo "2. ESP32 недоступен. Проверьте:"
    echo "   - Подключен ли ESP32 к сети"
    echo "   - Правильный ли IP адрес ESP32 ($ESP32_IP)"
    echo "   - Запущен ли Telnet сервер на ESP32"
    echo ""
fi

if ! curl -s http://localhost:3000 &>/dev/null; then
    echo "3. Запустите HTTP-to-Telnet Bridge:"
    echo "   ./start_server.sh"
    echo "   или"
    echo "   ./start_dev.sh (для разработки)"
    echo ""
fi

echo "=========================================="




