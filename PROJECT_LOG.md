# 追光者 Lumen Arc — 项目日志

> 最后更新：2026-06-28

---

## 项目信息

| 项目 | 说明 |
|------|------|
| 名称 | 追光者 Lumen Arc |
| 版本 | v0.35 |
| 类型 | 视频亮度分析桌面工具 |
| 技术栈 | Qt 6.8 + libVLC + Python/OpenCV + CMake + MSVC 2022 |

---

## 项目沿革

| 版本 | 说明 |
|------|------|
| v0.1 | 初始版本：视频播放、ROI 分析、亮度图表、截图融合、放大镜、倍速播放、中英双语、VLA 持久化 |
| v0.2 | UI 增强 + Bug 修复：启动画面美化（背景图+左对齐布局）、按钮状态提示、放大镜修复、截图叠加交互改进（含外部图片加载）、时间轴刻度尺、放大镜内 ROI、ROI 数据保护、光标时间显示、无控制台窗口、倍速快捷键(c/x/z)、标签快捷键(b)、全量中文化、注释系统、版权文档 |
| v0.3（当前） | 音频分析 + 多进程 + 多视频：Python 多进程亮度分析、ffmpeg 音频提取、频谱图显示、音量折线图、多视频列表管理（拖拽排序）、后台异步分析（状态栏进度）、.vla v4 格式（含音频数据） |
| v0.35（当前） | 交互优化 + Bug 修复：放大镜锚点缩放、A/B 区域循环播放、音频降噪、语谱图时间轴缩放双向同步、折叠布局空隙修复、快捷键优化（J/K/L/N）、分析完成提示优化 |
| v0.5（进行中） | 新功能：多边形 ROI、辅助线工具（绘制/选中/平移/端点拖拽/悬停高亮）、跨视频 ROI 复制粘贴、右键直接删除。待修复：多边形闭合/亮度分析、辅助线删除后交互、复制粘贴数据隔离 |

---

## 架构

```
src/
├── main.cpp                          # QApplication 入口 + 启动画面
├── mainwindow.h/cpp                  # 主窗口：菜单/工具栏/拖放/分析流程
├── videowidget.h/cpp                 # 视频渲染 + OverlayWidget（ROI 交互）
├── chartpanel.h/cpp                  # QChartView：折线图/音量线/标签/光标/缩放平移
├── spectrogrampanel_enhanced.h/cpp   # [v0.3] 增强频谱图：GPU 渲染 + 对数频率 + 时间轴缩放
├── videolistpanel.h/cpp              # [v0.3] 视频列表面板：QDockWidget + 拖拽排序
├── magnifierwidget.h/cpp             # 放大镜（QDockWidget）：锚点缩放 + 中键拖拽
├── snapshotoverlay.h/cpp             # 截图融合浮窗：缩略图/编辑器/滑块
├── pinnedwidget.h/cpp                # 固定时间戳浮动窗口
├── i18n.h/cpp                        # 中英双语支持
├── aboutdialog.h/cpp                 # 关于对话框
├── domain/
│   ├── analysis_snapshot.h           # 不可变值类型 + AudioData + CSV 导出
│   ├── region_model.h/cpp            # 线程安全 ROI 管理 + 7 色调色板
│   ├── region_shape.h                # [v0.5] RegionShape 类型定义（Rect/Polygon 联合体）
│   ├── polygon_model.h/cpp           # [v0.5] 线程安全多边形 ROI 管理
│   ├── guide_line.h                  # [v0.5] 辅助线数据结构
│   ├── guide_line_model.h/cpp        # [v0.5] 线程安全辅助线管理
│   └── timeline_model.h/cpp          # 线程安全时间序列 + .vla v4 文件序列化
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
- [ ] **[v0.5]** 多边形 ROI 绘制（可绘制点，闭合和亮度分析待修复）

### 截图融合
- [x] Capture 截取当前帧 → 缩略图浮窗
- [x] Edit 展开编辑器（大图预览 + 亮度/对比度/透明度滑块）
- [x] Place 叠加到视频画面
- [x] 放大镜同步显示截图融合（按 source rect 裁剪）
- [x] **[v0.2]** 缩略图点击可切换展开/折叠
- [x] **[v0.2]** 工具栏"放置"按钮同步放大镜截图叠加
- [x] **[v0.2]** 从外部图片文件加载叠加层（菜单：文件→加载图片为叠加）

### 辅助线工具 [v0.5 新增]
- [x] G 键切换辅助线模式
- [x] 左键拖拽绘制辅助线（水平/垂直/斜线）
- [x] Shift 约束绘制水平或垂直线
- [x] 辅助线选中（线体/端点）
- [x] 平移整条辅助线
- [x] 拖动端点调整（Shift 约束水平/垂直，相对于另一端点）
- [x] 悬停高亮（线段加粗变亮 + 端点手柄显示）
- [x] 右键直接删除（无需先选中）
- [x] Delete 键删除选中辅助线
- [x] 放大镜内显示辅助线
- [ ] 删除后需额外点击才能恢复交互（已知 bug）

### 跨视频 ROI 复制粘贴 [v0.5 新增]
- [x] 工具栏"复制ROI"按钮 → 复制所有矩形+多边形
- [x] 工具栏"粘贴ROI"按钮 → 替换/追加选择对话框
- [ ] 粘贴后亮度曲线被错误共享（已知 bug）

### 放大镜
- [x] QDockWidget，滚轮缩放 1.5x~10x
- [x] 滚轮以鼠标位置为中心缩放（zoom-at-point 算法）
- [x] 中键拖拽平移放大镜视图（主视频和放大镜内均生效）
- [x] 创建放大镜时自动隐藏视频列表，显示占位侧边栏
- [x] 关闭放大镜时自动恢复视频列表状态
- [x] 截图叠加同步
- [x] **[v0.2]** 修复内部 OverlayWidget 信号未连接问题
- [x] **[v0.2]** 修复截图叠加卸载后放大镜残留问题
- [x] **[v0.2]** 放大镜内光标移动不再导致视图跳转
- [x] **[v0.2]** 修复放大镜内 ROI 右下边界截断问题
- [x] **[v0.35]** 修复放大镜创建时不显示视频帧的问题（createMagnifier 中转发当前帧）
- [x] **[v0.35]** 移除 Alt 键跟随模式，简化交互

### 时间轴
- [x] **[v0.2]** 起止时间标注（黑色粗体，自动防重叠）
- [x] **[v0.2]** 刻度尺式主/次刻度线（右键菜单可开关）
- [x] **[v0.2]** 青色光标上方显示当前播放时间

### UI 增强
- [x] **[v0.2]** 专业深色科技感启动画面（进度条跟随加载进度移动）
- [x] **[v0.2]** 截取/编辑/放置按钮橙色高亮提示启用状态
- [x] **[v0.2]** 程序启动无控制台窗口（WIN32 子系统）

### 快捷键优化 [v0.35 新增]
- [x] J 键减倍速、K 键播放/暂停、L 键加倍速
- [x] N 键添加标签（原 Tab 键）
- [x] A 键设置 A 点、B 键设置 B 点
- [x] 删除 keyPressEvent 重复代码（eventFilter 完全覆盖）

### 语谱图增强 [v0.35 新增]
- [x] 普通滚轮缩放时间轴（以鼠标位置为中心）
- [x] 图表 ↔ 语谱图双向同步（X 轴范围 + 光标 + 坐标轴标签）
- [x] Ctrl+滚轮缩放频率轴（原有功能保持）
- [x] 双击 Y 轴区域重置频率范围

### 布局优化 [v0.35 新增]
- [x] splitter handle 宽度设为 0，消除折叠面板间的间隙
- [x] 折叠图表/语谱图时设 setMinimumSize(0,0) + setMaximumSize(0,0)，防止隐藏内容撑大容器
- [x] 展开时检查对方面板折叠状态，防止空间分配错误
- [x] 放大镜呼出时自动隐藏视频列表面板，显示占位侧边栏（24px）
- [x] 关放镜关闭时自动恢复视频列表状态

### 分析完成提示优化 [v0.35 新增]
- [x] 亮度分析完成提示 5 秒自动关闭 + OK 按钮
- [x] 音频分析完成提示"音频分析完成，音量图、语谱图已生成。" + 5 秒自动关闭

### 快捷键焦点问题调试 [v0.35 进行中]

**问题描述**：点击工具栏按钮后，空格键和方向键仍被按钮消费（触发按钮点击或移动焦点），而非触发视频播放快捷键。

**Qt 事件机制分析**：
- 当 QPushButton 有焦点时，Space 键由 `QPushButton::keyPressEvent()` 直接处理
- 事件**不会**传播到 MainWindow 的 `eventFilter`（因为 eventFilter 只监控 overlay、menuBar、MainWindow）
- 方向键在按钮间移动焦点（Qt 默认行为）

**已尝试的方案（均未解决）**：

| # | 方案 | 原理 | 结果 | 失败原因分析 |
|---|------|------|------|-------------|
| 1 | `clearFocus()` in mousePressEvent | 点击视频时清除按钮焦点 | ❌ | 可能：clearFocus 后焦点回到 MainWindow，但 MainWindow 的 eventFilter 未正确拦截；或按钮在鼠标点击时重新获取焦点 |
| 2 | eventFilter 中拦截 QPushButton 的 Space/方向键 | 在 eventFilter 中检查焦点 widget 类型 | ❌ | 根本原因：事件根本不会到达 eventFilter。QPushButton::keyPressEvent 在事件到达 MainWindow 之前就消费了事件 |
| 3 | 所有按钮 `setFocusPolicy(Qt::NoFocus)` | 按钮不接收焦点 | ❌ | 可能原因：(a) QToolBar 本身可能有焦点策略；(b) 其他 widget（如 QSlider、QChartView）也可能获取焦点；(c) 需要验证 NoFocus 是否真正生效 |

**下一步排查方向**：
1. 在 eventFilter 中添加调试输出，打印 `focusWidget()` 的类型和名称，确认焦点在哪个 widget 上
2. 检查 QToolBar 的焦点策略
3. 检查 QChartView（ChartPanel）的焦点策略
4. 考虑使用 `QApplication::installEventFilter()` 在应用级别拦截所有事件

### 音频分析 [v0.3 新增]
- [x] ffmpeg 音频提取（subprocess 调用，便携版打包）
- [x] 纯 numpy STFT 语谱图计算（零额外依赖）
- [x] 纯 numpy RMS 音量计算（归一化 0-1）
- [x] `--audio-only` 独立音频分析模式
- [x] `--include-audio` 音频分析与亮度分析分离（亮度分析默认不含音频）
- [x] `--ffmpeg-path` ffmpeg 路径传递（便携版优先 → 同目录 → PATH）

### 多进程分析 [v0.3 新增]
- [x] Python multiprocessing.Pool 分段并行亮度分析
- [x] 自适应进程数（<30s 单进程，<120s 2 进程，更长最多 4 进程）
- [x] `--check-fps` 帧率检查 + 总帧数返回
- [x] `--processes N` 多进程参数
- [x] `--start-frame` / `--end-frame` 分段分析

### 多视频管理 [v0.3 新增]
- [x] VideoListPanel 左侧 QDockWidget 视频列表
- [x] QListWidget 拖拽排序 + timeOffsetMs 自动重算
- [x] 多选文件打开（getOpenFileNames）
- [x] 帧率一致性检查（差异 >0.1 弹窗警告）
- [x] 多视频首尾相接分析，时间轴无缝衔接

### 后台异步分析 [v0.3 新增]
- [x] 状态栏 QProgressBar 进度显示（替代模态 QProgressDialog）
- [x] 工具栏"取消分析"按钮
- [x] 分析期间保持视频播放功能（不阻塞 UI）
- [x] 取消分析时 terminate() 优先，超时再 kill()

### 频谱图显示 [v0.3 新增]
- [x] SpectrogramPanel 独立 QWidget + QImage 热力图渲染
- [x] 与 ChartPanel X 轴同步（QValueAxis::rangeChanged 信号）
- [x] 动态颜色范围（min/max 自动计算，蓝→绿→黄→红）
- [x] 时间轴标签（HH:MM:SS）+ 频率标签（Hz）
- [x] 鼠标悬停 tooltip（Time / Freq / Value）
- [x] **[v0.3]** 播放指针（青色虚线竖线，与 ChartPanel 光标同步）

### 音量折线图 [v0.3 新增]
- [x] QLineSeries 绿色半透明音量线（ChartPanel 内）
- [x] 右侧 Y 轴 "Volume (dB)"
- [x] **[v0.3]** 音频分析独立于亮度分析（--include-audio 参数分离）
- [x] **[v0.3]** 纯音频模式支持（无亮度数据时用音频时长设置 X 轴）
- [x] **[v0.3]** 图例点击切换曲线可见性（QLegendMarker::clicked）
- [x] **[v0.35]** Volume 图例在音频分析前隐藏

### A/B 区域循环播放 [v0.35 新增]
- [x] 键盘 A 键设置 A 点、B 键设置 B 点
- [x] 右键菜单设置 A/B 点
- [x] 设置 A/B 后自动开启循环播放
- [x] 按 L 键切换循环开/关
- [x] 清除 A/B 区域后自动关闭循环
- [x] 到达 B 点自动跳回 A 点（循环模式）
- [x] 到达 B 点自动暂停（非循环模式）
- [x] 设置 A/B 后自动缩放到区域
- [x] 清除 A/B 后恢复全时间轴
- [x] A/B 标记跟随坐标轴缩放/平移
- [x] A/B 状态保存到 VideoState（切换视频时保持）
- [x] 图表右键菜单添加"删除光标处标签"

### 音频降噪 [v0.35 新增]
- [x] Python 端频谱门控降噪算法（纯 numpy，零额外依赖）
- [x] `--noise-reduction` CLI 参数（0=关闭, 1=标准, 2=强）
- [x] 语谱图标题栏降噪滑块（0.0~2.0）+ 应用按钮
- [x] 点击应用自动触发音频重新分析

### 持久化
- [x] .vla v3 文件格式（含 ROI + 时间偏移 + 图表标签 + 截图融合图像+参数）
- [x] **[v0.3]** .vla v4 文件格式（新增 audio 字段，向后兼容 v3）
- [x] CSV 导出（亮度数据 + 标签分文件 `_labels.csv`）
- [x] **[v0.3]** CSV 导出增加 Volume 列
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

### v0.3 修复（审查整改）

| # | 问题 | 修复 |
|---|------|------|
| 38 | 多进程分析硬编码为单进程 | getVideoInfo() 返回 fps+totalFrames，computeProcessCount() 实际调用 |
| 39 | 版本号大面积 v0.2 残留 | 更新 14 个文件 + app.rc VERSIONINFO 资源块 |
| 40 | 拖拽排序后 timeOffsetMs 未重算 | Qt::UserRole 存储索引，onItemMoved() 累加重算 |
| 41 | onOpenFile 不支持多选 | 改用 getOpenFileNames()，遍历添加到列表 |
| 42 | 帧率一致性检查未集成 | onAnalyze() 中遍历 FPS，差异 >0.1 弹窗警告 |
| 43 | 倍速浮点比较跳过匹配 | qFuzzyCompare → qAbs(...) < 0.01f |
| 44 | cancelAnalysis 未给清理机会 | terminate() 优先等 3 秒，超时再 kill() |
| 45 | 频谱图缺时间轴标签 | renderSpectrogram() 底部绘制 HH:MM:SS |
| 46 | 频谱图空 spectrogram[0] 边界 | 增加 spectrogram[0].isEmpty() 检查 |
| 47 | processEvents 竞态风险 | 改用 QTimer::singleShot(0, ...) 延迟弹窗 |
| 48 | 频谱图颜色范围硬编码 | setSpectrogramData() 动态计算 min/max |
| 49 | 多进程空段合并 IndexError | 排序前过滤空结果 |
| 50 | 时间戳无毫秒精度 | formatTimeMs 输出 HH:MM:SS.zzz |
| 51 | splitter drop 冗余 | 移除 eventFilter 中 splitter 拦截，统一 dropEvent() |
| 52 | 频谱图无 tooltip | mouseMoveEvent + setMouseTracking 显示 Time/Freq/Value |
| 53 | 临时文件残留 | _temp_files 列表 + atexit + SIGTERM 信号处理 |
| 54 | 音量 Y 轴标题不完整 | 改为 "Volume (0-1)" |
| 55 | getVideoFps 残留定义 | 删除旧函数，统一用 getVideoInfo() |
| 56 | 脚本路径 fallback 不可达 | isEmpty() 检查移到 exists() 之前 |
| 57 | onFinished 信号在清理前发出 | 局部指针 proc，先置 nullptr 再发信号 |
| 58 | 重复 include videowidget.h | 删除重复行 |
| 59 | 未使用的 m_videoList 成员 | 删除 |
| 60 | qFuzzyCompare 近零不可靠 | 改用 1.0 + 偏移技巧 |
| 61 | dropEvent 使用错误的 duration | 改为 0（播放时更新） |
| 62 | Python 检测失败静默返回 | 添加 QMessageBox::warning 提示 |
| 63 | 版本号仍有 21 个文件残留 v0.2 | 批量更新全部 .h/.cpp 文件 @version 为 0.3 |
| 64 | timeResolutionMs 整数除法写法脆弱 | (hopLength/sampleRate)*1000 → 1000*hopLength/sampleRate（2处） |
| 65 | updateDuration 重排后索引错位 | 通过 UserRole 查找正确的列表项而非直接用行号 |
| 66 | getVideoInfo 阻塞主线程重复查询 | 添加 QMap 缓存，同一视频只查一次 |
| 67 | startMultiVideoPlayback msleep+processEvents 重入 | 改用 QElapsedTimer + ExcludeUserInputEvents |
| 68 | 音量曲线错误集成在亮度分析中 | Python 脚本分离音频分析，新增 --include-audio 参数 |
| 69 | 音频分析按钮无法独立显示音量曲线 | onDataReplaced 支持纯音频模式，用音频时长设置 X 轴 |
| 70 | 频谱图缺少播放指针 | SpectrogramPanel 新增 setCursorTime + 青色虚线光标绘制 |
| 71 | 图例无法点击隐藏曲线 | rebuildSeries 连接 QLegendMarker::clicked 切换 series 可见性 |
| 72 | 亮度分析覆盖已有音频数据 | onAnalysisFinished 保留已有音频，不清除频谱图 |

---

## VLA 文件格式

### v4 格式（v0.3 当前）

```json
{
  "version": 4,
  "analyzed_at": "2026-06-13T10:00:00",
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
  "audio": {
    "volume": [0.3, 0.5, 0.8, ...],
    "spectrogram": [[0.1, 0.2, ...], ...],
    "sample_rate": 16000,
    "hop_length": 512,
    "n_fft": 2048
  },
  "timestamps": [0, 33, 66, ...],
  "luminances": [[128.5, 130.2, ...]],
  "point_count": 1000,
  "region_count": 1
}
```

### v3 格式（v0.2，向后兼容）

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

| 依赖 | 版本 | 说明 |
|------|------|------|
| Qt | 6.8.0 (Widgets + Charts) | 不变 |
| CMake | 3.16+ | 不变 |
| MSVC | 2022 | 不变 |
| libVLC | 3.0.21 | 不变 |
| Python | 3.8+ (运行时探测) | 最低版本调整为 3.8 |
| OpenCV (Python) | opencv-python-headless | 不变 |
| numpy | >=1.24.0 | 不变 |
| ffmpeg | 便携版打包 | v0.3 新增，打包在 `portable/ffmpeg/` |

### 构建命令

```powershell
$env:QT6_DIR = "C:/code/Qt/6.8.0/msvc2022_64"
cmake -B build -S . -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

### 便携版打包

```
portable/
├── LumenArc.exe
├── analyze_video.py
├── ffmpeg/
│   └── ffmpeg.exe          ← 新增（通过 setup_deps.py 下载）
├── libvlc.dll
├── ... (Qt DLLs)
```

---

## 2026-06-16 ~ 2026-06-17：语谱图引擎重构 + UI 全面优化

### 一、语谱图 GPU 渲染引擎重构

#### 1.1 根本问题排查

发现并修复了语谱图完全不显示的根本原因链：

| 序号 | 问题 | 根因 | 修复 |
|------|------|------|------|
| 1 | 语谱图漆黑一片 | `allocateStorage()` 调用 `glTexStorage2D` 创建不可变存储，后续 `glTexImage2D` 静默失败（`GL_INVALID_OPERATION`） | 移除 `allocateStorage()`，直接用 `glTexImage2D` |
| 2 | 应用启动后主窗口不显示 | `AA_UseSoftwareOpenGL`（main.cpp:124）强制软件渲染，但 `opengl32sw.dll` 未部署。回退到系统 `opengl32.dll`，无法满足 GL 3.3 Core Profile 要求 | 移除 `AA_UseSoftwareOpenGL` |
| 3 | 语谱图只显示左下角 1/4 | `glScissor` 使用逻辑像素但 OpenGL 期望设备像素，200% 缩放显示器上只覆盖帧缓冲左下四分之一 | `glScissor` 参数乘以 `devicePixelRatioF()` |
| 4 | 语谱图时间轴错位 | `setData()` 在 `setSpectrogramData()` 之前调用，同步触发 `xAxisRangeChanged`，用图表范围覆盖语谱图自身的持续时间 | 移除 `if (m_viewXMax <= m_viewXMin)` 条件，始终用语谱图自身持续时间设置 X 范围 |
| 5 | 语谱图频谱数据到达后仍空白 | Python `atexit` 处理器在 C++ 读取前删除了频谱二进制文件 | 不将 spec 文件加入 `_temp_files`，C++ 读取后自行删除 |
| 6 | 进度条显示 100% 后程序卡住 | Python `print(json)` 写几十 MB 到 stdout 时管道满阻塞 | `onReadyReadStderr()` 中主动排空 stdout；频谱改用二进制文件传输 |

#### 1.2 OpenGL 版本升级

- 上下文版本：`2.0 CompatibilityProfile` → `3.3 CoreProfile`
- GLSL 版本：`#version 110` → `#version 330 core`
- Shader 语法：`attribute`→`in`，`varying`→`out`，`texture2D`→`texture`，`gl_FragColor`→`fragColor`
- 函数类：`QOpenGLFunctions` → `QOpenGLFunctions_3_3_Core`

#### 1.3 纹理上传优化

- 移除 `allocateStorage()`，直接用 `glTexImage2D` 分配可变存储
- 添加 `GL_MAX_TEXTURE_SIZE` 查询，超过时 CPU 端降采样列
- Python 端添加 `MAX_SPEC_FRAMES = 8000` 限制
- 添加 `glGetError()` 检查和 `qWarning` 日志

#### 1.4 频谱数据传输优化

**旧方案**：`spec.tolist()` → `json.dumps` → 管道传输 → `QJsonDocument::fromJson` → 逐值解析
- 问题：820 万个浮点数，JSON 约 50-100MB，`json.dumps` 耗时 10-30 秒，C++ 解析再耗时 10-30 秒

**新方案**：`spec.tofile()` → 二进制文件 → C++ `QFile::readAll()` + `memcpy`
- 频谱数据：~65MB 二进制文件，毫秒级读取
- JSON 只传文件路径和维度，约 100KB
- 总延迟：从 30-120 秒降至 <3 秒

---

### 二、UI 全面优化

#### 2.1 语谱图功能完善

| 功能 | 说明 |
|------|------|
| 线性频率轴 | 默认 `FreqScale::Linear`，天花板 8KHz |
| 底噪阈值滑块 | 工具栏水平滑块，范围 [-10.0, 0.0] dB，默认 -6.0 dB，实时调节 |
| 坐标轴动态对齐 | 通过 `QChart::plotAreaChanged` 信号同步语谱图 leftMargin/rightMargin |
| 坐标轴标签动态间距 | `minLabelSpacing = 25px`，跳过过密标签 |
| 频率缩放修复 | 对数用 `pow()` 比例锚定鼠标，线性用差值锚定 |
| 频率平移修复 | 对数改为乘法比例 `exp(-dy/heatH * logRange)` |
| Y 轴范围保留 | 仅首次加载重置，`m_yRangeInitialized` 标志 |
| 除零保护 | `paintGL()` 使用 `safeTimeResolutionMs()` |

#### 2.2 语谱图光标拖拽

- `mousePressEvent`：检测光标线附近点击（±10px），启动拖拽；点击远离光标只 seek
- `mouseMoveEvent`：拖拽中计算时间，发出 `seekRequested`；hover 检测变 `SizeHorCursor`
- `mouseReleaseEvent`：`unsetCursor()` 恢复
- 与趋势图光标逻辑完全一致

#### 2.3 趋势图 UI

- 背景色：`RGB(245,245,245)` → `RGB(40,40,40)` 深灰
- 字体颜色：`Qt::black` → `#F5F0E8` 骨白色
- 标题：`setTitleFont("Microsoft YaHei", 14, Bold)` + `setMargins(QMargins(2,2,2,2))`
- 轴标签/刻度/图例/光标标签全部改为骨白色

#### 2.4 音量曲线动态 Y 轴

- 固定 `-80, 0` dB → 动态范围
- 扫描实际最低 dB 值，加 5% margin，至少 1 dB 范围

#### 2.5 颜色统一

- 语谱图光标/坐标轴：`#00FFFF` → `#FF981C`（橙色）
- 趋势图光标：`#00FFFF` → `#FF981C`

#### 2.6 收起/展开机制

**量化分析面板**：
- 容器 widget 包裹 ChartPanel
- 标题栏（24px，`#363636`）：▼/▲ 按钮 + "量化分析" 标签
- 收起时隐藏内容区域，标题栏保持可见

**语谱图面板**：
- 容器 widget 包裹 SpectrogramPanelEnhanced
- 标题栏（24px，`#363636`）：▼/▲ 按钮 + "语谱图" 标签

**视频列表**：
- `QDockWidget::setTitleBarWidget()` 自定义竖向标题栏
- 竖排"视频列表"文字 + ◀ 收起按钮
- `resizeDocks({m_videoListPanel}, {250}, Qt::Horizontal)` 限制初始宽度
- 移除 `DockWidgetClosable`

#### 2.7 其他修复

| 修复 | 说明 |
|------|------|
| MKV 亮度分析只生成一半曲线 | 不依赖 `CAP_PROP_FRAME_COUNT`，改为 `cap.read()` 返回 `ret=False` 时退出 |
| 启动页/关于页文字 | `视频亮度分析系统` → `火灾调查视频分析工具` |
| app.rc | `Video Luminance Analyzer` → `Fire Investigation Video Analyzer` |
| 语谱图窗口大小适配 | `setStretchFactor(2, 0)` → `setStretchFactor(2, 1)` |
| VLA 加载后语谱图不显示 | 3处 VLA 加载后调用 `setSpectrogramData(snapshot.audio)` |
| 进度条时序 | 100% 仅在 JSON 输出完成后打印 |

---

### 三、代码变更统计

| 文件 | 主要变更 |
|------|----------|
| `main.cpp` | 移除 `AA_UseSoftwareOpenGL`，修改启动页文字 |
| `spectrogrampanel_enhanced.h` | GL 3.3 Core，成员变量重构，新增信号/槽 |
| `spectrogrampanel_enhanced.cpp` | 全面重写：shader、纹理上传、交互、坐标轴 |
| `chartpanel.h` | 新增 `plotArea()`/`plotAreaUpdated` 信号 |
| `chartpanel.cpp` | 深灰背景、骨白色字体、动态音量 Y 轴 |
| `mainwindow.h` | 新增容器 widget 成员 |
| `mainwindow.cpp` | 重构布局：容器标题栏、收起展开、视频列表竖向标题栏 |
| `python_analysis_engine.cpp` | 二进制文件读取、管道排空、诊断日志 |
| `analysis_snapshot.h` | 新增 `specMin`/`specMax` 字段 |
| `analyze_video.py` | 二进制文件传输、MKV 帧读取修复、进度报告优化 |
| `aboutdialog.cpp` | 文字修改 |
| `app.rc` | 文字修改 |

---

## 2026-06-18：布局优化 + 收起展开重构 + 底噪控制

### 一、布局系统重构

#### 1.1 初始比例优化

Stretch factors 从 `2:1:1` 调整为 `7:5:4`（44%:31%:25%），给量化分析和语谱图更多显示空间：

| 面板 | 旧比例 | 新比例 | 实际绘图区高度（1080p） |
|------|--------|--------|------------------------|
| 视频播放器 | 55% | 44% | ~437px |
| 量化分析 | 25% | 31% | ~250px |
| 语谱图 | 20% | 25% | ~200px |

- 移除 `m_splitter->setSizes()` 调用，让 stretch factors 完全控制比例
- 启动时 `window.showMaximized()` 最大化窗口

#### 1.2 容器标题栏系统

量化分析和语谱图采用统一的容器 widget + 标题栏模式：

```
Container (QWidget + QVBoxLayout)
├── TitleBar (24px, #363636)
│   ├── CollapseBtn (▼/▲)
│   └── Label ("量化分析" / "语谱图")
└── Content (stretch=1)
    └── ChartPanel / SpectrogramPanelEnhanced
```

- 标题栏始终可见
- 收起时隐藏 Content 区域，splitter 将目标区域缩至 24px
- 展开时恢复保存的尺寸

#### 1.3 收起/展开逻辑重构

**问题**：之前使用单一 `m_splitterSizes` 变量，两个面板的折叠互相干扰。

**修复**：使用独立的保存变量：
- `m_chartSavedSizes` — 量化分析折叠前的 splitter 尺寸
- `m_spectrogramSavedSizes` — 语谱图折叠前的 splitter 尺寸

**逻辑**：
- 量化分析折叠：`sizes[1] = 24`，空间给视频播放器
- 语谱图折叠：`sizes[2] = 24`，空间分配给视频和量化分析
- 展开：从各自的保存变量恢复

### 二、视频列表重构

#### 2.1 布局改为内容区域内水平布局

**问题**：之前使用 `setTitleBarWidget()` 设置自定义标题栏，但 `QDockWidget` 的 `DockWidgetMovable` 会拦截标题栏的鼠标事件，导致按钮失效。

**修复**：不使用 `setTitleBarWidget()`，改为 dock widget 内容区域内的水平布局：
- 左侧 24px 竖向标题栏（◀ 按钮 + 竖排"视频列表"文字）
- 右侧列表内容
- `setTitleBarWidget(new QWidget())` + `setFixedHeight(0)` 隐藏默认标题栏

#### 2.2 拖拽文件支持

**新增**：`VideoListPanel` 添加 `dragEnterEvent` / `dropEvent` 重写：
- 接受外部视频文件拖入（mp4/avi/mkv/mov/wmv/flv/webm/m4v/mpg/mpeg/ts）
- 自动去重
- `setAcceptDrops(true)` 启用拖放

### 三、UI 细节优化

#### 3.1 ChartPanel 间距修复

- `m_chartContent` margins：`(0, 30, 0, 0)` → `(0, 2, 0, 0)`（消除 50px 空隙）
- `m_chart->setMargins()`：`(2, 2, 2, 2)` → `(2, 35, 2, 2)`（图表内部留出光标标签空间）
- 删除 X 轴 'Time' 标题文字，释放约 15px

#### 3.2 底噪阈值优化

- 默认值从 `-6.0 dB` → `-5.5 dB`
- 滑块移到语谱图标题栏（`specTitleLabel` 右侧）
- `m_minValue` 默认值同步改为 `-5.5`
- `setSpectrogramData()` 保留用户噪声阈值：`if (dataMin > m_minValue)` 才更新

#### 3.3 网格线移除

- `m_axisX->setGridLineVisible(false)`
- `m_axisY->setGridLineVisible(false)`
- `m_axisYVolume->setGridLineVisible(false)`

#### 3.4 坐标轴动态对齐

- `m_storedChartPlotArea` 存储图表 plotArea
- `paintGL()` 开头用存储的 plotArea + 当前 `width()` 重新计算 margins
- 窗口缩放后坐标轴自动重新对齐

### 四、代码变更统计

| 文件 | 主要变更 |
|------|----------|
| `main.cpp` | `show()` → `showMaximized()` |
| `mainwindow.h` | 新增 `m_chartSavedSizes`/`m_spectrogramSavedSizes`/`m_videoListCollapseBtn`/`m_videoListSidebar`/`m_videoListContent` |
| `mainwindow.cpp` | 布局重构：stretch factors 7:5:4、独立折叠保存变量、视频列表内容区域内布局、底噪滑块移到标题栏、ChartPanel margins 修复 |
| `chartpanel.cpp` | 删除 X 轴 'Time' 标题、网格线隐藏、图表内部顶部 margin 增大 |
| `spectrogrampanel_enhanced.h` | `m_minValue` 默认改为 -5.5、新增 `m_storedChartPlotArea` |
| `spectrogrampanel_enhanced.cpp` | `paintGL()` 重算 margins、`setSpectrogramData()` 保留噪声阈值 |
| `videolistpanel.h` | 新增 `dragEnterEvent`/`dropEvent` 重写 |
| `videolistpanel.cpp` | 拖拽文件支持、`setAcceptDrops(true)` |

---

## 2026-06-24 交互修复 + 语谱图缩放漂移修复 + 音频分析持久化

### 一、语谱图缩放漂移修复

**问题**：语谱图在 X 轴缩放时，峰值点出现严重漂移。

**根因**：GPU 全屏四边形纹理坐标 `[0,1]` 覆盖整个 widget，但语谱图渲染在带边距的子区域内（`glScissor` 裁剪）。当 `m_leftMargin > 0` 时，同一像素位置的时间标签与 GPU 纹理采样坐标不一致，导致缩放时漂移。

**修复**：`spectrogrampanel_enhanced.cpp` — 将 `xMin`/`xMax` 扩展以补偿边距：
```cpp
qreal V = m_viewXMax - m_viewXMin;
qreal H = width() - m_leftMargin - m_rightMargin;
float xNormMin = (m_viewXMin - m_leftMargin * V / H) / totalDurationMs;
float xNormMax = (m_viewXMax + m_rightMargin * V / H) / totalDurationMs;
```

### 二、图表汉化

`chartpanel.cpp`、`spectrogrampanel.cpp`、`spectrogrampanel_enhanced.cpp` 中 6 处英文文本改为 `lang()` 双语：
- Y 轴标题：`"Brightness (Y avg)"` → `lang("亮度 (Y均值)", ...)`
- 图例名：`"Region %1"` → `lang("区域 %1", ...)`、`"Volume"` → `lang("音量", ...)`
- 音量轴标题：`"Volume (dB)"` → `lang("音量 (dB)", ...)`
- 工具提示：`"Time: %1 Freq: %2 Value: %3"` → `lang("时间: %1 频率: %2 值: %3", ...)`

### 三、ChartPanel 焦点策略

`chartpanel.cpp` — 添加 `setFocusPolicy(Qt::NoFocus)`，防止图表窃取键盘焦点导致全局快捷键失灵。

### 四、音频分析持久化修复

**问题**：仅做音频分析（无亮度分析）时，切到其他视频再切回，音频结果丢失。

**根因**：三处"空判断"只检查 `timestamps`，忽略 `audio` 字段：
- `AnalysisSnapshot::isEmpty()` — 回滚，保持只查 `timestamps`（保护 15+ 处调用者）
- `VideoState::hasData()` — 增加 `snapshot.hasAudio()` 检查
- `TimelineModel::saveToFile()` — 增加 `!m_snapshot.hasAudio()` 条件

**修复**：
| 文件 | 改动 |
|------|------|
| `videostatemanager.h:24` | `hasData()` 追加 `\|\| snapshot.hasAudio()` |
| `timeline_model.cpp:87` | `saveToFile()` 空检查追加 `&& !m_snapshot.hasAudio()` |

### 五、Slider 快捷键修复

**问题**：操作底噪/降噪滑块后，全局快捷键被 Slider 拦截。

**根因**：Qt 的 `installEventFilter(this)` 只拦截 MainWindow 自身事件，不拦截子控件（如 QSlider）的事件。Slider 的内置 `keyPressEvent` 消费方向键。

**修复**：`mainwindow.cpp` — 两个滑块创建后安装事件过滤器：
```cpp
m_noiseFloorSlider->installEventFilter(this);
m_noiseReductionSlider->installEventFilter(this);
```

### 六、语谱图 .vla.spec 加载诊断

`timeline_model.cpp` 和 `mainwindow.cpp` 中添加 `qDebug()` 诊断日志，用于排查跨 session 加载音频分析结果的问题。

### 七、代码变更统计

| 文件 | 主要变更 |
|------|----------|
| `chartpanel.cpp` | `setFocusPolicy(Qt::NoFocus)`、6 处 `lang()` 汉化 |
| `chartpanel.h` | 无变更 |
| `spectrogrampanel.cpp` | 工具提示 `lang()` 汉化 |
| `spectrogrampanel_enhanced.cpp` | 缩放漂移修复（边距补偿公式）、工具提示 `lang()` 汉化、`setSpectrogramData()` 诊断日志 |
| `mainwindow.cpp` | 删除 QSlider 方向键保护逻辑、Slider 安装 `installEventFilter`、`restoreAnalysisState()` 诊断日志 |
| `mainwindow.h` | 无变更 |
| `videostatemanager.h` | `hasData()` 增加 `snapshot.hasAudio()` |
| `timeline_model.cpp` | `saveToFile()` 空检查增加 `!m_snapshot.hasAudio()`、`loadFromFile()` 诊断日志 |
| `analysis_snapshot.h` | `isEmpty()` 回滚为只查 `timestamps` |

### 八、全局快捷键修复 — QListWidget 事件过滤

**问题**：视频列表点击后，空格和上下快捷键被 QListWidget 消费。

**根因**：`VideoListPanel::m_listWidget`（QListWidget）默认 `StrongFocus`，点击后获得焦点，其内置 `keyPressEvent` 消费方向键和空格键。Qt 的 `installEventFilter(this)` 只拦截 MainWindow 自身事件，不拦截子控件事件。

**方法**：
1. 在 `videolistpanel.h` 添加 `QListWidget *listWidget() const` public accessor
2. 在 `mainwindow.cpp` 中 `m_videoListPanel->listWidget()->installEventFilter(this)`
3. 同时在 `mainwindow.cpp` 添加 `#include <QListWidget>`

**原理**：Qt 的事件过滤机制：当 `filterObj->installEventFilter(targetObj)` 时，`targetObj` 的事件会先经过 `filterObj->eventFilter()`。安装 eventFilter 到 QListWidget 后，其 KeyPress 事件先被 MainWindow 的 eventFilter 拦截，全局快捷键优先处理。

**当前 eventFilter 安装清单（6 个对象）**：

| 对象 | 作用 | 安装位置 |
|------|------|----------|
| overlay | 视频覆盖层 | `setupConnections()` |
| menuBar() | 菜单栏 | `setupConnections()` |
| this (MainWindow) | 主窗口自身 | `setupConnections()` |
| m_noiseFloorSlider | 底噪滑块 | 语谱图标题栏创建后 |
| m_noiseReductionSlider | 降噪滑块 | 语谱图标题栏创建后 |
| m_listWidget | 视频列表 | `setupConnections()` |

**代价**：QListWidget 不再响应键盘上下键导航（可鼠标点击选择）。

---

### 九、音频降噪功能增强

**问题**：降噪效果不明显，"应用"按钮只能点击一次。

#### 9.1 降噪算法增强（`analyze_video.py:reduce_noise_spectral`）

| 参数 | 修改前 | 修改后 | 原理 |
|------|--------|--------|------|
| 噪声估计 | `np.percentile(magnitude, 10, axis=1)` | `np.percentile(magnitude, 20, axis=1)` | 更高的百分位数 → 更准确的噪声基底估计 |
| 强度缩放 | `noise_profile * strength` | `noise_profile * (1 + strength * 0.5)` | 非线性缩放，strength=5.0 时阈值为噪声基底的 3.5 倍 |
| 掩码下限 | `np.maximum(mask, 0.05)` | `np.maximum(mask, 0.01)` | 允许更深度的噪声抑制（保留 1% 信号 vs 5%） |

**效果对比**（strength=1.0）：
- 修改前：阈值 = 噪声 × 1.0，掩码下限 5% → 几乎无感知
- 修改后：阈值 = 噪声 × 1.5，掩码下限 1% → 明显降噪
- strength=5.0 时：阈值 = 噪声 × 3.5，掩码下限 1% → 强力降噪

#### 9.2 "应用"按钮可靠性（`mainwindow.cpp/.h`）

**问题**：按钮通过 `findChildren<QPushButton*>()` 按文本查找连接，首次点击后可能因 UI 更新导致查找失败。

**修复**：
- `nrApplyBtn` 从局部变量改为成员变量 `m_nrApplyBtn`（`mainwindow.h`）
- 删除 `findChildren` 查找逻辑，直接 `connect(m_nrApplyBtn, &QPushButton::clicked, ...)`
- 添加 `setFocusPolicy(Qt::NoFocus)`，防止空格键被按钮拦截

#### 9.3 滑块范围扩展

- 修改前：`setRange(0, 20)`（对应 0.0-2.0，步进 0.1）
- 修改后：`setRange(0, 50)`（对应 0.0-5.0，步进 0.1）

#### 9.4 代码变更统计

| 文件 | 主要变更 |
|------|----------|
| `analyze_video.py` | `reduce_noise_spectral()` 算法增强（20th percentile + 非线性缩放 + 掩码下限 0.01） |
| `mainwindow.h` | 新增 `m_nrApplyBtn` 成员变量 |
| `mainwindow.cpp` | `m_nrApplyBtn` 改为成员变量直接连接、`setFocusPolicy(Qt::NoFocus)`、滑块范围 0-50 |

---

## 2026-06-28：v0.5 新功能开发 — 多边形ROI + 辅助线 + 跨视频复制粘贴

### 一、新增功能概述

| 功能 | 状态 | 说明 |
|------|------|------|
| 多边形 ROI | ⚠️ 部分完成 | 可绘制多边形点，但双击闭合和亮度分析未完成 |
| 辅助线工具 | ⚠️ 部分完成 | 可绘制/选中/平移/端点拖拽/悬停高亮，但删除后交互有 bug |
| 跨视频 ROI 复制粘贴 | ⚠️ 部分完成 | 可复制粘贴，但亮度曲线被错误共享 |
| 右键直接删除 | ✅ 完成 | 右键点击即可删除任意 ROI/辅助线，无需先选中 |
| 右键菜单简化 | ✅ 完成 | 移除了右键菜单中的 A/B 点设置选项 |

### 二、新文件

| 文件 | 说明 |
|------|------|
| `src/domain/region_shape.h` | RegionShape 类型定义（Rect/Polygon 联合体，当前未使用） |
| `src/domain/polygon_model.h/cpp` | PolygonModel：线程安全的多边形 ROI 管理 |
| `src/domain/guide_line.h` | GuideLine 数据结构（start, end, color） |
| `src/domain/guide_line_model.h/cpp` | GuideLineModel：线程安全的辅助线管理 |

### 三、修改文件

| 文件 | 主要变更 |
|------|----------|
| `src/videowidget.h` | OverlayWidget 新增：DragMode 枚举（MoveGuideLine/ResizeGuideEndpoint）、多边形/辅助线相关成员、hitTest 方法、QTimer |
| `src/videowidget.cpp` | **重大修改**（~1200行）：多边形绘制交互、辅助线绘制/选中/平移/端点拖拽/悬停高亮、右键命中检测删除、坐标映射 |
| `src/mainwindow.h` | 新增 PolygonModel/GuideLineModel 实例、工具栏按钮（Rect/Polygon/Guide/CopyROI/PasteROI）、信号槽连接 |
| `src/mainwindow.cpp` | 模式切换逻辑、复制粘贴 ROI（含替换/追加选择对话框）、快捷键 P/G 处理 |
| `src/magnifierwidget.h/cpp` | 新增 `setGuideLineModel()` 支持放大镜内显示辅助线 |
| `analyze_video.py` | 新增多边形 ROI 的亮度分析（cv2.fillPoly 掩码） |
| `CMakeLists.txt` | 新增 3 个 .cpp 和 6 个 .h 文件 |

### 四、辅助线交互实现详情

#### 4.1 交互模式

| 操作 | 行为 |
|------|------|
| G 键 | 切换辅助线模式 |
| 左键点击空白处 | 开始画新线（拖拽确定终点） |
| 左键点击端点 | 拖动端点（Shift 约束水平/垂直） |
| 左键点击线体 | 平移整条线 |
| 右键点击 | 直接删除（无需先选中） |
| ESC | 退出辅助线模式 |
| Shift+拖拽 | 水平/垂直约束 |

#### 4.2 悬停高亮

- 鼠标靠近端点（6px）→ `SizeAllCursor`，端点手柄显示
- 鼠标靠近线体（5px）→ `OpenHandCursor`，线段加粗+变亮
- 拖拽中 → `ClosedHandCursor`

#### 4.3 实现方式

- `hitTestGuideEndpoint()`：遍历所有线段的两个端点，6px 半径检测
- `hitTestGuideLine()`：点到线段距离公式，5px 阈值
- `m_hoveredGuideLine`：悬停状态跟踪，`drawGuideLines()` 中根据状态绘制不同样式
- `m_dragOriginalLine`：拖拽前保存原始线段，用于计算 delta

### 五、多边形交互实现详情（未完成）

#### 5.1 已实现

- P 键切换多边形模式
- 单击添加顶点，实时显示点和预览连线
- 双击闭合多边形（timer 200ms 区分单击/双击）
- ESC 退出、右键取消绘制
- 右键直接删除已创建的多边形

#### 5.2 待修复（见第七节）

### 六、跨视频 ROI 复制粘贴实现详情（有 bug）

#### 6.1 已实现

- 工具栏"复制ROI"按钮 → 复制所有矩形+多边形到剪贴板
- 工具栏"粘贴ROI"按钮 → 弹出替换/追加选择对话框
- 快捷键支持

#### 6.2 待修复（见第七节）

### 七、已知问题（待下次解决）

#### 问题 1：辅助线删除后需额外点击才能恢复交互

**现象**：右键删除辅助线后，第一次左键/右键无法立即执行创建或删除操作，需要再点击一次才能"唤醒"。

**已尝试的修复**：
- 在 `DrawGuideLine` 分支加入 `m_currentMousePos = pos` 立即更新预览位置
- 右键删除后重置 `m_selectedGuideLine = -1`、`m_hoveredGuideLine = -1`

**可能的根因方向**（待排查）：
1. 右键删除后焦点丢失（overlay 失去焦点，需要额外点击重新获取）
2. 右键事件处理后内部状态未完全清理
3. 可能需要在删除后主动 `setFocus()` 或检查焦点策略

#### 问题 2：复制粘贴 ROI 后亮度曲线被错误共享

**现象**：在 B 视频分析的亮度曲线也会错误地出现在 A 视频中（共享了数据）。

**可能的根因方向**（待排查）：
1. `RegionModel` 或 `PolygonModel` 在视频切换时未正确隔离
2. `VideoState` 中的分析数据在复制粘贴后未正确绑定到对应视频
3. `TimelineModel` 的 `m_snapshot` 可能在多视频间共享了引用

#### 问题 3：多边形双击闭合不稳定

**现象**：双击有时无法闭合多边形，需要三击或四击。

**当前实现**：使用 QTimer（200ms）区分单击和双击：
- 单击 → 启动 timer，添加点
- Timer 运行中再次点击 → 双击第二次点击 → 直接完成多边形
- Timer 到期 → 单击完成

**可能的根因方向**（待排查）：
1. Windows 双击事件序列可能与 Qt 的 timer 机制有冲突
2. 双击的两次 press 事件间隔可能超过 200ms
3. `mouseDoubleClickEvent` 可能干扰了 timer 逻辑
4. 可能需要改用 Qt 原生的 `QMouseEvent::isDoubleClick()` 或 `QApplication::doubleClickInterval()`

#### 问题 4：多边形不参与亮度分析

**现象**：创建的多边形没有走 ROI 的亮度分析逻辑，无法进行亮度分析。

**当前状态**：
- `analyze_video.py` 中已实现 `cv2.fillPoly` 掩码计算
- `PolygonModel` 已实现多边形存储
- 但分析流程中未将多边形传递给 Python 脚本

**可能的根因方向**（待排查）：
1. `PythonAnalysisEngine` 启动分析时未传递多边形数据
2. `analyze_video.py` 中多边形参数未正确解析
3. 多边形坐标格式与 Python 端期望的格式不匹配

### 八、构建环境变更

CMakeCache.txt 从 v0.35 复制导致源代码路径错误（`CMAKE_HOME_DIRECTORY` 指向 v0.35）。已删除旧缓存并重新生成，正确指向 v0.5。

**正确的构建命令**：
```powershell
cd C:\code\LumenArc\LumenArc_v0.5
Remove-Item build\CMakeCache.txt -Force
Remove-Item build\CMakeFiles -Recurse -Force
& "C:\cmake-temp\CMake\bin\cmake.exe" -B build -S . -G "Visual Studio 17 2022" -A x64 -DCMAKE_PREFIX_PATH="C:/code/Qt/6.8.0/msvc2022_64"
& "C:\cmake-temp\CMake\bin\cmake.exe" --build build --config Release
```

### 九、代码变更统计

| 文件 | 变更行数（约） |
|------|----------------|
| `videowidget.h` | +20 行（新成员、新枚举、新方法声明） |
| `videowidget.cpp` | +400 行（多边形交互、辅助线交互、右键删除、悬停高亮） |
| `mainwindow.h` | +15 行（新成员、新按钮） |
| `mainwindow.cpp` | +100 行（模式切换、复制粘贴、快捷键） |
| `magnifierwidget.h` | +1 行（setGuideLineModel 声明） |
| `magnifierwidget.cpp` | +5 行（setGuideLineModel 实现） |
| `analyze_video.py` | +30 行（多边形 ROI 掩码计算） |
| `CMakeLists.txt` | +9 行（新文件列表） |
| 新文件 4 个 | ~200 行 |

---

## 2026-06-29：v0.5 优化 — 多边形分析集成 + 交互完善 + 持久化

### 一、完成的优化项

| # | 任务 | 优先级 | 说明 |
|---|------|--------|------|
| 1 | 多边形亮度分析集成 | P0 | 修改 IAnalysisEngine 接口接受 QVector&lt;QPolygon&gt;，Python 脚本已有 cv2.fillPoly 支持 |
| 2 | 多边形双击闭合修复 | P0 | 改用 QApplication::doubleClickInterval() 替代硬编码 200ms，闭合后保持多边形模式 |
| 3 | 多边形顶点编辑 | P1 | 新增 MovePolygon/ResizePolygonVertex 拖拽模式，支持整体移动和顶点拖拽 |
| 4 | 多边形/辅助线持久化 | P1 | .vla v5 格式（新增 polygons 字段），VideoStateManager 保存/恢复多边形和辅助线 |
| 5 | 复制粘贴 ROI 数据隔离 | P1 | 粘贴时检测已有分析数据并弹出失效确认，复制/粘贴辅助线，粘贴后立即刷新 overlay |
| 6 | 辅助线删除后交互恢复 | P1 | 右键删除后重置 m_dragMode + setFocus()，矩形/多边形删除同步处理 |
| 7 | 辅助线复制粘贴 | P2 | 已在 P1-5 中一并完成 |
| 8 | 版本号统一 | P2 | CMakeLists.txt/aboutdialog/mainwindow/main.cpp/app.rc/Info.plist/README/MANUAL 统一到 v0.5 |

### 二、修改文件清单

| 文件 | 主要变更 |
|------|----------|
| `ianalysis_engine.h` | startAnalysis() 新增 QVector&lt;QPolygon&gt; 参数 |
| `python_analysis_engine.h/cpp` | startAnalysis() 接受多边形，序列化到统一 JSON |
| `mainwindow.h` | 新增 m_guideLineClipboard 成员 |
| `mainwindow.cpp` | onAnalyze() 传递多边形、onPasteRoi() 失效确认+辅助线+刷新、saveState/restoreState 传递多边形和辅助线、saveToFile/loadFromFile 传递多边形、版本号更新 |
| `timeline_model.h/cpp` | saveToFile/loadFromFile 新增多边形参数，版本升级到 v5 |
| `videostatemanager.h/cpp` | VideoState 新增 polygons/guideLines 字段 |
| `videowidget.h` | 新增 DragMode::MovePolygon/ResizePolygonVertex、hitTestPolygonVertex、clampPointToVideo、m_dragOriginalPolygon/m_dragPolygonVertexIndex |
| `videowidget.cpp` | 多边形移动/顶点拖拽交互、双击闭合修复、删除后交互恢复、光标反馈、QApplication::doubleClickInterval() |
| `app.rc` | VERSIONINFO 更新到 0.5.0.0 |
| `CMakeLists.txt` | VERSION 0.5.0 |
| `aboutdialog.cpp` | 版本标签 v0.5 |
| `main.cpp` | 启动画面 v0.5 beta |
| `Info.plist` / `mac_port/Info.plist` | 0.5 beta |
| `README.md` / `MANUAL.md` | 标题更新到 v0.5 |

### 三、.vla v5 格式

```json
{
  "version": 5,
  "regions": [{"x": 100, "y": 200, "w": 300, "h": 150}],
  "polygons": [{"points": [[100,200], [300,200], [200,400]]}],
  "time_offset": 36000000,
  "timestamps": [0, 33, 66, ...],
  "luminances": [[128.5, 130.2, ...]],
  "audio": { ... },
  "labels": [...],
  "magnifier": {...},
  "pinned": {...},
  "snapshot_fusion": {...}
}
```

### 四、构建验证

```
cmake -B build -S . -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
→ LumenArc.exe 编译成功，0 错误
```

---

## 2026-07-01：v0.5 DataEntry 数据索引重构 + 交互优化

### 一、DataEntry 元数据系统

**问题**：`values[]` 数组用位置索引映射 ROI，删除/新增 ROI 后索引错位，导致曲线颜色混乱、数据错位。

**方案**：引入 `DataEntry{type, roiId}` 元数据，通过 roiId 精确匹配 ROI 和数据。

| 文件 | 变更 |
|------|------|
| `region_model.h/cpp` | 自增 roiId 计数器 + `roiIdAt()` + `findIndexByRoiId()` |
| `polygon_model.h/cpp` | 同上 |
| `analysis_snapshot.h` | 新增 `DataEntry` 结构体 + `dataEntries` 成员 |
| `timeline_model.h/cpp` | 新增 `removeRegionDataByRoiId()` + 4 参数 `setData` |
| `python_analysis_engine.h/cpp` | 传递 roiId 到 JSON + 构建 dataEntries |
| `analyze_video.py` | 透传 roi_id |
| `chartpanel.h/cpp` | SeriesMapping + roiId 匹配合并策略 |
| `mainwindow.cpp` | polygonRemoved/polygonAdjustmentFinished 连接 |

### 二、ChartPanel SeriesMapping

**问题**：`rebuildSeries()` 的 `mergedEntries` 按 model 顺序排列，但 `values[]` 按旧 dataEntries 顺序排列，导致新画的矩形"抢"了多边形的数据。

**方案**：`SeriesMapping` 结构体记录每个 series 对应的 `dataIndex`（在 values[] 中的正确位置），`onDataReplaced()` 用 `dataIndex` 访问数据。

### 三、交互优化

| 功能 | 说明 |
|------|------|
| 曲线线宽 | 从 2 减为 1（减细 30%） |
| 切换视频清除 | openVideoFile 无缓存路径添加 clearPolygons/clearLines |
| 快捷键速查 | 帮助菜单 → 25% 透明弹窗，深色主题表格布局 |
| 模式悬浮提示 | 多边形/辅助线按钮 tooltip 显示详细操作说明 |

### 四、已知遗留

| 项目 | 说明 |
|------|------|
| cursor tooltip 索引 | `updateCursorPosition` 仍用 `snap.values[i]` 直接访问，可能与 series 顺序不一致 |
| RegionShape 统一类型 | rect/polygon 仍由独立模型管理，未统一 |
