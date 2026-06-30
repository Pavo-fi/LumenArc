# LumenArc v0.3 升级验证审查报告

**审查日期：** 2026-06-13  
**审查范围：** DEVELOPMENT_PLAN_V0.3.md 定义的 8 个开发阶段全部内容  
**审查方法：** 逐行对比开发计划与实际代码实现，覆盖 17 个修改文件 + 4 个新建文件  
**审查结论：** 核心功能基本完成，但存在 **5 项严重缺陷**、**8 项中等问题**、**4 项轻微问题**  
**整改状态：** ✅ **全部 17 项审查问题已修复**（2026-06-13），另自查修复 10 项  
**整改记录：** 见 `FIX_PLAN_V0.3.md` 第四节

---

## 一、总体评估

v0.3 升级在架构层面完成度较高，核心功能（Python 多进程分析引擎、音频数据结构、频谱图、音量折线图、视频列表面板、后台异步分析）均已实现。但在**生产环境鲁棒性**方面存在若干缺陷，其中多进程分析硬编码为单进程、版本号大面积不一致等问题可能影响发布质量。

---

## 二、开发计划完成度矩阵

| 阶段 | 开发计划要求 | 实现状态 | 问题编号 |
|------|------------|---------|---------|
| 阶段0 | 版本号更新为 0.3.0 | ⚠️ 部分完成 | S2 |
| 阶段1 | Python 多进程/音频分析脚本重构 | ✅ 完成 | — |
| 阶段2 | C++ AudioData 数据结构扩展 | ✅ 完成 | — |
| 阶段3 | 分析引擎多进程 + ffmpeg 路径 | ❌ 进程数硬编码 | S1 |
| 阶段4 | 多视频管理 VideoListPanel | ⚠️ 部分完成 | S3, S4, S5 |
| 阶段5 | 后台异步分析（UI 不阻塞） | ⚠️ 基本完成 | M5 |
| 阶段6 | 频谱图显示 SpectrogramPanel | ⚠️ 基本完成 | M3, M4, M6 |
| 阶段7 | 音量折线图显示 | ✅ 完成 | L4 |
| 阶段8 | 版本更新 + ffmpeg 打包 | ⚠️ 部分完成 | S2 |

---

## 三、严重问题（5 项 — 必须修复）

### S1. 多进程分析实际未启用

**文件：** `src/infrastructure/python_analysis_engine.cpp:145`

```cpp
int procCount = 1;  // Default single process for safety
args << "--processes" << QString::number(procCount);
```

进程数硬编码为 `1`。`computeProcessCount()` 函数（第 82 行）已正确定义但从未被调用。v0.3 核心卖点"多进程提速"未实际生效。

**整改意见：** 在 `startAnalysis()` 中先通过 `getVideoFps()` 获取视频帧数和 FPS，再调用 `computeProcessCount(totalFrames, fps)` 计算实际进程数。

---

### S2. 版本号大面积不一致（v0.2 残留）

以下文件中的版本号仍为 `v0.2` / `0.2`：

| 文件 | 行号 | 当前值 | 应改为 |
|------|------|--------|--------|
| `src/aboutdialog.cpp` | 38 | `版本 v0.2` | `版本 v0.3` |
| `src/mainwindow.cpp` | 6 | `@version 0.2` | `@version 0.3` |
| `src/mainwindow.cpp` | 183 | `v0.2 Beta`（PDF 路径） | `v0.3 Beta` |
| `src/mainwindow.cpp` | 716 | `Lumen Arc v0.2 beta` | `Lumen Arc v0.3 beta` |
| `src/mainwindow.cpp` | 853 | `Lumen Arc v0.2 beta` | `Lumen Arc v0.3 beta` |
| `src/aboutdialog.h` | 6 | `@version 0.2` | `@version 0.3` |
| `src/aboutdialog.cpp` | 6 | `@version 0.2` | `@version 0.3` |
| `src/app.rc` | — | 无 VERSIONINFO 资源块 | 需添加 |
| `src/Info.plist` | 22, 24 | `0.2 beta` / `0.2` | `0.3 beta` / `0.3` |
| `mac_port/Info.plist` | 22, 24 | `0.2 beta` / `0.2` | `0.3 beta` / `0.3` |
| `README.md` | 1 | `v0.2` | `v0.3` |
| `MANUAL.md` | 1 | `v0.2` | `v0.3` |

**整改意见：** 统一更新所有位置。`app.rc` 需增加标准 Windows `VERSIONINFO` 资源块。

---

### S3. VideoListPanel 拖拽排序后 timeOffsetMs 未重新计算

**文件：** `src/videolistpanel.cpp:174-186`

```cpp
void VideoListPanel::onItemMoved()
{
    QVector<VideoEntry> newOrder;
    for (int i = 0; i < m_listWidget->count(); ++i) {
        if (i < m_videos.size())
            newOrder.append(m_videos[i]);  // 直接按索引复制，未重新计算时间偏移
    }
    m_videos = newOrder;
}
```

拖拽重排后，每个视频的 `timeOffsetMs` 未根据前序视频时长累加更新。多视频合并分析时，时间轴将出现重叠或断裂。

**整改意见：** 在 `onItemMoved()` 中遍历重建 `timeOffsetMs`（累加前序视频 `durationMs`），并发出信号通知 `MainWindow` 更新合并时间轴。

---

### S4. onOpenFile() 不支持多选文件

**文件：** `src/mainwindow.cpp:661-668`

```cpp
void MainWindow::onOpenFile()
{
    QString filePath = QFileDialog::getOpenFileName(this, ...);  // 仅单选
    openVideoFile(filePath);
}
```

开发计划阶段 4 步骤 4.5 明确要求"修改 `onOpenFile()` 支持多选文件"，但实际仍使用 `getOpenFileName()`（单选）。

**整改意见：** 改用 `QFileDialog::getOpenFileNames()`，遍历选择的文件逐个调用 `m_videoListPanel->addVideo()`，并将第一个文件作为当前播放文件。

---

### S5. 帧率一致性检查未集成到分析流程

**文件：** `src/mainwindow.cpp:1192-1233`（`onAnalyze()`）

开发计划阶段 4 步骤 4.7 要求"分析前校验所有视频 FPS，不一致时弹窗警告"。`onAnalyze()` 中未实现多视频帧率一致性检查。当多个视频帧率不同时，时间轴合并将产生时间偏移错误。

**整改意见：** 在 `onAnalyze()` 开始处遍历 `m_videoListPanel->allVideos()`，比较 FPS（差异 > 0.1 时弹窗警告并阻止分析）。

---

## 四、中等问题（8 项 — 应修复）

### M1. 播放倍速显示格式异常

**文件：** `src/mainwindow.cpp:1071-1101`

速度数组为 `float`，`qFuzzyCompare(m_currentSpeed, speeds[i])` 在 0.5x → 1x 切换时可能因浮点精度问题跳过匹配，导致速度按钮显示异常。

**整改意见：** 将速度数组改为 `int` 存储（值 × 100），或统一用格式化输出避免浮点比较。

---

### M2. cancelAnalysis() 未给 Python 脚本清理机会

**文件：** `src/infrastructure/python_analysis_engine.cpp:169-181`

```cpp
proc->kill();                      // 直接强制终止
proc->waitForFinished(2000);       // 仅等待 2 秒
```

`proc->kill()` 在 Windows 上发送 `TerminateProcess`，Python 脚本的 `finally` 块（清理临时 WAV 文件）无法执行。

**整改意见：** 优先发送 `proc->terminate()`（SIGTERM），超时后再 `kill()`。

---

### M3. 频谱图面板缺少时间轴标签

**文件：** `src/spectrogrampanel.cpp`

频谱图面板仅显示频率标签（Y 轴），缺少 X 轴时间标签，用户无法直观判断时间位置。

**整改意见：** 在 `renderSpectrogram()` 底部绘制时间刻度标签，与 ChartPanel 的时间标签对齐。

---

### M4. 频谱图未处理空 spectrogram[0] 的边界情况

**文件：** `src/spectrogrampanel.cpp:81`

```cpp
int nFrames = m_audioData.spectrogram.isEmpty() ? 0 : m_audioData.spectrogram[0].size();
```

当 `spectrogram` 非空但 `spectrogram[0]` 为空时，`nFrames` 为 0，后续 `aIdxMax` 计算可能越界。

**整改意见：** 增加 `spectrogram[0].isEmpty()` 检查。

---

### M5. 分析完成后的 processEvents() 调用存在竞态风险

**文件：** `src/mainwindow.cpp:1282`

```cpp
QCoreApplication::processEvents();
```

开发计划阶段 5 步骤 5.6 要求"分析期间保持视频播放功能"。`processEvents()` 可能导致分析完成事件与用户操作产生竞态。

**整改意见：** 移除 `processEvents()` 调用，改用信号槽异步更新；或确保调用范围最小化。

---

### M6. 频谱图颜色映射范围硬编码

**文件：** `src/spectrogrampanel.cpp:142-148`

```cpp
qreal normalized = qBound(0.0, (value + 10.0) / 15.0, 1.0);
```

假设对数刻度范围为 [-10, 5]，但实际音频数据的动态范围可能不同，导致频谱图对比度不足或饱和。

**整改意见：** 根据实际数据动态计算 min/max 进行归一化，或提供可调节的对比度参数。

---

### M7. Python 多进程合并未处理空段

**文件：** `analyze_video.py:147-153`

```python
def merge_segment_results(results):
    results.sort(key=lambda r: r["timestamps"][0] if r["timestamps"] else 0)
```

如果某个进程返回的 `timestamps` 为空（如视频段过短），排序时 `r["timestamps"][0]` 会触发 `IndexError`。

**整改意见：** 过滤掉空结果段，或在排序前检查 `timestamps` 是否为空。

---

### M8. 时间戳格式化未考虑毫秒精度

**文件：** `src/chartpanel.cpp:695-706`

毫秒级时间戳被截断为秒级，高频分析场景（如 100fps 视频）中时间精度不足。

**整改意见：** 根据视频帧率动态决定是否显示毫秒（如 `HH:MM:SS.mmm`）。

---

## 五、轻微问题（4 项 — 建议修复）

### L1. m_splitter 拖放事件处理冗余

**文件：** `src/mainwindow.cpp:1448-1470`

`eventFilter` 中对 `m_splitter` 的 Drop 事件仅处理第一个文件，与 `dropEvent()` 的多文件处理逻辑不一致。

**整改意见：** 统一在 `dropEvent()` 中处理，`eventFilter` 中的 splitter Drop 可移除或统一为多文件处理。

---

### L2. 频谱图面板无 tooltip 交互

用户无法通过悬停查看具体频率/时间值。

**整改意见：** 实现 `mouseMoveEvent()` 显示 tooltip，显示 `Time: HH:MM:SS, Freq: XXX Hz`。

---

### L3. Python 脚本临时文件在进程被 kill 时残留

**文件：** `analyze_video.py:264-268`

进程被 `kill()` 时 `finally` 块不会执行，临时 WAV 文件可能残留。

**整改意见：** 在 C++ 端 `onFinished()` 中扫描并清理 `QDir::temp()` 下的 `*.wav` 文件。

---

### L4. 音量折线图 Y 轴标题与开发计划不一致

**文件：** `src/chartpanel.cpp:414`

```cpp
m_axisYVolume->setTitleText("Volume");
```

开发计划要求 `"Volume (0-1)"`，当前实现缺少量程标注。

**整改意见：** 改为 `"Volume (0-1)"`。

---

## 六、整改优先级建议

| 优先级 | 问题编号 | 说明 |
|--------|---------|------|
| P0 — 阻塞发布 | S1, S2 | 多进程未启用、版本号不一致 |
| P1 — 发布前必须修复 | S3, S4, S5 | 拖拽时间偏移、多选文件、帧率检查 |
| P2 — 下一迭代修复 | M1-M8 | 中等鲁棒性问题 |
| P3 — 后续优化 | L1-L4 | 轻微体验问题 |

---

## 七、附录：修改文件清单

本次审查涉及的全部文件（共 21 个）：

| 文件 | 操作 | 审查结果 |
|------|------|---------|
| `analyze_video.py` | 重构 | ⚠️ M7 |
| `src/domain/analysis_snapshot.h` | 扩展 | ✅ |
| `src/domain/timeline_model.h` | 扩展 | ✅ |
| `src/domain/timeline_model.cpp` | 扩展 | ✅ |
| `src/infrastructure/python_analysis_engine.h` | 扩展 | ✅ |
| `src/infrastructure/python_analysis_engine.cpp` | 重构 | ❌ S1, ⚠️ M2 |
| `src/mainwindow.h` | 扩展 | ✅ |
| `src/mainwindow.cpp` | 重构 | ❌ S4, S5, ⚠️ M1, M5 |
| `src/chartpanel.h` | 扩展 | ✅ |
| `src/chartpanel.cpp` | 扩展 | ⚠️ M8, L4 |
| `src/videolistpanel.h` | 新建 | ✅ |
| `src/videolistpanel.cpp` | 新建 | ❌ S3 |
| `src/spectrogrampanel.h` | 新建 | ✅ |
| `src/spectrogrampanel.cpp` | 新建 | ⚠️ M3, M4, M6 |
| `src/aboutdialog.h` | 扩展 | ❌ S2 |
| `src/aboutdialog.cpp` | 扩展 | ❌ S2 |
| `src/app.rc` | 修改 | ❌ S2 |
| `src/Info.plist` | 修改 | ❌ S2 |
| `mac_port/Info.plist` | 修改 | ❌ S2 |
| `CMakeLists.txt` | 修改 | ✅ |
| `README.md` | 修改 | ❌ S2 |
| `MANUAL.md` | 修改 | ❌ S2 |
