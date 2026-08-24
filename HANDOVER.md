# LumenArc 工作交接文档（HANDOVER）

> **本文件只保留最近 5 次更新**；更早记录依修改顺序（时间序）存档于
> **WORK_HISTORY.md**（规则 R2）。

## 表头（每次写完 HANDOVER 与 WORK_HISTORY 后必须同步更新本表头——规则 R2）

- **当前 HEAD**：施工批（2026-08-22 §73 P-69 编号合并轨落地，
  版本 **v1.14.0**）
- **构建**：`cmd //c "build_tmp\build_target.bat ALL"`；测试：`QT_QPA_PLATFORM=offscreen`
  + PATH 含 `C:\code\Qt\6.8.0\msvc2022_64\bin`（配置：`build_tmp\reconfigure.bat`）
- **全回归基线**（**17 套**，v1.14.0 施工批）：mw 97 / task_registry 41 / case 248 /
  case_e2e 51 / piecewise 129 / preprocess 268 / ui_chain 97 /
  calibration 99 / roi_model 23 /
  vla v10 累计 40 + gaptick 4 + gapcollide 2 / libav 32（无 caltest 素材
  环境跑 20）/ v17 37 / **sync 193（含 P-69 合并轨 44 + P-73 引导 9/可听集 5）**/ sidecar 34 / segment 54 / **docx 23 + report 49（P-28 批次①②③）**
- **当前保留批次**（新→旧，R2 限 5 批）：
  第六十六批 §75（P-28 批次②：报告聚合+章节映射+生成入口，版本→1.14.0）·
  第六十五批 §74（P-28 报告模块批次①：DOCX 地基 + P-74 点位图方案）·
  第六十四批 §73（P-69 编号合并轨，版本→1.13.3）·
  第六十三批 §72（P-73 多机同事件间接校时，版本→1.13.2）·
  第六十二批 §71（放大镜布局重构+案件折叠条+启动比例，版本→1.13.1）。
- **最近归档动作**：2026-08-23 §75 批——第六十一批（§70）移入 WORK_HISTORY.md 末尾；
  早前：§74 批——第六十批（§69）移入；
  早前：§72 批——第五十八批（§67）移入。
- **常用参考导航**（已归档，查 WORK_HISTORY.md）：机位勾选面板（§54）、
  校时落盘双根因修复（§55）、显示旋转 90° 方案 A
  （第三批 §12）、音频时间轴对齐与问题 A/B 定案（深夜批）、项目规则 R1/R2
  （08-13 晚批）、架构分层与红线 R 规则（二章）、构建部署 CI（九章）、
  升级计划表 v1.2~v1.9（十章）、测试体系 28 项矩阵（七章）、校时管线
  救复速览（〇章）、案件模块 M1-M3（二十三章）。
- **管理文档**（2026-08-16 §44 建立）：待办唯一登记处 `docs/PENDING.md`；
  文档体系与维护规矩 `docs/DOCS_MAP.md`（规矩 D1-D8，待办必登记 PENDING）。
# ============================================================================
# 工作记录（2026-08-22，第六十三批）——P-73 多机同事件间接校时（v1.13.2）
# ============================================================================

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
