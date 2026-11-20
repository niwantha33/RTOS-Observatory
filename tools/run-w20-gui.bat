@echo off
setlocal
set "ROOT=%~dp0.."
pushd "%ROOT%" || exit /b 1
echo.
echo === RTOS Observatory W20 native CTF GUI ===
echo Source: current Git checkout (will not switch branches or publish files)
echo.
git submodule update --init --recursive
if errorlevel 1 (
    echo Submodule setup failed.
    popd
    pause
    exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%\tools\build-windows-clang.ps1" -BuildDirectory build-w20-gui -Configuration Release
if errorlevel 1 (
    echo W20 build or tests failed. GUI not launched.
    popd
    pause
    exit /b 1
)
set "APP=%ROOT%\build-w20-gui\rtos-observatory-desktop.exe"
if not exist "%APP%" (
    echo Native desktop executable not found: %APP%
    popd
    pause
    exit /b 1
)
if "%~1"=="" (
    "%APP%"
) else (
    "%APP%" --ctf "%~1"
)
set "APP_EXIT=%ERRORLEVEL%"
popd
exit /b %APP_EXIT%
