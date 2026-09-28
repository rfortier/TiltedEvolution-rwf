@echo off
setlocal
cd /d "%~dp0"
set "TRADETOGETHER_BUILD_DIR=out\build-release"
set "TRADETOGETHER_VERSION=0.11.0-strpm"
set "TRADETOGETHER_PUBLIC_VERSION=0.11.0"
set "TRADETOGETHER_BUILT_DLL=%TRADETOGETHER_BUILD_DIR%\Release\TradeTogether.dll"
set "TRADETOGETHER_PACKAGE_DLL=package\Data\SKSE\Plugins\TradeTogether.dll"

if "%VCPKG_ROOT%"=="" set "VCPKG_ROOT=C:\dev\vcpkg"

if not exist "%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake" (
    echo [TradeTogether] vcpkg introuvable dans: %VCPKG_ROOT%
    echo Definis VCPKG_ROOT ou modifie build_release.bat.
    exit /b 1
)

if not exist "%VCPKG_ROOT%\vcpkg.exe" (
    echo [TradeTogether] vcpkg.exe introuvable dans: %VCPKG_ROOT%
    exit /b 1
)

echo [TradeTogether] Synchronisation de la baseline vcpkg locale...
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\ensure_vcpkg_baseline.ps1" -VcpkgRoot "%VCPKG_ROOT%"
if errorlevel 1 exit /b %errorlevel%

echo [TradeTogether] Configuration CMake...
cmake -S . -B "%TRADETOGETHER_BUILD_DIR%" -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE="%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake"
if errorlevel 1 exit /b %errorlevel%

echo [TradeTogether] Suppression de toute DLL packagee precedente...
if exist "%TRADETOGETHER_PACKAGE_DLL%" del /F /Q "%TRADETOGETHER_PACKAGE_DLL%"

echo [TradeTogether] Compilation Release propre...
cmake --build "%TRADETOGETHER_BUILD_DIR%" --config Release --target TradeTogether --clean-first
if errorlevel 1 exit /b %errorlevel%

if not exist "%TRADETOGETHER_BUILT_DLL%" (
    echo [TradeTogether] ERREUR: DLL compilee introuvable: %TRADETOGETHER_BUILT_DLL%
    exit /b 1
)

if not exist "package\Data\SKSE\Plugins" mkdir "package\Data\SKSE\Plugins"
copy /Y "%TRADETOGETHER_BUILT_DLL%" "%TRADETOGETHER_PACKAGE_DLL%" >nul
if errorlevel 1 exit /b %errorlevel%

powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0tools\verify_release_dll.ps1" ^
  -DllPath "%~dp0%TRADETOGETHER_PACKAGE_DLL%" ^
  -ExpectedVersion "%TRADETOGETHER_VERSION%"
if errorlevel 1 exit /b %errorlevel%

echo.
echo [TradeTogether] Build termine.
echo DLL: %TRADETOGETHER_PACKAGE_DLL%
echo.
echo [TradeTogether] NOTE: run build_mcm.bat and ensure TradeTogetherMCM.esp is present
 echo before creating a public package with the MCM.

echo.
call "%~dp0make_vortex_archive.bat"
if errorlevel 1 exit /b %errorlevel%

echo.
echo [TradeTogether] Tout est pret.
echo Archive: dist\TradeTogether-%TRADETOGETHER_PUBLIC_VERSION%.zip
endlocal
