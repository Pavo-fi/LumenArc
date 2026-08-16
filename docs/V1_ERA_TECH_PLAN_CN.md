# LumenArc v1 时代技术方案（v1.2.0 ~ v1.9.0）

> 文档版本：v1.1（全部 20 项技术细节已拍板，见第十三章「决策记录」；报告格式改为 DOCX）
> 编制日期：2026-08-05（2026-08-05 晚修订 2：Q-7~Q-20 全部拍板）
> 基线代码：`LumenArc_v1.0 remake` @ v1.1.1（HANDOVER.md 第二十章之后）
> 排期依据：HANDOVER.md 第十章《升级计划表》（重要性 × 紧迫性三梯队）
> 本方案性质：v1 时代全部八个版本的总体技术设计。第一梯队（校时/案件/报告）为详细设计，
> 第二、三梯队为设计要点 + 前置条件，进入对应版本前按需补充分册。

---

## 目录

1. [总览](#1-总览)
2. [跨版本公共决策](#2-跨版本公共决策)
3. [v1.2.0 视频校时深化](#3-v120-视频校时深化)
4. [v1.3.0 案件模块](#4-v130-案件模块)
5. [v1.4.0 分析报告模块](#5-v140-分析报告模块)
6. [v1.5.0 P3 FFmpeg 分析引擎](#6-v150-p3-ffmpeg-分析引擎)
7. [v1.6.0 GPU 显示管线](#7-v160-gpu-显示管线)
8. [v1.7.0 前处理 v2](#8-v170-前处理-v2)
9. [v1.8.0 P1a/P1b 任务化与通道化](#9-v180-p1ap1b-任务化与通道化)
10. [v1.9.0 P2 MainWindow 拆分](#10-v190-p2-mainwindow-拆分)
11. [测试与验收总策略](#11-测试与验收总策略)
12. [风险登记册](#12-风险登记册)
13. [待确认技术细节清单](#13-待确认技术细节清单)

---

## 1. 总览

### 1.1 现状基线（与本方案相关的既有能力）

| 能力 | 现状 | 本方案中的作用 |
|---|---|---|
| 手动校时 | 工具栏「设置时间」→ QInputDialog 输入 HH:MM:SS → `offset = 输入 - 当前播放位置` → `ChartPanel::m_startTimeOfDayMs`（仅显示层加法偏移，无日期、无证据、无漂移概念） | v1.2.0 自动化的替代对象；手动路径保留为兜底 |
| 前处理 OCR | `probe_timestamps.py` + RapidOCR：首/尾帧投票、行合并、窄宽双裁剪链、PNG 证据帧、`--frames-only-json`（absStart 跳推理 88×） | v1.2.0 校时取帧/OCR 的直接复用底座（需扩 `--at-ms` 任意位置模式） |
| 流内绝对时间 | `MediaProbeEngine` 产出 `absStartEpochMs`（DHAV start_time，0.6 权重证据层） | 免 OCR 即时校时通道 |
| .vla 格式 | v7（META：`version=7` + 二进制块），逐视频保存 ROI/标签/辅助线/融合/time_offset | 每版本按 F2 规则演进（v8/v9） |
| 逐视频状态 | `VideoState`（含 timeOffsetMs）+ `VideoStateManager`，切走保存/切回恢复 | 校时数据、案件挂接的落点 |
| 分析引擎 | Python 子进程 `analyze_video.py`（OpenCV，MAX_ANALYSIS_FRAMES=5000 抽稀） | v1.5.0 被 libav 原生引擎替代，Python 仅剩 OCR 租户 |
| 播放内核 | FfmpegVideoEngine：D3D11VA/多线程软解自适应、scrub 追逐、滚动帧缓存、frameReady 有界队列 + ackFrame | v1.6.0 显示段优化的宿主 |
| 测试体系 | `lumenarc_engine_test`（场景化无头）、`lumenarc_preprocess_test`（168 断言）、`lumenarc_preprocess_integration`（29 断言）、`vla_load_test` | 各版本验收沿用同一模式扩展 |
| 架构分层 | ui / app（仅 PreprocessingCoordinator）/ domain / infrastructure；MainWindow ~2896 行上帝类 | 新功能一律落 domain/app，禁止再进 MainWindow（规范八） |

### 1.2 版本节奏与依赖图

```
v1.2.0 视频校时深化 ────────────────┐
（2 周，vla v8，聚焦单机位）         ├────► v1.4.0 分析报告模块（1.5-2 周）
v1.3.0 案件模块 ───────────────────┘      （DOCX，覆盖案件全部视频）
（2-3 周，case.json v1，含多机对齐视图/起始页）

v1.5.0 P3 FFmpeg 分析引擎（1-2 周；首批提交 = RoiModel 合并，行为冻结 Q-18）
v1.6.0 GPU 显示管线（1.5-2 周，独立于上列，可并行）
v1.7.0 前处理 v2（1-2 周，依赖 v1.5.0 的硬编等价性结论可后置）
v1.8.0 P1a/P1b 任务化+通道化（2 周，vla v9）
v1.9.0 P2 MainWindow 拆分（2 周，建议最后，收口 R2/R3/R5）
```

- 第一梯队串行：校时 → 案件 → 报告（报告消费前两者的产物）。
- 第二梯队与第一梯队**无代码级依赖**，如有人力可并行；串行执行时按 HANDOVER 顺序。
- 第三梯队放最后，但 v1.8.0 的 RoiModel 合并若在 v1.9.0 之前完成，可显著降低 P2 拆分时的状态副本清理难度。
- 全周期预估 **13~17 周**（单人串行）。

### 1.3 版本号与格式号对照（重要：HANDOVER 第十章的 "v7" 已过期）

| 版本 | .vla 格式 | case.json | 说明 |
|---|---|---|---|
| v1.1.1 | **v7**（已实现） | — | HANDOVER 第十章所写 "v1.8.0 落地 v7" 为笔误，v7 已在库 |
| v1.2.0 | **v8**（+time_calibration） | — | F2 三件套；严格升版（Q-19） |
| v1.2.1 | **v9**（+piecewise 分段表，v8 旧文件可读） | — | **2026-08-16 修正：v9 已被 v1.2.1 占用**（原表误标 v1.8.0） |
| v1.3.0 | v9 不变 | **v1** | .vla 保持独立可用，案件仅持引用 |
| v1.4.0 | v9 不变 | v1 不变 | 报告产物入案件 reports/ |
| v1.5.0 | v9 不变 | v1 不变 | libav 引擎，.vla 结构不变（Q-14 方案 A） |
| v1.7.0 | v9 不变 | v1 不变 | 前处理 v2（已实施 dfb4f7c） |
| v1.8.0 | **v10**（通道化字典） | v1 兼容（vla 版本号透传） | 迁移链 v7→v8→v9→v10 |

---

## 2. 跨版本公共决策

### 2.1 时间模型统一（v1.2.0 先行落地，全时代受益）

现状痛点（R5 违反）：时间偏移信息分散在 `ChartPanel::m_startTimeOfDayMs`（显示层）、`VideoState::timeOffsetMs`（状态层）、.vla `time_offset`（持久层）三处，且语义只是"日内秒偏移"，无日期、无来源、无证据。

统一为 domain 值类型（v1.2.0 新建，`domain/time_calibration.h`）。
**核心：仿射时间模型（Q-2 拍板"修正报时"的载体）**——修正不只是加一个偏移，
而是"偏移 + 走时速率"两个参数，全应用唯一一处换算：

```cpp
/// 校时数据：仿射模型 wallMs = offsetMs + rate × streamMs
struct TimeCalibration {
    enum class Source { None, Manual, Ocr, AbsStart, Inherited };
    Source  source = Source::None;
    qint64  offsetMs = 0;          ///< 流内 0 点对应的墙钟（epoch 毫秒，含日期）
    double  rate = 1.0;            ///< 墙钟走时速率（>1 = 相机钟偏快）
    bool    rateApplied = false;   ///< 漂移修正是否生效（false 时 rate 按 1.0 使用）
    double  conf = 0.0;            ///< 0~1，OCR 投票置信度
    bool    dateKnown = false;     ///< false=旧版日内秒偏移（v7 迁移值）

    /// 测点证据：三点拟合的原始观测（逐字保留，永不静默修正）
    struct Sample {
        qint64 streamMs = -1;      ///< 取样流内位置（showinfo 实测 relMs）
        qint64 wallMs = 0;         ///< 该点解析出的墙钟
        QString rawText;           ///< OCR 原文
        QString frameImgPath;      ///< 证据截图（相对路径）
        bool    used = true;       ///< 用户可在对话框剔除野点重新拟合
    };
    QVector<Sample> samples;
    double  sigmaRate = 0.0;       ///< 拟合速率标准误（显著性门控与报告用）
    qint64  calibratedAtMs = 0;

    /// 全应用唯一换算入口（C3：界面/图表/CSV/报告只准调这一个函数）
    qint64 wallMsOf(qint64 streamMs) const {
        const double r = rateApplied ? rate : 1.0;
        return offsetMs + static_cast<qint64>(std::llround(r * streamMs));
    }
};
```

- SSOT：`VideoState::timeOffsetMs` 被 `VideoState::calibration` 取代；ChartPanel 只接收渲染用的派生值，不持有语义。
- 显示规则：`dateKnown=true` 且跨天时轴标签带日期（`MM-dd HH:mm`），光标 tooltip 与 CSV Time 列输出完整 ISO 时间；`dateKnown=false` 保持现状 HH:MM:SS。
- C3 红线：所有墙钟换算集中在 `TimeCalibration::wallMsOf` 纯函数；仿射变换下图表轴刻度仍均匀，**渲染代码零改动**。
- rate 用 double 存储/传递；换算输出一律 qint64 毫秒（C6）。

### 2.2 .vla 演进策略

- v8（v1.2.0）：新增 `time_calibration` 对象；旧 `time_offset` 字段迁移为 `TimeCalibration{source=Manual, dateKnown=false, offsetMs=旧值}`；**本轮不做新旧版本兼容写出，严格升版**（Q-19 拍板：以最优效率做，兼容性下一轮排期再议）。**加载迁移（旧文件在新版可读）不受影响**——F2 迁移测试照常强制。
- v9（v1.8.0）：`luminances`/`audio` 硬编码成员 → `channels` 字典；迁移测试覆盖 v7→v8→v9 链。
- 每次演进严格执行 F2：版本号 +1、显式迁移函数、迁移测试（字段级断言）。

### 2.3 新代码落点纪律（规范八 + R1/R4）

| 版本 | domain 新增 | app 新增 | infrastructure 新增 | ui 新增 |
|---|---|---|---|---|
| v1.2.0 | time_calibration.h、calibration_inherit.h | CalibrationService | （复用 TimestampOcrEngine/MediaProbeEngine，扩脚本模式） | TimeSettingsDialog、视频列表校时徽标 |
| v1.3.0 | case_model.h、case_manifest.h | CaseManager、CasePackageService | （zip 走 bundled python） | CaseDock、案件管理对话框 |
| v1.4.0 | report_model.h | ReportGenerator | build_report.py（python-docx，走 bundled Python） | ReportDialog（预览+结论编辑） |
| v1.5.0 | RoiModel 合并（首批提交，行为冻结纯重构，Q-18） | — | FfmpegAnalysisEngine | — |
| v1.8.0 | task_registry.h、channel 化 AnalysisSnapshot、RoiModel | AnalysisTaskService | — | — |
| v1.9.0 | — | AnalysisController、VideoSessionManager、ProjectIO、UiState | — | MainWindow 瘦身 |

MainWindow 在整个 v1 时代**只减不增**：新功能挂接点（菜单/工具栏按钮）除外，业务逻辑一律进 app 层。

---

## 3. v1.2.0 视频校时深化

> HANDOVER 定位：前处理 OCR 结果反哺——相机时钟偏差检测、多机时间线对齐、图表"时间设置"自动校准替代手输。
> 排期：第一梯队首位（报告的时间线、多机对齐都依赖校时结果）。预估 1-2 周。

### 3.1 功能清单

| # | 功能 | 说明 |
|---|---|---|
| F1 | 单视频一键三点校时 | 主窗口「自动校时」：自动在**开头 / 当前位置 / 结尾**三处取帧 OCR → 最小二乘拟合仿射模型（§3.4）→ 对话框预填结果，点「采用」生效（Q-3） |
| F2 | 免 OCR 候选（absStart） | `MediaProbeEngine::absStartEpochMs > 0`（DHAV 等）：探测即得候选校时值，**仅预填进对话框，不自动生效**（Q-3 拍板） |
| F3 | 同机位多文件批量校时 | 视频列表「全部校时」：逐文件走 F2→F1（现阶段聚焦单机位，多文件通常是同机位的分段） |
| F4 | **时钟漂移修正报时**（Q-2 核心） | 三点拟合速率 + 显著性门控：显著漂移才应用 rate，否则按 1.0；测点/参数/误差全留档（§3.4） |
| ~~F5~~ | 多机时间线对齐视图 | **已移至 v1.3.0 案件模块**（Q-5 拍板，见 §4） |
| F6 | 校时证据留档 | 三测点 OCR 原文 + 截图 + 实测位置 + 拟合参数入 .vla v8；过渡期截图落 `<视频目录>/LumenArc_Calibration/`（Q-6：归档策略随 v1.3.0 定稿） |
| F7 | 设置时间对话框升级 | 替代 QInputDialog：测点表格（截图/原文/剔除勾选）、拟合结果（偏移+速率+误差）、「采用」按钮、日期+时间手输兜底、「不应用漂移修正」开关 |
| F8 | 拼接产物校时继承 | 前处理输出旁挂 sidecar 校时文件（含逐段 rate），主窗口打开时继承；**缺口警告必须进报告**（Q-4 拍板，§3.5/§5.2） |

### 3.2 取帧/OCR 通道（复用前处理底座 + 一处脚本扩展）

**脚本扩展（唯一的新协议能力）**：`probe_timestamps.py` 新增

```
--at-ms <毫秒>     # 在指定流内位置取候选帧（at-250/at/at+250 三帧投票，复用
                   # 现有 showinfo 实测 relMs 机制——输入侧 seek 落点偏移已被
                   # ss+pts_time 修正，证据位置永远为实测真值）
--single-file      # 单文件模式：跳过批量进程池，模型惰性加载
```

- C++ 侧 `TimestampOcrEngine::runAtPosition(path, streamMs)` 新重载；接口签名仍无 python/ffmpeg 路径（R4 延续）。
- OCR 解析链零改动复用：merge_ocr_lines、窄增强→窄二值→宽增强三级裁剪链、投票阈值（SINGLE_HIT_MIN_SCORE=0.85，≥2 帧一致 0.95）。
- 证据帧 PNG 落盘：无案件时落 `<视频目录>/LumenArc_Calibration/`；案件打开时落案件 evidence 区（v1.3.0 挂接）。

**F2 通道**：`MediaProbeEngine::probeOne` 已产出 `absStartEpochMs`（2000-01-01~当前+1天 值域过滤在库）。主窗口打开视频时轻量探测（仅流信息，几十 ms）→ 命中即建议校时。时区约定沿用第十五章结论：DVR 本地墙钟当 UTC 秒写入 → 取 UTC 分量按本地重解释。

### 3.3 校时应用与状态流

```
用户触发（F1/F2/F3）
   │
   ▼
CalibrationService::calibrate(videoPath, streamMs)   [app 层，UI 线程编排]
   │  ├─ probe.absStartEpochMs > 0 ──► TimeCalibration{AbsStart, conf=0.6档}
   │  └─ 否则 ocrEngine.runAtPosition ─► 投票命中 ─► TimeCalibration{Ocr, conf, 证据}
   │        └─ 全失败 ─► TimeSettingsDialog 手动兜底（source=Manual）
   ▼
VideoState.calibration = 结果（SSOT 更新）
   ├─► ChartPanel 派生显示偏移（当日零点换算）
   ├─► 视频列表校时徽标（✓OCR / ✓流内 / ✓手动 / —未校时）
   ├─► .vla 自动保存（沿用现有路径）
   └─► 状态栏反馈："已校时：2026-07-22 06:00:02 起（画面时间识别，置信 0.95）"
```

- **重新校时覆盖规则**：新校时替换旧值，但旧值进 operations 日志（取证可追溯）；不同来源冲突（如手动 06:00 vs OCR 06:05）弹确认，不静默覆盖（C2）。
- 播放引擎、A/B、标签、辅助线内部**全部保持流内毫秒语义不变**——校时只是显示/导出/报告层的换算，存量逻辑零回归风险。

### 3.4 时钟漂移修正报时（F4，Q-2 拍板的核心设计）

> 用户决策原文："要实现修正报时，也是为什么要深化校时功能的原因，
> 需要想一个高效优雅的解决办法。"

#### 设计思想：模型升级一处，全链路免费获益

把校时模型从"固定偏移"升级为**仿射模型**（offset + rate）：

```
真实时间 = offset + rate × 视频流内进度
时钟准的摄像机：rate = 1.000（行为与现状完全一致）
时钟偏快（每天快 2 分钟）：rate ≈ 1.00139，时间轴自动按真实节奏拉伸
```

- **唯一换算**：界面/图表/CSV/报告全部调 `wallMsOf()`——"三者永远一致"在结构上被保证；
- **渲染零改动**：仿射变换下轴刻度仍均匀，ChartPanel 绘制代码不动；
- **A/B/标签/光标/逐帧**：内部全部保持流内毫秒语义不变，仅显示层换算——存量逻辑零回归。

#### 测量：一次点击，三点采样（用户零额外操作）

点一次「自动校时」，自动在 **开头 / 当前位置 / 结尾** 三处取帧识别（3 次 OCR，秒级）：

1. 三点间隔拉满 = 速率测量最准（速率误差 ≈ 2×OCR误差 ÷ 间隔；47min 视频 ±1s 误差 → ±60s/天）；
2. 尾部取帧复用前处理既有经验（`-sseof` 降级、showinfo 实测 relMs）；
3. 任一点失败则退化：两点 → 精确线；单点 → rate 固定 1.0（现状语义）。

#### 拟合与显著性门控（防 OCR 误差毁掉修正）

```
三点最小二乘 → offset、rate、标准误 σ_rate
应用条件：|rate − 1| > max(3σ_rate, 30秒/天)   → rateApplied = true（显著漂移才修正）
否则：rate 按 1.0 使用，offset 取拟合值，漂移值仅作信息展示
残差过大（任一点 >3s）：警告"测点异常"，对话框可取消勾选野点重新拟合
```

- 手动校时 = 单测点（rate=1.0）；用户可在对话框强制「不应用漂移修正」（override）；
- **取证留档**：三测点截图/OCR 原文/拟合参数/标准误全部入 .vla v8 与报告；原始观测永不改，修正参数可审计可关闭。

#### 效果示例（D17，47 分钟）

开头识别 06:00:02、结尾识别 06:47:15，实际走 46 分 58 秒 → 钟每天偏快约 31 秒。
不修正：中段报时误差 ±15 秒；修正后：±1 秒级。

### 3.5 拼接产物校时继承（F8，sidecar 方案）

前处理 Coordinator `finalize()` 处（HANDOVER 13.7 已留挂点）在写出证据报告的同时，对**每个拼接/转码输出**写旁挂文件：

```json
// <输出文件名>.lumencal.json（与输出同目录）
{ "version": 1,
  "segments": [
    {"streamStartMs": 0,      "streamEndMs": 133000, "wallStartMs": 1784700002000,
     "rate": 1.0000000, "source": "absStart"},
    {"streamStartMs": 133000, "streamEndMs": 185000, "wallStartMs": 1784700135000,
     "rate": 1.0000000, "source": "ocr"}
  ],
  "gaps": [{"afterStreamMs": 133000, "gapWallMs": 0}]
}
```

主窗口 `openVideoFile` 探测同目录 `.lumencal.json`：

- **段间连续（Σ|gap| ≤ 2s，Q-4 拍板容差）**：offset=首段 wallStart，rate=各段 rate 中位数
  （单机位同 DVR 场景段间 rate 一致），source=Inherited；
  状态栏提示"已继承前处理校时（N 段，最大缺口 X s）"。
- **段间有缺口/重叠超容差**：仍按首段线性校时 + **明确警告"拼接含缺口，首段后墙钟可能漂移"**
  （C3 不静默）；**该警告必须进分析报告"时间基准"节**（Q-4 拍板，§5.2）。
  分段映射表留作后续增强（涉及轴渲染分段标签）。
- sidecar 随输出文件一并复制即随案移交；案件打开时证据区登记。

### 3.6 （多机时间线对齐视图——已整体移至 v1.3.0 案件模块，Q-5 拍板，见 §4.4）

v1.2.0 校时聚焦单机位监控摄像头；多机对齐依赖案件作为多机证据的容器，故并入案件模块实施。

### 3.7 UI 改动面

| 位置 | 改动 |
|---|---|
| TimeSettingsDialog（新） | 替换 QInputDialog：流内位置、当前校时模型、**测点表格**（缩略图+OCR原文+剔除勾选）、拟合结果（偏移+速率+误差范围+「采用」按钮）、日期+时间手输兜底、来源徽标、「不应用漂移修正」开关 |
| 工具栏/菜单 | 「自动校时」按钮（原「设置时间」入口并入对话框）；视频列表右键「校时此视频/全部校时」 |
| 视频列表 | 校时状态徽标列（复用现有状态图标模式） |
| ChartPanel | 跨天轴标签格式（`dateKnown` 驱动）；tooltip 完整时间；**无其他渲染改动** |
| CSV 导出 | Time 列：`dateKnown` 时输出 `yyyy-MM-dd HH:mm:ss.zzz`，否则维持 HH:MM:SS（文档同步注明语义差异） |

### 3.8 v1.2.0 工作量分解（约 2 周，Q-2 升级后修正）

| 任务 | 预估 |
|---|---|
| TimeCalibration 仿射模型 + 三点最小二乘拟合 + 显著性门控（domain 纯函数 + 单测） | 2 人日 |
| VideoState 迁移 + .vla v8（F2 三件套：版本+迁移+迁移测试） | 1.5 人日 |
| probe_timestamps.py 多位置取帧（`--at-ms` 列表/三点模式）+ 引擎重载 + 协议测试 | 1.5 人日 |
| CalibrationService：一键三点流程 + absStart 预填（Q-3）+ sidecar 继承（含 rate） | 2 人日 |
| TimeSettingsDialog：测点表格/拟合详情/「采用」/手动兜底/不应用修正开关 | 2 人日 |
| 图表轴/CSV/状态栏接统一换算 + 校时徽标 + 回归 + 文档（MANUAL/README/点检清单） | 1.5 人日 |

---

## 4. v1.3.0 案件模块

> HANDOVER 定位：案件=证据容器：视频、ROI/标签/辅助线、分析结果、前处理产物、证据报告统一入案；案件列表/归档/打包移交；.vla 关联案件存储。预估 2-3 周。

### 4.1 核心决策：目录制案件（Q-7 拍板）

```
<案件目录>/
├── case.json                 # 案件清单（格式 v1，魔数+版本+迁移，F1 合规）
├── videos/
│   ├── V001.vla              # 案件全部 vla 集中存此一个文件夹（Q-7 拍板）
│   └── V002.vla
├── evidence/                 # 校时证据帧、前处理证据目录（整体迁入）
├── preprocess/               # 前处理证据报告 CSV/HTML、operations.log 副本
├── reports/                  # v1.4.0 生成的分析报告 DOCX
├── snapshots/                # 截图叠加导出、钉图导出
└── manifest.json             # 完整性清单（逐文件相对路径+size+sha256，Q-9 必算）
```

**关键语义**：

- **视频默认引用不复制**：case.json 记录 `originalPath + size + mtime + sha256`；源文件只读原则延续（NFR2）。视频失踪时打开案件给"重新定位"对话框（按文件名+大小模糊匹配，人工确认）。
- **.vla 归案件所有**：案件模式下分析完成自动存 `videos/V###.vla`（不再写视频同目录）；**独立模式（无案件）行为完全不变**——这是兼容性底线。
- **移交打包 = 轻量包唯一形态（Q-8 拍板：不打视频）**：案件目录原样拷贝（引用清单+全部小文件），接收方重新定位视频；无完整包、无 zip 分支。
- **哈希必算（Q-9 拍板）**：入案登记 size+mtime 即时完成；SHA-256 **闲时后台队列**（QThreadPool 单线程，GUI 空闲时泵出，低 IO 优先级）；**每个文件算完即给提示**（列表徽标 ⏳→✓ + 状态栏）；另有「统一计算哈希」按钮手动触发全量；完成后回写 manifest；「校验案件完整性」可显式复核。**分析报告必须列出所有用到视频的哈希值**（生成报告时存在未算视频 → 提示并可立即补算）。

### 4.2 case.json 数据模型（domain/case_model.h）

```cpp
struct CaseVideoRef {
    QString id;                 // "V001" 递增，案件内稳定键
    QString originalPath;       // 原始绝对路径（留档）
    QString vlaRelPath;         // videos/V001.vla
    qint64  sizeBytes = 0;
    qint64  mtimeMs = 0;
    QString sha256;             // 空=未算（闲时队列/手动统一算，Q-9）
    TimeCalibration calibration;        // v1.2.0 数据随案走（与 vla 内副本同源）
    bool    calibrationFromVla = true;  // 真身以 vla 为准的派生标记
};
struct CasePreprocessRef {
    QString sessionDirRelPath;  // evidence 目录（迁入 evidence/）
    QString reportCsvRelPath;
    QStringList outputFiles;    // 拼接/转码输出（引用或包内）
};
struct CaseMeta {
    int     formatVersion = 1;
    QString caseNo;             // 案件编号（用户输入）
    QString title;
    QString investigator;       // 调查员
    QString unit;               // 单位
    QString description;
    qint64  createdMs = 0, modifiedMs = 0;
    QVector<CaseVideoRef> videos;
    QVector<CasePreprocessRef> preprocessSessions;
    QStringList reports;        // reports/ 相对路径
    QHash<QString,QString> extraFields;   // 扩展位（F3 只加不改）
};
```

### 4.3 CaseManager（app 层服务）

```
CaseManager : QObject
  ├─ createCase(dir, meta) / openCase(dir) / closeCase() / saveCase()
  ├─ addVideo(path) → 分配 V### id、登记引用、入闲时哈希队列
  ├─ computeAllHashes() → 手动统一算（Q-9 按钮）
  ├─ importPreprocessSession(evidenceDir, reportCsv, outputs)
  ├─ verifyIntegrity() → 逐文件 size/hash 复核报告
  ├─ exportPackage(targetDir) → 轻量包拷贝（不含视频，Q-8）+ README.txt 移交说明
  └─ recentCases()（QSettings 存最近 10 条）
信号：caseOpened/caseClosed/caseDirtyChanged/integrityReportReady/
      hashProgress(videoId, done, total)（Q-9 逐文件完成提示）
```

- **SSOT**：打开的案件由 CaseManager 持有；MainWindow/PreprocessWindow 经接口读写，禁止各自解析 case.json（R5/R6）。
- **自动保存**：案件 dirty 时切换视频/关闭程序提示保存（沿用现有"分析结果未保存"提示模式）；.vla 写入沿用现有 QtConcurrent 异步 + 完成回执。
- **并发安全**：同一时刻只开一个案件；二次打开同目录检测 `case.json.lock` 存在性提示（防双实例写冲突，简单文件锁即可）。

### 4.4 UI 改动面

| 位置 | 改动 |
|---|---|
| 菜单「案件」 | 新建/打开/最近案件/保存/另存/关闭/打包移交/校验完整性 |
| 启动与空态 | **起始页必做（Q-10 拍板）**：无视频时主区显示最近案件列表 + 新建/打开按钮 |
| CaseDock（新，**替代视频列表**，Q-10 拍板） | 案件模式下占据视频列表区域：证据树——视频（V### + 文件名 + 校时徽标 + 哈希状态）/ 前处理会话 / 报告 / 截图；双击视频=切到该视频；独立模式视频列表照旧 |
| 哈希反馈（Q-9） | 闲时队列逐文件算完即提示（徽标 ⏳→✓ + 状态栏）；菜单加「统一计算哈希」手动全量 |
| 多机时间线对齐视图 | **自 v1.2.0 移入（Q-5 拍板）**：按钮弹出的只读视图，案件内各已校时视频墙钟块位/重叠/缺口（复用 ClipTimelineWidget 绘制模式）；校时证据帧归档策略（Q-6）随案件 evidence 区定稿 |
| PreprocessWindow | 案件打开时：输出目录默认 `<案件>/preprocess/<时间戳>/`、finalize 自动 importPreprocessSession |

### 4.5 边界与错误处理

- 打开损坏/高版本 case.json：F1/F4 规则——版本超上限明确提示"由更新版本创建"，未知字段忽略并提示。
- 视频重新定位不修改 vla 内部任何时间数据（只更新引用路径）。
- 轻量包仅含小文件，空间风险可忽略；包内生成 README.txt 移交说明（视频清单与重新定位指引）。
- 取消：打包可复制过程可取消，半成品目录清理（沿用 TranscodeEngine 取消语义）。

### 4.6 v1.3.0 工作量分解（2.5 周）

| 任务 | 预估 |
|---|---|
| case_model + case.json 读写 + F1/F2 迁移框架 + 单测 | 2 人日 |
| CaseManager + 闲时哈希队列（完成提示/统一算按钮）+ 完整性校验 + 单测 | 2.5 人日 |
| MainWindow/VideoList/Preprocess 挂接（含独立/案件双模式回归） | 3 人日 |
| CaseDock（替代视频列表）+ 案件对话框 + 起始页 + 多机对齐视图（Q-5 移入） | 3.5 人日 |
| 轻量包移交（含 README.txt 生成）+ 集成测试 | 1 人日 |
| 文档（MANUAL 新章、README）、手工点检清单更新 | 1 人日 |

---

## 5. v1.4.0 分析报告模块

> HANDOVER 定位：一键生成标准分析报告：案件信息 + 校时后的时间线 + 亮度曲线 + 音频分析 + 截图/标签 + 前处理证据 + 结论栏，PDF 输出（含盖章位）。前置：校时 + 案件。预估 1.5-2 周。
> **格式修订（2026-08-05 用户拍板）**：输出格式 PDF → **DOCX**（Q-7）；报告覆盖**案件全部视频**（1..N，Q-11）；模板固定不可自定义（Q-12），做到本版本时与用户详细讨论格式细节。

### 5.1 技术选型：python-docx（bundled Python，Q-7 拍板）

| 候选 | 结论 | 理由 |
|---|---|---|
| **python-docx（bundled Python 子进程）** | ✅ 拍板 | 用户要求 DOCX。项目已有 bundled Python + C++↔Python 子进程 JSON 协议（analyze_video.py / probe_timestamps.py 同构成熟模式），增量仅 python-docx + lxml（约 5MB）；表格/图片/标题/边框空段（盖章位）全部 Word 原生元素 |
| QPdfWriter + QTextDocument | ❌ | 输出 PDF 不符合拍板要求 |
| C++ 手写 OOXML + zip | ❌ | XML 拼装正确性/可维护性差，无收益 |
| QTextDocumentWriter（ODF） | ❌ | 不支持 docx |

**打包同步（缺一不可，沿用 R-9 三处同步纪律）**：`setup_python_deps.bat` 与 CI 两个 workflow 的 bundled python pip 行追加 `python-docx`；`THIRD_PARTY_LICENSES` 补 python-docx（MIT）/lxml（BSD）；Release 体积增量约 +5MB。

**图表图像获取（Q-13 拍板；结果可信红线：报告图 = 屏幕图）**：

| 内容 | 方式 |
|---|---|
| 亮度/音量曲线（Qt Charts） | 现有 QChartView `grab()` 离屏抓取（ensurePolished + resize 到报告宽度 2×，保证打印 DPI）；**不做数据重绘**，避免"报告与屏幕不一致" |
| 语谱图 | SpectrogramPanelEnhanced `grabFramebuffer()`（面板存在时）；面板不存在时按既有 colormap 用 AudioData 数据 CPU 渲染等价图（降级路径，注明） |
| 视频截图 | 现有截图功能产物（frame QImage） |
| 校时/前处理证据帧 | 案件 evidence 区 PNG 原件 |

### 5.2 报告结构（固定模板 v1，章节可勾选）

```
第 1 节 案件信息      编号/名称/调查员/单位/案情简介/报告生成时间/工具版本
第 2 节 时间基准      每视频：校时来源徽标、校时模型参数（偏移+速率+是否应用修正）、
                      置信度、三测点 OCR 原文+证据截图、时钟偏差测量值与误差范围、
                      **拼接缺口/重叠定量警告（如有，Q-4 拍板必列）**、多机对齐时间线缩略图
第 3 节 视频清单      每视频：文件名/路径/时长/分辨率/fps/**SHA-256（Q-9 必列；
                      存在未算视频时生成前提示补算，报告不得缺哈希）**
第 4~N 节 逐视频分析  **案件每个视频一套完整章节（Q-11 拍板，1..N 个）**，每套含：
                      亮度分析（曲线全图含标签/辅助线/校时轴 + ROI 参数表）
                      音频分析（音量曲线 + 语谱图：色标说明 + 底噪阈值参数）
                      关键时刻（标签表：墙钟时间/流内时间/标注文本/对应截图）
第 N+1 节 前处理证据  排序报告摘要（来源/置信度/衔接警告）+ 证据帧缩略图阵列
第 N+2 节 分析结论    调查员文本（ReportDialog 内编辑，必填校验可关）
第 N+3 节 签署        调查员签名栏/日期栏/**盖章预留框**（Word 边框空段 + "（盖章处）"）
附注                 报告哈希（本文件生成后计算并写回末页副栏）/ .vla 版本
```

### 5.3 数据流

```
ReportDialog（章节勾选 + 结论编辑 + 预览）
   │「生成报告」
   ▼
ReportGenerator::build(ReportRequest)         [app 层]
   │  ReportRequest := CaseManager 快照（案件全部视频，Q-11）
   │                 + 图像抓取回调（UI 层注入，R1：app 不碰 widget 指针）
   │  逐视频：VideoState 换入图表/语谱（禁绘制更新）→ 抓图 → 复原
   │  产出：report_spec.json（报告结构化内容）+ 图片文件集
   ▼
DocxReportWriter（infrastructure，QProcess 调 bundled python）
   │  python build_report.py --spec report_spec.json --out <报告.docx>
   │  （协议与 analyze_video.py 同构：stdout JSON 结果 / stderr PROGRESS:/ERROR:，P1）
   ▼
完成 → 入案件 reports/ + manifest 登记 + 「打开所在文件夹」
```

- 图像抓取经回调注入（`std::function<QImage(const QString &videoId, const QString &what)>`），保证 app/infrastructure 不 include Widgets（R1）；逐视频抓图走"状态换入→抓取→复原"（不触碰播放引擎，只换图表数据源，全程禁绘制更新用户无感），Q-13 屏幕一致性底线不变。
- **哈希门槛（Q-9）**：build 前检查案件全部视频 sha256 非空；缺失 → 弹提示「N 个视频哈希未算」+「立即计算」（阻塞至算完）或取消。
- C6：报告内浮点显式精度（亮度 1 位小数、dB 1 位、时间毫秒整数）。

### 5.4 v1.4.0 工作量分解（1.5-2 周）

| 任务 | 预估 |
|---|---|
| report_model + ReportGenerator + report_spec.json 协议（P1 双端同步）+ 单测 | 2 人日 |
| build_report.py（python-docx：标题/表格/图片/盖章位/附注哈希）+ 模板样式 | 2 人日 |
| 图像抓取通道（逐视频状态换入→抓取→复原；图表/语谱/截图/证据帧）+ 一致性目检用例 | 2 人日 |
| ReportDialog（预览/勾选/结论/哈希门槛提示）+ 案件挂接 + manifest | 1.5 人日 |
| 真实案件样张评审（Word/WPS 打开目检）+ 文档 + 手工点检 | 1 人日 |

---

## 6. v1.5.0 P3 FFmpeg 分析引擎

> HANDOVER 定位：libav 原生亮度+音频分析，NVDEC 解码 + 显存下采样；干掉 Python 依赖与 5000 帧上限，分析提速至近实时。预估 1-2 周。

### 6.1 设计要点

**亮度通路（进程内，复用 FfmpegVideoEngine 的 demux/decode 模式）**：

```
avformat_open_input → 视频流 decode（软解多线程；GPU 段见下）
   → YUV 帧 Y 平面直采 ROI 均值（luma 即亮度，免 RGB 转换）
   → 多边形 ROI：QPolygon 光栅化为行 span 掩码（一次）→ 按行累加
   → 大 ROI 降采样：patch > 10000px 时隔行隔列采样（对齐 Python 现策略）
   → AnalysisSnapshot（结构不变，.vla 不变，UI 零改动）
```

**⚠ 证据连续性关键决策（Q-14 拍板方案 A）**：Y 平面 luma 系数依流元数据是 BT.601（SD）或 BT.709（HD），而现行 Python 通路统一 `0.299R+0.587G+0.114B`（BT.601）。同一 HD 文件两条通路亮度值可差数个点。**拍板：方案 A——swscale 转 GRAY8（BT.601 表）复刻 OpenCV 语义，A/B 验证容差 ±1 LSB**，牺牲少量性能保历史数据可比；方案 B（直采 Y 平面）永久放弃。

**音频通路**：decode → swr → float PCM → RMS（hop 512 / win 2048，参数对齐现状）→ STFT：libavutil `av_rdft`（零新依赖，Q-15 拍板；性能不足再换 pocketfft）→ Hanning 窗/log10/归一化，输出 AudioData 结构不变。

**GPU 段（兼容性红线：添头，失败即回退）**：NVDEC 解码 + 显存下采样为可选加速；运行时探测失败 → 软解通路（永久功能底线与 CI 基线）。

**解除 5000 帧上限**：47min 25fps ≈ 70,500 点 × 8B/点/ROI ≈ 0.6MB/ROI——全帧率亮度内存无压力；语谱**维持现有 8000 列抽稀上限**（Q-15 拍板）。

**Python 依赖收敛**：analyze_video.py 退役后，bundled Python 仅服务 RapidOCR（前处理+校时）。OpenCV/numpy 可从发布包剔除（瘦身 ~60MB），**但须过一个版本的过渡期**：v1.5.0 保留 Python 引擎为隐藏回退（设置项），v1.6/1.7 确认无现场回归后删除（R10 届时执行）。

### 6.2 验收（A/B 对拍为唯一可信依据）

| 项 | 验收线 |
|---|---|
| 亮度数值一致性（Q-14 方案 A） | 测试矩阵 8 文件 × 3 ROI（矩形+多边形+大 ROI），与 Python 通路逐点对比，|Δ| ≤ 1 且均值偏差 ≤ 0.5 |
| 音量/语谱一致性 | 同矩阵 RMS 曲线相关系数 ≥ 0.999；语谱 |Δ| ≤ 0.05（log10 域） |
| 性能 | D17（47min 1440p）全帧率亮度分析 ≤ 60s（软解 3140fps 实测推算 ~22s + IO）；30min 1440p 现状 Python 29.6s（抽稀 5000 帧）→ 新引擎全帧率 ≤ 同级 |
| 上限解除 | 2h 文件全帧率分析完成，点数 = 帧数 ±1% |
| 回退 | 设置切换 Python 引擎（过渡期）结果与现状一致 |

---

## 7. v1.6.0 GPU 显示管线

> HANDOVER 定位：D3D11 解码纹理零拷贝 → QRhi/OpenGL 渲染，shader NV12→RGB；放大镜/截图叠加/预读缓存随同 GPU 化。预估 1.5-2 周。
> **重要背景**：FUTURE_OPTIMIZATIONS.md §1 记录零拷贝管线曾完整实现后又移除（目标场景 CPU 光栅已够、复杂度高、截图叠加/放大镜需 CPU 帧回退）。本版本须先回答"为什么现在做"。

### 7.1 动机再评估（进入本版本的决策门）

HANDOVER 12.6 遗留③给出唯一实测驱动：**1440p+ 快拖时 QPainter CPU 缩放 ~10ms/帧 → UI 背压（uiDrops 高）**。

**分级方案**：

| 阶段 | 内容 | 收益/成本 |
|---|---|---|
| **Stage 1（本版本必做）** | VideoWidget 绘制改 QOpenGLWidget：QImage → 纹理上传 → GPU 缩放（~10ms → ~1ms/帧）；放大镜/钉图/截图叠加仍为 QImage CPU 通路（数据可得性不受影响） | 消除 UI 背压主因；复杂度低；CPU 回退一行开关 |
| Stage 2（本版本选做，需重新立项评审） | 零拷贝：硬解 D3D11 纹理 → VideoProcessor NV12→BGRA → keyed-mutex 共享纹理 → 呈现 | FUTURE_OPTIMIZATIONS §1 有完整移除时落点（ensureGpuPipeline/GpuFrameInfo/GpuVideoPresenter/shader）；重启前须验证 Stage 1 不足 |

- **Stage 2 立项条件（Q-16 拍板）**：Stage 1 达标（1440p 快拖 uiDrops 归零）则 Stage 2 不立项。
- **红线重申**：GPU 路径运行时探测、失败回退现有 QImage/sws 路径；核显办公机完整可用；CI 基线永远跑 CPU 路径。
- 验收：1440p 快拖 uiDrops 归零（引擎 diag 日志 `scrub 2s:` 行）；4K 60Hz 播放 CPU 占用下降；放大镜/截图叠加功能逐项手工回归。

---

## 8. v1.7.0 前处理 v2

> HANDOVER 定位：硬件编码（NVENC/QSV，参数等价性评审）；OCR 时间戳驱动的重叠段自动剪切；多通道并行流水线；"通道_日期_起止时间"自动命名。预估 1-2 周。

| 子项 | 设计要点 | 前置/边界 |
|---|---|---|
| 硬件编码 | NVENC/QSV 运行时探测 + 回退 libx264；**等价性评审**：CQ↔CRF18 映射实测（抽样 SSIM/PSNR + 目检），报告记录实际编码器（取证留档）；NVENC 在用户机曾因驱动 API 版本不可用（HANDOVER 6.6.5）——探测失败静默回退 + 日志 | 不阻塞默认软编路径 |
| 重叠自动剪切 | 排序证据（wallStart/wallEnd）驱动：重叠段按墙钟精确切——切点非关键帧须局部重编码（切点前后各 1 GOP），其余流拷贝；**策略拍板（Q-17）：剪掉后一段开头（保前段完整）；默认关闭，检测到时间重叠时提示用户是否需要修剪** | 依赖校时数据质量；"保留原样+报告"为默认语义 |
| 多通道并行 | 组级并行（每相机一条流水线），转码仍单进程串行的磁盘约束改为"组间并行 N=2"；OCR workers 配额共享 | 线程/磁盘吞吐实测调参 |
| 自动命名 | `<通道>_<yyyyMMdd>_<HHmmss>-<HHmmss>.mp4`，通道缺失用组名；命名映射写入证据报告（原始名↔输出名可追溯） | 与 merged_concat.mp4 现行命名共存（单组默认组） |

另：HANDOVER 十六章遗留的"全量 OCR 失败路径激进早退"、十五章遗留的"混 fps 恒定帧率导出选项（-r 统一）"、十七章遗留的"转码统一分辨率/帧率参数（跨相机混拼）"归入本版本范围。

---

## 9. v1.8.0 P1a/P1b 任务化与通道化

> HANDOVER 定位：TaskRegistry 分析功能注册制 + AnalysisSnapshot 通道字典 + .vla 通道化格式；RoiModel 合并。预估 2 周。

### 9.1 TaskRegistry（P1a，落地 R8）

```cpp
struct AnalysisTaskDesc {
    QString taskId;            // "luminance" / "audio" / 未来 "ocrTenant" ...
    QString displayNameKey;    // i18n 键
    std::function<bool(const VideoState&)> canRun;   // 前置条件（如需 ROI）
    // 结果写入的 channel id 列表、展示面板工厂、进度语义
};
class TaskRegistry { void registerTask(const AnalysisTaskDesc&); ... };
```

- 现有亮度/音频两个硬编码阶段（`MainWindow::AnalysisPhase` 枚举）迁移为注册任务，状态机由 AnalysisTaskService 统一持有（替代硬编码枚举，消技术债表第 2 项）。
- **校时（v1.2.0）不等待本框架**：校时以 CalibrationService 直接落地（其产物是状态而非分析 channel），P1a 落地时视语义决定是否收编为任务。此偏离已在 §2.3 记录，**待确认**。

### 9.2 AnalysisSnapshot 通道化（P1b）+ .vla v9

```cpp
struct AnalysisSnapshot {
    QVector<qint64> timestamps;                          // 共享时间轴（不变）
    QHash<QString, ChannelData> channels;                // "luminance"/"audio"/...
    // luminance: QVector<QVector<qreal>> + dataEntries
    // audio:     AudioData（现结构原样内嵌）
};
```

- 迁移期保留 `values/audio` 兼容访问器（内部转发 channel），调用点分批切换后删除（R10）。
- RoiModel 合并（RegionModel+PolygonModel → 统一 RoiModel，RegionShape 已在库未用）**提前到 v1.5.0 开工前作为首批提交**（Q-18 拍板），消 roiId 跨模型冲突债项；**操作逻辑完全不变**（行为冻结纯内部重构：界面/交互/数据零变化）。

---

## 10. v1.9.0 P2 MainWindow 拆分

> HANDOVER 定位：上帝类（~2896 行）→ AnalysisController + VideoSessionManager + ProjectIO，行为冻结纯移动。预估 2 周。
> 放最后的原因：前序版本把新逻辑都落在了 app/domain，MainWindow 剩余内容趋于稳定，拆分面最小。

### 10.1 目标结构

```
app/AnalysisController    分析状态机 + 进度 + UI 状态驱动（消 AnalysisPhase、进度条逻辑）
app/VideoSessionManager   打开/切换/关闭/列表管理 + VideoStateManager 编排（消 onVideoSelected 群）
app/ProjectIO             .vla/CSV 读写 + 参数组装（消 save/load/export 群）
app/UiState               时长/融合参数等唯一数据源（消 5 份时长副本，R5 收口）
```

### 10.2 拆分纪律

- **行为冻结纯移动（T1 红线加强版）**：拆分期禁止任何行为变更；每个拆分 commit 可独立 bisect。
- 前置保护网：v1.2~v1.8 各版本沉淀的集成测试全绿 + 手工点检清单（届时约 30 项）全过，才允许动工。
- R2 收口：剩余 4 处 `qobject_cast<PythonAnalysisEngine*>`（python 路径注入）随 ProjectIO 上移到接口或配置对象；R3 收口：`m_chartPanel->axisX()->setRange` 类穿透封装逐处加公开接口。
- 拆分顺序建议：ProjectIO（最独立）→ VideoSessionManager → AnalysisController → UiState 收尾。

---

## 11. 测试与验收总策略

### 11.1 各版本新增测试（沿用现有四件套模式扩展）

| 版本 | 单测（headless） | 集成测试 | 手工点检新增项 |
|---|---|---|---|
| v1.2.0 | TimeCalibration 换算（含跨天/未知日期/迁移）；`--at-ms` 协议解析；sidecar 继承（连续/缺口/缺失） | 合成走秒素材任意位置校时精度 ±1s；DHAV absStart 校时 | 自动校时全流程；多机对齐视图；跨天标签显示 |
| v1.3.0 | case.json 序列化往返/迁移/损坏拒绝；manifest 哈希校验；闲时队列调度 | 轻量包移交→另一目录打开→重新定位→分析复现 | 双模式（独立/案件）切换；起始页；哈希完成提示/统一算 |
| v1.4.0 | ReportRequest 组装字段断言；report_spec.json 协议正反例 | 端到端 DOCX 生成（非空、python-docx 可重开、章节数=N 视频、哈希全列） | 样张评审（Word/WPS 目检）；报告↔屏幕图一致性 |
| v1.5.0 | ROI span 光栅化；RMS/STFT 数值 | **A/B 对拍矩阵（§6.2）** | 大文件分析；回退切换 |
| v1.6.0 | —（渲染不自动化） | uiDrops 引擎日志断言 | 放大镜/截图叠加/钉图全回归 |
| v1.7.0 | 剪切点计算（重叠/边界/非关键帧） | 硬编等价性抽样；剪切产物播放验证 | 并行流水线进度 |
| v1.8.0 | channel 迁移 v7→v8→v9 链式字段断言；RoiModel ID 唯一性 | 任务注册/状态机对抗（取消竞态） | 全功能回归 |
| v1.9.0 | UiState SSOT 断言 | 存量全量 | 手工点检清单全量 |

### 11.2 贯穿性验收线

- 全回归常绿：四测试二进制 + 28 项引擎矩阵 + scrub 场景，任一版本发版前全过。
- 证据连续性：v1.2.0 起每次发版用同一标准案件（D17 + dav 12 段 + $RCR79YF 10 段）走"前处理→校时→分析→（案件）→报告"全链路，比对关键数值。
- C3 专项：时间轴/坐标/ID/格式解析相关改动一律双人评审级自查（本方案 v1.2.0/1.8.0 全部命中）。

---

## 12. 风险登记册

| # | 风险 | 等级 | 应对 |
|---|---|---|---|
| 1 | 校时 OCR 在无 OSD/异形 OSD 素材上失败 | 中 | 人工手输兜底永远可用（F7）；absStart 通道补位；证据链不依赖 OCR 精度（前处理既定原则） |
| 2 | epoch 校时 + 仿射修正引入后轴/CSV 显示回归 | 中 | dateKnown=false 且 rate=1.0 路径与现状逐像素一致为验收项；换算集中 wallMsOf 纯函数；显著性门控防 rate 抖动 |
| 3 | 拼接缺口下线性校时误导墙钟 | **高（C3）** | 超容差强制警告 + **警告必进报告**（Q-4 拍板）；分段映射后续评估 |
| 3b | OCR 野点污染三点拟合（rate 被带偏） | **高（C3）** | 3σ 显著性门控 + 残差>3s 警告 + 对话框剔除野点重拟合；拟合参数/标准误全留档可审计 |
| 4 | 案件双模式（独立/案件）行为分叉 | 中 | 独立模式行为冻结为验收项；挂接点最小化 |
| 5 | 完整包打包体积失控（多 GB） | 低 | 空间预估前置；轻量包为默认 |
| 6 | 报告图与屏幕不一致（抓取失败/降级渲染）；逐视频抓图的状态切换风险 | 中 | 状态换入→抓取→复原全程禁绘制更新；抓取回调失败即报错不静默（C2）；样张评审为发版门槛 |
| 7 | v1.5.0 亮度系数语义漂移（BT.601/709） | **高（C3）** | Q-14 拍板方案 A（BT.601 复刻）；A/B ±1 LSB 验收；不过则引擎不上线 |
| 7b | python-docx/lxml 打入 embeddable python 的兼容性 | 低 | embeddable 无 ensurepip 已有 get-pip 引导先例（rapidocr 同路径）；CI 三处同步 + 打包实测 |
| 8 | GPU 显示重蹈"做了又删" | 中 | Stage 1 先行；Stage 2 独立立项评审，不达标不做 |
| 9 | v1.8.0 通道化迁移损坏旧 .vla | 中 | F2 三件套 + 链式迁移测试 + 标准案件对拍 |
| 10 | 排期乐观（前处理 12→15 人日修正先例） | 中 | 各版本预留 20% 缓冲；里程碑出口条件未达不带债进入下一版本 |

---

## 13. 技术细节决策记录（全部 20 项已拍板）

> 2026-08-05 两轮拍板（Q-1~Q-6 首轮、Q-7~Q-20 次轮）。此表为单一事实来源，
> 各章节设计均已按此修订；后续变更需另行评审记录。

| # | 决策 | 落点 |
|---|---|---|
| Q-1 | ✅ epoch 毫秒全日期基准；旧文件 dateKnown=false 兼容模式 | §2.1 |
| Q-2 | ✅ **升级为修正报时**（"也是为什么要深化校时功能的原因"）→ 仿射模型 + 三点拟合 + 显著性门控 | §3.4 |
| Q-3 | ✅ (b)：absStart/OCR 结果仅预填候选，点「采用」才生效 | §3.1 F1/F2、§3.7 |
| Q-4 | ✅ 线性+警告（容差 2s）；**缺口警告必须进报告** | §3.5、§5.2 |
| Q-5 | ✅ 按钮弹出；**多机对齐视图整体移至 v1.3.0 案件模块** | §3.6、§4.4 |
| Q-6 | ✅ 证据帧归档策略随案件定稿；v1.2.0 过渡期落 `<视频目录>/LumenArc_Calibration/`，不联动删除；现阶段校时聚焦单机位 | §3.1 F6 |
| Q-7 | ✅ 目录制案件 + case.json；**案件全部 vla 集中存 videos/ 一个文件夹**；**分析报告格式 DOCX** | §4.1、§5.1 |
| Q-8 | ✅ **不打包视频**：轻量包为唯一移交形态，接收方重新定位视频 | §4.1、§4.3 |
| Q-9 | ✅ **哈希必算**：闲时后台队列逐文件算、**算完有提示**、「统一计算哈希」按钮；**报告必列所有用到视频的哈希**（缺失则生成前提示补算） | §4.1、§4.3、§5.2/5.3 |
| Q-10 | ✅ 起始页显示最近案件；**进入案件后 CaseDock 替代视频列表** | §4.4 |
| Q-11 | ✅ 报告覆盖**案件全部视频**（1..N，每个一套完整章节） | §5.2 |
| Q-12 | ✅ 模板固定不可自定义；做到 v1.4.0 时与用户详细讨论格式 | §5 |
| Q-13 | ✅ 图表 widget 抓取；语谱面板缺席时数据重绘并注明 | §5.1 |
| Q-14 | ✅ **方案 A**：swscale 复刻 OpenCV BT.601 语义，A/B 对拍 ±1 LSB | §6.1/6.2 |
| Q-15 | ✅ 语谱维持现有 8000 列上限；STFT 用 av_rdft（零新依赖） | §6.1 |
| Q-16 | ✅ Stage 1 达标则 Stage 2 零拷贝不立项 | §7.1 |
| Q-17 | ✅ 重叠**剪掉后一段开头**（保前段完整）；默认关闭；发现时间重叠时提示用户是否需要修剪 | §8 |
| Q-18 | ✅ RoiModel 合并提前到 v1.5.0 首批提交；**操作逻辑完全不变**（行为冻结纯内部重构） | §9.2、§2.3 |
| Q-19 | ✅ **本轮不做新旧版本兼容**，严格升版以最优效率做；兼容性下一轮排期再议；加载迁移（旧文件新版可读）照常强制 | §2.2、§1.3 |
| Q-20 | ✅ 沿用中英双语 lang(zh,en) | 全程 |

---

*本方案与 DEVELOPMENT_STANDARDS.md 全面对齐：R1-R10、C1-C6、F1-F6 在各模块设计中逐条落位；凡偏离（如校时不经 TaskRegistry）均已显式记录并经用户拍板（第十三章）。*
