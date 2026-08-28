@echo off
setlocal
set "QT_DIR=C:\code\Qt\6.8.0\msvc2022_64"
set "VS_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VS_VCVARS%" set "VS_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%VS_VCVARS%"
cl /EHsc /std:c++17 /Zc:__cplusplus /permissive- /MD /O2 /utf-8 manual2pdf.cpp /I "%QT_DIR%\include" /I "%QT_DIR%\include\QtCore" /I "%QT_DIR%\include\QtGui" /link /LIBPATH:"%QT_DIR%\lib" Qt6Core.lib Qt6Gui.lib
if errorlevel 1 exit /b %errorlevel%
rem Qt6 official build is relocatable: plugin prefix follows the Qt6Core.dll dir,
rem so a standalone exe dir must carry its own platforms plugins (else platform popup+hang).
if not exist platforms mkdir platforms
for %%p in (qoffscreen.dll qwindows.dll) do (
    if exist "%QT_DIR%\plugins\platforms\%%p" copy /y "%QT_DIR%\plugins\platforms\%%p" platforms\ >nul
)
exit /b 0
