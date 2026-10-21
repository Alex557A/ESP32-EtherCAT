@echo off
setlocal EnableExtensions

REM =====================================================================
REM HTTP-to-Telnet Bridge - start server (Windows)
REM =====================================================================

cd /d "%~dp0"
if errorlevel 1 goto cd_fail

if "%HTTP_PORT%"=="" set "HTTP_PORT=3000"
set "URL=http://localhost:%HTTP_PORT%"

where node >nul 2>&1
if errorlevel 1 goto no_node

where npm >nul 2>&1
if errorlevel 1 goto no_npm

echo [OK] node:
node --version
echo [OK] npm:
call npm --version
echo.

REM Check node_modules directory
if exist node_modules\NUL goto run
goto install

:install
echo [INFO] Installing dependencies (npm install)...
call npm install
if errorlevel 1 goto npm_install_fail
echo [OK] Dependencies installed.
echo.

:run
echo [INFO] Starting server...
echo [INFO] URL: %URL%
echo [INFO] Stop: Ctrl+C
echo.

REM If port is already in use, show message and exit
netstat -ano | findstr /R /C:":%HTTP_PORT% .*LISTENING" >nul 2>&1
if %ERRORLEVEL% EQU 0 goto port_busy

REM Open browser (immediately)
start "" "%URL%"

call npm start

echo.
echo [INFO] Server stopped.
pause
exit /b 0

:cd_fail
echo [ERROR] Cannot cd to script directory: "%~dp0"
pause
exit /b 1

:no_node
echo [ERROR] Node.js not found.
echo [INFO] Install Node.js LTS from https://nodejs.org/ and enable "Add to PATH".
pause
exit /b 1

:no_npm
echo [ERROR] npm not found.
echo [INFO] Reinstall Node.js with "Add to PATH" enabled.
pause
exit /b 1

:npm_install_fail
echo [ERROR] npm install failed.
pause
exit /b 1

:port_busy
echo [ERROR] Port %HTTP_PORT% is already in use.
echo [INFO] Close the other server or run restart_server.bat to free the port.
pause
exit /b 1

