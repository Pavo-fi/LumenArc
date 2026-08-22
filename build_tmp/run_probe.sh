#!/bin/bash
cd "C:/code/LumenArc/LumenArc_v1.3.0"
export QT_FORCE_STDERR_LOGGING=1
export PATH="/c/code/Qt/6.8.0/msvc2022_64/bin:/c/code/LumenArc/LumenArc_v1.3.0/build/Release/ffmpeg:/c/code/LumenArc/LumenArc_v1.3.0/third_party/ffmpeg/bin:$PATH"
./build_tmp/probe_export.exe "$@"
