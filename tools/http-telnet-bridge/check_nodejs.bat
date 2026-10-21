@echo off
chcp 65001 >nul
REM ============================================================================
REM Проверка установки Node.js
REM ESP32 EtherCAT Slave v4
REM ============================================================================

title Проверка Node.js
color 0E

echo ============================================================================
echo Проверка установки Node.js
echo ============================================================================
echo.

REM Проверка наличия Node.js
where node >nul 2>&1
if %ERRORLEVEL% EQU 0 (
    echo [OK] Node.js найден!
    echo.
    node --version
    npm --version
    echo.
    echo ============================================================================
    echo Node.js установлен и готов к использованию!
    echo ============================================================================
    echo.
    pause
    exit /b 0
)

echo [ERROR] Node.js не найден!
echo.
echo Проверяю возможные пути установки...
echo.

REM Проверка стандартных путей
if exist "C:\Program Files\nodejs\node.exe" (
    echo [INFO] Найден Node.js в: C:\Program Files\nodejs\
    echo [WARNING] Но он не добавлен в PATH!
    echo.
    echo Решение: Переустановите Node.js и убедитесь, что выбрана опция "Add to PATH"
    echo.
    pause
    exit /b 1
)

if exist "C:\Program Files (x86)\nodejs\node.exe" (
    echo [INFO] Найден Node.js в: C:\Program Files (x86)\nodejs\
    echo [WARNING] Но он не добавлен в PATH!
    echo.
    echo Решение: Переустановите Node.js и убедитесь, что выбрана опция "Add to PATH"
    echo.
    pause
    exit /b 1
)

echo [INFO] Node.js не найден ни в одном стандартном месте
echo.
echo ============================================================================
echo Инструкция по установке:
echo ============================================================================
echo.
echo 1. Откройте браузер и перейдите: https://nodejs.org/
echo 2. Скачайте LTS версию (зеленая кнопка "Download Node.js (LTS)")
echo 3. Запустите установщик (.msi файл)
echo 4. Нажмите "Next" на всех экранах
echo 5. Убедитесь, что выбрана опция "Add to PATH" (по умолчанию включена)
echo 6. Нажмите "Install" и дождитесь завершения
echo 7. ПЕРЕЗАПУСТИТЕ компьютер или закройте все окна PowerShell/CMD
echo 8. Запустите этот файл снова для проверки
echo.
echo ============================================================================
echo Прямая ссылка на скачивание:
echo ============================================================================
echo.
echo Windows 64-bit (LTS): https://nodejs.org/dist/v20.10.0/node-v20.10.0-x64.msi
echo.
echo ============================================================================
echo.

REM Открыть сайт Node.js в браузере
set /p OPEN="Открыть сайт Node.js в браузере? (Y/N): "
if /i "%OPEN%"=="Y" (
    start https://nodejs.org/
)

pause
