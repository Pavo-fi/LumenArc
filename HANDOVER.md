# LumenArc v1.3 工作交接文档

> 编制日期：2026-07-28
> 最新标签：`v1.3.3-always-flush`
> 提交历史：`77c8e44`→`abd7b11`→`ff4c9a4`→`adb429b`→`5bbb59b`→`6b61a8d`→`25efc66`→`d978496`→`f496680`→`4447ca7`→`66b2b8f`→`0306427`→`e812e0c`→`f073804`→`16a64de`→`d9cc07a`→`977cb96`→`f9141f8`→`9d1230a`
> 前处理批次：`9c97072`→`2ba4776`→`772c97f`→`7f5f20b`→`e34bdd9`→`40abd96`→`4b87a5a`（见第十三章）
> 现场反馈批次：`95f6dba`→`a8a2fe1`→`62a1c76`（见第十四章）
> DAV 批次：见第十五章（GBK 编码崩溃根因 + DHAV 流内绝对时间证据层）
> 性能批次：见第十六章（absStart 跳过 88×、子进程低优先级、merged 命名+播放出口）
> 流程批次：见第十七章（非强制一键拼接、逐文件转码、假成功修复）
> 拖拽批次：见第十八章（seek 回归三连修、34fps 拖拽、金色插入线、Del 删除、列表收录）

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
                            │   └─ scrub 追逐解码（scrubChaseMainFrame，拖拽时）
                            ├─ processAudioPacket → swr_convert → QAudioSink
                            ├─ 空闲：预读缓存 / 预读打开 / 等待
                            └─ 退出：closeFile
```

**时钟**：
- 音频主时钟：`m_sink->elapsedUSecs()`（已修复的版本，曾用 bytes-written-minus-buffered 导致 runaway）
- 低通平滑：增益 0.25 渐进校正，大漂移（>300ms）直接重置
- 回退系统时钟：音频时钟停滞 >500ms 或音频帧时长 ×4 自动切系统时钟

**硬件解码**：D3D11VA，`thread_count=1`（hwaccel 与帧级多线程不兼容），自动回退软解。适配器枚举 via DXGI，偏好最大 DedicatedVideoMemory（独显）。

### 4.2 接口能力（IVideoEngine 扩展）

```cpp
virtual void setScrubMode(bool enabled);                  // 拖拽追逐模式（原子目标）
virtual void setScrubTarget(qint64 timeMs);               // 拖拽高频调用，免锁免命令
virtual void ackFrame();                                  // UI 归还 frameReady 配额（有界队列）
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
| 多线程软解（thread_count=0）D17 1440p 追赶 | ~3140fps |

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

## 六、拖拽预览：从代理系统到 VLC 路线（代理已于 567072a 整体移除）

> **当前状态**：代理媒体系统（6.1-6.4）已整体删除（commit `567072a`，-866 行）。
> 拖拽流畅性改走 VLC 路线（见 6.6）。以下 6.1-6.5 保留作历史记录与教训。

### 6.1 设计目标（历史，已移除）

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

### 6.5 Scrub 模式（拖拽连续解码）—— ✅ 已解决（追逐模型）

**目标**：拖拽图表光标时，视频画面逐帧连续流动（如 VLC/FCPX），松手后精确落位。

#### 最终方案：原子目标 + 追逐循环（Chase Loop）

**根因再诊断**：前 6 次失败的共同前提错误——"每次 mouseMove = 一条 seek 命令"。拖拽期间每秒 20 条 Seek 命令使 `hasPendingCommand()` 恒真，`scrubDisplayNextPxFrame` 的连续解码在结构上永远不可能运行（解码一帧→发现 pending→break→seek+flush→循环），本质是"快速幻灯片"。瓶颈不是 seek 延迟，是**输入模型**。

**关键洞察**：g=1 全 I 帧代理帧间零依赖——"前进到下一帧"只是读下一个 packet，不需要 seek/flush；且 packet 自带 PTS，落后目标的包可**不解码直接丢**（demux 级追赶，微秒级/包）。

**实现**（`v1.4.0-scrub-chase`）：
```
MainWindow 拖拽:  engine->setScrubTarget(ms)   // std::atomic store + wakeAll，免锁免节流免命令
Worker scrub 分支: scrubChasePxFrame() 循环：
    target = m_scrubTargetMs.load()     // 每包刷新，目标持续移动
    |delta| > 500ms 或后退 >2帧 → seek+flush（全 I 帧有索引，~ms 级）
    已落位且目标未变 → 睡 8ms（光标静止不空转）
    否则 av_read_frame 连续读：
        pkt.pts < target-2帧 → unref 直接丢（demux 级追赶，不解码）
        连续丢包 >4 → flush 一次（防解码器滞留跳段前旧帧）
        只解码目标附近的帧，显示首个 >= target-半帧 的帧
        relMs > target+500ms → retarget（目标中途大幅后退，交外层 seek）
```

- **拖拽期间 `m_pendingCmd` 恒为 None** → `hasPendingCommand()` 恒假 → 连续解码第一次真正跑起来；
- 前进追逐全程**零 seek 零 flush**，管线不断流；真命令（Play/Stop/退出 scrub）仍由 hasPendingCommand 逃逸；
- 无代理文件（≤1080p 有索引）维持逐 seek 主管线路径（seek 本来快）；
- 松手逻辑不变：退出 scrub → 代理一次性精确帧 → 300ms 沉淀回全分辨率。

**验收**（新场景 `scrub-chase`，模拟 60 步×10ms 真实拖拽）：
| 指标 | 结果 |
|---|---|
| 前进拖拽 600ms 显示不同帧 | 43-47 帧（~72fps，0 回退，严格单调） |
| 后退拖拽 600ms 显示不同帧 | 38-39 帧 |
| 松手落点误差 | 0ms（≤单帧） |
| 全回归 | 24 项矩阵 + scrub/step/avsync/jitter/rate/stress/proxy 全绿 |

**新验收指标**（替代旧的"600ms 帧数"——旧指标与用户手感脱节）：拖拽期间每秒实际显示的、与光标路径单调对应的不同帧数 ≥ 40；后退同理。

#### 历史：已尝试方案及结论（保留教训）

| # | 方案 | 结果 | 教训 |
|---|---|---|---|
| 1 | **代理逐位置 seek**（每次 mouseMove → proxyDisplayFrame，节流 50ms） | 代理 seek 10ms，理论 20fps。用户反馈：**帧跳跃，不连续** | 每次 seek = 清空解码器 + 解码一帧 + break。帧与帧之间有 worker 循环延迟（等命令 + seek + 解码）。**不是"连续播放"，是"快速幻灯片"** |
| 2 | **Scrub 模式 + 连续解码**（drainDecoder 在 scrub 模式不 break，持续出帧） | 测试程序：D17 59帧/600ms≈100fps。用户反馈：**依然跳跃** | 测试程序通过是因为 `frameReady` 信号被计数（测试不关心显示顺序）。**实际问题是每次 seek 都 flush 解码器 → 解码管线清空 → 只出一帧 → 等下一次 seek** |
| 3 | **方向感知 flush**（前进不清空解码器，后退清空） | 测试：h265.mp4 seek-matrix 失败（落在下一 GOP）。用户反馈：**更差** | **frame-threading 下"不清空前进"不安全**：解码器内部有多线程帧队列，`avformat_seek_file` 重定向 demuxer 后解码器仍持有旧帧，`avcodec_receive_frame` 返回旧帧（乱序）。**所有帧都必须清空才能保证正确性** |
| 4 | **始终清空**（代理和主管线都 flush） | 测试：24/24 全回归通过。用户反馈：**甚至更差** | 代理路径 `proxyDisplayFrame` = seek+flush+解码一帧+break → 帧与帧之间有完整 worker 循环延迟。**"清空"本身正确，但破坏了连续解码的前提** |
| 5 | **proxyDisplayFrame + scrubDisplayNextPxFrame**（首次 seek+flush+显示，后续不 seek 连续解码） | 测试：D17 46帧/600ms。用户反馈：**依然跳跃** | `scrubDisplayNextPxFrame` 读代理解码器 → 代理解码器在 `proxyDisplayFrame` 后停在显示帧之后 → `scrubDisplayNextPxFrame` 继续读 → **每次 mouseMove 触发 `handleSeek` → 重新 `proxyDisplayFrame` → 重置解码器位置**。`scrubDisplayNextPxFrame` 永远不会被调用（handleSeek 先走了） |
| 6 | **proxyRedirectSeek**（前进只重定向 demux，不清空解码器） | 测试：D17 60帧/600ms。用户反馈：**帧跳跃** | `proxyRedirectSeek` 不清空代理解码器 → 解码器内部仍有旧位置的帧 → `scrubDisplayNextPxFrame` 返回旧帧（错误位置） |

#### 历史方案的核心矛盾（已被追逐模型化解）

**"连续播放"和"seek 精确帧"在当前架构下是互斥的**：

- **连续播放**：解码器不停，帧持续流出。需要解码器持续运行、管线不断流。
- **seek 精确帧**：每次 mouseMove 要求显示"光标位置对应的帧"。需要 seek 到目标位置并解码显示。

在 VLC/FCPX 中，这两个目标通过**极低延迟的 seek + 连续播放**实现：
- VLC 的 seek 代价极低（~5ms），每次 slider 位置变化 → seek → 解码器立即出帧 → 显示。帧与帧之间的间隔 ≈ seek 延迟 ≈ 5ms，低于人眼感知，所以看起来连续。
- 我们的 seek 代价：代理 ~10ms，主管线 ~30-100ms。帧间隔 > 16ms（60Hz 显示器刷新率），人眼可感知跳跃。

**根本差距在于 seek 延迟**：
- VLC seek 5ms → 200fps 显示 → 看起来连续
- 我们 seek 10ms → 100fps 显示 → 测试程序看起来连续，但实际 UI 中 Qt 事件循环 + paint 合并 → 有效帧率 ~30-60fps → 用户感知跳跃

**教训**："连续播放"和"seek 精确帧"并非互斥——只要输入模型改为"原子目标 + 围绕目标的连续解码"，二者自然统一。瓶颈从来不在 seek 延迟，而在每条 mouseMove 都经过锁+条件变量+flush 的命令循环。

~~**下一步方案（待实施）**~~（已废弃，见上方最终方案）

<details><summary>废弃的方案 A/B 存档</summary>

**方案 A：独立 Scrub 线程（绕过 worker 命令循环）**

当前瓶颈：每次 seek 经过 worker 命令循环（锁 → 条件变量唤醒 → 处理 → 解码 → emit），总延迟 ~10-30ms。

解决方案：**scrub 不经过 worker 命令循环**。MainWindow 拖拽时，启动一个独立的"Scrub 专用线程"：

```
ScrubThread:
    打开独立的代理上下文（或复用现有）
    while scrubActive:
        read next packet from proxy demux
        decode
        emit frameReady(img)   // 直接 emit，不经过 worker 命令循环
        // 帧率 = 代理解码速度（~100fps），不受 worker 命令循环延迟影响
```

MainWindow 拖拽时：
- 设置 `scrubTargetPos`（原子变量）
- ScrubThread 每解码一帧，比较 `currentPos` 和 `scrubTargetPos`：
  - 偏差 < 半帧：直接显示（连续播放）
  - 偏差 > 1s：seek 代理到 `scrubTargetPos`（快速跳转）
  - 偏差 100ms-1s：继续播放，下几帧内自然追上

这实现了 VLC 的行为：**帧以解码速度连续流出，seek 只在偏差大时发生**。拖拽时看到的是连续运动，松手后精确 seek 到最终位置。

**方案 B：不 seek，直接播放 + 定期同步（更简单）**

更简单的替代方案：拖拽时不 seek，而是**从当前代理位置持续播放**。MainWindow 定期把 `scrubTargetPos` 告诉 ScrubThread。ScrubThread 持续播放代理帧，偶尔（每 300ms 或偏差 >500ms 时）seek 一次同步到目标位置。

这更接近 VLC 的行为：播放器持续播放，slider 只改变"要跳到哪里"的意图。

**方案选择**：方案 B 更简单，但拖拽速度受限于代理播放速率（~100fps）。方案 A 更灵活（可变速）。建议先做方案 B 验证手感，再按需升级到方案 A。

</details>

---

### 6.6 第二阶段：追逐模型推广、精确跟踪语义、最终砍掉代理走 VLC 路线（v1.4.x 系列）

本节记录追逐模型落地后的完整迭代过程（2026-07-28，commits `4059b13` → `567072a`）。

#### 6.6.1 迭代时间线

| 阶段 | commit | 内容 | 结果/转折 |
|---|---|---|---|
| 追逐模型 | `4059b13` | 原子 `m_scrubTargetMs` + worker 追逐循环（见 6.5），代理文件拖拽 43-47 帧/600ms | 代理就绪后顺滑；**无代理文件仍是逐 seek 幻灯片** |
| F1 主管线追逐 | `5a9577a` | `scrubChaseMainFrame()`：无代理文件也走追逐模型，主管线全分辨率连续解码 + 追赶中显示中间帧（catch-up display） | 测试全绿；**用户否决追赶显示**："不要快进感，要指哪打哪" |
| 精确跟踪语义 | `f9b3794` | 只显示目标窗口帧（落后帧静默解码跳过，绝不显示）；`m_chaseDecodePosMs` 跟踪解码位置；前进阈值 20s；状态栏代理标签；NVENC 失败会话记忆 + x264 4 线程 | 手感语义对了，但 D17（GOP=10s）硬解追赶太慢 |
| 自适应解码器 | `5f63a56` | 硬解先行 → 首帧实测追赶 >4s 切多线程软解重 seek；GOP 学习（`m_lastCatchupMs`）免二次试探；三级锚点防填充期 reseek 风暴；无索引阈值 `max(4s, 2×margin)` | 全文件测试最优（见 6.6.4 数据） |
| **最终：砍代理** | `567072a` | 用户拍板"砍掉代理，直接对标 VLC"：删 ProxyManager 及全部代理代码（-866/+43）+ 三项 VLC 路线改进 | **当前架构** |

#### 6.6.2 诊断发现（砍代理前的根因分析）

1. **代理进度卡在 -1%**：`ProxyManager::requestProxy` 探测时长只调 `avformat_open_input`，FFmpeg 8 下不读包头 `fmt->duration` 常为 `AV_NOPTS_VALUE` → `srcDur<0` → 进度公式得负值。修法：探测加 `avformat_find_stream_info`。（已随代理删除而失效）
2. **2h 文件代理"生成完找不到"**：47 分钟 D17 全 I 帧 960p 代理 ≈5.3GB，2h 文件代理 ≈10.4GB **超过 10GB LRU 上限**，生成完成的瞬间被 `enforceCacheLimit` 自己清掉。
3. **慢拖卡死（几秒无帧后时间码猛跳）**：像素粒度 seek 风暴（2h 文件 1px ≈7s 视频 > 20s 阈值 → 每 1-2px 一次 seek），每次 seek 还重置 WASAPI sink（10-50ms 固定开销）。
4. **VLC 对比**：VLC 语义与我们相同（指哪打哪，绝不播中间帧），但每次落点 ~100ms——多线程软解（thread_count=auto）、seek 不做硬解切换体操、不重置 sink、vout 队列有界化丢帧。

#### 6.6.3 最终架构（commit 567072a）

**删除**：ProxyManager（NVENC/libx264 转码、10GB LRU 缓存、.part muxer）、引擎代理上下文（m_pxFmt/m_pxDec/m_pxSws）、proxyDisplayFrame/scrubChasePxFrame、沉淀补全逻辑（m_mainSeekPending/forceMainPipeline）、UI 代理开关与状态标签、proxy/scrub-chase 测试场景。

**VLC 路线三项改进**：
1. **scrub seek 跳过音频 sink 重置**：`scrubRedirectDemuxer` 中 `if (m_sink && !m_scrubMode)` 才 reset+start（拖拽静音，每次 seek 省 10-50ms）。
2. **frameReady 队列有界化（VLC vout 式）**：`displayFrame` 检查 `m_framesInFlight >= 2` 时丢帧（仍更新 positionChanged），防止 Qt 信号队列积压导致画面滞后/回放感；UI 侧 `VideoWidget::onFrameReady` 调新虚接口 `IVideoEngine::ackFrame()` 归还配额。**注意：任何 frameReady 消费者都必须 ack，否则 2 帧后永久丢帧**（无头测试 harness 已在 lambda 中补 ack）。
3. **自适应追逐解码器**（实测最优，保留）：短追赶用硬解（thread_count=1 无管线填充延迟，24 帧 ≈24-48ms）；首帧实测追赶 >4s 切多线程软解重 seek（~3000fps 吞吐）；`m_lastCatchupMs` GOP 学习避免后续拖拽重复试探；`m_chaseSeekTargetMs` 三级锚点防软解填充期（~100ms 无帧输出）reseek 风暴。

**曾尝试被否决的方案**：全程多线程软解（纯 VLC 路线）——2h 文件 20 次 0.5% 跳点 **0 帧落点**（50ms 泵周期内），软解管线填充延迟在短追赶场景是硬伤。硬解/软解自适应是实测平衡点。

#### 6.6.4 验收数据（scrub-playback 场景：0.5% 步长 × 20 次，50ms 泵周期）

| 文件 | GOP | 显示帧 | 跟踪率 | 落点误差 |
|---|---|---|---|---|
| 小文件 h264/4K | 密 | 20/20、36/36 | 100% | 0ms |
| 明景拼接 2h | 1s | 20/20 | 100% | ≤16ms |
| D17 PS（无索引） | 10s | 12/12 | 100% | 4ms |
| D17 mp4 | **10s** | 4/4 | 100% | 4ms |

D17 4/20 是稀疏关键帧的物理极限（每次跨 GOP 需解码 ~240 帧，VLC 在此文件同样跨不过去，只是单次落点更快）。全回归 24 项矩阵 + scrub/step/avsync/jitter/rate/corrupt 全绿。

#### 6.6.5 遗留问题

- D17（GOP=10s）拖拽若仍嫌比 VLC 慢：可实测单次落点毫秒数对比 VLC。
- NVENC 在用户机器全局不可用（驱动 API 13.0 < 13.1，需 ≥610.00 驱动）——代理删除后已无影响。
- 已生成的代理缓存残留：`%LOCALAPPDATA%/LumenArc/LumenArc/cache/proxy/` 可手动清理。

#### 6.6.6 研究结论："多线程硬解"是否存在？追赶还能更快吗？（catchup-bench 实验）

**结论：多线程硬解在 FFmpeg 里不存在，且 D17 的追赶速度已触物理下限。**

1. **硬解无法多线程**：hwaccel 与帧级多线程互斥（`thread_count` 强制 1，多线程会复制解码器状态而 hwaccel 上下文不支持）。硬解路径 = CPU 单线程 CABAC 熵解码 + GPU 固定电路（NVDEC/D3D11VA）做重建。实测硬解追赶 ~500-1000fps（1-2ms/帧），**比多线程软解 ~2300fps 还慢**——这就是自适应方案长追赶切软解的原因。GPU 解码电路想并行只能开多解码器实例按 GOP 分段（NVDEC 多会话），对 D17 无效（10s 一个关键帧，GOP 内无法分段），对密 GOP 文件无必要（已 20/20 跟手）。工程量大收益零，不做。
2. **skip_frame=NONREF 实验**（追赶期丢非引用帧，位精确安全）：对 **D17 零收益**——实测 251 包两种模式都解 236 帧，说明 D17 的 h264 是 baseline profile（无 B 帧，每帧都是引用帧），一帧都省不了。D17 的"慢"是编码 profile 决定的：每个 GOP 必须解码全部 ~240 帧，÷ ~2300fps ≈ **~108ms 已是 20 线程 CPU 的物理下限**，VLC 同文件同量级。2h 文件 NONREF 能跳 72% 帧（有 B 帧）但实测 wall 几乎不变——其瓶颈在 demux 而非解码，且它已 20/20 跟手。
3. **软解填充延迟实测仅 23-47ms**（首帧输出时间），非此前担心的 ~100ms。

实验数据（`catchup-bench <file> <startMs> <spanMs>`，多线程软解，10s 跨度）：

| 文件 | DEFAULT | NONREF | 填充 |
|---|---|---|---|
| D17 mp4 1440p | 108ms / 2324pkt/s | 100ms（跳 0 帧，baseline 无 B 帧） | 23ms |
| 2h 明景 | 470ms / 511pkt/s | 457ms（跳 72% 帧但 demux 瓶颈） | 47ms |
| D17 PS | 239ms / 1050pkt/s | 238ms（跳 0 帧） | 29ms |

**第二轮否决（LOW_DELAY / THREADS8，同场景四模式实测）**：LOW_DELAY 消除重排缓冲
（填充 25→9ms）但禁用帧多线程，吞吐 2074→780pkt/s，总时长 121→322ms，净亏；
THREADS8 在 D17 mp4 填充减半但 2h/PS 吞吐降 12-24%，得失相抵。解码器参数调优至此封顶。

#### 6.6.7 滚动帧缓存 + 自适应阈值（拖拽手感第二轮优化）

**滚动帧缓存**：追赶解码的副产品帧（解 240 帧只为显示 1 帧，其余 239 帧原本丢弃）
保留在环形缓存（192MB 预算：1440p≈34 帧≈1.4s / 1080p≈64 帧≈2.6s / 4K≈15 帧；
仅软解帧 av_frame_clone refcount 零拷贝——硬解帧持 GPU 表面会耗尽解码池，不存）。
目标命中缓存直接显示：后退微调/手抖 0ms（原：每次后退 2 帧也要 seek+重解 GOP ~100ms）。
退出拖拽时清空（内存不滞留）。

**自适应前进阈值**：原固定 20s decode-through 阈值改为 `clamp(学习到的 GOP 长度, 4s, 20s)`
——稀疏 GOP（D17 10s）>10s 即 seek（直追 20s≈250ms > seek+追赶≈140ms）。

验收（新场景 `scrub-oscillate`：+600ms/−300ms 振荡 ×60 步，40ms 泵，模拟真实慢拖手抖）：

| 文件 | 帧数/60 步 | 跟踪率 | 落点 |
|---|---|---|---|
| D17 mp4（GOP=10s） | 35（≈15 落点/秒，原 ~5） | 100% | 8ms |
| 2h 明景 | 58 | 100% | 0ms |
| 小文件 | 69 | 100% | 0ms |

 scrub-playback 与全回归保持全绿（D17 PS 12→14 帧）。

---

## 七、测试体系

### 7.1 无头测试程序

`lumenarc_engine_test.exe`（链接 FfmpegVideoEngine，无需 GUI）：

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
| `scrub-playback <file>` | | 模拟真实拖拽（0.5% 步长×20 次，50ms 泵）：显示帧 100% 精确跟踪目标，最终落点 ≤16ms |
| `catchup-bench <file> [startMs] [spanMs]` | | 追赶解码吞吐基准：skip_frame DEFAULT vs NONREF，首帧填充延迟（见 6.6.6） |
| `scrub-oscillate <file>` | | 拖拽振荡（±步进手抖模拟）：滚动缓存命中，D17 ≥20 帧/60 步、100% 跟踪、松手精确（见 6.6.7） |
| `adapters <file>` | | 逐适配器加载，报告 hwdec + seek 追赶耗时 |

### 7.2 测试矩阵（28 项 + D17 专项）

8 文件 × 3 场景（seek-matrix / play / audio）= 24 + D17 seek/avsync/stress/scrub = 28。

**已知自动测试限制**：jitter 测试依赖 QElapsedTimer 精度（泵粒度 20ms）；音频同步启动瞬态跳过 3 个采样点。

### 7.3 手工验收清单

1. 拖拽手感（慢拖全程顺滑不卡、快拖指哪打哪、松手帧精确；重点验证 D17 GOP=10s 与 2h 长文件）
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
| ~~旧版 SpectrogramPanel（死代码）~~ | ~~低~~ | ✅ 已删（V1.0 remake） |
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
| v1.2.0-proxy | FCPX 级代理拖拽（已于 567072a 移除） |
| v1.3.0-scrub | Scrub 连续解码（D17 77fps，真正 VLC 级流畅） |
| v1.4.0-scrub-chase | 追逐模型（原子目标 + 连续解码，见 6.5） |
| v1.4.1-scrub-precise | 精确跟踪语义（指哪打哪，无快进感）+ 主管线追逐 |
| v1.4.2-adaptive-dec | 自适应追逐解码器（硬解短追赶/软解长追赶 + GOP 学习） |
| v1.5.0-no-proxy | 代理系统整体移除，VLC 路线拖拽（567072a，见 6.6） |

---

## 十、升级计划表

> 排序原则：性能项（GPU 管线）优先解锁硬件红利；P2 拆分属纯重构，宜插在功能冻结窗口；
> P1a/P1b 是新分析类型的前置；P3 干掉 Python 依赖；P4 OCR 依赖任务框架。
>
> **兼容性红线：独显优化只能是添头。** 所有 GPU 路径必须运行时探测、失败即回退 CPU 路径
> （解码：hwaccel→多线程软解，现有代码已如此；显示：零拷贝纹理→现有 QImage/sws 路径）。
> CPU 路径是永久功能底线与 CI 测试基线，核显办公电脑（同样支持 D3D11VA）完整可用。

| 阶段 | 项目 | 内容 | 前置依赖 | 预估 | 价值 |
|---|---|---|---|---|---|
| ✅ v1.5 | 拖拽 VLC 路线 | 代理移除 + 追逐模型 + 有界队列（§6.6） | — | 已完成 | — |
| **v1.6** | **GPU 显示管线** | D3D11 解码纹理零拷贝 → QRhi/OpenGL 渲染，shader 做 NV12→RGB；放大镜/截图叠加/预读缓存随同 GPU 化 | 无 | 1.5-2 周 | 显示 CPU 归零；4K 多视频、高刷屏解锁；GPU 功能前置 |
| v1.7 | P1a 任务化状态机 | TaskRegistry，分析功能注册制（替代 AnalysisPhase 硬编码） | 无 | 3-5 天 | OCR 前置 |
| v1.7 | P1b 通道化 | AnalysisSnapshot 改通道字典 + .vla v7 格式 | P1a | 1 周 | 新分析类型前置 |
| v1.8 | P3 FFmpeg 分析引擎 | libav 原生亮度+音频分析，NVDEC 解码 + 显存下采样 | 可叠加 GPU 管线 | 1-2 周 | 干掉 Python 依赖与 5000 帧上限，分析提速至近实时 |
| v1.9 | P4 OCR 时间戳 | Python + RapidOCR 自动识别监控时间戳，任务框架第二租户 | P1a | 1 周 | 自动对时 |
| 择机 | **P2 MainWindow 拆分** | 上帝类（~2876 行）→ AnalysisController + VideoSessionManager + ProjectIO，行为冻结纯移动 | 无（建议插在两版本之间的功能冻结期） | 2 周 | 可维护性（R2/R3/R5 红线收口） |
| 持续 | 小项清理 | SpectrogramPanel 死代码删除、qobject_cast 上移、roiId 跨模型冲突（RoiModel 合并） | 随 P1b/P2 | 零碎 | 技术债 |

**已研究否决（§6.6.6）**：多线程硬解（FFmpeg 不存在）、GOP 分段并行解码（D17 无效）、skip_frame=NONREF（D17 baseline 零收益）——解码侧已触物理下限，不再投入。

## 十一、V1.0 remake 文件夹（2026-07-29）

`C:/code/LumenArc/V1.0 remake` 为本项目的干净重建副本（git clone + 死内容清理提交 `bad15ea`），
已验证 fresh configure/build/冒烟测试通过。清理内容：

- **死代码**：旧版 SpectrogramPanel（从未实例化，活体是 SpectrogramPanelEnhanced）
- **死移植**：mac_port/ + build_mac.sh（Windows-only 项目，无 mac CI）
- **历史文档**：DEVELOPMENT_PLAN_V0.3 / FIX_PLAN_V0.3 / REVIEW_REPORT_V0.3 / PROJECT_LOG.md（均已入 git 历史）
- **过期手册**：v0.2/v0.5 PDF（MANUAL.md 为现行版）
- **CI 修复**：两个 Windows 工作流补齐 qtmultimedia + qtshadertools 模块（build.yml 原缺 qtmultimedia）

原文件夹 `LumenArc_v1.0` 保持原样未动。未拷贝内容：build/（可再生）、__pycache__、
build/testdata（CI 生成的测试剪辑）。已拷贝：third_party/ffmpeg（160MB SDK）、vlc_extracted（185MB）。

**运行时依赖已随包内置（2026-07-29，与 CI 打包一致，零手动安装）**：
- `build/Release/python/`：Python 3.12.8 嵌入式 + opencv-python-headless + numpy
  （python.org/pypa 下载，._pth 已启用 import site；已删 pip/setuptools 瘦身）
- `build/Release/ffmpeg/ffmpeg.exe`：GPL 版（github 在本机被墙，从原文件夹复制，同 CI 来源）
- 主程序优先使用内置 python（mainwindow.cpp `appDir + "/python/python.exe"`），无需 PATH/PYTHON_PATH
- 端到端已验证：内置 python + analyze_video.py + 内置 ffmpeg 跑通亮度+音量+语谱图全分析
- 注意：重新 cmake --build 不会清除这两项（非 CMake 部署项）；clean 重建后需重跑本节步骤

### 11.1 验收反馈修复批次（2026-07-29）

| commit | 问题 | 根因与修法 |
|---|---|---|
| `5c9ac46` | 图表辅助线跨视频泄漏 | ChartPanel::m_chartGuideLines 从未纳入逐视频状态。修复：ChartGuideData（domain 可序列化）入 VideoState，切走保存/切回恢复/无状态清空 |
| `5c9ac46` | 视频列表 fps 翻倍（15→30） | cv2.CAP_PROP_FPS 与 avg_frame_rate 都读容器元数据，部分 DVR 文件元数据就是错的。修复：两侧实测前 48 帧 PTS 节奏（中位间隔，偏差 >4% 以实测为准）；引擎侧校准同时修好逐帧步进与图表时间轴。元数据正确文件不受影响（已验证） |
| 本次 | 辅助线 UX 重构 | ① 去掉 QInputDialog 弹框：右键菜单"添加水平/垂直辅助线"直接取鼠标位置换算（水平=鼠标 Y→亮度值，垂直=鼠标 X→时间，垂直原先用播放光标时间）② 标签文本移入 drawChartGuideLines 每次 draw 刷新 → 拖动时数值实时联动（原只在创建时设置）③ 水平辅助线双标签：左侧=亮度值（左 Y 轴），右侧=响度 dB 值（右 Y 轴同一像素等效值，无音量轴时隐藏）④ 响度标签绘制在图表内侧右对齐，不遮挡右 Y 轴刻度 ⑤ 多条水平辅助线间右侧加 ▲ 响度差值（dB 差绝对值，与左侧 △ 亮度差值对应） |
| 本次 | 文件夹改名构建修复 | 用户改名 V1.0 remake → LumenArc_v1.0 remake 使 CMakeCache 路径失效：build 目录已按新路径重建（内置 python/ffmpeg 保留） |

---

## 十二、Scrub 拖拽顺滑化两轮迭代 + 音频无声排查（2026-08-02）

> 用户场景：在图表/语谱上拖拽光标观测局部亮度在十几分钟跨度上的变化。
> 初始症状：画面不规则跳跃兼卡顿；迭代后变为"顺滑一会→冻屏→再顺滑"。
> 战场文件：`明景拼接视频（）长72528.mp4`（1440p H.264 23.98fps AAC 48kHz，**2h/10.2GB**）。

### 12.0 问题文件关键实测数据

| 文件 | 分辨率/fps | 可 seek 关键帧 | 解码吞吐（catchup-bench） | 备注 |
|---|---|---|---|---|
| $RCR79YF/*.mp4（恢复文件） | 720x480 15fps | ~15s/个（40 个/10min） | ~7900fps（全 GOP 追赶仅 ~28ms） | AAC 8kHz mono；前 2 分钟内容近乎静音（max -48dB） |
| D17_...17015190.mp4 | 2560x1440 25fps | ~5-10s/个 | 软解 ~3140fps（10s GOP 追赶 ~80ms） | HANDOVER 老测试文件 |
| 明景拼接视频（）长72528.mp4 | 2560x1440 23.98fps | **每 10s 一个 IDR**（stss 只索引 IDR；ffprobe 看到的 ~1s"关键帧"是开放 GOP 恢复点，不可 seek） | ~750fps（≈31× 实时，demux 上限 ~800pkt/s） | **超 31× 的拖拽物理上必靠 seek 跳显** |

### 12.1 两层匀速化（第一迭代）

- **第一层·输入侧量化**（chartpanel.cpp `quantizeDragTarget`，仅 ChartPanel）：
  目标速度 EMA（0.5/0.5）→ 步长 `n = clamp(v×25ms/frameMs, 1, 256)`，seek 目标量化到
  `n·frameMs` 网格（锚点=拖拽起点，步长切换时平移锚点保持相位连续），相同目标去重不 emit。
  慢拖严格逐帧均匀，快拖等距跳帧。`setFrameDuration()` 由 MainWindow::onDurationChanged 注入。
- **第二层·输出侧节拍闸**（ffmpeg_video_engine.cpp scrubChaseMainFrame）：
  显示由"解码事件驱动"改为"墙钟节拍驱动"（`SCRUB_DISPLAY_INTERVAL_MS=25`）。
  闸未开：软解帧静默入滚动缓存（超前量 `SCRUB_LOOKAHEAD_MS=800` 有界后交外层，由缓存命中路径服务后续拍）；
  硬解帧持帧睡到下一拍。进入 scrub 复位闸门（首帧零延迟）。
  **UI 背压自适应**：丢帧（inFlight≥2）一次节拍 +5ms（上限 +20ms），成功显示逐帧衰减——
  生产速率自动降到 UI 可持续水平，防"光标在动画面冻结"。

### 12.2 关键 Bug 修复（按发现顺序，均为实测/日志驱动）

1. **语谱拖拽从未进入 scrub 模式**（最大单点收益）：`MainWindow::onSeekFromChart` 的拖拽判断
   只查 `m_chartPanel->isDraggingCursor()`，语谱拖拽全部走 50ms 节流一次性 seek
   （每拍全量 flush 重定，无追赶无缓存）。修复：判断改为图表||语谱；
   `SpectrogramPanelEnhanced` 补 `scrubEnded` 信号（松手退出 scrub + 精确落位）。
2. **片段音频收割阻塞追逐循环**（"时好时坏"主因）：每次显示后收割 100ms 音频窗口需读 ~80 包
   （≈100ms 墙钟）+ sink reset/start 每 120ms 一次。修复：引擎侧目标速度 EMA >4× 时
   关闭片段音频与收割（`m_scrubSnippetAudio`）；慢拖保留。
3. **窗口显示无上界**：目标回退时窗口条件 `relMs >= target-halfFrame` 会显示超前 1.3s 的帧
   （快进感）。修复：`relMs > target + 2*halfFrame` 交外层后退重定位。
4. **关键帧跳显（hop）双通道自适应**：
   - `sparseGop` = 实测 GOP（reseek 落点间距 `m_gopLearnMs`）> 6s 且追赶墙钟 EMA > 120ms
     （真慢解码文件；D17 软解 80ms 不进此通道）；
   - `denseVelHop` = 目标速度 > 30×（实测吞吐上限 ~31×，超出后 decode-through 物理追不上）。
   - 防回归细节：墙钟只在 GOP 已知后采样、软解切换重 seek 后重新计时
     （否则一次性硬解慢样本把 sparseGop 锁死）；`m_gopLearnMs` 只在 reseek 落点更新
     （直追期的 `m_lastCatchupMs` 是"目标在 GOP 内偏移"噪声，不可用于判定）。
   - **跳显抑制锚点**：目标未越过"上一跳目标+实测 gap"（≈下一关键帧）前不重 seek——
     防同关键帧 seek+重复显示风暴；**拒绝也要设锚点**（见第 5 条教训）。
   - **同帧去重**：落点=当前显示帧时免 sws/emit/重绘。
5. **终局根因（日志实锤）：hop-reject 风暴**。拼接视频 seek 落点全是整 10s IDR 网格，
   误差普遍 2~11s，固定 2s 误差闸把跳显全部拒绝 → 无显示 + 同帧 seek 循环
   （用户日志：reseeks=198/2s、同 (rel,target) 1ms 内重复 10-20 次、displays=5/2s、maxGap=1361ms）。
   修复：**误差闸随速度缩放 `errCap = max(2000, v×100ms)`**——100× 快拖落后 10s 时间轴
   = 100ms 墙钟延迟，人眼不可察；慢速自动收紧。
6. 效果（2h 文件无头仿真）：75× 与 250× 全程扫动均稳定 ~35fps、maxGap ≤ 79ms、0 hop-reject；
   用户实测慢拖冻屏率可接受、快拖冻屏消除。

### 12.3 音频无声排查（$RCR79YF 结论）

- 现象：该文件夹视频在 app 内无声，别的播放器有声，别的视频在 app 内有声。
- 排查（全部在你本机对你的文件实测）：`audio`（字节实时速率入声卡）、`avsync`（偏差 84ms）、
  新增 `scrub-then-play`（拖拽 30 次后播放，设备时钟正常）、新增 `audio-peak`
  （复刻解码→swr→s16 通路分级测电平，输入输出成比例）——**引擎全链路无罪**。
- app 日志实锤：`play: state=0 err=0 written=... outPeak=18618 (-4.9dB)`——
  -5dB 真实电平已进入 Windows 音频会话（Realtek 扬声器，8kHz 不被声卡原生支持，
  自动回退 48kHz 立体声重采样正常）。
- **结论**：问题在 Windows 侧——待用户确认音量合成器 LumenArc.exe 条目（静音/音量）
  与按应用输出设备路由。文件自身特性：开头 2 分钟近乎静音（max -48dB），易误判。

### 12.4 临时诊断代码（定位完成后应移除）

- `ffmpeg_video_engine.cpp` 顶部 `audioDiag()` → 写 `%TEMP%/lumenarc_audio.log`；
  埋点：ensureAudioOutput/sink start/openFile/cmd Play/背压超时/play 2s 电平。
- `diagScrubTick/diagScrubFlush/diagScrubDisplay`：`scrub 2s:` 周期统计
  （displays/cacheHits/reseeks/uiDrops/maxGap/inFlight）+ 显示点追踪
  （`display[cache|window|hop|hop-reject|eof-drain]`）。
- 引擎公有诊断接口 audioBytesWritten/hasAudio/audioClockMs/hardwareDecodeActive 保留（测试用）。

### 12.5 测试场景新增与断言更新（tests/engine_test_main.cpp）

| 场景 | 用途 |
|---|---|
| `audio-peak <file>` | 分级测量解码/swr 电平，定位无声环节（复刻引擎通路含设备回退） |
| `scrub-then-play <file>` | scrub 后播放音频是否存活（设备时钟断言，bytesWritten 不可信——sink 空写也计数） |
| `scrub-sweep <file> [steps] [pumpMs] [startPct] [endPct]` | 长距离连续拖拽仿真：帧数、平均/最大位置步进；配合引擎 diag 日志看 maxGap/reseeks/uiDrops |

断言与引擎语义对齐：`scrub-playback`/`scrub-backward` 追踪容忍 = 精确帧 ±3s 或
"落后（不超前）目标 ≤ v×100ms"（>30× 跳显是预期行为）；oscillate 容差不变（慢拖必须精确）。

### 12.6 遗留事项

1. Windows 音量合成器/输出路由待用户确认（12.3）。
2. 移除 12.4 临时诊断（确认音频问题解决后）。
3. 若 UI 背压（uiDrops 高）在 1440p+ 上仍频发：视频绘制改 GPU 路径
  （QOpenGLWidget 纹理上传替代 QPainter CPU 缩放，~10ms/帧 → ~1ms）。
4. 调参速查：节拍 25ms（自适应 +0~20ms）、超前量 800ms、片段音频闸 4×、
   跳显速度闸 30×、误差闸 max(2000, v×100ms)、sparseGop(GOP>6s 且墙钟 EMA>120ms)。


---

## 十三、前处理板块：方案评审修正 + M0 验证 + M1-M5 全量实施（2026-08-02）

> 任务链：评审 `docs/PREPROCESSING_TECH_DESIGN_CN.md`（多视频智能排序/无损拼接/统一转码）
> → 修正方案 v1.0→v1.1（13 项评审意见）→ 按里程碑 M0-M5 实施并全部落地。
> 设计文档为单一事实来源，本节只记录**实施结果与现场知识**。

### 13.1 批次提交（7 个，全部构建/测试绿）

| commit | 内容 |
|---|---|
| `9c97072` | refactor：`trustedDurationFor` 下沉——`IAnalysisEngine` 新增 `VideoTiming/trustedDurationMs`（引擎中立，R2），`detectPythonPath` 移入 `PythonAnalysisEngine`（static），MainWindow 时长路径不再 qobject_cast（剩 4 处 cast 为 python 路径注入配置代码，存量债务） |
| `2ba4776` | feat domain：`probe_result/ocr_result/sort_model/preprocess_task` + `filename_timestamp`（M1-M5，**M5 先于 M2** 否则毫秒被截胡）+ `smart_sorter`（证据分层/分组/连续性/矛盾裁决，纯函数）+ `concat_precheck`（OK/WARN/BLOCK，**mjpeg 移出 MP4 白名单**）+ `evidence_report`（RFC4180 CSV+BOM）+ `preprocess_text.h`（CSV/concat 转义、进度解析、creation_time 脏值过滤） |
| `772c97f` | feat 引擎：`MediaProbeEngine`（进程内 libavformat，QThreadPool×4，内容嗅探伪 MP4，首包 PTS/关键帧，时长交叉验证）/`TimestampOcrEngine`（**接口签名无 python/ffmpeg 路径**，R-2）/`ConcatEngine`（concat demuxer 流拷贝，可选逐段 `-output_ts_offset` 归一化）/`TranscodeEngine`（libx264 veryfast CRF18，`.part.mp4` 临时名→原子改名）+ `probe_timestamps.py` |
| `7f5f20b` | feat app+ui：`PreprocessingCoordinator`（状态机 SSOT：Idle→Probing→Ocr→Sorting→UserConfirm→Precheck→Transcoding⇄Concat→Done/Failed/Cancelled；OCR 缺失降级续跑不静默）+ `PreprocessPanel`（四步向导，与视频列表同区标签页）+ MainWindow 挂接（`tabifyDockWidget` + 引擎接口注入） |
| `e34bdd9` | test：`lumenarc_preprocess_test`（64 断言）+ `lumenarc_preprocess_integration`（29 断言，真实 ffmpeg/Python 子进程）；**MSVC 全局 `/utf-8`**（全部源文件为无 BOM UTF-8，此前中文字面量靠本机 locale 侥幸正确） |
| `40abd96` | chore：rapidocr_onnxruntime **三处同步**（build-win64.yml / build.yml / build-mac.yml 的 bundled python pip 行 + setup_python_deps.bat）；THIRD_PARTY_LICENSES 补 RapidOCR(Apache-2.0)/onnxruntime(MIT)；**删 setup_deps.py**（VLC 下载脚本死代码，R10） |
| `4b87a5a` | docs：方案 v1.1（R-1~R-13 修订记录）+ `tools/m0_synth_benchmark.py` + MANUAL 第十三章 + README 功能表 |

### 13.2 M0 决策门结果（合成矩阵 8/8 = 100%，deltaMs 全 0）

`tools/m0_synth_benchmark.py --ffmpeg build/Release/ffmpeg/ffmpeg.exe`：
正常×3 / yuvj420p / 噪声(CRF35) / 中文日期 OSD / 伪MP4(TS改名) / 截断TS，全部命中。
**合成全绿 ≠ 现场验收**（方案 R-12）：≥90% 决策门须由火调队用发布包内
`probe_timestamps.py` 跑 ~30 段真实素材；2GB 级无索引文件尾帧 seek 验证（§12.5）同待现场。

### 13.3 OCR 管线四个真实工程坑（全部已修，勿再踩）

1. **输入侧 `-ss` 在 TS/伪 MP4 上落点偏移 9 秒**（字节插值）。修法：取帧命令带
   `-vf showinfo`，解析 `pts_time`，**实际位置 = ss + pts_time**——证据帧 relMs 永远为实测真值，
   投票推导的墙钟起点不再被 seek 精度污染。输出侧 `-ss` 同样平移时间轴（pts_time≈0）。
2. **RapidOCR 把单行 OSD 拆成多框**（"2024-07-01" + "12:00:01" 分离 → 正则全废）。
   修法：`merge_ocr_lines` 按基线 y 聚类、x 排序拼接成整行再解析。0%→100% 的决定性修复。
3. **CJK 日期超出 30% 裁剪宽度**（"2024年07月01日 12:xx" 被切）。修法：追加 60% 宽裁剪通道
   （窄增强→窄二值→宽增强三级链，首个解析命中即止）。
4. **mjpeg 编码器拒写全色域 YUV**（yuvj420p 源，"Non full-range YUV is non-standard"）。
   修法：证据帧一律 PNG（无损且免去 strict 标志）。

投票阈值校准：RapidOCR 在干净 OSD 上 conf≈0.85-0.9，单帧门槛从 0.9 降到
`SINGLE_HIT_MIN_SCORE=0.85`（得 conf=0.7 中置信），≥2 帧一致仍 0.95。

### 13.4 集成测试运行方式

```
build/Release/lumenarc_preprocess_test.exe            # 64 断言 headless
build/Release/lumenarc_preprocess_integration.exe <clips_dir> <out_dir> 1719835200
# clips_dir 由 m0_synth_benchmark.py --keep 生成；exe 须在 build/Release
# （依赖旁置的 probe_timestamps.py / python/ / ffmpeg/）
```

### 13.5 drawtext 合成素材转义知识（再生测试素材必备）

- fontfile 盘符冒号要过两层反转义（graph 层 + filter-args 层）：argv 层需恰好 **2 个**反斜杠，即 `C\\:/Windows/Fonts/...`（Python 源码写 `"\\\\:"`；实测 1 个或 4 个均失败，偶数 ≥2 可行——切勿凭文档想当然，以实测为准）；
- `%{pts\:gmtime\:EPOCH\:%T}`：扩展参数按 `:` 分割且**不认反斜杠转义**，FMT 内含冒号
  必报 "requires at most 3 arguments"——用 `%T`（本构建 strftime 支持）而非 `%H\:%M\:%S`；
- text 值整体单引号包裹（无 shell 时 argv 直传）。

### 13.6 打包注意

- Release 体积增量实测 **约 +56MB**（onnxruntime 42.5MB + rapidocr 13.2MB + 传递依赖 ~5MB），
  方案原估 +25MB 已修正；CI 三处 pip 行已同步，发版无需手动操作。
- 本机 `build/Release/python/` 已手动装 rapidocr（embeddable 无 ensurepip，走 get-pip.py
  引导，同 CI）；**clean 重建后需重装**（同第十一章内置 python 注意事项）。
- `probe_timestamps.py` 已纳入 CMake POST_BUILD 拷贝（与 analyze_video.py 同模式，规范 P5）。

### 13.7 遗留事项（按优先级）

1. **现场决策门**（13.2）：30 段真实素材 OCR 成功率 + 2GB 无索引尾帧 seek。
2. **report.html 未实现**：设计 §9.1 要求 HTML 报告（内嵌缩略图），当前仅 CSV+截图+日志
   已落地。Coordinator::finalize 留好了挂点（evidence dir 迁移后写文件处）。
3. **排序微调用 ↑↓ 按钮**，未做 QListWidget 式拖拽（设计 §8.2 的"复用 VideoListPanel
   拖拽模式"列为 UX 增强候选；`applyGroupOrder` 接口已支持任意重排）。
4. **时间戳归一化路径已实现但无集成测试**（需重叠段素材；ConcatEngine 两段式：
   逐段 `-output_ts_offset` remux → 再拼）。
5. 发版前手工点检清单需补前处理四步流程（规范 7.4，UI 不做自动化）。
6. 转码 60 分钟超时/磁盘预估系数（1.2）写死在代码里，v2 参数面开放（方案 §5.5.2/§10.2）。


---

## 十四、现场反馈修复 + 前端重设计（2026-08-02 第二批）

> 用户拿 `$RCR79YF` 真实恢复素材（10 段 720x480 15fps，含 75s 截断段）实测后反馈：
> ① 同一摄像头被"分到不同分组"；② 看不到尾帧 OCR 结果，无法判断能否拼合；③ 前端难用。

### 14.1 真实素材验证结论（重要基线）

- **OCR 管线真实通过率 10/10**（首帧+尾帧全部识别，conf 0.95，截断段尾帧在 74s 处也命中），
  232s 处理 10 段（workers=4）。现场决策门（第十三章遗留①）在这批素材上事实通过。
- **分组规则实为"通道号制"**：`smartSort` 只按文件名通道号（CH01/IPC 模式）或人工指定分组，
  **时间缺口不会拆组**（仅生成 Gap 警告）。`02-04-52_6m.mp4` 类文件名无通道信息 → 全部进
  （默认组）。`lumenarc_preprocess_test --group-debug <files...>` 可用生产代码复算分组
  （本批次新增的诊断模式）。用户所见"不同分组"无法复现，疑似把缺口警告行误读为分组边界。
- 参数全一致（h264/yuv420p/720x480/15fps/AAC 8k）→ 走无损拼接，单输出。

### 14.2 尾帧证据可见性修复（commit `a8a2fe1`）

根因：引擎层一直解析尾帧（`OcrResult.wallEndMs/lastFrameImg`），但 UI 只有缩略图无时间文字；
且 `smart_sorter.cpp` 把截图/原文绑定在 `wallStartMs>0` 分支内——**首帧解析失败会连带丢弃
尾帧证据**。修复：SortEntry 增加 `ocrEndMs/rawStartText/rawEndText`，证据材料与首帧解析成败
解耦；表格加尾帧时间列（实测值或 `~` 推算值）、OCR 原文 tooltip、缺口/重叠量值入状态列、
分组规则入标签 tooltip 与日志。单测 64→72。

### 14.3 前端重设计（commit `62a1c76`，方案 docs/PREPROCESSING_UI_REDESIGN_CN.md）

诊断：入口隐形（dock 标签页）/流程无导航/信息形态错（表格 vs 时间线心智）/术语技术化/结果无出口。

落地（用户拍板：**独立窗口**；无时间片段**末尾标 "?"**）：
- `src/preprocesswindow.cpp`（~1050 行）：四幕向导+步骤条（可回退，执行中锁定）；
  ①拖放导入+探测进度 ②结论一句话+时间线+证据卡片 ③自动判定拼接方式+磁盘预估+折叠技术详情
  ④大进度条+ETA+结果卡片（打开输出文件夹/查看证据报告）。
- `src/cliptimelinewidget.cpp`（~230 行）：块位=墙钟、宽∝时长；缺口灰纹+"缺口 4:21"；
  重叠红影；点击↔卡片双向联动；无墙钟末尾等宽 "?"。
- 卡片 246px：首/尾帧 230px 图、起止时间（尾帧 OCR 实测优先，`~` 标推算）、状态徽标、
  双击大图+手输、**QDrag 拖拽换序**（同组内，落点指示条，←/→ 兜底）。
- 入口：工具栏右侧金色按钮「素材整理拼接」+ 文件菜单 Ctrl+M；WA_DeleteOnClose 每次新会话；
  执行中关闭弹确认并 cancel（引擎析构均 cancel 子进程，无悬空）。
- 旧 `preprocesspanel.*` 已删（R10）；MANUAL 第十三章重写为新流程。

### 14.4 遗留事项（更新）

1. ~~现场 OCR 决策门~~：$RCR79YF 批次已过；其他型号 DVR 字体仍建议按 30 段抽检。
2. **报告 HTML 版未实现**（CSV+截图+日志已落地，Coordinator::finalize 有挂点）。
3. 2GB 级无索引尾帧 seek 验证仍待现场大文件。
4. 时间戳归一化路径仍缺集成测试（需重叠段素材）。
5. 发版手工点检清单需补新版四幕流程（窗口打开→导入→校对→拼接→报告出口）。
6. 分组若真出现多组（含通道号文件名），卡片支持组内拖拽，跨组移动需走 applyGrouping
   （UI 未暴露，v1.2 有意隐藏通道概念）。


---

## 十五、Dahua .dav 支持：GBK 崩溃根因 + 流内绝对时间证据层（2026-08-02 第三批）

> 用户反馈 `20260722广州增城/.../06` 目录下 12 个 .dav "没法截出来，时间帧全错"。

### 15.1 根因链（两层，逐一定位）

1. **GBK 编码崩溃（致命）**：嵌入式 Python 子进程 `text=True` 用本机 locale（GBK）
   解码 ffmpeg stderr；DAV 路径含中文（广州增城/监控视频/…），UTF-8 字节喂给 GBK
   解码器 → reader 线程 `UnicodeDecodeError` → `r.stderr=None` → 脚本在时长预取
   阶段整体崩溃 exit 1 → OCR 全无 → 排序退化为 mtime（导出时间 7-21 22:0x）→
   "没法截出来 时间帧全错"。修复：`probe_timestamps.py` 全部 subprocess 调用改
   `encoding="utf-8", errors="replace"`；`_force_utf8_io()` 强制 stdout/stderr UTF-8；
   C++ 引擎启动参数加 `-X utf8`（双保险）。
2. **OCR 仍全部失败（真实素材差异）**：该摄像头 OSD 在右上，RapidOCR 把日期行切碎
   误读（"2026-07-22 06:00:03" → "12026-07"+"06:00:03"），完整日期正则全不中。
   帧提取本身正常（含 dhav 尾帧），属识别层最佳努力范畴。

### 15.2 关键发现：DHAV 流内绝对起始墙钟（新证据②'）

`dhav` demuxer 暴露 `start_time` = 录制时刻 epoch 秒（1784700002 ↔ 文件名/OSD
06:00:02）。**时区约定**：DVR 把本地墙钟当 UTC 秒写入 → 取 UTC 分量按本地时间
重解释（与 OCR 本地墙钟语义对齐，实测一致）。落地：
- `ProbeResult.absStartEpochMs`（仅接受 2000-01-01 ~ 当前+1天，防垃圾 PTS 误判）
- 排序证据层级：OCR(1.0) > 文件名(0.8) > **absStart(0.6)** > creation(0.5) > mtime(0.2)；
  持 absStart 的组不标 suspicious；OCR↔absStart 偏差>2min 出 EvidenceConflict（不改序）
- `SortEntry.sourceKind`（枚举 int，UI 映射文案：画面时间/人工/文件名/流内录制时间/
  拍摄时间(元数据)/文件修改时间）——顺带修正了"非 OCR 证据一律显示需人工输入"的旧 UX 误标
- 证据报告 CSV 新增"流内起始墙钟(派生)"列
- Dahua 文件名 `HH.MM.SS-HH.MM.SS[...]` 是纯时间段（无日期/通道），**刻意不解析**

### 15.3 实测结果（12 段 dav 全链路）

探测 12/12 ✓ → 单组 suspicious=0、时间完美衔接（06:00:02+133s=06:02:15 链式连续、
无警告）→ precheck BLOCK（dhav/hevc/1fps↔15fps 混合）→ 转码 MP4（h264/yuv420p，
1fps 段保持 1fps，PTS 正确）→ 混 fps 流拷贝拼接验证通过（133s+51s→185s，
Non-monotonic DTS 警告无害）。诊断工具：`--probe/--sort <files...>`（集成测试二进制）、
`--group-debug`（单测二进制）。

### 15.4 遗留

- 混 fps 段拼接的输出为变帧率 MP4（PTS 驱动播放正确）；如需恒定帧率导出（仲裁/演示）
  后续加 `-r` 统一选项（会帧复制 1fps 段，体积膨胀 15×）。
- 音轨 copyAudio 策略与方案 §5.5.1 "48k 立体声统一"存在偏差（8k/16k DVR 音轨直拷保留），
  属既有偏差，未在本批处理。
- 报告 HTML 版仍未实现；2GB 无索引尾帧 seek 现场验证仍挂起。


---

## 十六、性能与可用性三连修（2026-08-02 第四批）

> 现场反馈：①拼接输出文件"拖入播放后消失找不到"；②读帧排序慢得令人发指（要求 10×）；
> ③排序期间主窗口亮度分析从秒级饿殍到 5 分钟。

### 16.1 问题①（实为找不到，文件未丢）

取证：`$RCR79YF/LumenArc_Merged_20260802_1938/_默认组__concat.mp4`（203MB）安在；
dav 批次 19:41 会话因 GBK 崩溃 OCR 全败、用户 11 分钟后取消（operations.log 实锤），
根本没有产出文件。修复：默认组输出名 `_默认组__concat.mp4` → **`merged_concat.mp4`**；
结果卡片新增**「在主窗口播放输出」**（PreprocessWindow::openOutputRequested →
MainWindow::openVideoFile，信号解耦 R2），消除"拖入"动作。

### 16.2 问题②：OCR 速度（实测 88×/1.7×）

- **absStart 跳过策略（主修复）**：`setSkipOcrWhenAbsStart(true)`（默认开，①幕可关）——
  absStartEpochMs>0 的文件走 `--frames-only-json`：仅截 head_1s+tail_eof 证据帧、零推理。
  实测 3 个 dav：132.3s → **1.5s（88×）**；12 段批 8.7min → ~6s。OcrResult 仅带截图，
  排序仍由 absStart 证据驱动（frames-only 写失败退化为全量 OCR，不静默）。
- **全量 OCR 提速（次级）**：帧降采样至 1600 宽（det 成本∝像素）、宽裁剪兜底收敛为
  2 个上角、bonus eof 仅单遍窄增强、模型加载惰性化（frames-only 批次不加载）。
  失败路径 132.3s → 76.4s（1.7×）；成功路径（常态）2 帧 2 裁剪 ≈ 3-5s/段。
- 推理改单线程（intra/inter_op_num_threads=1，RapidOCR 不支持时回退默认）——
  牺牲 ~2× 单吞吐换主窗口隔离（见 16.3），失败路径属病态素材的固有浪费。

### 16.3 问题③：主窗口饿殍（进程优先级隔离）

根因：4 worker × onnxruntime 默认全核线程池 → 线程过订阅 + 同优先级抢占。
修复：OCR/转码/拼接子进程一律 `CREATE_BELOW_NORMAL_PRIORITY_CLASS`（Qt
`setCreateProcessArgumentsModifier`，qt_windows.h 未导出该常量，回退定义 0x00004000）；
OCR 进程环境 OMP/OPENBLAS/MKL_NUM_THREADS=1。Windows 调度器保证主分析进程优先，
I/O 优先级随进程级一并降低。

### 16.4 遗留

- 全量 OCR 失败路径 76s/3 段仍慢（病态 OSD 的固有成本）；如需再压可加"首帧零命中
  即跳过其余帧"的激进早退，但会牺牲开机黑屏素材的检出率，暂未做。
- 问题①若用户实际遇到的是"文件真的被删/播放报错"，待其进一步描述后复查
  （当前证据：文件在、可探测、命名与出口已友好化）。


---

## 十七、流程非强制化 + 假成功修复（2026-08-02 第五批）

> 现场反馈：①排序完显示"完成"但没有拼接结果；②自动排序/转码不应是强制步骤——
> 拖进去用户自己在列表排完即可快速无损拼接，转码只处理确实需要的单个文件。

### 17.1 问题①根因（C3 违规，已修）

`onTranscodeOneFailed`/`onConcatOneFailed` 只记录失败并继续队列，`finalize()` 原先
**无条件** `emit finished()` → UI 无脑绿勾"✓ 拼接完成"。转码/拼接全败时
`m_concatOutputs` 为空 → `report.outputPath` 竟指向输出**目录**（0 B）→ 播放按钮无效。

修复：`finalize()` 在 `m_concatOutputs.isEmpty()` 时改为 `emit failed(ConcatFailed)`
（附失败统计明细）；UI `onFinished` 额外校验 outputPath 是**存在的非空文件**，否则
显示"⚠ 未产出输出文件"红色卡片且播放按钮禁用；`startProcessing` 阶段不符不再静默
return（写日志）。

### 17.2 问题②：排序/转码非强制

- **begin 不再强制 OCR**：`begin()` 探测完成后直接按**导入顺序**成组（`buildListOrderGroups`，
  默认组，时间未知）→ UserConfirm。新增 `runAutoSort()`（UserConfirm 可选触发）与
  `beginWithAutoSort()`（一键自动排序链）；OCR 引擎降级/absStart 跳过逻辑移入 `runAutoSort`。
- **导入页一键拼接**：表格行支持拖拽排序（`InternalMove` + `rowsMoved` 同步
  m_pendingFiles）；「开始拼接 ▶」= begin → evidenceReady(列表顺序) → confirmOrder →
  precheckReady → startProcessing 自动链（`m_pendingQuickMerge`，不切校对/设置页）；
  探测已完成时（UserConfirm）直接复用会话。设置页「开始拼接」原路径保留。
- **逐文件转码判定**：新增 `filesNeedingTranscode()`（domain）——白名单外/探测失败/
  编码与组基准不一致 → 仅该文件转码；分辨率/像素/音轨/帧率大偏差 → 整组转码。
  取代原"组级 BLOCK → 全组转码"。dav 批次（hevc 同参，1fps/15fps 混）仍整组转码（fps
  偏差），mp4 混入单个 avi/mjpeg 时只转那一个。磁盘预估与设置页文案同步改为逐文件口径。
- 单测 99→106（testFilesNeedingTranscode 7 项），集成 29/29，vla 回归绿。

### 17.3 遗留

- 转码输出固定 h264/yuv420p，**不统一分辨率/帧率**：组内分辨率不一致时转码后 concat
  仍可能失败（concat demuxer 限制）。单相机批次（同参）不受影响；跨相机混拼需后续在
  TranscodeEngine 增加 scale/fps 统一参数（v2）。
- 直接拼接的报告按"人工顺序"记录（sourceKind=None），证据 CSV 如实呈现。


---

## 十八、拖拽连续帧三连根因 + 拖拽视觉/删除/列表（2026-08-03）

> 现场反馈：①dav 转码拼接产物（55min/2.1GB/1fps+15fps 混合）拖拽"完全没有连续帧"；
> ②拖拽动画要整行半透明；③导入列表要 Del 删除；④"主窗口播放输出"要进视频列表；
> ⑤频谱长视频 X 轴仍糊。

### 18.1 拖拽无连续帧（三连根因，全部实测定位）

1. **ffmpeg nightly 的 seek 回归**（N-125752-20260724）：`avformat_seek_file(fmt,-1,…)`
   用 AV_TIME_BASE 换算严重超前——merged 视频 seek 1500s 落 3307s（ffmpeg -ss 实测同样）。
   修复 `seekToRelMs()`：显式视频流 + 流时基 pts → 误差 ≤1 GOP（~2.5s），
   seek-matrix 实测 err 36-560ms。三处 seek（scrub/加载倒回/prefetch）统一走此路径。
   注：该回归是 bundled ffmpeg nightly 特有，稳定版无此问题；引擎层修复后无需换库。

2. **跳显误拒**：关键帧跳显的 err 用"显示时刻最新 target"计算——快拖 794× 时
   解码 70ms 内光标已前移 83s → err=96s > errCap → 全部 hop-reject → 不显示。
   改用 seek 发起时目标（m_chaseSeekTargetMs）判定；gopLearn 同样用 seek 时目标
   （原被污染成 96s）。

3. **chase 解码器选择**：gopLearn>4000 门槛导致选了软解（2304×1296 IDR 软解
   ~110ms/帧）；改硬解优先（m_vdec，IDR ~5ms，flush 实测仅 0-11ms），软解仅兜底。
   另：scrub 显示降采样 1280 宽（回传+sws 成本），快拖跳显后立即返回（decode-through
   追不上跑远的目标，纯浪费）。

**结果**：引擎 scrub-sim（794× 快拖 2.5s）：1 帧 → **80 帧，avgGap 29ms（34fps）**。
慢拖（vel<30）走 decode-through 精确帧路径不受影响；seek-matrix/play/集成全绿。

### 18.2 拖拽视觉与列表

- 整行原比例 72% 半透明拖拽卡（18.1 同批）；**金色插入线**（3px，目标行上/下缘）
  与校对页卡片拖拽一致；拖拽期吞 MouseMove 去掉 Qt 默认 current 蓝框。
- **Del/Backspace 删除选中行**（空闲阶段），同步 m_pendingFiles 与按钮状态。
- **「在主窗口播放输出」进视频列表**（hasVideo 去重，仅新文件跑 videoTiming）。
- 频谱 X 轴刻度算法提取为 domain/tick_utils.h `computeXAxisStepMs()`，35 项单测
  （10min~48h×宽度，≥72px 间距性质），166/166。

### 18.3 遗留

- 引擎 scrub-sim 模式保留（`lumenarc_engine_test scrub-sim <file> [ms]`）作回归。
- bundled ffmpeg 为 2026-07-24 nightly；后续可换稳定版消除 seek 回归（引擎已免疫，
  换库属可选优化）。
