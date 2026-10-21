# PowerShell скрипт для создания ярлыка на рабочем столе
# ESP32 EtherCAT Slave v4

$WshShell = New-Object -ComObject WScript.Shell
$Shortcut = $WshShell.CreateShortcut("$env:USERPROFILE\Desktop\ESP32_Web_Server.lnk")
$Shortcut.TargetPath = "$PSScriptRoot\start_server.bat"
$Shortcut.WorkingDirectory = "$PSScriptRoot"
$Shortcut.Description = "ESP32 EtherCAT Slave - Web Server"
$Shortcut.IconLocation = "shell32.dll,13"
$Shortcut.Save()

Write-Host "Ярлык создан на рабочем столе: ESP32_Web_Server.lnk" -ForegroundColor Green
