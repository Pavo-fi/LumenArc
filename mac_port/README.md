# Lumen Arc v0.2 Beta — Mac 版本移植文件

本目录包含 Mac 版本编译所需的文件和说明。

## 文件说明

| 文件 | 说明 |
|------|------|
| `Info.plist` | macOS 应用包配置（放到 `src/Info.plist`） |
| `build_mac.sh` | 一键编译打包脚本（放到项目根目录） |
| `CMakeLists.txt` | 跨平台构建文件（替换项目根目录的同名文件） |
| `i18n.h` | 新增 `fontSans()`/`fontMono()` 跨平台字体函数 |
| `i18n.cpp` | 字体函数实现 + macOS 重启方式修复 |

## Mac 编译步骤

### 1. 前置条件

```bash
# 安装 Homebrew（如果没有）
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"

# 安装依赖
brew install qt@6 vlc python3
pip3 install opencv-python-headless numpy
```

### 2. 复制文件到项目

将本目录中的文件复制到 `C:\code\LumenArc_v0.2\` 对应位置：
- `Info.plist` → `src/Info.plist`
- `build_mac.sh` → 项目根目录
- `CMakeLists.txt` → 项目根目录（覆盖）
- `i18n.h` → `src/i18n.h`（覆盖）
- `i18n.cpp` → `src/i18n.cpp`（覆盖）

### 3. 编译打包

```bash
cd /path/to/LumenArc_v0.2
chmod +x build_mac.sh
./build_mac.sh
```

输出：`LumenArc-v0.2-beta-mac.dmg`

### 4. 使用

双击 DMG → 拖拽 LumenArc.app 到 Applications → 双击运行

## 注意事项

- 需要 macOS 11.0+ （Big Sur 及以上）
- 嵌入式 Python 需要单独准备（参考 build_mac.sh 第 6 步）
- VLC 需要从 /Applications/VLC.app 复制 dylibs
- 如果要分发给其他 Mac 用户，需要代码签名
