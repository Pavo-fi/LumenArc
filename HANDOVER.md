# LumenArc 工作交接文档（HANDOVER）

> **本文件只保留最近 5 次更新**；更早记录依修改顺序（时间序）存档于
> **WORK_HISTORY.md**（规则 R2）。

## 表头（每次写完 HANDOVER 与 WORK_HISTORY 后必须同步更新本表头——规则 R2）

- **当前 HEAD**：`6c9ddc9`（2026-08-15 v1.5.0-1 RoiModel 合并）
- **构建**：`cmd //c "build_tmp\build_target.bat ALL"`；测试：`QT_QPA_PLATFORM=offscreen`
  + PATH 含 `C:\code\Qt\6.8.0\msvc2022_64\bin`（配置：`build_tmp\reconfigure.bat`）
- **全回归基线**：case 239 / case_e2e 51 / piecewise 96 / preprocess 170 /
  ui_chain 92 / calibration 73 / ocr_atpositions 21 / vla 13×PASS 含 [bugA][snaprender]
  / **roi_model 23**（v1.5.0-1 新增）
- **当前保留批次**（新→旧）：
  第十一批 §20（v1.5.0 首批：RoiModel 合并，Q-18，`6c9ddc9`）·
  第十批 §19（开发副本建立：v1.3.0 工作区，原仓库冻结备份）·
  第九批 §18（真机终验全过：快照全图 + 滑杆/OSD/4K 积压项，§14 收口）·
  第八批 §17（快照 v2 两连修：刻度轨重建 + 语谱频率轴，`c0a7b0e`）·
  第七批 §16（快照翻车重做：离屏重渲染路线，dock resize+grab 废止）
- **最近归档动作**：2026-08-15 第十一批——第六批（§15 §14 拍板落地）移入
  WORK_HISTORY.md，原文未删改。
- **常用参考导航**（已归档，查 WORK_HISTORY.md）：显示旋转 90° 方案 A
  （第三批 §12）、音频时间轴对齐与问题 A/B 定案（深夜批）、项目规则 R1/R2
  （08-13 晚批）、架构分层与红线 R 规则（二章）、构建部署 CI（九章）、
  升级计划表 v1.2~v1.9（十章）、测试体系 28 项矩阵（七章）、校时管线
  救复速览（〇章）、案件模块 M1-M3（二十三章）。

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
