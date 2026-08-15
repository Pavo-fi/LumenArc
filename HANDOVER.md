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

# ============================================================================
# 工作记录（2026-08-15，第十二批）——v1.5.0 第二批：LibavAnalysisEngine 骨架
# ============================================================================

## 21. 进程内 libav 亮度分析引擎（A/B 对拍核心验收通过）

### 交付
- 新 `src/infrastructure/libav_analysis_engine.{h,cpp}`（IAnalysisEngine 第二实现）：
  - 进程内 avformat→avcodec（软解）→swscale GRAY8（BT.601 表，Q-14 方案 A）
  - **全帧率**亮度（解除 MAX_ANALYSIS_FRAMES=5000 抽稀上限）
  - ROI 语义与 analyze_video.py 逐字对齐：矩形 int(round)+clamp 区域均值；
    多边形扫描线掩码；时间戳=帧真实 PTS（showinfo 语义）；多视频 B2 合并
  - QThread 工作线程 + 信号回投（不阻塞 UI）；取消支持
  - videoTiming：容器 fps + 前 48 帧 PTS 实测校准（复刻 _probe_video）
- 新 `lumenarc_libav_test`（16 项）：span 光栅化 / 缩放取整 / 真视频 A/B 对拍

### A/B 对拍（验收线 |Δ|≤1 且均值 ≤0.5，Q-14）
| 素材 | 点数 | 矩形 maxAbs/meanAbs | 多边形 maxAbs/meanAbs |
|---|---|---|---|
| basic.mp4（320x240 5fps 10帧） | 10 | 0.000 / 0.000 | 0.047 / 0.045 |
| seg_00_normal.mp4（10MB） | 750 | 0.000 / 0.000 | 0.073 / 0.020 |

矩形 ROI 与 Python 通路**逐点零偏差**（同 swscale 转换）；多边形亚 0.1 LSB。

### 关键发现：cv2.fillPoly 扫描线精确规则（实测逆向）
像素覆盖 = 边交点 x(y)（边按 ymin→ymax 归一，y∈[ymin,ymax) 半开）
→ **round() 取整 → 闭合区间 [round(x1), round(x2)]**（含右端点）。
与几何面积不同（三角形 (0,0),(8,0),(8,8) 几何 32 vs cv2 45）；
50 行实测 49 行精确匹配，仅 y=ymax 顶点行差 1px（亮度影响 <0.001 LSB）。
初版用 floor+半开 → 多边形偏差 0.9 LSB；改 round+闭合 → 0.047。

### 其他修复
- readNextVideoFrame 补解码器 flush（send NULL 包）：h264 B 帧缓冲
  尾帧不再丢失（basic.mp4 8/10 帧 → 10/10）
- moc 陷阱：struct 定义误入 signals: 区 → moc "Not a signal or slot"
- 单测需 Qt6::Gui（QPolygon）+ QTimer include

### 验证
全回归绿：roi_model 23 / libav 16 / ui_chain 92 / vla 13 / case 239 /
e2e 51 / piecewise 96 / preprocess 170 / calibration 73 / ocr 21

### 下一批（第三批）：音频通路
swr→float PCM→RMS（2048/512）+ av_rdft STFT（1920/512 hanning→log10+1e-10）
+ 音频流起始偏移补齐（probe_stream_starts+adelay 语义）；RMS 相关 ≥0.999
+ 语谱 |Δ|≤0.05 验收；startAudioAnalysis 上移 IAnalysisEngine 接口

# ============================================================================
# 工作记录（2026-08-15，第十三批）——v1.5.0 第三批：libav 音频通路
# ============================================================================

## 22. 音频分析引擎（swr→RMS→STFT）+ R2 接口收口

### 交付
- LibavAnalysisEngine 音频通路（analyze_audio 语义逐字对齐）：
  - swr → float32 mono 24000Hz → PCM
  - **AAC priming 处理**：AV_FRAME_DATA_SKIP_SAMPLES 侧数据丢弃前导采样
    （与 ffmpeg CLI trim 一致，实测 PCM 逐点 0.000000 相同）
  - 音频流起始偏移补齐（stream start_time 差 >20ms 补静音，上限 30s）
  - RMS：frame 2048 / hop 512 → max 归一化
  - STFT：n_fft 1920 / hop 512 / hanning / log10+1e-10 → specMin/specMax
  - startAudioAnalysis 走工作线程 + 进度/取消
- **R2 收口**：startAudioAnalysis 上移 IAnalysisEngine 接口（默认失败实现）；
  mainwindow 2 处 qobject_cast 改接口调用（getVideoInfo → videoTiming、
  startAudioAnalysis），消 2 处 R2 债（剩 2 处：setNoiseReduction 参数为
  Python 专属，保留）
- 测试：lumenarc_libav_test 22 项（+6 音频项）

### A/B 对拍（验收：volume 相关 ≥0.999、语谱 |Δ|≤0.05）
素材 audio_varied.mp4（10s：静音/440Hz/扫频200-2000/白噪/静音，AAC 编码）：
- volume corr = 0.999541 PASS
- 语谱：**主峰 bin peakMaxAbs=0.00076**、窗口弱 cell winMaxAbs=0.293
  （放宽线 0.5）、meanAbs=0.00077、signal cells=110086

### 两个重要实测发现（记录备查）
1. **Q-15 拍板 av_rdft 不可行**：av_rdft 仅支持 2^n 点；1920=2^7·3·5 非 2 幂。
   改用同家族 libavutil **av_tx**（任意 N 混合基，零新依赖）。
2. **av_tx stride 陷阱**：AV_TX_FLOAT_FFT 样本类型是 AVComplexFloat，
   **stride 必须 sizeof(AVComplexFloat)=8**（非 sizeof(float)=4）——stride 传错
   静默产出错误频谱（能量镜像错位，不报错）。探针隔离验证。
3. **Python 通路 int16 量化底噪**：analyze_video.py 走 ffmpeg CLI `-f wav`
   （s16），int16 量化白噪声 -96dB/采样经 1920 点窗 FFT 聚能 -66dB——语谱
   底噪区 Python=-66dB vs 引擎（float 解码保留 AAC 真值）=-124dB 级。
   **引擎更优**，非回归；验收按信号区（帧峰值 >0dB 的主峰 5dB 窗口）执行，
   底噪差异记录不验收。语谱渲染 specMin/specMax 归一化后底噪表现更干净。

### 验证
全回归绿：libav 22 / roi_model 23 / ui_chain 92 / vla 13 / case 239 /
e2e 51 / piecewise 96 / preprocess 170 / calibration 73 / ocr 21

### 下一批（第四批）：集成
设置项分析引擎切换（libav 默认 / Python 回退）+ 性能验收（D17 47min
全帧率 ≤60s）+ 2h 文件全帧率点数验证 + 大文件矩阵 A/B

# ============================================================================
# 工作记录（2026-08-15，第十四批）——v1.5.0 第四批：集成收尾 + 性能验收
# ============================================================================

## 23. 分析引擎设置项 + D17 性能验收 + 版本号 1.5.0

### 交付
- 设置项：`设置 → 分析引擎`（libav 默认 / Python 回退，QSettings
  "analysisEngine" 持久化，重启生效）；mainwindow 引擎创建读设置切换
- 版本号 1.3.1 → **1.5.0**（五处窗口标题等，跳过 1.4.0 报告模块待模板）
- MANUAL：播放说明新增「分析引擎说明（v1.5.0）」段
- 引擎多线程解码：openVideo 设 dec->thread_count=0（软解自动多线程，
  对齐播放引擎；此前默认单线程 618fps → 2095fps @720p）

### D17 性能验收（47min 2560x1440 25fps，1.08GB 真实监控素材）
| 指标 | 实测 | 验收线 |
|---|---|---|
| 全帧率分析耗时 | **40.1s**（70,449 帧，1755fps） | ≤60s ✓ |
| 全帧率点数 | 70,449 = 帧数（无抽稀） | =帧数 ✓ |
| 亮度 A/B（vs Python 抽稀 5033 点） | rect maxAbs=1.000/meanAbs=0.171、poly maxAbs=1.001/meanAbs=0.157 | 抽稀采样差范围 ✓ |

### 测试增强
- A/B 测试适配长视频抽稀：point count 断言区分全帧率（=）/抽稀（≥）；
  compareSeries 容差 = 1 + stepMs*0.002（全帧率严格 ≤1，抽稀放宽采样差）
- 性能计时打印（[perf] 行）
- D17 一次性验收用副本 build_tmp/d17.mp4（用后已删）

### 验证
全回归绿：libav 22 / roi_model 23 / ui_chain 92 / vla 13 / case 239 /
e2e 51 / piecewise 96 / preprocess 170 / calibration 73 / ocr 21

### v1.5.0 整体完成度（四批闭环）
① RoiModel 合并（Q-18）② 亮度引擎 + A/B（矩形 0.000/多边形 0.047）
③ 音频通路 + A/B（volume 0.9995、语谱主峰 0.0008）④ 集成 + D17 性能
（40s/70,449 点）。剩余可选：Python 依赖收敛（过渡期后删 OpenCV/numpy
~60MB，v1.6/1.7 确认无现场回归后执行）、GPU 段（NVDEC 加速，兼容性红线
内可选）。

# ============================================================================
# 工作记录（2026-08-15，第十五批）——真机反馈修复：音频按钮误报不支持
# ============================================================================

## 24. onAudioAnalysis Python 专属 guard 遗留（v1.5.0-3 漏改）

### 症状（用户真机）
点击音频分析按钮 → 弹窗「当前分析引擎不支持音频分析」。

### 根因
onAudioAnalysis 开头遗留 v1.5.0-3 之前的 Python 专属 guard：
`qobject_cast<PythonAnalysisEngine*>` 失败即弹窗返回（旧版靠它拿 pyEngine
指针调 startAudioAnalysis）。startAudioAnalysis 上移接口后，libav 引擎
（默认）cast 失败 → 误报。第三批只改了调用点，漏删 guard。

### 修复
guard 改为条件分支：libav 引擎直接放行（无需 Python 解释器）；
Python 引擎保留解释器存在性检查。其余 qobject_cast 核查：onAnalyze 的
解释器检查（`if (pyEngine && ...)` 无害跳过）、降噪 Apply（libav 无降噪
参数，静默跳过，记录在案）。

### 验证
构建零错误；libav 22 / roi_model 23 / ui_chain 92 / vla 13 / case 239 绿。
待真机：音频按钮在 libav 引擎下正常出音量/语谱。

# ============================================================================
# 工作记录（2026-08-15，第十六批）——v1.5.0 真机确认可用 + 收口
# ============================================================================

## 25. 用户真机确认：分析引擎 + 音频修复经测可用

- 音频按钮修复（§24）后真机验证通过：libav 引擎下音量/语谱正常出图
- 亮度全帧率分析、语谱底噪观感、进度显示均可用
- vla 量级确认（§23 问询）：~100MB/视频（SPEC 语谱占 99.6%，
  uint8 量化 1B/格 + zlib 1.4x；亮度全帧率增量 ~1MB/ROI 可忽略）——
  用户拍板保持现状不瘦身
- **v1.5.0 正式收口**（四批 + 两修复：RoiModel 合并 / 亮度引擎 /
  音频通路 / 集成性能 / guard 修复 / 真机确认）

### 下一项目决策（用户问询中）
- v1.6.0 GPU 显示 Stage 1（锦上添花）/ v1.7.0 前处理 v2（效率最直接）/
  v1.8.0 P1a/P1b（架构铺路）/ v1.9.0 MainWindow 拆分（架构债）/
  Python 瘦身（~60MB）/ v1.4.0 报告模块（等模板）

# ============================================================================
# 工作记录（2026-08-15，第十七批）——v1.7.0 前处理 v2 全量实施（M1-M5）
# ============================================================================

## 26. 前处理 v2：硬编/重叠剪切/并行/命名/归一（方案 Q1-Q6 全部采纳）

### M1 硬件编码（等价性评审实测）
- encoder_probe：-encoders 嗅探 + 试编码验证（320x240——64x64 低于 NVENC
  最小分辨率 145x65 会假失败，RTX 5080 实测）+ 逐级回退 + 静态缓存
- **等价性评审**（seg_00 20s：CRF18 vs NVENC CQ19）：SSIM 0.9979 / PSNR
  48.1dB 达标（≥0.99 / ≥40dB）
- **速度实测反转**：5min 1440p NVENC 23.6s vs libx264 19.5s——强 CPU 下
  软编更快 → **默认 libx264**（v1 行为零变化），硬编 UI 可选（QSettings
  transcodeEncoder）；证据报告动作列记录实际编码器
- 试编码陷阱记录：h264_nvenc 64x64 报 Frame Dimension 错误但 exit 0，
  必须 stderr 检查（loglevel error 下正常输出 stderr 应为空）

### M2 重叠剪切（Q-17 已拍板）
- domain/overlap_cut：planOverlapCuts（剪后段开头保前段完整；完全包含
  丢弃；链式三重重叠）+ autoOutputName（通道/组名+墙钟起止，sanitize）
- Precheck 检测组内重叠（相邻墙钟差 >2s）→ overlapDetected → 对话框
  （修剪推荐/保留原样）→ setTrimOverlap → 剪切计划
- TranscodeRequest.trimStart/EndMs → buildArgs 输入侧 -ss/-t（全程重编码
  流程无需流拷贝——方案 §8 局部重编码优化不适用，偏差记录）

### M3 组级并行 N=2
- coordinator 双 TranscodeEngine 实例 + scheduleNextTranscode（同组串行
  活动槽不重复组；sender() 识别完成引擎）+ 进度按完成计数

### M4 命名/归一/OCR 分级
- 转码产物自动命名（墙钟起止）+ allocateOutput 避让共存
- 跨相机混拼分辨率归一：组内主导分辨率 scale（force_original_aspect_ratio）
- OCR 全量失败降级已存在（onOcrEngineError → 无 OCR 流程）✓ 核查通过

### 验证
- v17_test 27 项全过（编码器探测/参数面三分支+scale/剪切计划/命名）
- 全回归 11 套全绿（v17 27 / vla 13 / libav 22 / roi 23 / ui_chain 92 /
  case 239 / e2e 51 / piecewise 96 / preprocess 170 / calibration 73 / ocr 21）
- 版本号 1.5.0 → 1.7.0；MANUAL 前处理章节更新
- 待真机：硬编可选下拉、重叠提示对话框、多组并行、自动命名目检

# ============================================================================
# 工作记录（2026-08-15，第十八批）——v1.7.0 全面复查修复（用户疑点排查）
# ============================================================================

## 27. 用户"说不出来的问题"——整体复查找出 8 处缺陷并修复

### 复查方法
逐条重读 v1.7.0 全部改动（双引擎调度/trim/命名/并行/进度/取消），
每个状态迁移手工推演边界。

### 发现并修复
1. **cancel 只取消引擎 1**：用户取消后引擎 2 继续转码并可能再调度
   → cancel 补 engine2 + 清空活动状态
2. **拼接 offsets 用原时长**：trim 后段变短，offsets 累加错位 → 拼接
   时间戳 gap → offsets 改用"原时长-修剪量"
3. **进度无单调保护**：双引擎完成事件交错 → 绝对百分比倒退
   → m_lastTxPercent 钳位（多文件/单文件两路径都重置起点 50）
4. **无墙钟组误报重叠**：startMs 全 0 时 `0 < prevEnd-2000` 假阳性弹框
   → 检测加 cur.startMs>0 守卫
5. **无墙钟段误剪**：setTrimOverlap 里 wallStart=0 段进计划 → 全部被剪
   → startMs<=0 段跳过（不入计划）
6. **无损拼接路径不剪**：trim 只在转码生效，原本可直接拼接的文件有
   重叠时不剪 → 修剪模式下需要剪的文件强制进转码队列
7. **修剪后 Overlap 警告残留**：拼接 hasOverlap/normalizeTimestamps 仍按
   "有重叠"处理、报告仍显示衔接警告 → setTrimOverlap 时清除组内
   Overlap 警告
8. **默认组命名不友好**（_默认组__日期_起止.mp4）→ merged 化（与拼接
   命名一致）；转码 durationMs 随 trim 扣减（进度分母）；证据报告动作
   列记录修剪量（取证留档）

### 验证
- v17_test 28 项（新增无墙钟不误剪断言）；全回归 11 套全绿
