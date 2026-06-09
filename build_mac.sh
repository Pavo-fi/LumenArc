#!/bin/bash
# =============================================================================
# Lumen Arc v0.2 Beta - macOS 编译打包脚本
# 
# 使用方法：
#   1. 首次运行前确保已安装依赖：
#      brew install qt@6 vlc python3
#      pip3 install opencv-python-headless numpy
#   2. 运行: chmod +x build_mac.sh && ./build_mac.sh
# =============================================================================

set -e

echo "============================================"
echo "  Lumen Arc v0.2 Beta - macOS Build"
echo "============================================"

# --- 1. 环境检测 ---
echo ""
echo "[1/8] 检测环境..."

# Qt6
if [ -z "$QT6_DIR" ]; then
    if command -v brew &> /dev/null; then
        export QT6_DIR=$(brew --prefix qt@6 2>/dev/null || echo "/opt/homebrew/opt/qt@6")
    else
        echo "错误: 未找到 QT6_DIR 环境变量，且 brew 不可用"
        echo "请设置: export QT6_DIR=/path/to/qt6"
        exit 1
    fi
fi
echo "  Qt6: $QT6_DIR"

# Python
PYTHON3=$(which python3)
if [ -z "$PYTHON3" ]; then
    echo "错误: 未找到 python3"
    echo "请安装: brew install python3"
    exit 1
fi
echo "  Python3: $PYTHON3"

# --- 2. 安装 Python 依赖 ---
echo ""
echo "[2/8] 安装 Python 依赖..."
$PYTHON3 -m pip install opencv-python-headless numpy -q

# --- 3. 配置 CMake ---
echo ""
echo "[3/8] 配置 CMake..."
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release

# --- 4. 编译 ---
echo ""
echo "[4/8] 编译..."
cmake --build build --config Release

# --- 5. macOS 应用打包 ---
echo ""
echo "[5/8] Qt 应用打包..."
APP_PATH="build/Release/LumenArc.app"
MACDEPLOYQT="$QT6_DIR/bin/macdeployqt"
if [ -f "$MACDEPLOYQT" ]; then
    "$MACDEPLOYQT" "$APP_PATH" -qmfiles="portable/translations"
else
    echo "  警告: macdeployqt 未找到，跳过 Qt 依赖打包"
fi

# --- 6. 复制运行时依赖 ---
echo ""
echo "[6/8] 复制运行时依赖..."

# VLC dylibs
VLC_BASE="/Applications/VLC.app/Contents/MacOS"
if [ -d "$VLC_BASE" ]; then
    cp -f "$VLC_BASE/lib/libvlc.dylib" "$APP_PATH/Contents/MacOS/" 2>/dev/null || true
    cp -f "$VLC_BASE/lib/libvlccore.dylib" "$APP_PATH/Contents/MacOS/" 2>/dev/null || true
    if [ -d "$VLC_BASE/plugins" ]; then
        cp -r "$VLC_BASE/plugins" "$APP_PATH/Contents/MacOS/" 2>/dev/null || true
    fi
    echo "  VLC dylibs 已复制"
else
    echo "  警告: 未找到 VLC.app，请手动复制 libvlc.dylib"
fi

# 分析脚本
cp -f analyze_video.py "$APP_PATH/Contents/MacOS/" 2>/dev/null || true

# 背景图
if [ -f "lightchaser.jpg" ]; then
    cp -f lightchaser.jpg "$APP_PATH/Contents/MacOS/" 2>/dev/null || true
fi

# 操作手册
if [ -f "追光者 Lumen Arc v0.2 Beta — 操作手册.pdf" ]; then
    mkdir -p "$APP_PATH/Contents/Resources"
    cp -f "追光者 Lumen Arc v0.2 Beta — 操作手册.pdf" "$APP_PATH/Contents/Resources/"
fi

# --- 7. 创建 DMG ---
echo ""
echo "[7/8] 创建 DMG..."
DMG_NAME="LumenArc-v0.2-beta-mac"
rm -f "${DMG_NAME}.dmg"
hdiutil create -volname "LumenArc" \
    -srcfolder "$APP_PATH" \
    -ov -format UDZO \
    "${DMG_NAME}.dmg"

echo "  DMG 已创建: ${DMG_NAME}.dmg"

# --- 8. 完成 ---
echo ""
echo "[8/8] 完成！"
echo ""
echo "============================================"
echo "  输出文件: ${DMG_NAME}.dmg"
echo "  使用方法:"
echo "    1. 双击 .dmg 文件"
echo "    2. 拖拽 LumenArc.app 到 Applications"
echo "    3. 双击 LumenArc.app 即可运行"
echo "============================================"
