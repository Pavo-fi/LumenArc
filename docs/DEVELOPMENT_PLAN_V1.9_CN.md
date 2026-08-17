# v1.9.0 P2 MainWindow 拆分施工方案（PENDING P-31）

> **状态：已拍板（2026-08-17 用户确认：Q1-Q6 全部按建议——案件 UI/快照/播放传输均留守 MainWindow、时长只收时长、R3 只修实锤 1 处、P-34 过期勾销、动工门槛照抄）。待施工。**
> 基线：HEAD `537bde1`（2026-08-17）。**前置：P-28~P-30 全部完成**（PENDING 表
> "前序全部"；其中 P-30 的 AnalysisTaskService 落地是本方案 AnalysisController
> 范围的输入）。
> 定位（V1_ERA_TECH_PLAN §10）：上帝类 MainWindow → **行为冻结纯移动**拆为
> app 层四组件（ProjectIO / VideoSessionManager / AnalysisController / UiState），
> 收口三条架构债：P-35（R2 向下转型）、P-36（R3 穿透封装）、P-37（R5 时长
> 五副本 SSOT）。预估 2 周。
> **基线数据修正**：V1_ERA 写 MainWindow "~2896 行"已过期——当前 **4183 行**
> （mainwindow.cpp）+ 298 行头文件；其中约 1700 行是构造/菜单/工具栏/接线
> （createMenus/createToolBar/setupConnections）纯 UI 搭建，**不属于拆分对象**。

---

## 1. 现状盘点（代码事实）

### 1.1 MainWindow 方法群地图（4183 行，按行号段）

| 行号段 | 内容 | 行数 | 拆分归属 |
|---|---|---|---|
| 1-680 | 构造器、状态栏/控件创建、openPreprocessWindow | ~680 | **留**（UI 壳） |
| 681-1695 | createMenus / createToolBar / setupConnections | ~1015 | **留**（UI 壳） |
| 1696-1708 | detectPythonPath / trustedDurationFor | 13 | 随 AnalysisController |
| 1709-2140 | 打开/入案/案件 UI 群（onOpenFile→onMultiCamView） | ~430 | 案件 UI 留；打开逻辑随 VideoSessionManager |
| **2141-2479** | **openVideoFile 单函数** | **~340** | VideoSessionManager（最大单体） |
| 2480-2708 | 保存/加载/时间戳 ROI 记忆/校时徽标（onSaveAnalysis/onLoadAnalysis/onLoadOverlayImage/readTimestampRoiRegistry/savedTimestampRoi/saveCurrentVlaAsync/saveTimestampRoi/calibrationBadgeSummary） | ~230 | **ProjectIO** |
| 2709-2826 | 托盘通知 / onSetStartTime（校时对话框编排） | ~118 | 留（UI 编排） |
| 2827-2978 | 拖放事件 / 播放传输（onPlay/Pause/Stop/adjustSpeed/cycleSpeed/applySpeed/updatePlaybackButtons） | ~150 | 拍板 Q3（建议留） |
| 2979-3169 | 放大镜创建/移除/滚轮、视频右键菜单、钉图 | ~190 | **留**（UI 交互壳） |
| 3170-3397 | 分析入口/进度/完成/失败（P-30 后剩余 UI 接线） | ~230 | **AnalysisController**（P-30 后实为薄层） |
| 3398-3472 | onExportCsv | ~75 | **ProjectIO** |
| 3473-3596 | onDurationChanged/onPositionChanged/onSeekFromChart/updateTimeDisplay/showOperationStatus | ~125 | 时长收口入 UiState；其余留 |
| 3597-3634 | restoreAnalysisState（vla 状态恢复） | ~38 | **ProjectIO** |
| 3635-3805 | eventFilter（快捷键路由 + 上下文菜单） | ~170 | **留**（UI 交互壳） |
| 3806-4040 | onSnapshotQuick（证据快照编排） | **~235** | 拍板 Q2（建议留） |
| 4041-4183 | onVideoSelected / ROI 模式切换 / ROI 剪贴板 | ~143 | 切换随 VideoSessionManager；模式/剪贴板留 |

拆分净移出约 **1100-1300 行** → MainWindow 目标 ~2900 行（UI 搭建 1700 +
交互壳），仍超"上帝类"感知线但**剩余内容是稳定 UI 胶水**（V1_ERA §10 原文
逻辑：前序版本已把新逻辑落在 app/domain，拆分面最小化）。

### 1.2 三条债项的实锤现场

**P-37 / R5 时长五副本**（全部实测定位）：

| # | 副本 | 位置 | 语义 |
|---|---|---|---|
| 1 | `m_trustedDurationMs` | mainwindow.h:288 | 分析引擎可信时长（上限校准源） |
| 2 | `m_currentDurationMs` | :289 | 校准后权威时长（UI 用） |
| 3 | `m_videoEngine->duration()` | 引擎 | 容器原始观测（可能异常大，VLC/DVR 拼接） |
| 4 | VideoListPanel 条目时长 | updateDuration 推送（:3490） | 列表列显示 |
| 5 | ChartPanel `setDuration` | :3482 推送 | 图表轴范围 |

校准规则散在 `onDurationChanged`（:3473-3480：engineDur > trusted 时截断）
与 `updateTimeDisplay`（:3569 二次 fallback `engine->duration()`）。

**P-36 / R3 穿透封装实锤 1 处**：mainwindow.cpp:1652-1653
`m_chartPanel->axisX()->setRange(xMin, xMax)`（语谱图缩放→图表轴同步）。
其余 `m_chartPanel->labels()/setPointA()` 等均为公开接口调用，不属穿透。

**P-35 / R2 向下转型 3 处**：mainwindow.cpp:1275 / 3179 / 3237
（`qobject_cast<PythonAnalysisEngine*>`，解释器路径注入）——P-30 方案 T6 已
计划清零（构造时自探测）；本方案兜底：若 P-30 未清，随 AnalysisController
迁移时一并上移。

**P-34 顺带发现**：PENDING"旧版 SpectrogramPanel 死代码删除"——实测
`src/` 仅存 spectrogrampanel_enhanced.*，旧面板**已在历史版本删除**，
该条目过期（拍板 Q5）。

### 1.3 既有 app 层先例（拆分的落地模式）

CaseManager（app/case_manager.*，1000+ 行，纯逻辑无 widget 引用）、
CalibrationService、PreprocessingCoordinator、AnalysisTaskService（P-30）——
**app 层组件一律：纯数据接口 + 信号上报，不 include Widgets**（R1）。
本方案四组件全部沿用该模式：对话框、状态栏、按钮态全部留在 MainWindow。

---

## 2. 拍板点（Q 项，待用户确认）

### Q1 案件 UI 群（enterCaseMode/exitCaseMode/closeCaseWithPrompt/onNewCase/onOpenCase/openCaseFlow/onCaseProperties/onCaseRootDir/onShowStartPage/onExportCase/onBatchRelocate/onMultiCamView，~430 行）是否外移 ✅ 建议"不移"

外移必然要求新组件持有 MainWindow 内部控件句柄（视频列表/CaseDock 互换、
状态栏、窗口标题）= **违反 R1（app 层不碰 ui）**。V1_ERA §10 四组件清单
本就不含案件 UI。建议：案件 UI 编排留在 MainWindow 作为"界面壳"职责；
其业务逻辑早已在 CaseManager（app），现状分层是干净的。

### Q2 onSnapshotQuick（235 行证据快照编排）归属 ✅ 建议"留 MainWindow"

全链是 UI 编排（面板存在时抓图→OSD 烧录→合成→入案→气泡），与 widget
强耦合；抽走需注入 5+ 抓取回调，收益为负。其中若有纯函数段（OSD 文字
拼装）可顺手下沉 domain，但不作为拆分目标。

### Q3 播放传输群（onPlay/onPause/onStop/adjustSpeed/cycleSpeed/applySpeed/updatePlaybackButtons/onPositionChanged/onSeekFromChart/updateTimeDisplay，~250 行）归属 ✅ 建议"留 MainWindow"

均为 1-10 行的薄 UI 胶水（按钮态/标签刷新/seek 节流），与控件零距离；
拆走徒增信号转发层。V1_ERA §10 VideoSessionManager 定位是"打开/切换/关闭/
列表管理 + VideoStateManager 编排（消 onVideoSelected 群）"，不含传输控制。

### Q4 R3 收口范围 ✅ 建议"只修实锤 1 处"

ChartPanel 新增公开方法 `setXAxisRange(qreal min, qreal max)`（内部转
axisX），:1652 调用点改走该方法；连接体并入 setupConnections 的既有
面板互连线。不做全工程 axisX 审计（其余调用点为零）。

### Q5 PENDING P-34 条目处理 ✅ 建议"勾销（注明：旧面板已在历史版本删除，代码库无残留）"

### Q6 动工门槛 ✅ 建议"照抄 V1_ERA §10.2 原文"

前置保护网：① 全回归 11 套（届时含 v1.8 新增 task_registry_test）全绿；
② 手工点检清单（v1.3 的 A-H + v1.4~v1.8 新增项，届时约 30 项）全过；
③ 拆分期间每个组件落地后重跑全回归——任一组件完成即整体可发版（防烂尾）。

---

## 3. 详细设计

### 3.1 app/ProjectIO（工程读写，~345 行净移）

```
职责：.vla / CSV / 时间戳 ROI 记忆 / 校时徽标文案 的读写与参数组装
依赖：TimelineModel*（QObject 父传）、CaseManager*（路径分流）、RoiModel 等 domain 模型
接口（纯数据进/出；QFileDialog 留在 MainWindow）：
  SaveRequest buildSaveRequest(defaultDirHint)        // 组装 13 参数（ROI/校时/标签/融合/…）
  bool saveVla(path, SaveRequest) → saveFinished(path, ok)
  LoadResult loadVla(path)                            // 读 vla + 还原字段包（VideoState 形）
  bool exportCsv(path, regions, calibration)          // TimelineModel::snapshot().exportToCsv 包装
  QString suggestSavePath(currentVideoPath)           // 案件 videos/V###.vla / 覆写原文件 / 默认名
  savedTimestampRoi / saveTimestampRoi / readTimestampRoiRegistry（QSettings 读写收口）
  calibrationBadgeSummary()（徽标文案纯函数，随 ProjectIO 或留 domain/text 工具）
  restoreAnalysisState 的数据装配半段（UI 应用半段留 MainWindow）
信号：saveFinished / loadReady(LoadResult) / badgeTextChanged
```

迁移函数清单：onSaveAnalysis/onLoadAnalysis/onExportCsv/onLoadOverlayImage/
saveCurrentVlaAsync（QtConcurrent 异步 + 案件徽标回写）/savedTimestampRoi/
saveTimestampRoi/readTimestampRoiRegistry/restoreAnalysisState（数据段）。
**行为冻结点**：保存默认路径分流（案件 videos/ 原文件覆写/analysis_result.vla
兜底）、保存成功后徽标刷新条件（绝对路径比对 :2507）、QSettings 键名。

### 3.2 app/VideoSessionManager（视频会话，~500 行净移，含 openVideoFile 340 行）

```
职责：打开/切换/关闭/列表/入案登记/拖放文件分发的编排 + VideoStateManager 调用群
依赖：IVideoEngine*、CaseManager*、CalibrationService*（sidecar 继承）、
     VideoStateManager*（既有类，从 MainWindow 持有转为本组件持有）
接口：
  void open(path) / openTemporary(paths) / select(index) / closeCurrent()
  void admitToCase(path, interactive)
信号（UI 副作用全部剥离为状态上报，MainWindow 刷控件）：
  sessionLoading(path)          → MainWindow: setLoading(true)/列表行状态
  sessionReady(path, meta)      → 标题/时长请求/恢复状态应用（经 ProjectIO 载荷）
  sessionFailed(path, reason)   → 弹窗（MainWindow 出面，C2 不静默）
  caseAdmitted(videoId)         → CaseDock 刷新
```

**openVideoFile 拆法（最大风险点，逐步**）**：先按分支清单写集成测试
（ui_chain_test 扩展），再整体平移。分支清单（迁移前测试必须覆盖）：
① .dav 拒绝弹窗 ② .vla 直接加载 ③ 案件模式路径分流+同目录 .vla 询问导入
④ 临时打开不入案 ⑤ sidecar `.lumencal.json` 继承（连续/缺口警告）
⑥ absStart 探测建议 ⑦ 大文件 loading 态 ⑧ trustedDuration 请求与回填
⑨ 切换时取消运行中分析（B6，:4046）⑩ 无 ROI 场景恢复。

### 3.3 app/AnalysisController（分析 UI 驱动，~230 行；P-30 后为薄层）

```
职责：引擎实例生命周期（QSettings analysisEngine 选择/构造）+ AnalysisTaskService
     接线 + 进度/按钮/状态栏/完成气泡的 UI 状态驱动 + 自动保存触发
依赖：IAnalysisEngine*（自持）、AnalysisTaskService*（P-30 产物）
信号：progressUi(taskId, percent, detail) / buttonsUi(enabling) / finishedUi(msg)
     ——仅驱动 UI 态，不含分析状态机（那是 TaskService 的）
P-30 兜底项：若 R2 cast 3 处仍在，随本组件迁移清零（引擎自探测路径）
```

### 3.4 app/UiState（时长 SSOT 收口，P-37）

```
职责：时长唯一 owner
  ingestEngineDuration(ms)      // 引擎观测（可能异常大）
  ingestTrustedDuration(ms)     // 分析引擎可信时长（打开时/分析后）
  effectiveDurationMs()         // 校准规则单点：trusted>0 且 engine>trusted → trusted
信号：effectiveDurationChanged(ms) → VideoListPanel/ChartPanel/时间标签统一刷新
删除：m_trustedDurationMs / m_currentDurationMs（MainWindow 不再持时长）
范围拍板（Q 项见下）：只收时长；播放速度/音量/降噪滑块等散状态**不动**
（它们各自已是单点：引擎/面板自持，无多副本病）
```

引擎内部 `duration()` 保留（引擎自身状态的原始观测，属引擎私有）；列表/
图表接收 UiState 推送值（只收不发，非 owner）——五副本收敛为"一源两派生"。

### 3.5 不拆清单（明确排除，防 scope 蔓延）

| 项 | 理由 |
|---|---|
| createMenus/createToolBar/setupConnections（~1700 行） | UI 搭建是 MainWindow 本职 |
| 案件 UI 群（Q1）、快照编排（Q2）、播放传输（Q3）、放大镜、eventFilter、ROI 模式/剪贴板 | UI 交互壳（§2 拍板） |
| VideoListPanel/CaseDock/ChartPanel 等面板内部 | 各面板已是独立类 |
| 任何行为优化/顺手重构 | T1 红线加强版：拆分期零行为变更 |

---

## 4. 拆分顺序与提交切分（V1_ERA §10.2 原序）

```
保护网达标（Q6）→ T1 ProjectIO（最独立，无引擎交互）
              → T2 VideoSessionManager（依赖 ProjectIO 载荷）
              → T3 AnalysisController（薄层）
              → T4 UiState 时长收尾（改 5 个触点）
```

| # | 提交序列 | 内容 |
|---|---|---|
| T0 | test | openVideoFile 分支清单集成测试先行（10 分支，见 §3.2） |
| T1a | refactor | ProjectIO 类落位（新文件 + 接口空实现），编译绿 |
| T1b | refactor | 读写函数群纯移动（onSaveAnalysis 等 9 函数），MainWindow 调用点改指 ProjectIO |
| T2a | refactor | VideoSessionManager 落位 + openVideoFile 平移 + UI 副作用改信号 |
| T2b | refactor | onVideoSelected/onOpenFile 系/admitVideoToCase/拖放分发移动 |
| T3 | refactor | AnalysisController 落位（引擎选择 + TaskService 接线 + 进度 UI 驱动） |
| T4 | refactor | UiState 落位 + onDurationChanged 收口 + 删两成员（R5 收口） |
| T5 | refactor | R3 修 1 处（ChartPanel::setXAxisRange）+ PENDING 勾销（P-34/35/36/37） |
| T6 | test/docs | 全回归 + RELEASE_CHECKLIST_V1.9（存量全量手工点检）+ 文档四处同步（D4） |

每步提交后全回归；T1-T4 每组件完成即整体可发版（烂尾保险）。

---

## 5. 测试与验收

### 5.1 自动化

| 套件 | 新增/扩展 |
|---|---|
| ui_chain_test | openVideoFile 分支清单 10 项（T0 先行）；ProjectIO 保存路径分流断言（案件/独立/覆写）；UiState 校准规则（engine>trusted 截断/fallback） |
| case_e2e | 拆分前后同案件全链路快照对比（开案→打开→分析→保存→重开，状态字段逐项一致） |
| 全回归 | 每组件落地后 11+1 套（含 P-30 新增 task_registry_test）全绿 |

### 5.2 行为冻结验收（点检清单 v1.9 = 存量全量 ~30 项）

拆分前跑一遍全量点检并**留档截图/结果**，拆分后逐项对照一致：打开各格式/
拖放/列表切换/入案/临时打开/校时三来源/亮度+音频分析/保存加载 vla/CSV 导出/
放大镜/钉图/融合/快照/A-B 循环/ROI 三模式/剪贴板粘贴到全部/案件开闭/导出包/
多机视图/前处理窗口……（以 RELEASE_CHECKLIST 累积版为准）。

### 5.3 债项勾销线

P-35（R2 cast 3 处清零）、P-36（R3 axisX 收口）、P-37（时长五副本 →
UiState 一源两派生）、P-34（过期条目勾销）——四条全勾才算 P-31 完成。

---

## 6. 风险登记

| # | 风险 | 等级 | 应对 |
|---|---|---|---|
| 1 | openVideoFile 340 行平移引入行为回归（C3 级：路径分流/校时继承错了会污染证据链） | **高** | T0 分支测试先行；平移用纯移动纪律（diff 审查：只许搬家不许改写）；拆分前后 case_e2e 快照对比 |
| 2 | 拆分期夹带行为修改（T1 违例） | 高 | 每提交一句话可述；"顺手优化"一律另开 PENDING |
| 3 | 新组件与 MainWindow 循环依赖 | 中 | 组件只持 domain/app 依赖 + 信号上报；禁止回指 MainWindow（接口审查项） |
| 4 | app 层悄悄 include Widgets（R1 漂移） | 中 | 对话框/控件操作全留 UI；组件头文件 include 审查（QtWidgets 零出现） |
| 5 | 拆一半烂尾 | 中 | 组件序独立收口（§4）；任一组件完成即可发版 |
| 6 | 时长收口改动图表轴刷新时序（拖拽中偶发跳变） | 中 | effectiveDurationChanged 仅在值变化时发；点检含拖拽全时长往返 |

---

## 7. 工作量分解（~10 人日 / 2 周日历，含 20% 缓冲）

| 任务 | 预估 |
|---|---|
| T0 openVideoFile 分支集成测试 | 1 人日 |
| T1 ProjectIO（落位+9 函数迁移+路径分流断言） | 2 人日 |
| T2 VideoSessionManager（340 行平移+信号剥离+群迁移） | 3 人日 |
| T3 AnalysisController（引擎选择+接线迁移；R2 兜底） | 1 人日 |
| T4 UiState 时长收口 | 1 人日 |
| T5 R3 修 1 处 + PENDING 勾销 | 0.5 人日 |
| T6 全量点检 + RELEASE_CHECKLIST_V1.9 + 文档四处 | 1.5 人日 |

---

## 8. 拍板记录（2026-08-17 用户逐条确认，全部按建议）

1. ✅ Q1 案件 UI 群留守 MainWindow（R1：app 层不碰 ui；业务已在 CaseManager）。
2. ✅ Q2 onSnapshotQuick 留守（仅纯函数段顺手下沉 domain，不作拆分目标）。
3. ✅ Q3 播放传输群留守 MainWindow。
4. ✅ Q4 R3 只修实锤 1 处：ChartPanel::setXAxisRange（不做全工程审计）。
5. ✅ Q5 PENDING P-34 过期勾销（旧面板已在历史版本删除，代码库无残留）。
6. ✅ Q6 动工门槛照抄：全回归绿 + 手工点检全过留档 + 组件序独立收口。
