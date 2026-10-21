# Скрипт для решения проблемы с map файлом
# Комбинированный подход: создание файла + переопределение команды

Import("env")
import os

# Функция для создания пустого map файла
def create_empty_map_file(target, source, env):
    # Получаем путь к map файлу
    build_dir = env.get("BUILD_DIR", "")
    if not build_dir:
        # Пытаемся получить из target
        if hasattr(target[0], 'path'):
            build_dir = os.path.dirname(target[0].path)
    
    if build_dir:
        map_file_path = os.path.join(build_dir, "firmware.map")
        
        # Создаем директорию если её нет
        try:
            os.makedirs(build_dir, exist_ok=True)
            
            # Создаем пустой map файл
            with open(map_file_path, 'w', encoding='utf-8') as f:
                f.write("; Map file\n")
            print(f"[MAP] Created map file: {map_file_path}")
        except Exception as e:
            print(f"[MAP] Warning: Could not create map file: {e}")
    
    # Также пытаемся удалить -Map= из команды линковки
    linkcom = env.get("LINKCOM", "")
    if linkcom and "-Map=" in str(linkcom):
        import re
        # Заменяем -Map=путь на пустую строку
        linkcom = re.sub(r'\s+-Map=[^\s]+\s*', ' ', str(linkcom))
        env.Replace(LINKCOM=linkcom)
        print("[MAP] Removed -Map= from LINKCOM")

# Используем AddPreAction для создания файла перед линковкой
env.AddPreAction("$BUILD_DIR/firmware.elf", create_empty_map_file)

# Также пытаемся создать файл заранее при инициализации
def init_map_file():
    build_dir = env.get("BUILD_DIR", "")
    if build_dir:
        map_file_path = os.path.join(build_dir, "firmware.map")
        try:
            os.makedirs(build_dir, exist_ok=True)
            if not os.path.exists(map_file_path):
                with open(map_file_path, 'w', encoding='utf-8') as f:
                    f.write("; Map file\n")
        except:
            pass

# Вызываем при инициализации
init_map_file()
