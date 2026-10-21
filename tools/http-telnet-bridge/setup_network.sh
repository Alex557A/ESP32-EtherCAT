#!/bin/bash
# =====================================================================
# Настройка сетевого интерфейса для параллельной работы
# EtherCAT (MACRAW) и Telnet (TCP/IP)
# =====================================================================

set -e

# Конфигурация
ETHERCAT_INTERFACE="${ETHERCAT_INTERFACE:-enp3s0}"
TELNET_NETWORK="${TELNET_NETWORK:-192.168.20.0}"
TELNET_NETMASK="${TELNET_NETMASK:-255.255.255.0}"
HOST_IP="${HOST_IP:-192.168.20.100}"
ESP32_IP="${ESP32_IP:-192.168.20.1}"

echo "=========================================="
echo "Настройка сети для EtherCAT + Telnet"
echo "=========================================="
echo ""
echo "Интерфейс: $ETHERCAT_INTERFACE"
echo "Сеть Telnet: $TELNET_NETWORK/$TELNET_NETMASK"
echo "IP хоста: $HOST_IP"
echo "IP ESP32: $ESP32_IP"
echo ""

# Проверка прав root
if [ "$EUID" -ne 0 ]; then 
    echo "[ERROR] Скрипт должен запускаться с правами root (sudo)"
    echo "Использование: sudo $0"
    exit 1
fi

# Проверка существования интерфейса
if ! ip link show "$ETHERCAT_INTERFACE" &>/dev/null; then
    echo "[ERROR] Интерфейс $ETHERCAT_INTERFACE не найден"
    echo "Доступные интерфейсы:"
    ip link show | grep -E "^[0-9]+:" | awk '{print $2}' | sed 's/:$//'
    exit 1
fi

# Проверка текущей конфигурации
CURRENT_IP=$(ip addr show "$ETHERCAT_INTERFACE" | grep -oP 'inet \K[\d.]+' || echo "")
if [ -n "$CURRENT_IP" ]; then
    echo "[INFO] Текущий IP на $ETHERCAT_INTERFACE: $CURRENT_IP"
    read -p "Продолжить и изменить IP? (y/n) " -n 1 -r
    echo
    if [[ ! $REPLY =~ ^[Yy]$ ]]; then
        exit 0
    fi
fi

echo "[INFO] Настройка интерфейса $ETHERCAT_INTERFACE..."

# Удаление существующего IP (если есть)
ip addr flush dev "$ETHERCAT_INTERFACE" 2>/dev/null || true

# Добавление IP адреса
ip addr add "$HOST_IP/24" dev "$ETHERCAT_INTERFACE" 2>/dev/null || {
    echo "[WARNING] Не удалось добавить IP адрес (возможно, уже существует)"
}

# Включение интерфейса
ip link set "$ETHERCAT_INTERFACE" up

# Добавление статического маршрута (если нужно)
if ! ip route | grep -q "$TELNET_NETWORK"; then
    ip route add "$TELNET_NETWORK/24" dev "$ETHERCAT_INTERFACE" 2>/dev/null || {
        echo "[WARNING] Не удалось добавить маршрут (возможно, уже существует)"
    }
fi

# Проверка результата
echo ""
echo "[INFO] Проверка конфигурации..."
NEW_IP=$(ip addr show "$ETHERCAT_INTERFACE" | grep -oP 'inet \K[\d.]+' || echo "")
if [ -n "$NEW_IP" ]; then
    echo "[OK] IP адрес установлен: $NEW_IP"
else
    echo "[ERROR] Не удалось установить IP адрес"
    exit 1
fi

# Проверка доступности ESP32
echo ""
echo "[INFO] Проверка доступности ESP32 ($ESP32_IP)..."
if ping -c 2 -W 2 "$ESP32_IP" &>/dev/null; then
    echo "[OK] ESP32 доступен по ping"
else
    echo "[WARNING] ESP32 недоступен по ping (это нормально, если он еще не запущен)"
fi

# Проверка Telnet
echo ""
echo "[INFO] Проверка Telnet соединения..."
if command -v nc &>/dev/null; then
    if timeout 2 nc -z "$ESP32_IP" 23 2>/dev/null; then
        echo "[OK] Telnet порт 23 открыт на ESP32"
    else
        echo "[WARNING] Telnet порт 23 недоступен (ESP32 может быть не запущен)"
    fi
else
    echo "[INFO] Установите 'netcat' для проверки портов: sudo apt install netcat"
fi

echo ""
echo "=========================================="
echo "Настройка завершена!"
echo "=========================================="
echo ""
echo "Текущая конфигурация:"
ip addr show "$ETHERCAT_INTERFACE" | grep -E "inet |state"
echo ""
echo "Маршруты к сети $TELNET_NETWORK:"
ip route | grep "$TELNET_NETWORK" || echo "  (нет маршрутов)"
echo ""
echo "Для постоянной настройки создайте файл:"
echo "  /etc/network/interfaces.d/$ETHERCAT_INTERFACE"
echo ""
echo "Или используйте netplan (Ubuntu 18.04+):"
echo "  /etc/netplan/01-ethercat-telnet.yaml"
echo ""




