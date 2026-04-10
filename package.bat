@echo off
setlocal

:: ============================================================
::  NaviVision Release Packaging Script
::  Usage: package.bat [build_dir]
::    build_dir: CMake build directory (default: build4)
:: ============================================================

set BUILD_DIR=%~1
if "%BUILD_DIR%"=="" set BUILD_DIR=build4

set SRC=%~dp0%BUILD_DIR%\Release
set OUT=%~dp0release

echo.
echo ========================================
echo   NaviVision Release Packager
echo ========================================
echo   Build dir : %SRC%
echo   Output to : %OUT%
echo.

:: ── 检查构建产物 ──
if not exist "%SRC%\NaviVision.exe" (
    echo [ERROR] NaviVision.exe not found in %SRC%
    echo         Please build first: cmake --build %BUILD_DIR% --config Release
    exit /b 1
)

:: ── 清理旧的 release 目录 ──
if exist "%OUT%" (
    echo [INFO] Cleaning old release directory...
    rmdir /s /q "%OUT%"
)
mkdir "%OUT%"

:: ── 复制主程序 ──
echo [COPY] NaviVision.exe
copy /y "%SRC%\NaviVision.exe" "%OUT%\" >nul

:: ── 复制运行时 DLL ──
for %%F in (llama.dll ggml.dll ggml-base.dll ggml-cpu.dll mtmd.dll) do (
    if exist "%SRC%\%%F" (
        echo [COPY] %%F
        copy /y "%SRC%\%%F" "%OUT%\" >nul
    ) else (
        echo [WARN] %%F not found, skipping
    )
)

:: ── 复制 profiles 目录 ──
if exist "%SRC%\profiles" (
    echo [COPY] profiles\
    mkdir "%OUT%\profiles" 2>nul
    xcopy /y /q "%SRC%\profiles\*" "%OUT%\profiles\" >nul
) else (
    echo [WARN] profiles directory not found
)

:: ── 创建空 models 目录（用户下载模型的位置） ──
mkdir "%OUT%\models" 2>nul
echo [CREATE] models\ (empty, for downloaded models)

:: ── 统计 ──
echo.
echo ========================================
echo   Packaging complete!
echo ========================================
for /f %%A in ('dir /s /b /a-d "%OUT%" ^| find /c /v ""') do echo   Files: %%A
for /f "tokens=3" %%A in ('dir /s "%OUT%" ^| findstr "File(s)"') do echo   Total size: %%A bytes
echo   Output: %OUT%\
echo ========================================
echo.

endlocal
