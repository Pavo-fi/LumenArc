# LumenArc 前处理板块技术方案

> 多视频智能排序 / 无损拼接 / 非 MP4 格式转码
>
> 文档版本：v1.1（评审修订版，已落实评审意见 R-1 ~ R-13，见文末修订记录）
> 状态：评审通过（有条件），M0 进行中
> 适用范围：LumenArc v1.1 后续版本

---

## 目录

1. [背景与目标](#1-背景与目标)
2. [需求分析](#2-需求分析)
3. [总体架构](#3-总体架构)
4. [技术选型](#4-技术选型)
5. [模块详细设计](#5-模块详细设计)
6. [数据模型与接口契约](#6-数据模型与接口契约)
7. [流程设计与状态机](#7-流程设计与状态机)
8. [UI 设计](#8-ui-设计)
9. [证据完整性与合规](#9-证据完整性与合规)
10. [错误处理与边界情况](#10-错误处理与边界情况)
11. [性能设计](#11-性能设计)
12. [测试策略](#12-测试策略)
13. [打包与分发](#13-打包与分发)
14. [工作量与里程碑](#14-工作量与里程碑)
15. [风险与应对](#15-风险与应对)
16. [范围外与后续演进](#16-范围外与后续演进)

---

## 1. 背景与目标

### 1.1 背景

LumenArc（追光者）是面向火灾调查的视频分析取证工具，当前版本已具备成熟的分析能力（ROI 亮度量化、音频分析、语谱图、多视频列表、自研 FFmpeg 播放内核）。但在**分析前的素材准备环节**存在明显痛点：

- 火调现场素材通常为**多台监控摄像机、每台数十段片段**（DVR/NVR 分段录像、运动触发录像）；
- 调查员目前依赖手工整理：人工核对文件名 → 人工确认顺序 → 用第三方工具转码/拼接；
- 文件名可能被重命名、metadata 可能丢失，**画面中烧录的 OSD 时间戳**才是像素级真相，但目前没有工具化利用；
- 证据链要求"顺序可证明"，手工整理缺乏可留档的**硬证据**（首帧/尾帧画面）。

### 1.2 目标

在 LumenArc 内新增**前处理板块**，提供一条可审计的素材准备流水线：

```
添加素材 → 智能排序（OCR 硬证据） → 一致性校验 → 统一转码 → 无损拼接 → 输出 + 证据报告
```

三大核心功能：

| 功能 | 目标 |
|---|---|
| 多视频智能排序 | 以画面 OCR 时间戳为首要证据，文件名/metadata 为交叉验证，输出可人工确认、可留档的有序时间线 |
| 无损拼接 | 同参数片段流拷贝拼接（秒级、零画质损失）；参数不一致时自动路由转码后再拼 |
| 非 MP4 格式转码 | AVI/WMV/FLV/MKV/MOV/TS 等统一转码为 MP4/H.264/AAC，兼容分析引擎与播放内核 |

### 1.3 非目标（本版本范围外）

- 视频裁剪、重叠片段的精确剪切去重（v1 保留原样并报告，v2 提供裁剪）；
- 字幕提取/合成、音频处理（音频降噪已有独立功能）；
- GPU 硬件编码（v1 纯软件编码，v2 可选）；
- 多组并行批处理（v1 单组串行 + 文件级并行探测）；
- 云/服务端部署形态。

---

## 2. 需求分析

### 2.1 功能需求

#### FR1 素材添加与探测
- 支持多选文件、拖放添加（沿用现有拖放模式）；
- 每文件探测：容器格式、编码器、分辨率、帧率、像素格式、音轨数、start_time、首帧 PTS、时长、是否有 seek 索引、creation_time、首包是否关键帧；
- 支持"伪 MP4"（MPEG-PS/TS 改名、无索引、起始时间戳偏移）——与播放内核同源能力；
- 探测结果以文件级状态图标呈现（✓ / ⚠ / ✗ + 原因）。

#### FR2 智能排序
- **证据①（最高）**：首帧/尾帧画面 OCR 时间戳（硬证据，附截图）；
- **证据②**：文件名时间戳（正则解析，多模式优先级）；
- **证据③**：容器 creation_time / 文件 mtime（兜底）；
- 按摄像机通道分组（文件名通道号正则）；无通道信息时默认单组并提示；
- 组内排序 + **连续性校验**（相邻文件首尾时间衔接：重叠/缺口定量检测）；
- 证据矛盾时自动裁决（连续性校验误差最小化原则），仍存疑则标记并要求人工确认；
- 排序结果可拖拽微调（复用 VideoListPanel 交互），调整后实时重算连续性提示；
- 产出：有序列表 + 校验报告（重叠/缺口/冲突明细）。

#### FR3 无损拼接
- 同参数片段：concat demuxer + 流拷贝，零画质损失，秒级完成；
- 拼接前一致性校验（详见 5.4），不一致项分级：`OK / WARN / BLOCK`；
- `BLOCK` 项自动路由：先统一转码 → 再无损拼接；
- `WARN` 项（如色彩范围不一致）默认放行 + 报告说明；
- 支持重叠/缺口检测结果展示，v1 保留原时间戳拼接，`-avoid_negative_ts make_zero` 防负时间戳；
- 时间戳非单调（重叠导致）时提供"时间戳归一化"选项（流拷贝 + `-output_ts_offset` 逐段平移，不重编码）。

#### FR4 非 MP4 格式转码
- 输入：AVI/WMV/FLV/MKV/MOV/TS/MPEG-PS 等；
- 输出：MP4 / H.264 (8-bit, yuv420p) / AAC，`-movflags +faststart`；
- 音频已为 AAC 时 `-c:a copy` 直拷；无音轨时仅视频轨；
- 隔行源检测到 field_order 非 progressive 时默认 yadif 反交错（可配置）；
- 输出文件自动避让（时间戳目录），禁止覆盖既有文件；
- 提供进度（`-progress pipe:1`）、取消（终止子进程 + 清理半成品）、超时（每文件上限，默认 60 分钟可配）。

#### FR5 证据报告
- 每文件：首帧截图 + 尾帧截图（含 OCR 裁剪块）落盘；
- 排序证据报告导出 CSV（RFC4180 转义，符合规范 F6）+ HTML（内嵌缩略图）；
- 报告记录：原始 OCR 文本（逐字保留）、解析结果（标注"派生值"）、文件名、metadata、置信度、源文件 SHA-256、处理命令与退出码；
- **取证原则：原始观测值永不静默修正**——相机时钟偏差只报告不篡改。

### 2.2 非功能需求

| 编号 | 需求 | 指标 |
|---|---|---|
| NFR1 | 结果可信（规范 C3） | 时间值全程 qint64 毫秒；排序/校验/报告三处一致；导出精度显式声明 |
| NFR2 | 证据不丢（规范 C2） | 任何失败路径必须有可见错误 + 日志；OCR 失败可人工手输兜底 |
| NFR3 | 长时间稳定（规范 C5） | 子进程句柄、QImage、临时文件全部有界；任务取消必须释放全部资源 |
| NFR4 | 性能 | 探测 100 文件 ≤ 30s；OCR 100 文件总时长 ≤ 5 分钟（Python 内多进程并行，默认 4 workers，单文件均拥 ≤ 3s）；拼接 1 小时素材 ≤ 30s；转码 ≥ 2× 实时（1080p 软编） |
| NFR5 | 兼容性 | Windows 10 1809+ / macOS 11+；与现有播放内核、分析引擎输出互认 |
| NFR6 | 许可证合规 | 新增依赖全部 Apache-2.0 兼容（详见 §13.3） |
| NFR7 | 可维护（规范 R1-R10） | 分层依赖单向；新功能以引擎 + 任务框架注册；纯逻辑可 headless 单测 |

---

## 3. 总体架构

### 3.1 分层视图（遵循 DEVELOPMENT_STANDARDS §2.1）

```
┌─────────────────────────────────────────────────────────────┐
│ ui/           PreprocessPanel（QDockWidget 向导式面板）       │
├─────────────────────────────────────────────────────────────┤
│ app/          PreprocessingCoordinator（流程编排、状态机）     │
├─────────────────────────────────────────────────────────────┤
│ domain/       probe_result.h / sort_model.h /               │
│               preprocess_task.h（纯数据模型，无 Qt Widgets）  │
├─────────────────────────────────────────────────────────────┤
│ infrastructure/  MediaProbeEngine  TimestampOcrEngine       │
│                  ConcatEngine     TranscodeEngine            │
│                  （QProcess 封装 ffmpeg.exe / Python 脚本）   │
└─────────────────────────────────────────────────────────────┘
复用：findFfmpegPath()（python_analysis_engine.cpp 同款路径解析，infrastructure 内静态方法）
     trustedDurationFor()（**下移至 PythonAnalysisEngine 公有方法**，见 §3.4）
     VideoListPanel 拖拽排序交互模式
     analyze_video.py 的 Python 子进程调用链（新增 probe_timestamps.py 同构）
```

> 注 1：仓库当前无 `app/` 目录，本方案新增 `src/app/`，符合规范 2.1 的四层定义。新增代码全部遵守 R1（依赖只向下）、R4（ui/app 层不出现 FFmpeg/Python 字样）。
> 注 2：UI 层沿用仓库现状——面板类文件直接放 `src/` 根（同 mainwindow/videolistpanel），不新建 `ui/` 目录；上图为逻辑分层，非物理目录。

### 3.2 运行拓扑

```
PreprocessPanel (UI 线程)
      │ 信号/槽
      ▼
PreprocessingCoordinator (UI 线程，状态机 SSOT)
      │ 调用（异步，QueuedConnection 返回）
      ▼
MediaProbeEngine ──► ffmpeg 子进程（probe 用 avformat CLI 不可行时）——实际为 libavformat 进程内轻量探测
TimestampOcrEngine ─► python probe_timestamps.py（批量，模型只加载一次）──► ffmpeg 抽帧子进程
ConcatEngine ───────► ffmpeg.exe -f concat ...
TranscodeEngine ────► ffmpeg.exe -i ... -progress pipe:1
```

### 3.4 既有代码下沉改造（评审 R-1）

现状 `MainWindow::trustedDurationFor()`（mainwindow.cpp）是 UI 层私有方法，且内部 `qobject_cast<PythonAnalysisEngine*>` 向下转型（R2 反模式）。本方案实施时同步完成下沉：

```cpp
// infrastructure/python_analysis_engine.h 新增公有方法
qint64 trustedDurationFor(const QString &videoPath);   // 可信时长（ms），失败返回 0
```

- 实现体自 MainWindow 原样迁移（getVideoInfo → totalFrames/fps 换算），MainWindow 两处调用点（1412/1545 行附近）改道新接口；
- TimestampOcrEngine 与 MainWindow 共用同一下沉实现，杜绝 R1 反向依赖。

### 3.3 线程与并发模型

- **UI 线程**：Coordinator 状态机、面板交互；
- **探测并发**：MediaProbeEngine 内 QThreadPool（默认 4 线程，可配），每个文件一个探测任务；探测为纯 demux 无解码，线程安全（各自独立的 AVFormatContext）；
- **OCR 并发**：C++ 侧不并发多个 Python 进程（避免模型内存 ×N）；Python 脚本**内部多进程池**（`--workers N`，默认 4，模型每进程一份约 +150MB/进程，4 进程可接受），预处理（numpy）与推理（onnxruntime 内部多线程）在池内并行；
- **转码/拼接**：同一时刻至多 1 个 ffmpeg 子进程（磁盘 IO 密集，串行更稳）；进度经 `-progress pipe:1` 解析，UI 节流 200ms；
- **所有引擎回 UI 一律 `Qt::QueuedConnection`**（规范 Q1）。

---

## 4. 技术选型

### 4.1 转码/拼接引擎：FFmpeg（现状延续，零新依赖）

| 候选 | 结论 | 理由 |
|---|---|---|
| **FFmpeg CLI（已捆绑）+ libavformat（已链接）** | ✅ 采用 | 已在项目内；concat demuxer 是无损拼接事实标准；`-c copy` 转封装、libx264 转码全覆盖；LGPL shared 与 Apache-2.0 共存无合规负担 |
| HandBrake CLI | ❌ | 重编码导向，无损拼接需 hack；GPL 合规负担；体积 +100MB |
| Tdarr / Unmanic / FileFlows | ❌ | 服务端平台形态，不可嵌入桌面工具 |
| StaxRip / Nmkoder / VidCoder | ❌ | 独立 GUI，不可嵌入；GPL 不兼容 |
| jave2 / handbrake-js | ❌ | 语言栈不符（Java/Node） |
| 进程内自实现转码（libavcodec 编码器） | ⚠️ 部分 | 探测用 libavformat（进程内）；**转码/拼接走 CLI 子进程**——复用成熟滤镜链（yadif/drawtext/scale），开发量小一个数量级，且与现有分析引擎模式一致 |

**决策：探测 = 进程内 libavformat 轻量 API；转码/拼接 = ffmpeg.exe 子进程。**

### 4.2 OCR 引擎：RapidOCR（onnxruntime）

| 候选 | 精度 | 中文日期 | 接入成本 | 体积 | 许可证 | 结论 |
|---|---|---|---|---|---|---|
| **RapidOCR** | 高 | ✅ | 低（pip，复用现有 Python 栈） | ~15MB 模型 | Apache-2.0 | **采用** |
| Tesseract CLI | 中 | 中 | 中（QProcess exe） | 30-60MB | Apache-2.0 | 备选 |
| 纯 OpenCV 数字识别 | 高（仅固定 OSD） | ❌ | 低 | 0 | — | 特化备选 |
| PaddleOCR | 最高 | ✅ | 高（全家桶重） | 数百 MB | Apache-2.0 | 不采用 |

**决策依据**：
1. 项目已内置 Python + opencv + numpy 分析栈（`analyze_video.py`），RapidOCR 纯 pip 接入，模型随 Python 环境打包；
2. 国产监控 OSD 常见中文日期格式（`2024年07月01日 12:00:00`），RapidOCR 中英混排原生支持；
3. Apache-2.0 许可证与项目一致，无合规风险；
4. 每文件仅 6 次裁剪块 OCR（~1-3s），性能完全可接受。

---

## 5. 模块详细设计

### 5.1 MediaProbeEngine（视频探测）

**职责**：对每文件做轻量 demux 探测，产出 `ProbeResult`；不建解码器、不解码像素。

**关键实现**：

```cpp
// avformat_open_input 选项（限流防呆）
AVDictionary *opts = nullptr;
av_dict_set(&opts, "probesize", "5M", 0);        // 探测字节上限
av_dict_set(&opts, "analyzeduration", "5M", 0);  // 分析时长上限
// 格式探测依赖内容嗅探（av_probe_input_format2），不按扩展名强制格式
// → 伪 MP4（PS/TS 改名）与播放内核同源处理
```

探测项与来源：

| 字段 | 来源 | 说明 |
|---|---|---|
| container | `fmt->iformat->name` | mp4 / mpegts / matroska … |
| videoCodec / profile / level | `codecpar->codec_id / profile / level` | 拼接一致性校验输入 |
| width/height | `codecpar` | |
| pixFmt | `codecpar->format` | yuv420p / yuvj420p 差异是常见 WARN 源 |
| colorRange / colorSpace | `codecpar->color_range / color_space` | TV/PC 范围不一致 → WARN |
| fieldOrder | `codecpar->field_order` | 隔行检测（转码决策输入） |
| rotation | 流 display matrix side data | 旋转不一致 → WARN |
| fps | `avg_frame_rate` / `r_frame_rate` | 双源校验，异常标记 |
| startTimeMs | `stream->start_time`（AV_NOPTS_VALUE 处理） | TS/伪 MP4 非零常见 |
| firstFramePtsMs | 首包视频 PTS 相对换算（`PTS×tb − start_time`） | 复用内核 `ptsToRelMs` 同款换算 |
| firstPktKeyframe | `pkt->flags & AV_PKT_FLAG_KEY` | **拼接前置条件**（见 5.4） |
| durationMs | `fmt->duration` vs 流时长 vs 包计数 | 截断文件时长虚报 → 标"时长存疑" |
| indexed | `fmt->pb->seekable` + 探测结果 | 无索引容器影响拼接后 seek |
| audioStreams | 流枚举 | 编码/采样率/声道数 |
| creationTime | `fmt->metadata` / 流 metadata `creation_time` | ISO8601 解析 + 脏值过滤 |

**并行**：QThreadPool 每文件一任务；每任务独立 AVFormatContext，无共享状态；单文件探测 ≤ 几十 ms，100 文件 ≤ 30s（NFR4）。

### 5.2 TimestampOcrEngine（画面时间戳 OCR）

**职责**：对每文件提取首帧/尾帧候选帧 → 定位 OSD 区域 → 预处理 → OCR → 正则解析 → 多帧投票，产出 `OcrResult`（含证据截图路径与原始文本）。

#### 5.2.1 帧提取（ffmpeg 子进程，复用 findFfmpegPath 模式）

```
首帧候选：ffmpeg -ss 0   -i <file> -frames:v 1 -q:v 2 首帧_0s.jpg      ← 输入侧 seek（快）
          ffmpeg -ss 1   -i <file> -frames:v 1 -q:v 2 首帧_1s.jpg
          ffmpeg -ss 2   -i <file> -frames:v 1 -q:v 2 首帧_2s.jpg
          （0s/1s/2s 三帧防开机黑屏、OSD 延迟出现）
尾帧候选：-ss (可信时长-3s) / (可信时长-1s) 各 1 帧                      ← 可信时长来自 trustedDurationFor 同源逻辑
          + 最后一帧：-sseof -0.1（失败则跳过，截断文件正常降级）
```

- 可信时长获取：`PythonAnalysisEngine::trustedDurationFor()`（§3.4 下沉后的公有方法），OCR 引擎直接调用；Python 侧脚本也内置 ffprobe 兜底；
- **尾帧 seek 性能（评审 R-6）**：`-ss (可信时长-3s)` 输入侧 seek 对 TS/PS 按字节插值尚可，但 `-sseof` 在无索引文件上可能失败或触发近全量 demux；**无索引伪 MP4 的尾部取帧耗时纳入 M0 验证项**（2GB 级样本），不达标则降级策略为“仅首帧 OCR + 时长推算尾帧”并在报告中标注；
- 临时帧存放：任务专属临时目录，任务结束清理（NFR3）；
- 截图同时落盘到证据目录（§9），一份文件两用。

#### 5.2.2 OSD 区域定位（角部裁剪，不做全图 OCR）

监控 OSD 固定位置统计：左上 / 右上 / 左下 / 右下 / 顶部居中 / 底部居中。

```
对每候选帧取 6 个裁剪块：
  4 角：宽 30% × 高 12%
  顶/底居中：宽 30% × 高 12%
每块放大至宽度 ≥ 512px（1080p 源 ≈ 2×）
```

裁剪块独立 OCR，取置信度最高且正则命中的块作为该帧结果；6 块全失败 → 该帧失败。

> 工作量口径（评审 R-3）：每文件最坏情况为首 3 候选帧 + 尾 3 候选帧 × 每帧 6 块 = 36 次推理（命中即止，典型远少于此）；性能指标按 NFR4 总时长口径验收，不按“每文件仅 6 次”估算。

#### 5.2.3 预处理（压缩噪声素材的关键）

```
灰度化 → CLAHE 对比度增强 → 高斯模糊去噪 → OTSU 二值化 → 形态学开运算
```

预处理参数集中为常量表（可配置），用真实素材调参；预处理在 Python 侧用 OpenCV 完成（栈内已有）。

#### 5.2.4 OCR 与正则解析

```python
# probe_timestamps.py（与 analyze_video.py 同构，批量模式）
from rapidocr_onnxruntime import RapidOCR
engine = RapidOCR()          # 进程内单例，模型只加载一次
result, _ = engine(crop_img) # → [[box, text, score], ...]
```

解析正则（优先级列表，命中即止）：

```
P1 完整日期时间：(\d{4})[-/年.](\d{1,2})[-/月.](\d{1,2})[日号]?[\sT]+(\d{1,2}):(\d{2})(?::(\d{2}))?(?:[:.](\d{1,3}))?
P2 无年份（用文件名年份作上下文）：(\d{1,2})[-/月.](\d{1,2})[日号]?[\s]+(\d{1,2}):(\d{2}):(\d{2})
P3 纯时间（仅同组参考）：(\d{1,2}):(\d{2}):(\d{2})
```

- 数字混淆防护：OCR 文本块置信度 < 0.6 直接丢弃；正则锚定 + 多帧投票双保险；
- 时间值域校验（时 0-23、分秒 0-59、月 1-12、日 1-31、年份 2000~now+1d）。

#### 5.2.5 多帧投票与结果组装

```
首帧时间：3 个候选帧（0s/1s/2s）解析结果
  ≥2 帧一致        → 高置信（conf=0.95）
  仅 1 帧成功且 conf≥0.9 → 中置信（conf=0.8）
  全部失败          → 空（进入人工兜底）
尾帧时间：同理（-3s/-1s/末帧）
交叉验证：与文件名时间戳偏差 > 容差(默认 2min) → 降置信 + WARN
```

**产出字段**（`OcrResult`）：`wallStartMs / wallEndMs / rawStartText / rawEndText / conf / 截图路径 / 裁剪块路径 / 帧位置(流内相对 ms) / 源文件哈希`。

#### 5.2.6 人工兜底（证据链不依赖 OCR 精度）

任一文件 OCR 失败 → 面板显示首/尾帧截图，调查员**看图手输时间戳**（人眼 1 秒可完成）；手输值标记 `source=Manual`，与 OCR 值同等进入证据报告。

### 5.3 SmartSorter（智能排序，纯函数 domain 逻辑）

**设计原则**：`smartSort(const QVector<ProbeResult>&, const QVector<OcrResult>&) -> QVector<SortGroup>`，无 UI/无 IO 依赖，可 headless 单测。

#### 5.3.1 证据层级

```
证据① OcrResult（像素级真相，最高）    权重 1.0
证据② 文件名时间戳（正则，多模式）     权重 0.8
证据③ creation_time（ISO8601 解析+脏值过滤） 权重 0.5
证据④ 文件 mtime（兜底）              权重 0.2（强制人工确认标记）
```

文件名时间戳正则模式（优先级列表）：

```
M1  CH(\d+)[_-](\d{8})[_-](\d{6})         // CH01_20240701_120000
M2  (\d{8})[_-](\d{6})                    // 20240701-120000 / 20240701_120000
M3  (\d{14})                              // 20240701120000
M4  (\d{4})-(\d{2})-(\d{2})[ _](\d{2})-(\d{2})-(\d{2})
M5  (\d{8})[_-](\d{6})[_-](\d{3})         // 含毫秒
```

每个模式捕获组同时提供通道号（分组输入）。解析值域校验同 5.2.4。

#### 5.3.2 分组

- 通道号来自文件名模式捕获（`CH01` / `IPC_03`）或用户手动分组；
- **只有同组文件才进入同一条拼接序列**；不同组 UI 分区展示；
- 无通道号 → 默认单组 + 提示用户确认。

#### 5.3.3 组内排序与置信度裁决

```
对每文件取最终候选：权重最高且有值者优先；同权冲突取 conf 高者。
排序后对相邻对 (A, B) 做连续性校验：

  A.end   = A.start + A.durationMs（A.start 为 OCR 首帧时间，下同）
  B.start = B.start
  Δ = B.start − A.end

  |Δ| ≤ 容差(2s)      → OK（连续）
  Δ < 0               → OVERLAP（重叠 −Δ，报告）
  Δ > 容差             → GAP（缺口 Δ，报告）

矛盾裁决：证据①与②冲突时，分别用两套候选排序，计算 Σ|Δ|（连续性误差总和），
取误差小者；仍无法裁决（如都无 OCR）→ 组标记"存疑"，UI 强制人工确认后才能进入下一步。
```

#### 5.3.4 输出

- `SortGroup { channel, ordered[], warnings[], suspicious }`；
- warning 类型枚举：`Overlap / Gap / EvidenceConflict / LowConfidence / DurationDubious / ManualInput`；
- 面板表格 + 证据报告（§9）双输出。

### 5.4 ConcatEngine（无损拼接）

#### 5.4.1 主命令

```
ffmpeg -f concat -safe 0 -i list.txt \
       -c copy \
       -avoid_negative_ts make_zero \
       -fflags +genpts \
       -movflags +faststart \
       -y <输出.mp4>
```

- `list.txt`：每行 `file '<绝对路径>'`，路径统一正斜杠（Windows 反斜杠转义坑规避）；路径含单引号时按 concat demuxer 转义规则处理；`-safe 0` 允许绝对路径；
- `-avoid_negative_ts make_zero`：TS/伪 MP4 起始 PTS 偏移导致负时间戳的兜底；
- `-fflags +genpts`：缺失 PTS 流自动生成；
- `-movflags +faststart`：moov 前置，输出即刻可拖拽（播放内核 seek 友好）。

#### 5.4.2 拼接前一致性校验（ConcatPrecheck）

对同组有序文件两两取并集校验（数据来自 ProbeResult）：

| 校验项 | 不一致处理级别 | 说明 |
|---|---|---|
| 编码器 codec_id | BLOCK | 必须一致（或路由转码） |
| 分辨率 | BLOCK | |
| 像素格式 pix_fmt | BLOCK | yuvj420p vs yuv420p 视为不一致 |
| 帧率（容差 1‰） | BLOCK（大偏差）/ WARN（小偏差） | 输出容器帧率取首文件 |
| H.264 profile/level | WARN | 播放兼容性风险提示 |
| 色彩范围/空间 | WARN | TV/PC 跳变肉眼可见 |
| 旋转 metadata | WARN | |
| 音轨数 / 编码 / 采样率 / 声道 | BLOCK / WARN | 音轨数必须一致，参数不一致 WARN |
| 首包关键帧 | WARN | 段首非关键帧是**源文件自身问题**（该段开头解码不出画面），与拼接无关；报告文案不得暗示拼接导致花屏。且首包可能是 B 帧重排前的包，此项仅为提示性指标 |
| 目标容器兼容性（codec 白名单） | BLOCK→自动路由转码或输出 MKV | MP4 白名单：h264/hevc/mpeg4/mpeg2/vp8/vp9 + aac/mp3/ac3/eac3/opus/flac；**mjpeg 不在白名单**（ffmpeg 可写 MP4+mjpeg 但播放器兼容性差，一律路由转码）；白名单外自动转码 |
| 时长存疑（截断文件） | WARN | 拼接后报告实际时长 |

**BLOCK 路由规则**：任何 BLOCK 项存在 → 该组自动进入"先转码后拼接"路径（转码参数取组内统一值，见 5.5）；用户也可强制忽略 WARN 项继续（取证场景允许，但报告必须记录）。

#### 5.4.3 时间戳归一化（可选，流拷贝不重编码）

重叠（Δ<0）导致拼接后 PTS 非单调时提供选项：

```
对每段 i：ffmpeg -i seg_i -c copy -output_ts_offset <累计偏移_i> seg_i_norm.mp4
然后按 5.4.1 拼接归一化段
```

- 累计偏移 = Σ(前段实际时长)（由 OCR 首尾时间计算）；
- 纯流拷贝，无画质损失，代价为每段一次快速 remux；
- v1 默认关闭（保持原始时间戳语义），仅当检测到非单调 PTS 时提示开启。

#### 5.4.4 进度与取消

- 拼接为单文件输出，进度经 `-progress pipe:1` 的 `out_time_ms` / 总时长换算；**实现注意（评审 R-5）：`out_time_ms` 单位实际是微秒**（ffmpeg 经典命名陷阱），换算前必须 ×1000 比对，进度解析单元测试覆盖此断言；
- 取消：`QProcess::terminate()` → 3s 后 `kill()` → 删除半成品输出与 list.txt；
- 超时：默认每任务 30 分钟可配（规范 C4）。

### 5.5 TranscodeEngine（统一转码）

#### 5.5.1 默认参数预设（目标：分析引擎与播放内核最优路径）

```
ffmpeg -i <输入> \
       -map 0:v:0 -map 0:a? \
       -c:v libx264 -preset veryfast -crf 18 -pix_fmt yuv420p \
       -c:a aac -b:a 128k -ac 2 -ar 48000 \
       -movflags +faststart \
       -avoid_negative_ts make_zero \
       -progress pipe:1 -nostats \
       <输出.mp4>
```

设计说明：

| 参数 | 理由 |
|---|---|
| libx264 / veryfast / CRF 18 | 火调场景近视觉无损基线；veryfast 平衡吞吐（≥2× 实时 1080p）；CRF 18-23 可在高级选项调整 |
| yuv420p | 全平台解码兼容基线 |
| `-map 0:a?` | 无音轨素材不报错；音轨已为 AAC 且参数达标时 `-c:a copy` 直拷（探测结果驱动） |
| `-movflags +faststart` | moov 前置，输出即拖即播，播放内核 seek 友好 |
| `-avoid_negative_ts make_zero` | 伪 MP4/TS 负时间戳兜底 |
| 反交错 | 探测到 field_order≠progressive 时默认追加 `-vf yadif`（可配置关闭） |
| 旋转 | 沿用 ffmpeg `-autorotate` 默认（旋转 metadata 生效为正向画面） |

#### 5.5.2 多文件队列

- 同组串行转码（磁盘 IO 密集，串行吞吐更稳）；队列进度 = 已完文件数 + 当前文件百分比；
- 每文件独立临时输出 → 全部成功后原子改名进输出目录；任一失败保留已完成文件并报告（规范 C2 不静默）；
- 输出命名：`<原名>_lumen.mp4`，冲突自动加序号；输出目录 = 用户指定或素材目录下 `LumenArc_Transcode_<时间戳>/`。

#### 5.5.3 硬件编码（v2 预留）

接口预留 `encoder` 参数（libx264 / h264_nvenc / h264_qsv），v1 仅软件编码，参数面不做硬编分支，避免"看起来正常但质量不一致"的风险（规范"结果可信"优先）。

### 5.6 临时文件与资源管理

| 资源 | 管理策略 |
|---|---|
| 抽帧 JPG | 任务临时目录，任务结束（含取消）递归删除 |
| list.txt / 归一化中间段 | 同任务临时目录 |
| OCR 裁剪块 | 证据目录（需留档）或临时目录（仅诊断用，默认临时） |
| 子进程句柄 | QProcess 栈对象，finished 必接；取消走 terminate→kill→waitForFinished(3s) |
| 输出半成品 | 失败/取消时删除；`-y` 仅用于任务自身输出路径（防覆盖由命名避让保证） |

---

## 6. 数据模型与接口契约

### 6.1 domain（纯数据，无 Qt Widgets 依赖）

```cpp
// domain/probe_result.h
struct ProbeResult {
    QString filePath;
    QString container;        // mp4 / mpegts / matroska ...
    QString videoCodec;       // h264 / hevc / mpeg4 ...
    int     profile = 0, level = 0;
    int     width = 0, height = 0;
    double  fps = 0.0;
    QString pixFmt;
    QString colorRange, colorSpace;
    int     fieldOrder = 0;             // AVFieldOrder
    int     rotation = 0;
    qint64  startTimeMs = 0;            // 流起始（相对0基准）
    qint64  firstFramePtsMs = 0;        // 首视频包 PTS（相对毫秒）
    bool    firstPktKeyframe = false;
    qint64  durationMs = 0;             // 容器时长（存疑时 <0 标记）
    bool    durationDubious = false;
    bool    indexed = true;
    int     audioStreams = 0;
    QString audioCodec, audioSampleRate, audioChannels;
    QString creationTimeRaw;            // 原始字符串（取证留档）
    qint64  creationTimeMs = 0;         // 解析值（脏值=0）
    QString probeError;                 // 空=成功
};

// domain/ocr_result.h
struct OcrResult {
    QString filePath;
    qint64  wallStartMs = 0, wallEndMs = 0;   // 解析后的墙钟毫秒
    QString rawStartText, rawEndText;         // 原始 OCR 文本（逐字保留）
    double  conf = 0.0;
    enum Source { Ocr, Manual, None } source = None;
    QString firstFrameImg, lastFrameImg;      // 证据截图（相对证据目录）
    QString startCropImg, endCropImg;
    qint64  startFrameRelMs = 0, endFrameRelMs = 0;
    QString sha256;
};

// domain/sort_model.h
enum class SortWarningType { Overlap, Gap, EvidenceConflict,
                             LowConfidence, DurationDubious, ManualInput };
struct SortWarning { SortWarningType type; int indexA, indexB; qint64 deltaMs; QString detail; };
struct SortEntry {
    QString filePath; qint64 startMs; qint64 endMs;
    OcrResult::Source startSource; double conf;
    QString thumbnailFirst, thumbnailLast;
};
struct SortGroup {
    QString channel;                    // 通道/组名
    QVector<SortEntry> ordered;
    QVector<SortWarning> warnings;
    bool suspicious = false;            // 必须人工确认
};

// domain/preprocess_task.h
struct ConcatRequest {
    QStringList orderedFiles;
    QString outputPath;
    bool normalizeTimestamps = false;   // 时间戳归一化选项
    bool ignoreWarnings = false;
};
struct TranscodeRequest { QString input; QString output; int crf = 18; bool deinterlace = true; };
enum class TaskPhase { Idle, Probing, Ocr, Sorting, UserConfirm,
                       Precheck, Transcoding, Concat, Done, Failed, Cancelled };
```

### 6.2 infrastructure 引擎接口（QObject，异步，信号回传）

```cpp
class MediaProbeEngine : public QObject {
    Q_OBJECT
public:
    void probe(const QStringList &paths);                 // 并行（QThreadPool）
signals:
    void probeProgress(int done, int total);
    void probeFinished(const QVector<ProbeResult> &results);
    void probeFailed(const QString &file, const QString &error);
};

class TimestampOcrEngine : public QObject {
    Q_OBJECT
public:
    // 评审 R-2：python/ffmpeg 路径不出现在接口签名中（R4）——
    // 由引擎内部自解析（findFfmpegPath 同款 + 分析引擎现有 python 路径注入模式）
    void run(const QStringList &paths, const QString &workDir);
signals:
    void ocrProgress(int done, int total, const QString &currentFile);
    void ocrFinished(const QVector<OcrResult> &results);
    void ocrFailed(const QString &file, const QString &error);   // 可继续（人工兜底）
};

class ConcatEngine : public QObject {
    Q_OBJECT
public:
    void run(const ConcatRequest &req);
signals:
    void progress(int percent, const QString &file);
    void finished(const QString &outputPath);
    void failed(const QString &error);      // 结构化错误码见 §10
};

class TranscodeEngine : public QObject {
    Q_OBJECT
public:
    void run(const TranscodeRequest &req);
signals:
    void progress(int percent, const QString &file);
    void finished(const QString &outputPath);
    void failed(const QString &error);
};
```

### 6.3 app 协调层

```cpp
class PreprocessingCoordinator : public QObject {
    Q_OBJECT
    // SSOT：TaskPhase 状态机 + 各阶段数据（probeResults/ocrResults/groups/…）
    // 对外（UI）：begin(session) / confirmOrder() / startProcessing() / cancel()
    // 对内：编排 4 引擎，串接 探测→OCR→排序→确认→校验→(转码)→拼接→报告
public:
    void begin(const QStringList &files);
    void confirmOrder();                 // 人工确认后进入处理
    void startProcessing(const ProcessingOptions &opts);
    void cancel();
signals:
    void phaseChanged(TaskPhase phase);
    void progress(int percent, const QString &detail);
    void evidenceReady(const QVector<SortGroup> &groups);
    void finished(const PreprocessReport &report);
    void failed(const QString &error);
};
```

### 6.4 Python 侧契约（probe_timestamps.py）

```
用法：python probe_timestamps.py --ffmpeg-path <ffmpeg> --work-dir <dir> <file1> <file2> ...
输出（stdout JSON，与 analyze_video.py 同构）：
[{
  "file": "…", "ok": true,
  "first": {"relMs": 0, "text": "2024年07月01日 12:00:00", "conf": 0.96,
            "frameImg": "…", "cropImg": "…"},
  "last":  {"relMs": 2999990, "text": "…", "conf": 0.94, "frameImg": "…", "cropImg": "…"}
}]
错误：stderr 输出 ERROR:<file>:<reason>（C++ 侧按前缀解析，规范 C1 用类型不用文案）
```

批量模式：一次进程调用处理全部文件（模型单例加载一次 ≈1s，之后每文件 1-3s）。

---

## 7. 流程设计与状态机

### 7.1 主流水线

```
添加文件
   │
   ▼
[P1 Probing]   MediaProbeEngine 并行探测（进度条）
   │
   ▼
[P2 Ocr]       TimestampOcrEngine 批量 OCR（进度条；失败文件标黄可人工手输）
   │
   ▼
[P3 Sorting]   SmartSorter 纯函数排序 → 面板展示有序表 + 首尾帧缩略图 + 校验警告
   │
   ▼
[P4 UserConfirm] 调查员核对（拖拽微调 / 手输兜底 / 分组调整）→ 确认
   │
   ▼
[P5 Precheck]  ConcatEngine::precheck（一致性校验 → OK / WARN / BLOCK 分级清单）
   │                     │
   │ BLOCK 或用户选转码   │ 全部 OK / 用户忽略 WARN
   ▼                     ▼
[P6 Transcoding]     [P7 Concat]  ──► [P8 Done]
   │                                     │
   └──────► 完成后回到 P7                 ▼
                                    证据报告导出 + 输出目录打开
```

### 7.2 状态机（Coordinator 持有，SSOT）

```
Idle ──begin()──► Probing ──► Ocr ──► Sorting ──► UserConfirm
UserConfirm ──confirmOrder()──► Precheck ──► Transcoding ⇄ Concat ──► Done
任意异步态 ──cancel()──► Cancelled（资源清理后）──► Idle
任意异步态 ──引擎失败──► Failed（结构化错误，可重试/可取消）──► Idle
```

- 状态迁移日志化（证据报告含时间线）；
- 取消语义：当前子进程 terminate→kill、临时目录清理、已完成证据保留；
- 阶段间数据不可变传递（probe → sorter 输入快照），重入安全。

---

## 8. UI 设计

### 8.1 布局

新增 `PreprocessPanel : QDockWidget`，与视频列表并列（左侧标签页 `视频列表 | 前处理`，或独立停靠，实现时按空间评估）。内部 QStackedWidget 四步：

```
┌─ 前处理 ──────────────────────────────────────────────┐
│ ① 素材  │ ② 排序与证据 │ ③ 处理选项 │ ④ 执行与结果     │ ← 步骤条
├──────────────────────────────────────────────────────┤
│ ② 排序与证据（核心步骤）                                │
│ ┌─ 组: CH01 ───────────────────────────────────────┐ │
│ │ # │ 首帧缩略图 │ 尾帧缩略图 │ OCR首帧时间 │ 依据 │ 状态 │
│ │ 1 │ [img]     │ [img]     │ 12:00:01   │ OCR │  ✓  │
│ │ 2 │ [img]     │ [img]     │ 12:30:00   │ OCR │ ⚠重叠 │
│ └──────────────────────────────────────────────────┘ │
│ [添加] [智能排序] [手输时间戳] [分组调整]  [↑][↓] 拖拽排序  │
│ ⚠ 警告汇总：2 处重叠（共45s）、1 处缺口（23s）            │
│ 确认顺序并继续 →                                        │
└──────────────────────────────────────────────────────┘
```

### 8.2 交互要点

| 交互 | 实现 |
|---|---|
| 添加素材 | 按钮 + 拖放（复用现有 dropEvent 模式）；重复添加去重（路径规范化） |
| 智能排序 | 点击后跑 P1-P3，表格刷新，缩略图异步加载（QPixmap 懒加载，限制同时解码数） |
| 手输兜底 | 双击 OCR 失败行的时间单元格 → 弹出首/尾帧大图 + 时间输入框；标记 `Manual` |
| 拖拽微调 | 复用 VideoListPanel 的 QListWidget 拖拽实现模式，调整后重算连续性警告 |
| 一致性校验结果 | ③ 步骤以分级清单呈现（OK 绿 / WARN 黄 / BLOCK 红 + 处置建议按钮"统一转码"） |
| 输出设置 | 输出目录、CRF（高级）、反交错开关、时间戳归一化开关 |
| 执行 | ④ 步骤队列进度条 + 当前文件 + 详细日志（ffmpeg 命令、退出码、耗时）+ 完成打开目录 |
| i18n | 全部文案走 i18n.cpp 键值（zh/en），主题色用 theme.h 常量 |

---

## 9. 证据完整性与合规

### 9.1 证据目录结构

```
<输出目录>/LumenArc_Evidence_<yyyyMMdd_HHmmss>/
├── frames/           首帧/尾帧截图（每文件 2 张 + OCR 失败兜底的候选帧）
├── crops/            OCR 裁剪块（诊断/复核用）
├── report.csv        RFC4180 转义（规范 F6）
├── report.html       内嵌缩略图（base64）的可读报告
├── concat_list.txt   实际使用的拼接清单（含转义）
└── operations.log    命令、退出码、耗时、状态迁移时间线
```

### 9.2 报告字段（CSV 列）

```
序号, 文件路径, SHA-256, 组, 首帧截图, 尾帧截图,
首帧OCR原始文本, 首帧解析时间(派生), 首帧依据(OCR/Manual), OCR置信度,
尾帧OCR原始文本, 尾帧解析时间(派生), 尾帧依据, OCR置信度,
文件名时间戳(原始), 文件名解析时间(派生), creation_time(原始), 时长(容器), 时长(可信),
衔接警告(与前段重叠/缺口, 派生), 处理动作(转码/拼接/未处理), 输出文件
```

- **取证原则**：`原始` 列逐字保留观测值；`派生` 列显式标注；相机时钟偏差以"观测值 + 偏差说明"呈现，**绝不静默修正**；
- SHA-256：源文件哈希（QThreadPool 并行计算，可选开关；大文件耗时提示）；截图与报告自身哈希可复核；
- 源文件只读：处理全程不写源文件（规范 NFR2）。

### 9.3 审计链路

排序结论 = 证据①(截图+OCR文本) + 证据②(文件名原文) + 证据③(metadata 原文) + 人工确认记录（面板核对即确认，操作日志留痕）。

---

## 10. 错误处理与边界情况

### 10.1 错误码体系（规范 C1：类型化错误）

```cpp
enum class PreprocessError {
    FileUnreadable,      // 打不开/权限
    ProbeFailed,         // 探测失败（详情附文件）
    OcrEngineMissing,    // Python/RapidOCR 缺失 → 引导 setup
    OcrAllFailed,        // 全部帧 OCR 失败 → 人工兜底路径
    SortSuspicious,      // 排序存疑 → 强制人工确认
    PrecheckBlock,       // BLOCK 项存在 → 路由转码
    TranscodeFailed,     // 转码失败（附退出码）
    ConcatFailed,        // 拼接失败（附退出码）
    OutputConflict,      // 输出冲突（命名避让后仍冲突）
    Cancelled,           // 用户取消
    Timeout              // 子进程超时
};
```

### 10.2 边界情况清单

| 情况 | 处理 |
|---|---|
| 伪 MP4（PS/TS 改名、无索引、起始 PTS 偏移） | 内容嗅探探测；`-avoid_negative_ts` / `-fflags +genpts`；与播放内核同源验证 |
| creation_time 为 `2036-02-06…Z`（零值 bug） | 年份值域校验拒绝 → 降级下一证据 + WARN |
| creation_time 无时区标记 | 按本地时区解析 + 置信度降一级 + 报告注明 |
| 断电截断文件（时长虚报） | 包计数交叉验证 → `durationDubious` + WARN；尾帧提取用可信时长-3s |
| 首帧黑屏 / OSD 延迟出现 | 0s/1s/2s 三候选帧投票 |
| 首包非关键帧 | 拼接 WARN → 建议转码 |
| 组内单文件 | 跳过排序逻辑，直接可拼接 |
| 文件名无时间模式 | 正则全失败 → mtime 兜底 + 强制人工确认 |
| 含毫秒时间戳 | 解析保留毫秒，排序按完整精度 |
| 重叠/缺口 | 定量报告；v1 保留原样拼接（可选时间戳归一化） |
| 无音轨素材 | `-map 0:a?` 不报错；报告注明 |
| 转码中途失败 | 已完成文件保留 + 失败文件报告；不产生半成品 |
| OCR 依赖缺失（Python/rapidocr） | 引擎初始化检测 → 引导安装（setup 脚本更新） |
| 超长素材（>24h 拼接输出） | 进度按 out_time_ms 累计；MP4 64 位大小支持；提示快进可用性 |
| 磁盘空间不足 | 转码前预估输出大小（码率×时长），不足则前置报错（不等到写满） |
| 输出路径含空格/引号 | QProcess 参数数组传参（非 shell 拼接）；list.txt 按 concat 转义规则 |

---

## 11. 性能设计

| 环节 | 设计 | 目标 |
|---|---|---|
| 探测 | QThreadPool 4 线程并行，probesize 限流 | 100 文件 ≤ 30s |
| OCR | Python 批量进程（每 worker 模型单例）；每文件首/尾候选帧 × 6 裁剪块命中即止；多进程池默认 4 workers（numpy 预处理 + onnxruntime 推理在池内并行） | 100 文件 ≤ 5 分钟 |
| 缩略图 | 懒加载 + 有界缓存（≤ 40 张 QPixmap，LRU），1080p 缩略图 ≤ 320px | 面板滚动流畅 |
| 拼接 | 流拷贝（零解码），IO 顺序读 | 1 小时素材 ≤ 30s |
| 转码 | veryfast 预设；`-threads 0` 自动；串行队列防磁盘颠簸 | ≥ 2× 实时（1080p） |
| 进度 | out_time_ms 解析，UI 200ms 节流 | 不刷爆 UI |
| 内存 | 帧图为临时文件不驻留；QImage 隐式共享；任务结束全量释放 | 峰值 < 500MB |
| 磁盘 | 证据目录与输出目录同卷（避免跨卷拷贝）；预估空间前置检查 | — |

---

## 12. 测试策略

### 12.1 合成素材生成（可复现的 ground truth）

用 ffmpeg lavfi 生成带**真实走秒 OSD** 的测试素材（drawtext 时间扩展）：

```
ffmpeg -f lavfi -i "testsrc2=size=1920x1080:rate=25" \
       -vf "drawtext=fontfile=C\:/Windows/Fonts/arial.ttf:text='%{pts\:hms}':x=50:y=50:fontsize=48:fontcolor=white" \
       -t 600 -c:v libx264 -pix_fmt yuv420p seg_%02d.mp4（或分段生成）
```

- 已知时间戳 → OCR 精度可自动断言（不再依赖人工核对）；
- 派生场景矩阵：正常段 / 重叠段 / 缺口段 / 截断段（dd 截尾）/ 伪 MP4（ts→mp4 改名）/ 非关键帧起始（`-bf 0` 变体 / seek 截取）/ 旋转 metadata / yuvj420p / 隔行源 / 无音轨 / 中文日期 OSD（`text='2024年07月01日 %{pts\:hms}'`，需中文 OSD 字体） / 时区标记缺失。

### 12.2 单元测试（headless，沿用 lumenarc_engine_test 模式）

新增 `lumenarc_preprocess_test`：

| 模块 | 用例 |
|---|---|
| SmartSorter | 构造 ProbeResult/OcrResult 输入，断言顺序、警告、矛盾裁决（证据①vs②）、分组、单文件组、空输入 |
| 文件名正则 | M1-M5 全模式正反例；值域校验；通道捕获 |
| OCR 文本解析 | P1-P3 正反例；数字混淆（8↔3）容错；毫秒截断 |
| 连续性校验 | 重叠/缺口/恰好连续/容差边界 |
| list.txt 转义 | 空格、单引号、中文路径、盘符 |
| 证据报告 | CSV 转义（F6 反例）、字段完整性、哈希一致性 |
| 错误码 | 各错误路径映射 |

### 12.3 集成测试（真实 ffmpeg 子进程）

- 拼接：同参数段 → 输出在播放内核打开、时长 = Σ段长 ±1s；混参数段 → BLOCK 路由转码后成功；
- 转码：avi/wmv/flv/mkv/mov/ts 各 ≥3 样本 → 输出可播、音画同步、时长 ±1%；
- 伪 MP4 全链路：探测 → OCR → 排序 → 拼接 → 内核播放验证；
- OCR：30 段真实素材（或合成素材）解析成功率 ≥ 90% 验收线；失败路径人工兜底可用性；
- 取消/超时/磁盘满：各错误路径资源释放断言（句柄数、临时目录清理）。

### 12.4 验收标准（评审用）

| 项 | 验收线 |
|---|---|
| OCR 时间戳解析成功率（首帧） | ≥ 90%（**30 段真实素材**样本集；合成素材不计入本项） |
| 排序正确率 | 100%（OCR 结果 + 人工确认环节保证；自动化断言限合成素材） |
| 无损拼接 | 输出与源段逐帧哈希一致（合成素材全帧比对）；时长 ±1s |
| 转码兼容 | 输出可在本程序播放内核打开；时长 ±1%；音画同步 |
| 证据报告 | CSV 通过 RFC4180 校验器；SHA-256 复核一致 |
| 性能 | §11 目标全部达成 |
| 资源 | 任务完成/取消后无句柄泄漏（连续 50 次任务断言） |

---

> 验收依据口径（评审 R-12）：合成素材矩阵全绿是**集成回归门槛**，不代表验收通过；OCR 与排序正确率的验收以 30 段真实素材为准，防止执行中以合成结果偷换验收依据。

### 12.5 尾帧 seek 性能验证（评审 R-6，并入 M0）

- 样本：2GB 级无索引 TS/PS 改名 .mp4（lavfi 长时合成 + dd 截断制造）；
- 断言：`-ss (可信时长-3s)` 尾部取帧 ≤ 5s/文件；`-sseof` 失败时降级路径可用；
- 不达标 → 启用降级策略（仅首帧 OCR + 时长推算尾帧），报告标注，并下调 NFR4 OCR 指标重审。

## 13. 打包与分发

### 13.1 体积增量

| 项 | 增量 |
|---|---|
| rapidocr_onnxruntime（含 det+cls+rec 模型） | 实测 13.2MB |
| onnxruntime | 实测 42.5MB |
| 传递依赖（pyclipper / shapely / six 等） | ~5MB |
| probe_timestamps.py | <10KB |
| 无新增原生 DLL | 0 |
| **合计（评审 R-4 修正，原估 +25MB 低估 3~5 倍）** | **约 +56MB，以 M0 打包实测为准** |

### 13.2 构建集成（CMakeLists.txt）

```cmake
# 新增源文件
src/app/preprocessing_coordinator.cpp
src/infrastructure/media_probe_engine.cpp
src/infrastructure/timestamp_ocr_engine.cpp
src/infrastructure/concat_engine.cpp
src/infrastructure/transcode_engine.cpp
src/preprocesspanel.cpp
src/domain/probe_result.h（header-only）
# 新增测试
add_executable(lumenarc_preprocess_test tests/preprocess_test_main.cpp ...)
# POST_BUILD 拷贝
probe_timestamps.py → 产物目录（与 analyze_video.py 同模式）
```

### 13.3 依赖与许可证

| 依赖 | 许可证 | 兼容性 |
|---|---|---|
| FFmpeg（已捆绑，LGPL shared） | LGPL 2.1+ | ✅ 现状延续 |
| RapidOCR | Apache-2.0 | ✅ |
| onnxruntime | MIT | ✅ |
| OpenCV / numpy（已有） | Apache-2.0 / BSD | ✅ |

`THIRD_PARTY_LICENSES` 补充 RapidOCR/onnxruntime 条目。**依赖安装三处同步（评审 R-9，缺一不可）**：

1. `setup_python_deps.bat`（开发版）增加 `rapidocr_onnxruntime`；
2. `.github/workflows/build-win64.yml` 的 bundled Python pip install 段增加 `rapidocr_onnxruntime`；
3. `.github/workflows/build-macos.yml` 的 `pip install numpy opencv-python-headless` 行同步增加。

**顺手清债（评审 R-10）**：`setup_deps.py` 全文仍为已淘汰的 VLC SDK 下载脚本（死代码，R10 零容忍），本方案实施时删除。

### 13.4 文档

- MANUAL.md / 操作手册 PDF：新增"前处理"章节（含证据报告解读）；
- README 功能表更新；DEVELOPMENT_STANDARDS 无冲突（本方案全程对齐）。

---

## 14. 工作量与里程碑

| 里程碑 | 内容 | 工作量 | 出口条件 |
|---|---|---|---|
| M0 技术验证 | OCR 原型：真实素材 20-30 段跑 RapidOCR，统计解析成功率；尾帧 seek 性能验证（§12.5）；打包体积实测 | 1 人日 | **成功率 ≥90% 决策门**；<90% 则评估兜底强度或换引擎 |
| M1 探测+OCR | MediaProbeEngine + TimestampOcrEngine + probe_timestamps.py + trustedDurationFor 下沉（§3.4）+ 单测 | 4 人日 | 合成素材全矩阵通过；真实素材成功率达标 |
| M2 排序逻辑 | SmartSorter + 正则/裁决/校验 + 单测 | 1.5 人日 | 全部单测绿 |
| M3 拼接+转码 | ConcatEngine + TranscodeEngine + 一致性校验 + 集成测试 | 2.5 人日 | 拼接/转码验收线达标 |
| M4 UI+协调 | PreprocessPanel + Coordinator 状态机 + i18n + 主题 | 4 人日 | 全流程手测通过 |
| M5 证据与打包 | 证据报告、哈希、CMake、CI workflow 与 setup 脚本三处同步、文档、QA | 2 人日 | §12.4 验收全绿 |
| **合计** | | **15 人日（约 3-4 人周，评审 R-13 修正，原估 12 人日偏乐观）** | |

工作量说明：M1 增加 1 人日（OCR 预处理参数对真实素材调参是开放迭代 + trustedDurationFor 下沉）；M4 增加 1 人日（四步向导 + 缩略图懒加载 + 拖拽微调 + 手输兜底交互复杂度相当于半个 VideoListPanel）；M5 增加 0.5 人日（CI 三处同步 + 打包实测）。

建议：M0 先行并设**评审决策门**（材料为真实素材 OCR 报告），通过后再投入 M1-M5。

---

## 15. 风险与应对

| # | 风险 | 等级 | 应对 |
|---|---|---|---|
| 1 | **OCR 在真实 OSD 多样性下精度不足**（异形字体、半透明、彩色、被画面遮挡） | 高 | M0 决策门提前暴露；预处理可调参数；人工看图手输兜底路径（证据链不依赖 OCR 精度）；缩略图人眼核对为最终裁决 |
| 2 | 伪 MP4 变体超出探测假设（含无索引文件尾帧 seek 性能，评审 R-6） | 中 | 内容嗅探 + 播放内核同源处理；测试矩阵含改名/截断/无索引；M0 实测尾部取帧耗时并准备降级策略；失败路径明确报错不静默 |
| 3 | 拼接后 PTS 非单调（重叠段）导致播放异常 | 中 | 连续性校验提前量化；时间戳归一化选项（流拷贝零损失）；报告记录 |
| 4 | 相机时钟错误（OCR 也错） | 中 | 连续性校验暴露断裂；证据报告记录原始值+偏差说明，绝不静默修正 |
| 5 | Python 打包体积/兼容性（onnxruntime 平台差异） | 低 | 已随包发布 Python 环境（现状延续）；macOS arm64 验证；setup 脚本兜底 |
| 6 | 硬编与软编质量不一致隐患 | 低 | v1 全软编；硬编接口预留 v2 单独评审 |
| 7 | 长任务（多小时转码）期间 UI 无响应 | 低 | 全异步 + 节流进度 + 取消路径；超时上限可配 |
| 8 | 磁盘空间不足导致任务中断 | 低 | 输出体积前置预估检查 |

---

## 16. 范围外与后续演进

| 演进项 | 说明 |
|---|---|
| v2 重叠裁剪 | 基于 OCR 精确时间戳的重叠段自动剪切（`-ss` 精确裁切，逐段流拷贝） |
| v2 硬件编码 | NVENC/QSV 可选项 + 参数等价性评审 |
| v2 多组并行 | 多通道素材并行处理流水线 |
| v2 音频增强 | 拼接时音频轨统一处理（响度归一化可选） |
| v2 智能命名 | 按"通道_日期_起止时间"自动重命名输出 |
| v3 PDF 报告 | 证据报告 PDF 版（含盖章位） |
| v3 云端比对 | 与云录像时间轴自动比对校验 |

---

*本文档与 DEVELOPMENT_STANDARDS.md 全部红线条目对齐（分层 R1-R10、正确性 C1-C6、格式 F1-F6、Qt 规范 Q1-Q3），评审时如发现冲突以标准为准。*

---

## 附：修订记录

### v1.1（评审修订版）

评审发现 3 项设计缺陷 + 10 项实现级修正，全部落实：

| # | 评审意见 | 落实位置 |
|---|---|---|
| R-1 | `trustedDurationFor` 为 UI 层私有方法且内部 qobject_cast（R1/R2 双重违规），infrastructure 不可复用 | §3.4 下沉改造；§5.2.1 引用点更新 |
| R-2 | `TimestampOcrEngine::run()` 签名暴露 python/ffmpeg 路径，违反 R4 | §6.2 签名修正，路径引擎内自解析 |
| R-3 | OCR 并发模型自相矛盾（§3.3 串行 vs NFR4 并行），指标无法达成 | §3.3 改为 Python 内多进程池；NFR4 改为总时长口径；§5.2.2 工作量口径修正 |
| R-4 | 体积增量 +25MB 低估 3~5 倍 | §13.1 实测数据修正为约 +56MB |
| R-5 | `-progress` 的 `out_time_ms` 单位是微秒（经典陷阱） | §5.4.4 实现注意 + 单测断言 |
| R-6 | 无索引伪 MP4 尾帧 seek 性能未验证 | §5.2.1 + §12.5 并入 M0；风险表 #2 更新 |
| R-7 | mjpeg 在 MP4 白名单不妥（播放器兼容性差） | §5.4.2 白名单修正 |
| R-8 | “首包关键帧”校验诊断价值有限，文案不得暗示拼接导致花屏 | §5.4.2 该项说明重写 |
| R-9 | 依赖安装遗漏 CI workflow 两处（只提了 setup 脚本） | §13.3 三处同步清单 |
| R-10 | `setup_deps.py` 为 VLC 死代码（R10 零容忍） | §13.3 顺手删除 |
| R-11 | 分层图 `ui/` 标注与现状（UI 文件在 src/ 根）不一致 | §3.1 注 2 说明逻辑分层 vs 物理目录 |
| R-12 | 合成素材全绿 ≠ 验收通过，需防偷换验收依据 | §12.4 验收口径声明 |
| R-13 | 工作量 12 人日偏乐观 | §14 修正为 15 人日 + 说明 |
