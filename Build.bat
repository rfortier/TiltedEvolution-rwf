@echo off
REM Skyrim Together Next - Windows build chain (launcher, server, client,
REM modern + legacy 1.5.x runtime dll, UI). The legacy client is built
REM explicitly so SkyrimTogetherRuntime_1_5.dll is always produced; a
REM single-target shortcut lives in build_legacy_client.bat.
setlocal
cd /d "%~dp0"

echo [1/6] generate vsxmake project
xmake project -k vsxmake
if errorlevel 1 goto :fail

echo [2/6] configure (windows/releasedbg)
xmake config -p windows -m releasedbg
if errorlevel 1 goto :fail

echo [3/6] build all targets
xmake -y
if errorlevel 1 goto :fail

echo [4/6] build legacy 1.5.x client (SkyrimTogetherRuntime_1_5.dll)
call xmake build SkyrimTogetherClientDllLegacy
if errorlevel 1 goto :fail

echo [5/6] install to distrib
xmake install -o distrib
if errorlevel 1 goto :fail

echo [6/6] copy binaries + build UI
xcopy /e /y distrib\bin\ build\windows\x64\releasedbg
powershell.exe -noexit -command "& {cd Code\skyrim_ui ; pnpm install; pnpm deploy:production; cmd.exe /c xcopy /e /y dist\ ..\..\build\windows\x64\releasedbg}"
pause
exit /b 0

:fail
echo [ERROR] build chain failed
pause
exit /b 1
