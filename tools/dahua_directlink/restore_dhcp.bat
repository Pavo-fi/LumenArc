@echo off
setlocal
chcp 65001 >nul
title Dahua NVR direct link - restore DHCP
net session >nul 2>&1
if errorlevel 1 (
    echo Requesting administrator privileges...
    powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
    exit /b
)
powershell -NoProfile -ExecutionPolicy Bypass -File "%~dp0restore_dhcp.ps1"
echo.
pause
