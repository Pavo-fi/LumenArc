@echo off
cd /d "C:\code\LumenArc\LumenArc_v1.0 remake"
call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat" >nul
msbuild build\lumenarc_vla_test.vcxproj /p:Configuration=Release /m /v:m
