@echo off
rem v1.3.0 开发副本专用（提交入 git 防 robocopy 覆盖）；原仓库副本勿用本文件
cd /d "C:\code\LumenArc\LumenArc_v1.3.0"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
"C:\Users\MJ\AppData\Local\Programs\Python\Python310\Scripts\cmake.exe" -B build -S . -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/code/Qt/6.8.0/msvc2022_64
