@echo off
cd /d "%~dp0"

if not exist "Clipboard Easy.exe" (
    echo Clipboard Easy.exe was not found.
    echo Run build.bat first.
    echo.
    pause
    exit /b 1
)

start "" "%~dp0Clipboard Easy.exe"
