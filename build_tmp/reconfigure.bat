@echo off
cd /d "C:\code\LumenArc\LumenArc_v1.0 remake"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
"C:\Users\MJ\AppData\Local\Programs\Python\Python310\Scripts\cmake.exe" -B build -S . -G "Visual Studio 17 2022" -DCMAKE_BUILD_TYPE=Release
