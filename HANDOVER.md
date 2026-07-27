# LumenArc v1.2 工作交接文档

> 编制日期：2026-07-27
> 最新标签：`v1.2.0-proxy`
> 提交历史：`77c8e44`（v1.0 基线）→ `abd7b11`（N0）→ `ff4c9a4`（N1）→ `adb429b`（N2）→ `5bbb59b`（N3）→ `6b61a8d`（N4）→ `25efc66`（F-A）→ `d978496`（F-C）→ `f496680`（F-B'）→ `4447ca7`（F-D1）→ `66b2b8f`（F-D2）→ `0306427`（D3D11VA fix）→ `e812e0c`（dGPU selection）→ `f073804`（proxy chain）

---

## 一、项目概况

**LumenArc（追光者）** 是面向火灾调查的视频分析桌面工具，核心功能：框选 ROI 逐帧亮度量化分析、独立音频分析（音量曲线 + 语谱图）、截图叠加、A/B 循环、辅助线标注。取证工具第一原则：**结果可信**——界面显示、时间轴、导出数据三者永远一致。

**技术栈**：C++17 / Qt 6.8（Widgets + Charts + Multimedia）/ FFmpeg lgpl-shared 8.x / VLC 3.0 / Python 3.12 嵌入式 / CMake

**目标机器**：Win10 核显办公机（Intel 6代+）、Win10/N 独显工作机（RTX 5080 已验证）、Mac（mac_port 分支，未涉及）

**关键测试文件**：
- `D17_20260722074234.mp4`：MPEG-PS 伪装 .mp4，HEVC 2560×1440 25fps，起始 PTS 偏移 21586s，AAC 16kHz 单声道，~47 分钟——监控导出的典型棘手文件，主测拖拽/seek 精度
- `02-39-10_6m.mp4`：720×480 H.264 15fps，AAC 8kHz 单声道——低采样率音频时钟卡顿测试
- `uhd4k_420.mp4`：4K H.264 4:2:0——硬件解码测试
- 测试矩阵：h264_aac.mp4 / h265.mp4 / h264.mkv / mpeg4.avi / noaudio.mp4 / lowrate8k.mp4 / uhd4k.mp4（4:4:4，应走软解）/ corrupt.mp4（截断文件）

---

## 二、v0.5 → v1.0 架构升级（已完成）

### 2.1 目标架构（分层）

```
┌─────────────────────────────────────────────────┐
│ ui/          纯展示组件，不持有业务状态              │
│  VideoWidget / ChartPanel / MagnifierWidget ...  │
├─────────────────────────────────────────────────┤
│ app/  (新增) 应用服务层：流程编排、状态机           │
│  AnalysisController   分析状态机+UI状态驱动        │
│  VideoSessionManager  打开/切换/状态保存恢复        │
│  ProjectIO            .vla/CSV 读写参数组装        │
│  UiState              时长/融合参数等唯一数据源      │
├─────────────────────────────────────────────────┤
│ domain/      纯数据模型，不依赖 Qt Widgets         │
│  RoiModel(统一) / TimelineModel / ProjectState    │
│  ProjectSerializer (格式+版本迁移)                 │
├─────────────────────────────────────────────────┤
│ infrastructure/  引擎实现                         │
│  IVideoEngine / IAnalysisEngine (接口能力完整)     │
│  VlcVideoEngine / FfmpegVideoEngine / PythonEngine│
│  ProxyManager                                     │
└─────────────────────────────────────────────────┘
依赖只允许向下。ui 可持有 domain 模型指针；app 协调一切；
domain 和 infrastructure 之间只允许通过接口通信。
```

**已实现**：infrastructure 层全部就位（接口 + 实现）。app 层（AnalysisController / VideoSessionManager / ProjectIO）尚未拆分——mainwindow.cpp 仍是上帝类（~2876 行），是下一阶段架构重构重点。

### 2.2 架构红线（R 规则）

| 规则 | 内容 | 状态 |
|---|---|---|
| R1 | 依赖方向不可逆：domain 禁 include Widgets；infrastructure 禁知 ui | ✅ |
| R2 | 禁止向下转型（qobject_cast 到具体引擎） | ⚠️ mainwindow.cpp 仍有 6 处 PythonAnalysisEngine cast（P2 拆分时处理） |
| R3 | 禁止穿透封装（如 m_chartPanel->axisX()->setRange） | ⚠️ 存在（P2 拆分） |
| R4 | 引擎中立：UI 层无 VLC/FFmpeg/Python 字样 | ✅ 引擎创建集中一处 |
| R5 | 单一数据源（SSOT） | ⚠️ 视频时长仍有 5 份副本（P2 拆分） |
| R8 | 新分析功能走 TaskRegistry 注册 | 待实现（OCR 时落地） |

---

## 三、P0 Bug 修复清单

| # | Bug | 修复 | 文件 |
|---|---|---|---|
| 1 | roiId 冲突：矩形/多边形模型各自从 1 分配 ID，removeRegionDataByRoiId 误删 | `dataIndexOfRoiId`/`removeRegionDataByRoiId` 增加 `DataEntry::Type` 参数，9 处调用点全部更新 | `analysis_snapshot.h:170`, `timeline_model.cpp:86`, `mainwindow.cpp` 9 处 |
| 2 | 多视频 fps 复用第一个视频的 fps | `offset_ms += (tf / fps_v) * 1000.0` 改用每个视频自己的 fps | `analyze_video.py:546-549`（三处副本已同步） |
| 3 | `hasData()` 漏判 guideLines/labels/fusion/A-B → 辅助线/标签静默丢失 | 补判全部字段 | `videostatemanager.h:28` |
| 4 | `onAudioAnalysis` pyEngine 空指针 | cast 失败时明确报错返回 | `mainwindow.cpp:2066` |
| 5 | 加载 .vla 覆写 m_currentVideoPath → 状态键污染 | 删除赋值 | `mainwindow.cpp:1419, 1643` |
| 6 | labels CSV 无转义 | RFC 4180 引号转义 | `mainwindow.cpp:2267` |
| 7 | 快捷键文档 L=A/B 循环与实际冲突 | 删除 L 行，修正 tooltip | `mainwindow.cpp`, `README.md`, `MANUAL.md` |
| 8 | 每帧多余深拷贝 | 删 `.copy()` | `videowidget.cpp:1183` |
| 9 | portable 脚本版本不同步 | 复制根目录版本 | `portable/analyze_video.py` |

---

## 四、FFmpeg 播放引擎（FfmpegVideoEngine）

### 4.1 架构设计

单工作线程完成 demux + decode + pace。命令（play/pause/stop/seek）经锁队列下发，命令覆盖（单槽最新胜出）。

**线程模型**：
```
UI线程 ──postCommand()──► 工作线程（workerMain 循环）
                            ├─ 处理命令
                            ├─ av_read_frame → processVideoPacket → drainDecoder → displayFrame
                            │   └─ 代理解码（proxyDisplayFrame）→ m_mainSeekPending
                            ├─ processAudioPacket → swr_convert → QAudioSink
                            ├─ 空闲：沉淀升级 / 预读缓存 / 预读打开 / 等待
                            └─ 退出：closeFile
```

**时钟**：
- 音频主时钟：`m_sink->elapsedUSecs()`（已修复的版本，曾用 bytes-written-minus-buffered 导致 runaway）
- 低通平滑：增益 0.25 渐进校正，大漂移（>300ms）直接重置
- 回退系统时钟：音频时钟停滞 >500ms 或音频帧时长 ×4 自动切系统时钟

**硬件解码**：D3D11VA，`thread_count=1`（hwaccel 与帧级多线程不兼容），自动回退软解。适配器枚举 via DXGI，偏好最大 DedicatedVideoMemory（独显）。

### 4.2 接口能力（IVideoEngine 扩展）

```cpp
virtual void setProxySource(const QString &proxyPath);     // 拖拽预览代理
virtual void setHardwareDecode(bool enabled);               // 硬解开关
virtual bool hardwareDecodeActive() const;                  // 诊断
virtual void setHardwareAdapter(int index);                 // 适配器选择
static QVector<D3D11AdapterInfo> availableAdapters();       // DXGI 枚举
virtual bool supportsRateAudio() const;                     // false = 倍速静音
```

### 4.3 seek 三段式

```
1. avformat_seek_file(fmt, -1, INT64_MIN, ts, ts, AVSEEK_FLAG_BACKWARD)  // 时间戳精确
2. 失败 → avformat_seek_file(..., AVSEEK_FLAG_BYTE | AVSEEK_FLAG_BACKWARD)  // 字节估算兜底
3. 失败 → av_seek_frame(fmt, -1, ts, AVSEEK_FLAG_BACKWARD)
```

无索引容器（PS/TS）：seek 目标前移 `m_seekMarginMs`（初始 2500ms，按实测落点误差自适应收缩）。

### 4.4 追赶解码可中止

```cpp
// drainDecoder 每帧检查：
if (discard && hasPendingCommand()) {
    av_frame_unref(frame);
    break;   // 新 seek 到达，立即放弃旧追赶
}
```

### 4.5 EOF 解码器冲空

frame threading 滞留最后 N 帧。修复：`avcodec_send_packet(m_vdec, NULL)` + `drainDecoder()`。

### 4.6 关键性能数据（D17 PS 文件）

| 项目 | 数值 |
|---|---|
| seek 精度 | ≤ 单帧（~20ms） |
| D3D11VA（RTX 5080）5 次 seek 追赶 | 660ms（2.2x 优于核显） |
| D3D11VA（AMD 核显）5 次 seek 追赶 | 1470ms |
| 代理模式单次 seek | ≤10ms |
| 沉淀升级到 4K | ~0.5s |

---

## 五、音频管线

### 5.1 链路

```
AAC packet → avcodec_receive_frame → swr_convert → QAudioSink (push mode) → m_sinkIo->write()
```

QAudioSink 推模式：不需事件循环（WASAPI 后端自行管理线程）。`m_sinkIo = m_sink->start()` 获取内部 QIODevice。

### 5.2 seek 对齐（F-A）

- 音频丢弃阈值 = seek 目标 − 半帧容差
- `processAudioPacket`：按音频帧 PTS 丢弃早于阈值的帧；部分重叠按比例裁剪输出起始
- 音频时钟基点 = 实际第一个写入样本的 PTS（而非 seek 目标）
- 验收：avsync 偏差 ≤ 86ms（D17、8kHz、44.1kHz 均通过）

### 5.3 时钟平滑（F-C）

- 原方案：`bytes-written - buffered / bytesPerSec`——推模式下 sink 消耗速度快导致 runaway（音频时钟随 demux 速度漂移，视频追音频→倍速播放）
- 修复：`m_sink->elapsedUSecs()`——设备自身时钟，与内容无关
- 低通平滑：增益 0.25 渐进校正阶梯（8kHz AAC 一帧 128ms 阶梯 → 平滑后 jitter stdev 7%）
- freshness 阈值：`max(500, 4 × m_audioFrameMs)`（8kHz 下为 512ms，避免误判停滞）

### 5.4 倍速静音

`processAudioPacket` 开头：`if (qAbs(m_rate.load() - 1.0f) > 0.01f) return;`——一期方案，界面有"（音频已静音）"提示。

---

## 六、代理媒体系统（FCPX 式）

### 6.1 设计目标

拖拽时逐帧实时跟随（≤10ms/seek），帧位置精确（帧号与原片 1:1），分辨率降为 960p；沉淀 300ms 后自动回 4K 全分辨率；分析/放大镜/截图永远走原片（证据链不变）。

### 6.2 ProxyManager

**触发条件**：分辨率 >1080p 或无索引容器（PS/TS）。`needsProxy()` 通过 `avformat_open_input` 快速探测 demuxer 名。

**转码命令**（NVENC 优先，失败自动回退 libx264）：
```
ffmpeg -y -i src -vf scale=960:-2 -an -sn -dn -fps_mode passthrough
       -c:v h264_nvenc -preset p1 -g 1   // 或 libx264 -preset ultrafast -threads 2 -g 1
       -movflags +faststart out.part.mp4
```

关键参数：`-fps_mode passthrough`（逐帧不丢，帧号 1:1）、`-g 1`（全 I 帧，任意帧 = 关键帧）。

**临时文件**：`.part.mp4`（保留 .mp4 扩展名，mp4 muxer 按扩展名推断格式——曾因 `.mp4.part` 导致 muxer 失败）。

**缓存**：`%LOCALAPPDATA%/LumenArc/<appname>/cache/proxy/`，SHA1(规范化路径|大小|mtime) 命名，10GB LRU 自动清理。

**进度**：探测源时长后按 ffmpeg stderr `time=` 计算百分比。

### 6.3 引擎代理路径（P2）

```
handleSeek(timeMs):
    if (!forceMainPipeline && m_pxReady && 非播放态):
        proxyDisplayFrame(timeMs)  // 全 I 帧 mp4 → seek + 1帧解码 → 10ms 出图
        m_mainSeekPending = true
        return   // 不动主管线

空闲沉淀升级（工作线程 idle 循环）：
    if (m_mainSeekPending && 300ms 无新 seek):
        m_mainSeekPending = false
        handleSeek(pos, forceMainPipeline=true)  // 绕过代理，主管线精确 seek → 全分辨率

Play 命令：
    if (m_mainSeekPending):
        handleSeek(pos, forceMainPipeline=true)  // 主管线追平，然后播放
```

**`forceMainPipeline` 参数**是沉淀路径的关键：沉淀调用 handleSeek 时必须绕过代理分支，否则会再次被代理截获（死循环 bug 修复）。

### 6.4 代理上下文

独立于主管线：`m_pxFmt/m_pxDec/m_pxSws`，在工作线程 idle 打开。代理就绪后禁用 F-D2 预读缓存（二者功能重叠）。

---

## 七、测试体系

### 7.1 无头测试程序

`lumenarc_engine_test.exe`（链接 FfmpegVideoEngine + ProxyManager，无需 GUI）：

| 场景 | 参数 | 验收 |
|---|---|---|
| `seek-matrix <file>` | tol 默认 1000ms | 5%~95% 位置 seek，误差 ≤ tol |
| `random-seek <file> <count>` | tol 1000ms | 随机位置 seek，误差 ≤ tol |
| `play <file> <seconds>` | | frames ≥ fps×sec×0.4，位置前进 |
| `audio <file> <seconds>` | | audioBytesWritten > 0，音量 set/get |
| `avsync <file> <seconds>` | | 跳过启动瞬态 1.5s，max deviation ≤ 300ms |
| `jitter <file>` | | 帧间隔 stdev < 80% 均值 |
| `step <file> <steps>` | | 暂停态连续 seek +1 帧，严格单调，误差 ≤ frameMs |
| `rate <file>` | | 2x/0.5x 位置前进在预期区间 |
| `stress <file> <seeks>` | | seek + 30s play，内存增长 < 300MB |
| `corrupt <file>` | | 不崩溃，state=Idle |
| `scrub <file>` | | 10 次连续 seek（80ms 间隔），最终落点精确 |
| `proxy <file>` | | 代理生成 → 连续 10 次 seek 每次 ≤150ms → 沉淀升级到全分辨率 |
| `adapters <file>` | | 逐适配器加载，报告 hwdec + seek 追赶耗时 |

### 7.2 测试矩阵（28 项 + D17 专项）

8 文件 × 3 场景（seek-matrix / play / audio）= 24 + D17 seek/avsync/stress/scrub = 28。

**已知自动测试限制**：jitter 测试依赖 QElapsedTimer 精度（泵粒度 20ms）；音频同步启动瞬态跳过 3 个采样点。

### 7.3 手工验收清单

1. D17 拖拽手感（代理就绪后应逐帧实时跟随）
2. 音画同步听感（人声/环境声无漂移）
3. 逐帧步进（←/→ 严格逐帧）
4. 倍速（2x/4x/8x 流畅；1x 恢复音频）
5. 引擎切换（设置→播放内核→VLC，重启验证）
6. 回归（亮度分析/音频分析/ROI/标签/AB 循环/保存加载 .vla）

---

## 八、已知技术债

| 债项 | 严重度 | 计划阶段 |
|---|---|---|
| MainWindow 上帝类（2876 行） | 高 | P2 拆分（AnalysisController / VideoSessionManager / ProjectIO） |
| AnalysisPhase 硬编码两阶段枚举 | 高 | 任务化状态机替代 |
| AnalysisSnapshot 硬编码 luminance/audio 成员 | 高 | 通道化改造 |
| RegionModel/PolygonModel 双模型复制 | 中 | 合并为 RoiModel（RegionShape 已有但未用） |
| 旧版 SpectrogramPanel（死代码） | 低 | 删除 |
| i18n 为 lang(zh,en) 硬编码二选一 | 低 | 多语言需求时迁移 tr() |
| 6 处 qobject_cast<PythonAnalysisEngine> | 中 | 接口上移到 IAnalysisEngine |
| roiId 跨模型仍可能冲突（RoiModel 未合并前） | 中 | P1b |
| Python 分析脚本多视频 fps offset 已修但多视频音频路径是死代码 | 低 | 二期 |

---

## 九、构建与部署

### 9.1 依赖

| 依赖 | 来源 | 大小 |
|---|---|---|
| Qt 6.8.0 msvc2022_64 | C:\code\Qt\6.8.0 | — |
| Qt Multimedia | aqtinstall 补装 | — |
| FFmpeg lgpl-shared | third_party/ffmpeg/（gitignored） | ~200MB |
| FFmpeg gpl（编码用） | build\Release\ffmpeg\（CI 下载） | ~100MB |
| VLC SDK | vlc_extracted/（gitignored） | ~150MB |
| Python 3.12 嵌入式 + cv2 + numpy | CI "Bundle Python" 步骤 | ~80MB |

### 9.2 本地构建

```powershell
cmake -S . -B build -G "Visual Studio 17 2022" -DCMAKE_PREFIX_PATH="C:/code/Qt/6.8.0/msvc2022_64"
cmake --build build --config Release
```

`CMakeLists.txt` 自动查找 FFmpeg SDK（third_party/ffmpeg/）、VLC（vlc_extracted/）、Qt6（$QT6_DIR）。

### 9.3 Release 部署

POST_BUILD 自动：windeployqt（Qt DLLs）→ FFmpeg DLLs → analyze_video.py → ffmpeg/ → lightchaser.jpg → PDF。

手动：下载 Python embeddable + pip install opencv-python-headless numpy（CI 步骤）。

### 9.4 CI（GitHub Actions）

`build-win64.yml`：checkout → install Qt 6.8 + qtmultimedia → VLC SDK → FFmpeg SDK（lgpl-shared）→ configure → build → download ffmpeg gpl（测试用）→ Bundle Python → **Run engine self-tests**（生成测试剪辑 + seek-matrix/play/audio/step）→ Create portable package → Upload artifact。

### 9.5 Git 标签

| 标签 | 内容 |
|---|---|
| v1.0.0-ffmpeg | P0 修复 + 基线 |
| v1.1.0-scrub | 音频对齐/时钟平滑/可中止 seek/预读缓存 |
| v1.1.1-hwdec | D3D11VA 修复（thread_count=1） |
| v1.1.2-dgpu | 独显自动选择（2.2x） |
| v1.2.0-proxy | FCPX 级代理拖拽 |

---

## 十、下一步待做

1. **P2 架构拆分**：mainwindow.cpp → AnalysisController + VideoSessionManager + ProjectIO（行为冻结纯移动，预计 2 周）
2. **P1a 任务化状态机**：分析功能走 TaskRegistry 注册（为 OCR 做准备）
3. **P1b 通道化**：AnalysisSnapshot 改为通道字典 + .vla v7 格式
4. **FFmpeg 分析引擎**（P3）：libav 原生亮度+音频分析，干掉 Python 依赖和 5000 帧上限
5. **OCR 时间戳**：Python + RapidOCR，任务框架第二个租户
6. **显示管线上 GPU**：QOpenGLWidget/Rhi 渲染消除每帧 CPU swscale，多视频 CPU 占用优化

---

*文档结束*
