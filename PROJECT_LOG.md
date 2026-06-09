# 追光者 Lumen Arc — 项目日志

> 最后更新：2026-05-31

---

## 项目信息

| 项目 | 说明 |
|------|------|
| 名称 | 追光者 Lumen Arc |
| 版本 | v0.2 |
| 类型 | 视频亮度分析桌面工具 |
| 技术栈 | Qt 6.8 + libVLC + Python/OpenCV + CMake + MSVC 2022 |

---

## 项目沿革

| 版本 | 说明 |
|------|------|
| v0.1 | 初始版本：视频播放、ROI 分析、亮度图表、截图融合、放大镜、倍速播放、中英双语、VLA 持久化 |
| v0.2（当前） | UI 增强 + Bug 修复：启动画面美化（背景图+左对齐布局）、按钮状态提示、放大镜修复、截图叠加交互改进（含外部图片加载）、时间轴刻度尺、放大镜内 ROI、ROI 数据保护、光标时间显示、无控制台窗口、倍速快捷键(c/x/z)、标签快捷键(b)、全量中文化、注释系统、版权文档 |

---

## 架构

```
src/
├── main.cpp                          # QApplication 入口 + 启动画面
├── mainwindow.h/cpp                  # 主窗口：菜单/工具栏/拖放/分析流程
├── videowidget.h/cpp                 # 视频渲染 + OverlayWidget（ROI 交互）
├── chartpanel.h/cpp                  # QChartView：折线图/标签/光标/缩放平移
├── magnifierwidget.h/cpp             # 放大镜（QDockWidget）：滚轮缩放 + 光标跟随
├── snapshotoverlay.h/cpp             # 截图融合浮窗：缩略图/编辑器/滑块
├── pinnedwidget.h/cpp                # 固定时间戳浮动窗口
├── i18n.h/cpp                        # 中英双语支持
├── aboutdialog.h/cpp                 # 关于对话框
├── domain/
│   ├── analysis_snapshot.h           # 不可变值类型 + CSV 导出
│   ├── region_model.h/cpp            # 线程安全 ROI 管理 + 7 色调色板
│   └── timeline_model.h/cpp          # 线程安全时间序列 + .vla v3 文件序列化
└── infrastructure/
    ├── ivideo_engine.h               # 视频引擎抽象接口（含 setRate/rate）
    ├── ianalysis_engine.h            # 分析引擎抽象接口
    ├── vlc_video_engine.h/cpp        # libVLC 实现：I420 回调 + RenderWorker 异步 RGB
    └── python_analysis_engine.h/cpp  # QProcess 运行 analyze_video.py 异步引擎
```

---

## 功能清单

### 视频播放
- [x] libVLC 视频播放 + I420→RGB 异步渲染
- [x] 倍速播放（0.25x/0.5x/1x/2x/4x/8x 循环切换）
- [x] 拖放打开视频文件
- [x] 键盘控制（Space 播放/暂停、←→ 帧进退、↑↓ 音量）
- [x] **[v0.2]** 倍速快捷键（c 加速、x 减速、z 恢复 1x）

### ROI 分析
- [x] ROI 绘制/移动/缩放/删除（7 色循环、键盘 Delete）
- [x] Python + OpenCV 离线亮度分析（QProcess 异步、进度条）
- [x] 多折线图 + HH:MM:SS 手动时间标签
- [x] 青色可拖拽光标 + 视频双向联动
- [x] 图表滚轮缩放 + 中键/Shift+左键平移 + 双击 Fit All
- [x] Y 轴自适应范围（右键菜单切换）
- [x] 图表标签（双击添加、6 色预设、点击跳转）
- [x] **[v0.2]** 标签快捷键（b 在当前播放位置添加标签）
- [x] **[v0.2]** 放大镜内可绘制/调整 ROI，坐标映射回主视频
- [x] **[v0.2]** 调整已有分析数据的 ROI 时弹出数据失效确认提示

### 截图融合
- [x] Capture 截取当前帧 → 缩略图浮窗
- [x] Edit 展开编辑器（大图预览 + 亮度/对比度/透明度滑块）
- [x] Place 叠加到视频画面
- [x] 放大镜同步显示截图融合（按 source rect 裁剪）
- [x] **[v0.2]** 缩略图点击可切换展开/折叠
- [x] **[v0.2]** 工具栏"放置"按钮同步放大镜截图叠加
- [x] **[v0.2]** 从外部图片文件加载叠加层（菜单：文件→加载图片为叠加）

### 放大镜
- [x] QDockWidget，滚轮缩放 1.5x~10x
- [x] Alt 跟随/锁定光标
- [x] 截图叠加叠加同步
- [x] **[v0.2]** 修复内部 OverlayWidget 信号未连接问题
- [x] **[v0.2]** 修复截图叠加卸载后放大镜残留问题
- [x] **[v0.2]** 放大镜内光标移动不再导致视图跳转
- [x] **[v0.2]** 修复放大镜内 ROI 右下边界截断问题

### 时间轴
- [x] **[v0.2]** 起止时间标注（黑色粗体，自动防重叠）
- [x] **[v0.2]** 刻度尺式主/次刻度线（右键菜单可开关）
- [x] **[v0.2]** 青色光标上方显示当前播放时间

### UI 增强
- [x] **[v0.2]** 专业深色科技感启动画面（进度条跟随加载进度移动）
- [x] **[v0.2]** 截取/编辑/放置按钮橙色高亮提示启用状态
- [x] **[v0.2]** 程序启动无控制台窗口（WIN32 子系统）

### 持久化
- [x] .vla v3 文件格式（含 ROI + 时间偏移 + 图表标签 + 截图融合图像+参数）
- [x] CSV 导出（亮度数据 + 标签分文件 `_labels.csv`）
- [x] 自动缓存 .vla 到视频同目录

### 国际化
- [x] 中英双语切换（关于菜单 → 语言 → 中文/English）
- [x] 切换后自动重启应用
- [x] 语言偏好保存到 QSettings

### 关于
- [x] 版本信息、GitHub 项目地址、使用手册（打开 PDF 操作手册）

---

## 已修复的问题

### v0.1 修复

| # | 问题 | 修复 |
|---|------|------|
| 1 | 启动闪退 | 拆分流器创建后再设置拖放 |
| 2 | 分析完成点击 OK 闪退 | setData() 同步 → processEvents() → 再弹对话框 |
| 3 | 析构时 cancelAnalysis 双重 deleteLater | 局部保存指针 → disconnect → kill → 仅一次 deleteLater |
| 4 | 播放到末尾后拖回光标失效 | Ended/Stopped/Idle 改为 stop() → set_time() → play() |
| 5 | 修饰键拖拽视频画面消失 | 放大镜改为 QDockWidget，不再 reparent VideoWidget |
| 6 | 截图融合在放大镜中未映射 | 按 source rect 裁剪 snapshot |
| 7 | VLA 保存截图帧失败 | 截取时立即赋值 imageData |
| 8 | 加载 VLA 后滑块值未恢复 | 添加 setParameters() 恢复亮度/对比度/透明度 |
| 9 | 滑块数值不更新 | onSliderChanged 中添加 label setText |
| 10 | 分析变慢 10x | VideoWidget::paintEvent 添加缓存，避免每帧重复计算 |

### v0.2 修复

| # | 问题 | 修复 |
|---|------|------|
| 11 | 启动画面弹出控制台窗口 | CMakeLists.txt 添加 WIN32 标志 |
| 12 | 启动画面不美观 | 重绘深色科技感启动画面（渐变背景 + 网格纹理 + 青色装饰线） |
| 13 | 启动画面进度条不动 | createSplashPixmap 增加 progress 参数，分阶段更新 |
| 14 | 截取/编辑/放置按钮无状态提示 | 添加橙色 fusionBtnStyle（启用时橙色边框，checked 时橙色背景） |
| 15 | 放大镜内部 OverlayWidget 信号未连接 | 构造函数连接 magnifierWheelZoom 和 magnifierCursorMoved |
| 16 | 截图叠加卸载后放大镜残留 | clearSnapshotOverlay 增加强制 update；onFrameReady 增加防御性检查 |
| 17 | 放大镜内光标移动导致视图跳转 | 断开 magnifierCursorMoved 信号连接 |
| 18 | 工具栏"放置"按钮不同步放大镜 | checked=true 分支增加 setSnapshotOverlay 调用 |
| 19 | 时间轴刻度尺不随缩放/平移更新 | resizeEvent 改为调用 updateTimeLabels() |
| 20 | 起止时间标签颜色不一致 | 改为黑色粗体，自动防重叠 |
| 21 | 刻度尺只有孤零零的几条线 | 添加主/次刻度线（4 等分次刻度） |
| 22 | 缩略图只能放大不能缩小 | 点击预览图区域可折叠回缩略图模式 |
| 23 | 放大镜内 ROI 右下边界截断 | clampRectToVideo 考虑 m_videoOriginOffset |
| 24 | 调整 ROI 不提示数据失效 | mouseReleaseEvent 中 emit regionAdjustmentFinished，MainWindow 弹出确认 |
| 25 | 光标移出视频区域放大镜仍移动 | mouseMoveEvent 增加 m_videoDisplayRect.contains(pos) 检查 |
| 26 | OverlayWidget 吞掉所有键盘事件 | keyPressEvent 改为 event->ignore()，事件传播到 MainWindow |
| 27 | 暂停后视频/图表不同步 | onPollPosition 检测 VLC 实际状态再停止 timer |
| 28 | stateChanged 信号未连接 | 按钮状态根据引擎实际状态同步更新 |
| 29 | 按钮获焦后空格键被按钮吞掉 | eventFilter 拦截按钮上的空格键，重定向到播放/暂停 |
| 30 | 分析相关弹窗未中文化 | ~20 处 QMessageBox/QInputDialog 文本添加 lang() |
| 31 | 更改倍速偶发自动暂停 | onPollPosition 排除 libvlc_Error 瞬态检测 |
| 32 | 菜单项左侧间距过大 | QMenu::item padding 样式表调整 |
| 33 | 启动画面背景图 + 左侧布局 | lightchaser.jpg 背景 + 左对齐 780×450 布局 |
| 34 | 截图叠加支持外部图片加载 | onLoadOverlayImage + 菜单项"加载图片为叠加" |
| 35 | 标签快捷键 b | keyPressEvent 添加 Key_B + addLabelAtTime |
| 36 | 所有源文件添加注释 | 文件头 + 类注释 + 方法注释（30 个文件） |
| 37 | Copyright 统一 | 全部改为 Copyright 2026 Huang Jingyun |

---

## VLA v3 文件格式

```json
{
  "version": 3,
  "analyzed_at": "2026-05-30T10:00:00",
  "time_offset": 36000000,
  "regions": [{"x": 100, "y": 200, "w": 300, "h": 150}],
  "magnifier": {"x": 100, "y": 200, "w": 300, "h": 250},
  "labels": [{"time_ms": 5000, "text": "门开了", "color": "#ff0000"}],
  "pinned": {"x": 1200, "y": 10, "w": 200, "h": 30},
  "snapshot_fusion": {
    "brightness": 10,
    "contrast": -5,
    "opacity": 70,
    "image": "iVBORw0KGgoAAAANSUhEUgAA..."
  },
  "timestamps": [0, 33, 66, ...],
  "luminances": [[128.5, 130.2, ...]],
  "point_count": 1000,
  "region_count": 1
}
```

---

## 构建环境

| 依赖 | 版本 |
|------|------|
| Qt | 6.8.0 (Widgets + Charts) |
| CMake | 3.16+ |
| MSVC | 2022 |
| libVLC | 3.0.21 |
| Python | 3.10+ (运行时探测) |
| OpenCV (Python) | opencv-python-headless |

### 构建命令

```powershell
$env:QT6_DIR = "C:/code/Qt/6.8.0/msvc2022_64"
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```
