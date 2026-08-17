# v1.8.0 P1a/P1b 任务化与通道化施工方案（PENDING P-30）

> **状态：已拍板（2026-08-17 用户确认：Q1 数据块沿用+META 通道清单 / Q2 未知通道 opaque 保全 / Q3 校时不收编 / Q4 AnalysisPhase 当版删 / Q5 Python 引擎退役 / Q6 进度统一 0-100）。待施工。**
> 基线：HEAD `537bde1`（2026-08-17，§46 收口后）。前置：v1.5 ✅（PENDING 表）。
> 定位（V1_ERA_TECH_PLAN §9 / §2.2 / §1.3）：**P1a** TaskRegistry 分析任务注册制
> （消 `MainWindow::AnalysisPhase` 硬编码枚举 = PENDING P-32）；**P1b**
> AnalysisSnapshot 通道化 + **.vla v10**（消 luminance/audio 硬编码成员 =
> PENDING P-33）；附 **Python 分析引擎退役评估**（P-25，用户已拍板"再留一
> 版本，v1.8.0 时评估"）。预估 2 周。
> 注意：.vla 现为 **v9**（v1.2.1 piecewise 占用，V1_ERA §1.3 已修正），本版
> 通道化落 **v10**，迁移链 v7→v8→v9→v10。

---

## 1. 现状盘点（代码事实）

### 1.1 分析编排：硬编码两阶段（P-32 债项现场）

| 环节 | 代码落点 | 现状 |
|---|---|---|
| 阶段枚举 | mainwindow.h:56 `enum AnalysisPhase { None, Luminance, Audio }` + :297 `m_analysisPhase` | **R7/R8 违例**：流程状态散在枚举+多个按钮使能布尔里 |
| 进度语义 | mainwindow.cpp:3271 `onAnalysisProgress`：Audio 硬映射 70%~100%、Luminance 0%~100% | 新增分析类型必须再加 if 分支（R8 明令禁止） |
| 启动入口 | `onAnalyze()`（:3221 置 Luminance）/ `onAudioAnalysis()`（:3265 置 Audio） | 两份近似样板：校验→引擎调用→按钮/进度条状态设置 |
| 完成合并 | `onAnalysisFinished`（:3300） | 手工合并策略：亮度结果保留既有 audio、audio-only 合入既有 snapshot——**合并策略本身就是通道耦合点** |
| 失败/取消 | `onAnalysisFailed`（:3358） | 按文案 `"Analysis cancelled by user."` 比较（C1 违例，顺带收口） |

### 1.2 数据模型：硬编码成员（P-33 债项现场）

```cpp
struct AnalysisSnapshot {                    // domain/analysis_snapshot.h:123
    QVector<qint64> timestamps;              // 亮度共享时间轴
    QVector<QVector<qreal>> values;          // 亮度矩阵（ROI 行主序）
    QVector<DataEntry> dataEntries;          // ROI 身份映射（v6）
    AudioData audio;                         // 自含时间轴（i*timeResolutionMs，B3）
};
```

消费方（通道化改造面）：
`TimelineModel::setData/clearLuminanceData/removeRegionData*`（timeline_model.cpp:45-110）、
`ChartPanel`（series/dataIndex，chartpanel.h:240）、`SpectrogramPanelEnhanced::
setSpectrogramData`、CSV `exportToCsv`（analysis_snapshot.h:283，亮度列+Volume 列）、
`CamTimeline`（多机对齐，读校时为主）、`onAnalysisFinished` 合并逻辑。

### 1.3 .vla v9 现场与 RoiModel 现状

- VLA2 二进制容器（timeline_model.cpp:127）：魔数 VLA2 + 块结构
  `META`(JSON, version=9) / `TMS`(qint64 时间戳) / `LUM`(float32 矩阵) /
  `VOL`(float32 音量) / `SPEC`(uint8 量化谱)；kCurrentVlaVersion=9；
  加载端版本上界拒绝（F4）+ v≤7 JSON 老格式嗅探分流 + v7→v8 校时迁移已就绪。
- `peekCalibrationFromVla`（:851）只解 META——**任何版本演进必须保它可用**。
- **RoiModel 合并已完成**（Q-18 提前到 v1.5.0 首批提交，src/domain/roi_model.*
  单模型双形态 API）——本版 P1b 范围相应缩减，只剩 snapshot/vla 通道化。

### 1.4 引擎双轨与 Python 依赖事实（P-25 评估输入）

- 默认 `LibavAnalysisEngine`（QSettings `analysisEngine`，mainwindow.cpp:519，
  默认 "libav"）；`PythonAnalysisEngine` 为过渡回退（A/B 对拍已过：亮度
  |Δ|≤1、volume 相关 ≥0.999、语谱 |Δ|≤0.001）。
- 剩余 `qobject_cast<PythonAnalysisEngine*>` 3 处（mainwindow.cpp:1275/3179/3237，
  python 解释器路径注入）——退役即清零（P-35 提前部分收口）。
- **关键事实（影响退役收益判断）**：`probe_timestamps.py`（OCR，活跃租户）
  深度依赖 cv2+numpy（resize/CLAHE/高斯/阈值/形态学/imencode，35-36 行起）；
  v1.4.0 报告模块（P-28）规划 `build_report.py` 走 bundled Python。→
  **bundled Python、OpenCV、numpy 均无法随分析引擎退役出包**，§6.1 当年
  "瘦身 ~60MB" 的估计已不成立（详见 §5）。

---

## 2. 拍板点（Q 项，待用户确认）

### Q1 通道字典的物理布局：内存侧先改、磁盘侧 v10 块标签沿用 ✅ 建议照抄

- 内存：`QHash<QString, ChannelData> channels`（id 约定 "luminance"/"audio"）。
- 磁盘：VLA2 容器不变；`META["channels"]` 新增通道清单（id+类型+参数+计数）；
  数据块**沿用既有标签**（`LUM `/`VOL `/`SPEC`）——luminance/audio 的字节
  格式零改动，v10 的差异仅在 META 通道清单与语义版本号。未来新通道按
  `CH:<id>` 四字标签扩展（块头 tag 本就 4 ASCII）。
- 收益：v9→v10 迁移风险最小化（数据块读回代码复用），F2 三件套聚焦 META。

### Q2 未知通道保全策略（F4 升级：取证场景"数据不丢"优先）✅ 建议"不丢弃"

新版读旧版产物遇未知 channel → 提示 + **不解析但原块保留**，回写 .vla 时
原样带回（opaque passthrough）。理由：旧版本创建的数据被新版本静默剥离 =
违反第一原则"数据不丢"，比 F4 原文的"忽略并提示"更强。（实现：未识别
`CH:*` 块以原始 QByteArray 存入 channels 字典的 opaque 槽。）

### Q3 校时是否收编为任务 ✅ 建议"不收编"（维持 §9.1 偏离记录）

CalibrationService 产物是**状态**（TimeCalibration 入 VideoState/.vla）而非
分析 channel，且已被案件徽标/报告/多机视图多点消费。收编徒增间接层。本版
仅在 TaskRegistry 文档中记录此决策，等未来出现"产物即 channel"的新校时
类任务（如周期性 OSD 巡检）再议。

### Q4 AnalysisPhase 枚举删除时点 ✅ 建议"本版删"

P1a 状态机上线 + 全回归绿后**当版删除** `MainWindow::AnalysisPhase` 与
`m_analysisPhase`（R10 死代码零容忍），不留兼容别名。

### Q5 Python 退役动作分级 ✅ 建议"评估报告 + 用户拍板后仅删引擎壳"

见 §5——评估结论先行，退役实施（删 python_analysis_engine + 设置项 +
analyze_video.py 出包）须用户当批拍板；rapidocr/cv2/numpy/bundled Python
**明确不删**。

---

## 3. P1a 设计：TaskRegistry + AnalysisTaskService（app 层状态机）

### 3.1 domain：任务描述与注册表（纯数据，无 Qt Widgets，R1）

```cpp
// domain/task_registry.h（新）
struct AnalysisTaskDesc {
    QString taskId;            // "luminance" / "audio"（与 .vla channel id 同源）
    QString displayNameKey;    // i18n：lang("亮度分析","Luminance") 两语词条
    // 前置条件（如 luminance 需 ≥1 ROI）；domain 不持 VideoState 头依赖，
    // 以闭包注入（app 层注册时绑定）
    std::function<QString()>  preconditionError;  // 非空=不满足，返回用户可读原因
    QStringList producedChannels;                 // {"luminance"} / {"audio"}
    // 进度语义：0~100 自解释；服务层不再按任务类型重映射（消灭 70%~100% hack）
};
class TaskRegistry {
public:
    static TaskRegistry &instance();             // 进程级（两任务起步，够用）
    void registerTask(const AnalysisTaskDesc &desc);
    QVector<AnalysisTaskDesc> tasks() const;
    const AnalysisTaskDesc *find(const QString &taskId) const;
};
```

面板工厂**不进 domain**（R1）：展示面板仍由 MainWindow 按 channelId 接既有
ChartPanel/SpectrogramPanelEnhanced——v1.8 只有两个任务，面板本就存在；
"任务→面板"注册表留待第三个任务出现时一并设计（YAGNI，防过度抽象）。

### 3.2 app：AnalysisTaskService（R7 显式状态机，替代 m_analysisPhase）

```
Idle ──start(taskId)──► Precheck(前置校验) ──► Running ──► Finished/Failed/Cancelled
                              │失败                │（引擎信号驱动）
                              ▼                    ▼
                           Failed              合并策略(§3.3) + .vla 异步保存
```

```cpp
// app/analysis_task_service.h（新，QObject）
class AnalysisTaskService : public QObject {
    // 编排：校验 → 按任务调用 IAnalysisEngine::startAnalysis / startAudioAnalysis
    // （接口零改动，R2/R4 保持）→ 按任务聚合 progressUpdated/analysisFinished/
    // analysisFailed → 发 UI 中性信号：
signals:
    void taskStarted(const QString &taskId);
    void taskProgress(const QString &taskId, qreal percent, const QString &detail);
    void taskFinished(const QString &taskId, const AnalysisSnapshot &merged);
    void taskFailed(const QString &taskId, const QString &errorCode); // C1：错误码不用文案
    void taskCancelled(const QString &taskId);
};
```

- **取消竞态**（§11.1 v1.8 对抗测试项）：引擎 cancelAnalysis 后仍可能到达
  一次 finished/failed——服务层以状态机 gating（Running 之外一律忽略），替代
  现有按错误文案判断取消（C1 收口）。
- MainWindow 三处样板（onAnalyze/onAudioAnalysis/onAnalysisProgress/Finished/
  Failed 共 ~200 行）收敛为：按钮 → `service->start("luminance")`；
  信号 → 统一的 UI 状态刷新 lambda（按 taskId 查 displayNameKey）。
- **行为冻结**：按钮使能次序、进度条可见性、完成气泡 5 秒自闭、.vla 自动
  保存时机逐项对照现状清单化验收（§6.3）。

### 3.3 完成合并策略（从 MainWindow 移入服务层，通道化第一步）

现状两规则原样迁移：亮度任务完成→保留既有 audio；audio 任务完成→合入既有
亮度。通道化后自然表述为：**按 producedChannels 逐通道覆盖，未产出通道保持**。
此规则入 TaskRegistry 文档注释，成为未来所有任务的统一合并语义。

---

## 4. P1b 设计：通道化 + .vla v10（F2/F3/F4 三件套）

### 4.1 内存模型

```cpp
struct ChannelData {
    enum Kind { Luminance, Audio, Opaque } kind;
    // Luminance: rows(ROI×time) + dataEntries；时间轴共享 snapshot.timestamps
    QVector<QVector<qreal>> lumRows;  QVector<DataEntry> dataEntries;
    // Audio: 现结构原样内嵌（自含时间轴，B3 语义不变）
    AudioData audio;
    // Opaque: 未知通道原块（Q2），回写原样带回
    QByteArray opaqueTag;  QByteArray opaquePayload;
};
struct AnalysisSnapshot {
    QVector<qint64> timestamps;              // 亮度共享时间轴（语义不变）
    QHash<QString, ChannelData> channels;    // "luminance" / "audio" / 未来扩展
    // 迁移期兼容访问器（内部转发 channels；调用点分批切换后删，R10）：
    const QVector<QVector<qreal>> &values() const;      // → channels["luminance"].lumRows
    const AudioData &audio() const;                     // → channels["audio"].audio
    bool hasAudio() const;
};
```

- 迁移期兼容器保持**引用语义**（现状多处按 const 引用读 `values[i]`），
  避免隐性深拷贝（Q2 隐式共享规则）。
- 调用点切换顺序（每批一提交，独立可 bisect）：
  ① TimelineModel::setData/clear* ② onAnalysisFinished 合并 ③ ChartPanel 取数
  ④ SpectrogramPanelEnhanced 入参 ⑤ exportToCsv ⑥ 全绿后删兼容器。

### 4.2 .vla v10（磁盘格式）

| 块 | v9 | v10 |
|---|---|---|
| META | version=9；point_count/region_count；audio 参数；data_entries | version=**10**；新增 `channels` 清单：`[{"id":"luminance","kind":"luminance","roi_count":N,"point_count":M,"data_entries":[…]}, {"id":"audio","kind":"audio","sample_rate":…}]`；`point_count` 等旧字段**保留双写一版**（防老版本读者误判，下版可议去留——F3 只加不改） |
| TMS/LUM/VOL/SPEC | 亮度/音量/谱数据块 | **字节格式零改动**（Q1） |
| CH:xx | — | 预留：未来新通道数据块（本版只定义标签规约不产出） |

读写与迁移：

- `kCurrentVlaVersion = 10`；saveToFile 写 v10（META channels + 双写计数字段）。
- loadFromFile：v10 → channels 路径；**v9/v8/v7/JSON 老格式 → 读入后内存升
  channels**（LUM→luminance、VOL+SPEC+audio 参数→audio）——加载迁移零成本，
  保存自然落 v10。
- `peekCalibrationFromVla`：版本上界同步 10，逻辑不变（只解 META）。
- **F4/未知通道**：v10 文件含本版不认识的 `CH:*` 块 → 载入 opaque 通道 +
  状态栏提示"含未知分析通道（N 个），已原样保留"（Q2）。
- 迁移链测试锚点：v7(JSON)→v8→v9→v10 逐级字段断言（vla_load_test 扩展），
  标准案件（D17 + dav 12 段 + $RCR79YF 10 段）对拍关键数值（§11.2 贯穿线）。

### 4.3 case.json / 其他落点

- case.json **不动**（v1 兼容，vla 版本号自携带——V1_ERA §1.3）。
- `CaseVideoRef` 校时徽标缓存与 vla 内部格式解耦，零影响。
- CSV 导出列语义不变（Time/R*_Brightness/Volume），属于通道数据的外显，
  列生成逻辑改为遍历 channels["luminance"]（列序冻结防用户脚本断链）。

---

## 5. Python 引擎退役评估（P-25，产出评估报告 + 拍板）

### 5.1 评估清单（施工批内完成，写入 WORK_HISTORY）

| # | 检查项 | 现状初判 |
|---|---|---|
| 1 | v1.5.0 起现场回归记录中 Python 引擎使用痕迹（WORK_HISTORY/用户反馈检索） | 待查（预期：默认 libav 后无人切回） |
| 2 | A/B 对拍基线是否持续有效（亮度 ±1 / volume ≥0.999 / 语谱 ±0.001） | 已过（§1.4），libav 为默认已 2+ 版 |
| 3 | bundled Python 依赖面：analyze_video.py 独占 vs 共享 | **共享**：probe_timestamps.py（cv2/numpy/rapidocr）+ P-28 build_report.py（python-docx，规划中） |
| 4 | 可移除物清单 | 仅 `python_analysis_engine.{h,cpp}`（664 行）、`analyze_video.py`、CMake 4 处测试目标引用、设置项"python"分支、MANUAL 相关段落 |
| 5 | 体积收益 | **≈0**（§1.4 关键事实：OpenCV/numpy/Python 运行时均被 OCR 租户占用，§6.1 的 ~60MB 估计失效） |
| 6 | 风险面 | 极低：回退引擎退役后若 libav 出现现场级 bug，回退路径=降级到上一发布版（取证工具常规兜底） |

### 5.2 评估结论（草案，待用户拍板）

- **建议退役**：收益是维护面收窄（双引擎 × 双测试矩阵 × R2 cast 债 3 处清零），
  而非体积；风险由"默认 libav 已运行 2+ 版无回退诉求"覆盖。
- 若用户再留一版：登记 PENDING 顺延，设置项文案补"下版移除"预告（D1/D7）。
- **无论拍板结果**：`qobject_cast<PythonAnalysisEngine*>` 3 处中的解释器路径
  注入逻辑应在本版改为引擎自探测（构造时 static detectPythonPath，见
  mainwindow.cpp:1700 已有先例），cast 清零不依赖退役拍板（P-35 部分提前）。

---

## 6. 测试与验收

### 6.1 新增自动化（V1_ERA §11.1 v1.8.0 行落地）

| 套件 | 新增断言 |
|---|---|
| 新 `task_registry_test` | 注册/查找/前置条件；状态机全迁移路径（Idle→…→Finished/Failed/Cancelled）；**对抗**：引擎不发信号（超时）、cancel 后迟到 finished 被忽略（竞态）、Running 中重复 start 拒绝 |
| `vla_load_test` 扩展 | v7→v8→v9→**v10** 链式字段断言（ROI id/data_entries/audio 参数/校时逐级保留）；v10 往返（round-trip）；**未知 CH: 块保全**（载入→回写→字节不变）；v11 拒绝读（F4 上界）；peekCalibrationFromVla 对 v10 可用 |
| `case_test` 回归 | 案件模式下 v10 vla 登记徽标/校时透传不变 |
| 全回归 11 套 | 常绿门槛（含 ui_chain_test 亮度/音频分析链路） |

### 6.2 行为冻结验收（P1a 专项）

进度条语义（亮度 0-100 / 音频原 70-100 映射→**统一 0-100**，此为**唯一有意
行为变化**，点检注明并经用户确认）；按钮使能次序、完成气泡、取消即时性、
切换视频时分析进行中的行为与现状一致。

### 6.3 手工点检（RELEASE_CHECKLIST_V1.8_CN.md 新建）

亮度分析（矩形+多边形 ROI）/ 音频分析 / 先音频后亮度（合并保留）/ 先亮度后
音频（保留 audio）/ ROI 删除后曲线同步 / CSV 列序 / v9 老案件打开→分析→
另存（升 v10）→ 重开一致 / 报告模块（若 P-28 已实施）消费 v10 正常。

---

## 7. 风险登记

| # | 风险 | 等级 | 应对 |
|---|---|---|---|
| 1 | 通道化迁移损坏旧 .vla（C3 级） | **高** | F2 三件套 + 链式迁移测试 + 标准案件数值对拍；saveToFile 只在数据非空时写（现状不变）；QSaveFile 原子写沿用 |
| 2 | 兼容访问器引用语义被破坏引发隐性拷贝/悬垂 | 中 | 兼容器返回 const 引用；切换点逐批小提交 + 全回归；最终删除时编译器兜底 |
| 3 | 状态机重构夹带行为变更（T1 违例） | 中 | P1a 与 P1b **分开提交序列**；进度映射唯一白名单化变化；行为冻结清单逐项勾选 |
| 4 | 退役拍板反复造成半吊子状态 | 低 | §5 评估报告先行；拍板前不动引擎壳；cast 清零独立提交不绑定退役 |
| 5 | TaskRegistry 过度设计（两任务硬上框架） | 中 | 本版只做注册表+状态机最小面；面板工厂/多任务并行明确不做（§3.1） |

---

## 8. 任务卡（提交切分）

| # | 任务 | 内容 | 预估 |
|---|---|---|---|
| T1 | domain：task_registry + AnalysisTaskDesc + 单测 | 纯数据注册表；两任务注册 | 0.5 人日 |
| T2 | app：AnalysisTaskService 状态机 + MainWindow 接线切换 + 删 AnalysisPhase（Q4） | 含取消竞态 gating、C1 错误码化；行为冻结清单 | 2.5 人日 |
| T3 | domain：ChannelData + channels 字典 + 兼容访问器 | 内存模型；TimelineModel setData/clear 切换 | 1.5 人日 |
| T4 | .vla v10：save/load/META channels + 双写 + peek 上界 + 迁移链测试 | F2 三件套 + 未知通道 opaque（Q2） | 2 人日 |
| T5 | 消费点切换（ChartPanel/语谱/CSV/onAnalysisFinished）+ 删兼容器 | 分 3-4 个小提交 | 1.5 人日 |
| T6 | Python 退役评估报告 + cast 清零（不依赖拍板部分） | §5 清单逐项 + 报告入 WORK_HISTORY | 1 人日 |
| T7 | 全回归 + 手工点检清单 + 文档（MANUAL/README/DOCS_MAP/PENDING/D4 版本四处） | RELEASE_CHECKLIST_V1.8 新建 | 1 人日 |

合计 ~10 人日（2 周日历，含 20% 缓冲——风险 #10 排期纪律）。

---

## 9. 拍板记录（2026-08-17 用户逐条确认，全部按建议）

1. ✅ Q1 磁盘布局：META `channels` 清单 + LUM/VOL/SPEC 数据块字节零改动，`CH:<id>` 标签预留。
2. ✅ Q2 未知通道：**opaque 保全不丢弃**（F4 加强为取证“数据不丢”），提示 + 回写原样带回。
3. ✅ Q3 校时不收编为任务（维持 CalibrationService，决策已记录）。
4. ✅ Q4 AnalysisPhase 枚举当版删除（R10）。
5. ✅ Q5 **Python 分析引擎退役**（P-25 同步收口）：收益为维护面收窄；实施范围仅引擎壳
   （python_analysis_engine.{h,cpp}/analyze_video.py/CMake 引用/设置项）；bundled Python
   与 cv2/numpy/rapidocr 因 OCR/报告租户保留。
6. ✅ Q6 音频进度统一 0-100%（唯一用户可见变化，已确认接受）。
