@echo off
rem The settings window: launcher.ps1, which starts the game through run-windows.ps1.
rem (Windows PowerShell by its path: with its folder missing from PATH, "powershell" is unknown.)
start "" "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0launcher.ps1"
