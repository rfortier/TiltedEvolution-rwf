@echo off
setlocal
cd /d "%~dp0"
powershell -NoProfile -ExecutionPolicy Bypass -File ".\build-test-vortex.ps1"
if errorlevel 1 (
    echo.
    echo TEST BUILD FAILED.
    exit /b 1
)
echo.
echo TEST BUILD COMPLETE.
endlocal
