# v1.17.0 MainWindow 拆解施工方案（PENDING P-76~P-80）

> **状态（2026-09-07）：四阶段全部完成**（`e99c947`→`01ad822`→`e30d534`→`0e4e2e5`），
> P-76~P-80 已勾销；阶段 D 额外抓出并修复 B3 的 KeyGuardFilter eventFilter 遗漏（快捷键失效）。

> **状态：已拍板（2026-09-06 用户确认：Q1~Q7 全部按推荐敲定；**每阶段完成后提交 git**）→ 施工中**
> 基线：HEAD `7d66ce0`（v1.16.2）。安全回退点：分支+标签 `safety/pre-mw-split-20260906`。
> 定位：规范§八「MainWindow 上帝类」收口第二批。v1.9（P-31）已拆出四组件
> （ProjectIO/VideoSessionManager/AnalysisController/UiState），但 mainwindow.cpp
> 从 4183 行涨到 **4770 行**（v1.10~v1.16 多机/工作台/快照分屏全部堆回 UI 壳）。
> 方案经 reviewer 审查（有条件通过），7 项必须修改已并入本文（审查原文存档
> `C:/Users/MJ/tmp/mw_research/PLAN_review.md`，施工前移入 docs/ 归档）。

---

## 一、现状盘点（2026-09-06 实测，含审查修正）

### 1.1 方法分布（83 个方法；Top6 占 54%）

| 区段 | 行号 | 行数 | 性质 |
|---|---|---|---|
| 构造函数 | 1-811 | 811 | 纯 UI 装配（共享样式局部 :186-192 跨 4 区段） |
| createMenus | 824-1206 | 382 | QAction 构建 |
| createToolBar | 1206-1519 | 313 | 按钮构建 |
| setupConnections | 1519-2006 | 487 | ~90 条 connect 混一个函数 |
| openVideoFile | 2558-2862 | 304 | 打开/切换/恢复编排（~150 行 restore 扇出到 10+ 控件） |
| onSnapshotQuick | 4351-4628 | 277 | 纯渲染段 :4392-4585 + 入案编排 :4588- |
| 其他 100+ 行 | — | — | onSetStartTime 106 / createMagnifier 126（放大镜群 ≈285） |

### 1.2 红线/规范残留

| 项 | 证据 | 债号 |
|---|---|---|
| R3 残留 1 处 | :3043 `axisX()->min()/max()` 视口存/恢复（P-36 后新增） | P-76 |
| R5 残留 | `m_currentVideoPath` 47 处代码+3 注释；`m_currentSpeed` 10 处（Key_Z 直写 :4256） | P-77 |
| Q5 违规 | eventFilter :4180-4351 = 18 键键位路由本体（:4198-4348），装在 6 处具体控件（:295-296/:638/:1512/:1668-1671）；机制=控件有焦点时全局键仍生效。**composeworkbench 的 QShortcut 先例不可照搬**（那边运输控件全 NoFocus） | P-78 |
| 上帝单文件 | 4770 行单 TU | P-79 |
| 快照渲染管线在壳 | onSnapshotQuick 纯渲染段 200 行不可单测 | P-80 |

### 1.3 测试闸现状

- 涉及 MW 的 5 套：mw_test 112 / ui_chain 72 / case_e2e 52 / vla_load 41 / case_test 271（CHECK 计数）
- 测试通道：`MainWindow mw;` + `QMetaObject::invokeMethod`（私有槽即元方法）
- **testLumaFullChain 受 `LUMENARC_REPRO_VIDEO` 环境变量门控**（mw_test:606-609，不设即 SKIP）——常规全回归跑不到恢复链
- openVideoFile 覆盖缺口：内存态恢复值保真（:2627-2742）、无状态清场（:2745-2810）、.vla 缓存案内加载 vs 弹框（:2799-2830）
- 像素断言先例（合成图）：vla_load_test:807-886 / ui_chain:453+；mw_test 有 bundled ffmpeg 合成可播视频先例（:471-496）

---

## 二、拍板记录（Q1~Q7，2026-09-06 用户全部按推荐确认）

| # | 拍板项 | 决定 |
|---|---|---|
| Q1 | multi-TU 拆分（7 个翻译单元，单一类 API 零变化） | ✅ 做 |
| Q2 | onSnapshotQuick 渲染管线抽 SnapshotComposer（输入集闭合：+videoSize/OSD 文件名/预渲染 chartImg/specImg） | ✅ 做 |
| Q3 | R5 SSOT：currentVideoPath→VideoSessionManager；速度+降噪强度→PlaybackState | ✅ 做 |
| Q4 | Q5：eventFilter 改 KeyGuardFilter（守卫+转发，18 键 switch 搬共享处理函数；保留「全局键优先于聚焦控件」语义；不用 QShortcut） | ✅ 做 |
| Q5 | 不做：案件 UI/播放传输外移（v1.9 Q1/Q3 拍板延续）；运行时性能项（P-29/P-45）另案；多机入口不动 | ✅ 认可 |
| Q6 | 版本 v1.17.0；PENDING 登记 P-76~P-80；DOCS_MAP 同步；**每阶段完成后提交 git** | ✅ 认可 |
| Q7 | buildStamp 改小头文件 `inline`（戳=mainwindow_ui.cpp 编译时刻；:117-119 语义注释同步更新） | ✅ 按推荐 |

**R9 收益归零声明**（审查实锤）：报告模块仅复用 `ChartPanel::renderToImage`
（report_service.cpp:545），不存在第二份可省复制；P-80 动机仅为「出壳+可单测」。

---

## 三、阶段设计（每阶段=独立提交组+全回归门槛+可独立回退）

### 阶段 A：multi-TU 拆分（P-79）~2-3 天 ✅ 完成 `e99c947`（2026-09-07）

MainWindow 保持单一类，.cpp 按职责域拆 7 个翻译单元（成员函数定义跨 TU，
CMake 显式源列表 :66-127 加 6 行）：

| 新文件 | 内容 | 预估 |
|---|---|---|
| mainwindow.cpp（留） | 打开/切换+校时编排+任务槽+播放传输+保存加载+事件+ROI 模式 | ~1700 |
| mainwindow_ui.cpp | 构造体（逐字抽取 buildXxx 私有方法）+菜单+工具栏+状态栏 | ~1900 |
| mainwindow_wiring.cpp | setupConnections → 8 个 `setupXxxConnections()` | ~500 |
| mainwindow_snapshot.cpp | onSnapshotQuick | ~280 |
| mainwindow_export.cpp | 分段/合成导出三入口 | ~150 |
| mainwindow_case.cpp | 案件 UI 群+多机入口+入案 | ~480 |
| mainwindow_magnifier.cpp | 放大镜群（create/remove/wheel/右键/钉图） | ~285 |

规则：
- A1 口径=**行为冻结重构**（逐字抽取+调用序不变）；构造体共享样式局部
  :186-192 参数化传递（`const QString &` 传入 buildXxx）
- 门槛：**mainwindow.h public/protected 区零 diff**（private 区只增私有方法声明，
  不删不改既有行）+ 全回归 18 套绿 + 手工点检（开视频/切视频/菜单工具栏观感）
- buildStamp（:120 唯一 file-local static）改 `build_stamp.h` inline（Q7）

### 阶段 B：红线债收口（P-76/P-77/P-78）~1-2 天（顺序 B1→B2→B3）✅ 完成 `01ad822`（2026-09-07）

- **B1 R3**：ChartPanel 加 `xAxisRange() const`（setXAxisRange 先例
  chartpanel.cpp:2390）；:3043 改调；确认 :1962-1964 语谱→曲线联动信号路径不受影响
- **B2 R5**：VideoSessionManager 加 `currentVideoPath()`/setter（与 stateManager 键一致）
  + PlaybackState（速度+降噪强度；Key_Z :4256 写路径随迁）。三条边界不变量：
  ① .vla 直载不覆写路径（:2578 注释约束）② videoRelocated 双写同步（:737-745
  migrateKey+路径）③ 列表清空时清零（:1874）。新单测：.vla 直载后
  `currentVideoPath()` 不变。验收 grep 覆盖全部 `src/mainwindow*.cpp`
- **B3 Q5**（B2 之后，Key_Z 耦合）：KeyGuardFilter 小对象（<30 行）只做守卫+转发；
  18 键 switch 搬入共享处理函数；保留「全局键优先于聚焦滑块/列表/树」语义
- 门槛：全回归 + mw_test + 新增单测 + 手工点检（含「聚焦滑块/视频列表时按 ←→/空格」）

### 阶段 C：SnapshotComposer（P-80）~2-3 天 ✅ 完成 `e30d534`（2026-09-07）

- **C0**：`OverlayWidget::burnAnnotations/drawMagnifierIndicator/mapStoredRectToFrame`
  （videowidget.cpp:359+ 静态渲染函数）迁至非 Widgets 模块（R1 前提；
  迁移闸=ui_chain_test:524/:555 既有像素断言）
- **C1**：`SnapshotInputs` = 帧+旋转+videoSize+校时+标签+OSD 文件名+ROI 模型+
  放大镜图+**预渲染 chartImg/specImg**（:4500-4503 的 renderToImage/renderHeatmapImage
  留 MainWindow 对活控件调用，composer 只收图）；纯渲染段 :4392-4585 整体搬入
- **C2**：snapshot_test 像素断言（先例引 vla_load_test:807-886 / ui_chain:453；
  offscreen 无 CJK 字体→ASCII 断言，§89 先例）
- 门槛：snapshot_test 全绿 + 全回归 + 手工点检（证据快照观感截图对照基线）

### 阶段 D：openVideoFile 瘦身（restore 扇出下沉）~3-4 天 ✅ 完成 `0e4e2e5`（2026-09-07）

- **D1 闸先行**：新增 mw_test switch-restore 用例——a→b→a 切换值保真 +
  无状态清场零泄漏断言（复用 :471-496 ffmpeg 合成视频模式）+ .vla 直载断路径未覆写
- **D2**：各控件加 `resetForNewVideo()`/`applyRestoredState(...)` 公开 API
  （ChartPanel/SpectrogramPanelEnhanced/PlaybackAdjustPanel/VideoListPanel/CaseDock，
  一次只动一个控件）；restore 扇出改走 API（R3 合规）；收口 B2 的
  currentVideoPath setter 落点（避免二次动刀）
- 门槛：新 switch-restore 用例绿 + 全回归 + 手工点检（切视频现场恢复抽 5 项）

### 收尾

- 版本 bump v1.17.0（CMake 单一真源）；用户无感知变更（手册不动）✅
- HANDOVER/WORK_HISTORY/PENDING 勾销/DOCS_MAP（D1/D2/D6/D7）✅
- RELEASE_CHECKLIST_V1.17_CN.md（docs/）真机点检（行为冻结对照：开/切视频/校时/案件/多机/
  快照/导出 各 1 条 + 聚焦控件按键 1 条）✅ 2026-09-07 用户真机点检通过，封板

**总预估 9-12 天（2 周左右），与 v1.9 拆分周期一致。**

---

## 四、风险与回退

| 风险 | 缓解 |
|---|---|
| A 阶段搬漏 include/样式局部 | 编译器兜底 + public 区零 diff 门槛 + `build_tmp\build_target.bat ALL` |
| C 阶段像素漂移 / R1 破 | C0 迁移有 ui_chain 闸；像素断言+基线截图对照；ASCII 断言 |
| D 阶段现场恢复漂移 | D1 闸先行（switch-restore 值保真+清场断言）；控件 API 一次一个 |
| 烂尾 | 每阶段独立可发版（v1.9 Q6 门槛）；任一阶段后
  `git reset --hard safety/pre-mw-split-20260906` 整体回退 |

## 五、纪律符合性

- T1：A=行为冻结重构（public 区零 diff）；B/C/D 每阶段内 API 增补与换调用同属
  行为不变迁移，提交粒度=文件/组件
- R1：SnapshotComposer/PlaybackState 不 include Widgets（C0 是前提）
- R10：搬出即删原件，不留转发壳
- D1/D2：PENDING P-76~P-80 + DOCS_MAP 同步