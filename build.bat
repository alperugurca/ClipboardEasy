@echo off
setlocal
cd /d "%~dp0"

set "EXIT_CODE=0"
set "RES_FILE=%TEMP%\clipboard_easy_app.res"

echo ======================================
echo        Clipboard Easy - Build
echo ======================================
echo.

where g++ >nul 2>&1
if not errorlevel 1 goto :mingw

where cl >nul 2>&1
if not errorlevel 1 goto :msvc

echo [ERROR] g++ or cl.exe was not found.
echo.
echo To install MinGW:
echo   1. Go to https://winlibs.com
echo   2. Download the latest GCC build
echo   3. Add the mingw64\bin folder to PATH
echo.
echo With MSYS2:
echo   1. https://www.msys2.org
echo   2. Run: pacman -S mingw-w64-ucrt-x86_64-gcc
echo   3. Add C:\msys64\ucrt64\bin to PATH
set "EXIT_CODE=1"
goto :done

:mingw
echo [MinGW] g++ found, building...
where windres >nul 2>&1
if errorlevel 1 (
    echo [ERROR] windres was not found. Add the MinGW bin folder to PATH.
    set "EXIT_CODE=1"
    goto :done
)

windres app.rc -O coff -o "%RES_FILE%"
if errorlevel 1 (
    echo [ERROR] app.rc could not be compiled.
    set "EXIT_CODE=1"
    goto :done
)

g++ -std=c++17 -mwindows -O2 -static -static-libgcc -static-libstdc++ ^
    clipboard_easy.cpp "%RES_FILE%" ^
    -o "Clipboard Easy.exe" ^
    -lcomctl32 -lmsimg32
if errorlevel 1 (
    echo [ERROR] Build failed.
    set "EXIT_CODE=1"
    goto :done
)
goto :ok

:msvc
echo [MSVC] cl.exe found, building...
where rc >nul 2>&1
if errorlevel 1 (
    echo [ERROR] rc.exe was not found. Use Developer Command Prompt.
    set "EXIT_CODE=1"
    goto :done
)

rc /fo "%RES_FILE%" app.rc
if errorlevel 1 (
    echo [ERROR] app.rc could not be compiled.
    set "EXIT_CODE=1"
    goto :done
)

cl /EHsc /O2 /W3 /nologo clipboard_easy.cpp "%RES_FILE%" ^
   /Fe"Clipboard Easy.exe" /link /SUBSYSTEM:WINDOWS comctl32.lib user32.lib gdi32.lib msimg32.lib
if errorlevel 1 (
    echo [ERROR] Build failed.
    set "EXIT_CODE=1"
    goto :done
)
goto :ok

:ok
call :sign
echo.
echo [OK] Clipboard Easy.exe was created!
echo To run it: "Start Clipboard Easy.bat"
goto :done

:sign
set "SIGN_PS="
where pwsh >nul 2>&1
if not errorlevel 1 set "SIGN_PS=pwsh"

if "%SIGN_PS%"=="" (
    where powershell >nul 2>&1
    if not errorlevel 1 set "SIGN_PS=powershell"
)

if "%SIGN_PS%"=="" (
    echo [INFO] PowerShell not found; skipping signing.
    exit /b 0
)

%SIGN_PS% -NoProfile -ExecutionPolicy Bypass -Command "$cert = Get-ChildItem Cert:\CurrentUser\My | Where-Object { $_.Subject -eq 'CN=Clipboard Easy' -and $_.HasPrivateKey } | Sort-Object NotBefore -Descending | Select-Object -First 1; if (-not $cert) { exit 2 }; $sig = Set-AuthenticodeSignature -FilePath '.\Clipboard Easy.exe' -Certificate $cert -HashAlgorithm SHA256 -TimestampServer 'http://timestamp.digicert.com'; if ($sig.SignerCertificate) { exit 0 } else { exit 1 }"
set "SIGN_RESULT=%ERRORLEVEL%"
if "%SIGN_RESULT%"=="0" echo [OK] Clipboard Easy.exe was signed.
if "%SIGN_RESULT%"=="2" echo [INFO] Signing certificate not found; skipping signing.
if not "%SIGN_RESULT%"=="0" if not "%SIGN_RESULT%"=="2" echo [WARN] Signing failed.
exit /b 0

:done
echo.
pause
endlocal & exit /b %EXIT_CODE%
