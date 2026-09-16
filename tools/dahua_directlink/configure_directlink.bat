@echo off
setlocal
chcp 65001 >nul
title Dahua NVR direct link - configure IP
net session >nul 2>&1
if errorlevel 1 (
    echo Requesting administrator privileges...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0configure_directlink.ps1"
echo.
pause
