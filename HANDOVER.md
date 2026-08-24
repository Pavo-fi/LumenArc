# LumenArc 工作交接文档（HANDOVER）

> **本文件只保留最近 5 次更新**；更早记录依修改顺序（时间序）存档于
> **WORK_HISTORY.md**（规则 R2）。

## 表头（每次写完 HANDOVER 与 WORK_HISTORY 后必须同步更新本表头——规则 R2）

- **当前 HEAD**：施工批（2026-08-22 §73 P-69 编号合并轨落地，
  版本 **v1.14.0**）
- **构建**：`cmd //c "build_tmp\build_target.bat ALL"`；测试：`QT_QPA_PLATFORM=offscreen`
  + PATH 含 `C:\code\Qt\6.8.0\msvc2022_64\bin`（配置：`build_tmp\reconfigure.bat`）
- **全回归基线**（**18 套**，v1.14.0 施工批）：mw 97 / task_registry 41 / case 248 /
  case_e2e 51 / piecewise 129 / preprocess 268 / ui_chain 97 /
  calibration 99 / roi_model 23 /
  vla v10 累计 40 + gaptick 4 + gapcollide 2 / libav 32（无 caltest 素材
  环境跑 20）/ v17 37 / **sync 193（含 P-69 合并轨 44 + P-73 引导 9/可听集 5）**/ sidecar 34 / segment 54 / **docx 23 + report 49 + sitemap 16（P-28 全批+P-74）**
- **当前保留批次**（新→旧，R2 限 5 批）：
  第六十七批 §76（P-28 收尾：曲线嵌入+哈希进度+P-74 点位图落地）·
  第六十六批 §75（P-28 批次②：报告聚合+章节映射+生成入口，版本→1.14.0）·
  第六十五批 §74（P-28 报告模块批次①：DOCX 地基 + P-74 点位图方案）·
  第六十四批 §73（P-69 编号合并轨，版本→1.13.3）·
  第六十三批 §72（P-73 多机同事件间接校时，版本→1.13.2）。
- **最近归档动作**：2026-08-23 §76 批——第六十二批（§71）移入 WORK_HISTORY.md 末尾；
  早前：§75 批——第六十一批（§70）移入；
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
