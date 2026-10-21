#!/bin/bash
# =====================================================================
# Создание постоянной конфигурации сети для EtherCAT + Telnet
# =====================================================================

set -e

# Конфигурация
ETHERCAT_INTERFACE="${ETHERCAT_INTERFACE:-enp3s0}"
HOST_IP="${HOST_IP:-192.168.20.100}"
NETMASK="${NETMASK:-255.255.255.0}"

echo "=========================================="
echo "Создание постоянной конфигурации сети"
echo "=========================================="
echo ""
echo "Интерфейс: $ETHERCAT_INTERFACE"
echo "IP адрес: $HOST_IP"
echo ""

# Проверка прав root
if [ "$EUID" -ne 0 ]; then 
    echo "[ERROR] Скрипт должен запускаться с правами root (sudo)"
    echo "Использование: sudo $0"
    exit 1
fi

# Определение системы управления сетью
if [ -d "/etc/netplan" ]; then
    echo "[INFO] Обнаружен netplan (Ubuntu 18.04+)"
    
    CONFIG_FILE="/etc/netplan/01-ethercat-telnet.yaml"
    
    cat > "$CONFIG_FILE" << EOF
# Конфигурация сети для EtherCAT + Telnet
# Создано автоматически
network:
  version: 2
  renderer: networkd
  ethernets:
    $ETHERCAT_INTERFACE:
      addresses:
        - $HOST_IP/24
      # Не используем DHCP для этого интерфейса
      dhcp4: false
      # Опционально: отключить автоматическое управление
      # optional: true
EOF
    
    echo "[OK] Создан файл конфигурации: $CONFIG_FILE"
    echo ""
    echo "[INFO] Применение конфигурации..."
    netplan apply
    
elif [ -f "/etc/network/interfaces" ] || [ -d "/etc/network/interfaces.d" ]; then
    echo "[INFO] Обнаружен /etc/network/interfaces"
    
    CONFIG_FILE="/etc/network/interfaces.d/$ETHERCAT_INTERFACE"
    mkdir -p /etc/network/interfaces.d
    
    cat > "$CONFIG_FILE" << EOF
# Конфигурация сети для EtherCAT + Telnet
# Создано автоматически
auto $ETHERCAT_INTERFACE
iface $ETHERCAT_INTERFACE inet static
    address $HOST_IP
    netmask $NETMASK
    # Не используем gateway для этого интерфейса
EOF
    
    echo "[OK] Создан файл конфигурации: $CONFIG_FILE"
    echo ""
    echo "[INFO] Применение конфигурации..."
    ifdown "$ETHERCAT_INTERFACE" 2>/dev/null || true
    ifup "$ETHERCAT_INTERFACE" 2>/dev/null || systemctl restart networking
    
else
    echo "[ERROR] Не удалось определить систему управления сетью"
    echo "[INFO] Создайте конфигурацию вручную:"
    echo ""
    echo "Для netplan (/etc/netplan/01-ethercat-telnet.yaml):"
    echo "  network:"
    echo "    version: 2"
    echo "    ethernets:"
    echo "      $ETHERCAT_INTERFACE:"
    echo "        addresses: [$HOST_IP/24]"
    echo ""
    echo "Для /etc/network/interfaces:"
    echo "  auto $ETHERCAT_INTERFACE"
    echo "  iface $ETHERCAT_INTERFACE inet static"
    echo "    address $HOST_IP"
    echo "    netmask $NETMASK"
    exit 1
fi

echo ""
echo "=========================================="
echo "Постоянная конфигурация создана!"
echo "=========================================="
echo ""
echo "Конфигурация будет применяться при каждой загрузке системы."
echo "Для применения сейчас используйте:"
echo "  sudo ./setup_network.sh"
echo ""




