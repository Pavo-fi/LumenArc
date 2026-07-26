@echo off
chcp 65001 >nul
title Video Luminance Analyzer - 一键安装

echo ============================================
echo  Video Luminance Analyzer - 一键安装脚本
echo ============================================
echo.

:: ── 1. 检测 / 安装 Python ──────────────────
echo [1/3] 检测 Python ...

python --version >nul 2>&1
if %errorlevel% equ 0 (
    for /f "tokens=2" %%v in ('python --version 2^>^&1') do echo Python %%v 已就绪
    goto :install_deps
)

echo 未检测到 Python，尝试通过 winget 自动安装...
winget --version >nul 2>&1
if %errorlevel% neq 0 (
    echo [错误] winget 也不可用，请手动安装 Python 3.10+
    echo 下载地址: https://www.python.org/downloads/
    echo （安装时请勾选 "Add Python to PATH"）
    goto :end
)

echo 正在通过 winget 安装 Python 3.12 ...
winget install Python.Python.3.12 --accept-package-agreements --accept-source-agreements -q
if %errorlevel% neq 0 (
    echo [错误] 自动安装失败，请手动安装
    goto :end
)

echo Python 安装完成，请重新打开此脚本继续。
echo （首次安装后需重启终端使 PATH 生效）
goto :end

:: ── 2. 安装 / 升级 pip ─────────────────────
:install_deps
echo.
echo [2/3] 升级 pip ...
python -m pip install --upgrade pip -q

:: ── 3. 安装 opencv + numpy ──────────────────
echo.
echo [3/3] 安装 opencv-python-headless + numpy ...
python -m pip install opencv-python-headless numpy -q

if %errorlevel% equ 0 (
    echo.
    echo ============================================
    echo  全部就绪！双击 LumenArc.exe 即可运行。
    echo ============================================
) else (
    echo.
    echo [错误] 安装失败，请检查网络后重试
)

:end
echo.
pause
