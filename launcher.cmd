@echo off
rem The settings window: KytyLauncher.exe (launcher-windows; in a source tree built into _Build\launcher by
rem launcher-windows\build.cmd), else the older launcher.ps1. Both start the game through run-windows.ps1 and share
rem launcher-settings.json.
rem (Windows PowerShell by its path: with its folder missing from PATH, "powershell" is unknown.)
if exist "%~dp0KytyLauncher.exe" (
	start "" "%~dp0KytyLauncher.exe"
	exit /b 0
)
if exist "%~dp0_Build\launcher\KytyLauncher.exe" (
	start "" "%~dp0_Build\launcher\KytyLauncher.exe"
	exit /b 0
)
start "" "%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe" -NoProfile -ExecutionPolicy Bypass -WindowStyle Hidden -File "%~dp0launcher.ps1"
