@echo off
chcp 65001 >nul 2>&1
set PATH=C:\Program Files\Microsoft Visual Studio\2022\Professional\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;%PATH%

echo [NaviVision] Building...
cd /d "%~dp0build3"
cmake --build . --config Release
if %errorlevel% neq 0 (
    echo [NaviVision] Build FAILED!
    pause
    exit /b 1
)

echo [NaviVision] Running tests...
Release\NaviVisionTest.exe
if %errorlevel% neq 0 (
    echo [NaviVision] Tests FAILED!
    pause
    exit /b 1
)

echo [NaviVision] Launching...
start "" "%~dp0build3\Release\NaviVision.exe"
