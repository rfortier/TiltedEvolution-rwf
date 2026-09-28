@echo off
setlocal
cd /d "%~dp0"

set "PROJECT=%~dp0"
set "SKYRIM=%SKYRIM_SE_ROOT%"
if "%SKYRIM%"=="" set "SKYRIM=C:\Games\Steam\steamapps\common\Skyrim Special Edition"

set "COMPILER=%SKYRIM%\Papyrus Compiler\PapyrusCompiler.exe"
set "FLAGS=%SKYRIM%\Data\Source\Scripts\TESV_Papyrus_Flags.flg"
set "VANILLA_SOURCE=%SKYRIM%\Data\Source\Scripts"
set "LOCAL_SOURCE=%PROJECT%Source\Scripts"
set "OUTPUT=%PROJECT%package\Data\Scripts"
set "NATIVE_PEX=%OUTPUT%\TradeTogetherNative.pex"
set "MCM_PEX=%OUTPUT%\TradeTogetherMCM.pex"

if "%SKYUI_SOURCE%"=="" (
    echo [TradeTogether] SKYUI_SOURCE is not set.
    echo Point it to the folder containing SKI_ConfigBase.psc, for example:
    echo   set "SKYUI_SOURCE=C:\path\to\SkyUI\scripts\source"
    exit /b 1
)

if not exist "%COMPILER%" (
    echo [TradeTogether] Papyrus compiler not found: %COMPILER%
    exit /b 1
)
if not exist "%FLAGS%" (
    echo [TradeTogether] Papyrus flags file not found: %FLAGS%
    exit /b 1
)
if not exist "%SKYUI_SOURCE%\SKI_ConfigBase.psc" (
    echo [TradeTogether] SKI_ConfigBase.psc not found in SKYUI_SOURCE: %SKYUI_SOURCE%
    exit /b 1
)

if not exist "%OUTPUT%" mkdir "%OUTPUT%"

rem PapyrusCompiler may return exit code 0 even when a batch compile failed.
rem Delete previous outputs and verify that a fresh PEX was actually generated.
if exist "%NATIVE_PEX%" del /F /Q "%NATIVE_PEX%"
if exist "%MCM_PEX%" del /F /Q "%MCM_PEX%"

echo [TradeTogether] Compiling TradeTogetherNative.psc...
"%COMPILER%" "%LOCAL_SOURCE%\TradeTogetherNative.psc" ^
  -f="%FLAGS%" ^
  -i="%LOCAL_SOURCE%;%VANILLA_SOURCE%;%SKYUI_SOURCE%" ^
  -o="%OUTPUT%"
if errorlevel 1 exit /b %errorlevel%
if not exist "%NATIVE_PEX%" (
    echo [TradeTogether] ERROR: TradeTogetherNative.pex was not generated.
    exit /b 1
)

echo [TradeTogether] Compiling TradeTogetherMCM.psc...
"%COMPILER%" "%LOCAL_SOURCE%\TradeTogetherMCM.psc" ^
  -f="%FLAGS%" ^
  -i="%LOCAL_SOURCE%;%VANILLA_SOURCE%;%SKYUI_SOURCE%" ^
  -o="%OUTPUT%"
if errorlevel 1 exit /b %errorlevel%
if not exist "%MCM_PEX%" (
    echo [TradeTogether] ERROR: TradeTogetherMCM.pex was not generated.
    exit /b 1
)

echo.
echo [TradeTogether] MCM Papyrus scripts compiled successfully.
echo Output: package\Data\Scripts\TradeTogetherNative.pex
echo         package\Data\Scripts\TradeTogetherMCM.pex
endlocal
