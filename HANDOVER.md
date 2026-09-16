# LumenArc 工作交接文档（HANDOVER）

> **本文件只保留最近 5 次更新**；更早记录依修改顺序（时间序）存档于
> **WORK_HISTORY.md**（规则 R2）。

## 表头（每次写完 HANDOVER 与 WORK_HISTORY 后必须同步更新本表头——规则 R2）

- **当前 HEAD**：施工批（2026-09-10 §92 P-81 微变分析落地：**已编译链接通过，待运行验证**；
  上一批 2026-09-07 §91 v1.17.0 MainWindow 拆解收官 P-76~P-80 全勾销）
- **构建**：`cmd //c "build_tmp\build_target.bat ALL"`；测试：`QT_QPA_PLATFORM=offscreen`
  + PATH 含 `C:\code\Qt\6.8.0\msvc2022_64\bin`（配置：`build_tmp\reconfigure.bat`）
- **全回归基线**（19 套，v1.17.0 后）：mw 117 / ui_chain 103（2 已知遗留）/
  libav 26 / case 270 / case_e2e 51 / segment 120 / sync 195 / preprocess 268 /
  calibration 99 / piecewise 129 / report 52 / docx 23 / roi 23 / sidecar 34 /
  task 41 / v17 37 / snapshot 36（新增）/ vla PASS / denoise ALL PASS
  （sitemap 127 = 环境遗留，旧 exe 同表现）
- **云端**（CloudBase）：env `lumenarc-prod-d6gcdfb6a8873d906`；四函数已部署+HTTP 触发器已通；
  改函数后 `cd build_tmp/tcb_deploy && MSYS_NO_PATHCONV=1 tcb fn deploy <name> --force --yes`；
  AUTH_SECRET 在 build_tmp/tcb_deploy/.auth_secret（不入库）；详见 docs/cloudbase/README.md
- **当前保留批次**（新→旧，R2 限 5 批）：
  第七十八批 §91（v1.17.0 MainWindow 拆解收官：四阶段五提交）·
  第七十七批 §90（P2.8：聚光灯 50%/条带全量化+语谱/打开输出文件夹）·
  第七十六批 §89（切割/倍速/ETA/编码提速+标注轨 v1）·
  第七十五批 §88（覆盖条/部分覆盖音轨/播放头联动+v1.16.2 打包）·
  第七十四批 §87（四步引导改版+快捷键对齐剪映/PR）。
  （§86 ROI/曲线滚动条 批 已归档 WORK_HISTORY——见下）
- **最近归档动作**：2026-09-07 §91 批——§86（合成导出 P2：ROI/曲线滚动条+宫格
  +ffmpeg8 排雷+docx 原子写）移入 WORK_HISTORY.md 末尾（R2 限 5 批）；
  早前：2026-09-04 §90 批——§85（工作台本体）移入 WORK_HISTORY.md 末尾；
  早前：2026-09-04 §89 批——§84（P1 引擎多段双模式）移入；
  早前：2026-09-03 §88 批——§83（MLT melt 构建）移入；
  早前：2026-09-03 §87 批——§82（引擎三连修）移入；
  早前：2026-09-03 §86 批——§81（账号 v1.4 署名写死）移入；
  早前：2026-09-03 §85 批——§80（账号 v1.2）移入；
  早前：2026-09-03 §84 批——§75~§79 移入；
  早前：2026-08-28 §79 批——第六十五批（§74）移入。
- **补录说明**：§80~§83 对应提交 93e6d91→434a6ec（2026-08-30~09-03）当时未逐批记录，
  本批一次性补录（每节标注对应提交哈希）。
- **常用参考导航**（已归档，查 WORK_HISTORY.md）：机位勾选面板（§54）、
  校时落盘双根因修复（§55）、显示旋转 90° 方案 A
  （第三批 §12）、音频时间轴对齐与问题 A/B 定案（深夜批）、项目规则 R1/R2
  （08-13 晚批）、架构分层与红线 R 规则（二章）、构建部署 CI（九章）、
  升级计划表 v1.2~v1.9（十章）、测试体系 28 项矩阵（七章）、校时管线
  救复速览（〇章）、案件模块 M1-M3（二十三章）。
- **管理文档**（2026-08-16 §44 建立）：待办唯一登记处 `docs/PENDING.md`；
  文档体系与维护规矩 `docs/DOCS_MAP.md`（规矩 D1-D8，待办必登记 PENDING）。

# ============================================================================
# 工作记录（2026-09-03，第七十一批）——合成导出器 P1 + 账号 v1.2~v1.4 + 引擎三连修 + MLT 基建
# ============================================================================

## 93. 微变变化率曲线：并行实现现状与剩余工作（2026-09-11 接管交接）

### 背景
P-81（微变叠加显示，§92）之上的追加需求：引擎对整段视频逐秒计算 ROI 内微变统计量
（blkMax/medD，单位=灰度级，不乘显示增益），产出 μ+3σ 阈值 + 首帧微变 onset（连续≥3s），
图表渲染曲线 + .vla 持久化 + 面板按钮触发。判据/标定值/接线契约都在
`C:/Users/MJ/AppData/Local/Temp/microchange-research/`：`curve_stage1_brief.md`（数据层）、
`curve_stage2_brief.md`（UI 层，含并行模式角色 A 实现者/B 验收者定义）、
`stage2_wiring_map.md`（299 行精确接线地图，file:line 全部实证过）。

### 现状（截至 2026-09-11 11:00，两路 worker 均阵亡，父代理接管）
| 块 | 状态 |
|---|---|
| S1 契约层：analysis_snapshot.h microdiff 通道（mdTs/mdRows[每 ROI 2 行 blkMax,medD]/mdEntries/mdOnsets/microdiff::MicroDiffOnset/mdStatMu/Sigma/Threshold/mdBaseStartMs/mdBaseDurMs）+ setMicroDiff；ianalysis_engine.h MicroDiffCurveParams + startMicroDiffAnalysis（默认发 analysisFailed）；TaskRegistry 注册；task_service 分发/合并；microdiff_curve.{h,cpp} 纯函数（roiFrameStats/aggregateSeconds/detectOnset）；CMake 登记 | ✅ 已落地 |
| S1 libav 引擎：openVideo + Pass A 复用 extractMicroDiffBaseline + Pass B 全片扫描 + onset 判定 | ⚠️ 代码写完，**openVideo bug**（下） |
| S1 engine_test `microdiff-curve <video> <x,y,w,h> <baseStartMs> <baseDurMs>` 子命令（含 `--pure-only` 纯函数自检） | ✅ 已写，**真实素材验证未跑** |
| S2 A 图表：chartpanel.{h,cpp} +250 行（m_mdSeries 每 ROI 粗线 blkMax/细线 medD、阈值虚线、首帧微变竖线+文本、MdMark 结构、5 个插入点齐全） | ⚠️ **L1222 API bug**（下），从未编译成功 |
| S2 B .vla：timeline_model.cpp MDCF 块 + META channels 条目 + lumenarc_vla_test 往返用例 | ❌ 未动 |
| S2 C UI：microdiffdialog「计算变化率曲线」按钮+信号、MainWindow::onMicroDiffCurve、onTaskStarted 分支、“基准已采集成功”状态（需新增 3 成员 + mainwindow.cpp:505 / wiring:615 两处复位） | ❌ 未动 |

### 两个已知 bug（必修，位置精确）
1. **S1 openVideo 5s 重探测失败**（`libav_analysis_engine.cpp:320-380`）：worker 实测
   `[trace] openVideo: pix fmt NONE after fast probe, re-probing 5s` 后最终仍无 pix fmt，
   Pass B 无法解码；而同素材 **Pass A（microdiff_baseline.cpp 的 extractMicroDiffBaseline）
   打开正常** → bug 在引擎 openVideo 自己的探测/解码逻辑（嫌疑：fast_probe 副作用后未
   avformat_flush、重探测解码帧数不足、stream 索引处理）。worker 死时正在对照
   microdiff_baseline.cpp 已审查的打开段，直接照那段重写 openVideo 的 pix fmt 获取即可。
2. **S2 chartpanel.cpp:1222 不存在的 API**：`formatDisplayTime(m_cal.toDisplay(mk.value))`
   —— 成员实为 `m_calibration`，`toDisplay` 不存在；应改 `formatDisplayTime(displayMsOf(mk.value))`
   （接续 worker 死前已定位，尚未改）。同区域可能还有类似问题，修后对照接线地图第 1 节
   把 chartpanel 微变段整体过一遍。

### 构建产物状态（无一个是绿的，接管后先跑 build_lumenarc.bat 看树状态）
- `build/`：LumenArc.exe 09:57（**曲线前**版本）；lumenarc_engine_test.exe 10:52（含 S1 引擎
  代码，当时编译通过“Pass A 无警告”）；lumenarc_vla_test.exe 13:15（⚠️ 异常未来时间戳，
  不可信，用前重构建）
- `build2/`：CMake configure 完成（`build2_lumenarc.bat` 已被 worker 修正为纯 ASCII），
  无 LumenArc.exe（后台构建随 worker 阵亡；且 chartpanel bug 意味着那次构建本就会编译失败）

### worker 阵亡记录与教训
- stage1 worker（90min 预算）：死于 **API 层 "Request timed out"**（非预算），死在调试 openVideo 中途
- stage2 第一 worker：**派工时忘设 timeoutMs 吃 30min 默认超时**（父代理流程失误，同一坑两次）
- stage2 接续 worker（90min）：同样死于 "Request timed out"，死在 sed 修 chartpanel 中途
- 结论：qwen 供应在本时段不稳（scout 档位之前也因冷启动空响应被路由排除）。教训：
  ① 派工**必须显式 timeoutMs**；② 派工提示词应要求 worker **每 15 分钟把进度增量写进报告文件**
  （断点可接管，本次两个 worker 死时进度只存在于上下文里）；③ bat 含中文注释必须纯 ASCII
  （cmd.exe GBK 解读会把引号撑坏，build2_lumenarc.bat 已修）

### 剩余工作清单（按序，父代理已接管）
1. 修 chartpanel.cpp:1222（bug 2）→ `cmd //c "C:\Users\MJ\AppData\Local\Temp\microchange-research\build_lumenarc.bat"`
   确认树编译（预期还有 chartpanel 同类编译错，逐个修）
2. 修 openVideo（bug 1，对照 microdiff_baseline.cpp 打开段）→ 重建 engine_test →
   `build\Release\lumenarc_engine_test.exe microdiff-curve --pure-only`（纯函数自检：
   恒定帧无 onset / 阶跃帧 onset 在阶跃秒 / 短基准段拒绝）
3. **真实素材硬验收**：`build\Release\lumenarc_engine_test.exe microdiff-curve "C:\Users\MJ\Desktop\20260722广州增城\监控视频\明景拼接视频_20260722172528 00_33_43-01_03_41~1.mp4" 961,54,227,209 700000 180000`
   预期：noiseFloor≈3.0 / 基准段 medD≈5.1 / **onset≈936000ms±5000**（烧录时标 05:49:23）/
   onset 前 10s blkMax≈8~9、onset 后 30s 内阶跃到 ≈30+。对不上 = 算法实现错，回查 microdiff_curve.cpp
4. S2 B：timeline_model.cpp MDCF 块（**version 保持 10**，>10 被 F4 拒载；未知块已有 opaque
   保全 L963-994，旧版读新文件不崩不丢）+ META channels 条目 + lumenarc_vla_test 仿现有 3 个
   v10 用例加 MDCF 往返（接线地图第 2/6 节；CMake 不用改）
5. S2 C：microdiffdialog 按钮+信号（仅基准段已设置时可用）+ onMicroDiffCurve（照抄 onAnalyze
   装配，末行 `m_taskService->start(AnalysisChannels::microdiff(), …, MicroDiffCurveParams{baseStartMs,baseDurMs})`）
   + onTaskStarted 加分支（别掉进“亮度分析中”else）+ 基准完成状态 3 成员与两处复位
6. 全构建绿（LumenArc + lumenarc_vla_test + lumenarc_engine_test 三目标）+ 跑全部测试
7. CHANGELOG 未发布节 + 本节状态更新 → 交用户 UI 验证（6 步流程见 §92，曲线追加：
   面板点「计算变化率曲线」→ 约 3~4 分钟进度 → 图表出现 blkMax/medD 双线 + 阈值虚线 +
   “首帧微变 05:49:23”竖线标记）

### 文件清单（本功能）
- 新建：`src/domain/microdiff_curve.{h,cpp}`、`src/domain/microdiff_core.{h,cpp}`（§92）、
  `src/infrastructure/microdiff_baseline.{h,cpp}`（§92）
- 修改：`src/domain/analysis_snapshot.h`、`src/infrastructure/ianalysis_engine.h`、
  `src/infrastructure/libav_analysis_engine.{h,cpp}`、`src/app/analysis_controller.cpp`、
  `src/app/analysis_task_service.{h,cpp}`、`src/chartpanel.{h,cpp}`、`tests/engine_test_main.cpp`、
  `CMakeLists.txt`
- 未动（待 S2 B/C）：`src/domain/timeline_model.cpp`、`src/microdiffdialog.{h,cpp}`、
  `src/mainwindow.{h,cpp}`、`src/mainwindow_wiring.cpp`（C 部分的 onMicroDiffCurve 等）、
  `tests/vla_load_test_main.cpp`

### 接管结果（2026-09-11 收尾，父代理亲自完成）
§93 上面是 11:00 的断点快照；**以下为接管后全部完成**，两段功能均已落地并验证。

**1. 真凶不是 openVideo（stage1 死前误判）**。openVideo 实际正常（trace 显示 5s 重探测后
`final pix fmt=0` = YUV420P）。引擎首帧崩溃的真因是 **`microdiff_curve.cpp` 的
`roiFrameStats` 8×8 分块索引越界写堆**：`nx=ny=8, bw=w/8, bh=h/8`，但 `by=y/bh` 在
y=208（h=209）、`bx=x/bw` 在 x=224..226（w=227）时取到 8，`blkSum[8*8+8]` 写到只有 64 元素
的 vector 之外 → 首帧即堆破坏崩溃（16×16 纯函数自检因整除而漏检）。修法：
`qMin(nx-1, x/bw)` / `qMin(ny-1, y/bh)` 钳制 + 补 227×209 非整除回归用例。

**2. 第二个 bug（接管时发现，语义级）**：`detectOnset` 从视频第 0 秒起找首次过阈，
而本素材横跨日出、基准段之前画面与基准差异巨大（首秒 blkMax≈54），导致首帧微变被误判为
t=0。修法：首帧微变只在**基准段结束之后**判定（基准段是参照系，之前时段用户并未标记为
干净）；纯函数补“基准段前阶跃不得报”用例。

**3. 真实素材硬验收通过**（明景拼接 2560×1440@20fps，ROI 961,54,227,209，基准 700000~880000ms）：
```
seconds=1799  mu=8.316 sigma=1.096 threshold=11.605
onset: tsMs=936000 direction=1
[onset-10s] 926000ms blkMax= 9.23   [onset -1s] 935000ms blkMax= 9.39
[onset +0s] 936000ms blkMax=11.85   [onset +4s] 940000ms blkMax=31.12
```
→ onset=936000ms = 烧录 05:49:23，与 Python 原型标定**逐秒一致**；干净段 μ 与原型 8.8 吻合。

**4. S2 A 图表**：修 `chartpanel.cpp:1222` 不存在的 API（`m_cal.toDisplay` →
`displayMsOf`+`formatDisplayTime`）、`clearMdSeries` 值/指针遍历错（`auto *mk` →
`const MdMark &mk`）；另新增无头图表渲染用例（**纯 microdiff 快照**——正是会撞三处早退
陷阱的那条路径）→ `[chart-md] series=2 points=5 render=1200x400 => PASS`。

**5. S2 B .vla**：新增 `MDCF` 块（**version 保持 10**；未知块 opaque 字节保全已存在）+
META channels 声明 kind=microdiff + 读路径对称解析（上限防御 roiCount≤1024 /
secondCount≤1e6）；`lumenarc_vla_test` 新增 MDCF 往返 → **57 项检查 0 失败**。

**6. S2 C UI**：面板新增「计算变化率曲线」按钮（基准采集成功才解锁，`setBaselineReady`）+
`curveRequested` 信号 + `MainWindow::onMicroDiffCurve`（校验视频/基准/ROI → 装配 ROI →
`taskService->start(microdiff, …, IAnalysisEngine::MicroDiffCurveParams{baseStartMs,baseDurMs})`）
+ `onTaskStarted` 加微变分支（不再掉进“亮度分析中”）+ 基准就绪状态 3 成员与
openVideoFile / 清空列表两处复位。

**7. 构建与测试**：`LumenArc` / `lumenarc_vla_test` / `lumenarc_engine_test` /
`lumenarc_ui_chain_test` 四目标 Release 全绿；vla 测试 0 FAIL；engine 纯函数 6 项 PASS。

**教训补一条**：stage1 的“openVideo 5s 失败”是**在错误假设上耗掉了整个预算**——它握着
真实素材（一次 `microdiff-curve` 实跑只要 4 分钟就能定位到 roiFrameStats），却选择反复
重读打开代码。下次遇到“某函数失败”，**先用真实输入跑一次拿现象/栈，再读代码**。

### 状态（接管后）
- ✅ 数据层 + UI 层全部实现并编译通过（四目标绿）；曲线数值与原型标定逐秒一致
- ✅ 无头可验证项全绿（纯函数 / MDCF 往返 / 微变图表渲染）
- ⏳ **待用户界面验证**（无法无头验证）：① 面板「计算变化率曲线」按钮仅在基准采集成功后
  解锁；② 点击后进度条 0→100%（约 3~4 分钟），状态栏显示“正在计算变化率曲线”；③ 完成后
  图表出现每 ROI 两条线（blkMax 粗 / medD 细）+ 水平虚线（μ+3σ）+ 红竖线（首帧微变
  05:49:23）；④ 保存工程后重开曲线仍在（MDCF 块）；⑤ 取消按钮能中断（引擎协作取消）
- ❗ 未做：CHANGELOG 版本号（仍是“未发布”）——按惯例等用户验证通过后定版

### 状态（11:00 断点快照，保留作历史）

## 92. P-81 微变分析落地（2026-09-10）

### 背景
火调行业"微变分析"（应急管理部天津消防研究所+天津大学+海康"火察"同类原理：央视
《危机现场》第 4 集展示"微变视频分析展现烟气流动的过程"）。落地前先用 Python 原型在
**真实案件素材**（2026-07-22 广州增城，明景拼接 2560×1440/20fps/3.16Mbps/30min）上
做了可行性实测，关键结论直接决定了参数与首版取舍：

- 固定基准不可用：30 分钟跨日出，全域漂移累积 **36 灰度级** ≫ 烟信号 2~5 级 → 必须用户标记干净基准段。
- 滚动基准可压回 2~3 级，但会**吸收渐进式变化**，检出比固定基准晚约 90 秒 → 首版选固定基准。
- 光照是**空间非均匀**的，仅减全局中值不够；但用全画面均值做增益补偿会**抵消烟信号**
  （烟使全画变暗）→ 首版**不做光照补偿**。
- 单帧原始偏差直接放大 = 纯 H.264 块噪声 → 必须时域一致性 + 空间低通（v1 曾漏，观感无法对标）。
- ROI 定向（227×209）实测：干净基准段噪声 med|D| = 3.0 级；烟在 **05:49:23** 首次持续
  越过 3σ（最活跃 8×8 分块 8.8→31.7），与调查员肉眼判断（05:49:00）基本同步。
- 用户明确要求：原视须**保留彩色**，不做灰度显示。

### 新增文件
| 文件 | 职责 |
|---|---|
| `src/domain/microdiff_core.{h,cpp}` | 纯计算核心（无 QObject/Widgets）：中值基准、时域环缓冲、盒式低通、JET LUT、噪声基底标定 |
| `src/microdiff.{h,cpp}` | 显示层 QImage 胶水：`computeMicroDiff`（每帧推进环缓冲）/ `renderMicroDiff`（纯渲染） |
| `src/infrastructure/microdiff_baseline.{h,cpp}` | 独立 libav 解码一遍算基准与噪声基底（惰性 sws 建表，P-55 同规） |
| `src/microdiffdialog.{h,cpp}` | 非模态设置面板（等级/强度/模式/范围/时域窗 + 基准段采集与进度） |

修改：`videowidget.{h,cpp}`（显示链末级）、`mainwindow.{h,cpp}`（编排/ROI 解析/基准采集）、
`mainwindow_ui.cpp`（工具栏按钮）、`mainwindow_wiring.cpp`（接线+启用禁用）、`CMakeLists.txt`。

### 关键设计决策
1. **两段式 API**（重要）：时域环缓冲必须**每帧只推进一次**。`computeMicroDiff` 在
   `onFrameReady` 调（推进环缓冲）；`renderMicroDiff` 在 `rebuildAdjustedFrame` 调（纯函数）。
   这样拖亮度滑杆/改旋转/重绘都不会污染时域平均。
2. **做在共用显示链上**（与 displayadjust 同级）：旋镜/钉图/全屏/证据快照共用该链，
   因此"微变局部放大"零额外开发自动获得。
3. **基准独立解码一遍**：不改 `ffmpeg_video_engine` 的播放状态机（注入同步解码风险高）。
4. **坐标顺序**：`原始帧 → renderMicroDiff → 旋转 → LUT`。ROI 与基准都定义在**原始坐标系**，
   先叠加后旋转才能保证任何旋转档位不错位。
5. **噪声基底放渲染段**：扣基底/乘增益都在 render，所以暂停时拖等级滑杆**即时生效**。
6. 取证红线：仅影响显示与证据快照；分析数据/ROI/导出证据仍走原始帧（与画面调节同规）。

### 首版参数（实测标定）
等级 1..5 → 增益 20/15/11.25/8.44/6.33；时域窗 11 帧；空间 σ=5；噪声基底由基准段自标定。

### 测试方法（用户可观察步骤）
1. 打开一段有早期烟/火的监控（例：`明景拼接视频_20260722172528 00_33_43-01_03_41~1.mp4`）。
2. 把播放头拖到**起火前**（例 05:48），工具栏点「微变分析」，面板里把基准段设为
   05:45:27 起 120 秒，点「采集基准」（约 16 秒，进度条+取消）。
3. 在画面上圈出怀疑区域（例 x=961 y=54 w=227 h=209 的"友谊专业空调"铺面），
   勾「启用微变分析」，处理范围选「仅当前 ROI 区域」。
4. 预期：画面基本不变；把播放头拖到 **05:49:27** 附近，ROI 内出现黄→红色云团并随
   烟扩散（05:53 后大片深红）。等级调到 1 更敏感、调到 5 更保守；叠加强度 0 时不染色。
5. 反向验证：基准段设错（含烟）时应**看不出**微变或效果明显变差。
6. 回归：关闭微变后画面与之前逐位一致；亮度/音频分析、ROI、导出不受影响。

### 代码审查与修复（2026-09-10 同批）
reviewer 审查后修复（均已落地并随本批编译通过）：
- **F-1** 换视频/清空列表未清基准 → `openVideoFile` 与清空列表 handler 各加 `clearMicroDiffBaseline()`
  （否则同分辨率的下一个视频与旧基准求差 → 整屏假阳性）
- **F-2** seek/逐帧未重置时域环 → `scrubEnded`（图表+语谱）、`onSeekFromChart` 非拖拽分支、
  `Key_Left/Right` 逐帧均加 `resetMicroDiffTemporal()`（否则拖时间轴即出假变化云）
- **F-3** 基准提取（同步约 16s）期间可切视频 → 返回后加 `currentVideoPath()==path` 守卫
- **F-4** 并排对比模式 overlay 错位 2×（且 ROI 落盘坐标会错）→ `videoDisplayRect()` 该模式下取左半
- **F-5** 放大镜/钉图/副屏全屏拿不到微变（原文档承诺未实现）→ 新增
  `VideoWidget::applyMicroDiffTo()`（纯渲染、不推进环缓冲），wiring 转发已叠加微变帧；
  连接顺序上 VideoWidget 先接 `frameReady`（setVideoEngine 时），故放大镜取到的缓存帧与本帧同步
- P2-1 中值基准先校验后分配；P2-2 时域累加改 int32；P2-3 纠正两处显示链顺序注释；
  P2-5 `setMicroDiffBaseline` 补尺寸契约；P2-6 基准提取一并锁定像素格式；P2-7 seek 失败不再静默；
  P2-8 ROI 变更推送去掉 isVisible 守卫；P2-9 `State::configure` 加环缓冲内存上限（超限自动降时域窗）

### 未完成的下一件事：变化率曲线（本轮中止）
已派 worker 实现"微变变化率曲线"（新 `microdiff` 分析通道 + 图表曲线 + 首帧微变判定 + .vla），
**worker 30 分钟超时，产物为不可用半成品**：只写了 `analysis_snapshot.h`/`timeline_model.cpp`
的一部分与 `src/domain/microdiff_curve.{h,cpp}`，且 engine/controller/task-service/chart 均未接线、
CMake 未登记，且 `analysis_snapshot.h` 的 `setMicroDiff` 有参数重名编译错误（`sigma` 同时作
`double` 与 `QList<double>`）。**已整体 `git checkout` 回退 + 删除 microdiff_curve.*，树已干净**。

下次重开建议（避免重蹈超时）：
1. **拆成两次交付**，先只做"引擎产出曲线数据 + 控制台/测试可见"，再做图表与 .vla；不要把
   通道/引擎/图表/持久化/UI 一次塞给一个子代理（本轮就是因为 brief 过大 + 中途追加审查修复而超时）。
2. 判据与统计量已有实测标定，直接用：每 ROI 行 `blkMax`（最灵敏）与 `medD`；
   单位=灰度级（不乘显示增益）；首帧判据 = `stat > 干净段 μ+3σ 且连续≥3 秒`；
   方向 `signedD<0` 为烟挡光、`>0` 为火光；输出需双时标。
3. 参考真实标的：真素材实测首帧微变在 05:49:23（blkMax 8.8→31.7），可与肉眼判断互校。

### 状态
- ✅ 编译 + 链接通过（MSVC Release x64；`build/Release/LumenArc.exe` 2026-09-10 18:52，
  8814080 字节；`lumenarc_ui_chain_test` 目标也通过）
- ✅ 代码审查已完成，必修项 F-1~F-5 与 P2 已全部落地并重新编译通过
- ⏳ **未运行验证**：上表 6 步测试步骤待执行（本版已含全部修复，可直接用于验证）
- ❗ 下次开工：跑上表 6 步验证；确认观感后再做变化率曲线（按上面 3 条建议重开）

---

## 91. v1.17.0 MainWindow 拆解收官：四阶段五提交（P-76~P-80 全勾销）

- **背景**：v1.9 拆 app 层后 UI 壳复胖至 mainwindow.cpp 4770 行 + R3/R5/Q5
  三笔红线债。方案 `docs/DEVELOPMENT_PLAN_V1.17_CN.md`（2026-09-06 用户 Q1~Q7
  全按推荐拍板）；回退点 `safety/pre-mw-split-20260906` 标签 @ `7d66ce0`。
- **阶段 A（`e99c947`）multi-TU 拆分**（P-79）：4770 行 → 7 翻译单元
  （core 1819 / ui 1532 / wiring 640 / case 555 / magnifier 343 / snapshot 389 /
  export 266）；单类 API 零变化（public 区零 diff 门槛）；buildStamp 迁
  build_stamp.h inline。纯移动零重写，头文件只 +private 声明。
- **阶段 B（`01ad822`）红线债收口**：B1 ChartPanel::xAxisRange() 只读 getter
  消 R3 穿透；B2 currentVideoPath → VideoSessionManager SSOT + PlaybackSettings
  组件（R5，47+10+3 处收口，三条边界不变量保留并有 testCurrentPathSsot 闸）；
  B3 KeyGuardFilter 守卫+转发（Q5，18 键 switch 搬 handleGlobalShortcut，
  禁 QShortcut 拍板保留）。
- **阶段 C（`e30d534`）SnapshotComposer**（P-80）：C0 六个渲染函数自
  OverlayWidget 静态成员迁 FrameAnnotation 纯模块（R1 方向修正，含
  segment_export_engine/ui_chain 调用方）；C1 SnapshotInputs 输入集闭合，
  onSnapshotQuick 280 行 → 95 行薄壳；C2 lumenarc_snapshot_test 像素断言
  36 checks（映射单元/OSD/分段几何/分屏/PNG 元数据）。
- **阶段 D（`0e4e2e5`）切换/恢复闸 + 扇出瘦身**：先补 testSwitchRestore
  （双合成视频 a→b→a，Key_A/Key_B 端到端）——**逮住 B3 遗留真 bug**：
  KeyGuardFilter 只写 filterKeyPress 未 override eventFilter 本体，8 处
  installEventFilter 改挂后全局快捷键实际失效（B 阶段无键路测试漏过）；
  修复后 openVideoFile 恢复/清空扇出抽 applyRestoredState/resetForNewVideo/
  enableVideoActions（305 → ~150 行，行为冻结由 D1 闸验证）。
- **回归基线**：19 套全绿（见表头）；版本 bump 1.17.0（CMake 单一真源）；
  PENDING P-76~P-80 勾销。
- **真机点检**：✅ 2026-09-07 用户确认全部通过（RELEASE_CHECKLIST_V1.17_CN.md
  10 项：行为冻结 7 条 + 快捷键 9 键 + ▶ 按钮/标题版本 2 项）→ **v1.17.0 封板**。

## 90. P2.8 实测修订：聚光灯 50% 上限 / 条带全量化+语谱 / 打开输出文件夹

- **聚光灯**：放大终点从满屏改为居中 50% 面积（边长 ×0.7071，保持聚焦框宽高比）；
  变暗不再随放满撤销（全程保持，聚焦区提亮覆盖）；聚焦框边常驻。
- **曲线条全量化**（用户："跟主视频窗一样，旧版导出已实现"）：drawChartStrip 签名
  改 (cursorMs, rangeStartMs, rangeEndMs)——整段铺显不滚动不缩放，游标=白线贯穿
  移动；新增语谱热力带（spectrogram[freq][time]→40% 高热力条，低频在下，
  蓝→青→黄→红简易色带，specMin/Max 归一）；曲线区=亮度+音量+标签竖标。
  引擎调用传 seg.inMs/outMs。
- **导出完成**：进度行「📂 打开输出文件夹」按钮现身（QDesktopServices 开目录）。
- 测试：segment 120 全绿（条带调用改新签名，游标中点断言）；全回归 9 套绿；
  手册/PDF/包重出。

## 89. 工作台 P2.7：切割/倍速/ETA/编码提速 + 标注轨 v1（聚光灯/箭头/字幕）

- **用户实测反馈六连**：①要切割按钮 ②倍速要更简便 ③导出慢 ④进度要已用/预计
  ⑤导出是否无损 ⑥要聚光灯/箭头/字幕轨。
- **切割**：✂按钮+Ctrl+B（剪映同款），预览位置严格落段内（两端≥200ms）才可切；
  两半继承素材/倍速/宫格；标注按切点分家（跨界标注两边各留夹取副本）。
- **倍速简化**：块右键「倍速」子菜单 ×0.5/1/1.25/1.5/2/4（当前档打勾）+双击自定义不变。
- **编码提速**（慢的根因=软编 medium）：pickH264EncoderFast——候选逐一**实跑冒烟**
  （testsrc2 1s→null，防 nvenc 在名单但无驱动运行期炸）→ h264_nvenc(p4/cq21)
  → libx264 veryfast/crf18 → openh264 → h264_mf；作用于 runCompose+旧复合路径；
  **runMultiCam 冻结路径刻意不动**。本机 RTX 5080 冒烟 nvenc rc=0。
- **ETA**：setExportRunning 起 QElapsedTimer，setProgress 报「已用 m:ss · 预计剩余 m:ss」。
- **标注轨 v1**：ComposeAnno{Spotlight,Arrow,Caption; inMs/outMs 源域; rect 归一化;
  text; colorRgb} 挂 ComposeSeg.annos；引擎单视频段逐帧烧录（compose_render
  drawAnnotations：聚光灯=剩余区 145α 变暗淡入淡出+聚焦框 smoothstep 放大至满幅；
  箭头=起→止 5px+三角头；字幕=底部黑带白字 64px 上偏移避 OSD/曲线条）。
  UI：标注条三钮（预览位置落单视频段才亮）→ 🎯/↗ 起 AnnoPickOverlay 拖框
  （CamTileWidget 新增 videoFitRect 公开映射，zoom>1 先复位提示）→ 弹窗起止/颜色/
  文本；时间线块上方 chips 行（🎯↗💬，右键删）；💬免框选直弹窗。证据模式有标注→
  黄字提示不携带；有标注段自动绕开旧复合全保真路径（走新管线才烧得出）。
- **测试**：segment 120 全绿（标注 e2e：字幕亮像素/红箭头像素/聚光灯四角压暗Δ≥20，
  KEEP_ANNO_FRAMES 调试位）；mw 110 全绿（切割 e2e：直发 sliderMoved——
  **setValue 不发 sliderMoved 信号**，qWait 等 seek；Space 从 smoke 撤下防真播放漂移）。
- **陷阱**：offscreen 环境无 CJK 字体→字幕断言用 ASCII；ffmpeg 抽帧 -ss 放 -i 后
  （精确 seek 防尾帧空帧）。

## 88. 合成导出 P2.6 收官：覆盖条/部分覆盖音轨/播放头联动 + v1.16.2 打包排雷

- **宫格段覆盖条**（runCompose lanes 分支）：画面顶部每路一行 3px 彩条（段内覆盖
  区间=syncLaneWallStart/End ∩ [in,out]，Theme::DataPalette 与机位名同色）+白竖线
  游标；右上水印 350px 让位。无画面格本就有「该时刻无画面」占位（§85）。
- **部分覆盖音轨细分**：新增 `AudioSegPart{label,inMs,outMs,rate}` +
  `buildAudioFilterChainV2`（段=子片序列：有源片 atrim/atempo/aresample 归一，
  盲区片 anullsrc 等长静音，段内 concat 再段间 concat）；runCompose 宫格段映射改为
  盲区头/有源中/盲区尾三片（全盖/全盲退化为单片，单视频段单片不变——旧
  buildAudioFilterChainMulti 保留供既有断言）。**e2e 实锤**：LAMerged 主听路只盖
  前半 → ffprobe astats 覆盖区 RMS -65dB（监控音本低）vs 盲区 -120dB 死寂。
- **播放头联动**：ComposeTimelineWidget::setPlaySeg——预览位置落入段源区间时块顶
  画 ▼（单路按 sourcePath+in/out 匹配，多通道按墙钟覆盖）。
- **v1.16.2 打包排雷**：pack_release.py 此前 EXCLUDE_DIRS 仅 cases → mlt/ 385 条目
  ~160MB 混进 zip；+mlt 排除后 v1.16.2 包 290MB、mlt 0 条目、必含 13/13。
  CMakeLists project(VERSION) → 1.16.2。
- **测试**：segment 115 全绿（V2 链 7 断言+部分覆盖 RMS e2e）；全回归 9 套绿。
- **教训**：ffmpeg astats 判静音用 "-inf" 解析；监控源 RMS 绝对值低，须用
  覆盖/盲区差值判定（≥25dB）而非绝对阈值。

## 87. 工作台四步引导改版（新手向重构）+ 快捷键对齐剪映/PR

- **缘起**：用户实测"打开视频编辑页无所适从"——拍板新手向重构（用户补充：
  I/O 保留给提示+快捷键尽量对齐剪映/PR）。
- **四步引导条**（顶部常驻）：①选素材→②截片段→③排顺序→④导出，当前步蓝底
  高亮、完成步绿色；右侧一句白话动态提示随状态机切换（updateGuide()：
  无素材→提示选素材 / 有素材无片段→提示截取键位 / 有片段→提示排序导出）。
- **截取改版**：红色圆钮录音笔式单键流（⏺从这里开始(I) → ⏹到这里加入清单(O)，
  armed 态变亮红）；I/O 按钮保留并标快捷键；onMarkOut 设终点即提交（一拍成片）；
  提交后打点自动清零；开始导出钮无片段时禁用（可观测态）。
- **快捷键**（QShortcut WindowShortcut + 输入框聚焦守卫；运输控件全部 NoFocus
  防空格被按钮吃掉）：空格/K=播放暂停、I=起点、O=终点并加入、回车=等价O、
  ←/→=逐帧(按 fps)、Shift+←/→=±1s、J/L=±5s、Home/End、Delete=删选中块、
  Ctrl+E=开始导出。
- **导出面板**：白话二选一（演示片——带角标红标用于汇报 / 证据原始片段——零改动
  用于存档送检）+ tooltip 解释；OSD/案号/图表/ROI/曲线收进「更多选项▸」折叠面板；
  「保存到」+「更改…」+大蓝钮「开始导出（Ctrl+E）」。
- **陷阱**：QStringLiteral 不能包运行时三元表达式（编译错）；工作台 onMaterialChanged
  读 currentItem——测试里 emit itemClicked 前须 setCurrentItem；slider seek 异部，
  提交前须等 positionChanged（测试 qWait 1500）。
- **测试**：mw 107 全绿（工作台引导流 e2e：点视频→红点 arm→seek→提交→导出钮亮；
  快捷键 smoke 不崩）；手册七·2 整节重写（四步流+快捷键表）+PDF 重出。

