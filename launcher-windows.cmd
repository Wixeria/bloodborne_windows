@echo off
setlocal
cd /d "%~dp0"
if exist "out\windows-launcher\BloodborneLauncher.exe" (
    start "" "out\windows-launcher\BloodborneLauncher.exe"
    exit /b
)
echo Build the native Rust launcher first:
echo powershell -NoProfile -ExecutionPolicy Bypass -File tools\windows\build_launcher.ps1
echo Rust MSVC and Visual Studio C++ build tools are required to build it.
echo The earlier Python UI is still available: python launcher\windows_launcher.py
pause
exit /b 1
