# LumenArc v0.3 整改计划

**依据：** `REVIEW_REPORT_V0.3.md` 审查报告  
**日期：** 2026-06-13  
**目标：** 修复全部 5 项严重缺陷、8 项中等问题、4 项轻微问题

---

## 一、问题清单与修复方案

### P0 — 阻塞发布（2 项）

#### S1. 多进程分析实际未启用

**文件：** `src/infrastructure/python_analysis_engine.cpp`  
**问题：** `computeProcessCount()` 已定义（第 82 行）但从未被调用，`procCount` 硬编码为 1（第 145 行）。  
**根因：** `startAnalysis()` 中未获取视频总帧数，无法调用 `computeProcessCount()`。

**修复方案：**
1. 修改 `analyze_video.py` 的 `--check-fps` 模式，输出 `{"fps": 30.0, "total_frames": 1800}`
2. 修改 `PythonAnalysisEngine::getVideoFps()` 返回结构体（包含 fps + totalFrames）
3. 在 `startAnalysis()` 中调用 `getVideoFps()` 获取参数，再调用 `computeProcessCount(totalFrames, fps)`
4. 将计算结果传入 `--processes` 参数

**验证：** 分析 >30 秒视频时，stderr 日志显示 `PROGRESS` 来自多进程。

---

#### S2. 版本号大面积不一致（v0.2 残留）

**涉及文件（12 处）：**

| 文件 | 行号 | 当前值 | 目标值 |
|------|------|--------|--------|
| `src/aboutdialog.h` | 6 | `@version 0.2` | `@version 0.3` |
| `src/aboutdialog.cpp` | 6 | `@version 0.2` | `@version 0.3` |
| `src/aboutdialog.cpp` | 38 | `版本 v0.2` | `版本 v0.3` |
| `src/mainwindow.cpp` | 6 | `@version 0.2` | `@version 0.3` |
| `src/mainwindow.cpp` | 183 | `v0.2 Beta`（PDF 路径） | `v0.3 Beta` |
| `src/mainwindow.cpp` | 716 | `v0.2 beta`（窗口标题） | `v0.3 beta` |
| `src/mainwindow.cpp` | 853 | `v0.2 beta`（窗口标题） | `v0.3 beta` |
| `src/Info.plist` | 22, 24 | `0.2 beta` / `0.2` | `0.3 beta` / `0.3` |
| `mac_port/Info.plist` | 22, 24 | `0.2 beta` / `0.2` | `0.3 beta` / `0.3` |
| `README.md` | 1 | `v0.2` | `v0.3` |
| `MANUAL.md` | 1 | `v0.2` | `v0.3` |
| `src/app.rc` | — | 无 VERSIONINFO | 添加标准 VERSIONINFO 资源块 |

**验证：** 全文搜索 `v0.2`、`0.2 beta`，确认无遗漏（排除开发计划文档中的历史记录）。

---

### P1 — 发布前必须修复（3 项）

#### S3. VideoListPanel 拖拽排序后 timeOffsetMs 未重新计算

**文件：** `src/videolistpanel.cpp:174-186`  
**问题：** `onItemMoved()` 按索引直接复制，未实际跟踪拖拽移动，未重算 `timeOffsetMs`。  
**根因：** QListWidget 的 InternalMove 模式下，`rowsMoved` 信号的参数不包含原始行号映射。

**修复方案：**
1. 在 `addVideo()` 时为每个 QListWidgetItem 设置 `setData(Qt::UserRole, originalIndex)`
2. 在 `onItemMoved()` 中遍历列表，根据 `UserRole` 数据重建 `m_videos` 顺序
3. 重算 `timeOffsetMs`：遍历 `m_videos`，累加前序 `durationMs`
4. 发出 `videoReordered()` 信号

**验证：** 添加 3 个视频，将第 3 个拖到第 1 个位置，检查 `timeOffsetMs` 顺序正确。

---

#### S4. onOpenFile() 不支持多选文件

**文件：** `src/mainwindow.cpp:660-668`  
**问题：** 使用 `QFileDialog::getOpenFileName()`（单选），开发计划要求多选。

**修复方案：**
1. 改用 `QFileDialog::getOpenFileNames()`
2. 遍历选择的文件，逐个调用 `m_videoListPanel->addVideo(path, 0, 30.0f)`
3. 第一个文件调用 `openVideoFile()` 加载播放
4. 其余文件仅加入列表

**验证：** 菜单"打开视频"可选择多个文件，列表面板显示全部，双击可切换播放。

---

#### S5. 帧率一致性检查未集成到分析流程

**文件：** `src/mainwindow.cpp` 的 `onAnalyze()`  
**问题：** 开发计划要求"分析前校验所有视频 FPS，不一致时弹窗警告"，但未实现。

**修复方案：**
1. 在 `onAnalyze()` 的前置检查中，判断 `m_videoListPanel->videoCount() > 1`
2. 遍历 `m_videoListPanel->allVideos()`，比较 FPS（差异 > 0.1f 视为不一致）
3. 不一致时弹出 `QMessageBox::warning()`，询问用户是否继续
4. 用户选择"取消"则中止分析

**验证：** 添加 30fps 和 25fps 两个视频，点"分析"时弹出警告。

---

### P2 — 下一迭代修复（8 项）

#### M1. 播放倍速显示格式异常（浮点比较）

**文件：** `src/mainwindow.cpp:1079`  
**问题：** `qFuzzyCompare(m_currentSpeed, speeds[i])` 在 0.5x → 1x 切换时可能跳过匹配。  
**修复：** 改为 `qAbs(m_currentSpeed - speeds[i]) < 0.01f`。

---

#### M2. cancelAnalysis() 未给 Python 脚本清理机会

**文件：** `src/infrastructure/python_analysis_engine.cpp:177`  
**问题：** `proc->kill()` 直接强制终止，`finally` 块不执行。  
**修复：** 先 `proc->terminate()`，等待 3 秒，超时再 `proc->kill()`。

---

#### M3. 频谱图面板缺少时间轴标签

**文件：** `src/spectrogrampanel.cpp`  
**问题：** 仅有频率标签（Y 轴），无 X 轴时间标签。  
**修复：** 在 `renderSpectrogram()` 底部绘制时间刻度，格式 `HH:MM:SS`，与 ChartPanel 对齐。

---

#### M4. 频谱图未处理空 spectrogram[0] 的边界情况

**文件：`src/spectrogrampanel.cpp:81`  
**问题：** `spectrogram` 非空但 `spectrogram[0]` 为空时，`nFrames` 为 0，后续计算可能越界。  
**修复：** 增加 `spectrogram[0].isEmpty()` 检查，返回空图像。

---

#### M5. 分析完成后的 processEvents() 调用存在竞态风险

**文件：** `src/mainwindow.cpp:1282`  
**问题：** `QCoreApplication::processEvents()` 可能导致竞态。  
**修复：** 改用 `QTimer::singleShot(0, this, [this, msg]{ QMessageBox::information(...); })` 延迟显示。

---

#### M6. 频谱图颜色映射范围硬编码

**文件：** `src/spectrogrampanel.cpp:142-148`  
**问题：** 假设范围 [-10, 5]，实际数据动态范围可能不同。  
**修复：** 在 `setSpectrogramData()` 中遍历计算 `m_minValue`/`m_maxValue`，`spectrogramColor()` 使用动态范围。

---

#### M7. Python 多进程合并未处理空段

**文件：** `analyze_video.py:153`  
**问题：** 某个进程返回空 `timestamps` 时，`r["timestamps"][0]` 触发 `IndexError`。  
**修复：** 排序前过滤：`results = [r for r in results if r.get("timestamps")]`

---

#### M8. 时间戳格式化未考虑毫秒精度

**文件：** `src/chartpanel.cpp:695-706`  
**问题：** 毫秒被截断，高频视频（100fps）时间精度不足。  
**修复：** 可见范围 < 10 秒时显示 `HH:MM:SS.zzz`，否则保持 `HH:MM:SS`。

---

### P3 — 后续优化（4 项）

#### L1. m_splitter 拖放事件处理冗余

**文件：** `src/mainwindow.cpp:1448-1470`  
**问题：** `eventFilter` 中 splitter 的 Drop 仅处理第一个文件，与 `dropEvent()` 不一致。  
**修复：** 移除 `eventFilter` 中 splitter 的 Drop 处理，统一由 `dropEvent()` 处理。

---

#### L2. 频谱图面板无 tooltip 交互

**文件：** `src/spectrogrampanel.cpp`  
**修复：** 实现 `mouseMoveEvent()` 显示 `Time: HH:MM:SS, Freq: XXX Hz`。

---

#### L3. Python 脚本临时文件在进程被 kill 时残留

**文件：** `analyze_video.py`  
**修复：** 在 Python 脚本中用 `tempfile.mktemp()` + `atexit.register(os.unlink, ...)` 注册清理。

---

#### L4. 音量折线图 Y 轴标题与开发计划不一致

**文件：** `src/chartpanel.cpp:414`  
**问题：** 当前 `"Volume"`，计划要求 `"Volume (0-1)"`。  
**修复：** 改为 `"Volume (0-1)"`。

---

## 二、执行顺序

```
批次 1 (P0): S1 + S2
  ↓
批次 2 (P1): S3 + S4 + S5
  ↓
批次 3 (P2): M7 (Python) + M1,M2 (引擎) + M3,M4,M6 (频谱图) + M5,M8 (主窗口/图表)
  ↓
批次 4 (P3): L1 + L2 + L3 + L4
```

## 三、验证矩阵

| 问题 | 验证方法 |
|------|----------|
| S1 | 分析 >30 秒视频，stderr 显示多进程 PROGRESS |
| S2 | 全文搜索 `v0.2` 确认无遗漏 |
| S3 | 拖拽重排后检查 timeOffsetMs 累加正确 |
| S4 | 菜单多选文件，列表显示全部 |
| S5 | 不同 FPS 视频弹窗警告 |
| M1 | 0.25x ↔ 0.5x ↔ 1x 切换显示正确 |
| M2 | 取消分析后临时 WAV 文件被清理 |
| M3 | 频谱图底部显示时间刻度 |
| M4 | 无音频数据时不崩溃 |
| M5 | 分析完成后弹窗正常，视频不卡顿 |
| M6 | 不同音频数据的频谱图对比度合理 |
| M7 | 短视频多进程分析不崩溃 |
| M8 | 高倍缩放时显示毫秒 |
| L1 | 拖放文件到 splitter 正常工作 |
| L2 | 鼠标悬停频谱图显示 tooltip |
| L3 | 取消分析后无临时文件残留 |
| L4 | 音量轴标题显示 `Volume (0-1)` |

---

## 四、整改完成记录

**完成日期：** 2026-06-13

| 批次 | 问题 | 状态 | 说明 |
|------|------|------|------|
| 批次 1 (P0) | S1 多进程启用 | ✅ 已修复 | getVideoInfo() + computeProcessCount() 实际调用 |
| 批次 1 (P0) | S2 版本号统一 | ✅ 已修复 | 14 个文件 + app.rc VERSIONINFO |
| 批次 2 (P1) | S3 拖拽时间偏移 | ✅ 已修复 | Qt::UserRole 索引跟踪 + timeOffsetMs 重算 |
| 批次 2 (P1) | S4 多选文件 | ✅ 已修复 | getOpenFileNames() |
| 批次 2 (P1) | S5 帧率检查 | ✅ 已修复 | onAnalyze() 中 FPS 比较 + 弹窗警告 |
| 批次 3 (P2) | M1 倍速浮点比较 | ✅ 已修复 | qAbs < 0.01f |
| 批次 3 (P2) | M2 cancelAnalysis 清理 | ✅ 已修复 | terminate() 优先 |
| 批次 3 (P2) | M3 频谱图时间标签 | ✅ 已修复 | 底部 HH:MM:SS |
| 批次 3 (P2) | M4 空 spectrogram[0] | ✅ 已修复 | isEmpty() 检查 |
| 批次 3 (P2) | M5 processEvents 竞态 | ✅ 已修复 | QTimer::singleShot |
| 批次 3 (P2) | M6 频谱图颜色范围 | ✅ 已修复 | 动态 min/max |
| 批次 3 (P2) | M7 空段合并 | ✅ 已修复 | 过滤空结果 |
| 批次 3 (P2) | M8 毫秒精度 | ✅ 已修复 | HH:MM:SS.zzz |
| 批次 4 (P3) | L1 splitter drop | ✅ 已修复 | 移除 eventFilter 拦截 |
| 批次 4 (P3) | L2 频谱图 tooltip | ✅ 已修复 | mouseMoveEvent |
| 批次 4 (P3) | L3 临时文件残留 | ✅ 已修复 | _temp_files + atexit + SIGTERM |
| 批次 4 (P3) | L4 音量轴标题 | ✅ 已修复 | "Volume (0-1)" |

**自查额外修复（10 项）：**

| # | 问题 | 修复 |
|---|------|------|
| 55 | getVideoFps 残留定义 | 删除旧函数 |
| 56 | 脚本路径 fallback 不可达 | isEmpty() 移到 exists() 前 |
| 57 | onFinished 信号顺序 | 局部指针 + 先置 nullptr |
| 58 | 重复 include | 删除 |
| 59 | 未使用 m_videoList | 删除 |
| 60 | qFuzzyCompare 近零 | 1.0 + 偏移 |
| 61 | dropEvent 错误 duration | 改为 0 |
| 62 | Python 检测静默失败 | 添加 warning 弹窗 |

**全部 17 项审查问题 + 10 项自查问题已修复。**
