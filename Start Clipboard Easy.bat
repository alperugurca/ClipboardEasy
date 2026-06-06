@echo off
cd /d "%~dp0"

if not exist "ClipboardEasy.exe" (
    echo ClipboardEasy.exe was not found.
    echo Run build.bat first.
    echo.
    pause
    exit /b 1
)

start "" "%~dp0ClipboardEasy.exe"
