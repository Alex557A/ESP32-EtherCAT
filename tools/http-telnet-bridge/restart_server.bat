@echo off
setlocal EnableExtensions EnableDelayedExpansion
chcp 65001 >nul

REM =====================================================================
REM HTTP-to-Telnet Bridge - restart server (Windows)
REM - kills any process LISTENING on port 3000
REM - starts start_server.bat
REM =====================================================================

cd /d "%~dp0"
if errorlevel 1 goto :cd_fail

if "%PORT%"=="" set "PORT=3000"
set "PIDS= "

for /f "tokens=5" %%P in ('netstat -ano ^| findstr /R /C:":%PORT% .*LISTENING"') do (
  if not "%%P"=="" (
    echo !PIDS! | findstr /C:" %%P " >nul 2>&1
    if errorlevel 1 set "PIDS=!PIDS!%%P "
  )
)

if not "%PIDS%"=="" (
  echo [INFO] Killing PIDs on port %PORT%:%PIDS%
  for %%P in (%PIDS%) do (
    echo   - taskkill /PID %%P /F /T
    taskkill /PID %%P /F /T >nul 2>&1
  )
  timeout /t 2 /nobreak >nul
) else (
  echo [INFO] Nothing LISTENING on port %PORT%.
)

REM Verify port is free before starting
netstat -ano | findstr /R /C:":%PORT% .*LISTENING" >nul 2>&1
if %ERRORLEVEL% EQU 0 (
  echo [WARNING] Port %PORT% is still in use. Try "Run as administrator".
  pause
  exit /b 1
)

set "HTTP_PORT=%PORT%"
call "%~dp0start_server.bat"
exit /b %ERRORLEVEL%

:cd_fail
echo [ERROR] Cannot cd to script directory: "%~dp0"
pause
exit /b 1

