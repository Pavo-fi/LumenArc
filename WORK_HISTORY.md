# LumenArc 工作历史存档（WORK HISTORY）

> **本文件是 HANDOVER.md 的历史存档，按修改顺序（时间序）只增不改。**
> 存档规则：见 HANDOVER.md 表头「规则 R2」——HANDOVER.md 只保留最近 5 次
> 更新，超出部分整体移入本文件末尾；每次写完两份文档后同步更新两边表头。

> **当前存档范围**：2026-07-28 ~ 2026-08-14 晚（第〇~二十三章 + 2026-08-13
> 晚批 + 深夜批：项目规则 R1/R2、交接摘要、问题 A/B 排查与定案、音频取证、
> 音频时间轴对齐、播放选项包拍板、技术债）
> 内容索引：v0.5→v1.0 架构升级（分层/红线 R 规则/P0 修复）· FFmpeg 播放引擎
> （线程模型/时钟/seek 三段式/性能数据）· 音频管线 · 拖拽预览演进（追逐模型/
> VLC 路线）· 测试体系与 28 项矩阵 · 已知技术债 · 构建部署与 CI · 升级计划表
> （v1.2~v1.9 排期）· 前处理板块 M0-M5 · 现场反馈与前端重设计 · DAV/GBK 根因 ·
> 性能三连修 · 流程非强制化 · 拖拽三连根因 · GO 复合键 · 校时深化与时间重建
> v1.2.x（含 08-12 救复四连修）· v1.2.2 收尾 · v1.3.0 案件模块 M1-M3 全记录 ·
> v1.3.1 封版自检与 08-14 上午修复系列（§23.11~23.23）
>
> **最近归档动作**：2026-08-20——HANDOVER 第四十六批（§55，P-59 校时落盘
> 双根因修复）移入本文件末尾（R2 限 5 批）；同日早前：第四十五批（§54）。
> 早前：2026-08-16——HANDOVER 第五批（§14）至第二十九批（§38）
> 共 25 批整体移入本文件末尾（原文未删改；归档前 HANDOVER 表头自第十一批
> 后长期未同步，随本次一并修复）。
> 早前：第十一批归档第六批（§15）；第十批归档第五批（§14 旧文）；第九批
> 归档第四批（§13）；第八批归档第三批（§12）；第七批归档第二批（08-13 深夜
> §9-11）；第六批归档第一批（08-13 晚 §0-8）；第五批整体切分第〇~二十三章。

---

# LumenArc v1.3 工作交接文档

> 编制日期：2026-07-28（2026-08-13 v1.3.0 M2 完成后更新）
> 最新标签：`v1.2.2`（2026-08-12 收尾封版：GO 预检第三点确认 + toast 通知 + ui_chain 入 CMake + 版本对齐）
> 提交历史：`77c8e44`→`abd7b11`→`ff4c9a4`→`adb429b`→`5bbb59b`→`6b61a8d`→`25efc66`→`d978496`→`f496680`→`4447ca7`→`66b2b8f`→`0306427`→`e812e0c`→`f073804`→`16a64de`→`d9cc07a`→`977cb96`→`f9141f8`→`9d1230a`
> 前处理批次：`9c97072`→`2ba4776`→`772c97f`→`7f5f20b`→`e34bdd9`→`40abd96`→`4b87a5a`（见第十三章）
> 现场反馈批次：`95f6dba`→`a8a2fe1`→`62a1c76`（见第十四章）
> DAV 批次：见第十五章（GBK 编码崩溃根因 + DHAV 流内绝对时间证据层）
> 性能批次：见第十六章（absStart 跳过 88×、子进程低优先级、merged 命名+播放出口）
> 流程批次：见第十七章（非强制一键拼接、逐文件转码、假成功修复）
> 拖拽批次：见第十八章（seek 回归三连修、34fps 拖拽、金色插入线、Del 删除、列表收录）
> 校时管线救复批次（2026-08-12）：`b7040eb`→`b87b68f`→`683c0d2`→`4a7a23f`
>   （见第二十一章 21.6~21.9：重建死锁/中文路径/框选链路/星期 OSD 解析）
> v1.2.2 收尾批次（2026-08-12）：见第二十二章（预检第三点确认/toast/ui_chain CMake/版本对齐）
> v1.3.0 案件模块批次（2026-08-13 开工，M1/M2 已完成）：施工方案 `ed71a6a` → M1 `7a5e355`/`0e6eb90`
>   → M2 `4928626`→`b1f276a`→`3100fe6`→`713291e`→`c3b72b4`（见第二十三章）

## 〇、2026-08-12 校时管线救复速览（最先读）

校时管线此前"完全不可用"，本轮全面复盘修复并全部实机验证。四个提交、三类根因：

| 提交 | 修复 | 根因/现象 |
|---|---|---|
| `b7040eb` | 正常文件时间重建 100% 死锁 | `analyzeCoarse` 悬空 `for` 使无边界分支成死代码 → 空位置表 → OCR 静默 return → 状态机永挂「boundary 0 pts」；附带修测点乱序（并行分片按完成序聚合）、ffprobe 工作目录、空表静默 |
| `b87b68f` | 中文路径 OCR 全灭 | `cv2.imread` 在 Windows 用 ANSI fopen，证据帧在中文目录（如“测试文件”）下每帧静默失败 → `np.fromfile+imdecode` |
| `683c0d2` | 框选后不抵达下一步 | 框选按钮不置 `m_goStage`（确认后永不自动开始）；onSetStartTime 双窗 bug（raise 后无 return 照新建）；VideoWidget 信号 connect 累积；quickCheck “约 10 秒”文案不实（B3 尾部 seek 可达 90s） |
| `4a7a23f` | me00060 用例提取失败 + 框选 UX | OSD `2026-07-22 星期三 03:18:01`：RE_FULL 桥不了「星期」整行失配，长行被窄裁剪切断后碎片 `"11.1"` 错配 11月1日 → 墙钟静默错 3 月 + RateInsane 拒用；修复 = 星期容忍 + 全帧优先/命中分级 + timeonly 文件名日期兜底。UX 重构：框选时校时窗自动最小化 → 拖拽松开即完成 → 窗口自动恢复 +「✅ 确认并开始校时」醒目按钮 |

**验证矩阵（全部实机/离屏实测）**：B3 黄金用例 18/18（8 段、92.3%≤2s、音频吻合）
×2（ASCII 与中文真实路径结果逐位一致）；GO 路由 B3 rate=1.398 suspicious；
me00060 GO 全流程 8/8（三点全 full-date、rate 严格 1.000）；UI 链路 v3 双入口
16/16；calibration 73 / piecewise 96 / ocr_atpositions 21 / expect-normal 5 /
preprocess 168 全过。

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

> **版本号规则（2026-08 定）**：第一位 = 大架构重构；第二位 = 功能优化；第三位 = Bug 修复。
> 当前基线 v1.2.2（校时管线救复 + 收尾封版）。

> **排期依据（重要性 × 紧迫性）**：案件/报告/校时属业务核心（直接影响证据交付与可信），
> 优先于技术红利（更快/更省）；技术红利优先于架构维护。
>
> **兼容性红线：独显优化只能是添头。** 所有 GPU 路径必须运行时探测、失败即回退 CPU 路径
> （解码：hwaccel→多线程软解，现有代码已如此；显示：零拷贝纹理→现有 QImage/sws 路径）。
> CPU 路径是永久功能底线与 CI 测试基线，核显办公电脑（同样支持 D3D11VA）完整可用。

### 第一梯队：业务核心（重要性 ★★★★★ / 紧迫性 ★★★★★）

> 校时是基础：分析报告的时间线、多机对齐都依赖校时结果，故先行。

| 版本 | 项目 | 内容 | 前置依赖 | 预估 | 价值 |
|---|---|---|---|---|---|
| **v1.2.0** | **视频校时深化**（P4 OCR 的深化） | 前处理 OCR 结果反哺：相机时钟偏差检测（OSD vs 真实时间）、多机时间线对齐、图表"时间设置"自动校准替代手输 | 现有前处理 OCR + P1a 任务框架 | 1-2 周 | 时间线是火调证据骨架；自动对时消除手输误差 |
| **v1.3.0** | **案件模块** | 案件=证据容器：视频、ROI/标签/辅助线、分析结果、前处理产物、证据报告统一入案；案件列表/归档/打包移交；.vla 关联案件存储 | 无 | 2-3 周 | 证据链完整性；换机/移交不再丢资料；多案件并行管理 |

> **v1.3.0 状态（2026-08-13）**：施工中。施工方案终稿 `docs/DEVELOPMENT_PLAN_V1.3_CN.md`
> （六模块讨论 13 项拍板，Q-8 修订为完整包默认/轻量可选）；M1（domain+app 层）已完成，
> 见第二十三章。
| **v1.4.0** | **分析报告模块** | 一键生成标准分析报告：案件信息 + 校时后的时间线 + 亮度曲线 + 音频分析 + 截图/标签 + 前处理证据 + 结论栏，PDF 输出（含盖章位） | 校时 + 案件模块 | 1.5-2 周 | **最终交付物**：报告从手工拼装（小时级）到一键生成，格式统一可提交 |

### 第二梯队：技术红利（重要性 ★★★★ / 紧迫性 ★★）

| 版本 | 项目 | 内容 | 前置依赖 | 预估 | 价值 |
|---|---|---|---|---|---|
| v1.5.0 | **P3 FFmpeg 分析引擎** | libav 原生亮度+音频分析，NVDEC 解码 + 显存下采样 | 可叠加 GPU 管线 | 1-2 周 | 干掉 Python 依赖与 5000 帧上限，分析提速至近实时 |
| v1.6.0 | **GPU 显示管线** | D3D11 解码纹理零拷贝 → QRhi/OpenGL 渲染，shader 做 NV12→RGB；放大镜/截图叠加/预读缓存随同 GPU 化 | 无 | 1.5-2 周 | 显示 CPU 归零；4K 多视频、高刷屏解锁；GPU 功能前置 |
| v1.7.0 | 前处理 v2 | 硬件编码（NVENC/QSV，参数等价性评审）；OCR 时间戳驱动的重叠段自动剪切；多通道并行流水线；"通道_日期_起止时间"自动命名 | — | 1-2 周 | 转码 20x+（4090）；素材准备全流程自动化 |

### 第三梯队：架构维护（重要性 ★★★ / 紧迫性 ★★）

| 版本 | 项目 | 内容 | 前置依赖 | 预估 | 价值 |
|---|---|---|---|---|---|
| v1.8.0 | P1a/P1b 任务化 + 通道化 | TaskRegistry 分析功能注册制 + AnalysisSnapshot 通道字典 + .vla v7 格式 | 无 | 2 周 | 新分析类型/OCR 租户前置 |
| v1.9.0 | **P2 MainWindow 拆分** | 上帝类（~2876 行）→ AnalysisController + VideoSessionManager + ProjectIO，行为冻结纯移动 | 无 | 2 周 | 可维护性（R2/R3/R5 红线收口） |
| 持续 | 小项清理 | SpectrogramPanel 死代码删除、qobject_cast 上移、roiId 跨模型冲突（RoiModel 合并） | 随 P1b/P2 | 零碎 | 技术债 |

**已研究否决（§6.6.6）**：多线程硬解（FFmpeg 不存在）、GOP 分段并行解码（D17 无效）、skip_frame=NONREF（D17 baseline 零收益）——解码侧已触物理下限，不再投入。

**排期说明**：版本号从 v1.1.1 起按实施顺序连续递增（校时→案件→报告→技术红利→架构维护）；
校时是基础（报告时间线/多机对齐依赖它）故最先；案件/报告直接服务"交付"
（火调办案终点是可提交的报告），价值密度高于 GPU/引擎技术红利；技术红利解决
"更快/更省"（内部效率），故居中；架构维护最后。

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
修复：OCR 进程环境 OMP/OPENBLAS/MKL_NUM_THREADS=1（线程配额是防饿殍的
根本手段）。**2026-08-04 修订**：原方案对 OCR/转码/拼接子进程一律
`CREATE_BELOW_NORMAL_PRIORITY_CLASS` 被推翻——这些是用户盯进度等待的前台
任务，BELOW_NORMAL 在存在任何后台负载（杀软/索引/浏览器）的机器上会被
饿死（现场反馈：转码拼接巨慢、14900K 分析体感慢）。现改为：
- 转码/拼接/OCR 子进程恢复正常优先级；
- 转码 ffmpeg 加 `-threads 核数-2`（留核给主窗口）；
- 亮度分析（analyze_video.py）删除进程降级，解码线程配额改为 `核数-2`。
验证：30min 1440p 分析 29.6s；转码 ~3x 实时；168 单测 + 29 集成全绿。

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


---

## 十九、窗口更名与格式清单（2026-08-03）

> 反馈：①子窗口列出全部支持格式；②「素材整理拼接」改名「素材转码拼接」；
> ③自动排序标注实验性；④删除"流内时间跳过识别"勾选项（默认启用）；
> ⑤去掉 1234 步骤条，顶部改格式支持说明。

- 更名：窗口标题/工具栏金色按钮/文件菜单（Ctrl+M）→「素材转码拼接」。
- 顶部横幅列出支持格式：DAV（大华）、AVI、WMV、FLV、TS/MTS/M2TS、MOV、
  MKV、MPG/MPEG、3GP、WebM → 自动转码 MP4（H.264，2s 关键帧）→ 拖拽顺滑；
  同参数 MP4 直接无损拼接。
- 「自动排序 ⚡（实验性）」：tooltip 明示可能识别失败/时间不准，须校对页人工核对；
  不点也可直接拼接。原"跳过画面识别"勾选删除——流内时间跳过逻辑在自动排序内
  恒启用（coordinator 默认 true）。
- 步骤条移除后页面导航保留：自动排序→校对页；开始拼接→执行页；设置页可返回。


---

## 二十、GO 复合键与关键帧探测（2026-08-03）

> 反馈：①MP4 也可能关键帧不行（拖拽不流畅）——达不到 2s 关键帧也应路由转码；
> ②单个文件也要能通过右下角按钮转码；③按钮改名为「GO」；④横幅改为功能描述。

### 20.1 关键帧间隔探测（probe 层）

`MediaProbeEngine::probeOne` 采样至多 5 个关键帧 PTS（600 包上限防呆），中位间隔
写入 `ProbeResult.keyframeIntervalMs / keyframeSparse`（>2.5s 标记）。实测：
$RCR79YF 原始 DVR mp4 **kf=15s（GOP 250@15fps）**——这就是那些 mp4 拖拽也卡的
隐藏根源；转码/拼接产物 kf=2s 合格。`filesNeedingTranscode` 将稀疏关键帧 MP4 也
路由转码（重排 GOP），GO 矩阵第一行"参数一致 MP4 直接无损拼接"因此实际要求
"且关键帧 ≤2s"。

### 20.2 GO 复合键（右下角）

- 多文件：需转者转（非 MP4 / 关键帧稀疏 / 参数不一致），其余无损，再拼接；
- 单文件：需要转码 → 转码导出（不拼接，finalize 支持无 concat 的转码产物）；
  已合格 → "✓ 无需处理"卡片 + 5 秒自动关弹窗（reportCsvPath 为空作判定信号，
  evidenceDir 清空不留临时目录）；
- 失败仍红卡 + 日志。

### 20.3 遗留

- 横幅描述与 MANUAL 第十三章已同步；单测 168、集成 29。

---

## 二十一、v1.2.x 视频校时深化与时间重建（2026-08-05 ~ 08-10）

> **背景**：校时原为"日内固定秒偏移"，无法应对①摄像机时钟漂移、②DVR 抽帧录像
> 导出导致的变速文件、③拼接产物校时丢失、④OCR 识别率低。v1.2.0 解决漂移与
> 继承，v1.2.1 解决变速重建与界面易用性。

### 21.1 v1.2.0 校时深化（commits b5eb47b / be67aee）

**仿射模型**（`domain/time_calibration.h`）：
```
wallMsOf(streamMs) = offsetMs + rate × streamMs   // 唯一换算入口（C3）
beijingMsOf  = wallMsOf + truthOffsetMs           // 北京时间层（拍照对时法）
```
- rate=1.0 且 dateKnown=false 时与旧版"日内偏移"行为完全一致（v7 迁移路径）；
- 全应用（图表/光标/标签/CSV/报告）只调 wallMsOf/beijingMsOf，结构上保证一致。

**三点识别**：首 1s / 当前位置 / 尾-3s 三处 OCR → 最小二乘拟合（中心化防 epoch 量级
数值问题）。防护三层：显著性门控（|rate−1| > max(3σ, 30秒/天) 才应用漂移）、
野点剔除（残差>3s 提示，表格取消勾选重拟合）、速率 sanity（|rate−1|>1% 拒绝）。

**北京时间校验**：拍照法（同框拍手机北京时间 + 画面时间）→ truthOffset = 北京 − 画面；
说明留档（truthNote）随 .vla 保存。

**.vla v8**：`time_calibration` 对象（含测点证据/截图路径/拟合参数）；v≤7 的
time_offset 自动迁移（dateKnown=false）；v>8 明确拒绝（F4/Q-19 严格升版）。

**sidecar 继承**（`.lumencal.json`）：前处理 finalize 随拼接输出写逐段墙钟起点/速率/
缺口表；打开输出自动继承首段校时，段间缺口>2s 警告（Q-4，进报告）。

### 21.2 v1.2.1 时间重建（commits 772dc76 / e1dc8b4）

**动机（B3 黄金用例）**：DVR 抽帧录像（待机 12.5~20fps / 异动 25fps）导出时按 25fps
重打 PTS → 流内 74min = 真实 103min，单条仿射直线必然失败（rate=1.4 超上限被拒）。

**分段映射表**（`domain/time_piecewise.{h,cpp}`，纯函数）：
- `PiecewiseTimeMap`：分段线性（段内 rate 恒定、边界处突变），wallMsOf/streamMsOf
  查表换算，不假定全局线性；
- **两级采样编排**（`CalibrationService::runReconstruction`）：粗采样 60 点（间隔
  clamp(dur/60, 30s, 120s)）→ 相邻粗斜率判边界（间隔≥30s 才参与，加密点噪声
  ±1.0 不参与判定）→ 边界区间加密（保底 8 点/边界，总量 200 上限，按跳变幅度
  top 分配，步长自适应 ≥2s）；
- **野点剔除**（OCR 错读）：尖峰判据 `d2 ≈ −2×d1`（单点墙钟错 Δ 使两侧斜率各偏
  ±Δ/ds，幅度比 1.2~2.8）；跨边界点（斜率单调过渡）保留；迭代每轮剔除最强尖峰
  防误伤邻居；
- **边界精化**：在跳变区间 [lo,hi] 内枚举分割点，取"左右两段组合残差最小"
  （SSE + 剔除点数×1e8 惩罚，防鲁棒拟合抹平档位差）；区间内无中间点（无加密点）
  时直接用跳变左端点（两点必成线，SSE 无信息量）；
- **段合并**（相邻段率差<0.12 吸收错读碎段）、rate sanity（[0.4,2.5] 外并入邻居）、
  单点段用段首→段尾点对斜率（不能返回 1.0，防 1.65 档窄段误并）；
- **OCR 异常标注**：Sample.ocrSuspicious（⚠，已自动排除，不参与拟合），.vla 留档，
  校时窗口测点表 ⚠ 列 + 重建摘要提示；
- **音频校验**：OSD 总跨度 vs 音轨时长（±2%），detect 首尾（排除错读点 min/max 干扰）；
- **ffprobe 视频流时长防御**：容器总时长取音画最长流会虚标（B3 音轨 102min/画面
  74min），尾部取样必须用视频流时长（`probeVideoStreamDurationMs`）。

**probe_timestamps.py**：
- RE_FULL/RE_NO_YEAR/RE_TIME_ONLY 秒组容错空格（OCR 把冒号读成空格丢 25s 的解析 bug）；
- at 模式并行化：按位置分片 ProcessPoolExecutor（单文件多位置也并行），workers 默认
  = min(8, cpu_count)；B3 全量从 ~13min 降至 ~8min；
- `--roi-json`：用户框选时间戳区域（归一化 0~1），ocr_frame 只识别框内（放大 3 倍
  + 增强），排除画面干扰。

**.vla v9**：time_calibration 含 piecewise 分段表（v8 旧文件可读）。

### 21.3 校时窗口 UX 重构（commits 10f1573 / 1296d9f / 2cf639a / 084875d / 59a85ec / 707cec1 / 95ced1f / 33a2ef2）

参考拼接窗口 GO 键交互，按用户反馈迭代：

- **非模态**：窗口可最小化/关闭，识别/重建后台进行；进度常驻主窗口状态栏
  （MainWindow 直连 service::progress，对话框关闭后仍可见）；关闭不取消任务，
  重开可见进度结果；校时应用经 calibrationApplied 信号即时更新图表；
- **GO 一键**：Idle→Quick→(Ocr|Recon)→Done 状态机；快速检查（首尾 2 帧）自动路由
  ——正常→三点识别 / 变速→时间重建，用户无需选择；完成后无异常**自动应用**
  （"自动校时"直觉）；按钮文字随阶段变化 + 取消按钮；
- **秒级预检**（runQuickCheck）：整体速率偏差 >15% 判疑似变速；
- **三步布局 + 用法横幅**：第 1 步 自动校时 / 第 2 步 对真实时间（北京时间，
  含拍照对时法说明）/ 第 3 步 高级（折叠：手动/录像机时间/强制重建）；
- **时间戳区域框选**（识别率提升）：点 GO 且无已存区域 → 主窗口视频叠加框选模式
  （OverlayWidget TimestampRoi：暗色遮罩 + 框内高亮 + 鼠标跟随提示"拖拽框选时间戳
  区域"+ 确认/跳过浮动按钮）；归一化 ROI（0~1）经 roi.json 传脚本；按视频路径
  hash 存 QSettings（同一摄像头自动复用）；「重新框选时间戳区域」随时重框；
- **Bug 修复**：onAtPositionsFailed 未清 m_quickPending（状态残留误判后续任务）；
  快速检查 OCR 失败自动降级三点识别；paintEvent 框绘制缺失（替换匹配静默失败）；
  VideoWidget overlay 未同步视频分辨率（归一化失效）；onRunGo 无 ROI 分支未置
  m_goStage（框选确认后 GO 不自动继续）；
- **失败文案友好化**：区分"画面无时间/时间不含日期/字体特殊"与系统错误，显示
  框选坐标诊断。

### 21.4 测试体系（v1.2.x 增量）

| 测试 | 数量 | 覆盖 |
|---|---|---|
| lumenarc_calibration_test | 73 | 仿射拟合/门控/迁移/往返 |
| lumenarc_piecewise_test | 96 | 分段检测/边界定位/野点/段合并/JSON/往返 |
| lumenarc_ocr_atpositions_test | 21 | at 取样 + ROI 模式（C++→roi.json→脚本全链路） |
| lumenarc_reconstruction_test | 18 | **B3 黄金用例**：8 段全档位识别、92% 测点≤2s、音频吻合 |
| lumenarc_vla_test | — | .vla v9 读写/迁移 |

B3 最终重建结果：1.0→2.0→1.42→1.0→1.65→1.0→2.0→1.0 八段，OCR 错读点（~16%，
wall 本身错）标 ⚠ 自动排除，音频校验吻合（OSD 6196s vs 音轨 6121s）。

### 21.5 已知限制与遗留

- **B3 素材已移走**（`C:/code/LumenArc/B3一单元客梯.mp4` 不存在），reconstruction
  集成测试 SKIP；放回原路径即可恢复；
- 加密点总量上限 200（按跳变 top 分配）：弱边界（跳变 0.1~0.2）可能分不到加密点，
  定位精度 ±1 粗间隔（≈74s）；
- OCR 错读点无法修正（wall 本身错），已标 ⚠；报告模块（v1.4）应显式标注
  "OSD 疑似错读，时间不可信"；
- 时间戳不含日期（仅 HH:MM:SS）无法解析（需 年月日 时分秒），失败文案已说明；
- 时间重建全程 ~8min（并行后），UI 提示可最小化等待；
- 段数上限 32（保护）；错读密集区可能产生 2~3 个碎段（rate 1.0~1.4 间，影响局部
  ±几秒）；
- 校时窗口仍有优化空间：GO 完成通知气泡（Windows toast）、框选记忆迁移至案件
  模块（v1.3.0 目录制案件）。

### 21.6 复盘修复：正常文件重建死锁（P0，2026-08-12）

> **背景**：v1.2.1 发布后重建管线对正常文件"完全不可用"。全面复盘定位三组缺陷，
> 已修复并补回归。

**P0-1 悬空 for（已实机复现）**：`CalibrationService::analyzeCoarse()` 残留一行
无循环体的 `for (int i = 0; i < ca.ranges.size(); ++i)`，使紧随其后的
`if (!ca.hasBoundary()) {...}`（单段仿射/单段变速分支）成为该 for 的循环体——
无边界（正常文件）时 ranges 为空、循环体 0 次，整个分支沦为死代码；控制流落入
边界加密阶段，jobs 为空 → 空位置表 → OCR 引擎 `positionsMs.isEmpty()` 静默
return → 状态机永久停 Boundary，UI 永远"重建中…boundary 0 pts"。影响面：
①「强制变速重建」对正常文件必挂；②GO 预检误判后粗采样无边界必挂；③失败文案
恰好引导用户点「强制变速重建」。**盲区**：黄金用例 B3 有边界走不到死代码，且文件
移走后集成测试长期 SKIP，正常文件重建无任何覆盖。

**P0-2 测点乱序**：at 模式按位置分片并行，C++ 按 as_completed 完成顺序聚合，
`m_reconSamples` 不保证按 streamMs 排序；修复 P0-1 后 no-boundary 分支可达，其
`first()/last()` overallRate 计算在乱序下 ds 可为负 → 正常文件误判"整体变速"
且 rate 错误。修复：`onReconBatchFinished` 聚合后统一按 streamMs 排序。

**P1**：`analyzeCoarse(ps)` 与 `size<2` 检查顺序颠倒（先检后调）；ffprobe 动态版
av*.dll 在应用根目录而非 ffmpeg/ 子目录，显式 `setWorkingDirectory(appDir)` 防 cwd
不同时静默启动失败（视频流时长防御失效）；`runAtPositions` 空位置表改 emit
`atPositionsFailed`（静默 return 是死锁最后一环）。

**回归**：`lumenarc_reconstruction_test --expect-normal`（正常文件跑重建必须完成
且仿射回退 piecewiseMode=false，不得挂起；挂起即 P0 回归）。实测 synth.mp4：修复前
boundary 0 pts 挂死；修复后 5 checks 0 failures。全套：calibration 73 / piecewise
96 / ocr_atpositions 21 全过。

### 21.7 电梯用例全链路验证 + 中文路径 OCR 修复（2026-08-12）

> B3 黄金用例（用户移至 `C:\code\LumenArc\测试文件\B3一单元客梯.mp4`）首次在最终
> 代码上跑通全链路，并暴露一个真机阻断级缺陷。

**P0-3 cv2.imread 中文路径静默失败**：OpenCV（5.0.0）在 Windows 用 ANSI fopen，
路径含任何非 ASCII 字符即返回 None——视频在中文目录（如“测试文件”）时证据帧全
在中文路径下，每帧 OCR 静默失败 → 全片 ocr_all_failed。旧 B3 测试从未暴露：当时
证据帧目录 `C:/code/LumenArc/LumenArc_Calibration/` 全 ASCII，中文只在文件名（不
进证据路径）。修复：`np.fromfile + cv2.imdecode`（Win32 Unicode API），两处读取点
（ocr_frame / _ocr_frame_capped）统一封装 `_imread_unicode`。

**验证矩阵（全部实测通过）**：

| 用例 | 结果 |
|---|---|
| B3 黄金用例（ASCII 副本） | 18/18：8 段（1.0/2.0/1.0/2.0/1.389/1.0/2.0/1.0），92.3% 测点≤2s，OSD 6196s vs 音频 6121s 吻合 |
| B3 中文真实路径（测试文件\\B3一单元客梯.mp4） | 18/18，分段与 ASCII 完全一致（确定性）；证据帧正常落中文目录 |
| GO 路由（quickCheck harness） | B3：rate=1.398 suspicious=1 → 正确路由重建（真值 6196/4435=1.397） |
| synth 正常文件 --expect-normal | 5/5：完成 + 仿射回退（P0-1 不复发） |
| synth OCR 半链路 | 21/21（imdecode 对 ASCII 无回归） |
| calibration / piecewise / preprocess | 73 / 96 / 168 全过 |

遗留：B3 段 5（2.2s 窄段，rate 1.0）属文档已知“错读密集区碎段”现象，在测试容差内；
真人测试重点：GO 全流程（框选 → 预检 → 自动路由 → 自动应用 → 图表时间轴）。

### 21.8 UI 操作链路修复：框选后不抵达下一步（2026-08-12）

> **现场反馈**：框选完了以后不会抵达下一步。先以离屏 harness 复刻整条链路
> （OverlayWidget 鼠标链 + VideoWidget 转发 + MainWindow 接线块逐字 + 对话框状态机
> + 真实 OCR），证明代码链路本身畅通（11/11）——问题在四处 UX/接线缺陷：

1. **「框选时间戳区域」按钮不置 m_goStage**（主断点）：用户直接点框选按钮（不点 GO）
   时，确认后 setTimestampRoi 不满足 `m_goStage == Quick` → 不触发 startGo，对话框
   只显示“已选定…点击 GO”——用户直觉“框完该继续”落空。修复：onRoiButton 在
   Idle/Failed/Done 状态下一并置 Quick（先框后测入口，确认后自动开始）。
2. **onSetStartTime 双窗 bug**：`if (m_calibrationDialog) raise();` 后无 return 照样
   new → 两窗共存，框选确认只推进最后打开的窗口，先开的永远卡住。修复：已开窗口
   raise + activateWindow + return。
3. **VideoWidget 信号 connect 累积**：timestampRoiConfirmed/Cancelled 的 connect 挂在
   onSetStartTime（每开一窗加一份，lambda 永久残留）。修复：移至 MainWindow 构造
   函数一次性连接（lambda 只读成员，语义自洽）；删除重复 destroyed lambda。
4. **quickCheck 文案不实**：“约 10 秒”对 B3 类无索引大文件（尾部 seek 单次可达
   90s）严重低估，用户等 30s 误判死机。文案改为“可能需 1~2 分钟”。

**回归**：`tests/ui_chain_test_main.cpp`（离屏，需 Qt6Test + qoffscreen；双场景
11/11——GO 入口与框选按钮入口均自动续跑至自动应用）；calibration 73 / piecewise
96 / ocr_atpositions 21 全过。

### 21.9 星期间隔 OSD + 框选 UX 重构（2026-08-12 现场反馈②）

**OCR 解析三连修**（用例 `20260722_031301_me00060.mp4`，OSD = `2026-07-22 星期三
03:18:01` 顶部偏左长行）：

1. **RE_FULL 允许星期/英文周几间隔**：此前 `\s*[T\s]?` 桥接不了「星期三」→
   整行失配（全帧 OCR 1.00 置信读对整行，解析 None）；
2. **全帧优先 + 命中分级**：OSD 长行（2560 宽机身左上延至中上）被 30% 窄裁剪
   切断，碎片 `"11.1 03:18:01"` 被 RE_NO_YEAR 错配为 11月1日 + 文件名年 → 墙钟
   **静默错 3 个月**（取证最忌）+ 跨帧误读不一致 → RateInsane 拒用（用户视角
   "提取时间失败"）。链改为：全帧 enhanced → 窄裁剪 → 窄 binary → 宽上角；
   `_search_crops` 分级 full 恒优先于 noyear/timeonly，full 命中即返；
3. **timeonly 兜底**：OSD 仅 HH:MM:SS 时用文件名完整日期（YYYYMMDD_ 前缀，值域
   校验）补齐（原 design 为 reference-only；监控命名 20260722_031301_me00060
   自带可信日期）。

修复后该文件三点：761ms→03:13:02 / 299760ms→03:18:01 / 654760ms→03:23:56，
全 full-date（07-22 与文件名一致），rate 严格 1.000。B3 抽点无回归（通道前缀
`B3二单元客梯 2024-09-30 …` 照常 full 命中）。

**框选 UX 重构**（现场反馈：确认键不明显）。新流程：点 GO/「框选时间戳」→
校时窗**自动最小化** → 拖拽框选**松开即完成**（overlay 新增 timestampRoiReady，
mouseRelease 直发）→ 校时窗**自动恢复** + 主按钮变「✅ 确认并开始校时」→ 点击
进入后续步骤（GoStage 新增 Staged 待确认态）。叠加层角落小确认键保留为备选。
跳过框选 = 直接自动扫描开始（无可确认内容不挡一步）。

**回归**：ui_chain v3（双入口 16/16：最小化/恢复/未启动/醒目按钮/启动/应用全
断言）+ me60 真实文件 GO 全流程 8/8（quickCheck→三点→自动应用）；calibration
73 / piecewise 96 / ocr_atpositions 21 / expect-normal 5 全过。

---

## 二十二、v1.2.2 收尾批次（2026-08-12）

校时管线稳定后的封版批次，四项：

### 22.1 GO 预检"第三点确认"（防错读误判变速 → 白跑数分钟重建）

**问题**：预检取首尾两点算 overallRate，任一点被 OCR 错读 → rate 偏离 →
误判变速（>15%）→ 自动路由进时间重建（大文件数分钟级），结果还不可信。

**修法**：
- `runQuickCheck`：时长 >12s 时增采中点（positions = 首/中/尾三点）；
- 新增 `CalibrationService::quickCheckSamplesInconsistent()`（public static，
  可单测）：中点墙钟必须落在首尾直线上，容差 `max(5s, 2%跨度)`
  （OSD 分辨率 1s；错一位分钟 = 60s+ 必超阈）；任一点 wallMs≤0 亦可疑；
- `quickCheckReady` 信号增加第 4 参 `ocrSuspect`；
- 校时窗口 `ocrSuspect=true` → GoStage::Failed + 提示重新框选，
  **拒绝自动路由**（不进三点、更不进重建）；
- 附带防御：预检分支补按 streamMs 排序（at 模式并行聚合顺序随机，
  与重建分支同一防御，此前两点分支漏网）。

**用例**（ui_chain_test_main.cpp，静态函数直测 7 条）：共线/乱序/尾点错读
/首点错读/中点错读/仅两点不校验/±3s 秒级抖动容忍。

### 22.2 GO 完成 Windows toast 通知

重建数分钟、用户最小化等待场景：`TimeSettingsDialog::goTaskFinished(title,
message)` 新信号（三点/重建完成 + 长任务失败三处发出）→ MainWindow
`showTrayNotification`：`QSystemTrayIcon::showMessage`（仅当
`QApplication::activeWindow()==nullptr` 不打扰；messageClicked 还原校时窗；
托盘图标 15s 后自动隐藏不常驻）。

### 22.3 ui_chain_test 补进 CMake

`lumenarc_ui_chain_test` target（此前源码在库无 target）：
- 源：timesettingsdialog/videowidget/calibration_service + OCR/python/probe
  引擎 + 三个 domain 模型；**ivideo_engine.h / ianalysis_engine.h 必须列入
  源表**（AUTOMOC 才生成接口 moc，否则 LNK2019 staticMetaObject）；
- 链接 Qt6::Test（find_package 同步加 Test 组件，QTest 鼠标注入）；
- POST_BUILD 拷贝 `qoffscreen.dll` 到 exe 旁 platforms/ —— exe 旁
  platforms/ 只有 windeployqt 部署的 qwindows.dll，offscreen 运行时
  Qt 会弹 "no Qt platform plugin" 错误框（本批实测踩到）。

### 22.4 版本对齐 v1.2.2

CMakeLists project(VERSION) / MACOSX 两项 / aboutdialog / app.rc 两项 /
Info.plist 两项（原滞留 1.1.1）→ 1.2.2。

### 22.5 验证矩阵

| 测试 | 结果 |
|---|---|
| ui_chain（双入口 + 第三点 7 用例） | 23/23 绿（offscreen，synth.mp4 实战） |
| calibration / piecewise / preprocess | 73 / 96 / 168 全绿 |
| LumenArc 主程序 | 编译通过 |
| reconstruction 集成 | 未跑（B3 黄金视频不在本机，需在有素材环境回归） |
| vla_test | 跳过（测试 .vla 不在本机，既有条件非回归） |

### 22.6 遗留（挂 v1.4.0）

校时窗口 ⚠ 错读点随报告显式标注"OSD 疑似错读，时间不可信"——依赖报告
模块的报告上下文，已记入 v1.4.0 范围（docs/DEVELOPMENT_PLAN_V1.3_CN.md §六）。

---

## 二十三、v1.3.0 案件模块实施记录（2026-08-13 开工，M1/M2/M3 全部完成，已封版）

### 23.0 施工依据

- **方案**：`docs/DEVELOPMENT_PLAN_V1.3_CN.md`（终稿 `ed71a6a`）。六模块讨论
  闭环、13 项拍板固化于 §8（与旧文冲突处以它为准）。
- **关键修订**：Q-8 移交包从"轻量唯一"改为**完整包默认/轻量可选**
  （用户拍板：否则移交后找不回原视频导入查看）；不做"添加视频时拷入案件"。
- **三条底线**：独立模式（不建案件）行为与 v1.2.2 逐点一致（既有测试零修改
  通过为准绳）；取证红线（源视频只读、重定位只改引用）；per-video
  membership（视频是否在案决定其行为，非全局开关）。
- **里程碑**：M1 domain+app 层（5 任务）✅ → M2 挂接+主 UI（6 任务）✅ →
  M3 移交+多机视图+封版（任务 12-15 全部 ✅，v1.3.0 已封版打标）。

### 23.1 M1 完成内容（commits `7a5e355` + `0e6eb90`）

| 任务 | 交付 |
|---|---|
| case_model | `domain/case_model.h/.cpp`：CaseMeta/CaseVideoRef/CasePreprocessRef；case.json（magic `LumenArcCase` + formatVersion=1 + QSaveFile 原子写）；F1/F4 版本规则；F3 未知根字段忽略并收集警告 |
| 编号规则 | 案件编号 `YYYYMMDD-城市区县-x`（扫描根目录同前缀目录，提取"前缀后首个 '-' 前"的字母段）；`nextVideoSeq` 高水位入 case.json |
| CaseManager | create/open(.lock 防双开+残留锁 lockConflict 出参+force)/save/close；recentCases(QSettings×10)；addVideo(路径去重/V### 分配/size+mtime 登记)；removeVideo(deleteData 可选删案内 .vla+证据帧) |
| 双模式分流 | `vlaPathFor` / `evidenceDirFor`：入案→案件路径，未入案/无案件→独立模式老路径（逐字节照旧） |
| 随案数据 | 框选记忆 timestampRoi、校时徽标缓存（hasCalibration+summary，.vla 为 SSOT） |
| 哈希队列(Q-9) | QThreadPool×1 + OS 最低优先级 + 1MB 分块可中止 SHA-256；四触发（入案即排/开案补缺失/变更重算/手动全量）；hashProgress 逐文件 + hashQueueFinished |
| 完整性校验 | 异步 Verify：快扫 size/mtime + 差异/全量重算比对（一致 0/已变更 1/缺失 2），变更后 sha 重登记；manifest.json 队列回填（排除锁/manifest 自身/sources） |
| 证据帧瘦身 | probe_timestamps.py at 模式：A 框选命中帧存 ROI 裁剪图(外扩 25%)、无框选全帧降宽 1280；B 只留命中帧；C 首尾 2 张全帧 JPEG 上下文；D JPEG q90。实测 -87%（200KB vs 1.6MB/3 位置），重建规模推算 904MB→≤10MB(框选)/≤21MB(扫描) |

### 23.2 实施中发现并固化的设计修正（后来者勿再踩）

1. **V###"永不复用"内存 max+1 守不住**：移除最大编号后水位回退 → case.json
   增加 `nextVideoSeq` 高水位字段（旧文件缺省=既有最大+1）。
2. **案件编号扫描**：目录名 = 编号+标题，必须提取字母段比较（全名比较
   永远不匹配）。
3. **onVideoHashed 计数器需 `m_hashTotal>0` 守卫**：Verify 任务的逐路回投
   不经过队列计数，否则会误发 hashQueueFinished 并打乱进度。
4. **证据帧 context_map**：命中帧即上下文帧时 frameImg 必须改指 context
   JPEG，否则 PNG 清理后路径悬挂。
5. **JPEG 写入中文路径**：与 `_imread_unicode` 同款坑——cv2.imwrite 走 ANSI，
   用 `imencode + buf.tofile`（numpy 写 Unicode 路径安全）。
6. **abort 复位时机**：createCase/openCase 复位 `m_hashAbort`（上次关案
   cancelHashes 会置位，不复位则新案哈希队列入队即空转）。
7. **QSaveFile 原子写**：case.json 崩溃不留半文件（Qt 原生，勿手写 tmp+rename）。

### 23.3 测试状态

| 测试 | 结果 |
|---|---|
| **lumenarc_case_test（新增）** | 96/96：往返/版本/magic/未知字段/原子写/编号生成/高水位/锁冲突+force/双模式分流/框选记忆/徽标/重开持久化/deleteData/哈希队列(进度+排空)/校验(一致/篡改标变更/删除标缺失/sha 重登记)/manifest(含 case.json 排除锁) |
| 既有回归 | calibration 73 / piecewise 96 / preprocess 168 / ui_chain 23 / ocr_atpositions 21 零修改全绿 |

### 23.4 M2 完成内容（任务 6-11，commits `4928626`→`b1f276a`→`3100fe6`→`713291e`→`c3b72b4`）

| 任务 | 交付 |
|---|---|
| 6 .vla 路径分流挂接 | mainwindow 三处走 `vlaPathFor`：自动保存（`onAnalysisFinished`）/缓存探测（`openVideoFile`）/手动存取（`onSaveAnalysis` 默认路径 + `onLoadAnalysis` 起始目录）；入案视频缓存探测**不弹询问**（案件 .vla 即权威缓存直接加载）；写案件 .vla 同步刷新校时徽标（`calibrationBadgeSummary`：来源+点数+rate+分段标注） |
| 9 框选记忆迁移 | `savedTimestampRoi`/`saveTimestampRoi` 双模式分流：入案读写 case.json `timestampRoi`；注册表旧值**只读复制一次**入案（原值保留一版，拍板§8-12）；独立模式照旧 QSettings（`readTimestampRoiRegistry` 抽出） |
| 7 打开/添加行为 | `admitVideoToCase`：Ctrl+O/拖入即入案（先于 openVideoFile，源旁 .vla 导入后缓存探测直接命中）；已在案直接打开不重复登记；重复路径非模态拒绝；同大小仅提示；**源旁 .vla 询问导入（默认是，复制）**；登记即落盘。菜单「临时打开视频（不入案）」（`openVideosInteractive(bool)` 公共化） |
| 8 校时/前处理挂接 | `CalibrationService::setEvidenceDirResolver`（默认老路径，MainWindow 注入 `CaseManager::evidenceDirFor`）；PreprocessWindow 案件横幅「📁 案件模式：成果自动导入《编号-名称》」+【导入案件(默认)】/【独立输出(自选)】互斥；导入模式锁定输出目录 `<案件>/preprocess/<yyyyMMdd_HHmmss>`（`caseSessionDir` 惰性生成、mutable+const）；finalize 自动登记 `addPreprocessSession`（sessionDir/reportCsv 相对路径、输出引用制 P### 登记 size/mtime、**sidecar 复制归类 sidecars/** 原件保留输出旁供独立继承）；登记结果附注完成页。`PreprocessReport.outputPaths` 全量清单（协调器回填） |
| 10 CaseDock + 模式出口 | `casedock.{h,cpp}`：证据树四组（视频/前处理/报告/快照）；徽标 ⏳✓⚠✗+校时⏰（同步快判）；右键 打开/重定位/移除(默认保留案内数据三钮)/算指纹/复制指纹/资源管理器显示(explorer /select)；标题栏✕=退出案件模式（closeEvent 忽略+发请求）。**模式出口三处**（✕/Ctrl+W/状态栏📁按钮）；窗口标题带《编号-名称》（`windowTitleWithCase`）；CaseDock 替代视频列表；开案恢复现场（lastVideoId→openVideoFile，案件 .vla 自动加载）；退出不中断播放；closeEvent 应用退出 dirty 检查。哈希队列排空静默落盘 |
| 11 对话框 + 起始页 | `casedialogs.{h,cpp}`：NewCaseDialog（必填校验/编号预览随输入刷新/创建后固定提示）；CasePropertiesDialog（编号/案发时间/地点固定，改标题不改目录名）。`startpagewidget.{h,cpp}`：三钮+最近 10 条双击打开+空态引导文案+「不再显示」勾选。CaseManager：`caseRootDir` 静态设置项（默认 `<程序目录>/cases/`）、`updateCaseInfo`、M2 任务10 附带 `relocateVideo`（大小不一致默认拒绝、force「仍要采用」extraFields 留档、sha 作废重算）与 `queueHashFor`。案件菜单补全（新建/打开/最近子菜单动态/起始页/属性/根目录设置/关闭）；openCaseFlow 锁冲突提示 force |

### 23.5 M2 实施中发现并固化的修正（含一项 Windows 环境实测结论）

1. **Windows 同尺寸重写 mtime 可能不更新（实测复现）**：`verify_race_probe`
   复现——Qt QFile 重写同尺寸文件后内容已是新值但 mtime 保持原值，M1
   快扫依赖的 mtime 触发在该场景必漏报。结论固化：**快扫 size/mtime 为
   尽力而为语义；篡改确证走全量重算**（M3 校验报告对话框须把「全部重算」
   放显眼位）。case_test 校验用例已改为确定性双路径（同尺寸→全量/异尺寸
   →快扫 size）。
2. **AV/Defender 扫描锁**：刚落盘文件可能被瞬时锁读/锁删 → 测试统一加
   轮询重试（写后读回校验、删除重试、manifest 轮询至可解析）。生产代码
   不受影响（QSaveFile 原子写）。
3. **QMessageBox::addButton 返回 QPushButton\*** 但 qmessagebox.h 只前置
   声明：与 clickedButton()（QAbstractButton\*）比较需 `<QPushButton>` +
   `<QAbstractButton>` 完整类型。
4. **测试组污染真实注册表 case/recent**：createCase→pushRecent 无清理
   （M1 组 8 同样遗漏）→ `RecentGuard` RAII 快照清空/析构恢复。
5. **msbuild 无 ALL_BUILD 目标**：全量构建直接对整个 .sln 不带 /target
   （build_tmp/build_target.bat ALL 已封装；cmake 在 `C:\cmake-temp\CMake\bin`，
   不在 PATH）。
6. **Qt6Test/Qt6Concurrent dll 未随 exe 部署**：ui_chain 等测试需
   `PATH=<Qt>/bin` 前缀运行（DLL 未拷贝 Release/）。

### 23.6 测试状态（M2 封版）

| 测试 | 结果 |
|---|---|
| **lumenarc_case_test** | **155/155**（M1 96 + 迁移组 13 + 会话登记组 15 + 重定位组 16 + 属性组 11；校验组确定性重构；20 连跑全绿） |
| 既有回归 | calibration 73 / piecewise 96 / preprocess 168 / ui_chain 23 / ocr_atpositions 21 / vla 3×PASS **零修改全绿** |
| 离屏冒烟 | LumenArc.exe offscreen 启动 10s 无崩溃（起始页开/关两种模式） |

### 23.7 下一步 M3（任务 12-15）

12 导出移交包（完整包默认/轻量可选；导前自检；空间预检；后台可取消+半成品
清理；sources/+包内 case.json+manifest+导后快校；README.txt）✅（`415ef0c`）
→ 13 批量重新定位 ✅（见 §23.8）→ 14 多机时间线对齐视图 ✅（见 §23.9）→
15 文档与封版 ✅（见 §23.10）。
**手工矩阵（§4.3）待实机跑**：`docs/RELEASE_CHECKLIST_V1.3_CN.md`（A-H 八组）。

### 23.8 M3 任务 13 完成内容（批量重新定位）

| 交付 | 内容 |
|---|---|
| 模糊匹配 | `CaseManager::proposeRelocations(dir)`：递归扫候选目录视频扩展名集；仅缺失视频（effectivePathFor 不存在，完整包副本兼底场景跳过）列入；名+大小一致 level 2 / 仅文件名 level 1 / 无候选 level 0。同步快判不哈希 |
| 指纹强制比对 | `BatchRelocateDialog`（casedialogs）：扫描候选→表格人工确认（可【浏览】逐行覆盖候选）→【比对并采用】逐路串行后台算指纹（QtConcurrent+QFutureWatcher 链式，同哈希队列 IO 纪律）→登记指纹一致或无基线→採用；不一致**默认拒绝**（⚠ 状态行展示登记/候选前 8 位）→显式【仍要采用（留档）】二次确认后 force |
| knownSha 免二次哈希 | `relocateVideo` 增 `knownSha` 参：批量场景指纹已比对，直接登记不再入队重算；空值走老路径（作废入队重算，独立模式/M2 行为不变） |
| 留档 | 大小不一致覆写留档（M2 既有）；**新增**同尺寸异内容指纹覆写 `relocateShaOverride/<id>`（force+knownSha≠旧登记时，前后指纹前 8 位入 extraFields） |
| 键迁移 | `VideoStateManager::migrateKey(old,new)`（内存状态跟随）；`videoRelocated(id,old,new)` 信号由 relocateVideo 发出，MainWindow 挂接：migrateKey + 当前播放路径跟随（cleanPath 比较）；採用后静默落盘（同哈希队列排空纪律） |
| 入口 | 案件菜单「批量重新定位(&B)...」（随案开关启用）+ CaseDock 视频右键「批量重新定位…」 |

实施固化：① 工作线程 abort 用 `shared_ptr<atomic<bool>>` 副本传递（对话框
模态销毁后工作线程不悬垂；watcher 为对话框子对象随销毁断链）；② 比对取消
的行回到「待比对」可重试，读取失败保持失败态；③ 指纹比对属对话框职责，
manager 层 size 守卫对同尺寸异内容不拦——**勿把指纹比对下沉 manager**（批量
与独立重定位的红线一致：比对在交互层，留档在 manager 层）。

#### 测试状态（任务 13 封版）

| 测试 | 结果 |
|---|---|
| **lumenarc_case_test** | **213/213**（新增组 13：匹配分级/存在源跳过/computeSha256 正确性+abort/knownSha 直接登记免重算/videoRelocated 信号/同尺寸异内容 force 採用+留档/migrateKey 迁移+幂等） |
| 既有回归 | calibration 73 / piecewise 96 / preprocess 168 / ui_chain 23 / ocr_atpositions 21 / vla 3×PASS 零修改全绿 |
| 离屏冒烟 | LumenArc.exe offscreen 启动 10s 无崩溃 |

### 23.9 M3 任务 14 完成内容（多机时间线对齐只读视图）

| 交付 | 内容 |
|---|---|
| 数据装配（app 层） | `app/cam_timeline.{h,cpp}`：`buildCamLanes(caseDir, videos)` 逐已校时视频读案内 .vla（校时 SSOT）→ `wallMsOf(0)/wallMsOf(maxStream)` 墙钟块位；未校时/.vla 缺失/无分析数据跳过；按墙钟起点升序 |
| 只读视图（ui 层） | `multicamview.{h,cpp}`：一机位一行，块位=墙钟、宽∝已分析时长；**覆盖率扫掠**——≥2 机位同覆红色叠加带、零覆盖灰纹缺口带（跨行竖带+时长标签）；刻度首标签含日期（≥1 天跨度切 MM-dd）；DataPalette 逐行着色；悬停 tooltip（墙钟起止/时长/校时徽标）；双击块发 laneActivated（只读，仅供打开） |
| 置灰 | `CaseManager::calibratedVideoCount()`（徽标缓存口径）；案件菜单 aboutToShow 动态判定 <2 路已校时置灰；对话框内单路兜底提示 |
| 入口 | 案件菜单「多机时间线(&M)...」；MainWindow 接 laneActivated → effectivePathFor 打开该路（包内副本兼底命中） |

实施固化：① 装配与视图分层——`buildCamLanes` 纯函数可测（case_test 组 14），
视图只消费结构不碰 IO；② 块位宽度数据源是 **.vla 已分析最大流内时刻**
（方案口径"各路 .vla 校时→墙钟块位"，不依赖源视频在场，轻量包/缺源场景
可用）；③ 画法参数与 ClipTimelineWidget 对齐（容差 2000ms/刻度步进表/
fmtSpan 时长串），多机语义差异：重叠=互为印证（≥2 同覆）、缺口=零覆盖。

#### 测试状态（任务 14）

| 测试 | 结果 |
|---|---|
| **lumenarc_case_test** | **228/228**（新增组 14：未校时跳过/徽标计数/排序/墙钟起止/重叠可推出/徽标文案携带；.vla 真实 saveToFile→loadFromFile 往返） |
| 既有回归 | 零修改全绿 |

### 23.10 M3 任务 15 完成内容（文档与封版）

| 项 | 内容 |
|---|---|
| MANUAL | 新增「十四、案件管理（v1.3）」：何时用/新建打开/证据树徽标表/入案行为/前处理导入/指纹校验/单路+批量重定位/移交包/多机视图/模式出口+取证红线；快捷键表补 Ctrl+W；标题 v1.2→v1.3 |
| README | 功能表补「案件管理（v1.3）」行；快捷键表补 Ctrl+W |
| 手工点检清单 | `docs/RELEASE_CHECKLIST_V1.3_CN.md`：A 生命周期/B 入案随案/C 指纹校验/D 重定位/E 移交包/F 多机视图/G 双模式逐点比对/H 封版确认（§4.3 手工矩阵全展开，待实机跑） |
| 版本号 1.3.0 | CMakeLists project(VERSION)+MACOSX 两项 / aboutdialog / app.rc 两项（实测属性 1.3.0.0）/ Info.plist 两项；**附带回追修正**：主窗标题 7 处滞留 v1.1.1 → v1.3.0（v1.2.2 封版漏改项） |
| 全回归 | case_test 228 / calibration 73 / piecewise 96 / preprocess 168 / ui_chain 23 / ocr_atpositions 21 / vla 3×PASS 全绿；LumenArc.exe offscreen 10s 无崩溃 |
| 打标签 | `v1.3.0` |

**遗留（v1.4.0 视野）**：⚠ 错读点"OSD 疑似错读，时间不可信"随报告标注
（v1.2.2 挂账项）；报告模块（CaseMeta 头部/强制哈希清单/reports 登记位/
多机视图截屏入报告）；手工矩阵 A-H 实机跑。

### 23.11 封版后全面自检与修复（2026-08-14，post-v1.3.0 标签）

封版（`f81790e` + 标签 `v1.3.0`）后按"整个案件系统全面自检"要求补做了
**端到端集成自检**，并抓到且修复了一个封版代码携带的真实 bug。

| 项 | 内容 |
|---|---|
| 新增 `lumenarc_case_e2e` | `tests/case_e2e_main.cpp` 单一连续流程：建案→**中文路径/中文文件名**视频入案→指纹队列→校时徽标/框选记忆/lastVideoId→关案重开全恢复→删源缺失→批量重定位（一致採用+异尺寸 force 留档）→`videoRelocated`→`VideoStateManager::migrateKey` 挂接（仿 MainWindow 接线）→全量校验→完整包导出→**改名候选目录模拟换机**→包内副本兼底零操作→篡改包内副本校验必报→轻量包→接收端重定位后一致。**51/51 全绿** |
| **真 bug 修复** | `ExportTask` **pkgDir 从未创建**的边缘场景：轻量包 + 案内无任何被收集文件（无 .vla/前处理/报告/快照）→ ③④ 循环空转 → ⑤ `CaseModel::save` 的 QSaveFile 在不存在目录中必失败 → 导出失败。完整包被 sources 复制的 mkpath 掩盖、正常案件被 ③ 的 .vla 掩盖，任务 12 测试未命中。**修复：exists 拒绝后立即 `QDir().mkpath(pkgDir)`**（writeManifest 同类隐患随之消除）。**v1.3.0 标签代码含此 bug**，实际触发需"纯新案件导轻量包"，概率低但确定性 |
| 语义对齐 | `exportPrecheck` 缺失判定与 sources 收集改用 `effectivePathFor`（与哈希队列/完整性校验同语义）：完整包接收端自检不再误报缺失；**包再导出完整包以包内副本为源不丢副本**（副本名仍按原名登记，不复发前缀） |
| e2e 场景固化 | 换机模拟必须让重定位后的源路径**真的不存在**（改名候选目录），否则 effectivePathFor 正确命中原路径、走不到副本兼底分支——产品行为本身是对的，属测试场景设计点 |
| 回归 | e2e 51 / case_test 228 / calibration 73 / piecewise 96 / preprocess 168 / ui_chain 23 / ocr_atpositions 21 / vla 3×PASS 全绿；offscreen 冒烟 10s 无崩溃 |

**标签决策（待拍板）**：以上修复在 `v1.3.0` 标签之后，建议累积至
v1.3.1 一并出（或拍板重打 v1.3.0）。

### 23.12 v1.3.1 修复版封版（2026-08-14）

按拍板「修复累积到 v1.3.1 一并出」封版，标签 `v1.3.1`。

| 项 | 内容 |
|---|---|
| 版本号 1.3.1 | CMakeLists project(VERSION)+MACOSX 两项 / aboutdialog / **app.rc 四项**（字符串两项 + **数值 FILEVERSION/PRODUCTVERSION 回追修正**：v1.3.0 封版只改了字符串，数值滞留 1,2,0,0 → 本次 1,3,1,0）/ Info.plist 两项 / 主窗标题 7 处 |
| 包含修复 | `0da1400`：ExportTask pkgDir 未创建（轻量包+纯新案件导出必失败）+ exportPrecheck/sources 收集 effectivePathFor 对齐；端到端自检 `lumenarc_case_e2e` 51/51 |
| 回归 | e2e 51 / case_test 228 / calibration 73 / piecewise 96 / preprocess 168 / ui_chain 23 / ocr_atpositions 21 / vla 3×PASS 全绿；offscreen 冒烟 10s 无崩溃；**exe 文件版本实测 1.3.1.0 / 产品版本 1.3.1.0**（字符串+数值一致） |

注：`v1.3.0` 标签保留（历史节点，含轻量包边缘 bug）；用户侧升级路径 =
直接用 v1.3.1。

### 23.13 人工测试反馈修复：前处理成果列表不刷新 + 路径可见性（2026-08-14）

真机人工测试（案件模式 + 素材转码拼接）反馈：「处理完后文件点击按钮无法
导入案件列表，也不知道保存路径在哪里」。排查结果：

| 现象 | 根因 | 修复 |
|---|---|---|
| 处理完成案件列表无会话条目 | **非登记失败**——`addPreprocessSession` 已把会话/输出引用/sidecar 写入 case.json（关案重开可见），但 `MainWindow::refreshDock` 只连了 videoAdded/Removed/InfoChanged/hashProgress/hashQueueFinished，**漏连 `caseSaved`** → 登记落盘后 dock 不刷新，看起来"没导入" | `mainwindow.cpp` 补 `connect(caseSaved → refreshDock)`（一行） |
| 不知道保存路径 | 横幅只显示案件标题 + tooltip 相对路径；结果页虽有完整路径（可选中复制）与【打开输出文件夹】按钮，但登记附注文案不含路径 | 结果页附注改为「已登记案件：**<会话绝对路径>**（sidecar 归类 sidecars/）」 |
| 边缘 | 单文件已合格 MP4（noOp 分支）提前 return 未清 `m_caseSessionDir` → 下次处理复用旧时间戳会话目录 | return 前 `m_caseSessionDir.clear()` |

**答案（用户侧）**：案件模式处理完 → 成果在 `<案件目录>/preprocess/<时间戳>/`，
自动登记 case.json 并**立即**出现在案件列表「前处理会话」组（本次修复后），
条目内 P### 输出文件双击可播放，sidecar 校时文件归类同会话 `sidecars/`。

回归：ui_chain 23 / case_e2e 51 / case_test 228 全绿；offscreen 冒烟 8s 无崩溃。

### 23.14 前处理转码拼接修复：统一 CFR + 时间戳归零 + 中间产物清理 + 统计提示（2026-08-14）

真机人工测试发现 `merged_concat.mp4` 播放到尾段卡住。取证（帧级 PTS 分析 +
66 段源素材全量 probe + 完整解码）：

| 证据 | 数值 |
|---|---|
| moov 声明时长 | 59:52.7（3592.7s） |
| 实际帧时间轴末尾 | **2806.7s**（差 786s ≈ 13 分钟无帧 → 尾段卡住） |
| DTS 非单调 | 完整解码报错重复 950 次，第 2223 帧（第 4 段接缝 ≈278s）PTS 回退 0.25s |
| 源素材真实帧率 | **12.5fps×57、8fps×8（前 5 分钟静止降帧）、9.17/11.93 各 1**；tbr 标称 25/50/13 为 DVR 乱写（实测仍是 12.5） |
| 转码产物 | 无 -r 统一 → 段间帧率节奏各异；首帧偏移 0.138~0.23s 不归一 |

**根因**：转码未统一帧率/未归零时间戳 → concat demuxer 按时间戳拼接时接缝
错位逐段累积 → 尾部无帧。修复：

1. **统一 CFR**（transcode_engine）：`-r <统一帧率>`（= 全局最大 avg fps，
   四舍五入 0.1、上限 60；低帧率段重复帧差分编码体积≈0）
2. **时间戳归零**：`-vf setpts=PTS-STARTPTS`（与 yadif 合并）；音频取消直拷、
   恒 aac 128k/48k + `-af asetpts=PTS-STARTPTS`（重编码 PTS 天然归零）
3. **keyframeInterval 按统一帧率换算**（2s GOP）
4. **中间产物清理**（需求：只保留最终拼接文件）：拼接成功后删除该组转码段
   + 证据目录 `norm_*.mp4`，operations.log 留痕；**失败场景保留**便于排查；
   证据目录 concat_list 留档不动，取证不受影响
5. **统计入日志 + 完成页醒目提示**：探测完成后 operations.log 输出帧率/
   编码/分辨率分布 + 统一帧率预告（⚠ 帧率不统一时）；结果页新增醒目
   统计卡片（`buildProbeStatsText`，与 coordinator 同公式同源）

**验证**：
- 三段合成（8fps+0.25s 偏移 / 12.5fps / 25fps）走修复后链路：转码产物全部
  12.5fps、tbn 统一、start 一致；concat 后 **152 帧 PTS 全程单调**、首帧 0、
  末帧 12.08 ≈ 元数据 12.16（修复前 2806 vs 3592 背离）
- preprocess_integration：probe/sort/precheck/concat/transcode 全过
  （6 项 OCR 失败为素材无文字时间戳的环境性失败，与本次无关）
- 回归：case_e2e 51 / case_test 228 / piecewise 96 / preprocess 168 /
  ui_chain 23 / ocr_atpositions 21 / calibration 73 全绿；冒烟 8s 无崩溃

### 23.15 音频直拷保留原始层级 + 案件列表外部变更自动刷新 + 删除按钮（2026-08-14）

人工测试后续三条反馈：

**① 音频转码尽量不破坏原始数据层级**（transcode_engine / coordinator）
- 组级音频策略（探测驱动）：组内全部 AAC **且参数一致**（codec/采样率/
  声道）→ `-c:a copy` 直拷，原始音频数据零损失；实测 8kHz mono AAC
  直拷后原样保留
- 有异参（concat demuxer 会静默丢弃后续异参音轨）或非 AAC → 整组
  重编码 aac，**保留组内首个参数档**（原采样率/声道，不再强制 48k/2ch），
  `asetpts` 归零时间戳；日志注明策略选择
- 验证：直拷路径 concat 后完整解码零时间戳报错、音频流保留

**② 案件目录外部文件变动自动刷新**（casedock）
- 2s 轻量轮询（案件打开且面板可见时）：案内引用文件（视频源/副本/
  会话/输出/sidecar/报告）存在性快照对比，变化即重建证据树——资源
  管理器删文件/改名后软件内自动反映（缺失条目显示 ✗ 徽标）

**③ 删除误入/不需要的视频**（casedock）
- 面板顶部新增【🗑 删除选中】（选中视频条目时可用）+【🔄 刷新】按钮；
  右键菜单新增「删除视频文件（含源文件）…」
- 删除 = 源文件 + 案内数据（.vla/校时帧）一并删除，对话框明示路径与
  「不可恢复」警告；**包内副本（📦）保留**（保护完整包取证完整性）；
  删除后自动落盘 case.json
- 与既有「移除出案件」（不删源文件，取证红线）语义区分，两级操作并存

回归：case_e2e 51 / case_test 228 / piecewise 96 / preprocess 168 /
calibration 73 / ui_chain 23 / ocr_atpositions 21 全绿；冒烟 8s 无崩溃。

### 23.16 删除策略修正：按源文件归属分级（2026-08-14）

人工反馈拍板删除逻辑，修正 §23.15 的实现（原为无条件删源文件）：

| 源文件位置 | 删除内容 |
|---|---|
| **案件目录外**（如桌面监控目录） | 只删案内分析结果（.vla / 校时证据帧 / case.json 登记），**源文件保留**（取证红线：用户素材不因误删分析结果而丢失） |
| **案件目录内**（如 videos/ 下） | 源文件 + 案内数据一并删除（它本就是案件一部分） |

- 判定：`caseDir.relativeFilePath(originalPath)` 不以 `..` 开头且非绝对路径
- 对话框按场景显示不同文案（明示"源文件保留/将一并删除"）
- 包内副本（📦）仍不主动删（保护完整包取证完整性）
- 按钮 tooltip 与右键菜单文案同步修正

回归：case_e2e 51 / case_test 228 全绿；冒烟 6s 无崩溃。

### 23.17 锁残留自动清理 + 打开案件改页面内居中面板（2026-08-14）

人工反馈：① 每次打开案件都弹「残留锁/强制打开」；② 打开案件模式希望
在软件页面内完成（Blender 式居中），不要一开始就弹窗。

**① 残留锁自动清理**（case_manager）
- 锁文件含 `pid=`；`isLockStale()`：持有者进程已不存在（OpenProcess
  PROCESS_QUERY_LIMITED_INFORMATION 失败）→ 残留锁，**自动删除并正常
  打开，不弹窗**；解析失败/无 pid 也视为残留
- 进程仍在 → 真双开冲突才置 lockConflict（弹窗保留，文案去掉
  「上次未正常关闭」歧义）
- 注：此前「每次打开都弹」的直接诱因之一是开发冒烟用 kill 强杀
  进程留锁；现在此类残留自动接管
- 回归：case_test 新增 stale 锁用例（写死 PID 99999999 → 打开成功、
  不报冲突），231/231 全绿

**② 打开案件页面内居中**（case_open_panel.{h,cpp} 新文件 + MainWindow）
- `CaseOpenPanel`：非模态卡片（560×440）浮于主窗口内容区中央
  （resize 自动居中）——大标题 + 最近案件列表（目录名+路径，双击打开）
  + 【新建案件…】【浏览案件目录…】【✕ 关闭】
- 案件菜单「打开案件」/ 起始页入口 → 显示面板（不再直接弹系统
  文件对话框）；面板内「浏览」才用 QFileDialog；打开成功自动隐藏
- 空状态「暂无最近案件」；Blender welcome 式体验

回归：case_test 231 / case_e2e 51 / ui_chain 23 全绿；冒烟启动 6s 无崩溃
（强杀退出留锁，由 ① 自动清理——用户案件锁文件实测残留，下次打开
不再弹窗）。

### 23.18 起始页改页面内欢迎面板：主界面打开后才出现（不再启动即弹窗）

人工反馈：「目前没有进入主界面就已经弹窗了，希望进了启动页面结束、主界面
打开以后才有弹窗」。根因：启动起始页是**模态 QDialog**（`QTimer::singleShot`
后立即 `exec()`），盖在主界面之前弹出。

改动：
1. **删除模态起始页**（startpagewidget.{h,cpp} 移除出工程）
2. **并入 CaseOpenPanel**（页面内居中欢迎面板，非模态）：
   - 标题「追光者 Lumen Arc」+ 副标「案件 = 证据容器」
   - 按钮行：新建案件… / 浏览案件目录… / **独立模式** / ✕ 关闭
   - 最近案件列表（双击打开）+ 空态引导
   - 「启动时不再显示」勾选（迁自起始页 QSettings case/showStartPage，
     即时生效；案件菜单「起始页…」随时可再次打开面板）
3. MainWindow：启动 `QTimer` 时若开启 → 显示面板（主界面已 show 之后，
   QTimer::singleShot(0) 本就在事件循环首轮）；案件菜单「起始页…」同入口

流程：启动 → 主界面打开 → 中央欢迎面板（页面内）→ 新建/打开/独立/关闭；
案件相关弹窗（新建对话框、真双开锁冲突）都在主界面打开后发生。

回归：case_test 231 / case_e2e 51 / ui_chain 23 / preprocess 168 全绿；
冒烟 7s（含欢迎面板路径）无崩溃。

### 23.19 移除启动画面（QSplashScreen）——彻底消除先于主界面的窗口（2026-08-14）

人工反馈「目前还是先于主界面的弹窗」：前两轮已改锁弹窗与模态起始页，
但**启动画面 QSplashScreen**（780×450 独立置顶窗口，主界面构造前 show）
一直存在——它就是用户看到的先于主界面的窗口。

改动：
- main.cpp 删除 QSplashScreen 全部逻辑（createSplashPixmap 函数 +
  show/processEvents/finish 序列），启动 = 直接构造 MainWindow →
  showMaximized；文件头注释更新
- 顺带修复：CMakeLists 残留 startpagewidget.h 引用导致 reconfigure
  失败（上一提交只删了 .cpp 条目）；重新 configure 后 vcxproj 恢复
  src/main.cpp 条目（此前链接 LNK2019 即因 vcxproj 缺 main.cpp 且
  增量构建未触发重编译）

回归：case_test 231 / case_e2e 51 / ui_chain 23 / preprocess 168 /
piecewise 96 全绿；冒烟 6s 无崩溃。

启动序列（最终）：主界面直接打开 → 中央欢迎面板（页面内，非模态）
→ 新建/打开/独立/关闭。任何窗口不再先于主界面出现。

### 23.20 恢复启动画面 + 欢迎面板重新设计（2026-08-14）

人工反馈：启动画面（QSplashScreen）是重要的「启动页面」，必须恢复；
中央欢迎面板保留但现版不好看，需要重新设计。

改动：
1. **恢复 QSplashScreen**：main.cpp 回滚至带启动画面版本（splash 属
   正常启动流程，非「先于主界面的弹窗」——用户此前抱怨的是模态起始页）
2. **欢迎面板重新设计**（case_open_panel，页面内居中非模态）：
   - 品牌头部：26px「追光者 Lumen Arc」+ 副标「案件 = 证据容器」
   - 三枚大按钮（150×56）：新建案件…（品牌金强调）/ 打开案件… / 独立模式
   - 最近案件列表（目录名+路径，缺失目录灰显，双击打开）
   - 空态引导文案（暂无最近案件 → 引导新建/独立模式）
   - 底部：启动时不再显示（即时生效）+ ✕ 关闭
   - 面板 600×480，圆角卡片 + hover 反馈

最终启动序列：QSplashScreen（启动页面）→ 主界面打开 → 中央欢迎面板
（页面内）→ 新建/打开/独立/关闭。

回归：case_test 231 / case_e2e 51 / ui_chain 23 全绿；冒烟 6s。

### 23.21 外部删除自动清除列表条目 + 锁弹窗文案定稿（2026-08-14）

人工反馈：① 资源管理器删文件后列表条目未清除；② 打开案件仍见残留锁
弹窗（旧文案）。

1. **外部删除自动清理**：CaseManager::pruneMissingFiles——移除登记中
   「文件确实不存在」的引用（视频：原路径与有效路径均缺失才移除；
   会话输出文件缺失→移除该输出引用；会话目录缺失→整会话移除；报告
   缺失→移除），不删任何现存文件，完整包接收端（包内副本在场）不受
   影响。CaseDock 2s 轮询发现变化 → prune → saveCase → refreshTree，
   资源管理器删文件后列表条目自动消失。case_test 新增 8 项断言
   （239/239 全绿）
2. **锁弹窗文案定稿**：残留锁已自动清理，弹窗只出现在真双开（持有者
   进程仍在）——文案改为「案件正在另一个实例中打开…仍要强制打开吗？
   （建议先关闭另一实例）」，去掉「残留锁」表述

排查说明：用户仍见旧弹窗 = 运行的是旧构建（锁文件实测为空文件，
新逻辑按残留自动接管；旧逻辑遇空锁必弹窗）。已给出验证方法：
exe 时间戳 17:15+ / 关于对话框 v1.3.1 / 任务管理器确认无旧进程。

回归：case_test 239 / case_e2e 51 / ui_chain 23 全绿。

### 23.22 右键删除补齐 + 音频取证 + 文件名时间自动排序（2026-08-14）

人工反馈三连，逐一排查：

**① 右键没有删除选项——根因**：删除菜单此前只实现于「视频」条目分支；
前处理会话/输出/sidecar/报告/快照条目无删除。补齐：
- CaseManager 新增 removePreprocessSession / removePreprocessOutput /
  removeReport / removeSidecar（删文件可选，均落盘登记）
- CaseDock：session 子项存会话索引、output 子项存输出索引、报告存
  索引；右键新增「删除会话与文件…」「删除输出文件…」「删除文件…」
  （sidecar/报告/快照按归属分支），全部带不可恢复确认

**② 音频语谱高频缺失——取证结论（无损）**：
- 源音频实为 **pcm_alaw 8kHz mono（G.711 电话带宽 ~3.4kHz）**——语谱
  高频缺失是监控音频**固有特性**，非拼接损失
- 实测：源与拼接产物 8kHz+ 高频能量完全一致（mean -91.0dB）；AAC 段
  直拷路径下拼接逐样本无损
- 尝试 alaw 直拷 → **mp4 容器不支持 alaw sample entry（muxer 拒绝）**，
  故 alaw→aac（保留 8k mono）是容器限制下的最小有损，维持现状并注释
- 整改方案（可选，待拍板）：输出容器改用 .ts/mkv 可无损承载 alaw；
  或接受 aac 8k（推荐，播放兼容性优先）

**③ 明确时间排序仍混乱——根因**：buildListOrderGroups 按**导入顺序**
成组，无文件名时间解析。新增 `sortFilesByNameTime`：解析
`20260722-050041` 式时间戳（正则 `(\d{8})[-_]?(\d{6})` 变体），稳定
排序；无时间戳文件按文件名排末尾；日志记录「已按文件名时间戳自动
排序：首 → 尾」。探测完成即生效（默认组），OCR 自动排序路径不变。

回归：case_test 239 / e2e 51 / piecewise 96 / preprocess 168 /
ui_chain 23 全绿；冒烟 6s。

### 23.23 音量曲线切换视频"短一截"修复 + 排序验证（2026-08-14）

**① 音量曲线短一截（真 bug，已修复）**：
- 根因一：`ChartPanel::onDataReplaced` 对空快照直接 return——切换视频后
  旧音量曲线残留、X 轴换成新视频时长 → 曲线"短一截"（或错位）
- 根因二：rebuild 分支 `rebuildSeries(); return;`——rebuildSeries 只
  重建**空** volumeSeries 不填充数据 → 有亮度数据的视频切换后音量
  曲线为空（需重新分析才恢复）
- 修复：空快照分支清空并隐藏 volumeSeries；rebuild 分支去掉 return，
  继续走后半段统一填充亮度+音量（幂等）

**② 排序（04 时段验证）**：
- 79 个文件名全为 `20260722-HHMMSS[M]` 格式；parseFilenameTimestamp
  M2 正则匹配 ✓（补 M 后缀回归用例，preprocess_test 170 全绿）
- 04 目录按文件名时间排序实测单调递增（040007M → 045938）——排序
  逻辑正确；若用户仍见乱序 = 运行旧构建（f75c00f 之前的 exe 无
  sortFilesByNameTime），验证方法：关于对话框 v1.3.1 + exe 时间戳

回归：case_test 239 / e2e 51 / piecewise 96 / preprocess 170 /
ui_chain 23 全绿；冒烟 6s。


---

> **2026-08-14 第六批归档动作**：以下为 HANDOVER 第一批（2026-08-13 晚移交记录，
> §0-8：项目规则 R1/R2 + 交接摘要 + 问题 A/B 排查 + 音频取证结论 + 技术债），
> 依规则 R2 整体移入，原文未删改。

# ============================================================================
# 移交记录（2026-08-13 晚）——用户对近期修复结果不满意，交下一任专家接手
# ============================================================================

## 0. 项目规则（2026-08-14 用户拍板，必须遵守）

**规则 R1：每次工作完成，必须自动在 HANDOVER.md 记录进度**——包括：
做了什么、验证了什么、遗留了什么。与代码提交同步进行，不得遗漏。

**规则 R2（2026-08-14 用户拍板）：HANDOVER.md 只保留最近 5 次更新**——
超出部分依修改顺序（时间序）整体移入 WORK_HISTORY.md 存档（存档只增不改）；
**每次写完 HANDOVER 与 WORK_HISTORY 后，同步更新两份文档的表头**（当前
HEAD、保留批次清单、最近归档动作）。

## 1. 交接摘要

- 仓库 HEAD：`5adfff5`（v1.3.1 之后的 10 个修复提交，见 git log）
- 工作区干净（仅 build_tmp/ 杂物未跟踪）
- 构建：`cmd //c "build_tmp\build_target.bat ALL"`；测试运行：
  `QT_QPA_PLATFORM=offscreen` + PATH 含 `C:\code\Qt\6.8.0\msvc2022_64\bin`
- 全回归基线：case_test 239 / case_e2e 51 / piecewise 96 / preprocess 170 /
  ui_chain 23 / calibration 73 / ocr_atpositions 21 / vla 3×PASS

## 2. ⚠ 用户确认「未修复」的两个问题（最高优先，需真机复现排查）

### 问题 A：来回切换视频，已生成的音频音量曲线"短一截"，重新分析才恢复
- 用户操作：视频 A 分析音量曲线 → 切视频 B → 切回 A → 曲线短一截/不完整
- 我已改：`src/chartpanel.cpp`（提交 5adfff5）——
  ① 空快照分支清空隐藏 volumeSeries（原来残留旧曲线 + X 轴换新时长）
  ② rebuild 分支去掉 return，继续填充音量
- **用户测试后仍复现**。可能原因（需专家排查）：
  a) 用户运行的是旧 exe（用户多次出现"看不到更新"现象，疑似未重启；
     但本次用户明确说没修复，不能只归因于此）
  b) 我的修复不完整：音量曲线显示路径可能不止 chartpanel（spectrogram
     panel 的音量曲线？`src/spectrogrampanel_enhanced.cpp` 550 行有
     volume.size 日志）——需真机 GUI 复现（offscreen 无法复现 GUI 切换）
  c) 曲线数据源 TimelineModel/AudioData 在切换视频时未正确重置
- **排查路径**：真机打开两个视频 → 分析 A 音频 → 切 B → 切回 A →
  观察曲线；加 qDebug 跟踪 onDataReplaced/volumePointsForViewport；
  确认运行 exe 路径与构建时间（19:46 后）

### 问题 B：导入拼接的文件在有明确时间排序的情况下仍混乱
- 用户测试目录：`C:\Users\MJ\Desktop\20260722广州增城\监控视频\食咔咔
  烤肉店\2026-07-22\04`（79 个 `20260722-HHMMSS[M].mp4`）
- 我已改：`src/app/preprocessing_coordinator.cpp` `sortFilesByNameTime`
  （提交 f75c00f）：解析 `20260722-040007` 式时间戳稳定排序，buildList
  OrderGroups（默认组）探测完成即生效
- **验证过**：04 目录按文件名时间排序实测单调递增（040007M→045938）；
  parseFilenameTimestamp M2 正则带 M 后缀测试通过（preprocess_test 170）
- **用户测试后仍复现**。可能原因（需专家排查）：
  a) 用户运行旧 exe（f75c00f 之前无此功能）——需确认用户运行路径
  b) 用户走的不是默认组路径：点了「自动排序」（runAutoSort → OCR 引擎
     不可用 → 降级 → smartSort）——smartSort 有文件名证据（parse
     FilenameTimestamp）应该也排好，但需真机确认 OCR 降级路径实际行为
  c) 用户在**校对页**看到的顺序 vs **拼接产物**顺序不一致（拖拽改序后
     未重置？）
- **排查路径**：真机导入 04 目录 → 看校对页顺序与日志「已按文件名时间戳
  自动排序」是否出现 → 不出现则确认 exe 版本；出现但仍乱则跟踪
  m_groups.ordered

## 3. 近期已提交但用户尚未确认/或存疑的改动（按提交序）

| 提交 | 内容 | 状态 |
|---|---|---|
| a3e3558 | v1.3.1 封版（轻量包导出 bug 修复 + e2e 自检 51/51） | 用户之前确认过拼接测试通过 |
| 9001f8a | 转码统一 CFR + 时间戳归零 + 中间产物清理 + 统计提示 | 用户确认「测试了一个没问题」✓ |
| c424bbf | 音频直拷（组级）+ 案件列表外部变更自动刷新 + 删除按钮 | 删除逻辑后被用户修正过 |
| 0ae94a9 | 删除按源文件归属分级（案外只删分析结果） | 用户拍板逻辑 ✓ |
| ed04a45 | 残留锁自动清理 + 打开案件页面内面板 | 用户后续反馈仍见旧弹窗（疑旧 exe） |
| a9d7b79 / 1a50606 | 起始页改页面内面板 / 移除启动画面 | **用户拍板回滚**（42f192d 已恢复 splash） |
| 42f192d | 恢复启动画面 + 欢迎面板重设计 | 用户未确认视觉效果 |
| 2cdc77f | 外部删除自动清除列表条目（pruneMissingFiles）+ 锁文案 | 用户未确认 |
| f75c00f | 右键删除补齐（会话/输出/报告/快照）+ 文件名排序 + 音频取证 | 排序用户确认未修复 |
| 5adfff5 | 音量曲线短一截修复 + M 后缀排序用例 | 用户确认未修复 |

## 4. 音频取证结论（已定案，勿重复调查）

- 监控源音频 = `pcm_alaw` 8kHz mono（G.711，带限 ~3.4kHz）——语谱高频
  缺失是**源固有特性**，拼接产物与源 8k+ 能量完全一致（-91dB），拼接无损
- alaw 无法直拷入 mp4（mp4 无 alaw sample entry，muxer 拒绝）→
  alaw→aac（保留 8k mono）是容器限制下的最小有损（维持现状）
- 取证文件：build_tmp/aud_src.wav、aud_merged.wav、src.pcm、merged.pcm

## 5. 已知技术债/待办

- 锁 stale 检测用 OpenProcess（Windows-only，case_manager.cpp 含 windows.h）
- CaseOpenPanel 欢迎面板视觉待用户确认（用户曾要求重设计，42f192d 已做）
- 中间产物清理：拼接成功删转码段 + norm 文件；失败保留（已生效，用户
  154619 会话目录实测已清理）
- v1.4.0 候选：⚠错读点报告标注、报告模块、手工矩阵 A–H 真机走一遍
  （docs/RELEASE_CHECKLIST_V1.3_CN.md，建议用真机 GUI 走，用户已多轮
  真机测试发现多个 offscreen 覆盖不到的 UI bug）

## 6. 给下一任专家的建议

1. **第一件事**：在用户机器上确认用户实际运行的 exe 路径与构建时间
   （用户多次反馈"更新没看到"，疑似运行旧构建；但也可能真没修复，
   必须真机复现，不要想当然）
2. 问题 A/B 都需要 GUI 真机复现（offscreen 覆盖不到）；建议在用户
   在场时一起跑一遍，记录实际现象
3. 每次改动后立即在本文件记录（规则 R1），并明确告知用户 exe 路径
   与构建时间，要求用户完全退出旧进程再测


---

> **2026-08-14 第七批归档动作**：以下为 HANDOVER 第二批（2026-08-13 深夜，
> §9-11：音频时间轴对齐修复、问题 A/B 定案+加固、播放选项包+证据快照拍板），
> 依规则 R2 整体移入，原文未删改。

# ============================================================================
# 移交记录（2026-08-13 深夜，第二批）——问题 A 根因已修复；问题 B 定案+加固
# ============================================================================

## 9. 音频时间轴对齐修复（2026-08-14 上午）——光标/听觉差 ~1s 根因钉死

### 用户症状
① 图表/语谱图光标进度与听到的声音差 ~1s（听觉校验）；② 语谱图与音量
峰值"有时"对不齐。

### 根因（两个独立来源，主因已钉死）
**主因·线性漂移**：`analyze_video.py` 把 `time_resolution_ms` round 到 0.1
（512/24000=21.33333→**21.3**）。C++ 实时解析路径直接采信该值
（python_analysis_engine.cpp）。误差 = 0.0333/21.3333 = **0.156% 线性漂移**：
10 分钟处 ≈ 0.94s（与用户听觉校验吻合），52 分钟视频末尾 ≈ 4.9s。
且 .vla 缓存两条加载路径（timeline_model.cpp:721/855）都是 hop/sr **精确
重算**——造成"新分析的曲线漂移、重载缓存后正常"的双轨，解释了"有时"。

**次因·恒定偏移**：分析 WAV 的时间零点 = 音频流起点；播放引擎时间零点 =
**视频流起点**（ffmpeg_video_engine m_startPtsMs，音视频包同减）。音频流
晚于视频流起始时（监控导出可达 ~1s），曲线整体提前一个恒定量。用户现有
文件实测偏移 ≈0（4时视频视频流晚 128ms，04 段全 0），属隐患而非本次主因。

### 修复
1. `analyze_video.py`：`time_resolution_ms` 全精度输出（round 6 位）；
   merged 路径同步。
2. `python_analysis_engine.cpp`：timeResolutionMs 一律由 hop/sr **精确重算**，
   JSON 字段仅作 hop/sr 缺失兜底——与两条缓存加载路径对齐，旧缓存自动愈。
3. `analyze_video.py extract_audio`：新增 `probe_stream_starts`（ffprobe 探测
   流起始）；音频晚于视频 >20ms 时 WAV 头部 `adelay` 补等量静音（上限 30s
   防病态元数据；音频早于视频不裁切，保留全部声音证据），分析时间轴与
   播放时间轴强制同原点。

### 验证
- 真实 04 段（43.6s）：res=21.333333 全精度 ✓，曲线时长 43520ms ≈ 真实
  43600ms（差 85ms = 最后一个不满窗口，预期）。
- 合成 δ=+977ms 文件：旧逻辑曲线只有 19s 且起点即声音；新逻辑 WARNING
  留痕 + 头部补静音，曲线 19947ms ≈ 视频全长 20s，首个非静音点 939ms ✓。
- 回归全绿：vla 3×PASS+[bugA] / case 239 / e2e 51 / piecewise 96 /
  preprocess 170 / ui_chain 23 / calibration 73。

### 语谱图与音量峰值对应性说明
二者同音频、同 hop、同一 timeResolutionMs，修复后同一数据源任意时刻都对齐
（窗长差 2048 vs 1920 仅 ~43ms 中心偏移，亚感知）。"有时不对应"的另一成因
是 RMS 能量峰值 vs 对数频率语谱亮带的物理差异（低频重击音量显著但语谱
宽频带分散），非缺陷。

## 10. 待办新增（2026-08-14 用户提出，方案待确认）
1. 播放调节选项包：画面旋转/亮度/对比度（复用 SnapshotOverlay 的
   applyAdjustments 与滑杆交互，详见已向用户提交的方案与问题清单）。
2. 截图快照功能（证据帧画廊入案件，供报告模块）。

## 11. 播放选项包 + 证据快照（2026-08-14 下午实施，用户六项拍板后）

### 用户拍板
2=快照所见即所得（含调节后画面）；3=文件名 `视频名_校准后北京时间.png`
（未校时 fallback `视频名_tHH-MM-SS.png`）；4=快照含曲线分析区 + OSD 烧录
当前帧标签与北京时间（如有）；5=菜单/工具栏按钮调出，调出后常驻；
6=旋转常规 90° 步进即可。Q1（旋转后 ROI 坐标语义）用户未看懂，已重新
表述待选 A/B——旋转按钮本轮未启用，待拍板后实现。

### 已实现（提交待编）
- **PlaybackAdjustPanel**（新组件 src/playbackadjustpanel.*）：dock 常驻，
  亮度/对比度滑杆（±50，与截图叠加同域同式）+ 复位；工具栏「画面调节」
  按钮开关，面板点 X 与按钮态同步。
- **VideoWidget LUT 显示变换**：`setDisplayAdjust` 预计算 256 级 LUT
  （与 applyBrightnessContrast 同公式），onFrameReady 查表；默认零开销；
  保留 m_rawFrameImage 原始帧 → 暂停态拖滑杆实时预览；调节只影响显示与
  快照，分析/ROI/语谱/证据文件不动。
- **逐视频记忆**：VideoState + displayBrightness/displayContrast，
  saveState/restoreState 全链路；无状态视频自动回默认（防泄漏）。
- **证据快照**（工具栏「快照」/快捷键 S）：当前帧（所见即所得）+ 图表
  分析区（有数据才合成）竖拼 PNG；OSD 烧录当前帧标签（±1s 内最近，金色）
  + 北京时间（已校时）/相对时刻；案件打开存 `案件/snapshots/` 并即时刷新
  dock，无案件存视频同目录 `snapshots/`；重名自动避让；快捷键速查表补 S。

### 验证
- 构建一次通过；offscreen 启动 5s 无崩溃；回归全绿（case 239 / e2e 51 /
  piecewise 96 / preprocess 170 / ui_chain 23 / calibration 73 / vla PASS
  含 [bugA]）。
- 待真机 GUI 确认：面板滑杆手感、快照合成观感、OSD 字号比例。

### Q1 已拍板（2026-08-14 用户）：选方案 A——暂不开工，仅记录方案

**拍板内容**：旋转 90° 步进（0/90/180/270），覆盖物（ROI 矩形/多边形/
辅助线/时间戳框选/放大镜/截图叠加）随画面一起转，新画的框自动换算回
原视频坐标存储与分析——证据数据始终对应同一物理位置。

**施工规格（开工时按此执行，预估 1 天 + 四角度测试）**：
1. 变换管点：VideoWidget 显示链 `原始帧 → 旋转(QImage::transformed) →
   亮度/对比度 LUT → m_frameImage`；旋转后须同步
   `overlay()->setVideoSize(旋转后尺寸)`。
2. 坐标映射（双向，OverlayWidget 核心改动）：
   - 显示向：存储坐标（原视频系）→ 旋转矩阵 → 旋转后画面系，用于画
     ROI/多边形/辅助线/标签点/截图叠加框。
   - 输入向：鼠标拖拽/框选坐标（旋转后画面系）→ 逆旋转 → 原视频系存储，
     供 Python 分析（分析永远走原始帧，不进旋转）。
   - 放大镜：源区域取原始帧坐标，放大视图内单独做旋转显示（保持放大器
     内部逻辑不动）。
   - 时间戳框选（timestampRoiReady 归一化坐标）：归一化前先做逆旋转。
3. 持久化：VideoState 增 `displayRotation`（0/90/180/270），随
   displayBrightness/Contrast 同链路存取；不入 .vla（证据文件格式不动）。
4. 快照：所见即所得——合成图视频部分用已旋转+已调节帧（现有
   currentFrame() 即得，无需额外工作）；PNG 元数据/操作日志注明旋转档。
5. 交互兜底：旋转 ≠0 时 A/B 打点、逐帧步进、拖拽 scrub 不受影响（时间轴
   与像素无关）；倍速/音量不变。
6. 验收矩阵：四角度 ×（画矩形/画多边形/辅助线/时间戳框选/放大镜/截图
   叠加/快照/切换视频状态恢复）+ 4K 与 1440p 性能抽查（transformed 一次
   拷贝 ~10-20ms，仅旋转非零时付出）。

## 7. 问题 A（音量曲线"短一截"）——根因找到并已修复

### 真机证据链
- `%TEMP%\lumenarc_audio.log`：用户 8-13 19:37~19:48 在 **4 小时拼接视频
  （明景拼接视频_4时.mp4，8kHz mono）** 与 **~52 分钟拼接视频** 之间来回切换——
  正是"长短互切"场景。
- `明景拼接视频_4时.mp4.vla(.spec 655MB)`：4 时视频做过**纯音频分析**
  （无语义亮度数据）——触发 buggy 路径的必要条件。

### 根因（两处叠加，缺一不可）
1. **残留时长填充**：`openVideoFile` 切视频时 `m_currentDurationMs=0`，但
   `ChartPanel::m_durationMs` 从不清零——恢复状态 `setData` 触发
   `onDataReplaced` 时图表时长仍是**上一个视频的**。纯音频快照走
   `xMax = max(m_durationMs, dataMax=0)` = 旧短视频时长 → 音量曲线只填充前半段。
2. **时长到达后不重填**：引擎 `durationChanged`（正确时长）到达时
   `ChartPanel::setDuration` 只在 `!snapshot().isEmpty()`（有亮度数据）时
   重填；纯音频快照 `isEmpty()==true` → 只 setRange 不填数据 → **曲线永远
   停在旧视频时长处**。重新分析触发 setData 才恢复——与用户描述逐字吻合。

### 反证实验（钉死根因）
新增回归场景（vla_load_test `[bugA]`）：stale 52min → setData(4h 纯音频)
→ setDuration(4h)。**旧代码**：曲线末端 3,120,000ms = 恰好 52min（B 的时长），
4h 数据只画 21.7%，setDuration 后仍截断——复现用户现象；**新代码**：两步后均
覆盖 99.99%（14,398,720/14,400,000ms，差值为 8000 点下采样 stride）。

### 修复（src/chartpanel.cpp 两处）
1. `setDuration`：重填条件 `!isEmpty()` → `!isEmpty() || hasAudio()`。
2. `onDataReplaced` X 上限：`dataMax` 计入 `audio.durationMs()`——即使拿到
   残留/错误的时长，音量曲线也始终按音频全长铺满。

回归：vla 3×PASS+[bugA] 新增 PASS / case 239 / e2e 51 / piecewise 96 /
preprocess 170 / ui_chain 23 / calibration 73 全绿。

## 8. 问题 B（拼接顺序"混乱"）——代码定案：当前构建已正确；加固两项

### 排查结论（本机实证）
1. 用真实 04 目录 79 个 `20260722-HHMMSS[M].mp4` 验证**全部三条路径**：
   - 默认路径 `buildListOrderGroups → sortFilesByNameTime`：单调递增 ✓；
   - 自动排序路径 `smartSort`：OCR 引擎本机可用（rapidocr 已装），对真实文件
     实测 OCR 读数正确（`04:00:07` conf 0.95），OCR/文件名证据排序一致 ✓；
   - 导入顺序（对话框字母序）对本目录 = 时间序 ✓（即使旧代码也不会乱）。
2. 04 目录无 8-13 后的拼接产物；用户最近的拼接产物停在 8-04（修复前）。
3. **本机有 5+ 个旧版 LumenArc.exe**（`LumenArc_v1.0` 旧构建 7-29、v0.5 7-25
   等），桌面/任务栏无快捷方式——用户打开任意旧副本都无法看到修复。
   `LumenArc_v1.0`（7-29）早于 §17 非强制化与全部排序工作。

**结论**：f75c00f 起排序逻辑正确；用户复现最大可能是跑了旧构建
（或 8-13 19:31 前的当次构建）。代码侧未发现仍可乱序的路径。

### 加固（本轮实施）
1. **导入页即排序**（preprocesswindow.cpp `addFiles`）：文件进入待拼接列表
   立即按 `parseFilenameTimestamp` 排序——导入页顺序 = 校对页顺序 = 拼接
   顺序，所见即所得，用户第一眼即可确认。无时间戳文件保持相对序排末尾。
2. **标题栏构建时间戳**（mainwindow.cpp）：`Lumen Arc v1.3.1 (build Aug 13
   2026 20:42)` 常驻，截图即可辨识运行的是哪个构建——根治"旧 exe 扯皮"。

### 已知取舍（follow-up，不本轮处理）
- 导入页拖拽改序后，`buildListOrderGroups` 仍按文件名时间重排（f75c00f
  语义：文件名有时间 → 时间说了算）。若需"拖拽优先于文件名排序"，加
  userReordered 标志旁路即可，等用户明确表示需要再做。

### 给用户的验证口径
1. 完全退出旧进程 → 运行
   `C:\code\LumenArc\LumenArc_v1.0 remake\build\Release\LumenArc.exe`
   （标题栏应见 `build Aug 13 2026 20:xx`，无此行=旧构建）。
2. 问题 A：开 4 时视频→音频分析→切短视频→切回：音量曲线应铺满全程。
3. 问题 B：导入 04 目录：导入页表格第一行应即 `20260722-040007M`，末行
   `20260722-045938`；拼接产物按此时序。


---

> **2026-08-14 第八批归档动作**：以下为 HANDOVER 第三批（2026-08-14 下午，
> §12：显示旋转 90° 方案 A 全量实施——覆盖物随转 + 双向坐标映射），
> 依规则 R2 整体移入，原文未删改。

# ============================================================================
# 工作记录（2026-08-14 下午，第三批）——Q1 方案 A 旋转功能全量实施
# ============================================================================

## 12. 显示旋转 90° 步进（Q1 方案 A 落地，HEAD 待提交）

### 依据
§11 Q1 已拍板：方案 A（覆盖物随转 + 双向坐标映射），施工规格六项按原记录执行。

### 已实现
1. **VideoWidget 显示链**：`原始帧 → 旋转(QImage::transformed) → LUT → m_frameImage`
   （setDisplayRotation，0/90/180/270 顺时针吸附归一化；默认零开销直通）。
   新增 `rawFrame()` 公开原始帧访问器。
2. **OverlayWidget 双向映射（核心）**：旋转烘焙进 mapToVideo/mapFromVideo——
   widget↔显示系缩放 + storedToDisplay/displayToStored 精确整数互逆，
   覆盖层 ~40 处调用点零改动。m_videoWidth/Height 保持【原视频尺寸】，
   显示尺寸由 displayVideoSize() 按档位推导（90/270 宽高互换）。
   ⚠ 对施工规格第 1 条的有意偏离：规格写「overlay()->setVideoSize(旋转后尺寸)」，
   实施改为「overlay 保持原视频尺寸 + 映射函数内部旋转」——同一显示语义，
   但消除逐调用点转换的漏改风险（规格意图双向映射，路径更安全）。
3. **放大镜**：MagnifierWidget::setDisplayRotation——源区域/光标/内部 overlay
   仍全部工作在原视频系（内部逻辑不动），ContentWidget 在显示前旋转裁剪图
   （帧与截图叠加同一档位）；recalcSourceRect 链路复用完成即时刷新。
   顺带修复预存 bug：createMagnifier 首帧原给 currentFrame()（已 LUT/旋转的
   显示帧，裁剪几何错误），改给 rawFrame()。
4. **钉图 PinnedWidget**：setDisplayRotation，裁剪后显示前旋转；缩放系数按
   旋转前区域高度 30px 基准等比应用到旋转后尺寸（竖长时间戳不变形）。
5. **截图叠加（融合）**：存储始终保持原视频系方位——VideoWidget 绘制缓存随
   档位旋转（缓存键含旋转档）；grabFrameSnapshot 捕获时逆旋转回原方位
   （LUT 保持烘焙＝所见即所得拍板 #2 不变）；放大镜叠加裁剪后同档旋转。
6. **时间戳框选**：beginTimestampRoiSelection 默认 ROI 改走 mapFromVideo
   （旋转映射）；normalizedRoi 天然正确（mapToVideo 已含逆旋转 + 按原视频
   尺寸归一化——规格第 2 条「归一化前先做逆旋转」自然成立）。
7. **放大镜平移**：MagnifierPan 位移向量新增 displayDeltaToStored（轴向随
   档位置换取反；位移无 -1 偏移、无需尺寸）。
8. **持久化**：VideoState += displayRotation，saveState/restoreState/无状态
   重置/清空列表全链路；hasData 补判三个 display* 字段（仅旋转/调节过的
   视频切走切回也能恢复）。不入 .vla（证据文件格式不动）。
9. **UI**：PlaybackAdjustPanel 新增旋转行（「⟳ 顺时针 90°」循环按钮 +
   档位标签），复位键连带旋转归零；rotationChanged 信号由 MainWindow
   同步到主画面/放大镜/钉图。MANUAL.md 补「画面调节」小节。
10. **证据快照**：currentFrame() 天然所见即所得（旋转+LUT 已含，OSD 文字
    在旋转后帧上正立烧录）；旋转档 ≠0 时写 PNG 元数据
    `LumenArc:displayRotation` + 保存提示注明档位（0 档不记，旧产物字节级一致）。
11. **交互兜底**：A/B 打点/逐帧/拖拽 scrub/倍速/音量均时间轴语义，与像素
    方位无关，零改动（规格第 5 条成立）。

### 验证
- 新增 ui_chain [rotation] 场景（offscreen，真实 VideoWidget+QTest 鼠标注入）：
  四角度 ×（拖拽创建矩形=手算角点锚定 / 点击命中+拖拽移动位移方向语义 /
  辅助线端点存储系 / 多边形闭合顶点 / 时间戳框选归一化 / 默认 ROI / 绘制
  不崩）。**60 checks, 0 failures**（基线 23 → 60）。
- 全回归绿：case 239 / case_e2e 51 / piecewise 96 / preprocess 170 /
  calibration 73 / ocr_atpositions 21 / vla PASS 含 [bugA]。
- offscreen 启动 5s 无崩溃。

### 排查中固化的重要知识（后来者勿再踩）
1. **QRect(p1,p2).normalized() 的 Qt 历史 quirk**：角点交换的轴（负尺寸）
   归一化时每端损失 1px。例：QRect((100,149),(200,119)).normalized()
   = (100,120,101,29) 而非 (100,119,101,31)。**预存行为**——无旋转时反向
   拖拽（右下→左上）一直如此；旋转只是把某些拖拽方向映射为交换轴。
   取证影响 ±1~2px 可忽略，不改产品代码；测试锚点需经同一 normalized()
   构造吸收。
2. **QTest::mouseDClick 在 offscreen 时间戳下不保证触发 QtGui 双击转换**
   （第二个 press 不会变成 MouseButtonDblClick）——测双击逻辑应手构
   QMouseEvent(MouseButtonDblClick) sendEvent，确定性 100%。
3. Release 构建 QT_NO_DEBUG_OUTPUT 把 qDebug 编译掉——诊断输出用
   fprintf(stderr)。
4. offscreen 冒烟测试后必须 powershell Stop-Process 杀净（git-bash 的
   kill/taskkill 转义不可靠）——残留实例锁 exe 导致 LNK1104。

### 待真机 GUI 确认（offscreen 覆盖不到，建议用户在场走一遍）
- 四角度 ×（放大镜观感/截图叠加对齐/钉图比例/快照合成观感/拖拽手感）；
- 4K/1440p 旋转播放性能抽查（transformed 每帧 ~10-20ms，仅旋转非零付出；
  若 4K 25fps 吃紧，follow-up 可做旋转+LUT 单趟合并，本轮从简）；
- 滑杆/旋转按钮与状态栏提示文案观感。

### 遗留（不本轮处理）
- 旋转+LUT 单趟合并优化（性能余量，见上）；
- 上一批待确项不变：面板滑杆手感、快照合成观感、OSD 字号比例。


---

> **2026-08-15 第九批归档动作**：以下为 HANDOVER 第四批（2026-08-14 下午，
> §13：放大镜不吃画面调节修复 + 伽马/色阶/反色扩展），依规则 R2 整体移入，
> 原文未删改。

# ============================================================================
# 工作记录（2026-08-14 下午，第四批）——放大镜调节修复 + 画面调节参数扩展
# ============================================================================

## 13. 放大镜不吃画面调节（用户实测发现）+ 调节参数扩展

### Bug 根因
放大镜从引擎 frameReady 直接取**原始帧**（mainwindow.cpp 转发链路），
从未走 VideoWidget 的 LUT 显示链——主画面调亮度/对比度时放大视图保持原样。
钉图（PinnedWidget）同类（同样吃引擎原始帧），一并修复。

### 修复
- 新增 `DisplayAdjust` 参数包 + 共享 LUT 构建器（`src/displayadjust.h/.cpp`）；
  `applyDisplayLut` 三处共用（VideoWidget/Magnifier/Pinned），消除三份逐像素拷贝。
- MagnifierWidget::setDisplayAdjust：ContentWidget 在**裁剪→旋转之后**查 LUT
  （与主画面同一显示链顺序）；暂停态拖滑杆即时预览（m_lastFullFrame 重裁链）。
- PinnedWidget::setDisplayLut：裁剪→旋转→LUT。
- MainWindow：adjustChanged 同时下发三视图；createMagnifier/pinnedRequested/
  状态恢复/无状态重置/清空列表全链路同步。
- VideoState 改嵌 `DisplayAdjust display`（替代散字段），hasData 判
  `!display.isIdentity()`；saveState 参数由 2 个 int 并为结构体。

### 参数扩展（本轮一并实现；均为显示层 LUT 可折叠项，逐帧零额外开销）
常用取证/分析画面调节调研结论（Amped FIVE / dTective / Input-Ace 同类工具）：
| 参数 | 价值 | 本轮 |
|---|---|---|
| **伽马** | 夜暗监控提亮暗部不冲淡高光（火调现场断电夜间素材刚需） | ✅ 0.30~3.00 |
| **色阶（黑点/白点）** | 烟雾/雾霾低对比拉伸（火场视频核心痛点） | ✅ 0~127 / 128~255 |
| **反色（负片）** | 高光区域细节辨认，小众但一行可折入 LUT | ✅ 复选框 |
| 锐化/去模糊 | 车牌/人脸增强；需卷积管线，4K CPU 成本高 | ⏸ 缓（建议 GPU 管线后做） |
| 水平/垂直翻转 | 镜像安装相机；改动坐标映射（二面体群） | ⏸ 缓（旋转已覆盖 90% 场景） |
| 饱和度/色相 | 亮度量化分析用处小 | ❌ 不做 |
| 伪彩色 | 亮度→色映射；与亮度曲线语义重复 | ❌ 不做 |
| 去隔行 | 老式隔行监控；引擎侧处理更合适 | ⏸ 缓 |

LUT 流水线固定顺序：`反色 → 色阶 → 伽马 → 亮度/对比度`，恒等时零开销；
纯亮度/对比度输出与既有 applyBrightnessContrast **逐位一致**（截尾舍入，
有回归断言钉死）。

### 验证
- ui_chain 新增 runDisplayAdjustScenario：LUT 数学 8 项（恒等空表/反色/伽马
  中间调提升且端点不动/色阶端点与中点/纯亮度对比度与旧公式逐位一致）+
  放大镜像素级 3 项（均匀灰帧 128 → γ2.0 后中心像素 ~180 → 复位回 128，
  offscreen grab() 实测）。**70 checks, 0 failures**（60→70）。
- 全回归绿：case 239 / e2e 51 / piecewise 96 / preprocess 170 /
  calibration 73 / ocr 21 / vla 含[bugA]；offscreen 启动 5s 无崩溃。

### 排查中固化
- edit 工具按调用事务化：同一次 call 里任一 oldText 失配 → 整笔回滚
  （本轮 MagnifierWidget::setDisplayAdjust 声明因此丢过一次，编译期即现形）。
- Qt6 QWidget::grab() 返回 QPixmap（需 .toImage()）。
- 头文件中声明返回 QImage 的函数需 `class QImage;` 前置声明。

### 待真机 GUI 确认
- 放大镜/钉图内伽马与色阶观感（滑杆手感、暂停预览）；反色在真实素材上的可用性。
# ============================================================================

---

> **2026-08-14 深夜（副本内）第十批归档动作**：以下为 HANDOVER 第五批
> （2026-08-14 下午，§14：放大镜标识框 + 快照全面化方案拍板 Q1-Q5，暂不开工），
> 依规则 R2 整体移入，原文未删改。

# ============================================================================

## 14. 两项方案研究结论：用户已全部拍板（Q1-Q5 同意建议），暂不开工

> 本轮仅方案研究，未动代码。开工时按本节省实施，预估合计 1.5 天 + 四角度回归。

### 14.1 现状根因（排查结论，实施者必读）

**快照三问题（onSnapshotQuick，mainwindow.cpp:3648 起）**：
- **音量图被压缩**：`m_chartPanel->grab()` 按屏幕当前尺寸抓取（图表 dock 通常
  仅 ~200px 高），再 `scaledToWidth(视频宽)` 等比拉伸——曲线又糊又扁。
- **没有语谱图/放大镜**：合成时只拼了 ChartPanel，从未抓
  SpectrogramPanelEnhanced 与放大镜视图。
- **（附带发现）快照无 ROI/辅助线/多边形图形**：标注画在 OverlayWidget 子
  控件上，`currentFrame()` 只是裸帧（不含覆盖层）。

**放大镜标识框**：主画面此前无任何「放大位置」指示。数据链现成：
MagnifierWidget::m_sourceRect（原视频系）所有变化汇经 recalcSourceRect()；
旋转映射基建已就位（mapFromVideo 自动随转）。

### 14.2 拍板内容（2026-08-14 用户，Q1-Q5 全同意建议）

**Q1＝标识框样式：方案 A（四角括号 + 倍率徽章）**
- 金色（Theme::Accent）2px 四角括号 + 1px 黑色半透明衬影（亮底可读）；
  框内无填充无中线（零遮挡，取证第一原则）；倍率徽章深底金字「2.0×」，
  贴框外右上角（无空间改框内左上）。
- 仅绘制、不参与命中检测（不干扰 ROI 框选/拖拽）；放大镜 dock 开即显示、
  关即消失；光标跟随/缩放/平移时实时变化。

**Q2＝快照布局：竖向全宽堆叠**
```
┌────────────────────────────────┐
│ 视频帧（所见即所得：旋转+调节后画面，   │
│  OSD 烧录标签+时间码+文件名，          │
│  + 放大镜来源标识框烧录（Q4））        │
├────────────────────────────────┤
│ 亮度+音量曲线（全宽、足高重渲染，       │
│  含橙色时间光标）                   │
├────────────────────────────────┤
│ 语谱图（有音频分析时；同法全宽足高）     │
├────────────────────────────────┤
│ 放大镜视图（开启时）：原生裁剪分辨率，   │
│  右侧黑边烧录来源标注（坐标/倍率）      │
└────────────────────────────────┘
```

**Q3＝快照烧录覆盖层图形：烧录**。ROI 矩形/多边形/辅助线/标签点按模型颜色
全分辨率直接画到 videoPart 上（合成 PNG 是报告产物，不是原始证据；证据
文件不动）。实现：遍历模型用 storedToDisplay 映射（90° 步进精确整数式）
按显示系全分辨率绘制，不经屏幕坐标、不经 overlay grab，无损。

**Q4＝视频帧烧录放大镜来源标识框：烧录**。与屏上标识同款金色四角括号，
证据自解释（放大图从哪来）。

**Q5＝OSD 扩展：加第二行文件名**；调节参数不加（画面本身已是最终效果）。

### 14.3 施工规格（开工时按此执行）

**A. 放大镜标识框（预估 0.5 天）**
1. MagnifierWidget 加 `sourceRectChanged(QRect)` 信号，recalcSourceRect()
   末尾发射（覆盖光标跟随/滚轮缩放/中键平移/旋转/切视频全路径）。
2. OverlayWidget 加 `setMagnifierRect(QRect)`（原视频系，空=隐藏）+
   paintEvent 末尾绘制：四角括号（2px Accent + 1px 黑衬影偏移）+
   倍率徽章。零命中检测改动。
3. MainWindow：信号接线转发；removeMagnifier 时 overlay->setMagnifierRect({})。
4. 测试：ui_chain [rotation] 场景扩展——四角度下标识框显示坐标 ==
   mapFromVideo(sourceRect)（含旋转映射正确性）。

**B. 快照全面化（预估 1 天）**
1. 面板足高重渲染helper（治「被压缩」）：
   ```cpp
   panel->setUpdatesEnabled(false);
   const QSize old = panel->size();
   panel->resize(videoW, qMax(old.height(), 380));
   QImage img = panel->grab().toImage();   // 全宽原生渲染，曲线不糊
   panel->resize(old);
   panel->setUpdatesEnabled(true);
   ```
   ChartPanel 与 SpectrogramPanelEnhanced（QOpenGLWidget，grab 走
   framebuffer）同法。抓取条件沿用 `snapshot().hasAudio()`（语谱图）。
2. 放大镜段：MagnifierWidget 加 `currentMagnifiedImage()`（返回
   ContentWidget 当前 m_frameImage＝旋转+调节已应用的裁剪图，与放大视图
   逐位一致）；合成高度归一 ~320px 等比缩放，右侧黑边烧录
   「源区域 (x,y) w×h · N.N×」。
3. 覆盖层烧录：遍历 RegionModel/PolygonModel/GuideLineModel/labels，
   storedToDisplay → 按 videoPart 尺寸等比 → QPainter 直接画（模型原色）。
4. 来源标识框烧录：与 A 同款括号绘制函数（抽公共静态函数，屏上/快照复用）。
5. OSD：现有「标签+时间」保留，上方加一行文件名（白色小字，黑底阴影）。
6. 布局顺序见 14.2 图；各段之间不留白边，黑底。
7. 验收矩阵：有/无音频分析 × 有/无放大镜 × 四角度 × 有/无覆盖层；
   图表曲线清晰度目检（足高重渲染前后对比）；回归全绿 + ui_chain 扩展断言。

### 14.4 明确不做
- 快照不做 PiP（放大镜内嵌视频角）——遮挡画面，取证不妥。
- 不做屏幕分辨率整窗抓取（QScreen::grabWindow）——保真度低于原生分辨率
  合成，且会带入悬浮窗杂项。
- OSD 不烧调节参数（画面已是最终效果，参数冗余）。

# ============================================================================

---

> **2026-08-15 第十一批归档动作**：以下为 HANDOVER 第六批（2026-08-14 傍晚，
> §15：§14 拍板落地——放大镜标识框 + 快照全面化），依规则 R2 整体移入，
> 原文未删改。

# ============================================================================

## 15. §14 方案全量实施（Q1-Q5 拍板内容落地，HEAD 见本批提交）

### 依据
§14.2/14.3 施工规格逐条执行（用户已拍板，本轮直接开工）。

### 已实现

**A. 放大镜来源标识框（规格 14.3.A）**
1. `MagnifierWidget::sourceRectChanged(QRect, qreal)` 新信号：
   `recalcSourceRect()` 末尾发射（光标跟随/中键平移/旋转/切视频全路径），
   `zoomAtPoint()` 直改 `m_sourceRect` 的旁路同样补发射；无条件发射、
   去抖由接收方 `setMagnifierRect` 同值短路承担（源端旧值比对会漏掉
   「取偶后矩形不变但倍率变化」的徽章更新）。
2. `OverlayWidget::setMagnifierRect(QRect, qreal)` + paintEvent 末尾绘制
   （最上层，仅指示、零命中检测改动）：金色（Theme::Accent）四角括号
   2px + 1px 黑色半透明衬影，框内无填充无中线；倍率徽章深底金字「2.0×」
   贴框外右上角（上方无空间改框内左上）。
3. MainWindow 接线：createMagnifier 连接信号 + 立即下发初始值（setVideoSize
   的信号早于 connect）；removeMagnifier 清 `setMagnifierRect({})`——切视频
   必走 removeMagnifier，标识框不会跨视频残留。
4. 测试：ui_chain 新场景 runMagnifierIndicatorScenario——四角度下
   `magnifierRectWidget()` == 手算角点锚点（经 normalized() 吸收 quirk）、
   zoomAtPoint/updateCursorPosition 信号传播、空矩形隐藏、
   drawMagnifierIndicator 像素级（括号臂金色/框内零遮挡/徽章在位）。

**B. 证据快照全面化（规格 14.3.B）**
1. 足高重渲染 helper `grabPanelFullHeight`：临时 resize 到视频全宽 +
   ≥380/320px 高 → grab → 还原（resize 同步派发 Resize 事件，曲线不糊不扁）。
   ChartPanel 条件沿用 `!snap.isEmpty() || hasAudio()`；SpectrogramPanelEnhanced
   条件 `hasAudio()`（QOpenGLWidget grab 走 framebuffer，失败为 Null 自动跳过）。
2. 放大镜段：`MagnifierWidget::currentMagnifiedImage()`（ContentWidget 当前
   m_frameImage = 旋转+LUT 已应用裁剪图）高度归一 320px 等比缩放（超宽兜底
   按宽适配），右侧黑边烧录「源区域 (x, y) w×h · N.N×」。
3. 覆盖层烧录（Q3）：新公共静态 `OverlayWidget::burnAnnotations`——ROI 矩形/
   多边形/辅助线按「rotateStoredToDisplay + 等比缩放」映射（与 mapFromVideo
   同一整数式，支持 scrub 降采样帧）全分辨率画到 videoPart；模型原色 +
   半透明填充 + R/P 序号标签，与屏上观感一致。
4. 来源标识框烧录（Q4）：同一静态 `drawMagnifierIndicator`（屏上/快照复用），
   线宽/字号随分辨率缩放（annoPen 1~4px、括号 2~8px、徽章 10~28px）。
5. OSD（Q5）：现有「标签(金)+时间」保留，上方新增文件名行（白色小字
   0.75×，黑底阴影）；标签为空时行位自动上收。
6. 布局（Q2）：视频帧 → 曲线 → 语谱图 → 放大镜段，竖向全宽堆叠，黑底无留白。
7. 明确不做项（§14.4）均未做：无 PiP、无整窗抓取、OSD 不烧调节参数。

### 顺带重构（等价行为）
- 旋转映射提炼公共静态：`displaySizeForRotation` / `rotateStoredToDisplay` /
  `mapStoredPointToFrame` / `mapStoredRectToFrame`，OverlayWidget 成员函数
  storedToDisplay/displayVideoSize 改调静态版——屏上映射与快照烧录逐位一致，
  消除两份公式漂移风险。

### 验证
- ui_chain 70 → **92 checks, 0 failures**（新增 22 项：标识框四角度锚定/
  信号传播/隐藏、括号绘制像素级、burnAnnotations rot0+rot90 像素级、
  currentMagnifiedImage 尺寸/像素/旋转互换）。
- 全回归绿：case 239 / case_e2e 51 / piecewise 96 / preprocess 170 /
  calibration 73 / ocr_atpositions 21 / vla 5×PASS 含 [bugA]。
- offscreen 启动 5s 无崩溃（残留进程已 powershell Stop-Process 杀净）。
- MANUAL.md：§二新增「证据快照」小节（四段内容表 + 保存位置），
  §九放大镜新增「来源标识框」段落。

### 排查中固化（后来者勿踩）
- 同一 normalized() quirk 再现身：映射角点在交换轴上构造锚点必须经
  `QRect(p1,p2).normalized()` 吸收（本轮 rot90/270 锚点初版各差 1px，
  复核系手工算术失误 + quirk 叠加；实现本身逐位正确）。

### 待真机 GUI 确认（offscreen 覆盖不到）
- 标识框观感：括号/徽章在亮底暗底素材上的可读性、跟随流畅度。
- 快照产物：曲线足高重渲染清晰度、语谱图段（真实 GL 环境）抓取、
  放大镜段黑边标注排版、四角度 × 有/无覆盖层 × 有/无放大镜矩阵走查。
- 上一批待确项不变：面板滑杆手感、OSD 字号比例、4K 旋转播放性能。

# ============================================================================

---

> **2026-08-16 归档动作（规则 R2 补课）**：以下为 HANDOVER 第五批（2026-08-14
> 下午 §14 方案拍板）至第二十九批（§38 产物与视频待遇全面统一），共 25 批
> （§14~§38），因超过 R2 规定的保留 5 批上限，整体移入本文件末尾，
> 原文未删改。归档前 HANDOVER 表头自第十一批后长期未同步（已随本次
> 归档一并修复）。


---

# 工作记录（2026-08-14 下午，第五批）——放大镜标识框 + 快照全面化方案拍板（暂不开工）
# 工作记录（2026-08-14 傍晚，第六批）——§14 拍板落地：放大镜标识框 + 快照全面化
# 工作记录（2026-08-14 晚，第七批）——快照质量翻车重做：离屏重渲染路线
# ============================================================================

## 16. 用户实测：§14 v1 快照「相当烂」→ 根因确诊 + 离屏重渲染重写

### 用户实测翻车现场（真机快照 2560×3160 逐行亮度分析钉死）
- **曲线段 = 380~895px 纯背景块**（mean 25.0 恒定 = BgPanel 纯色，曲线/坐标轴
  全没画出来）：dock 内 `resize+grab()` 对 QChartView 不可靠。
- **语谱段 = 只有左半有内容**（约 1280px 宽有热力图，其余全黑）：
  `QWidget::grab()` 对 QOpenGLWidget 走旧 FBO，resize 后尺寸错位。
- 放大镜段正常但排版简陋；标识框 1px 衬影在原生分辨率下太弱。

### 根因结论（后来者勿再走此路）
**dock 内 widget 的「resize+grab」离屏路线整体不可靠**：QChartView 内容不随
grab 出来；QOpenGLWidget 的 grab 绑定旧 FBO 尺寸。快照质量不得低于
「页面截图+OSD」下限（用户原话），离屏重渲染必须**绕开 widget grab**。

### 修复（§14 v2，全部离屏、矢量/CPU、尺寸完全受控）
1. **ChartPanel::renderToImage(QSize)**（新公共方法）：`m_chart->resize(目标)`
   同步重排（plotAreaChanged 驱动标签/AB；光标项手动 updateCursorPosition）
   → `scene()->render()` CPU 矢量绘制 → 恢复原尺寸。屏幕 widget 全程不动。
   测试钉死：plotArea 626→2386 同步重排 PASS（ui_chain 无法覆盖，在 vla_test）。
2. **SpectrogramPanelEnhanced::renderHeatmapImage(QSize)**（新公共方法）：
   纯 CPU 光栅化——与 GPU 着色器同一归一化（min/max/noiseFloor）、同一
   颜色 LUT、同一视窗（viewX/Y、线性/对数频率轴）、同一橙色时间光标；
   任意目标尺寸全幅输出，**不经 GL**（测试 offscreen 直接可跑）。
3. **合成排版重设计（美工）**：段间 2px #2A2A2A 分隔线 + 34px 标题条
   （Accent 金色竖条 + 次级色标题「亮度/音量曲线」「语谱图」「放大镜视图」）；
   放大镜段 = 裁剪图（320px 高 + 金色描边）图左文右，信息块两行
   「源区域 (x,y) w×h」+「倍率 N.N× · 时刻 T」垂直居中。
4. 标识框衬影随主线同宽（原固定 1px，快照 4px 主线下太弱）。
5. 删除 grabPanelFullHeight（resize+grab 路线废止）。

### 验证
- vla_test 新增 [snaprender] 6 项全 PASS：plotArea 同步重排机制 /
  renderToImage 尺寸+widget 还原 / 全宽内容铺开 / 语谱尺寸 /
  频率轴方向（顶亮底暗）/ 时间光标像素。
- 全回归绿：vla 11×PASS / case 239 / e2e 51 / piecewise 96 /
  preprocess 170 / ui_chain 92 / calibration 73 / ocr 21。
- offscreen 启动 5s 无崩溃。
- **预览图**：build_tmp/snap_preview/ 探针用真实 V001.vla + t=20s 真实帧
  完整复刻新合成逻辑，输出 preview.png（2560×2588）——曲线全宽清晰、
  语谱全幅、光标两段对齐、放大镜图左文右、分段标题条齐全
  （预览中文字为方框系 offscreen 无中文字体，真机无碍）。

### 排查中固化
- QWidget::grab() 对 QOpenGLWidget 拿不到 GL 内容（文档明言要用
  grabFramebuffer()），且 dock 内 resize 后 FBO 尺寸错位——实测铁证。
- QChart::resize() 同步触发 plotAreaChanged（DirectConnection），
  离屏矢量重渲可行路径 = chart->resize + scene()->render + 尺寸还原。
- 探针独立 CMake 工程：build_tmp/snap_preview/（AUTOMOC 需把
  ivideo_engine.h 列入源，否则 IVideoEngine 信号 LNK2019）。

### 待真机 GUI 确认
- 新快照产物观感（用户验收）；真机中文字体渲染；
  曲线段 420px / 语谱段 300px 高度手感；放大镜信息块排版。

# ============================================================================
# 工作记录（2026-08-14 深夜，第八批）——快照 v2 真机反馈两连修
# ============================================================================

## 17. 用户实测快照 v2：语谱图无单位 + 图表 X 轴刻度轨错位

### 症状（真机 2560×2232 快照实测）
① 语谱段是纯热力图，无频率轴（用户：「单位没有了」）；
② 图表段中部横贯一条带刻度的横线（X 轴刻度轨浮在曲线中间），与底部
   时间标签脱节（用户：「坐标系的 X 轴错位了」）。

### 根因 ②（钉死，ChartPanel 预存架构弱点）
`updateTimeLabels()` 创建的**刻度线/底部基线是固定坐标 QGraphicsLineItem**，
`plotAreaChanged` 信号只驱动 `updateTimeLabelPositions()` 搬【文字】——刻度项
永不重定位。renderToImage 的 `chart->resize(目标尺寸)` 后，文字已搬新位、
刻度轨仍钉在旧 plotArea.bottom（屏上图表较矮 → 渲染图里浮在中部）。
屏上从不触发：widget 尺寸变化只来自用户拖拽，此时 Qt 事件循环里
resizeEvent→paint 序列中 updateTimeLabels 会被其他路径（rangeChanged 等）
顺带重建——离屏一次性 resize 场景才暴露。

### 修复
1. `ChartPanel::renderToImage`：resize 后补 `updateTimeLabels()`（重建刻度/
   基线，内部连带标签重排 + drawChartGuideLines），还原尺寸时同样重建。
2. **频率轴计算提炼共享 helper** `freqAxisTicks()`（spectrogrampanel_enhanced.cpp
   文件级静态）：标签样式/自适应步长/最小间距去重与原 drawAxes 逐字一致，
   drawAxes 本体改为调用它（单一事实来源，防两份公式漂移）。
3. `renderHeatmapImage` 加左侧频率轴条：轴宽 qBound(56, W/40, 96)，黑底 +
   屏上同款橙色刻度/标签（2.0k 式）+ 轴竖线 + 左上「Hz」单位提示；
   热力图/光标右移 axisW。轴条左缘 ≈ 图表 plotArea 左缘，两段视觉对齐。

### 验证
- vla_test [snaprender] 8 项全 PASS（新增：基线必须落在底部 1/4——
  bestRow=360/420 hits=591；频率轴条内容 hits=522；光标列随轴条偏移 527）。
- 全回归绿：vla 13×PASS / case 239 / e2e 51 / piecewise 96 / preprocess 170 /
  ui_chain 92 / calibration 73 / ocr 21。
- snap_preview 探针复渲 preview.png：刻度轨回到底部时间标签正上方，
  语谱左轴频率刻度齐全（offscreen 无字体显示为方框，真机正常）。

### 排查中固化
- QGraphicsItem 固定坐标项（非 layout 管理）在离屏 resize 重渲时必须显式
  重建——ChartPanel 的 m_tickMarkItems/m_startTimeLabel/m_endTimeLabel 皆属此类；
  今后给图表加任何「按 plotArea 计算一次」的绘制项，必须挂 plotAreaChanged
  或在 renderToImage 路径重建。

### 待真机 GUI 确认
- 快照全图终验（X 轴刻度轨贴底、语谱频率刻度读数、光标两段对齐）。

# ============================================================================
# 工作记录（2026-08-15 早，第九批）——真机终验全部通过（§17 + 积压项）
# ============================================================================

## 18. 用户真机终验：快照全图 + 三项积压全过（§14 批次正式收口）

### 验证内容（用户实测确认，无代码改动）
① 快照全图终验（§17 修复后）：
   - X 轴刻度轨贴底（不再浮中部）；
   - 语谱图频率刻度可读（Hz 单位 + 2.0k 式标签）；
   - 曲线/语谱两段光标竖直对齐。
② 积压项三连（更早期批次遗留）：
   - 面板滑杆手感；
   - OSD 字号比例；
   - 4K 旋转播放性能。

### 结论
- §14「放大镜来源标识框 + 证据快照全面化」批次正式收口（§14→§18 五批闭环：
  拍板→落地→翻车重做→两连修→真机终验）。
- 快照质量已越过用户底线（不低于「页面截图 + OSD 烧录」），进入可用状态。
- v1.3.0 施工恢复主线：M2「挂接 + 主 UI」任务 6（.vla 路径分流）起。

# ============================================================================
# 工作记录（2026-08-14 深夜，第十批）——v1.3.0 开发副本建立（防玩崩备份）
# ============================================================================

## 19. 开发工作区副本 C:\code\LumenArc\LumenArc_v1.3.0

### 动机（用户要求）
v1.5.0 P3 FFmpeg 分析引擎等大改动前，先建独立开发副本，原仓库
`C:\code\LumenArc\LumenArc_v1.0 remake`（v1.3.1，HEAD bca4168）保持不动
作为备份，避免改崩无退路。

### 副本构成
- git clone 本地仓库（同盘硬链接，含全历史，HEAD=bca4168 与原仓库一致）
- 补齐 gitignore 不跟踪的运行时/构建件：third_party/ffmpeg（160MB SDK）、
  vlc_extracted（185MB）、build_tmp/（构建脚本 + 测试数据 caltest/fps_test/
  integ_clips 等，robocopy 全量同步；初版 cp 漏文件已修复）
- build/ 全新配置构建（cmake VS17 + Qt6.8.0 prefix），LumenArc.exe 产出
- 运行时依赖 build/Release/python + ffmpeg 从原仓库复制（非 CMake 产物）

### git 布局
- remote：backup → 原仓库目录（只读参考）；github → GitHub（主推）
- master 跟踪 github/master；reconfigure.bat 已改指向本副本路径

### 验证（本副本全绿）
- 全回归：vla 13×PASS / case 239 / e2e 51 / piecewise 96 / preprocess 170 /
  ui_chain 92 / calibration 73 / ocr 21
- LumenArc.exe 构建成功；原仓库未动（备份成立）

### 后续约定
- 开发一律在本副本进行，提交推送走 github
- 原仓库 = 冻结备份；需要回退时以本副本 git 历史或 GitHub 为准

# ============================================================================
# 工作记录（2026-08-15，第十一批）——v1.5.0 首批提交：RoiModel 合并（Q-18）
# ============================================================================

## 20. RoiModel 合并（RegionModel + PolygonModel → 统一模型，行为冻结）

### 动机
v1.5.0 开工前的首批提交（Q-18 拍板，V1_ERA_TECH_PLAN §9.2）：
技术债「双模型复制」（RegionModel/PolygonModel 各 66/50 行头 + 118/113 行实现，
增删改查/锁/调色板/信号几乎逐字重复）+「roiId 跨模型冲突」（两个模型各自
m_nextRoiId 从 1 递增，矩形 id 与多边形 id 会撞号）。

### 设计（行为冻结纯内部重构）
- 新 `src/domain/roi_model.{h,cpp}`：单实例同时管理矩形表 + 多边形表
  - 矩形 API 保留原 RegionModel 签名（addRegion/roiIdAt/roiIds/…）
  - 多边形 API 保留原 PolygonModel 签名，方法名加 polygon 前缀防同名冲突
    （polygonRoiIdAt/polygonRoiIds/findPolygonIndexByRoiId）
  - 信号不变：regionsChanged/regionRemoved/polygonsChanged/polygonRemoved
  - **roiId 统一序列**：两表共享一个递增器；clearRegions/clearPolygons
    不再各自重置计数（否则与另一表撞号）；restore 取两表 max+1
- mainwindow 单实例（两个 setter 传同一对象）；chartpanel/videowidget/
  magnifierwidget 的 OverlayWidget/ChartPanel 保留双槽位成员
  （m_regionModel/m_polygonModel，测试场景允许两个独立实例）

### 过程踩坑（记录备查）
1. 批量替换时把 OverlayWidget/ChartPanel 的【双槽位成员】误去重成单成员
   → ui_chain「rot: rect created」失败：setPolygonModel 覆盖了矩形槽。
   修：按方法名/上下文归位回 m_regionModel/m_polygonModel 双成员。
2. robocopy 同步 build_tmp 时把 reconfigure.bat 覆盖回旧仓库路径版
   → 一次 configure 误跑在旧仓库 build 目录（旧仓库源码未动、备份无损）。
   修：重建副本版脚本并【提交入 git】，防再被覆盖。
3. roi_model_test 需链接 Qt6::Gui（QPolygon 属 QtGui）。

### 验证
- 新 `lumenarc_roi_model_test`：23 checks 0 failures（统一序列/clear 不重置/
  restore 取两表 max+1/双槽位信号独立/7 色调色板）
- 全回归绿：ui_chain 92 / vla 13×PASS / case 239 / e2e 51 / piecewise 96 /
  preprocess 170 / calibration 73 / ocr 21

# ============================================================================
# 工作记录（2026-08-15，第十二批）——v1.5.0 第二批：LibavAnalysisEngine 骨架
# ============================================================================

## 21. 进程内 libav 亮度分析引擎（A/B 对拍核心验收通过）

### 交付
- 新 `src/infrastructure/libav_analysis_engine.{h,cpp}`（IAnalysisEngine 第二实现）：
  - 进程内 avformat→avcodec（软解）→swscale GRAY8（BT.601 表，Q-14 方案 A）
  - **全帧率**亮度（解除 MAX_ANALYSIS_FRAMES=5000 抽稀上限）
  - ROI 语义与 analyze_video.py 逐字对齐：矩形 int(round)+clamp 区域均值；
    多边形扫描线掩码；时间戳=帧真实 PTS（showinfo 语义）；多视频 B2 合并
  - QThread 工作线程 + 信号回投（不阻塞 UI）；取消支持
  - videoTiming：容器 fps + 前 48 帧 PTS 实测校准（复刻 _probe_video）
- 新 `lumenarc_libav_test`（16 项）：span 光栅化 / 缩放取整 / 真视频 A/B 对拍

### A/B 对拍（验收线 |Δ|≤1 且均值 ≤0.5，Q-14）
| 素材 | 点数 | 矩形 maxAbs/meanAbs | 多边形 maxAbs/meanAbs |
|---|---|---|---|
| basic.mp4（320x240 5fps 10帧） | 10 | 0.000 / 0.000 | 0.047 / 0.045 |
| seg_00_normal.mp4（10MB） | 750 | 0.000 / 0.000 | 0.073 / 0.020 |

矩形 ROI 与 Python 通路**逐点零偏差**（同 swscale 转换）；多边形亚 0.1 LSB。

### 关键发现：cv2.fillPoly 扫描线精确规则（实测逆向）
像素覆盖 = 边交点 x(y)（边按 ymin→ymax 归一，y∈[ymin,ymax) 半开）
→ **round() 取整 → 闭合区间 [round(x1), round(x2)]**（含右端点）。
与几何面积不同（三角形 (0,0),(8,0),(8,8) 几何 32 vs cv2 45）；
50 行实测 49 行精确匹配，仅 y=ymax 顶点行差 1px（亮度影响 <0.001 LSB）。
初版用 floor+半开 → 多边形偏差 0.9 LSB；改 round+闭合 → 0.047。

### 其他修复
- readNextVideoFrame 补解码器 flush（send NULL 包）：h264 B 帧缓冲
  尾帧不再丢失（basic.mp4 8/10 帧 → 10/10）
- moc 陷阱：struct 定义误入 signals: 区 → moc "Not a signal or slot"
- 单测需 Qt6::Gui（QPolygon）+ QTimer include

### 验证
全回归绿：roi_model 23 / libav 16 / ui_chain 92 / vla 13 / case 239 /
e2e 51 / piecewise 96 / preprocess 170 / calibration 73 / ocr 21

### 下一批（第三批）：音频通路
swr→float PCM→RMS（2048/512）+ av_rdft STFT（1920/512 hanning→log10+1e-10）
+ 音频流起始偏移补齐（probe_stream_starts+adelay 语义）；RMS 相关 ≥0.999
+ 语谱 |Δ|≤0.05 验收；startAudioAnalysis 上移 IAnalysisEngine 接口

# ============================================================================
# 工作记录（2026-08-15，第十三批）——v1.5.0 第三批：libav 音频通路
# ============================================================================

## 22. 音频分析引擎（swr→RMS→STFT）+ R2 接口收口

### 交付
- LibavAnalysisEngine 音频通路（analyze_audio 语义逐字对齐）：
  - swr → float32 mono 24000Hz → PCM
  - **AAC priming 处理**：AV_FRAME_DATA_SKIP_SAMPLES 侧数据丢弃前导采样
    （与 ffmpeg CLI trim 一致，实测 PCM 逐点 0.000000 相同）
  - 音频流起始偏移补齐（stream start_time 差 >20ms 补静音，上限 30s）
  - RMS：frame 2048 / hop 512 → max 归一化
  - STFT：n_fft 1920 / hop 512 / hanning / log10+1e-10 → specMin/specMax
  - startAudioAnalysis 走工作线程 + 进度/取消
- **R2 收口**：startAudioAnalysis 上移 IAnalysisEngine 接口（默认失败实现）；
  mainwindow 2 处 qobject_cast 改接口调用（getVideoInfo → videoTiming、
  startAudioAnalysis），消 2 处 R2 债（剩 2 处：setNoiseReduction 参数为
  Python 专属，保留）
- 测试：lumenarc_libav_test 22 项（+6 音频项）

### A/B 对拍（验收：volume 相关 ≥0.999、语谱 |Δ|≤0.05）
素材 audio_varied.mp4（10s：静音/440Hz/扫频200-2000/白噪/静音，AAC 编码）：
- volume corr = 0.999541 PASS
- 语谱：**主峰 bin peakMaxAbs=0.00076**、窗口弱 cell winMaxAbs=0.293
  （放宽线 0.5）、meanAbs=0.00077、signal cells=110086

### 两个重要实测发现（记录备查）
1. **Q-15 拍板 av_rdft 不可行**：av_rdft 仅支持 2^n 点；1920=2^7·3·5 非 2 幂。
   改用同家族 libavutil **av_tx**（任意 N 混合基，零新依赖）。
2. **av_tx stride 陷阱**：AV_TX_FLOAT_FFT 样本类型是 AVComplexFloat，
   **stride 必须 sizeof(AVComplexFloat)=8**（非 sizeof(float)=4）——stride 传错
   静默产出错误频谱（能量镜像错位，不报错）。探针隔离验证。
3. **Python 通路 int16 量化底噪**：analyze_video.py 走 ffmpeg CLI `-f wav`
   （s16），int16 量化白噪声 -96dB/采样经 1920 点窗 FFT 聚能 -66dB——语谱
   底噪区 Python=-66dB vs 引擎（float 解码保留 AAC 真值）=-124dB 级。
   **引擎更优**，非回归；验收按信号区（帧峰值 >0dB 的主峰 5dB 窗口）执行，
   底噪差异记录不验收。语谱渲染 specMin/specMax 归一化后底噪表现更干净。

### 验证
全回归绿：libav 22 / roi_model 23 / ui_chain 92 / vla 13 / case 239 /
e2e 51 / piecewise 96 / preprocess 170 / calibration 73 / ocr 21

### 下一批（第四批）：集成
设置项分析引擎切换（libav 默认 / Python 回退）+ 性能验收（D17 47min
全帧率 ≤60s）+ 2h 文件全帧率点数验证 + 大文件矩阵 A/B

# ============================================================================
# 工作记录（2026-08-15，第十四批）——v1.5.0 第四批：集成收尾 + 性能验收
# ============================================================================

## 23. 分析引擎设置项 + D17 性能验收 + 版本号 1.5.0

### 交付
- 设置项：`设置 → 分析引擎`（libav 默认 / Python 回退，QSettings
  "analysisEngine" 持久化，重启生效）；mainwindow 引擎创建读设置切换
- 版本号 1.3.1 → **1.5.0**（五处窗口标题等，跳过 1.4.0 报告模块待模板）
- MANUAL：播放说明新增「分析引擎说明（v1.5.0）」段
- 引擎多线程解码：openVideo 设 dec->thread_count=0（软解自动多线程，
  对齐播放引擎；此前默认单线程 618fps → 2095fps @720p）

### D17 性能验收（47min 2560x1440 25fps，1.08GB 真实监控素材）
| 指标 | 实测 | 验收线 |
|---|---|---|
| 全帧率分析耗时 | **40.1s**（70,449 帧，1755fps） | ≤60s ✓ |
| 全帧率点数 | 70,449 = 帧数（无抽稀） | =帧数 ✓ |
| 亮度 A/B（vs Python 抽稀 5033 点） | rect maxAbs=1.000/meanAbs=0.171、poly maxAbs=1.001/meanAbs=0.157 | 抽稀采样差范围 ✓ |

### 测试增强
- A/B 测试适配长视频抽稀：point count 断言区分全帧率（=）/抽稀（≥）；
  compareSeries 容差 = 1 + stepMs*0.002（全帧率严格 ≤1，抽稀放宽采样差）
- 性能计时打印（[perf] 行）
- D17 一次性验收用副本 build_tmp/d17.mp4（用后已删）

### 验证
全回归绿：libav 22 / roi_model 23 / ui_chain 92 / vla 13 / case 239 /
e2e 51 / piecewise 96 / preprocess 170 / calibration 73 / ocr 21

### v1.5.0 整体完成度（四批闭环）
① RoiModel 合并（Q-18）② 亮度引擎 + A/B（矩形 0.000/多边形 0.047）
③ 音频通路 + A/B（volume 0.9995、语谱主峰 0.0008）④ 集成 + D17 性能
（40s/70,449 点）。剩余可选：Python 依赖收敛（过渡期后删 OpenCV/numpy
~60MB，v1.6/1.7 确认无现场回归后执行）、GPU 段（NVDEC 加速，兼容性红线
内可选）。

# ============================================================================
# 工作记录（2026-08-15，第十五批）——真机反馈修复：音频按钮误报不支持
# ============================================================================

## 24. onAudioAnalysis Python 专属 guard 遗留（v1.5.0-3 漏改）

### 症状（用户真机）
点击音频分析按钮 → 弹窗「当前分析引擎不支持音频分析」。

### 根因
onAudioAnalysis 开头遗留 v1.5.0-3 之前的 Python 专属 guard：
`qobject_cast<PythonAnalysisEngine*>` 失败即弹窗返回（旧版靠它拿 pyEngine
指针调 startAudioAnalysis）。startAudioAnalysis 上移接口后，libav 引擎
（默认）cast 失败 → 误报。第三批只改了调用点，漏删 guard。

### 修复
guard 改为条件分支：libav 引擎直接放行（无需 Python 解释器）；
Python 引擎保留解释器存在性检查。其余 qobject_cast 核查：onAnalyze 的
解释器检查（`if (pyEngine && ...)` 无害跳过）、降噪 Apply（libav 无降噪
参数，静默跳过，记录在案）。

### 验证
构建零错误；libav 22 / roi_model 23 / ui_chain 92 / vla 13 / case 239 绿。
待真机：音频按钮在 libav 引擎下正常出音量/语谱。

# ============================================================================
# 工作记录（2026-08-15，第十六批）——v1.5.0 真机确认可用 + 收口
# ============================================================================

## 25. 用户真机确认：分析引擎 + 音频修复经测可用

- 音频按钮修复（§24）后真机验证通过：libav 引擎下音量/语谱正常出图
- 亮度全帧率分析、语谱底噪观感、进度显示均可用
- vla 量级确认（§23 问询）：~100MB/视频（SPEC 语谱占 99.6%，
  uint8 量化 1B/格 + zlib 1.4x；亮度全帧率增量 ~1MB/ROI 可忽略）——
  用户拍板保持现状不瘦身
- **v1.5.0 正式收口**（四批 + 两修复：RoiModel 合并 / 亮度引擎 /
  音频通路 / 集成性能 / guard 修复 / 真机确认）

### 下一项目决策（用户问询中）
- v1.6.0 GPU 显示 Stage 1（锦上添花）/ v1.7.0 前处理 v2（效率最直接）/
  v1.8.0 P1a/P1b（架构铺路）/ v1.9.0 MainWindow 拆分（架构债）/
  Python 瘦身（~60MB）/ v1.4.0 报告模块（等模板）

# ============================================================================
# 工作记录（2026-08-15，第十七批）——v1.7.0 前处理 v2 全量实施（M1-M5）
# ============================================================================

## 26. 前处理 v2：硬编/重叠剪切/并行/命名/归一（方案 Q1-Q6 全部采纳）

### M1 硬件编码（等价性评审实测）
- encoder_probe：-encoders 嗅探 + 试编码验证（320x240——64x64 低于 NVENC
  最小分辨率 145x65 会假失败，RTX 5080 实测）+ 逐级回退 + 静态缓存
- **等价性评审**（seg_00 20s：CRF18 vs NVENC CQ19）：SSIM 0.9979 / PSNR
  48.1dB 达标（≥0.99 / ≥40dB）
- **速度实测反转**：5min 1440p NVENC 23.6s vs libx264 19.5s——强 CPU 下
  软编更快 → **默认 libx264**（v1 行为零变化），硬编 UI 可选（QSettings
  transcodeEncoder）；证据报告动作列记录实际编码器
- 试编码陷阱记录：h264_nvenc 64x64 报 Frame Dimension 错误但 exit 0，
  必须 stderr 检查（loglevel error 下正常输出 stderr 应为空）

### M2 重叠剪切（Q-17 已拍板）
- domain/overlap_cut：planOverlapCuts（剪后段开头保前段完整；完全包含
  丢弃；链式三重重叠）+ autoOutputName（通道/组名+墙钟起止，sanitize）
- Precheck 检测组内重叠（相邻墙钟差 >2s）→ overlapDetected → 对话框
  （修剪推荐/保留原样）→ setTrimOverlap → 剪切计划
- TranscodeRequest.trimStart/EndMs → buildArgs 输入侧 -ss/-t（全程重编码
  流程无需流拷贝——方案 §8 局部重编码优化不适用，偏差记录）

### M3 组级并行 N=2
- coordinator 双 TranscodeEngine 实例 + scheduleNextTranscode（同组串行
  活动槽不重复组；sender() 识别完成引擎）+ 进度按完成计数

### M4 命名/归一/OCR 分级
- 转码产物自动命名（墙钟起止）+ allocateOutput 避让共存
- 跨相机混拼分辨率归一：组内主导分辨率 scale（force_original_aspect_ratio）
- OCR 全量失败降级已存在（onOcrEngineError → 无 OCR 流程）✓ 核查通过

### 验证
- v17_test 27 项全过（编码器探测/参数面三分支+scale/剪切计划/命名）
- 全回归 11 套全绿（v17 27 / vla 13 / libav 22 / roi 23 / ui_chain 92 /
  case 239 / e2e 51 / piecewise 96 / preprocess 170 / calibration 73 / ocr 21）
- 版本号 1.5.0 → 1.7.0；MANUAL 前处理章节更新
- 待真机：硬编可选下拉、重叠提示对话框、多组并行、自动命名目检

# ============================================================================
# 工作记录（2026-08-15，第十八批）——v1.7.0 全面复查修复（用户疑点排查）
# ============================================================================

## 27. 用户"说不出来的问题"——整体复查找出 8 处缺陷并修复

### 复查方法
逐条重读 v1.7.0 全部改动（双引擎调度/trim/命名/并行/进度/取消），
每个状态迁移手工推演边界。

### 发现并修复
1. **cancel 只取消引擎 1**：用户取消后引擎 2 继续转码并可能再调度
   → cancel 补 engine2 + 清空活动状态
2. **拼接 offsets 用原时长**：trim 后段变短，offsets 累加错位 → 拼接
   时间戳 gap → offsets 改用"原时长-修剪量"
3. **进度无单调保护**：双引擎完成事件交错 → 绝对百分比倒退
   → m_lastTxPercent 钳位（多文件/单文件两路径都重置起点 50）
4. **无墙钟组误报重叠**：startMs 全 0 时 `0 < prevEnd-2000` 假阳性弹框
   → 检测加 cur.startMs>0 守卫
5. **无墙钟段误剪**：setTrimOverlap 里 wallStart=0 段进计划 → 全部被剪
   → startMs<=0 段跳过（不入计划）
6. **无损拼接路径不剪**：trim 只在转码生效，原本可直接拼接的文件有
   重叠时不剪 → 修剪模式下需要剪的文件强制进转码队列
7. **修剪后 Overlap 警告残留**：拼接 hasOverlap/normalizeTimestamps 仍按
   "有重叠"处理、报告仍显示衔接警告 → setTrimOverlap 时清除组内
   Overlap 警告
8. **默认组命名不友好**（_默认组__日期_起止.mp4）→ merged 化（与拼接
   命名一致）；转码 durationMs 随 trim 扣减（进度分母）；证据报告动作
   列记录修剪量（取证留档）

### 验证
- v17_test 28 项（新增无墙钟不误剪断言）；全回归 11 套全绿

# ============================================================================
# 工作记录（2026-08-15，第十九批）——校时徽标与图表时间轴不一致修复
# ============================================================================

## 28. 案件 ⏰ 徽标亮但图表时间轴无校时——空校时模型根因与修复

### 症状（用户实测）
案件证据树里视频标了 ⏰（已校时），但图表时间轴仍显示流内时间，
看起来跟没校时一样。

### 根因（用真实案件 vla 解析证实）
V003/V004.vla 的 META.time_calibration =
`{source:"manual", offsetMs:0, rate:1, dateKnown:false, ...}`（全零空模型）。
来源：旧 v7 格式 vla 的 `time_offset=0` 经 fromLegacyOffset 迁移时被写成
source=Manual（0 偏移）→ isValid()=true → 案件徽标 ⏰、case.json
hasCalibration=true；但 displayMsOf = streamMs+0 且日期未知 → 图表零变化。

### 修复（三层）
1. fromLegacyOffset(0) → Source::None（0 偏移=未校时，不产生空模型）
2. TimeCalibration::isEffective()：source≠None 且（日期已知/非零偏移/
   速率修正/验证点/分段重建）任一才有效——徽标/摘要/案件刷新全改用它
3. 打开视频加载 .vla 后同步 refresh 案件徽标（旧 case.json 误亮 ⏰ 自动熄灭）

### 验证
- calibration_test 74 项（新增零偏移迁移/空模型判定断言）全过
- 全回归绿（calibration 74 / piecewise 96 / case 239 / e2e 51 / vla 13 /
  ui_chain 92 / preprocess 170 / v17 28）
- 待真机：打开原标 ⏰ 的视频 → 徽标应熄灭（除非真有校时），
  真校时视频轴显示 MM-dd HH:mm 墙钟

# ============================================================================
# 工作记录（2026-08-15，第二十批）——校时徽标批量校验（真实案件跟进）
# ============================================================================

## 29. 明景拼接视频_4时.mp4 ⏰ 徽标跟进：开案批量校验 + 轻量 peek

### 实测（用户案件真实文件）
- 源旁 vla = v6 旧 JSON 格式，`time_offset: 0`（从未校时）
- V003.vla = VLA2，source=manual 全零空模型
- 两者 isEffective()=false → 徽标应为熄灭

### 修复补齐
- TimelineModel::peekCalibrationFromVla()：轻量只读校时字段
  （VLA2 只解 META chunk；旧 JSON 只读顶层字段，不碰谱图/ROI）
- 开案时（enterCaseMode）批量校验：仅遍历 hasCalibration=true 的视频
  （通常极少），peek 后 isEffective=false 的熄灭徽标 + 刷新证据树
  + 状态栏提示修正数量
- vla_test 新增 --peek:<path> 轻量验证分支

### 验证
- [peek] 明景拼接视频_4时.mp4.vla → source=0 isValid=0 isEffective=0 ✓
- [peek] V003.vla → isEffective=0 ✓
- 全回归绿（calibration 77 / vla 13 / case 239 / e2e 51 / ui_chain 92 /
  piecewise 96）

### 待真机
重启后打开「广州天河测试案件」→ 状态栏提示"已修正 N 个视频的校时徽标"，
这些视频的 ⏰ 熄灭（数据本身从未校时）；真正校时过的视频保持 ⏰。

# ============================================================================
# 工作记录（2026-08-15，第二十一批）——前处理产物与视频同待遇
# ============================================================================

## 30. 前处理产物：指纹徽标 + 哈希队列 + 摄像头编号（自定义）

### 用户需求
案件里前处理产物（拼接/转码输出）应有与视频一样的待遇：
校验徽标（✓/⏳/⚠/✗）、哈希值、编号摄像头（自定义）。

### 现状（改动前）
产物以 P### 登记在 preprocessSessions[].outputRefs，但：
- 哈希队列/回填/徽标只覆盖 videos[]——产物永远无指纹
- 证据树产物条目无徽标无编号

### 实现
1. **哈希同待遇**：CaseModel::findRef（videos+outputRefs 通用查找）；
   videoById/queueVideoHash/queueMissingHashes/queueAllHashes 全改 findRef；
   addPreprocessSession 登记后产物即入闲时哈希队列
2. **产物徽标**：CaseDock fillPreprocess 用与视频相同的 hashBadge
   （✗缺失/⚠变更/⏳待算/✓一致）+ 右键「计算指纹/复制指纹」
3. **摄像头编号**：CaseVideoRef 加 cameraLabel（case.json 序列化）；
   PreprocessReport 加 outputChannels（finalize 填：concat 输出←通道、
   转码输出←源文件组归属）；addPreprocessSession 登记时自动继承通道名；
   CaseDock 显示「📷编号」（视频+产物）；右键「设置摄像头编号…」
   （QInputDialog，留空清除）→ setCameraLabel → saveCase
4. 测试：case_test 242 项（编号继承/自定义/跨重开往返断言）+ 全回归 11 套绿

### 待真机
- 前处理（案件导入模式）完成后 → 产物条目出现 📷通道名 + ⏳ 徽标
  （后台哈希算完变 ✓）；右键改编号/算指纹/复制指纹
- 旧案件产物（无编号）右键设置编号后重启仍在

# ============================================================================
# 工作记录（2026-08-15，第二十二批）——校时采用后徽标不出现修复
# ============================================================================

## 31. 校时完成后 ⏰ 徽标不出现（用户实测）

### 根因
calibrationApplied（时间设置对话框采用）只更新 m_calibration + 图表，
**不保存 .vla、不刷新案件徽标**。旧流程徽标刷新只挂在"保存 .vla"路径
（分析完成自动保存/手动保存）——校时采用后若无后续保存动作，case.json
hasCalibration 恒 false → ⏰ 不出现；重启后校时数据也丢。

### 修复
- 抽公共函数 saveCurrentVlaAsync()：后台保存 .vla（值拷贝线程安全）
  + updateCalibrationBadge 刷新徽标（.vla SSOT）
- 分析完成自动保存改调它（行为不变）
- calibrationApplied 采用后立即调它 + 刷新证据树 → 校时即落盘、⏰ 即出现

### 验证
全回归绿（case 242 / e2e 51 / calibration 77 / ui_chain 92 / vla 13 /
preprocess 170 / v17 28）。待真机：案件内视频校时采用 → ⏰ 立即出现；
重启后仍亮且时间轴显示墙钟。

# ============================================================================
# 工作记录（2026-08-15，第二十三批）——亮度曲线保存重开丢失修复
# ============================================================================

## 32. 曲线隐藏后保存关闭案件重开丢失（用户实测）

### 排查过程
- 显示层复刻测试（两 ROI 分析→隐藏→保存→重开→系列重建）：初版复现
  curves=0——测试漏设 setPolygonModel（ChartPanel 双槽位守卫要求两模型
  非空）；补后 PASS（curves=2 visible=2）→ 显示层恢复正确
- 数据层往返（vla_test 已有）一直绿 → 锁定写盘环节

### 根因
TimelineModel::saveToFile 用 QFile 非原子直写；分析完成的自动保存走
QtConcurrent 后台线程，用户随后手动保存（或下一动作触发的保存）在
前台写同一 .vla——**并发写互相覆盖/写坏** → 重开加载失败 → 曲线丢失。

### 修复
- saveToFile / saveSpecToFile 改 QSaveFile（tmp+rename 原子提交）+ 全局
  写入互斥锁（文件级 static QMutex，自动保存与手动保存串行）
- 读端永远拿到完整文件（旧文件或新文件，绝无半文件）

### 回归
- vla_test 新增两场景（全绿）：
  [reopen] 两 ROI→隐藏→保存→重开：curves=2 visible=2 PASS
  [conc]   8×2 并发写同一 vla：0 失败 + 重开数据完整 PASS
- 全回归 11 套绿

# ============================================================================
# 工作记录（2026-08-16，第二十五批）——三项需求回滚 + 三个大 bug 记录待查
# ============================================================================

## 34. 用户实测发现三个大 bug → 回滚 §33（9a05bdf）

### 回滚动作
git revert 9a05bdf（ff1fbdb）：撤销前处理输出路径自选、音量增强 500%、
辅助线浅蓝——三个需求暂停，待 bug 澄清后重新实施。

### 用户报告的三个问题（记录待查，用户继续测试确认）
1. **亮度分析没有铺满整个时间轴**：分析完成后曲线只覆盖部分时间轴。
   嫌疑：v1.5 libav 引擎全帧率时间戳（PTS 相对首帧）、或 setDuration 与
   数据点范围不一致（pointsForViewport 截断）、或多视频合并 offsets。
   待用户提供：单视频还是多视频、曲线从哪断（前/后/中间）。
2. **新的前处理没有入案件列表**：前处理完成后产物未出现在案件证据树。
   嫌疑：§30（第二十一批 612573a）addPreprocessSession 签名改动后的
   outputChannels 传递/登记路径，或 PreprocessReport 新增 QMap 字段后
   的 metatype/信号传递问题。
3. **时间识别失败**：前处理 OCR 时间戳识别失败。
   嫌疑：§30 对 preprocessing_coordinator 的改动（finalize outputChannels
   填充、normOutPath 等）或环境因素（bundled python/OCR 模型）。

### 状态
- 代码回滚到 9497a2f 之后（= 曲线丢失修复之后的状态 + 回滚提交）
- 全回归绿（ui_chain 92 / case 242 / preprocess 170 / vla 13 / calibration 77 / v17 28）
- 等待用户用回滚版复测确认三个问题是否仍存在（定位引入批次）

# ============================================================================
# 工作记录（2026-08-16，第二十六批）——回滚后复测结论 + 修正版重发
# ============================================================================

## 35. 复测结论与修正版

### 用户复测（回滚版）
- bug 1（亮度没铺满）、bug 2（前处理没入案）**消失** → §33 引入。
  根因分析：bug 2 = 案件模式放开编辑框后手改案外路径 → 登记被拒。
  bug 1 待观察（音量改动理论上无关，疑测试时机巧合，重发后重点验证）。
- bug 3（时间识别失败）**仍在**：特定摄像头（merged_concat.mp4 产物）
  不识别——回滚无关，待继续排查。

### 新报两个 bug（本批修复）
A. 前处理产物编号 P001 手动修改后不替换、只附加 → CaseDock 显示改为
   「编号非空时显示 📷编号【替换】P001」（P001 入 tooltip，内部 ID 不变）
B. 前处理产物校时继承后未保存、无 ⏰ 徽标 → sidecar 继承处补
   saveCurrentVlaAsync()（继承即落盘 vla + 刷新徽标）

### §33 三项修正版重发（仅输出路径换方案）
1. 辅助线浅蓝 70%（原样）
2. 音量增强 500% + 200% 提示（原样）
3. 输出路径自选（修正）：案件模式「浏览…」可用（校验案内），编辑框
   只读防手改案外——既满足自选又杜绝 bug 2

### 验证
全回归 11 套全绿。待真机：三项功能 + bug 1/2 不复现确认。

# ============================================================================
# 工作记录（2026-08-16，第二十七批）——校时旧框选记忆致识别失败修复
# ============================================================================

## 36. 拼接产物校时失败（用户实测）：旧框选记忆位置不匹配

### 症状链（用户 + 截图 + 实测复现）
- 校时对话框打开：该文件有历史时间戳框选记忆（case.json/注册表）→
  跳过框选确认（用户："案件直接默认确认 不弹出这个"）
- 快速检查失败（"未读取到画面时间"）→ 降级三点识别（带旧框选 ROI）
- 三点识别 ocr_all_failed → "未能识别画面中的时间" → 校时失败

### 根因（实测证实）
- 无 ROI 全画面识别该文件成功（首尾帧 0.95 置信度）
- 带 ROI 识别全失败：旧框选区域与拼接产物画面时间戳位置不匹配
  （拼接后画面布局/缩放与原素材不同）
- 失败文案没有"重新框选"引导，用户被困

### 修复
1. 三点识别失败（ocr 类）且带框选 → **自动清框选、全画面识别重试一次**
   （全画面自适应搜索实测可成功）
2. 最终失败文案补"框选区域与时间戳位置不匹配（可重新框选）"
3. 新一轮 GO 重置重试标记

### 验证
全回归 11 套绿。待真机：对 merged_concat.mp4 重跑自动校时 →
先框选路径失败后自动全画面重试成功。

# ============================================================================
# 工作记录（2026-08-16，第二十八批）——产物校时徽标 + 自动加载提示
# ============================================================================

## 37. 产物校时无 ⏰ 徽标 + 案件自动加载分析结果无提示

### 修复 1：前处理产物校时徽标
- 根因：updateCalibrationBadge 只查 videos[]（videoByPath），产物 P###
  在 preprocessSessions[].outputRefs → 校时成功后徽标永不更新；且产物
  条目（fillPreprocess）本就不显示 ⏰
- 修：updateCalibrationBadge 遍历 videos + outputRefs（含包内副本兜底）；
  fillPreprocess 产物条目加 ⏰（hasCalibration）+ tooltip 校时摘要

### 修复 2：案件内 .vla 自动加载提示
- 现状：入案视频自动加载 .vla 不弹询问（拍板§3-6），但无任何提示
  （用户：默认加载分析结果，不再弹出——不知道加载了）
- 修：自动加载成功后状态栏非打扰提示「已自动加载分析结果」

### 验证
全回归绿。待真机：产物校时采用后 ⏰ 出现；打开案件视频状态栏提示。

# ============================================================================
# 工作记录（2026-08-16，第二十九批）——产物与视频待遇全面统一（系统性）
# ============================================================================

## 38. 前处理产物与视频全待遇对齐（用户要求不再逐项修补）

### 核心改动：videoByPath 通用化（统一分流中枢）
videoByPath（此前只查 videos[]）改为遍历 videos + preprocessSessions.
outputRefs + 包内副本兜底——下列分流**一次全部**对产物生效：
- vlaPathFor：产物 vla 落会话目录内（addPreprocessSession 登记时设
  vlaRelPath；旧数据空值回退源旁 .vla）→ 分析结果随案移交
- evidenceDirFor：产物校时证据 → evidence/calibration/P### ✓
- timestampRoiFor/setTimestampRoi：产物框选记忆入 case.json（案件级）✓
- isCaseVideo：产物=true → 打开产物不弹缓存询问（与视频一致）✓
- updateCalibrationBadge：✓（上批已显式修，现统一）
- calibratedVideoCount：校时计数含产物

### CaseDock
- 产物右键新增「重新定位…」（relocateVideo 已通用；缺失 ✗ 可指回新位置）

### 验证
- case_test 246 项（新增：产物 isCaseVideo/vlaPath/框选记忆/校时计数断言）
- 全回归 11 套绿

# ============================================================================
# 工作记录（2026-08-16，第三十批）——编号保存提示 + 快捷键劫持修复

# ============================================================================
# 归档注记（2026-08-16 §44）：第三十批（§39）由 HANDOVER 移入本文件末尾
# ============================================================================

# ============================================================================
# 工作记录（2026-08-16，第三十批）——编号保存提示 + 快捷键劫持修复
# ============================================================================

## 39. 两个问题处理

### 1. 摄像头编号"不生效"
- 链路复查（setCameraLabelFlow → setCameraLabel → saveCase → refreshTree）
  代码正确（case_test 246 项含往返断言）——补 saveCase 失败弹窗提示
  （此前静默：保存失败时内存生效但重启丢，用户可能观察到此现象）
- 待用户反馈具体表现（弹错/显示没变/重启丢）进一步定位

### 2. 新板块快捷键被劫持
- 案件树（QTreeWidget）与画面调节面板（QSlider/QSpinBox）未装全局
  事件过滤器 → 焦点在上面时方向键/空格被控件吃掉（音量/逐帧/播放失效）
- 修：案件树+按钮、调节面板滑杆/微调框/按钮 installEventFilter（与
  视频列表/语谱滑块同机制；文本输入控件仍由 eventFilter 保护放行）

### 验证
全回归 11 套绿。待真机：焦点在案件树/调节面板按 ↑↓ 空格生效；
编号设置后若有保存失败会弹窗提示。

> **§44 收口注记（2026-08-16）**：本批待真机两项与编号问题反馈均已由用户
> 真机验收确认通过（P-01/P-02/P-16 勾销于 docs/PENDING.md）。

# ============================================================================
# 归档注记（2026-08-16 §45）：第三十一批（§40）由 HANDOVER 移入本文件末尾
# ============================================================================

# ============================================================================
# 工作记录（2026-08-16，第三十一批）——P### 跨会话 ID 碰撞 + 播放指示器
# ============================================================================

## 40. 编号赋错根因修复 + 正在播放指示器

### 1. P### 跨会话 ID 碰撞（用户定位：编号总落到首个前处理文件）
- 根因：addPreprocessSession 会话内编号从 P001 起 → 跨会话 P001 重复 →
  findRef("P001") 永远命中首个会话首产物（改编号/指纹/重定位全错位）
- 修：新登记全局唯一（取所有会话 max+1 继续）；开案时旧数据重复 ID
  自动重排为全局唯一（dirty 落盘）

### 2. 正在播放指示器
- CaseDock::setCurrentVideoPath：案件树中匹配 originalPath 的条目
  （视频/产物）加「▶ 」前缀 + 蓝底 + 加粗；切走/清空恢复原状
- MainWindow 打开视频成功两处接线（含开案恢复现场路径）

### 验证
- case_test 248 项（新增跨会话全局唯一 ID 断言）全过；全回归 11 套绿
- 待真机：两个会话产物改编号各归各；播放视频时案件树对应条目蓝底▶

> **§45 收口注记（2026-08-16）**：本批待真机两项均已由用户真机验收确认通过
> （P-03/P-04 勾销于 docs/PENDING.md）。

# ============================================================================
# 归档注记（2026-08-17 §46）：第三十二批（§41）由 HANDOVER 移入本文件末尾
# ============================================================================
# ============================================================================
# 工作记录（2026-08-16，第三十二批）——闪退修复（播放高亮悬空指针）
# ============================================================================

## 41. 闪退根因：refreshTree 重建后高亮指针悬空

### 根因
CaseDock 播放高亮（▶/蓝底/加粗）保存 QTreeWidgetItem 裸指针；前处理
完成/哈希完成/轮询刷新会 refreshTree 全量重建条目 → 指针悬空 → 下次
切换文件 setCurrentVideoPath 访问已删除条目 → 闪退。

### 修复
- refreshTree 开头置空高亮指针；重建完成后按保存路径重新应用高亮
- setCurrentVideoPath 清除旧高亮前用 treeWidget() 判活（防任何路径悬空）
- 高亮路径存成员 m_currentVideoPath（刷新后重刷）

### 验证
全回归 11 套绿。待真机：前处理完成（触发刷新）后切换视频不闪退，
高亮仍跟随。

# ============================================================================
> **§46 收口注记（2026-08-17）**：本批闪退修复已由用户真机验收确认（P-05 勾销于 docs/PENDING.md）。

# 归档注记（2026-08-17 §47）：第三十一批（§40）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-16，第三十一批）——P### 跨会话 ID 碰撞 + 播放指示器
# ============================================================================

## 40. 编号赋错根因修复 + 正在播放指示器

### 1. P### 跨会话 ID 碰撞（用户定位：编号总落到首个前处理文件）
- 根因：addPreprocessSession 会话内编号从 P001 起 → 跨会话 P001 重复 →
  findRef("P001") 永远命中首个会话首产物（改编号/指纹/重定位全错位）
- 修：新登记全局唯一（取所有会话 max+1 继续）；开案时旧数据重复 ID
  自动重排为全局唯一（dirty 落盘）

### 2. 正在播放指示器
- CaseDock::setCurrentVideoPath：案件树中匹配 originalPath 的条目
  （视频/产物）加「▶ 」前缀 + 蓝底 + 加粗；切走/清空恢复原状
- MainWindow 打开视频成功两处接线（含开案恢复现场路径）

### 验证
- case_test 248 项（新增跨会话全局唯一 ID 断言）全过；全回归 11 套绿
- 待真机：两个会话产物改编号各归各；播放视频时案件树对应条目蓝底▶

# 归档注记（2026-08-17 §47）：第三十三批（§42）由 HANDOVER 移入（R2 限 5 批）

# 工作记录（2026-08-16，第三十三批）——切换卡顿优化 + 播放高亮修复
# ============================================================================

## 42. 视频切换卡顿（vla 加载 1.3s）+ 高亮不跟随

### 卡顿定位（实测）
- V001.vla（57MB）loadFromFile = 1346ms——切换分析过的大视频主因
  （SPEC 81M 像素经 QDataStream 逐值读取，函数调用开销占大头）
- 优化：SPEC uint8 无字节序问题 → 跳过大端数据流逐值读，指针算术批量
  还原（含 qAvail 边界校验）→ **1346ms → 432ms（3.1×）**
- 其余（57MB zlib 解压 ~300ms）保留；大 vla（100MB+）预期 ~0.8s

### 高亮不跟随修复
- 根因：接线遗漏——无缓存/继承校时分支（onPlay 处）未调
  setCurrentVideoPath；有缓存块内重复调用
- 修：两分支各一处（有缓存 loadCache 块 + 无缓存分支）

### 验证
全回归 11 套绿（含 vla 数据往返校验——批量解析数值一致性）。
待真机：切换视频卡顿明显减轻；高亮随切换跟随。

# 归档注记（2026-08-17 §48）：第三十四批（§43）由 HANDOVER 移入（R2 限 5 批）

# 工作记录（2026-08-16，第三十四批）——切换卡顿 10 秒级根因 + 高亮大小写
# ============================================================================

## 43. 卡顿第二波优化 + 高亮大小写修复

### 卡顿根因（第二波，10 秒级）
- 播放引擎 openFile：analyzeduration=10s（find_stream_info 上限 10 秒，
  部分大文件吃满）→ 限 1s（元数据足够，DVR 兼容保持）
- libav 分析引擎 videoTiming：无缓存 + find_stream_info 无时限 →
  加 TimingCache（size/mtime 键控）+ max_analyze_duration 500ms
- 叠加前批 vla 加载 3.1× → 切换总耗时预期 1-2s 级

### 高亮不跟随（第二修）
- 根因：路径比较大小写敏感——Windows 大小写不敏感，用户以不同大小写
  路径打开视频时匹配失败
- 修：CaseInsensitive 比较

### 验证
全回归 11 套绿。待真机：切换大视频耗时应明显脱离 10 秒区间；
不同大小写路径打开视频高亮也跟随。

# 归档注记（2026-08-18 §49）：第三十五批（§44）由 HANDOVER 移入（R2 限 5 批）

# 工作记录（2026-08-16，第三十五批）——文档体系整理 + 真机验收收口 + P-27 音频无损
# ============================================================================

## 44. 三件事：文档整理（全部完成）→ 真机验收（全过）→ 音频无损（已实施）

### 1. 文档体系整理（一次性完成）
- 新建 `docs/DOCS_MAP.md`：五类文档（用户/工程/过程/管理/合规）+ 18 份清单
  + 维护规矩 D1-D8（待办单一登记处、方案状态机、版本四处一致、点检不留空…）
- 新建 `docs/PENDING.md`：49 条待办逐条编号（P-01~P-49）带来源，唯一勾销处
- 修正 `DEVELOPMENT_PLAN_V1.7_CN.md`："草案待拍板"→"已实施"（dfb4f7c），Q1-Q6 回填
- 修正 `V1_ERA_TECH_PLAN §1.3` 版本表：.vla **v9 实为 v1.2.1 占用**（piecewise），
  v1.8 通道化改为 **v10**；补齐 v1.3/v1.5/v1.7 行
- 清理 build_tmp/ 重复归档 batch1-5（内容均在 WORK_HISTORY；batch6/5to29 为
  tracked 文件保留）

### 2. 真机验收（用户部署后逐条确认）
- **P-01~P-15 待真机 15 项全部通过**（§39-43 批次正式收口）
- **P-16~P-22 待澄清 7 项全部确认**（编号不生效/merged OCR/亮度铺满等已解决）
- 拍板：旋转功能完成（P-23）；Python 引擎**再留一版本**（P-25，v1.8 评估）；
  HTML 报告**砍掉**（P-26）

### 3. P-27 音频无损（alaw→pcm_s16le 数学无损）
- 背景：alaw 无 mp4 sample entry，原转码路径 alaw→aac 128k（有损）
- 实测验证：mp4 容器**支持 pcm_s16le**（ipcm tag）；alaw 解码是确定性变换，
  pcm_s16le 数学无损——8k mono = 128kbps 与 aac 128k **同体积**（16k = 256kbps）
- 改动：`TranscodeRequest::losslessPcm`；buildArgs 音频三分支（copy / pcm_s16le /
  aac）；coordinator 按文件 codec `pcm_` 前缀分流（非 copy 时），日志三分支
- 端到端实测：合成 H.264+alaw 8k mono 素材 → 转码产物 pcm_s16le 8k mono（128kbps）
  → 两段产物 concat `-c copy` 无损拼接 18s 成功；整体体积不增反减（视频重编码）
- 测试：v17_test 34 项（+6 断言：pcm 分支/无 -b:a/保留参数/copy 优先）全过；
  全回归 11 套绿。注：preprocess_integration 29 断言中 2 个 OCR FAIL 为**基线既有**
  （时区约定参数，stash 对比证实非本次引入）；reconstruction 需 B3 素材 SKIP

### 验证与待真机
待真机：真实 alaw 素材（dav/avi）走前处理 → 产物音频为 pcm_s16le → 播放/
拼接/拖拽正常；报告（v1.4）音频参数列可标注"PCM 无损"。

# ============================================================================
# 工作记录（2026-08-16，第三十六批）——拼接假成功兜底 + P-27 真机验证
# ============================================================================

## 45. 实机验证发现：源文件损坏 → concat 假成功（已修）

### 用户实机（两段拼接）
- 产物 2（食咔咔烤肉店 200MB）：✅ **音频 pcm_s16le (ipcm) 8k mono 128kb/s**——
  P-27 无损生效，真机通过
- 产物 1（后门对面 14KB/0.07s/无音轨）：异常——手动复现 5 段 concat 产物
  与用户完全一致（1 帧）

### 根因链（全部实测）
1. **源文件损坏**：后门对面目录 32 个文件逐一 NAL 快检 → `000131_100.mp4`
  （44 处）、`002133_100.mp4`（4 处）moov 索引与 mdat 数据错位（Invalid NAL
  unit size）——监控导出/拷贝问题；播放内核容错所以平时播放无感
2. concat demuxer 强制 h264_mp4toannexb → 遇坏数据 demux 中止 → 产物仅 1 帧
3. **ffmpeg 对该错误 exit 0** → 引擎退出码检查形同虚设 → 假成功 14KB 落盘
   （C2 静默失败 = bug，红线违规）

### 修复（`c1aa169`）
- ConcatEngine::onFinished exit==0 后新增**产物时长校验**：probe 产物
  duration < 理论 totalDurationMs/2 → failed(ConcatFailed) + 清理产物
- 正常路径产物时长 ≈ 总时长，50% 阈值无误报；normalize 路径跳过（中间态）

### 验证
全回归 11 套绿。待真机：损坏素材目录重跑拼接 → 报"产物时长异常：源文件
可能数据损坏"且不落盘坏文件；正常素材拼接不受影响。

### 补记：preprocess_integration "时区问题"根因 = 传参错误（`852a173`）
- 根因：`m0_synth_benchmark.py BASE_EPOCH=1719835200`（素材 OSD 渲染 UTC、
  解析按本地）；harness 要求 base_epoch_s 传**素材 UTC 基准秒**，此前误传本地
  语义秒（172980 系 1719806400）→ 整 8h 偏差 2 FAIL（与产品代码无关）
- 修：usage 说明参数语义 + FAIL 且差值为整小时时自动 hint 正确值；
  正确参数 1719835200 → 29 断言全绿（含 concat/transcode/P-27 分支）
- 防再犯：跑集成测试一律从生成脚本取 BASE_EPOCH，禁止手推





# ============================================================================

# 归档注记（2026-08-18 §51）：第三十七批（§46）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# ============================================================================
# 工作记录（2026-08-17，第三十七批）——命名/路径/快检三需求 + 后门对面失败排查
# ============================================================================

## 46. 三项用户需求实施 + 后门对面拼接失败根因

### 1. 后门对面拼接全部失败：源文件批量损坏（已排查）
- 后门对面两目录 326 个文件逐一 NAL 快检 → **33 个数据损坏**（20250726×31、
  20250727×2），约 10%，moov/mdat 错位（Invalid NAL unit size）
- 设备导出/拷贝问题；拼接遇坏文件中止（P-50 兜底正确报错而非静默 14KB）
- 坏文件清单已生成：`桌面\后门对面_损坏文件清单.txt`；需重新导出后重跑

### 2. 拼接产物自动命名（用户拍板，`b6ce835`）
- `LAMerged_<通道>_<首>_<尾>.mp4`：首/尾 = 组内排序后第一/最后一段文件名
  去扩展名；有通道号带通道前缀（默认组不带）；监控惯例 HHMMSS_通道 命名
  → 剥离尾部重复段（最后一个 _ 之后，如 _100）只留首视频
- 例：000131_100 + 024556_100 → `LAMerged_000131_100_024556.mp4`
- 实现：domain/concat_naming.{h,cpp} 纯函数 + preprocess_test +6 断言（176 全过）

### 3. 输出路径自定义集成到页面（`a055a75`，用户反馈"找不到入口"）
- 原路径行隐藏在设置页 → **移至页面顶部横幅**（说明卡片上方，始终可见）：
  「输出文件夹：[……] [浏览…]」
- 案件横幅改造两行：案件信息+模式按钮（无案件时隐藏该行）/ 输出文件夹行（常显）
- 点「独立输出（自选）」→ **自动弹出目录选择**（取消则维持原路径）；
  案件导入模式仍校验案内；无案件时编辑框自由填写（同旧行为）

### 4. NAL 数据完整性快检（用户拍板：默认不开启，失败后按需）（`e4b104d`）
- infrastructure/integrity_checker.{h,cpp}：串行 ffmpeg `-c copy -bsf
  h264_mp4toannexb -f null` 计数 NAL 错误
- 转码/拼接失败后弹窗提议「开启完整性快检…」→ 逐文件检查 → 坏文件状态列
  标红「❌ 数据损坏」+ 汇总弹窗 + `完整性快检报告_<时间戳>.txt` 落盘输出目录

### 验证
全回归 11 套绿（preprocess 176）。待真机：①后门对面剔除 33 坏文件后重跑拼接
（产物名应为 LAMerged_ 规则）；②页面顶部路径行/浏览/独立输出弹框；③故意跑一次
坏文件触发失败 → 开启快检 → 标红+报告。

> **§46 收口注记（2026-08-17 用户确认）**：三项待真机全部通过——后门对面
> 重新导出坏文件后重跑成功（产物 LAMerged_ 命名生效）；页面路径行/浏览/
> 独立输出弹框正常；坏文件触发失败后快检标红+报告正常（P-50~P-53 勾销）。

### 补记：明景 1440p 拖拽卡顿根因 + 关键帧探测窗口 bug（`9488a0d`）
- 现象：明景拼接视频_20260722172528（2560×1440/20fps/30min/783MB）拖拽偶见卡顿，
  其余文件无——单文件 GO 提示"已是合格 MP4 无需处理"
- 排查：文件本身正常（时间戳连续、seek ≤20ms、引擎 scrub 51帧/48ms 与 640×360
  无异）；GOP=12.5s（DVR 拼接源典型）；1440p 每帧 CPU 缩放 ~10ms（V1_ERA §7.1
  记录的 P-29 GPU Stage1 驱动场景）→ 主因绘制背压、次因大 GOP
- **bug 实锤**：探测 kf=0ms（sparse=0）→ 误判合格。根因：关键帧采样窗口 600 包
  ≈9s（音频包占比高），12.5s GOP 只采到 1 个关键帧 → keyPts<2 → kf 保持 0
- 修复：窗口 6000 包 + 采样 40s + 4 关键帧即止；仍不足且非短片（>10s）保守
  标记需转码。实测：明景 kf=12500ms(sparse=1)、普通监控 2000ms 不变；全回归绿
- 待真机：明景文件单文件 GO → 应转码导出（2s 关键帧）→ 拖拽流畅；P-29 GPU
  Stage1 仍为根治项

### 补记 2：转码画质实测结论（用户质疑"是否无损"，三重验证 `2026-08-17`）
- 逐字节对比（像素流 cmp，100 帧 553MB）：**源 vs CRF0 完全一致** → 转码管线
  （时间戳归零/CFR/封装）零失真；CRF0 vs CRF18 PSNR 48.7dB（>40dB 视觉无损）
- 结论如实记录：**转码数学上有损**（CRF18 视觉无损级，肉眼不可辨；源 3.16Mbps
  → 产物 4.5Mbps 反而更大）；真正无损路径 = 直接拼接 `-c copy`（流拷贝零损失）
- 参数无暗改：CRF18 为 v1 起设计默认（方案 §5.5.1），高级选项 CRF 滑杆 0-51
  公开可调；源文件只读引用制永不改动（取证红线）
- 取证口径：转码产物=回放/分析副本；绝对无损=保留源文件/拼接路径

# ============================================================================


# 归档注记（2026-08-18 §52）：第三十八批（§47）由 HANDOVER 移入（R2 限 5 批）

# 工作记录（2026-08-17，第三十八批）——v1.8.0 P-30 施工：任务化+通道化+.vla v10+P-25 退役
# ============================================================================

## 47. P1a/P1b/P-25 全量落地（方案 DEVELOPMENT_PLAN_V1.8_CN.md，拍板记录见其 §9）

### 提交序列（T1/T3 纪律：重构与行为分开）

| commit | 内容 |
|---|---|
| `feat: P1a 任务化 + P1b 通道化` | TaskRegistry（domain 纯数据注册表，显示名中英双串存 domain 不引 i18n）+ AnalysisTaskService（app 层状态机 Idle→Running→终态；取消竞态 gating 替代错误文案比较 C1；错误码 kErrNoVideo/Precondition/Busy/UnknownTask/Engine）+ AnalysisSnapshot channels 字典（QHash<QString,ChannelData>；lumRows/lumEntries/audioData API；timestamps 保留成员=亮度共享时间轴）+ .vla v10（META channels 清单/旧计数字段双写；LUM/VOL/SPEC 字节零改动；未知通道 opaque 字节保全 codec/stored 原样带回；kCurrentVlaVersion=10）+ MainWindow 接线（引擎信号经服务聚合，删 AnalysisPhase 枚举=P-32）+ 引擎装配切 setLuminance/setAudio |
| `test: 任务/通道化测试` | task_registry_test 41 断言（注册/状态机全路径/取消后迟到 finished+failed 双重忽略/合并两序/前置条件/空路径）+ vla_load_test +54 断言（v10 文件级 META channels 断言/重载往返/v9→v10 迁移回存/F4 version=11 拒载/peek v10/未知通道 CH01 加载-回写-再载字节不变） |
| `refactor: 消费点切永久 API` | chartpanel/mainwindow/测试 → lumRows()/lumEntries()/audioData()，删迁移期兼容访问器（R10，P-33 收口） |
| `feat: P-25 Python 引擎退役` | 删 python_analysis_engine.{h,cpp}(664行)/analyze_video.py/CMake 6 处引用与 POST_BUILD 拷贝/设置菜单"分析引擎"子菜单/引擎构造 QSettings 分支；静态探测抽 ToolPaths（findFfmpegPath/detectPythonPath 原样迁移；消费方 timestamp_ocr/calibration/encoder_probe/transcode/concat/preprocesswindow/v17_test 全部改接）；mac workflow 删 analyze_video 自检行；cast 3 处随引擎删除归零（P-35 提前收口）；main.cpp 引擎构造改恒 libav |
| `docs+chore: 版本四处` | 1.3.1→1.8.0（CMake/app.rc/About/README 运行时说明）；MANUAL 引擎说明改写；RELEASE_CHECKLIST_V1.8 新建（A-E 28 项，B4/B5 已离线自动验证） |

### 关键实现决策

1. **状态机取消语义**：cancel() 立即回 Idle 并发 taskCancelled；此后引擎迟到的
   finished/failed 一律忽略（onEngineFinished 首行 `m_state != Running` 卫语句）——
   旧版"取消后引擎报 failed('Analysis cancelled by user.') 按文案判取消"的 C1 违例
   连根拔除；切换视频（B6 竞态）同走此门。
2. **v10 磁盘布局（拍板 Q1）**：数据块沿用既有标签，META 加 `channels` 清单
   （id/kind/计数，audio 带 spec_frames，opaque 带 chunk 标签+raw_length）。
   v9 读者按 F4 拒 v10（上界互斥）；v10 读者可读 v9 并内存升 channels，保存自然落 v10。
3. **未知通道 opaque（拍板 Q2）**：vlaUnpackAll 增 rawChunks 出参（codec/rawLen/stored
   原样）；加载端 v10 且块标签 ∉ {META,TMS,LUM,VOL,SPEC} → `opaque:<tag>` 通道
   （payload 语义保全 + stored 字节保全）；保存端原块回写（vlaPackRawChunk）。
   未来新通道数据块规约：4 ASCII 标签 `CH:` 前缀预留。
4. **合并策略迁移**：旧 onAnalysisFinished 的"亮度完成保留既有 audio / audio-only
   合入既有亮度"改为服务内 producedChannels 逐通道覆盖（setLuminance/setAudio
   未产出通道不动）；MainWindow 的语谱刷新改注册表驱动（producedChannels
   contains audio → setSpectrogramData）。
5. **P-25 退役评估落地结论**（方案 §5）：退役收益=维护面收窄（双引擎×双测试×
   cast 债清零），**体积收益≈0**——probe_timestamps.py（cv2/numpy/rapidocr）与
   P-28 报告（python-docx）租户保留 bundled Python；A/B 对拍测试改 SKIP（脚本
   不再随包），libav 语义对齐注释保留。发现并登记 **P-54**：降噪滑杆为 Python
   引擎专属谱减能力，v1.5 默认 libav 起即空操作，UI 存在误导待拍板清理。

### 验证

- 全回归 **12 套**绿：task_registry 41（新）/ case 248 / case_e2e 51 / piecewise 96
  / preprocess 176 / calibration 77 / roi_model 23 / v17 34 / ui_chain 92 / vla
  （+54 新断言）/ libav 10（A/B 部分 SKIP 为退役后预期）
- 待真机（RELEASE_CHECKLIST_V1.8 A-E）：①音频进度 0-100 唯一可见变化确认
  ②v9 老案件加载→重分析→保存→重开往返 ③CSV 列序冻结 ④设置菜单无引擎项
  ⑤OCR 校时存活 ⑥回归抽查五项
- PENDING 勾销：P-25/P-30/P-32/P-33/P-35；新增 P-54（降噪滑杆待拍板）

### 版本与文档

- 版本 1.3.1 → **1.8.0**（D4 四处一致）；DOCS_MAP 登记 V1.8 已实施 + 点检清单；
  V1_ERA §1.3 对照表 v10 行与本实施一致（方案编写时已同步）。
# ============================================================================


# 归档注记（2026-08-18 §53）：第三十九批（§48）由 HANDOVER 移入（R2 限 5 批）

# 工作记录（2026-08-17，第三十九批）——v1.9.0 P-31 施工：MainWindow 拆分四组件
# ============================================================================

## 48. P2 拆分全量落地（方案 DEVELOPMENT_PLAN_V1.9_CN.md，拍板记录见其 §8）

### 提交序列（T1 行为冻结纯移动纪律；每步全回归）

| commit | 内容 |
|---|---|
| `feat: P-31 阶段1` | **AnalysisController**（引擎构造+TaskRegistry 注册+服务装配收口）/**UiState**（时长 SSOT：beginVideo/ingestEngineDuration/effectiveDuration 单点校准，删 MainWindow m_trusted/m_current 两副本——P-37 勾销）/**VideoSessionManager**（VideoStateManager 归属+OpenPlan 打开决策数据面+现场装配 saveCurrentState）/**ProjectIO** 落位 + **lumenarc_mw_test** 新测试目标（MainWindow 全源码无头链接，21 断言）；标题 v1.7.0→v1.8.0（D4 前批漏改补齐）；openVideoFile 入 private slots（QMetaObject 测试通道） |
| `refactor: P-31 T1 ProjectIO 实装` | MainWindow 四函数体迁出（readTimestampRoiRegistry/savedTimestampRoi/saveTimestampRoi/calibrationBadgeSummary → ProjectIO 同名方法）+ saveCurrentVlaAsync 薄化（saveVlaAsync + collectVlaSaveRequest 采集器）+ onSaveAnalysis 路径分流/写出改 ProjectIO + onExportCsv 标签段整函数替换（exportLabelsCsv，三态提示逐字保留） |
| `refactor: P-31 T2/T5` | openVideoFile 数据面拆分：planOpen 决策（内存现场探测/缓存路径/入案判定）+ .vla 直载与缓存两路装载归 ProjectIO::loadVla + **applyAnalysisArtifacts 去重**（R9：两处 30 行应用块合一）+ 内存现场引用化 + **ChartPanel::setXAxisRange** 收口 R3 实锤（全工程 axisX()->setRange 清零） |
| `test: P-31 T2 决策面` | mw_test +5 断言（planOpen 冷开/内存现场往返/清空/键迁移）→ 26 断言 |
| `refactor: P-31 T3 收尾` | MainWindow 删引擎直构造（仅经 AnalysisController）+ include 去 libav 具体引擎头（R4：ui 层不见引擎名） |
| `docs+chore: §48 收口` | RELEASE_CHECKLIST_V1.9（A-F 34 项行为冻结对照）；PENDING 勾销 P-31/P-36/P-37；版本 1.9.0 四处一致；HANDOVER 归档 34 批 |

### 结构成果

- MainWindow 4183 → **3985 行**（净移 ~200 行 + 三处应用块去重）；openVideoFile
  两段 30 行重复消除
- 新组件（app 层，全部不 include Widgets，R1）：analysis_controller /
  video_session_manager / project_io / uistate；mw_test 覆盖：UiState 校准规则、
  ProjectIO 往返、OpenPlan 决策、openVideoFile 分支（dav/vla/失败）
- **债项勾销**：P-31 ✅ / P-36（R3 穿透 1 处实锤收口）✅ / P-37（时长五副本 →
  UiState 一源两派生）✅；P-35 已于 P-25 批清零；P-34 前批过期勾销——
  **五条架构债全部收口**
- 排除项照拍板执行：案件 UI/快照/播放传输留守 MainWindow（Q1-Q3）；范围只收时长（B2）

### 遇到的坑（记录防再踩）

1. **AutoUic 把 `ui_state.h` 误认为 Qt 设计器头**（`ui_*.h` 命名约定）→ UIC 报
   "state.ui could not be found"。改名 `uistate.{h,cpp}` 解决——**app 层文件名
   禁用 ui_ 前缀**。
2. mw_test 链接 MainWindow 全源码需补 `${HEADERS}`（Q_OBJECT 元对象）与
   FFMPEG_INCLUDE_DIR（ffmpeg_video_engine 直编）。
3. 大段文本锚点脚本易脆（注释缩进/换行差异）——改行号手术 + 全函数替换；
   中途污染时 git checkout 回滚重来，未污染提交。

### 验证

- 全回归 **13 套**绿（新增 mw_test 26）；每阶段提交后全量重跑
- 待真机（RELEASE_CHECKLIST_V1.9 A-F，34 项）：全部为"与 v1.8 行为一致"的
  对照验收（纯移动无功能变化）；重点 A2-A4 缓存三态 / B3-B4 保存链 /
  C1 虚高钳制 / E1-E3 轴联动

# ============================================================================

# 归档注记（2026-08-18 §54）：第四十批（§49）由 HANDOVER 移入（R2 限 5 批）

# 工作记录（2026-08-18，第四十批）——亮度分析闪退排查（P-55）
# ============================================================================

## 49. 用户真机反馈：点击亮度分析后闪退（其余功能未见异常）

### 现象与复现

- 用户实测：打开视频、绘制 ROI 后点「亮度分析」→ 程序闪退；播放/切换/其他
  功能正常。崩溃时机（点击即崩 / 分析中 / 分析完成时）待用户确认。
- **本地无法复现**：offscreen 完整链路测试（合成 H.264 640×360 素材：
  打开视频 → 源旁缓存 .vla 询问(Yes) → ROI 恢复 → onAnalyze → 进度/完成/
  气泡/自动保存）27 断言全过；repro 程序直连引擎（LibavAnalysisEngine +
  AnalysisTaskService）分析成功 50 点无崩溃。

### 排查过程（已排除项）

| # | 疑点 | 结论 |
|---|---|---|
| 1 | 引擎解码崩溃（LibavAnalysisEngine） | 排除：本地真实解码成功；该引擎 v1.5 起真机多轮验证 |
| 2 | AnalysisSnapshot 跨线程信号未注册元类型 | 排除：Qt 隐式注册机制可工作（旧版同模式多年正常）——但已**显式注册**加固 |
| 3 | TaskRegistry 悬空指针（QVector 扩容） | 排除：注册仅构造期（AnalysisController），运行期只 find |
| 4 | 结果装配（setLuminance）悬空引用 | 排除：参数按值传递，只读求值 |
| 5 | 合并策略/ChartPanel 重建 | 排除：vla_test/ui_chain/mw_test 全覆盖，off-屏幕渲染链路正常 |
| 6 | ProjectIO 后台保存（QtConcurrent 捕获） | 排除：值拷贝 + 线程安全锁（QSaveFile + g_vlaWriteMutex） |
| 7 | 完成气泡/按钮态 | 排除：纯 UI 操作，无新指针 |

### 加固（本次提交，防御性，正常路径零行为变化）

1. `Q_DECLARE_METATYPE(AnalysisSnapshot)` + AnalysisTaskService 构造处
   `qRegisterMetaType<AnalysisSnapshot>()`——跨线程 QueuedConnection 契约
   显式化（消除隐式注册依赖与潜在警告）。
2. LibavAnalysisEngine::runLuminanceTask 装配处：亮度行长度与共享时间轴
   防御截断（异常素材帧序错乱时防下游越界；正常路径零变化）。
3. mw_test 新增 `testLumaFullChain`（环境变量 `LUMENARC_REPRO_VIDEO` 门控，
   未设置自动 SKIP）——"点击亮度分析"完整链路回归网（打开→缓存询问→
   ROI 恢复→分析→完成）。

### 待用户提供（P-55 勾销条件）

1. About 对话框版本号（确认是否为 v1.9.0 构建）；
2. 崩溃时机：点击后立即 / 分析进行中 / 分析完成弹窗时；
3. Windows 事件查看器 → Windows 日志 → 应用程序 → LumenArc.exe 错误记录
   的**故障模块**（LumenArc.exe 本体 or Qt6*.dll / ffmpeg dll / GPU 驱动）；
4. 素材参数：分辨率 / 编码（H.264? H.265? DVR 私有?）/ 时长 / 是否拼接产物。

拿到故障模块即可直接定位；若为素材相关，请把出问题素材单独拷一份
（或告知文件名/来源），本地用同素材复现。

# ============================================================================

# 归档注记（2026-08-19 §55）：第四十一批（§50）由 HANDOVER 移入（R2 限 5 批）

# 工作记录（2026-08-18，第四十一批）——P-55 亮度分析闪退【根除】
# ============================================================================

## 50. 用户提供稳定复现条件 → 原素材实复现 → 根因实锤 → 修复+回归固化

### 复现条件（用户第二轮反馈）
案件模式下对「明景拼接视频_20260722172528 00_33_43-01_03_41~1.mp4」
（2560×1440/20fps/30min/783MB，即 §46 卡顿排查同一文件家族）做亮度分析，
多边形+矩形各一区域 → 稳定闪退。

### 根因链（全部本地实测实锤）
1. 该文件音轨带 **956ms 负时间戳 AAC priming 前导包**（initial_padding=
   45888 采样，Mainconcept 封装器写实）→ find_stream_info 的 500ms 探测
   窗口（§43 P-07 卡顿优化引入）从 -0.956s 起算即被耗竭，**窗口内零视频包**
   → 视频 codecpar pix_fmt 未知（ffprobe -analyzeduration 500000 复现
   pix_fmt=unknown；默认参数则正常 yuv420p）
2. avcodec_open2 对 h264 不需要 pix_fmt（解码器从 SPS 自取）→ openVideo
   成功返回，但 dec->pix_fmt == AV_PIX_FMT_NONE
3. analyzeLuminanceOne 直接拿 dec->pix_fmt 调 sws_getContext → libswscale
   av_assert(desc) 断言（swscale_internal.h:778）→ **进程 abort 闪退**
4. **与 ROI 类型无关**：sws 建表在 ROI 处理之前；纯矩形同样崩（本地验证）。
   用户报的“多边形+矩形”是操作习惯巧合；决定因素是素材封装结构
5. 排查教训：§49 合成素材 GOP 短、音轨无前导 priming → 探测窗口内即可
   拿到 pix_fmt → 无法复现。等价结构合成法：`-itsoffset 1.0` 让视频轨
   延后（344KB 素材确定性复现，修复前同点 abort）

### 修复（analyzeLuminanceOne）
按解码帧实际 width/height/format **惰性建 sws 表**（与播放引擎
ffmpeg_video_engine 既有模式一致——播放从不崩正因于此）；帧属性中途变化
（拼接源分辨率切换）时重建转换表/缓冲并重做 ROI span 预处理；建表失败
（首帧即 NONE/不支持格式）→ 中止本视频走 analysisFailed，不再闪退。
正常素材 frame 属性与 codecpar 一致——**正常路径行为零变化**。

### 验证（7.1 标准顺序：复现→修→复现不再触发）
- 修复前二进制对用户原素材：abort 实复现（引擎级 repro，矩形/矩形+多边形同崩）
- 修复后：真实明景素材引擎级 **35969 点×2 行 72s 正常完成**；mw_test
  LUMENARC_REPRO_VIDEO 全链路（打开→缓存询问→ROI 恢复→onAnalyze→进度）
  27 断言绿
- libav 套件新增 **P-55 回归**：合成音轨前导素材（前置条件自检 pix_fmt==NONE
  打印留证）→ 亮度分析必须完成且行数/时间轴对齐不变式成立（libav 10→25 断言）
- 全回归 **12 套绿**：libav 25 / mw 27 / task_registry 41 / case 248 /
  case_e2e 51 / piecewise 96 / preprocess 176 / ui_chain 92 / calibration 77 /
  roi_model 23 / vla / v17 34

### 待真机（勾销条件）
新构建部署后：案件模式对该明景拼接视频画矩形+多边形 → 亮度分析正常完成
出曲线不闪退；其余长前导音轨素材（同批次明景拼接系列）抽查。PENDING
P-55 已标✅待真机点检。

# ============================================================================

# 归档注记（2026-08-19 §56）：第四十二批（§51）由 HANDOVER 移入（R2 限 5 批）


# 归档注记（2026-08-19 §56）：第四十二批（§51）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-18，第四十二批）——P-55 真机勾销 + 音量曲线 70% + 两调研草案
# ============================================================================

## 51. 三件事：P-55 用户确认收口 + 音量曲线不透明度 70% + 两份调研方案

### 1. P-55 真机确认（勾销）
- 用户部署 §50 构建后实测案件模式明景拼接视频亮度分析（矩形+多边形）
  → **正常完成不闪退**，PENDING P-55 正式勾销（2026-08-18）。

### 2. 音量曲线不透明度 70%（用户拍板，本批实施）
- chartpanel 两处音量笔刷 alpha 180（70.6%）→ **179（255×0.70 = 精确 70%）**
  （懒创建 + rebuildSeries 两处，注释留拍板日期）；视觉上与旧值几乎无差，
  如需更明显减透再报数（如 50%）。
- 登记 P-56 ✅。

### 3. 两项功能调研（用户提需 2026-08-18，草案待拍板）
- **P-57 多视频同步播放（多机时间线合并）**：
  `docs/MULTICAM_PLAYBACK_TECH_DESIGN_CN.md`——现状盘点（引擎 setRate/
  多机只读视图/cam_timeline/校时 SSOT 全现成；缺口：CamLane 块位宽用
  分析时长而非真实时长）；草拟架构：MultiCamSyncService 状态机 + 墙钟
  主时钟 + streamMsOf 逐路换算 + 250ms 纠偏环；性能诚实账（1440p 限 2 路，
  P-29 联动）；拍板点 Q1-Q6；粗估 7-8 人日。
- **P-58 选段变速播放 + 图表/语谱一并导出**：
  `docs/SEGMENT_EXPORT_TECH_DESIGN_CN.md`——现状盘点（倍速 0.25-8x 全有/
  选段概念全库为零/renderToImage 与 renderHeatmapImage 离屏光栅化现成）；
  草拟管线：A/B 标记（[/] 键）→ 选段循环预览（引擎零改动）→ 离线确定性
  合成（底图一次光栅化+游标逐帧画线，禁每帧全量重排）→ rawvideo 管道
  → ffmpeg libx264+atempo → MP4（角标明示演示副本/倍速/区间墙钟）；
  拍板点 Q1-Q6；粗估 7-8 人日，与 P-28 共用 renderToImage 扩展。

### 验证
全回归 12 套绿（chartpanel 常量改动，无行为路径变化）。待办出口：
P-57/P-58 待用户拍板（各 6 个 Q 项）；P-56 已实施待下版真机目检。

### 补记（2026-08-18 晚）：P-57 首轮拍板
用户拍板：**2-4 路 / 独立大窗口 / 2 路界面允许未校时路“临时进”**
（会话级手动对齐，不落盘）**/ 含临时进路走“分开进度条”模式**（每路
带时间进度条，无亮度/音量/语谱面板）**/ 纯视频不看亮度**。方案升 v0.2
（Q1/Q2/Q4 回填✅）；梳理待确认清单 U-1~U-6（临时对齐交互、音频策略、
1440p 性能闸门、游标手感、瓦片 OSD、回单路跳转）待用户回复后施工。
PENDING P-57 改◐；DOCS_MAP 状态同步。

### 补记 2（2026-08-18 晚）：P-57 二轮拍板定稿
用户回复 U-1~U-6 + 新增需求一条：
- U-1 ✅ 对齐交互按草拟（含独立模式放行）；U-2 改判：**音频可切听**
  （“有时可以通过声音来定对齐点”——对齐会话期瓦片点击即切听）；
- U-3 改判：**不设硬性路数限制**——用户“不接受限制不行吗”；定案
  三级自动治理（硬解 D3D11VA 优先[引擎现成 §1220] → 软解超阈 lowres
  预览档 → 仅提示不强制）；与 P-29 解耦；
- U-4 ✅ 实时追逐；U-5 ✅ OSD 可选开关；U-6 ✅ 双击回单路；
- **N-7 新增**：每瓦片独立放大镜（滚轮缩放+中键平移，渲染侧裁剪，
  引擎零改动；原 MagnifierWidget Dock 型太重不复用）。
方案升 **v0.3 已拍板（待施工）**；工作量改估 8-10 人日（M1-M4）。
PENDING P-57 改✅已拍板；DOCS_MAP 同步。

# ============================================================================

# 归档注记（2026-08-19 §57）：第四十三批（§52）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-18，第四十三批）——P-57 多机同步播放【施工落地】（v1.10.0）
# ============================================================================

## 52. P-57 施工：方案 v0.3（已拍板）→ M1-M4 全量落地

### 落地清单（对拍板项逐条）

- **Q1/Q2 入口形态**：独立大窗口 `MultiCamPlaybackWindow`（非模态单例，关窗
  closeAll 释放全部引擎）；案件菜单「多机时间线（只读）」升级为「多机同步播放」
  （≥1 路已校时可开）；文件菜单新增「多机对比播放（2 路）」独立模式（U-1 放行）。
- **架构**（方案 §3）：domain `sync_model.h` 纯函数（墙钟↔流内仿射/临时偏移/
  覆盖判定/模式判定/纠偏决策）→ app `MultiCamSyncService`（R7 状态机
  Idle→Loading→Ready→Playing/Paused→Ended；墙钟主时钟=QElapsedTimer×rate；
  100ms 节拍；R4 引擎工厂注入，服务不识具体引擎）→ ui 瓦片 `CamTileWidget`
  （轻量自绘，不复用 VideoWidget）。
- **模式判定**：全校时 2-4 路 → 模式A 合并时间线（MultiCamViewWidget 升 v1.1：
  游标竖线+时刻签/按下拖动 scrubPreview 实时追逐/松手 seekCommit；块位改按
  **真实时长**——原 CamLane 块位=已分析时长的数据缺口由引擎 durationChanged
  回报填实，方案 §2 盘点缺口补齐）；恰 1 路已校时/独立模式 → 模式B 分开进度条。
- **U-4 实时追逐**：拖游标 = beginScrub/scrubTo/endScrub（复用引擎 setScrubMode/
  setScrubTarget demux 级追赶，主窗拖拽同手感）。
- **U-2 音频切听**：单路主听音其余静音，单击瓦片即切（🔊 角标）；对齐会话期
  同样即点即切（以声定点）。
- **Q4/U-1 临时进**：2 路界面槽位选视频（会话级偏移 tempOffsetMs，**不落盘不
  写案**，取证红线）；「对齐…」会话=暂停+两路进度条独立拖动→「确认对齐」以
  参考路当前墙钟-临时路流内位置立等偏移；取消回原偏移；瓦片「临时对齐」角标。
- **N-7 瓦片放大镜**：滚轮以指针为中心 1x~8x 连续缩放+中键拖拽平移+缩回 1x
  自动复位；渲染侧裁剪（drawImage 源矩形），引擎零改动。
- **U-5/U-6**：OSD 窗口级开关（默认开）；双击瓦片/双击合并条块 → onOpenVideo
  回主窗单路分析（多机窗不关）。
- **U-3 性能三级治理**：evaluatePerformance（load 收口一次性判定+防抖不频繁
  切档）——①硬解路（hardwareAdapterName 非空）不计入软解负载 ②软解总吞吐
  >150Mpx/s → 最重路 lowres=1 预览降清重载（ffmpeg_video_engine openFile 软解
  分支应用 m_previewLowres；仅预览降清不碰源数据；瓦片「预览降清档」角标）
  ③>2×阈 → 状态条提示不强制。

### 施工中发现并修复（本批实测触发）

1. **加载收口计数死锁**：onLaneDuration 以「durationMs 首知」为收口条件，路数据
   预设时长时永不收口（sync_test 首轮 23 失败实锤）→ 改以 durationChanged 为
   准信、m_loadAccounted 防重（lowres 重载再报不误计）。
2. **瓦片帧通道漏接**：rebuildTiles 未调 setEngine → 画面不上屏（mw_test UI 链
   放大镜断言暴露）→ 接线修复；空槽位/换路全量重载自动重接。
3. **R-1 对抗补强**：运行期引擎暴毙（非 Loading 态回 Idle，如 lowres 重载失败）
   → 标死该路+上报+占位，不拖垮全局；setLaneOffsetMs 重算内容区间与
   finishLoading 同口径（跳过坏路）。
4. **R-2 落地**：IVideoEngine 新增 learnedGopMs（引擎 m_gopLearnMs 原子化跨线程
   暴露）；纠偏阈值 GOP 联动——>4s GOP 路放宽 500ms（120ms 严阈值留短 GOP 路），
   配合「持续增长才纠」防抖兜住长 GOP seek 风暴。

### 测试（方案 §5 全项）

- **sync_test 新套件 78 断言**：纯函数（映射/模式/纠偏决策）+ 服务级假引擎对抗
  （加载收口/播放联动/缺口驻停复出/切听/纠偏实发/临时偏移重映射/加载失败/
  **性能治理两案**（双 4K 软解触档+硬解路免降）/**GOP 阈值两案**（短 400ms 内
  必纠/长 400ms 不纠）/**运行期暴毙**）。
- **mw_test +10（36）**：P-57 UI 链——独立模式开窗→临时进两路→播停→对齐会话
  →单击切听→滚轮缩放/中键平移→双击回单路→合并条拖动 scrub/commit→关窗回收。
- 全回归 13 套绿（计数见表头）。

### 待真机点检（PENDING P-57 ◐）

①4 路 1080p 30min 漂移（纠偏后 ≤100ms）②2 路 1440p 明景素材硬解流畅度
③放大镜各档位表现（含 lowres 路放大降清提示）④临时进对齐实操（以声定点）。

### 版本与文档

- 版本 1.9.0 → **1.10.0**（四处一致：CMake/app.rc/About/主窗标题×3）。
- MANUAL「多机时间线对齐视图（只读）」改写为「多机同步播放」（两模式/临时进/
  放大镜/性能治理/取证红线）；README 案件管理行同步。
- PENDING P-57 改 ◐ 待真机；DOCS_MAP 与方案头状态同步「已施工」。


# 归档注记（2026-08-19 §58）：第四十四批（§53）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-18，第四十四批）——P-57 首轮实测修复【模式B 未对齐独立播放】
# ============================================================================

## 53. 用户首轮实测反馈：拖 A 时 B 显「无信号」+ 操作逻辑不清晰

### 根因（实测实锤，三处）

1. **未对齐临时路被硬塞统一墙钟轴**：案件模式的已校时路墙钟是真实 epoch
   （~1.75×10¹² ms），临时路偏移 0 落在 1970——两路墙钟区间永不相交，
   拖任一路另一路必显「无信号」，播放时临时路甚至被「缺口驻停」不播。
   独立模式双偏移 0：A 拖过 B 时长界同样显「无信号」。
   偏离拍板方案 §3.2 原意：**模式B 对齐前两路应各自独立**。
2. **时间线区重建不删旧控件**：rebuildTimelineArea 只 delete 布局，子控件
   （进度条/名称/时间标签/合并条）以宿主为父残留——换路重载后旧进度条
   游离叠加占位抢交互（mw_test findChildren 抓到 6 条废弃 slider 实锤）。
3. **测试面失真**：QSlider 鼠标语义随平台样式变（windowsvista 凹槽点击=
   步翻页不抓手柄），UI 链 QTest 鼠标拖不动 slider——改直发信号走接线。

### 修复（语义回方案 §3.2）

- **服务引入 laneLinked 状态**：校时路恒联动；临时路对齐（alignTempLane/
  setLaneOffsetMs）后入轴。未对齐临时路：**不驻停**（play 自由播放）、
  **不随墙钟 seek/追逐**、**不纠偏**、**不显无信号**（laneCoversNow 恒 true）；
  内容区间仅含联动路（无联动路兑底全量流内轴——独立模式双临时路场景）。
- **alignTempLane(tempIdx, refIdx) 服务内收口**：以参考路当前位置墙钟为锚
  建偏移；独立模式双临时路时参考路一并锚定为基准轴；时钟对齐到临时路
  当前画面防跳变（原为窗口内联计算，收进服务可测）。
- **窗口进度条三分支**：已联动路=墙钟轴 scrub 走带；未对齐路=本路独立
  拖拽（引擎 setScrubMode/Target 直驱，同手感追逐，松手精确 seek）；
  对齐会话=独立拖动。回起点：联动路回墙钟起点，未对齐路各回流内 0。
- **OSD/进度条口径**：未对齐临时路显「未对齐·独立播放」（不给 ≈墙钟
  推算——1970 伪推算防误读）；对齐后显 ≈墙钟。引导文案三档：未对齐
  操作说明 / 对齐中（以声定点） / 已对齐联动说明。
- rebuildTimelineArea 清旧控件（qDeleteAll 直接子控件）+ 布局。

### 测试

- sync_test **96 断言**（+18）：未对齐临时路不驻停/不随墙钟 seek/不显缺口
  /区间只含联动路；alignTempLane 偏移计算+双路转联动+锚定 seek 序列。
- mw_test **40 断言**（+4）：未对齐拖 A 不动 B（隔离断言）→ 对齐 → 拖 A
  两路联动；rebuildTimelineArea 清理后窗口 slider 数量正确。
- 全回归 13 套绿（计数见表头）。

### 待办

P-57 保持 ◐ 待真机复测（含本轮修复场景：模式B 未对齐独立播放体感）。
注意：本轮构建时用户正运行旧版 LumenArc.exe（PID 锁），主程序需用户
关闭后重链；测试目标已全部绿。


# 归档注记（2026-08-20 §59）：第四十五批（§54）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-18，第四十五批）——P-59 流程重设【机位勾选面板】（用户布置）
# ============================================================================

## 54. P-57 流程重设：清单直读 + 校时标识 + 勾选同播 + 新手全流程验证

### 用户布置（原文要点）
案件多机同步播放应直接读取案件的视频和前处理文件清单、识别哪些已完成校时，
勾选的几路即可多机同播；重设流程后以新手调查员视角从头跑流程验证合理性，
必要时增加引导或简化操作，完成后列出清单。

### 重设后流程（两页栈）

**第一页 · 机位勾选面板**（案件模式开窗即达）：
- 清单全量直读：videos[] + preprocessSessions[].outputRefs[]（P### 产物同列，
  两来源都有时分组小标题）——用户布置的「视频和前处理文件清单」全覆盖；
- 逐路状态实读案内 .vla（SSOT，不看徽标缓存）：✅ 已校时 / ⚠ 未校时 /
  ❌ 文件缺失（禁勾）；已校时路默认勾选（≤4）；
- 未校时行附「去校时…」按钮（主窗打开该路走既有时间设置流程），面板附
  「刷新清单」重读；校验引导内联实时刷新：<2 路/超 4 路/3 路以上含未校时
  （禁用+指引）/可开始时给出模式预览文案；
- 「开始同步播放」装配：校时路按墙钟起点升序在前、临时路随后；全校时
  →模式A 合并时间线，含未校时（限 2 路）→模式B 分开进度条。

**第二页 · 播放页**：工具行新增「↩ 重选机位」（closeAll 释放引擎后回面板，
面板重读清单——校时后状态即刷）。

**菜单门槛取消**：案件开着即可进（原 ≥1 路已校时置灰废除——门槛改为面板
内可视引导，新手不再对着灰菜单发愣）。独立模式（文件菜单 2 路）不变。

### 新手调查员全流程验证（mw_test testMultiCamCaseFlow，+19 断言）

fixture：建案 + 3 视频（V001/V002 校时、V003 未校时）+ 1 前处理会话产物
（P001 校时，经 addPreprocessSession 登记）。逐步验证：
1. 开窗即进面板，清单 4 行（视频 3 + 产物 1）——产物在列 ✅；
2. 默认勾 3 校时路、V003 未勾；「去校时」按钮仅出现在未校时行；
3. 直接开始 → 模式A 合并 3 路（3 瓦片+合并条，引擎恰好 3 实例，V003 不载）；
4. 重选机位 → 面板重建 → 退勾 V002/P001 加勾 V003 → 2 路含未校时放行 →
   模式B（2 瓦片+2 独立进度条，无合并条）；
5. 再重选 → 勾 V001+V002+V003（3 路含未校时）→ 开始钮禁用（内联引导）；
6. 全流程无崩溃；引擎/瓦片随重选正确重建（§53 重建清理回归兜住）。

### 验证中修掉的补强

onStartSync 增加「3 路以上含未校时」兕底拦截（与面板校验同口径——私有槽
可被元调用绕过 UI 置灰，验证实测实锤）。

### 文档

MANUAL 多机同步播放节改写为「两步走」（勾机位→同步播放，含引导说明）；
PENDING P-59 勾销；P-57 条目挂 §54。


# 归档注记（2026-08-20 §60）：第四十六批（§55）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-19，第四十六批）——P-59 校时落盘双根因修复（用户实测）
# ============================================================================

## 55. 「前处理产物已校时但清单不显 + 偶发校时结果没保存」双根因实锤

### 用户反馈（2026-08-19）
前处理文件明明已经校时，但勾选清单里没显示（已校时）；且偶发校时结果
没有在案件里保存。

### 根因实锤（两个独立机制叠加）

1. **saveToFile 快照空即拒写**（timeline_model.cpp 旧判定
   `m_snapshot.isEmpty() && !hasAudio() → return false`）：前处理产物常见
   场景正是「拼接/转码完成 → 直接校时（从未跑分析）」——校时应用调到
   saveCurrentVlaAsync（v1.7.1 修过一半：调用了落盘），但 saveToFile 底层
   仍拒写 → .vla 根本没生成 → 重开丢失 + 清单读不到（.vla SSOT）。
   「偶发」的真相：只发生在没跑过分析的视频上，跑了分析的不丢。
2. **保存顺序倒置**：saveVlaAsync 每call一支 QtConcurrent 线程无串行——
   分析完成自动保存（旧校时请求）与校时采用保存（新校时请求）并发时，
   旧请求可能后提交盖掉新校时（g_vlaWriteMutex 只保互斥不保顺序）。

### 修复（四处）

- **Fix A（timeline_model）**：校时有效即可写（META+空 TMS/LUM 是合法
  v10，读端容忍零计数）；真正全空（无分析+无效校时，如「清除校时」）
  → 串行区内删除残留旧文件（清除语义落盘，重开不复活）。
- **Fix B（project_io）**：saveVlaAsync 改单飞+尾追合并写——保存中来的
  新请求覆盖式登记只留最新，在途完成后拾取再写一次；**最新请求必胜**。
- **Fix C（cam_timeline 清单）**：vlaRelPath 空（旧数据产物）回落源旁
  .vla（与 CaseManager::vlaPathFor 回落同语义）。
- **Fix D（mainwindow）**：vlaSaved 此前无消费者（写盘失败用户无感）→
  接 showOperationStatus 警示（C2 不静默）。

### 测试

- vla 套件 +8（校时独占往返/清除删残留/不生幻影文件）；
- mw_test +5=64：ProjectIO 合并写最新必胜（111/222 轮候断言）+ P-59 流程
  ④（V003 校时独占 .vla 写入 → 刷新清单变 ✅ 并默认勾、4 路全校时放行）；
- 全回归 13 套绿（计数见表头）；主程序重链 09:19。

### 待真机

用户复测：前处理产物校时 → 清单刷新即见 ✅；偶发丢校时应绝迹（发现再报）。


# 归档注记（2026-08-21 §61）：第四十七批（§56）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-19，第四十七批）——多机播放窗原生最小化/最大化按钮（用户布置，两轮定稿）
# ============================================================================

## 56. 多机同步播放窗加最小化/最大化按钮（用户布置，两轮定稿）

- **第一轮**：工具行右侧自绘「⛶ 最大化/🗗 窗口化」切换钮（changeEvent 联动文案）。
- **用户截图拍板**：要的是素材转码拼接页那种**原生标题栏 —/□/✕**——该窗是
  QDialog（默认标题栏只有关闭钮）。第二轮撤自绘钮，构造置
  `Qt::WindowMinMaxButtonsHint`，原生最小化/最大化/关闭三键齐全（与前处理页一致）。
- **测试**：mw_test 改断言窗口旗标携带 Min/MaxButtonHint（65 checks，0 失败）；
  全回归 13 套绿；主程序重链 09:52；MANUAL 多机节同步。


# 归档注记（2026-08-21 §62）：第四十八批（§57）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-19，第四十八批）——音频分析 PTS 空档补齐（用户实测实锤）
# ============================================================================

## 57. 拼接产物「37:03 听到声音但频谱该段空白」——音频频谱轴与播放错位

### 用户实测链路
1. 合并产物听不到声 → 实测：文件音轨完好（AAC 8kHz mono），前 36 分钟为
   素材真实静音（62 段源中前 36 段 max_volume -28.8~-69.5dB≈无声），
   36 分钟起有强声；拼接音频直拷忠实还原——非软件问题。
2. 用户再测：37:03 听到声音，但音量曲线读 -80dB、语谱该段空白；前面
   曲线有显示却听不到声 → 音频频谱时间轴错位特征。

### 定量实锤
- 解码 PCM 首个强声点 = 36:36；用户实际听到 = 37:03 → **偏早 27 秒**。
- ffprobe 逐包扫描：音频流 PTS 存在 391 处 >0.2s 跳变（61 个拼点各插入
  ~0.7s 空档对齐墙钟），全长累计 ~56s——concat 直拷天然产物。
- 播放端按 PTS 走（音画正确）；分析端（libav_analysis_engine
  analyzeAudioOne）此前纯按解码序拼 PCM，把空档全部塌掉 → 音量/语谱轴
  渐进提前。帧级取证工具的时间轴错位 = 严重精度缺陷。

### 修复
analyzeAudioOne 解码循环逐帧比对 PTS 与期望位置（上帧 PTS+帧时长），
超 20ms 即补等量静音（单段上限 30s 防御；PTS 缺失帧自然顺延）。

### 测试
libav_test +7=32：合成 MKV（两段 440Hz 正弦夹 0.988s PTS 空档，注意
matroska 写头后 time_base 收编 1/1000 需按实际 tb 打点）→ 分析时间轴
张满全 PTS（音量帧 ~94 含空档）、空档区静音、两段正弦分列两侧不塌。
全回归 13 套绿。

### 待办
- 主程序已重链（2026-08-19 17:49，用户确认未开程序后执行；冒烟 OK）。
- **用户需对既有拼接产物重跑「音频分析」**——旧 .vla 存的是塌缩时间轴，
  重跑后曲线/语谱与播放位置即对齐。

# 归档注记（2026-08-21 §63）：第四十九批（§58）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-19，第四十九批）——P-60 OCR 排序「一个 Go」施工落地（v1.11.0）
# ============================================================================

## 58. P-60 智能排序体验重设（用户拍板 v0.2：Q1A/Q2A/Q3 修正仅本次/Q4A/Q5A/Q6 现在做）

### 落地项

1. **GO 一键直通（Q2A）**：导入页大 GO = 识别+排序+拼接一条龙（原 GO=直接拼接
   降级为 flat 小字旁路「直接拼接（不识别时间）」）。onEvidenceReady 经
   `canAutoProceed`（单分组/无重叠/无存疑/估算 ≤2 且 ≤20%）→ 直通执行，
   跳过②③页；B 路径才停确认页。
2. **未识别段夹缝插值（smart_sorter）**：mtime 级/无证据段 → 唯一空位夹缝
   插入（前后连续容差 2s，链式推算）→ 否则端点外延（mtime 近端优先，
   无则放尾，首段越界放尾）；标 `Estimated`（conf 0.5，提示级警告
   `EstimatedPlacement`「未识别到时间，位置为推算」）；推算归位不再触发
   suspicious（未推算残留仍存疑）。
3. **同机位 ROI 自学习（Q3 仅本次任务）**：
   - Python（probe_timestamps.py）：crop_blocks/merge_ocr_lines/_search_crops
     带框回报（fbox→roiNorm 归一化，外扩 3% 宽+1 行高）；process_file 接 roi
     参数；批量模式消费 --roi-json（此前仅 at 模式）；结果带 roiNorm。
   - 引擎：run() 末参 rois 写 --roi-json；解析 first.roiNorm → OcrResult.hitRoi。
   - Coordinator：首轮后从成功段学 hitRoi → 未识别段窄 ROI 重试一轮
     （m_roiRetryDone 防循环；frames-only 段排除）；reOcrWithRoi 人工定版
     （本段+其余未识别段一并套用）。
4. **问题卡确认页（Q4A）**：`collectSortProblems` 纯函数（OverlapPair/
   Unidentified ≤3 逐段/SuspiciousGroup 大批组级卡防卡片海）；②页重构——
   问题卡区（缩略图+大白话+就地按钮：框一下/手输/交换前后/保持现状）+
   完整视图折叠（查看全部片段）+ 确认主按钮门控「还剩 n 处待确认」清零
   才放行；框选对话框 RoiPickLabel（证据帧拖框，免 moc，无图退手输）。
5. **留痕**：operations.log 记估算段数与 ROI 学习/重试；报告「衔接警告」列
   带估算标注；完成页 ⚠ 估算段醒目提示。

### 测试与验证

- preprocess_test 176→**207**（+31：夹缝插值/端点外延×2/链式/警告分级/
  canAutoProceed 五态/collectSortProblems 五态）；全回归 13 套绿。
- 版本 → **v1.11.0**（4 处同步；顺手修 app.rc FILEVERSION 1,9 遗留）。
- 主程序重链 21:40（冒烟 OK）；probe_timestamps.py 随构建自动同步。

### 待真机（P-60 ◐）

- GO 一键：越秀 62 段批次全识别 → 应直通拼接零打扰；
- ROI 自学习提速与命中率（对比首版 3-5s/段）；
- 人为制造未识别段（遮 OSD/黑屏段）→ 问题卡框选一段全批跟着认；
- 估算段黄标+报告留痕+完成页提示可见性。


# 归档注记（2026-08-21 §64）：第五十批（§59）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-20，第五十批）——前处理排序现场修复系列（v1.12.0，越秀案实测）
# ============================================================================

## 59. 「按 GO 后读取首尾帧智能排序」全链修复——2026-08-19/20 广州越秀案实测反馈

### 现场反馈（越秀 99 段批次：`0.mp4~99.mp4` 无时间戳文件名）

1. 素材混入大量重复导出段，拼接产物出现重复画面；
2. 文件名全是数字无时间戳：旧字典序排成 0,1,10,11…19,2…（ Explorer 与用户
   预期均不符）；「直接拼接」按拖入锚点顺序盲拼——8/18 片段被拼到 8/19 之后；
3. 时间重叠段未修剪（旧版弹窗询问，默认保留原样）；
4. 拼接产物校时只有首段准：单条仿射在缺口后失真（Q-4 只能警告）。

### 修复项（①~④⑥ 用户布置/拍板，⑤ 随拍板「校时反映到产物时间轴」落地）

1. **导入即内容去重**（新 domain 纯函数 `dedupe_plan`）：尺寸不同必不同内容
   （免哈希，大文件友好）；仅同尺寸碰撞组算 SHA-256（与案件指纹同算法），
   指纹一致判重、保留首个；哈希失败保守保留（C2 不静默丢数据）。状态栏
   明示排除清单（前 5 段「X（同 Y）」+ 总计），导入横幅与 MANUAL 同步。
2. **无时间戳文件名自然序**（导入页 + coordinator 两处同修）：QCollator
   数字感知排序，0,1,2… 与资源管理器一致。
3. **「直接拼接」护栏**：文件名全无时间信息且尚未识别排序时弹乱序风险
   提示，一键改走 GO；导入页即时内联引导文案。
4. **重叠默认修剪 + GO 直通不再被打断**（本批关键改判）：
   - 检测到重叠 → 默认修剪（Q-17：剪后段开头、保前段完整）、运行日志与
     报告逐段留痕；设置页高级选项「保留时间重叠段原样」为唯一退出开关；
     原「修剪/保留」弹窗废除（改设置页 ⓘ 明示）。
   - `canAutoProceed(groups, trimOverlap)` / `collectSortProblems(groups,
     trimOverlap)` 增策略参：修剪生效时 Overlap 不再阻断 GO 直通、不出
     问题卡（越秀批 ~20 处重叠若逐段出卡即「不太可用」复现）；退出修剪
     时恢复旧阻断语义（顺序需人工裁决）。校对页摘要重叠条目标注
     「将自动修剪」。三条执行路径（GO 直通/确认后拼接/直接拼接）统一
     经 `overlapTrimOn()` 应用同一策略。
5. **sidecar 分段校时**（writeSidecar 增 trimStarts/skips/actualStreamMs
   三参）：感知重叠修剪（墙钟起点后移 trim×rate）/整段丢弃/转码失败剔除；
   转码段逐段 ffprobe 实测时长（与源时长偏差 ±30~300ms，累积污染尾锚点）。
   loadSidecar 分段锚点 → PiecewiseTimeMap 查表校时（`piecewiseApplied`），
   缺口处墙钟精确跳变（监控常态）；含变速段标 speedVariant。主窗口继承
   提示改「分段模式」信息级文案（缺口数量类型化解析）。
6. **默认组产物名修复**：`isDefaultGroup` 补半角 "(默认组)"（运行期实际组名，
   smart_sorter/coordinator 两侧均是半角；旧仅匹配全角 → 产物带字面
   "LAMerged_(默认组)_87_21.mp4"，实测实锤）。

### 答用户问「缺段怎么处理」时挖出的级联 bug（本批最大收益）

用户追问「有些时间节点没有（缺段）怎么解决」→ 逐段核对产物锚点时实锤：
**OSD 单位数字误读**（原始文本如 `03:318:56`/`03:58627`）使首/尾一端墙钟
跳变 → rate 出现 8.69/0.049 级异常 → 幻影跨度级联：47.mp4 首帧误读
（03:38:55 → 03:30:59）被排到错误位置 → 假重叠 53.8s → 误剪 47 开头、
健康段 37.mp4 被「完全重叠」误丢弃（数据丢失）；8/4/19.mp4 等尾帧误读
污染 sidecar 段速率与缺口表（43 处缺口里大半是幻影）。

**三处联动修复**：
- **smartSort 首尾帧交叉验证**：首帧 wallStart 与尾帧推算起点（尾帧墙钟
  − 尾帧流内实测位置）是两路独立证据；分歧 > max(15s, 尾帧位置×10%)
  （容忍真变速段）→ 两套候选各排一次序取全组连续性误差 Σ|Δ| 小者
  （与证据①②裁决同法），平局保留首帧并标存疑；被否端弃用防污染，
  原文留痕 EvidenceConflict 警告。单文件组无邻段可裁决 → 弃尾帧+存疑。
- **overlap_cut 速率换算**：墙钟重叠量 ÷ 段速率 = 流内修剪量（旧版
  rate=1 直剪，变速段过剪 2 倍）；丢弃判定改墙钟域；覆盖止点单调递推
  （丢弃段不再回退参照系漏剪后续）。
- **writeSidecar 速率分母**：尾帧墙钟对应尾帧流内实测位置（早总时长
  1~3s）——旧按总时长算出系统性 ~0.94 偏慢速率；SortEntry 新增
  ocrEndFrameRelMs 透传。

**修复后同批次复跑**：47.mp4 纠正到 03:38:55（46/48 之间严丝合缝）、
37.mp4 恢复入列（70 段锚点，零丢弃）；修剪从幻影 53.8s 降为 3 段真实
亚秒级（0.2/0.7/1.0s）；缺口从 43 处幻影噪声降为 **18 处真实缺口**
（最大隔夜 10.3h）；产物时长 01:05:31（+1:37 被找回的内容）；rate
范围 0.957~1.335（仅 1 段 14s 级尾帧误读低于容差下限逃脱，段内轻微
漂移不影响排序，已留痕）。

### 验证（真实批次端到端）

- 新无头全流程驱动 `lumenarc_folder_pipeline`（去重→探测→OCR 首尾帧→
  智能排序→重叠修剪→最小转码路由→拼接→产物校验→sidecar 写出+读回锚点
  抽查；--dry-run/--no-ocr/--no-trim/--recursive/--limit 可组合），
  越秀批实测：**99 → 去重 70（排除 29）→ OCR 70/70（conf 0.95）→ 单组
  全 OCR 时间序（8/18 段正确居前）→ 修剪 1（53.8s 重叠）/整段丢弃 1 →
  转码 69/69（统一 CFR 13fps，音频直拷）→ 拼接 1:03:54 → sidecar 69 段
  锚点读回校验逐秒吻合 → RESULT: PASS，退出码 0**（修剪生效下重叠不再
  触发阻断退出码 3）。
- preprocess_test 207→**249**（+去重 14/重叠放行与问题卡策略 3/半角命名 1/
  首尾交叉验证 24）；sidecar_test **34**（含速率分母用例）；v17 **37**
  （+变速修剪换算/丢弃后参照系 2）；全回归 **14 套绿**（计数见表头）。

### 版本与文档

- 版本 1.11.0 → **1.12.0**（CMake/app.rc/About/主窗标题×3 一致）；本批注释
  统一 v1.12.0 标签（上一会话误标 v1.3.1 已全部更正）。
- MANUAL：GO 子弹点改写「读取每段首尾帧的画面时间 → 自动排序 → 拼接」，
  重叠自动修剪不再打断流程；前处理继承节改「分段校时」说明。
- PENDING：P-60 勾销（真机验证通过）；新增 P-61 登记本系列并勾销。
- 主程序重链 20:30（探烟 OK）；probe_timestamps.py 本批无改动（构建自动同步）。
- 实测产物留存：`桌面/20260819广州越秀/_headless_test/`（拼接件 + 报告
  headless_pipeline_report.txt），用户目检后可删。
- 后续硬化点（未在本批）：脚本侧 parse_timestamp 对 `03:318:56` 类非法时间
  文本应拒识而非部分解析（排序层交叉验证已兑底）；19.mp4 级 14s 尾帧误读
  低于容差下限逃脱，必要时可收紧 kHeadTailCheckFloorMs。

### 待真机

- GUI 端 GO 一键复跑越秀批：应「识别 → 排序 → 修剪 → 拼接」零打扰直通
  （不再停问题卡页）；完成页/报告估算与修剪留痕可见性。
- 拼接产物在主窗打开：分段校时继承提示（43 处缺口）+ 时间轴缺口跳变符合
  预期（对照 36:36 强声点墙钟）。


# 归档注记（2026-08-21 §65）：第五十一批（§60）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-20，第五十一批）——完成页证据摘要卡（v1.12.1，拍板 A）
# ============================================================================

## 60. 拼接完成页证据清单加强（用户提需 + 拍板：只做 A 默认展开，缺口逐条）

### 拍板

用户审阅加强建议后拍板：**只做 A（完成页证据摘要卡，默认展开），缺口部分
逐条列出缺了哪些墙钟时间段**；B（去重清单进 CSV）/C（summary.txt 落盘）
不做，产物首尾帧缩略图不上卡。

### 落地

- 新 domain 纯函数 `run_summary`（computeRunSummary）：证据计数（OCR/流内/
  文件名/人工/估算/未知）、墙钟覆盖范围、缺口清单、修剪/丢弃统计。
  **口径与 writeSidecar 完全一致**（段速率 = (ocrEnd−start)/尾帧流内实测位置，
  修剪段起点后移 trim×rate，丢弃段不入覆盖，缺口容差 2s）——完成页数字与
  产物 sidecar/CSV 报告三处互证（取证一致性原则）。
- 协调器暴露 `cutPlans()`；窗口 `m_dupExcluded` 累计去重排除清单（清空重选
  时复位）；完成页新增只读摘要区（Consolas、可选中复制、默认展开，单文件
  免处理/无输出路径自动隐藏防上一轮残留）。
- 卡片内容：一行结论（导入 N → 排除重复 X → 识别 Y/推算 Z/人工 K → 输出
  M 个文件）→ 覆盖时间（yyyy-MM-dd HH:mm:ss 起止 + 跨度）→ **缺口 n 处
  （共缺约 X）逐条列出 `起 → 止（缺 时长）`**（同日省日期，上限 30 条防
  刷屏）→ 修剪/丢弃行 → 排除重复清单（前 10 条 + 等 n）。全双语（lang()）。

### 测试

- preprocess_test 249→**268**（+19：连续组/缺口 from-to/变速段速率感知无
  幻影缺口/修剪起点后移+丢弃剔除/无墙钟段）；全回归 14 套绿。
- 版本 1.12.0 → **1.12.1**（CMake/app.rc/About/主窗标题×3；顺手把误被
  sed 覆盖的 §59 sidecar 注释改回 v1.12.0 标签）。
- MANUAL ④节同步摘要卡说明；主程序重链。

### 待真机

GUI 跑越秀批 → 完成页目检摘要卡（缺口应列 18 条，最大 10.3h 隔夜；修剪
3 段亚秒级；去重 29 段清单）。


# 归档注记（2026-08-21 §66）：第五十二批（§61）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-21，第五十二批）——GO 链修复：按 GO 不走 OCR 智能排序（v1.12.2，用户实测实锤）
# ============================================================================

## 61. 前处理「按 GO 后不走 OCR/智能排序」双根因修复（GUI 接线级，无头驱动漏网）

### 用户反馈（2026-08-21）

前处理按 GO 后不走「识别画面时间 → 智能排序」流程，实际直接拼接。

### 双根因实锤（GUI 接线两级；§59 无头全流程驱动直驱 domain/引擎不经
协调器链式接线，故漏网）

1. **链式标志被会话复位吞掉**（beginWithAutoSort，bug 自 2c83c87「非强制
   一键拼接」批引入）：先 `m_autoSortAfterProbe = true` 再调 `begin()`，而
   begin() 会话复位第一刀 `m_autoSortAfterProbe = false`——探测完成后
   onTrustedDurationsReady 检标志必为假 → runAutoSort 永不链式触发。
2. **列表序中间态 evidenceReady 上表面**：探测就绪即发「按导入顺序成组」
   的 evidenceReady——该组单分组/无警告/估算 0，`canAutoProceed` 恒真 →
   窗口 onEvidenceReady 直通 startProcessing（文件名自然序盲拼，OCR 从未
   跑）。症状：GO 与「直接拼接」完全同效（还绕过乱序护栏弹窗）。

### 修复（coordinator 两处，窗口零改动）

- beginWithAutoSort：改先调 begin()、成功（阶段转 Probing）后才挂链式
  标志；begin 早退（进行中）不挂。
- onTrustedDurationsReady：GO 链分支不再发中间态——不发 evidenceReady
  （防窗口误直通）、不发 phaseChanged(UserConfirm)（防中途切校对页闪屏）；
  静默置 m_phase=UserConfirm（runAutoSort 相位前置）直接衔 runAutoSort。
  OCR+排序完成后由 runSorting 统一发相位与 evidenceReady——窗口看到的首个
  结果就是排序终态，可信直通 / 问题卡 B 路径恢复 §58 设计语义。
  非 GO 链（begin 直拼）行为逐字不变。

### 测试（mw_test 65→78，+13）

testPreprocessGoChain 双链对照（不存在文件：探测快败/OCR 降级均收敛
runSorting，状态机断言与环境无关，确定性）：
- GO 链：相位必经 Probing→Ocr→Sorting→UserConfirm（根因①回归）、
  evidenceReady 恰好一次且在 UserConfirm 发出（根因②回归）、文件全保留入组；
- 直拼链对照：仅 Probing→UserConfirm、不进 OCR/排序（非强制一键语义不变）。
- 变异验证：回退修复重跑，准确失败（OCR/排序相位缺失 2 断言）。
- 踩坑留痕：测试初版 watcher 声明在 coord 之后，作用域结束 watcher 先析构、
  coord 析构内 cancel() 发信号到死 lambda → 堆损坏（0xC0000374）；
  调声明序后绿（析构时信号仍发射，监听必须后死）。

### 版本与文档

- 版本 1.12.1 → **1.12.2**（CMake×3/app.rc×4/About/主窗标题×3）。
- 全回归 14 套绿（mw 78；libav 本机跑 20——caltest 素材缺失跳过，环境项）。
- 主程序重链（LumenArc.exe 随 ALL 构建刷新）。
- PENDING：新增 P-62 并勾销。

### 待真机

GUI 复跑含 OSD 批次（如越秀 0~99.mp4）按 GO：应见「OCR n/N」进度 →
可信直通拼接或停校对页问题卡；不再出现「按 GO 直接拼」。


# 归档注记（2026-08-21 §67）：第五十三批（§62）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-21，第五十三批）——拼接产物时间轴缺口语义（v1.12.3，越秀案复测实锤）
# ============================================================================

## 62. 「拼接没按时间顺序 + 播放时间轴不显示校时结果 + 缺段怎么处理」三连修复

### 用户复测反馈（2026-08-21，v1.12.2 会话 preprocess/20260821_083826）

①没按时间顺序拼接；②拼完播放时间轴不显示校时结果；③缺段校时应怎么处理
（要方案）。

### 证据链逐条实锤

**① 拼接顺序其实完全正确（误判）**：operations.log 显示 GO 链已生效（§61
修复立功：探测→OCR 88s→排序→修剪→转码→拼接）；sidecar 70 段锚点墙钟
严格单调递增（全 OCR 0.95）；产物抽帧 5 点（30/250/1300/2500/3900s）OSD
与 sidecar 推算逐秒吻合。误判根源在②。

**② 播放时间轴错乱根因 = streamMsOf 无缺口语义**：分段校时继承生效，但
ChartPanel 刻度用「墙钟域等距步进 → streamMsOf 反解流内位置」——旧
streamMsOf 对落在缺口的墙钟沿前段斜率外推幻影流内位置，外推超过轴尾后
标签循环提前 break。探针 probe_gap_axis 实数据复现：真实轴只画出
14:15~15:15 共 13 个幻影刻度（视频 94% 内容 OSD 实为 02:53~04:16）——
用户观感即「时间轴没校时、顺序不对」。产物旁 .vla 未落盘（继承异步保存
可能遇应用退出竞争 / 用户见轴乱后清过校时——无法复现确认，mw_test 新增
继承全链回归兑底）。

### 修复（逐条）

1. **域层缺口语义**（time_piecewise）：streamMsOf 缺口内墙钟夹取到缺口后
   一段起点（seek「跳过没录的」，不再外推）；新增 segmentWallEndMs/inGap/
   gaps() API；loadSidecar 补设末段 streamEndMs。
2. **时间轴分段模式改流内域等距刻度**（ChartPanel::updateTimeLabels）：
   每个刻度墙钟由 wallMsOf 逐段锚定（恒真）；缺口处红色虚线竖线 + 红色
   「缺 10.3h」小字上轴（fmtGapDuration 紧凑格式）；次级刻度同改流内域。
   仿射路径零改动。
3. **缺段校时显示方案**（答用户问，拍板级定案）：轴保持流内连续（播放
   连续），刻度只标真实有素材的墙钟 + 缺口红标；点击缺口 seek 到缺口后
   第一段。否决方案：拉伸轴到墙钟域（65min 素材摊 14h 轴不可用）、插黑帧
   补连续（篡改证据产物不可接受）。完成页摘要卡缺口清单（§60）互证。
4. **多机清单 sidecar 回落**：buildCamInventory 读 .vla 无效时回落
   .lumencal.json——产物不再误报「未校时」。loadSidecar 实现下沉 domain
   （loadSidecarCalibration，time_calibration；CalibrationService 同名壳
   转发，case_test 免链引擎链）。

### 测试（+35）

- piecewise_test 96→**121**（+25：segmentWallEndMs/gaps 容差/inGap 边界/
  streamMsOf 夹取/首末段外推不变/wallMsOf 单调不变）；
- vla_test +**gaptick 4**：仿越秀 fixture 渲染断言——14 真实刻度全轴分布、
  2 缺口标记、大缺口后不断供、刻度文本=该位置真实墙钟（无幻影）；
- mw_test 78→**84**（+6：真小视频+缺口 sidecar → openVideoFile 继承 →
  .vla 落盘 + piecewise 双段锚点逐毫秒正确）；
- 探针实数据对照：旧 streamMsOf 幻影 13 刻度+断供 → 新语义 169 刻度零
  断供（夹取生效）；全回归 14 套绿。
- 版本 1.12.2 → **1.12.3**（11 处同步）；主程序重链。

### 待真机

重开越秀案 → 开产物（或重跑 GO）：时间轴应全轴真实墙钟刻度 + 红色缺口
标记；缺口处墙钟跳变与 OSD 一致；多机勾选面板产物应示 ✅ 已校时。


# 归档注记（2026-08-21 §68）：第五十四批（§63）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-21，第五十四批）——缺口红字碰撞隐藏（v1.12.4，用户实测反馈）
# ============================================================================

## 63. 「时间缺口指示与图表时间轴重叠」——刻度优先，红字重叠即隐藏、放大恢复

### 用户实测反馈（2026-08-21，v1.12.3）

缺口红色文字（「缺 10.3h」）与图表时间轴刻度文字重叠。
用户拍板方案：有重叠时红色文字不显示（红虚线保留），放大到不重叠才显示。

### 根因

updateTimeLabelPositions 的碰撞隐藏遍历 m_timeLabelItems **数组序**，
lastVisibleRect 假定 x 递增；v1.12.3 的缺口标记**追加在刻度项之后**——
靠左的缺口与左侧刻度永不判碰 → 红字直接压刻度显示（探针级复现：
fixture 下旧逻辑全览两个缺口红字均显示并与刻度重叠）。

### 修复（chartpanel）

- updateTimeLabelPositions 改**两遍法 + 优先级**：第一遍非缺口刻度按 x 升序
  互避（刻度永远优先保留）；第二遍缺口红字与所有已放刻度/起止标签/已放缺口
  判碰，重叠即 setVisible(false)（虚线在 m_tickMarkItems 不受影响）。
  缩放/平移重建标签 → 放大到不重叠自动恢复显示。
- 新增平行数组成员 m_labelIsGap 标记缺口项；测试钩子
  axisTickVisibilityForTest()/zoomToRangeForTest(min,max)。

### 测试（vla_test +2，变异验证通过）

- gapcollide 回归：fixture 缺口 1 置于 600200（紧贴稳定可见刻度 600000、
  离起止标签远、%500=200 保证秒级放大后离两侧刻度 200/300ms≈100px）——
  全览断言两缺口红字均隐藏、放大（跨度 2.4s）断言缺口 1 红字恢复。
- **变异验证**：回退 chartpanel.cpp 碰撞修复 → 旧逻辑 shown@full=2（用户
  症状复现）测试准确转红；恢复修复 → 绿。
- 全回归 14 套绿；版本 1.12.3 → **1.12.4**（11 处同步）；主程序重链。


# 归档注记（2026-08-21 §69）：第五十五批（§64）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-21，第五十五批）——北京时间对时重做（v1.12.5，用户拍板）
# ============================================================================

## 64. 「校时页面对真实时间逻辑不对」→ 校时图片两框 OCR + 手动两时间 + 直输偏移

### 用户拍板（2026-08-21，附增城案典型校时照片实证）

旧逻辑：手输一个「真实北京时间」减当前播放位画面墙钟得偏移——不符合取证
习惯。新逻辑（用户逐条拍板）：

① 一张校时图片框两个框（监控主机时间 / 北京时间），OCR 自动识别算偏差；
② 中文数字混排格式约定由我方设计（实证照片：监控 OSD
「2026年07月22日 星期三 12:25:47」；手机授时网页日期行「现在是2026年
7月22日星期三，第30周」（不补零）+ 大数字钟「12:39:41」）；
③ 偏差 = 北京时间 − 监控主机时间，整体常量偏移映射到时间轴（沿用
truthOffsetMs）；④ 图片+框坐标+OCR 原文+偏差存档；⑤ 手动输入支持两种：
输入两个时间自动算偏差 / 直输偏移量「比北京时间 快/慢 X日X时X分X秒」；
⑥ 单张一次为准。

### 施工

- **域解析器** truth_time_parse（新）：单行完整（年月日中文/横杠/斜杠、
  补零与否、可带毫秒）→ 跨行组合（日期行+时间行）→ 纯时间+假定日期；
  无秒拒识（noseconds:）/值域非法（invalid:）/无匹配（nomatch）类型化错误；
  防碎片错配（112:39:41 不内层误命中）。
- **TimeCalibration 留档字段**：truthSource(photo/manualTimes/manualOffset)、
  truthImagePath、truthMonitorBox/truthBeijingBox（像素）、truthMonitorText/
  truthBeijingText（OCR 原文）；toJson/fromJson 向后兼容（老文件无字段
  安全退化）。
- **probe_timestamps.py calibphoto 模式**：一图两框 → preprocess 双变体
  OCR → merge_ocr_lines 行组按置信度降序（CALIBPHOTO: 单行 JSON）；
  TimestampOcrEngine::runCalibPhoto（3min 看门狗）→ CalibrationService 转发。
- **CalibPhotoDialog**（新组件）：两步橡皮筋框选（框1 橙红「监控主机」/
  框2 青蓝「北京时间」），视图↔原图像素换算，<8px 误触不收。
- **校时窗口第 2 步重做**：方式一按钮入口（引擎缺失置灰）→ 确认识别卡
  （两个时间+原文+偏差表述+跨日注记，OCR 误读提醒人工核对）；方式二两个
  时间编辑框（监控时间默认当前播放位墙钟、「取当前画面时间」按钮）；
  方式三 快/慢下拉 + 日/时/分/秒 spin。跨日疑义：框 2 仅时间且 |偏差|>12h
  → ±1 日取 |偏差| 较小者并在确认卡注明。
- **留档**：calibrationApplied 时若 truthSource==photo 且案件打开 → 图片
  复制入 案件目录/calibration/（best effort，失败明示状态栏）。

### 实证与测试（+27）

- 真实照片冒烟：python calibphoto 正确返回两框行组（含目标文本；同时实证
  OCR 会把监控 OSD 秒位 47 误读 17——确认卡人工核对设计由此而来）；
- calibration_test 77→**99**（解析器 10 组用例[增城原文逐字]+留档往返 12
  断言）；ui_chain 92→**97**（方式二/三/清除全链 5 断言）；
- 全回归 14 套绿；版本 1.12.4 → **1.12.5**（11 处同步）；主程序重链。

### 待真机

增城照片实测全链：导入 → 框 1 框 OSD 行、框 2 框手机大时钟（含日期行更准）
→ 确认卡核对 → 应用后时间轴/快照/CSV 报时应为北京时间；案件目录
calibration/ 应有图片副本。.vla 重载后留档字段应完整回读。


# 归档注记（2026-08-22 §70）：第五十六批（§65）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-21，第五十六批）——对时确认卡集成可放大图片 + 第 3 步删除（v1.12.6，用户实测反馈）
# ============================================================================

## 65. 确认卡集成校时图片（可放大校对）+ 校时第 3 步高级区删除（死代码清除）

### 用户实测反馈（2026-08-21，v1.12.5 真机截图：确认卡已正确识别增城照片
### 并算出「慢 13 分 59 秒」——链路全通）

①确认页面要把校时图片集成在卡上，图片带放大功能方便校对；②校时第 3 步
（高级）删除（没有作用），相关死代码清除。

### 施工

- **TruthPhotoConfirmDialog + ZoomPhotoView**（calibphotodialog 模块新增）：
  左侧校时图片（框 1 橙红/框 2 青蓝叠加，线宽与标签随缩放反比恒定）——
  **滚轮以光标为锚缩放（适应 0.5×~30×）、左键拖动平移、双击复位适应**；
  右侧两时间+两原文（可选中复制，等宽字体）+偏差表述+跨日注记+核对提示
  +「✅ 使用此偏差 / 取消」。替换原 QMessageBox 纯文本确认卡。
- **第 3 步高级区整体删除**：手动输入画面时间 / 录像机自带时间（免识别）/
  强制变速重建三节 + 折叠容器。GO 预检可疑自动进重建、结果仍由第 1 步
  结果区呈现应用（使用此结果/查看细节不变）。失败引导文案改指第 2 步
  手动输入。
- **死代码清除**（全库零引用验证后删除）：对话框 onAdoptManual/
  onAdoptAbsStart/onRunReconForce/onAbsStartReady/fillSegmentTable + 8 个
  高级区成员；CalibrationService 的 probeAbsStart/absStartReady/
  fromAbsStart/fromSinglePoint/onProbeFinished + m_probeEngine 成员 +
  m_absPending（MediaProbeEngine 类本身仍被前处理使用，保留）。
- MANUAL 校时节重排（①② 两组；时间重建改「GO 自动进入」表述；④⑤ 编号
  回收入正文）。

### 测试

- ui_chain 97 全绿（GO/框选/对时三式回归不经高级区，删除无感知）；
- 全回归 14 套绿；版本 1.12.5 → **1.12.6**（11 处同步）。
- 主程序重链受用户实测占用阻塞，关闭程序后重链即可（本批末次构建时
  LumenArc.exe 在运行中）。
- **追加修订（当日，用户复测反馈）**：确认卡两个时间由只读改为**可直接修改**
  （QDateTimeEdit），修改后偏差行**实时重算**，采用时以卡上编辑后的时间为准；
  人工修正会在 truthNote 追加「（确认卡人工修正读数）」留档。fmtOffsetVerbose
  移入 TruthPhotoConfirmDialog（静态，供确认卡与方式二预览共用）。


# 归档注记（2026-08-22 §71）：第五十七批（§66）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-21，第五十七批）——多机播放三修（v1.12.7，用户实测反馈）
# ============================================================================

## 66. 多机播放：北京时间偏移全局生效 + 画面调节 + 校时图片钮显眼化

### 用户实测反馈（2026-08-21，v1.12.6 真机）

①「从校时图片识别」按钮不够显眼；②多机同步播放没应用北京时间校对后的偏移；
③多机播放没有画面调节。（④编号合并轨单列，见 P-69）

### 施工

- **②根因**：sync_model.h 的 syncWallOf/syncStreamOf 用 cal.wallMsOf（OSD 墙钟），
  漏加 truthOffsetMs——单路分析/图表/CSV 全走 beijingMsOf，独多机掉队。
  修复单点：truthSet 路墙钟轴 = wallMsOf + truthOffsetMs（未对时路 offset=0 不变），
  服务 seek/覆盖/擦除/纠偏与窗口 OSD/合并时间线全部自动跟随。
- **画面调节**：CamTileWidget 增加 setDisplayAdjust(DisplayAdjust, rotation)——
  存原始帧，显示帧 = 旋转 + LUT（复用 displayadjust 共享实现，恒等零拷贝）；
  多机窗口工具行新增「画面调节」钮 → PlaybackAdjustPanel 悬浮面板，
  **全部瓦片统一应用**，rebuildTiles 后新瓦片继承当前参数。
- **校时图片钮**：与 GO 同级强调（40px 高 + 主题强调色背景 + 粗体 +「推荐」
  字样），占满行宽。

### 测试

- sync 96→103（truth 映射 7 断言：wallOf/streamOf 正反/wallStart/wallEnd/覆盖/
  未对时路不变）；全回归 14 套绿；版本 1.12.6 → **1.12.7**。


# 归档注记（2026-08-22 §72）：第五十八批（§67）由 HANDOVER 移入（R2 限 5 批）

# ============================================================================
# 工作记录（2026-08-21，第五十八批）——重建失控根因修复 + 画面调节逐瓦片（v1.12.8）
# ============================================================================

## 67. 天河案 merged_concat 重建失控（OCR 月份位成片误读）三层防线 + 画面调节逐瓦片

### 用户报告（2026-08-21）：merged_concat.mp4（天河测试案，85.3h 三拼）校时
### 「出了很大问题」；另拍板画面调节只对选中瓦片生效、每路独立调节。

### 根因（.vla 逆向实锤）

重建产出 5 段病态分段：62s 宽段 rate 1.94、墙钟锚在 2026-02（倒流 5 个月）。
样本链分析：该 DVR 字体的**月份位「07」被 OCR 成片误读为「02/03」**
（~260 样本中 30+ 处，conf 0.95、格式干净、不触发单点可疑判定），且常
2~3 个**连续成簇**——尖峰野点检测的「单点、前后斜率异号」特征不成立而漏网，
边界检测遂把每簇当真边界。

### 修复（三层防线，piecewise 域）

1. **markOffsetSpikes**：偏移域（r=wall−stream）双侧基线过滤——|r−前窗中位|
   与 |r−后窗中位| 同超 10min 才剔（真时间缺口是台阶非尖刺，单侧即吻合，
   不误伤）；两遍执行可剔 ≤3 连簇。
2. **数据级墙钟单调闸**：干净点序列相邻倒流 >10s → 物理不可能 → 整图拒绝
   （置于边界检测前——段合并会把台阶吸收成「正常单段」静默出错）。
3. **段级单调闸**：段交界墙钟倒流 >2s → 拒绝（防御纵深）。
   拒绝后 CalibrationService **回落稳健仿射**（干净测点偏移中位数、rate=1、
   conf 0.5 降置信访 UI 复核）——不再 emit failed。
- 附带：**零锚 sidecar 不再算有效校时**（isEffective：分段全 wallStartMs=0
  = 无时间信息，杜绝 1970 时间轴；源全未校时的拼接产物回落「未校时」）。

### 画面调节逐瓦片（用户拍板）

多机窗口：点击瓦片选中（强调色外框），「画面调节」面板只作用于选中瓦片，
面板标题实时显示作用机位、切换时回填该路既有参数；每路独立存储
（m_tileAdjusts/m_tileRotations），重建瓦片后保留。

### 测试

- piecewise 121→**129**：天河场景复现（孤立+2 连簇剔净无伪边界；4 连簇
  要么剔净要么闸拒；永久下台阶必被闸拒）；**两轮变异验证**：过滤关闭 →
  ①红（伪边界）；闸关 → ③红。全回归 14 套绿；版本 1.12.7 → **1.12.8**。


> 【归档注记】本节原为 HANDOVER.md 第六十八批记录（§68，v1.12.9），
> 2026-08-22 §73 批按 R2 规则归档移入。
## 68. 案件列表放大镜折叠 + 放大镜半屏并列 + 调节浮窗化 + 全局字体统一

### 用户反馈（2026-08-21，附 v1.7.0 旧版布局截图拍板②）

①案件系统列表打开放大镜后不折叠（旧版视频列表会）；②打开放大镜后期望
布局 = 列表折叠 + 左半原始画面/右半放大镜等大并列；③画面调节面板做成
独立浮窗呼出（与多机同步页一致）；④前端字体不统一，全面美化。

### 施工

- **①**：案件面板同款折叠占位条（m_casePlaceholder，竖排「案件列表」+ ▶
  展开钮）；createMagnifier 案件模式收 m_caseDock、removeMagnifier 按
  进入前状态恢复（用户中途主动展开则不打扰）。
- **②**：放大镜 dock 宽度 40% → **50%** 窗口宽（截图拍板的半屏并列）。
- **③**：画面调节面板呼出即 setFloating(true) + 定位主窗右侧（320×460），
  visibilityChanged 回写钮态（既有机制）。
- **④根因**：全局样式表未设应用默认字体，未显式指定的控件回落平台默认
  （MS Shell Dlg）混搭。Theme::apply 现设 setFamilies{YaHei UI → YaHei →
  Segoe UI → PingFang SC} 9pt；全局样式表补 QGroupBox/QTreeWidget/
  QTableWidget/QHeaderView/QTabWidget 统一观感（圆角、边框、选中、表头）。

### 测试

- 全回归 14 套绿（含几何敏感的 vla gapcollide）；版本 1.12.8 → **1.12.9**。


> 【归档注记】本节原为 HANDOVER.md 第六十批记录（§69，v1.13.0），
> 2026-08-23 §74 批按 R2 规则归档移入。
## 69. P-68 施工：选段分段变速导出（单路）+ 多机同款（第 10 条）

### 拍板（2026-08-21，SEGMENT_EXPORT v0.1 Q1-Q6 + 追加）

Q1 改判**上下布局**（上视频下双图）；Q2 改判 **OSD 角标可选**（导出对话框
勾选，默认开）；Q3 隐藏无数据面板 ✅；Q4 改判**倍速分段可调**（不关键段
快放/关键段常速慢放——选段内 N 标签自动成分速边界初值，对话框逐段调速
/游标处再分段/删边界）；Q5 入 .vla ✅；Q6 固定 1080p ✅；7 A/B 键既有
（复用）；8 对话框可调分速 ✅；9 工具栏入口+进度可取消 ✅；**10 多机同步
播放同款选段导出**。

### 落地

- **domain `speed_plan.h`**（纯函数）：SpeedPlan{splits, rates} 规范化 +
  输出↔源双向分段线性映射 + 帧计数 + rateAtOutputMs（OSD 动态倍速）+
  planFromLabels（标签初值分段）。
- **`SegmentExportEngine`**（infrastructure）：libav 顺序解码（关键帧回退）→
  QImage 画布合成（上下布局；图表/语谱底图区间光栅化一次 + 分速 warp +
  逐帧游标）→ rawvideo stdin 管道 → ffmpeg libx264 CRF18 + 音频分段
  atrim/atempo 级联/concat（atempoChain 纯函数可测）；进度/取消/半成品清理。
- **多机模式**（lanes 非空即入）：墙钟域 plan，宫格瓦片逐路 syncStreamOf
  取帧、缺席格「该时刻无画面」占位，底部各路覆盖条随分速 warp，音轨取主听
  路（未全程覆盖→无音轨并注明）。SeqDecoder 每路一个顺序解码。
- **持久化**：.vla META 增 ab_region/speed_plan JSON 键（旧读者忽略未知键）；
  全空判定门增 ab/plan（只有 AB+方案的现场也落盘，不再被当空删文件）。
- **UI**：主窗工具栏「导出选段」（AB 存在使能）；多机窗 A/B/Ctrl+A 打点
  +合并时间线选段底纹虚线边界+「导出选段」（模式A）。

### 排雷记（施工实证）

- bundled FFmpeg 头文件**无 extern "C"**——消费方必须手动包（既有代码惯例）；
- 离屏测试用 QCoreApplication 时 QPainter.drawText 崩 0xC0000409（字体子系
  统未初始化）——须 QApplication；
- QStringLiteral 不能包变量（kErrPrefix → QLatin1String）；
- 多机模式 start() 参数校验误要求 sourcePath（lanes 承载源）。

### 测试

- 新套件 **segment_test 54 断言**：speedplan 映射/规范化/标签分段/atempo 级联/
  音频链/布局/.vla 回环 + **caltest 单路 e2e**（产物时长 ±400ms 校验）+
  **双路多机 e2e**；映射 ×2 变异 → 3 断言转红（有判别力），回退复绿。
- 全回归 **15 套**绿；版本 1.12.9 → **1.13.0**。

# ============================================================================
# 工作记录（2026-08-21，第五十九批）——界面优化四项（v1.12.9，用户反馈+截图拍板布局）
# ============================================================================


> 【归档注记】本节原为 HANDOVER.md 第六十一批记录（§70，v1.12.9→1.13.0 区间），
> 2026-08-23 §75 批按 R2 规则归档移入。
## 70. 放大镜填满右半屏 + 导出面板非模态化 + 导出管线硬化（用户真机实测反馈）

### 反馈与根因

1. **放大镜布局**：初版「裁切取景填满 dock」被用户否掉（纵向取景收窄不可
   接受），代码+测试已干净回退；用户标注目标布局=放大镜嵌右上象限、
   图表/语谱图全宽、左列全高+手动折叠控件——重构施工见 §71。
2. **导出对话框模态挡游标**：打开后不能播视频拖游标，「在游标处分段」
   形同虚设。修：SegmentExportDialog 改**非模态浮窗**（可最小化置顶），
   游标经 positionChanged 实时喂入；进度条+取消内嵌面板（废模态
   QProgressDialog）——主窗/多机窗两侧同款改造。
3. **导出失败不可用（无头自检实锤两点）**：
   - **管道无背压**：QProcess 写缓冲由写入线程异步排空，1080p 一帧
     8.3MB、生产者快于 x264 消费者时缓冲无限膨胀，长选段可吃光内存。
     修：bytesToWrite>256MB 即 waitForBytesWritten 背压（实测 140s/2800
     帧长导峰值仅 125MB）。
   - **编码器假定 libx264**：LGPL 版 ffmpeg 无 libx264（探针实锤
     "Unknown encoder"）。修：pickH264Encoder 预检 -encoders 自动回退
     libx264→libopenh264→h264_mf（按二进制路径缓存）。
4. **真机自检（用户验收口径：≥2 真实案件视频、各 ≥3 分速段、打开验证）**：
   build_tmp/probe_export 探针（moc+cl 直编，build_probe_export.bat）——
   - 明景拼接（2560×1440 20fps AAC 48k 立体声）：60~75s 选段 2x/1x/0.5x
     三段 → 产物 17.46s（期望 17.5s）347 帧非黑、音轨在 ✓；
   - D17（2560×1440 25fps AAC 16k 单声道）：120~150s 选段 4x/1x/0.25x
     三段 → 52.40s（期望 52.5s）1307 帧非黑、音轨在 ✓；
   - 明景长导 300~540s 4x/2x/1x → 140.00s（期望 140s）2797 帧 ✓；
   - OSD 目检：GUI 模式导出抽帧确认「演示副本 · 1x · 00:05:04 · 案件」
     金字正常烧录、倍速随分段跳变 ✓（offscreen 无字体渲染为方框是
     Qt 无字库固有现象，非缺陷）。

### 排雷记

- 探针/CLI 程序解析路径须用 QCoreApplication::arguments()（宽字符命令行），
  raw argv[] 是 ANSI 码页毁中文路径；
- 打包版应用 ffmpeg = build/Release/ffmpeg/ffmpeg.exe（GPL 版含 libx264），
  third_party SDK 版是 LGPL（无 libx264）——两套二进制用途勿混。

### 测试

- ui_chain 97 绿（放大镜语义更新）；segment 54 绿（背压+编码器回退后）；
  全回归 15 套绿。探针产物 build_tmp/probe_out*.mp4 验证后可删。

# ============================================================================
# 工作记录（2026-08-21，第六十批）——P-68 选段分段变速复合导出落地（v1.13.0）
# ============================================================================


> 【归档注记】本节原为 HANDOVER.md 第六十二批记录（§71，v1.13.1），
> 2026-08-23 §76 批按 R2 规则归档移入。
## 71. 放大镜 QDockWidget→QWidget 内嵌顶行 + 案件列表常驻手动折叠条

### 拍板来源

用户两次实测纠偏：①v1.12.9「右 dock 半屏」方向错误——dock 天生贯通整窗高度，
挤压图表区且放大画面上下大黑边；②本批初案「裁切取景填满 dock」（源区域纵横比
随视口）被用户否掉（纵向取景收窄不可接受），代码+测试干净回退后，用户以
**亲手标注截图**拍板目标布局：

```
┌────────────────────────────────────────────┐
│ 菜单栏/工具栏                                │
├──┬───────────────────┬─────────────────────┤
│视│                   │                     │
│频│  原视频（左半）    │   放大镜（右半）     │  ← 同一行等大同高
│列│                   │                     │
│表│                   │                     │
│案├───────────────────┴─────────────────────┤
│件│  图表（量化分析）——右区全宽              │
│列├─────────────────────────────────────────┤
│表│  语谱图——右区全宽                        │
└──┴─────────────────────────────────────────┘
```

### 施工

1. **MagnifierWidget 基类 QDockWidget→QWidget**：ctor 改 QVBoxLayout 承载
   ContentWidget（左缘 1px 分隔线）；所有 dock API（addDockWidget/
   setWindowTitle/setFeatures/resizeDocks 50%）从 MainWindow 清除。
2. **中央布局**：顶行新增水平 QSplitter m_topRow [m_videoWidget | m_magnifier]，
   垂直 m_splitter 三行 [m_topRow | 图表 | 语谱图] 不变；左列 dock 天然全高。
   createMagnifier → m_topRow->addWidget + 首次均分；用户拖过分割条的比例在
   removeMagnifier 存 m_topRowSavedSizes、下次呼出恢复。
3. **关闭回排排雷**：QSplitter 子项 deleteLater/摘除后**不自动拉伸剩余子项**
   （offscreen 截图实证右半空白）——removeMagnifier 显式
   `setParent(nullptr)` + `setSizes({width()})` 让视频立即吃满整行。
4. **案件列表常驻手动折叠条**：复刻视频列表模式（28px 竖排细条+◀/▶钮），
   重排 CaseDock 内容 [细条|内容]；放大镜自动折叠（占位细条）逻辑保留且改为
   记录真实手动状态——用户本已手动收起的，关放大镜后仍保持收起。
5. **回归网**：mw_test 新增 testMagnifierLayout（合成 2s 彩条素材 → 开窗 →
   invoke onMagnifierWheelZoom → 几何断言：同行顶对齐/同高/等宽/图表全宽/
   左列全高/案件折叠钮存在/关闭后视频吃满整行）+ 开关态截图留档
   （build/Release/maglayout_shot_open.png 人工目检）。mw 84→96。

### 排雷记（offscreen 平台）

- **offscreen 下 isVisible() 恒 false**（裸 QWidget show() 探针实证），
  无头可见性断言无效，一律改几何断言；窗口截图用 grab()（强制渲染不受
  可见性影响）；
- offscreen 无字体目录 → 截图文字全 tofu，仅看布局不看文字。

### 启动比例二次修正（用户复测反馈，三轮定稿）

首版纯 stretch 因子 7/7/4 被图表/语谱 sizeHint 顶歪（实测启动视频行仅 21%）。
二版 singleShot(0) 落地——**真机仍无效**：main.cpp 构造 MainWindow 后、
showMaximized() 前 splash 连续 app.processEvents()，singleShot(0) 被提前
触发时窗口未 show 高度无效，守卫直接跳过（offscreen 测试 show 先于事件泵
所以假绿）。三版定稿：伸缩因子 55/21/15 + **resizeEvent 首次有效尺寸**
（h>200）显式 setSizes 落地一次（一次性，之后用户拖分割条/缩放自理）；
collapse/expand 基准 {550,230,160}。mw_test 补「视频行≈55%±8%」断言 +
**构造后先泵事件再 show 的 splash 次序回归网**（旧写法在此次序下必红）。

### 排雷记（真机 vs offscreen 次序差异）

- main.cpp splash 模式：ctor → processEvents×3 → showMaximized——ctor 内
  singleShot(0) 在 processEvents 即触发，勿用于依赖窗口尺寸的一次性落地；
  一次性布局落地钩子 = resizeEvent/showEvent 首达。

### 测试

- mw 97 绿（新增 13 断言）；ui_chain 97（mag.widget()→mag.grab() 适配基类
  变更）；全回归 15 套绿。
- 版本 → **v1.13.1**（CMakeLists/app.rc/aboutdialog/mainwindow 四同步）。

# ============================================================================
# 工作记录（2026-08-22，第六十一批）——P-68 实测四项返修（v1.13.0）
# ============================================================================


> 【归档注记】本节原为 HANDOVER.md 第六十三批记录（§72，v1.13.2 P-73），
> 2026-08-24 §77 批按 R2 规则归档移入。
## 72. 同事件对时：以已校时路为参考给未校时路生成正式校时（取证链入档）

### 拍板（用户三连）

①一步到位（单锚偏移+多锚仿射同期）；②事件名必填（报告可读性依赖）；
③确认卡独立取证链小节。追加拍板：**允许多跳链**（现实考量）+ **成环禁止**
+ 累积容差逐跳如实呈现。

### 架构

- **域层 `src/domain/event_calib.h`**（纯函数，无头可测）：
  - `EventAnchor`{refLaneId/refStreamMs/refWallMs(快照)/targetStreamMs/
    eventName/markedAtMs/toleranceMs}——参考墙钟取**录入时刻快照**，防参考
    校时后改导致链断；
  - `fitAnchors`：0 锚→EVENTCALIB_NO_ANCHOR（C1 类型化）；1 锚→偏移型；
    ≥2→最小二乘仿射，rate≤0 或 |rate−1|>0.2 → EVENTCALIB_BAD_RATE 拒收
    （对错事件防御）；目标时刻全同退化偏移型；
  - `wouldCreateCycle`：上游可达性递归（多锚点多参考逐边）；
  - `expandChain`/`cumulativeToleranceMs`：取证链展开+逐跳容差累加；
  - `frameToleranceMs`=500/min(fps)（较粗路半帧）。
- **TimeCalibration**：Source 增 `CrossCamEvent`（序列化 "crosscamevent"）+
  `eventAnchors` 成员随 cal JSON 一体入 .vla（`event_anchors` 键，老读取端
  忽略未知键向后兼容）——**无需改 project_io/timeline_model 模式**。
- **MultiCamSyncService::applyLaneCalibration**：换 cal+转正（temporary 摘帽）
  +偏移清零+重算区间+广播。
- **MultiCamPlaybackWindow 对时模式**：工具行「同事件对时」（≥2 路全载且
  有校时路可见；与对齐会话互斥）；进入强制**每路独立进度条**（合并条无法
  分路定位事件帧，rebuildTimelineArea 加 m_eventCalib 条件）+滑条四处理复用
  对齐路径（m_aligning||m_eventCalib）；右列 310px 面板：参考/目标路下拉
  （间接路标「（间接）」）、双路帧步进钮、事件名（必填校验）、锚点表
  （残差列）、生成校时并预览（播放联动即生效）、保存（确认卡模态：拟合
  摘要+**独立取证链小节**+累积容差）、退出（未保存=会话级，明示）。
- **落盘**：ProjectIO 载旧 .vla 全字段→仅替换 calibration→写回（保其他
  分析成果）；路径分流 suggestSavePath（案件 videos/V###.vla / 独立源旁）。

### 置信度与诚实性

间接校准天然低一档：单锚 conf=0.6；≥2 锚低残差（≤200ms）0.8，否则 0.7；
残差逐锚列出；链上每跳容差与累积容差在确认卡如实声明。

### 测试

- sync 103→**135**：拟合 6 组（空/单/双/带噪/退化/病态拒收+容差）+
  成环守卫 5 组（多跳允许/A←C 成环/自环/无锚新参考）+ 链展开+累积容差 +
  JSON 回环（source/锚点/中文事件名/无锚老数据兼容/rate 换算生效）；
- **变异验证**：偏移符号反转 → ecfit 红，回退绿；
- 全回归 15 套绿。版本 → **v1.13.2**。

### 排雷记

- heredoc 超长被截断（bash 报 PYEOF 未终结）——大块代码改用 write 写
  build_tmp 零件再 python 拼装；
- sed 批量版本号会误改历史注释（v1.13.1→v1.13.2 把 §71 注释洗版）——
  教训：版本 sed 后必须回查注释行，本批已逐行修正（仅标题栏保留）。

# ============================================================================
# 工作记录（2026-08-22，第六十二批）——放大镜布局重构（用户标注拍板）v1.13.1
# ============================================================================


> 【归档注记】本节原为 HANDOVER.md 第六十四批记录（§73，v1.13.2 P-73），
> 2026-08-24 §78 批按 R2 规则归档移入。
## 73. P-69 编号合并轨：同机位多文件并一路播放（先起步者赢 + 跨段换文件）

**日期**：2026-08-22｜**版本**：1.13.2 → **1.13.3**｜**性质**：功能批次（P-69 落地，设计 8-22 拍板）

**需求**：同机位编号连续文件（如 D17_001/002/003.mp4）在勾选面板各占一路、
挤占 4 路上限且时间线分行——用户拍板：同编号并成**一路**播放。

**拍板设计**：重叠段「先起步者赢」+ 时间线 ⚠ 告警；合并仅模式 A（同编号任一
文件未校时 → 整组不出现，拒静默拼半组 C1）；合并轨会话暂拒导出（明文提示）。

### 落地件
1. **域层段感知**（`src/domain/sync_model.h`）：`SyncSegment`（path/srcId/段级
   cal/durationMs + wallStartMs/wallEndMs/coversWall）；`SyncLaneData.segments`
   + `isMerged()` + `segCumDurations()`（虚拟流内轴前缀和）；`syncSegmentAt`
   （先起步者赢）、`syncMergedStreamOf`（墙钟→虚拟轴，缺口钉最近段端点）、
   `syncMergedSegmentOf`（虚拟轴→(段号,段内毫秒)）；`syncWallOf/syncStreamOf/
   syncLaneWallStart/syncLaneWallEnd/syncLaneCovers` 全部段感知——**服务层既有
   seek/覆盖逻辑零改动复用**。
2. **SegmentSwitchEngine**（新 `src/app/segment_switch_engine.{h,cpp}`）：
   IVideoEngine 装饰器，内置一台工厂真实引擎（C5：N 段不增引擎数）；
   seek 跨段 → 换文件 + pendingSeek 落点 + 续播意愿；播放至段尾且下一段墙钟
   紧邻（≤2s）自动顺接换文件（缺口交给服务覆盖逻辑停路）；引擎实测时长
   回写段时长自修正；`currentSegment()` 供瓦片段标。
3. **服务**：`loadLanes` 合并轨走装饰器分支，其余路径不变。
4. **装配层**（`cam_timeline`）：`CamInventoryItem.analyzedDurationMs`（.vla
   已分析时长，免 ffprobe）；`buildMergedGroups`（**头文件内联纯函数**，sync_test
   直编验证免链重依赖）；`probeMediaDurationMs`（ffprobe format=duration 兜底）；
   `ToolPaths::findFfprobePath` 收敛公用（calibration_service 本地副本删除）。
5. **勾选面板**：合并轨组行置顶「⊞ 标签 · N 段合并轨」+ 组合计 1 路 +
   组/成员勾选互斥禁用。
6. **时间线**（multicamview）：`CamLane.segs` 段块；`BlockRect` 条目重构
   （行+段号+矩形+overlapClips）；合并轨一行多块同色调明暗交替；被压重叠区
   斜纹 + 左缘 ⚠；tooltip 带段号。
7. **主窗收尾**：`currentCamLanes` 填段块；`updateTilesOsd` 合并轨段标 [k/N]；
   `onExportClip` 合并轨拒导提示。

### 验证
- **sync_test +33 = 168/168 绿**：段映射（赢家/缺口钉段/往返）、服务装载
  换段（FakeEngine 按路径报时长；跨段 seek → loadCount+1/落点 1500/虚拟轴
  位置 6500）、分组规则（未校时拒组/排序/单成员不成组）。
- **变异验证**：赢家规则 `<`→`>` 反转 → 2 断言红；回退复绿。
- **15 套全回归绿**（mw 97 / case 248 / sync 168 / vla 全 PASS …）。
- case_test 链 cam_timeline 需 tool_paths.cpp（findFfprobePath）——CMake 已补。

### 真机返修（同日第二轮，天河案实测「没有合并成功」）
- **根因**：归组键误用 displayName——无标签前处理产物退化为文件名，致
  P002/P005（产物标签指向源机位 id）漏配、P002/P003（同名 merged_concat.mp4）
  险错配。**修复**：新增 `CamInventoryItem.groupKey`（cameraLabel 非空用之，
  缺省=自身 id），buildMergedGroups 改按键归组；测试补「标签指向 id」真机
  数据形态断言（P002 组={P002,P005}、P003 组={P003,P004}、同名文件不互染）。
  sync 168→**172**；变异验证（回退 displayName 键→3 红）通过。
- **第三轮（同日，「还是没有合并成功」）**：默认勾选顺序把成员行先自动勾上
  → 互斥规则反而把合并轨组行禁用变灰，用户根本选不到；且成员行编号打头
  「P005 P002」被误读。修复：新增纯函数 `pickerDefaultChecks`（组行优先默认勾、
  组成员不勾、非组成员补足至 ≤4 路），成员行有标签时改「标签（编号）文件名」；
  sync 172→**179**，变异验证（组不优先→3 红）通过。
- **顺手排雷**：主窗标题 3 处「v1.7.0」陈年漏网串（案件打开/关闭/清空路径）
  从未随版本 bump 更新——已全部归位 v1.13.3（此前截图标题栏误显旧版号，
  与功能无关但误导排障）。

### 同事件对时面板 UX 重做（同日第四轮，用户拍板方案后施工）
- 痛点：面板一次性摆出全部控件、术语重（参考路/锚点/残差/间接），
  用户不知先后顺序。
- **方案（用户拍板）**：单面板④段逐段解锁 + 顶部状态横幅；术语大白话化
  （参考路→「谁的钟是准的」/目标路→「要修谁的钟」/锚点→「同一瞬间标记」/
  残差→「对时误差 X 秒」>0.5s 红字提醒找错事）；第 2 个标记**自愿**（拍板）；
  标记行即时口语结论「目标的钟慢 2 分 14 秒」；保存前强制先预览。
- 域层新增纯函数：`eventcalib::guidanceStep`（状态机 0选路/1打标记/2可预览/
  3预览中）+ `plainClockDeltaText`（钟差口语化）——sync 179→**188**；
  变异验证（状态机 1/2 颠倒→3 红）通过。
- 业务逻辑（建锚/成环守卫/拟合/取证链确认卡/落盘）零改动；MANUAL 小节重写。
- 引导逻辑复盘：横幅文案由 guidanceStep 唯一驱动（updateEcGuidance），
  分段使能 m_ecStep3/m_ecStep4；第 2 标记提示仅在恰 1 标记且未预览时显示。

### 对时模式滑条失效返修（同日第五轮，「拖进度条画面没动」）
- 根因：rebuildTimelineArea 重建的每路滑条量程停在 Qt 默认 0..99——量程
  只在 State::Ready / onLaneInfo 信号里设置，而那早在进对时模式前发完；
  用户拖到顶 = seek 前 99ms（2 小时素材里等于原地）。修复：滑条创建即按
  当前路时长 setRange。

### 对时沙盒三连修（同日第六轮，真机反馈）
- **静止帧不能播**：对时模式曾强制暂停且走服务联动（缺口停播/遮罩逻辑在
  「找事件」阶段全是有害干扰）→ 对时模式改为**自由沙盒**：播放钮直驱各路
  引擎 play/pause（不经服务状态机），窗内 150ms 定时器驱动进度条/OSD 跟随
  （updateBarsFromEngines 从 onClock 抽出共用）；退出对时收沙盒。
- **声音对不齐**：同听两路需求（喇叭/轰鸣类声音事件）→ 服务新增
  setCustomAudible/clearCustomAudible（非空集合优先于单可听路），面板③段加
  「🔊 同时听这两路的声音」开关，换路自动重挂，退出对时还原单可听路。
  sync 188→**193**。
- **遮罩不撤**：缺口遮罩判定 laneCoversNow 依赖服务墙钟，对时模式墙钟不走
  → 沙盒期不再显示缺口遮罩（updateTilesOsd 加 m_eventCalib 短路）。

### 第七轮：对时下拉错排 + 导出三修（真机反馈）
- **「要修的钟选不了第一路」**：refreshEcPanel 重建下拉时先填目标列表再还原
  参考选择——clear() 后参考当前项=0（第一路），目标列表错把第一路永久排除。
  修复：先还原参考再填目标。
- **导出完成无定位入口**：SegmentExportDialog 成功态新增「📂 打开所在文件夹」
  （explorer /select 直选产物；新一轮导出自动收起）。
- **导出不含放大镜画面**：引擎 Params 新增 laneZooms（多机逐瓦片 zoom/center
  快照）+ magnifierPip/SrcRect/Rotation/Zoom（单路放大镜）；合成时每格/视频区
  右下 PIP 嵌入放大内容（38% 宽、Accent 描边、「放大镜 ×N」角标）。探针
  probe_export 加第 8 参 PIP 规格，明景真机导出抽帧目检 PIP 生效。
- **图表条/标签/OSD（用户四轮澄清拍板）**：「时间轴」=光标（应动态随视频
  时间移动）、「书签」=图表标签（ChartLabel）。落地：①光标对齐 chartRect
  （原按画布全宽算右端溢出错位）+ 顶部三角柄造型；②选段内标签在曲线条打
  同色竖标+文字（位置随变速 warp）；③播到标签时刻 OSD 烧录「🏷 内容」，
  5 秒隐去（重叠取最新）。Params.labels 入参，主窗 m_chartPanel->labels()
  接线。探针两轮真机自检：三帧序列验证 5s 窗隐现（标签色正确）、合成图表
  条验证竖标位置（302s→20%、306s→60%）与光标 65% 动态位+三角柄。

### 遗留
- 真机待验：合并轨（已验 ✅ 用户确认「可以了」）；同事件对时新引导+沙盒
  三件套（自由播放/同听两路/无遮罩干扰）。
- 合并轨导出合成：后续批次（当前明文拒导）。
## 74. P-28 分析报告模块启动：DOCX 地基 + 点位图编辑器方案（P-74）

**日期**：2026-08-23｜**版本**：1.13.3（地基无用户可见面，随批次②升 1.14.0）｜**性质**：功能批次（P-28 批次①）

**拍板**（用户 + 《火灾视频分析报告模板.md》）：只出 DOCX；哈希 MD5+SHA-256
双列；静态目录无页码；向导入口（勾章节+补录元数据+附件图）；章节号重排
一~七+落款；远期 HTML 渲染器接口预留。

**地基落地**：
- `ZipStoreWriter`：手写 store（不压缩）模式 ZIP——OPC 对压缩无要求，
  零依赖零私有 API，固定 DOS 时间戳产物字节级确定（取证可复算）；
- `DocxWriter`：极简 OPC 子集——标题 1-3 级（黑体加粗居中/居左）、正文
  （宋体小四首行缩进 1.5 倍行距）、带框表格（首行底纹加粗/归一化列宽）、
  分页符、PNG 图片嵌入（EMU 宽高+rels）；
- `docx_test` 23 断言：CRC32 已知向量/zip 回读/中文 UTF-8 条目/OPC 结构/
  document.xml QXmlStreamReader 良构/转义/表格/分页/图片嵌入。
- **测试套数 15 → 16**（新增 lumenarc_docx_test）。

**P-74 点位图编辑器方案成文**（docs/SITEMAP_EDITOR_DESIGN_CN.md，拍板：
不画比例尺/扇形方向且扇面可调/一案一张）：底图导入+机位拖放布点+
扇形朝向（张角半径可调）+标准图框出图（2480×1754 PNG 入
reports/assets/sitemap.png）+ sitemap.json 归一化坐标持久化 + 孤儿点位
标红。待施工（粗估 1.5~2 天）。

## 29c) 光标-曲线 2s 偏移根治（08c5dee，v1.16.1）
- **三段判决**：合成片实验证分析双链清白（10000/9962ms）；AV 追踪探针（LUMENARC_AUDIO_TAP）证播放音画对齐清白（43ms）；病灶=**DVR 音频 PTS 空档**：分析侧 P-59 补静音（曲线对），播放侧压塌 → 空档后声响提前 gap 时长，仅带空档文件发病=时有时无
- **修复**：processAudioPacket 空档>40ms 补等长静音（padAudioSilence 分块背压、封顶10s）、重叠>40ms 裁帧首；sink 短写循环补写防护；open/seek 重置
- **测试**：engine_test avgap 场景（NUT 合成空档片+tap 断言哔声 2.0s±0.45，实测 1.998s）；libav_test testAvEventAlignment 永久回归
- **坑**：QFile 在 Windows 走 stdio 4KB 缓冲，小行写不 flush 不落盘 → tapClose（析构/unload）+每64行冲刷；matroska 拒 rawvideo（用 NUT+codec_tag I420）；engine_test 场景要先 seek(0) 再 play

## 29d) v1.16.1 发布 + Win10 闪退结案 + 副屏全屏（3fb9c05→发布）
- **Win10 闪退结案**：根因=缺 VC++ 运行库，安装随包 vc_redist.x64.exe 即可；已入 MANUAL 常见问题首行+异常退出诊断条目
- **副屏全屏**（adcc92b，用户拍板 6 点）：FullscreenVideoWindow（无边框/letterbox/ESC双击退出/光标3s自隐/4K自适应降质）；视图菜单动态列屏+F11 上次屏；帧与调节与主视口同源共享；暂停态推 rawFrame
- **崩溃黑匣子**（e097dbc）：UEF→MiniDumpWriteDump（dbghelp）+阶段面包屑+会话锁+异常退出提示；LUMENARC_CRASHTEST=1 自毁验证过
- **Release v1.16.1**：https://github.com/Pavo-fi/LumenArc/releases/tag/v1.16.1（290MB zip）
- **打包新坑**：①cygwin 重定向 >nul 会在目录生成真「nul」文件→Compress-Archive 崩（保留设备名），打包前删；②manual2pdf 独立目录运行弹 Qt platform plugin 框挂起——Qt6 可重定位构建插件前缀随 Qt6Core.dll 目录，build.bat 现自带 platforms/（qoffscreen+qwindows）且工具默认 offscreen；③build.bat 必须 CRLF+英文注释
- **遗留**：build/Release/Qt6Test.dll 是测试会话补的（不入包，已在打包排除）；mw_test 曾因此假绿（tail 管道吞 rc）——回归要看真 rc

## 29e) Win11 自动校时失效结案（c6877a1）
- **根因**：v1.16.0 起打包排除清单误杀 `probe_timestamps.py`（被当开发探针；实为自动校时 OCR 运行时脚本，TimestampOcrEngine 从 applicationDirPath 加载）。开发机有该文件故无感，安装包机器全部失效
- **处置**：pack_release.py 固化打包管线（必含清单 12 项源目录+zip 双校验；zip 直出）；v1.16.1 资产已重新打包上传（290MB，含脚本）；发布说明补重下提示
- **核查结论**：lightchaser.jpg 有 qrc 内嵌兜底（可排除）；analyze_video.py 是退役引擎遗物（排除）；python 依赖 cv2/numpy/rapidocr/onnxruntime 均在包

## 29f) 账号+反馈系统启动（476a57a，服务端先行）
- 拍板：强制登录+每月至少一次（30天token）；手机号短信验证码注册收姓名/单位；腾讯云开发；邀请码通道（离线机180天token）；云控制台导出即管理端
- 已入库 docs/cloudbase/：README（表结构/token设计/控制台部署6步）+ 四云函数（authRegister/authHeartbeat/inviteActivate/feedback），token=HMAC-SHA256，语法校验过
- **关键现实**：个人开发者拿不到短信签名企业资质→走 CloudBase Auth 内置短信通道（固定腾讯云签名）；HTTP 端点 VERIFY_SMS_TODO 待联调实锤，不通则备选邮箱验证码
- 待办：①用户开通 CloudBase 环境+开短信登录+建三集合+部四函数+给环境ID；②客户端：登录闸/凭证存储/反馈窗/HTTPS(Schannel 零依赖)

# ============================================================================
# 归档自 HANDOVER（2026-09-03 §84 批，R2 限 5 批滚动）
# ============================================================================

## 79. 账号与反馈系统 v1：登录闸/30 天策略/意见反馈 + CloudBase 全链部署联调

- **服务端（CLI 部署）**：`tcb login`（设备码授权）→ env `lumenarc-prod-d6gcdfb6a8873d906`
  （上海，体验版）；探针函数实锤文档型云数据库可用并创建 users/invites/feedback 三集合；
  四函数 + HTTP 触发器全部署；E2E 真测：邀请码激活→心跳→反馈→复用拒绝 全通，
  authRegister 格式 token 过 heartbeat 验签并触发自动续签。
- **短信链路实锤**（从 @cloudbase/js-sdk 3.9.0 拦截真实请求挖出）：
  网关 `https://<env>.api.tcloudbasegateway.com/auth/v1/<ep>?client_id=<env>` + 头 `x-device-id`；
  流程 verification→verify→signin/signup 得 access_token → 交 authRegister 验（user/me）。
  cygwin 坑：tcb --path /x 需 MSYS_NO_PATHCONV=1；zip 含 node_modules 会坏服务端 unzip（在线装依赖）。
- **客户端**：`cloud_account.h/.cpp`（网关+函数双链封装，15s 超时，错误码归一化）、
  `credential_store.h/.cpp`（QSettings，启动判决 Pass/NeedLogin：token 过期或 ≥30 天未验证→重登）、
  `logindialog.h/.cpp`（手机号/邀请码双通道，60s 重发倒计时）、`feedbackdialog.h/.cpp`
  （诊断包仅版本/系统/崩溃标记，无案件数据）；main.cpp 闸（splash 后）+ 启动异步心跳；
  mainwindow 帮助菜单「意见反馈」。HTTPS 走 Qt Schannel（windeployqt 已带 tls/qschannelbackend.dll，
  已入 pack_release REQUIRED）。
- **雷区**：authRegister 初版 token 格式与 heartbeat/feedback 不一致（base64url vs hex sig、
  uid vs phone 字段）——已统一为 `base64url(JSON{phone,kind,exp,nonce}).HMAC-hex`；
  跨函数一致性以后改任一函数 token 逻辑时必须三函数一起对。
- 全量回归绿（mw97/ui103/libav26/case270/denoise/avgap）；提交推送至 github master。
- **待联调**：真机短信全流程（用户首次启动注册即实测）；副屏全屏多屏真机验收。

## 78. 热修：识图校时偏差永不生效（v1.12.6 引入的丢行回归）

**日期**：2026-08-24｜**版本**：1.15.0 → **1.15.1**｜**触发**：用户重建案件实测——
北京时间识图校时流程全绿（OCR/确认卡/留痕全正常）但主视口时间轴 13 分钟
偏移纹丝不动。

**根因**：`TimeSettingsDialog::onCalibPhotoFinished` 确认卡采用尾巴里
`const qint64 offset = confirmDlg.offsetMs();` 算完后**从未赋给
m_working.truthOffsetMs**（孤儿变量）——v1.12.6（986f8f8）确认卡改可编辑
重构时丢行，v1.12.5 直算是好的。后果：truthSet=true 而偏移恒 0，
beijingMsOf=wallMsOf+0，轴/快照/多机全口径"不生效"；.vla 留痕照存。

**排查法备注**：后端管道（probe_timestamps.py calibphoto）合成图实测正常 →
逐段静态验证显示链全对 → 最终靠"应用 lambda 与状态栏同体、用户流程正常"
反推 emit 载荷本身有问题，肉眼抓包成功。

**修复**：采用尾巴抽为公有 `adoptPhotoTruth()`（赋值+留痕+emit 单点），
onCalibPhotoFinished 调之；ui_chain +6 断言直驱锁死（offset 834000 管道/
留痕字段/人工修正注记/beijingMsOf 反映）。18 套全绿。

**补记 26（手册上云管线，方案 A 拍板）**：git 留真源（MANUAL.md），WPS 在线
文档做阅读/批注/分享端。新增 tools/manual2docx：复用工程零依赖 DocxWriter
（P-28 报告同款排版），行解析 md 子集（# 标题/表格/列表/引用），行内 **粗体**
/`代码` 标记剥除（DocxWriter 只支持整段加粗，v1 可接受）。产出「追光者 Lumen
Arc — 操作手册.docx」（284KB，30 表）已挂 Release v1.16.0 第二资产；PDF/DOCX
导出物入 .gitignore 不入库（可再生成）。WPS 导入即转在线智能文档。

**补记 28（v1.16.1，曲线图/语谱图滚轮操作归一化，用户拍板规格）**：
滚轮=X 轴缩放@鼠标位（两图原有）；Ctrl+滚轮=Y 轴缩放@鼠标位（曲线图新增：
亮度轴，首用自动关 Y 自动范围，右键菜单恢复；语谱图原有频率轴）；
Alt+滚轮=X 轴平移（两图新增，上滚向过去，每格 10% 可视宽度，夹取边界）。
曲线图 X rangeChanged 信号自动联动语谱，语谱 Alt 平移 emit 联动曲线。
顺手修手册旧错：语谱 Y 平移实为 Ctrl+左键拖拽（非中键）。19 套全绿。
播放音频降噪方案调研中（afftdn 风格流式谱减 vs RNNoise 对比，待拍板）。

**补记 29（v1.16.1，P-54b 播放音频降噪落地，用户拍板方案 A）**：
SpectralGateStream 流式谱门控（audio_denoise.cpp 同类）：跨块 OLA 状态+
afftdn 式自适应底噪（低于估计立即下跟/高于 0.004/帧慢上浮；运行最小值
→典型底噪 ×4 标定，实测不标定力度不足 ×0.65→标定后 ×0.20）。
**音画对齐论证**：输出样本 p 恒为输入 p 降噪版（流重索引），计算滞后
[1536,2048] 样本（实测 1856=38.7ms@48k）由 1s 设备缓冲吸收（sink
setBufferSize(1s)）→ 稳态零偏移，无需时钟补偿；内容锚点在首个喂入帧
relMs（写时锚会偏晚一窗）。引擎插入点：swr_convert 后、音量增益/变速
重采样前；scrub 旁路；seek/开关 reset。设置菜单开关（默认关，
QSettings playbackDenoise），强度与语谱滑杆共享；多机窗
applyPlaybackDenoise 透传全引擎。测试 8 项（离线 4+流式 4：块大小
不变性逐位一致/长度守恒/降噪有效/滞后有界）。19 套全绿。

**补记 29b（播放降噪强度改原子热更新）**：用户问"是否实时"→发现拖滑杆需点
应用才下发 + 引擎端改强度会重建处理器（断音）——改 setStrength 原子热更新
（下一帧生效，状态保留）；configure 只在采样率/声道变化时重建；滑杆
valueChanged 直推引擎（不用点应用）。「应用」钮只剩分析显示链路语义。

**补记 28b（滚轮归一化三 bug 修复，用户实测）**：①Ctrl+滚轮「轴动线不动」——
只跑了音频分析时曲线在右侧 dB 音量轴（m_axisYVolume），旧码只缩放左侧亮度轴
→ 改缩放所有可见 Y 轴（各自独立锚点/边界：亮度[-10,265]、dB[-100,10]）。
②Alt+滚轮双向都只向前——Windows Alt+滚轮常走横向滚动消息 angleDelta.y()==0
（delta=0 被判为"下滚"）→ 取非零分量兜底。③语谱图中键拖拽补平移时间轴
（m_panningX，与曲线图同语义，emit 联动）。

**补记 27（v1.16.1，P-54 音频降噪 libav 原生落地）**：降噪滑杆自 v1.5 默认
libav 引擎起即空操作（Python 谱减法随 P-25 退役）——本批实装：domain/
audio_denoise.cpp 谱门控（就地 PCM：STFT N=2048/hop=512 Hann COLA → 等距采样
2000 帧×频点 25 分位噪声谱 → 谱减增益夹 [0.02,1] → 快攻慢释+频率 3 点平滑 →
ISTFT OLA Σw² 归一，长度不变）；仅作用于分析显示链路（语谱/音量），播放音频与
原始数据不动。引擎 setAudioDenoiseStrength 接口（ianalysis 默认空操作）；
onAudioAnalysis 统一读取滑杆下发；应用钮去 strength>0 守卫（调回 0 重跑=复原）。
**坑**：avutil av_tx 逆变换 scale 参数被忽略（txprobe 实测 ifft 恒输出 N·x，
nullptr/1/N 一样）——归一化手动除 N。测试 lumenarc_denoise_test（合成白噪+440Hz：
底噪 ×0.335、纯音 RMS ×0.937、相关性 0.9964、0 强度旁路）。版本升 v1.16.1。

**补记 26（手册重构 B+C，用户拍板）**：MANUAL.md 全文重写为八章工作流制
（认识→快速上手→案件与前处理→校时体系→单路分析→多机→产出与移交→附录），
854→650 行；正文 34 处版本标签/内部语言（P-编号/拍板/日期）全清，演进史剥离至
新建 CHANGELOG.md（v1.7~v1.16 用户可感知变更）；新增目录+快速上手双场景；
快照三处散述合并、快捷键附录唯一真源、常见问题过时口径更新；PDF 重出 17 页
（原 22），Release 资产已换新。

**补记 25（v1.16.0 发布收尾，版本号+手册 PDF+GitHub Release）**：
①启动画面 splash 版本号曾硬编码 v1.2.0（滞后 14 版）→ APP_VERSION 宏；
关于框（曾 v1.13.3）/案件包导出说明同改宏——对外版本号单一真源 =
CMakeLists project(VERSION)。②MANUAL.md → PDF 走 tools/manual2pdf
（Qt QTextDocument markdown+QPdfWriter，零第三方依赖，表格 yes/22 页）；
文件名去版本号「追光者 Lumen Arc — 操作手册.pdf」（CMake POST_BUILD 源
+帮助菜单查找名同步改）；工具源码收 tools/manual2pdf（PDF 本身不入库，
可再生成）。③GitHub Release v1.16.0 已发（gh CLI）：290MB 便携包，
打包排除 cases/（5.9GB 真实案件数据红线）+测试程序+日志杂项。

**补记 24（v1.16.0 续，多机返修三点）**：①双播放钮用户实测"没看到"——
窄面板横排小字不显眼 → 改竖排整行+金色描边加粗高36。②主视窗暂停：开窗时的
一次暂停挡不住用户回主窗再播 → 新增 onAboutToPlay 回调，多机每次起播前
（onTogglePlay/ecSetPlayRange）拦停主视口。③多机窗 showMaximized 默认最大化
（案件/独立两入口）。18 套全绿。

**补记 23（v1.16.0，多机同步播放三优化）**：①时间轴整体下移根因=
游标时刻气泡画在 y=kTopMargin-20=-12 全被裁——kTopMargin 8→26 内容区整体
下移气泡可见；刻度标签横向夹取防首末半字裁切；底部留白 14。②对时完成标识：
syncLaneHasTruth()（cal.truthOffsetMs≠0，直接/间接都算；合并轨任一段有真即
有）→ 瓦片绿色「✓已对时」角标（camtilewidget setTruthBadge）+ 时间线行标签
下绿色「✓ 已对时·北京时间」（CamLane.truthSynced）。③同事件对时沙盒双播放
钮：ecSetPlayRange(lane,on) 收口（-1 全部/>=0 仅该路），③段内「▶ 播放选中
瓦片」「▶▶ 播放全部」，工具栏播放钮=全部语义。④MANUAL 多机节同步重写。
18 套全绿。

**补记 22（v1.16.0 热修，快照删除误判报告）**：removeCaseFile 用
data(kRoleIdx).toInt() 判别——快照条目从不设该 role，无效 QVariant toInt()=0
→ 误判「报告 #0」：无报告时弹「报告索引越界」（用户实测"索引失败"）；有报告
时会误删报告 #0 文件（确认框显示快照路径却删别的，数据丢失级）。修：QVariant
isValid() 判别。另：预览器去「适应窗口」按钮（打开即自适应）+快照右半不烧录
倍率注记（左半 OSD 已有）。

**补记 21（v1.16.0，快照分屏+软件内图片预览）**：用户拍板 A1+缩略图+通用组件。
①onSnapshotQuick 分屏：放大镜开着时视频区=左原生全分辨率原帧（标注+金框+
OSD）+右放大视图同高（标注经 scale∘translate 变换烧入放大坐标系，底部注记
倍率+时刻），画幅 2× 宽细节零损失；底部"放大镜视图"独立小节取消；放大镜关
着行为不变。②新通用组件 ImagePreviewDialog（滚轮缩放光标锚点/拖动平移/双击
复位/适应窗口/1:1/资源管理器；非模态+WA_DeleteOnClose，不持嵌套事件循环——
吸取导出弹窗教训）。③CaseDock：快照条目 56px 缩略图（QtConcurrent 异步+
150ms 合批刷新+缓存）+双击预览+右键预览。④版本升 v1.16.0（CMakeLists+标题
12 处+mac bundle）。18 套全绿。

**补记 20（v1.15.3 续，显著性闸门收紧 30→10 秒/天 + 图解四点再修）**：
【代码】用户拍板"10 秒内可以接受"——kMinSignificantRateDev 30.0→10.0/86400000
（time_calibration.h，sidecar 继承处自动跟随）；calibration_test
testMinThresholdBoundary 低侧用例 20→5 秒/天随迁；18 套全绿。
【图解】①第四章换算尺图重画：横纵轴同单位（秒），斜率=1 恰为 45° 虚线基准，
走快更陡/走慢更平三线扇形，起点 offset 标注；②上篇纯原理化——OCR/软件手段
全部移出（取样="用眼睛看用笔抄也可以"，野点="坏数据不能进计算"，对表="读出
照片两个时间相减"，接力删"建锚沿链检查"，重建="先粗后细"原理化）；③第九章补
"原理→软件手段"对应段（OCR 自动取样/确认卡人工可改+原文留档/锚点预览/秒级
预检自动引导）；④闸门①图文同步 10 秒/天（2 小时录像仅差 0.8 秒）。

**补记 19（v1.15.3 续，图解按用户三点意见重写）**：①全书改科普小文章口吻——
连贯行文、充分论述，弃要点罗列体；②软件做法全部后置——上篇纯讲校时原理
（不依赖软件），下篇才讲 LumenArc 实现；实战案例改 outsiders 视角（1/2/3 号机
外号+完整背景），不再抛案件编号；③图 5-2 用户看不懂且确有错（缺口跳变画成
时间倒流）——重画为"两条带子映射图"：真实时间带（缺口=带子上的洞）+ 文件
时间带 + 虚线连接对应关系，直观呈现"同一流内位置 10:00 真实时间 08:20→08:35
瞬移"。重写后 32KB / 8 幅 SVG / 13 节，结构校验通过。

**补记 18（v1.15.3 续，校时逻辑图解离线 HTML）**：用户拍板把校时探讨沉淀为
教学文档——docs/calibration_explainer/index.html（单文件离线包，零外部依赖，
9 幅内联 SVG + 11 章）：三把钟/换算尺/四点校时方法/抽帧压缩特例/晶振与 RC
专题（含"只会变慢"误解纠正+量级表）/三道可靠性闸门/生效面与红线/术语词典。
已含实战案例（增城 C01 慢13'54"、C03 rate 异常指向文件特性）。

**补记 17（v1.15.3 续，导出完成弹窗卡死：问题转档交强援）**：用户三次反馈
「导出完弹窗关不掉/软件卡死」——历次修复（重复 connect 堆叠→一次性、ActionRole
→AcceptRole→非模态 open()）均未见用户确认生效；关键疑点：open() 版 exe 是否
已被用户测到未确认（期间多次 LNK1104 占链）。现象从未本地复现（开发机无法
GUI 操作导出）。已建立完整问题档案 docs/INVESTIGATION_EXPORT_FROZEN_20260825.md
（症状/时序/已排查/未闭环疑点/代码锚点/接手步骤），供更强专家接手。

**补记 16（v1.15.3 续，导出完成弹窗带「打开所在文件夹」+ 连环弹窗卡死修）**：
①用户要完成弹窗里直接有打开所在文件夹——QMessageBox 实例化+ActionRole 按钮，
点击 explorer /select 定位产物（面板内 📂 按钮保留）。②「导出一次后卡住」真凶：
startSegmentExport 每次导出都 connect finished（UniqueConnection 对 lambda
无效，每次新地址）→ 槽堆叠，第 N 次完成弹 N 个模态窗堵死界面；改连接只在
exporter 创建时建一次，产物路径经 m_lastExportPath 传递。18 套全绿。

**补记 15（v1.15.3 续，放大镜导出改主界面同款左右 50% 并列）**：用户拍板——
带放大镜导出不再右下角小窗：画布左半边原图（源区域金色四角括号+倍率徽章，
样式复制 OverlayWidget::drawMagnifierIndicator，引擎层自绘不依赖 widget）、
右半边放大视图（源裁剪→旋转→等比填满，等大同高=主界面观感）；drawPipImage
保留供多机 laneZooms 用。18 套全绿。

**补记 14（v1.15.3 续，选段导出冻结根因修复——湛江遂溪 D15 实测）**：
产物画面静止（42.56s 全首帧）。排查链：CLI ffmpeg 抽帧全同误导两次（bash
select 转义坑抽到前 4 帧）→ engine_test 加软解抽帧钩子证明内嵌解码正常 →
引擎内 DIAG 打印揪出真凶：**DVR 流包时间戳从 start_time 起算（D15=62585s），
而 aMs/bMs 是流内毫秒（0 起）**——curPtsMs=62585001 恒压过 target，主循环
永不拉新帧 → 全产物首帧。修复：startMs=start_time/1000 归一 seek（seekUs=
startMs×1000+aMs×1000 绝对微秒）与帧位置（pktMs−startMs）；顺带 dec->
pkt_timebase=tb、seek 改 avformat_seek_file。验证：DIAG curPtsMs 1592841→
1597961 跟进、产物 250 帧 scene 变化 13 处、抽帧 MAE 5~9 动态。诊断钩子已
全部还原；18 套全绿。

**补记 13（v1.15.3 续，选段导出两修——湛江遂溪案实测）**：①产物不对：LAClip
42.56s≠选段 26.95s——根因 onExportClip 对同选段静默沿用上次变速计划（vla
speed_plan rates[1,0.25,1]，Q5 持久化的副作用）；改默认恒 planFromLabels
原速 1x，同选段时 setLastPlan 醒目黄条提示+「恢复上次变速」一键钮。②导出完
毕没提示：finished 只写面板/状态栏——补 QMessageBox 完成弹窗（含产物路径）。
18 套全绿。

**补记 12（v1.15.3 续，校时卡速率文案人话化）**：用户实测看不懂双锚点确认卡
——「基本准」与「每 1 天快 621 秒」同卡自相矛盾（根因：①叠加校准时旧校时也
是 crosscam，两锚同点必差 0，「基本准」无信息量；②「每 1 天快 X 秒」把画面
时间轴速率差说成钟走快，吓人且概念错）。修：①旧校时为 crosscam 时改述
「标记瞬间已与基准路对齐（锚点强制）」，不再编「基本准」；②速率行改为「画面
每走 100 秒，真实约走 X 秒（画面相对真实慢/快 X%），已按此修正——否则离标记
越远偏得越多（每 1 小时约偏 X 秒）」；③|rate-1|>0.2% 追加警告「速率偏差较大，
请确认不是变速/抽帧录制」。编译全过（用户测试期 exe 被占用，待重链）。

**补记 11（v1.15.3 续，报告读数准确性与 C03 链完整性）**：用户实测揪出
①报告把 OCR 原始识别值 12:25:42 当监控显示时间摆出，而照片实为 12:25:47
（确认卡人工修正，偏移 834000=13:54 与 47 吻合，与 42 差 5 秒自洽）→ truthSet
分支改为：北京读数−偏移反推修正后监控读数显示于「监控显示时间」列，校准结果
摆「监控「12:25:42」(OCR 留档) 经确认卡人工修正为 12:25:47 ↔ 北京时间
「12:39:41」」。②C03 证据已落盘（V02.vla 23:04 source=crosscamevent 锚点
「骑车白衣男子举手指斜上方」05:52:05 ±33ms）→ 新链桥接：锚点挂在
「M_C02 烟酒店」合并轨 id 上，把成员（P01/P02）锚点并入别名，接力链
C03→合并轨→C01→绝对锚 在报告完整展开；camText 剥「M_」前缀。18 套全绿。
report_test 52。

**补记 10（v1.15.3 续，时间校准加深：差值显式化 + 三路结论）**：用户两点
①直接对时差值没说清→（二）表新增「监控较北京时间」列；直接路「校准结果」摆
同框对：监控显示「X」↔ 北京时间「Y」→ 差 13 分 54 秒（flatTruthText 展平 OCR
原文供报告）。②间接路差值存盘缺口→TimeCalibration 增 calibNote（F3 只加不
改），onEcSave 把控制器算好的 m_ecCorrText 写入 .vla；报告读出作差值白话；
另有 crosscamOsdDeltaMs() 读产物 .lumencal.json 在首锚点算「源监控较北京差
值」（C02 实测 0.0s/+0.3s，<0.5s 显示「一致（±0.5 秒内）」）。③新增「（五）
校时结论」小节：每路一句差值+接力关系（C03 → C02 → C01 → 标准授时）+整链
容差。report_test 49→52（差值列/结论小节断言）。18 套全绿（编译已过；用户
测试期 exe 被占用，需关闭后重链）。

**补记 9（v1.15.3 续，报告「四、时间校准」改版为白话可读）**：用户要求
报告讲清校时逻辑与结果（以 C02↔C01 为例）。改版：
（一）校准方法——先白话解释 OSD/墙钟/北京时间关系，分直接对时（照片同框比
对）与间接对时（多机同事件逐帧对齐）两种方式；间接明说"读出的不是快慢秒数、
而是两路画面内容同步的证据"。
（二）校准结果表——由「编号|显示时间|方式|时间差|公式」改为「监控编号(C01
烟酒店东侧)|显示时间(取样)|校时方式|时间基准(标准授时/参考机位)|校准结果
白话」；不再出现 epoch 巨型偏移数；间接路时间差=「≈0（依基准）」。
（四）取证链——参考路用机位编号+名（camNoText）替代 V### 文件名 id；链尾加
结论「经 N 个特征事件锚点对齐，整链容差 ±X ms，墙钟=基准路(北京)口径」。
service 新增 camText()（fileId→C## 机位名）；ReportVideoRow 增 camNoText/
baseRefText/resultText/anchorCount；ReportChain 增 eventHops。report_test 49
绿（旧断言靠字段回退支撑），18 套全绿。

**补记 8（v1.15.3 续，截图追查两个事实）**：①「快 8 秒」是误读——实读增城案
案内 .vla（V001）北京时间对时留档：truthOffsetMs=+834000（慢 13 分 54 秒，
12:25:42 vs 12:39:41 人工修正）；镜像 OSD 反字看花。②顺藤摸到真 bug：
`buildMergedGroups` 合并轨 label 用归组键（groupId "G002"）而非显示名——
camNo 落地后显示名已是「C02 烟酒店」而合并轨仍叫 G002（用户截图参考路
实锤）。修 label=首成员 displayName；sync_test 重构夹具（displayName/groupKey
分离）+2 回归断言（195→197 口径，实测 195 断言 0 失败）。18 套全绿。

**补记 7（v1.15.3 续，机位独立编号 C 方案拍板）**：用户纠正"多文件组标注
V01+P02"的信息错位——机位应有**独立编号体系**：CaseCameraGroup 新增 camNo
（C01/C02 自动排序、高位水位不复用、改名不动；CaseMeta.nextCamSeq 入档+
载入高水位自愈）；G### 退居纯内部稳定键（点位/同轴引用零迁移）；
groupDisplayName=「C01 烟酒店」（无名组=「C01」，编号是机位永远存在的身份）；
点位图标注只显 C01；侧栏/案件树=编号+助记名；迁移：存量组按 createdMs+组 id
确定性回填（用户当前案件的组自动获得 C01…）。case_test 270（+camNo 断言）。
18 套全绿。

**补记 6（v1.15.3 续，点位图三拍板）**：①标注大小可调——SiteMapPoint 新增
labelScale（0.5~3.0，默认不落字段 F3），属性条「%字号」spin + Alt+滚轮快捷，
渲染字号=短边×0.028×倍率；②图上标注文字=机位编号串（memberIds 以 "+" 连，
如 V01+P02）而非助记名（侧栏仍示组名助记）；③机位编号改两位（V01/P01，
超 99 自动扩位；V###/P### 三处格式化点改宽度，高水位解析不受影响；旧案
三位 id 作为既有字符串继续合法）。case_test 行为断言随迁（V01/P01…），
sitemap +5（labelScale 夹取/回环/缺省不写字段）。18 套全绿。

**补记 5（v1.15.3 续，用户截图实锤两 bug）**：①「应用预览点完没反应」——
applyLaneCalibration 只改模型不挪画面，暂停中零视觉反馈；修：预览即
seekWall 到参考路当前墙钟，两路当场跳齐。②「慢 495740 小时」疯话——
plainClockDeltaText(refWall-targetStream) 把 epoch 偏移当钟差，目标路已有
校时时必现（P-73 原生口径 bug）；修：修正量=与目标路**旧校时**在标记瞬间的
真实墙钟差（无旧校时则如实说「按参考路对齐到墙钟 X」），<1s 说「原本就基本
准」；锚点列表行尾假 delta 删除（只留 准钟墙钟⇄本路画面 映射）；预览状态条/
确认卡统一走 m_ecCorrText。

**补记 4（v1.15.3 续，用户实测"退出多机后主页面没打通"）**：onCaseDataChanged
回调带视频路径——多机窗保存的同事件校时若正中主视口当前视频，主窗重读 .vla
校时同步 m_calibration+时间轴（peekCalibrationFromVla 轻量只解 META chunk）。
**关键防回写**：不修的话主窗旧内存校时会在下次自动存盘覆盖掉多机窗刚存的
新校时（数据丢失级）。两创建点（案件/独立模式）同接线。18 套全绿。

**补记 3（v1.15.3 续，用户拍板）**：同事件对时确认卡改大白话——旧「模式：
仿射/速率 0.99980/偏移 epoch 毫秒原值/置信 0.80/残差 ms」→ 新「X 将按 Y 对时
（间接）· 方式：整体平移（1 标记）或平移+快慢（N 标记，每天快/慢 X 秒已修）·
修正量：目标的钟慢 M 分 S 秒 · 标记对齐误差最大 X 秒 · 可信度如实降档」；
取证链小节 jargon 翻译（绝对校时锚→基准：已直接对时；容差 ms→对表误差秒；
累积容差→本次对时最大可能误差）。单锚点可用性向用户再明示（代码本就支持，
前次"需双锚"实为禁用态保存钮假象）。

**补记 2（v1.15.3 随批，用户实测）**：同事件对时「💾 保存校时」点了无反应
——根因是样式表绿底不随 setEnabled(false) 变灰，禁用态看着能点、点击零反馈。
三修：①保存钮常可点，未预览点击出①②③步骤指引（守卫不再静默），样式随
预览态灰/绿切换（updateEcSaveBtn 单点）；②保存成功强反馈——成功弹窗（目标路
+累积容差）+ 案件树 ⏰ 徽标即同步（updateCalibrationBadge）+ 主窗回调
onCaseDataChanged 刷新案件树；③两创建点接线。sync 193 绿，18 套全绿。

**补记（v1.15.2 随批）**：归组对话框 QDialogButtonBox 漏 addWidget——
按钮成自由子控件飘左上、裁成 90px 碎片（用户盲点"稍后自调"致 P001 未归组）。
已入布局+最小尺寸 140×36+说明行（"稍后自调"语义写明案件树补归路径）。

**⚠️ 用户侧影响**：v1.12.6~1.15.0 期间做过识图对时的视频，.vla 里存的是
truthSet=true+truthOffsetMs=0——**需对每个受影响视频重做一次第 2 步**
（图片/手动均可），无法用旧数据自动修复（偏移值从未落盘）。

## 77. 机位组（Camera Group）正式化：案件组织轴心重写（视频/前处理区分废除）

**日期**：2026-08-24｜**版本**：1.14.0 → **1.15.0**｜**性质**：数据模型级重构（拍板：
组概念建在案件系统最开始；视频来源=直接导入/前处理生成全部归组；只有同组
才能同轴播放；内测期迁移从简；案件树=B 组分层）

**数据模型**：`CaseCameraGroup{groupId=G### 稳定不复用, name 可改不作键,
memberIds, createdMs}` 入 case.json（F3 只加不改；load 白名单+高水位自愈）；
`CaseModel::findGroup/groupIdOf/groupDisplayName/migrateCameraGroups`——
迁移：未归组引用按 cameraLabel 聚组（同标签并既有同名组），无标签各自成组，
幂等；开案时自动迁移置 dirty。

**CaseManager API**：createGroup（重名拒绝）/assignToGroup（摘除旧组+源组掏空
即清+目标组豁免——曾踩"ungroupRef 无差别清空组误杀新建目标组"bug，
case_test 抓出）/renameGroup（键不动，cameraLabel 镜像同步全组成员——老读者
无缝跟随）；addVideo 入案即自成组；removeVideo/removePreprocessOutput 出案
出组+清空空组。

**同轴闸**：cam_timeline buildCamInventory 的 groupKey 键源从"标签巧合"换成
正式组 id，displayName=组名。

**CaseDock 树 B（组分层）**：fillCameraGroups——组节点（📷 组名·M 个文件，
粗体）→成员文件行（V###/P### 同待遇混排，指纹/校时徽标保留）；会话区产物
行移除（留 sidecar+指引行）；右键：文件行「移到机位组 ▸」（现有组+新建组）、
组节点「机位组改名/新建机位组」；旧「设置摄像头编号」入口连根移除。

**前处理登记归组对话框**（拍板：产物完毕必选）：preprocesswindow finalize 登记
成功后逐产物行「归入已有摄像头 ▾（通道名匹配预选）/创建新摄像头…（默认名
=通道名）」，稍后自调可跳过（案件树补）。

**点位图换键**：点位 laneRef=G###（稳定，改名零牵连——上轮 rename 迁移 hack
删除）；旧引用（文件 id/旧标签）载入自动升格；侧栏读正式组。

**测试**：case_test +19（建组/重名拒/移组镜像/空组清理/改名键不动/迁移聚组
幂等）→ **267 断言**；18 套全绿。

## 76. P-28 收尾：曲线光栅嵌入+哈希进度条 + P-74 点位图编辑器落地

**日期**：2026-08-23｜**版本**：1.14.0｜**性质**：功能批次（P-28 收尾 + P-74 落地）

**图表光栅嵌入**：`ReportService::renderChartImages`（GUI 线程离屏）——
TimelineModel::setSnapshot + ChartPanel::renderToImage(1600×420) 矢量重渲染
（§14 定论不走 grab）→ 案内 reports/assets/chart_<V###>.png → 报告五（三）
节逐路嵌入「XX 亮度变化曲线（横轴：北京时间）」。

**哈希进度条**：collect 新增 cb 重载（工作线程安全：仅文件 IO/QProcess，
QueuedConnection 回投进度）；mainwindow 终生成改 QtConcurrent::run +
QProgressDialog（可取消，取消即弃稿）+ QEventLoop 等待。

**P-74 点位图编辑器落地**（方案 docs/SITEMAP_EDITOR_DESIGN_CN.md，拍板：
不画比例尺/扇形扇面可调/一案一张）：
- `domain/site_map.h`：SiteMapData/Point（归一化坐标+朝向/张角/半径夹取
  10~180°/3~50%）+ sitemap.json 原子写持久化 + 孤儿点位（机位删了标红
  「已移除」不自动删，改名跟随最新标签）；
- `infrastructure/site_map_render`：编辑器画布与成品图**共用** drawPoints
  （所见即所得）+ renderFramed 标准图框出图（2480×1754 A4@150dpi，双图框+
  右下标题栏：案件编号/图名/制图/审核/日期/图号 SP-01）；
- `SiteMapEditorDialog`：工具行（导入底图[复制入案]/适应窗口/删除选中/
  出图保存）+ 机位侧栏（拖到画布布点，同机位再拖=挪位）+ 画布（空白拖
  平移/滚轮缩放/滚轮在选中扇面上转朝向/Shift+滚轮调张角）+ 属性条
  （朝向/张角/半径 spin 即时生效即存盘）；机位色=Theme::DataPalette 与
  多机时间线同口径；
- 案件菜单「编辑监控点位图(&M)」（有案使能）；出图存
  reports/assets/sitemap.png → 报告二（三）节自动嵌入（批次②已留取用）；
- `sitemap_test` 16 断言（JSON 回环/边界夹取/案内存取/图框成品像素探针：
  外框墨线/标题栏有墨/扇面橙色可见/无底图不崩）；**测试 17 → 18 套全绿**。

**P-28 报告模块全部拍板项施工完毕**，待用户整体验收（真机：自检→补录→
生成→Word 版式；点位图编辑器实操；拼接记录核对）。

**P-74 真机返修（同日用户测后四项）**：
1. **前处理产物 P### 同待遇**：机位侧栏/机位色/标签表/报告检材清单全部改走
   新 `CaseModel::allCaseRefs`（videos + 各会话 outputRefs）——此前只迭代
   meta.videos 致 P### 进不了布点清单；报告检材清单同步收录 P###；
2. **点位标签去框框**：字色=扇形/圆点机位色 + QPainterPath 白色描边晕
   （深浅底图均可读）；孤儿点位红色「已移除」；
3. **底图一次裁切+固定**：导入后弹裁切对话框（拖框选区/区外压暗/整张使用
   或确认裁切/取消放弃导入），确认后统一 PNG 存案；画布底图**固定适配不再
   可平移缩放**（空白点击=取消选中；未选中扇面滚轮无操作）；
4. **扇面操作提示加亮**：属性条提示改黄底描边粗体胶囊样式 + 选中点位时
   画布底部浮出半透明黑底金字横幅「滚轮=转朝向 Shift+滚轮=调张角」。

**P-74 真机返修第二轮（08-24，机位识别问题，截图拍板）**：
- 问题：侧栏机位编号（V001/P002…）认不出是哪个位置的监控；同物理机位
  的原件+多次拼接产物（V001/P001、P002/P005…）重复罗列；
- 方案：**物理机位分组**——同机位标签的 V###/P### 聚为一个布点单位
  （CamGroup：组键=机位标签，无标签退化 id），侧栏一组一行
  「明景（3 个文件）」，未自定义名的组附源文件名「P002 ← 明景拼接视频…」
  + tooltip 列成员全清单（id：文件名）；
- **机位改名**钮：改名对组内全部成员生效（setCameraLabel 逐成员），
  既有点位组键跟随迁移；重名组拒绝（防两组混淆）；
- 点位 laneRef 语义升为**组键**；旧版存文件 id 的点位载入时自动升格；
- 点位图层级正式定为「物理机位」而非「文件」（报告检材清单仍逐文件，
  语义各当其位）。

## 75. P-28 批次②：报告数据聚合 + 模板章节映射 + 生成入口（草稿版）

**日期**：2026-08-23｜**版本**：1.13.3 → **1.14.0**｜**性质**：功能批次（P-28 批次②）

**落地**：
- `domain/report_data.h`：ReportData 聚合模型（检材行/关键节点行/取证链/
  局限性注记 + 案件元数据 + extraFields 报告扩展位 reviewer/approver）——
  渲染器唯一输入（远期 HTML 渲染器缝在此）；
- `domain/report_fmt.h`（header-only）：fmtWall/fmtDuration/fmtSizeMB/
  fmtTimeDiff（慢/快 X）/calibWayText（Source→中文）；
- `app/report_service`：聚合器——ffprobe 物理属性（15s 超时）、MD5+SHA-256
  单遍补算（已有 SHA-256 复用）、校时表数据（wallMsOf 唯一换算入口 C3 +
  truth 北京时间偏移 + 取样点时间差 + 公式人读）、P-48 错读点→局限性、
  标签→关键节点（墙钟升序）、P-73 expandChain 取证链（absoluteLaneIds
  三参口径）、校准证据帧/快照/导出片段/点位图清单；
- `app/report_docx_builder`：章节映射——封面/静态目录/一基本情况/二检材
  （来源清单表+逐视频物理属性含双哈希/点位图位）/三依据方法（软件名带
  版本号）/四时间校准（结果表+证据帧嵌入≤8 张+取证链小节）/五分析过程
  （标签→火势时间表+逐节点骨架留白）/六分析意见（起火时间由最早节点预填
  「不晚于」+局限性自动行）/七附件（快照嵌入≤12 张+导出片段清单）/落款留白；
- 入口：案件菜单「生成分析报告(&G)」（有案才使能）→ 案内
  `reports/火灾视频分析报告_yyyyMMdd_HHmmss.docx` + 成功卡可打开文件夹；
- DocxWriter 增补 addCentered（封面）；APP_VERSION 由 CMake
  PROJECT_VERSION 全局注入（报告落款版本追溯）。

**测试**：新增 `report_test` 37 断言（reportfmt 口径 + 合成 ReportData→DOCX
解包：封面/目录/七章标题/哈希双列/校准表/取证链/节点/预填/局限性/落款留白/
版本号/点位图占位）；**测试套数 16 → 17**；期间修 fmtTimeDiff 方向词后
缺空格 bug（测试抓出）。

**批次③（同日续拍板落地）**：生成前两道闸门——
- **自检**：`domain/report_preflight.h` 纯函数出检查项（❌阻断：文件缺失/
  全部未校时/无检材；⚠️放行：未校时/无证据帧/审核批准人空/点位图未绘/
  无标签节点；ℹ️ 拼接记录计数）；
- **补录**：`ReportPreflightDialog`（自检树 + 审核/批准/送检人 + 逐路
  拍摄方向/提取方式/存储介质），`CaseManager::setReportExtra` 持久化到
  extraFields["report/…"]（F3 只加不改），下次生成记忆；
- **拼接记录列为证据**（拍板：前处理文件也是分析文件）：聚合
  preprocess/*/LumenArc_Evidence_*/report.csv（按输出文件分组，列：
  序号/源文件/时长/处理动作）+ operations.log 关键决策行（素材统计/转码
  原因等≤5 行）+ 产物案内编号匹配 → 报告新增「二（四）前处理拼接记录」
  小节 + 附件第 5 条引用原件路径；
- 自检对话框用 computeHashes=false 快开，终生成才补算哈希（等待光标）；
- report_test 37 → 49 断言（preflight 阻断/放行/补录列/拼接记录渲染）。

**遗留（后续批）**：图表/语谱图整段光栅嵌入、大文件哈希进度条、
P-74 点位图编辑器施工。

## 80. 账号系统 v1.1/v1.2：账号管理对话框 + updateProfile 云函数（93e6d91 / 8138ee0）

- 帮助菜单「退出登录」（93e6d91）→ v1.2 升级为「账号管理」对话框（8138ee0，
  src/accountdialog.h/.cpp）：当前账号展示手机号打码、修改姓名/单位需短信验证码
  二次验证、退出登录红按钮；邀请码账号修改区灰置；casedialogs 预填登录档案。
- 新云函数 **updateProfile**（token+access_token 双校验、invite_cannot_edit、
  phone_mismatch 防护）已部署+入库；cloud_account 重构出 smsAccessToken 复用层
  （signInWithSms/reauthPhone 共用，_vtoken 无论 ok 与否都回填）。
- CloudBase 坑在案：短信码互作废旧码+发码限流；tcb deploy 须 MSYS_NO_PATHCONV=1；
  云函数 zip 不含 node_modules；AUTH_SECRET 在 build_tmp/tcb_deploy/.auth_secret（不入库）。

## 81. 账号系统 v1.3→v1.4：署名=账号档案写死（d75293d→766ee80→57cb075）

- v1.3 SignatureStore 独立值方案（d75293d）**被用户否决**→git revert（766ee80）。
- **v1.4 拍板**：署名=账号档案姓名/单位，**写死**在案件录入/点位图/报告 docx，
  各处只读；唯一修改渠道=帮助→账号管理（短信验证码）。
- 落地：casedialogs 调查员/单位改只读绑定账号档案（新建存快照、编辑只读显示历史
  快照、悬停提示）；authRegister 响应带 name/org（老用户=服务端档案值）+
  authHeartbeat 附带档案字段（均已部署+入 docs/cloudbase/functions/）；main.cpp
  心跳块补齐本地缺失 name/org（自愈旧凭证）；report_service/sitemapeditordialog
  历史空值兜底当前账号档案。
- harness profile 链验证通过；资源点结论：SMS=50 点/条唯一大头，先不升级。

## 82. v1.16.2 前瞻：引擎三连修 + 拼接工况全景补全（24db060 / df56533）

- **①副屏全屏落错屏**（用户双屏 DISPLAY1 1920x1200 + 34G1Q 3440x1440@x=3840）：
  fsprobe 真机四策略对比，唯一正解=`winId()`+`windowHandle()->setScreen()`+
  **handle 级** setGeometry+showFullScreen（fullscreenvideowindow.cpp showOnScreen）。
  坑：QWidget::setGeometry 与 setScreen+move 均落主屏；探针须 qInstallMessageHandler
  写文件（无控制台）+quitOnLastWindowClosed(false)。
- **②LAMerged 卡顿+音谱错位**：音频包 PTS 极不规则（180/78/20ms 乱跳），空档补偿
  阈值播放 40ms/分析 20ms 每包误触发 → 双侧统一 **200ms**，分析侧新增对称重叠裁剪；
  实测 25s 实播零补偿日志（%TEMP%/lumenarc_audio.log audioDiag）。
- **③输出设备热跟随**：followDefaultAudioDevice() 每 16 包检测系统默认设备变化
  热切换续播（用户默认设备被副屏抢成 34G1Q NVIDIA HDMI 实锤）。
- **拼接工况 8 矩阵补全**：⑥变速≠1x 过拼接空档补偿（pad 量 skew/rate）；⑦异构段
  直拷（concat -c copy）双侧引擎 swr 输入侧随帧重建（reconfigSwrInput + 分析侧
  curInRate/Fmt/Mask 快照）。已知边界在案：>10s 空档截断、40-200ms 小空档不补、
  预览缓存 sws 不重建、纯视频段。
- 病灶文件（现回归素材）：cases/20260722-广州增城/preprocess/.../LAMerged_02-04-52
  _6m_03-39-11.mp4（91min、15fps、AAC 8kHz mono、PTS 抖动族）。
