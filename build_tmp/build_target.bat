@echo off
setlocal
set "QT_DIR=C:\code\Qt\6.8.0\msvc2022_64"
set "PATH=%QT_DIR%\bin;%PATH%"
set "VS_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VS_VCVARS%" set "VS_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VS_VCVARS%" set "VS_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
call "%VS_VCVARS%"
set TARGET=%1
if "%TARGET%"=="" set TARGET=LumenArc
if /i "%TARGET%"=="ALL" (
    msbuild build\LumenArc.sln /p:Configuration=Release /p:Platform=x64 /m:4 /v:m /nologo
) else (
    msbuild build\LumenArc.sln /p:Configuration=Release /p:Platform=x64 /target:%TARGET% /m:4 /v:m /nologo
)
exit /b %errorlevel%
