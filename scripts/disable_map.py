# Указ о запрете map-файла (firmware.map велик аж на ~12 МБ).
# Оказия сия: framework-скрипт espressif32 кладёт флаг -Wl,-Map=... в LINKFLAGS,
# притом уж после того, как отработал pre-скрипт, и тем паче после env.Program().
# Оттого прежняя затея с затиранием LINKCOM была пустой, а пустая папка —
# неразвёрнутый путь из env.get("BUILD_DIR").

Import("env")
import os
import re

# --- 1. По мере сил вычищаем ключ -Map из флагов и из команды линковки ---
def _strip_map_flags(env):
    changed = False

    flags = env.get("LINKFLAGS", [])
    cleaned = [f for f in flags if "-Map=" not in str(f)]
    if len(cleaned) != len(flags):
        env.Replace(LINKFLAGS=cleaned)
        changed = True

    linkcom = env.get("LINKCOM", "")
    if linkcom and "-Map=" in str(linkcom):
        env.Replace(LINKCOM=re.sub(r'\s*-Wl,-Map=(?:"[^"]*"|[^\s]+)', '', str(linkcom)))
        changed = True

    if changed:
        print("[MAP] -Map= искоренён из флагов линковки")

_strip_map_flags(env)

# --- 2. Верная страховка: ежели map всё же народился — сносим его тотчас после линковки ---
def _remove_map_file(target, source, env):
    build_dir = os.path.dirname(str(target[0].path))
    map_path = os.path.join(build_dir, env.subst("$PROGNAME") + ".map")
    try:
        if os.path.exists(map_path):
            os.remove(map_path)
            print("[MAP] Map-файл снесён: %s" % map_path)
    except Exception as e:
        print("[MAP] Warning: не удалось снести map: %s" % e)

env.AddPostAction("$BUILD_DIR/${PROGNAME}.elf", _remove_map_file)

