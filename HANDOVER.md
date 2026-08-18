# LumenArc 工作交接文档（HANDOVER）

> **本文件只保留最近 5 次更新**；更早记录依修改顺序（时间序）存档于
> **WORK_HISTORY.md**（规则 R2）。

## 表头（每次写完 HANDOVER 与 WORK_HISTORY 后必须同步更新本表头——规则 R2）

- **当前 HEAD**：施工批（2026-08-18 §52 P-57 多机同步播放施工落地；版本 v1.10.0）
- **构建**：`cmd //c "build_tmp\build_target.bat ALL"`；测试：`QT_QPA_PLATFORM=offscreen`
  + PATH 含 `C:\code\Qt\6.8.0\msvc2022_64\bin`（配置：`build_tmp\reconfigure.bat`）
- **全回归基线**（13 套，v1.10.0 施工批）：mw **36**（+P-57 UI 链 10，LUMENARC_REPRO_VIDEO
  门控 +1）/ task_registry 41 / case 248 / case_e2e 51 / piecewise 96 / preprocess 176 /
  ui_chain 92 / calibration 77 / roi_model 23 / vla 54 / libav 25 / v17 34 /
  **sync 78（P-57 新套件）**
- **当前保留批次**（新→旧，R2 限 5 批）：
  第四十三批 §52（P-57 多机同步播放施工落地，版本→1.10.0）·
  第四十二批 §51（P-55 勾销 + 音量曲线 70% + 调研草案 P-57/P-58）·
  第四十一批 §50（P-55 闪退根除：音轨前导吃满探测窗→pix_fmt NONE→sws 断言；按帧属性惰性建表）·
  第四十批 §49（亮度分析闪退排查，P-55 待复现信息）·
  第三十九批 §48（P-31 拆分四组件+R3/R5 收口，版本→1.9.0）
- **最近归档动作**：2026-08-18 §52 批——第三十八批（§47）移入 WORK_HISTORY.md 末尾；
  早前：第三十七批（§46，§51 批）、第三十五批（§44，§49 批）、第三十一批（§40）、
  第三十批（§39）、第五批（§14）至第二十九批（§38）共 25 批整体移入（原文未删改）。
- **常用参考导航**（已归档，查 WORK_HISTORY.md）：显示旋转 90° 方案 A
  （第三批 §12）、音频时间轴对齐与问题 A/B 定案（深夜批）、项目规则 R1/R2
  （08-13 晚批）、架构分层与红线 R 规则（二章）、构建部署 CI（九章）、
  升级计划表 v1.2~v1.9（十章）、测试体系 28 项矩阵（七章）、校时管线
  救复速览（〇章）、案件模块 M1-M3（二十三章）。
- **管理文档**（2026-08-16 §44 建立）：待办唯一登记处 `docs/PENDING.md`；
  文档体系与维护规矩 `docs/DOCS_MAP.md`（规矩 D1-D8，待办必登记 PENDING）。
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
