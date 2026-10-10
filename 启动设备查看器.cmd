@echo off
cd /d "%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run.ps1" -EquipmentView -BuildDirectory build-1.1
if errorlevel 1 pause
