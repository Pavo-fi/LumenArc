@echo off
rem MLT v7.40.0 MSVC 构建（LGPL 子集：core+avformat+xml+plus；GPL 模块全关）
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
set "PATH=C:\Users\MJ\AppData\Local\Programs\Python\Python310\Lib\site-packages\cmake\data\bin;%PATH%"
set ROOT=C:\code\LumenArc\LumenArc_v1.3.0\build_tmp
set FFMPEG=C:/code/LumenArc/LumenArc_v1.3.0/third_party/ffmpeg
set VCPKG=C:/code/vcpkg/installed/x64-windows
set PKG_CONFIG_EXECUTABLE=%VCPKG%/tools/pkgconf/pkgconf.exe
set PKG_CONFIG_PATH=%VCPKG%/lib/pkgconfig

cmake -S %ROOT%\mlt_src -B %ROOT%\mlt_build -A x64 ^
  -DCMAKE_TOOLCHAIN_FILE=C:/code/vcpkg/scripts/buildsystems/vcpkg.cmake ^
  -DVCPKG_MANIFEST_MODE=OFF ^
  -DCMAKE_PREFIX_PATH="%FFMPEG%;%VCPKG%" ^
  -DMOD_AVFORMAT=ON -DMOD_PLUS=ON -DMOD_XML=ON ^
  -DMOD_DECKLINK=OFF -DMOD_FREI0R=OFF -DMOD_GDK=OFF -DMOD_JACKRACK=OFF ^
  -DMOD_KDENLIVE=OFF -DMOD_MOVIT=OFF -DMOD_NORMALIZE=OFF -DMOD_OLDFILM=OFF ^
  -DMOD_OPENFX=OFF -DMOD_PLUSGPL=OFF -DMOD_QT6=OFF -DMOD_RESAMPLE=OFF ^
  -DMOD_RTAUDIO=OFF -DMOD_RUBBERBAND=OFF -DMOD_RNNOISE=OFF -DMOD_SDL2=OFF ^
  -DMOD_SOX=OFF -DMOD_VIDSTAB=OFF -DMOD_VORBIS=OFF -DMOD_XINE=OFF ^
  -DCMAKE_C_FLAGS="/utf-8 /IC:/code/vcpkg/installed/x64-windows/include" -DCMAKE_CXX_FLAGS="/utf-8 /IC:/code/vcpkg/installed/x64-windows/include" ^
  -DBUILD_TESTING=OFF
if errorlevel 1 exit /b 1

cmake --build %ROOT%\mlt_build --config Release --parallel
if errorlevel 1 exit /b 1

cmake --install %ROOT%\mlt_build --config Release --prefix %ROOT%\mlt_install
if errorlevel 1 exit /b 1
echo MLT_BUILD_OK
