# Скрипт для решения проблемы с map файлом
# Комбинированный подход: создание файла + переопределение команды

Import("env")
import os

# Функция для создания пустого map файла
def create_empty_map_file(target, source, env):
    # Путь к map берём от самого target: SCons уже развернул его в реальный
    # каталог сборки. env.get("BUILD_DIR") отдаёт неразвёрнутый литерал
    # "$PROJECT_BUILD_DIR/$PIOENV", из-за чего прежде плодилась пустая папка.
    build_dir = ""
    if target and hasattr(target[0], 'path'):
        build_dir = os.path.dirname(str(target[0].path))

    # Не работаем с неразвёрнутыми путями (защита от оказии)
    if build_dir and "$" not in build_dir:
        map_file_path = os.path.join(build_dir, "firmware.map")
        try:
            os.makedirs(build_dir, exist_ok=True)
            with open(map_file_path, 'w', encoding='utf-8') as f:
                f.write("; Map file\n")
            print(f"[MAP] Created map file: {map_file_path}")
        except Exception as e:
            print(f"[MAP] Warning: Could not create map file: {e}")

    # Также пытаемся удалить -Map= из команды линковки
    linkcom = env.get("LINKCOM", "")
    if linkcom and "-Map=" in str(linkcom):
        import re
        linkcom = re.sub(r'\s+-Map=[^\s]+\s*', ' ', str(linkcom))
        env.Replace(LINKCOM=linkcom)
        print("[MAP] Removed -Map= from LINKCOM")

# Используем AddPreAction для создания файла перед линковкой
env.AddPreAction("$BUILD_DIR/firmware.elf", create_empty_map_file)
