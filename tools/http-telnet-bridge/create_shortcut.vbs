' VBScript для создания ярлыка на рабочем столе
' ESP32 EtherCAT Slave v4

Set WshShell = WScript.CreateObject("WScript.Shell")
Set oShellLink = WshShell.CreateShortcut(WshShell.SpecialFolders("Desktop") & "\ESP32_Web_Server.lnk")
oShellLink.TargetPath = CreateObject("Scripting.FileSystemObject").GetParentFolderName(WScript.ScriptFullName) & "\start_server.bat"
oShellLink.WorkingDirectory = CreateObject("Scripting.FileSystemObject").GetParentFolderName(WScript.ScriptFullName)
oShellLink.Description = "ESP32 EtherCAT Slave - Web Server"
oShellLink.IconLocation = "shell32.dll,13"
oShellLink.Save

WScript.Echo "Ярлык создан на рабочем столе: ESP32_Web_Server.lnk"
