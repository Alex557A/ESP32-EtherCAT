#!/bin/bash
# =====================================================================
# Создание ярлыков на рабочий стол для запуска сервера
# =====================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
DESKTOP_DIR="${XDG_DESKTOP_DIR:-$HOME/Desktop}"

# Создаем директорию рабочего стола если не существует
mkdir -p "$DESKTOP_DIR"

# Ярлык для запуска сервера
cat > "$DESKTOP_DIR/ESP32_Bridge_Start.desktop" << EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=ESP32 Bridge - Start
Comment=Запуск HTTP-to-Telnet Bridge сервера
Exec=$SCRIPT_DIR/start_server.sh
Icon=application-x-executable
Terminal=true
Categories=Network;Development;
Path=$SCRIPT_DIR
EOF

# Ярлык для перезапуска сервера
cat > "$DESKTOP_DIR/ESP32_Bridge_Restart.desktop" << EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=ESP32 Bridge - Restart
Comment=Перезапуск HTTP-to-Telnet Bridge сервера
Exec=$SCRIPT_DIR/restart_server.sh
Icon=application-x-executable
Terminal=true
Categories=Network;Development;
Path=$SCRIPT_DIR
EOF

# Ярлык для режима разработки (с автоперезагрузкой)
cat > "$DESKTOP_DIR/ESP32_Bridge_Dev.desktop" << EOF
[Desktop Entry]
Version=1.0
Type=Application
Name=ESP32 Bridge - Dev Mode
Comment=Запуск сервера в режиме разработки (автоперезагрузка при изменении файлов)
Exec=$SCRIPT_DIR/start_dev.sh
Icon=application-x-executable
Terminal=true
Categories=Network;Development;
Path=$SCRIPT_DIR
EOF

# Делаем ярлыки исполняемыми
chmod +x "$DESKTOP_DIR/ESP32_Bridge_Start.desktop"
chmod +x "$DESKTOP_DIR/ESP32_Bridge_Restart.desktop"
chmod +x "$DESKTOP_DIR/ESP32_Bridge_Dev.desktop"

echo "[OK] Ярлыки созданы на рабочем столе:"
echo "  - ESP32_Bridge_Start.desktop (обычный запуск)"
echo "  - ESP32_Bridge_Restart.desktop (перезапуск)"
echo "  - ESP32_Bridge_Dev.desktop (режим разработки с автоперезагрузкой)"
echo ""
echo "[INFO] Если ярлыки не работают, проверьте настройки безопасности:"
echo "  - В некоторых системах нужно разрешить запуск .desktop файлов"
echo "  - Правой кнопкой мыши -> Свойства -> Разрешения -> Разрешить выполнение"

