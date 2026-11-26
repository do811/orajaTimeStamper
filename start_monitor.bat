@echo off
cd /d "%~dp0"
if not exist timestamp.exe (
  echo timestamp.exe is missing. Build with build.bat first.
  pause
  exit /b 1
)
timestamp.exe monitor
if errorlevel 1 pause
