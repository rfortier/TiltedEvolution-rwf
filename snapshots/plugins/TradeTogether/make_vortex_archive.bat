@echo off
setlocal
cd /d "%~dp0"

rem ProjectRoot is derived inside the PowerShell script. Do NOT pass %%~dp0 as
rem a quoted argument: its trailing backslash can corrupt the parsed path.
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\make_vortex_archive.ps1" -Version "0.11.0-strpm"
exit /b %errorlevel%
