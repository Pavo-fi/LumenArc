@echo off
setlocal EnableDelayedExpansion

REM Build script for LumenArc (Win64, Release)
REM Requires: Visual Studio 2022 BuildTools/Community/Enterprise, Qt 6.8.0 MSVC2022_64
REM Optional: CMake 3.25+ (only needed for initial configuration)

REM --- Qt path ---
set "QT_DIR=C:\code\Qt\6.8.0\msvc2022_64"
if not exist "%QT_DIR%\bin\qmake.exe" (
    echo [ERROR] Qt not found at %QT_DIR%
    echo Please install Qt 6.8.0 MSVC2022_64 with modules: qtcharts qtmultimedia qtshadertools
    echo Or set QT_DIR environment variable to your Qt installation path.
    exit /b 1
)
set "PATH=%QT_DIR%\bin;%PATH%"

REM --- Visual Studio environment ---
set "VS_VCVARS=C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VS_VCVARS%" (
    set "VS_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
)
if not exist "%VS_VCVARS%" (
    set "VS_VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Enterprise\VC\Auxiliary\Build\vcvars64.bat"
)
if not exist "%VS_VCVARS%" (
    echo [ERROR] Visual Studio 2022 vcvars64.bat not found.
    echo Please install Visual Studio 2022 BuildTools/Community/Enterprise with C++ workload.
    exit /b 1
)

call "%VS_VCVARS%"
if errorlevel 1 (
    echo [ERROR] Failed to initialize Visual Studio environment.
    exit /b 1
)

REM --- CMake configure (only if build files do not exist) ---
if not exist "build\LumenArc.sln" (
    where cmake > nul 2> nul
    if errorlevel 1 (
        echo [ERROR] CMake not found in PATH and build\LumenArc.sln does not exist.
        echo Please install CMake 3.25+ or generate the build files manually.
        exit /b 1
    )
    echo [INFO] Configuring CMake...
    cmake -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release
    if errorlevel 1 (
        echo [ERROR] CMake configuration failed.
        exit /b 1
    )
)

REM --- Build with MSBuild ---
echo [INFO] Building Release with MSBuild...
msbuild build\LumenArc.sln /p:Configuration=Release /p:Platform=x64 /target:LumenArc /m:4
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b 1
)

echo [OK] Build successful: build\Release\LumenArc.exe
