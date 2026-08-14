# LumenArc 工作交接文档（HANDOVER）

> **本文件只保留最近 5 次更新**；更早记录依修改顺序（时间序）存档于
> **WORK_HISTORY.md**（规则 R2）。

## 表头（每次写完 HANDOVER 与 WORK_HISTORY 后必须同步更新本表头——规则 R2）

- **当前 HEAD**：`f046856`（2026-08-14 §15 信号发射修正；§14 落地主体 `855d5cf`）
- **构建**：`cmd //c "build_tmp\build_target.bat ALL"`；测试：`QT_QPA_PLATFORM=offscreen`
  + PATH 含 `C:\code\Qt\6.8.0\msvc2022_64\bin`
- **全回归基线**：case 239 / case_e2e 51 / piecewise 96 / preprocess 170 /
  ui_chain **92** / calibration 73 / ocr_atpositions 21 / vla 5×PASS 含 [bugA]
- **当前保留批次**（新→旧）：
  第六批 §15（§14 拍板落地：放大镜标识框 + 快照全面化，`855d5cf`）·
  第五批 §14（放大镜标识框 + 快照全面化方案拍板 Q1-Q5）·
  第四批 §13（放大镜/钉图吃调节修复 + 伽马/色阶/反色扩展，`c022d70`）·
  第三批 §12（显示旋转 90° 方案 A 落地，`eee4501`）·
  第二批 §9-11（音频时间轴对齐 + 问题 A/B 定案加固 + 播放选项包）
- **最近归档动作**：2026-08-14 第六批——第一批（2026-08-13 晚移交记录
  §0-8：项目规则 R1/R2 + 交接摘要 + 问题 A/B 排查 + 音频取证结论 +
  技术债）移入 WORK_HISTORY.md，原文未删改。
- **常用参考导航**（已归档，查 WORK_HISTORY.md）：项目规则 R1/R2 与问题
  A/B 排查史（2026-08-13 晚批）、架构分层与红线 R 规则（二章）、
  构建部署 CI（九章）、升级计划表 v1.2~v1.9（十章）、测试体系 28 项矩阵
  （七章）、校时管线救复速览（〇章）、案件模块 M1-M3（二十三章）。

---

# ============================================================================
# 移交记录（2026-08-13 深夜，第二批）——问题 A 根因已修复；问题 B 定案+加固
# ============================================================================

## 9. 音频时间轴对齐修复（2026-08-14 上午）——光标/听觉差 ~1s 根因钉死

### 用户症状
① 图表/语谱图光标进度与听到的声音差 ~1s（听觉校验）；② 语谱图与音量
峰值"有时"对不齐。

### 根因（两个独立来源，主因已钉死）
**主因·线性漂移**：`analyze_video.py` 把 `time_resolution_ms` round 到 0.1
（512/24000=21.33333→**21.3**）。C++ 实时解析路径直接采信该值
（python_analysis_engine.cpp）。误差 = 0.0333/21.3333 = **0.156% 线性漂移**：
10 分钟处 ≈ 0.94s（与用户听觉校验吻合），52 分钟视频末尾 ≈ 4.9s。
且 .vla 缓存两条加载路径（timeline_model.cpp:721/855）都是 hop/sr **精确
重算**——造成"新分析的曲线漂移、重载缓存后正常"的双轨，解释了"有时"。

**次因·恒定偏移**：分析 WAV 的时间零点 = 音频流起点；播放引擎时间零点 =
**视频流起点**（ffmpeg_video_engine m_startPtsMs，音视频包同减）。音频流
晚于视频流起始时（监控导出可达 ~1s），曲线整体提前一个恒定量。用户现有
文件实测偏移 ≈0（4时视频视频流晚 128ms，04 段全 0），属隐患而非本次主因。

### 修复
1. `analyze_video.py`：`time_resolution_ms` 全精度输出（round 6 位）；
   merged 路径同步。
2. `python_analysis_engine.cpp`：timeResolutionMs 一律由 hop/sr **精确重算**，
   JSON 字段仅作 hop/sr 缺失兜底——与两条缓存加载路径对齐，旧缓存自动愈。
3. `analyze_video.py extract_audio`：新增 `probe_stream_starts`（ffprobe 探测
   流起始）；音频晚于视频 >20ms 时 WAV 头部 `adelay` 补等量静音（上限 30s
   防病态元数据；音频早于视频不裁切，保留全部声音证据），分析时间轴与
   播放时间轴强制同原点。

### 验证
- 真实 04 段（43.6s）：res=21.333333 全精度 ✓，曲线时长 43520ms ≈ 真实
  43600ms（差 85ms = 最后一个不满窗口，预期）。
- 合成 δ=+977ms 文件：旧逻辑曲线只有 19s 且起点即声音；新逻辑 WARNING
  留痕 + 头部补静音，曲线 19947ms ≈ 视频全长 20s，首个非静音点 939ms ✓。
- 回归全绿：vla 3×PASS+[bugA] / case 239 / e2e 51 / piecewise 96 /
  preprocess 170 / ui_chain 23 / calibration 73。

### 语谱图与音量峰值对应性说明
二者同音频、同 hop、同一 timeResolutionMs，修复后同一数据源任意时刻都对齐
（窗长差 2048 vs 1920 仅 ~43ms 中心偏移，亚感知）。"有时不对应"的另一成因
是 RMS 能量峰值 vs 对数频率语谱亮带的物理差异（低频重击音量显著但语谱
宽频带分散），非缺陷。

## 10. 待办新增（2026-08-14 用户提出，方案待确认）
1. 播放调节选项包：画面旋转/亮度/对比度（复用 SnapshotOverlay 的
   applyAdjustments 与滑杆交互，详见已向用户提交的方案与问题清单）。
2. 截图快照功能（证据帧画廊入案件，供报告模块）。

## 11. 播放选项包 + 证据快照（2026-08-14 下午实施，用户六项拍板后）

### 用户拍板
2=快照所见即所得（含调节后画面）；3=文件名 `视频名_校准后北京时间.png`
（未校时 fallback `视频名_tHH-MM-SS.png`）；4=快照含曲线分析区 + OSD 烧录
当前帧标签与北京时间（如有）；5=菜单/工具栏按钮调出，调出后常驻；
6=旋转常规 90° 步进即可。Q1（旋转后 ROI 坐标语义）用户未看懂，已重新
表述待选 A/B——旋转按钮本轮未启用，待拍板后实现。

### 已实现（提交待编）
- **PlaybackAdjustPanel**（新组件 src/playbackadjustpanel.*）：dock 常驻，
  亮度/对比度滑杆（±50，与截图叠加同域同式）+ 复位；工具栏「画面调节」
  按钮开关，面板点 X 与按钮态同步。
- **VideoWidget LUT 显示变换**：`setDisplayAdjust` 预计算 256 级 LUT
  （与 applyBrightnessContrast 同公式），onFrameReady 查表；默认零开销；
  保留 m_rawFrameImage 原始帧 → 暂停态拖滑杆实时预览；调节只影响显示与
  快照，分析/ROI/语谱/证据文件不动。
- **逐视频记忆**：VideoState + displayBrightness/displayContrast，
  saveState/restoreState 全链路；无状态视频自动回默认（防泄漏）。
- **证据快照**（工具栏「快照」/快捷键 S）：当前帧（所见即所得）+ 图表
  分析区（有数据才合成）竖拼 PNG；OSD 烧录当前帧标签（±1s 内最近，金色）
  + 北京时间（已校时）/相对时刻；案件打开存 `案件/snapshots/` 并即时刷新
  dock，无案件存视频同目录 `snapshots/`；重名自动避让；快捷键速查表补 S。

### 验证
- 构建一次通过；offscreen 启动 5s 无崩溃；回归全绿（case 239 / e2e 51 /
  piecewise 96 / preprocess 170 / ui_chain 23 / calibration 73 / vla PASS
  含 [bugA]）。
- 待真机 GUI 确认：面板滑杆手感、快照合成观感、OSD 字号比例。

### Q1 已拍板（2026-08-14 用户）：选方案 A——暂不开工，仅记录方案

**拍板内容**：旋转 90° 步进（0/90/180/270），覆盖物（ROI 矩形/多边形/
辅助线/时间戳框选/放大镜/截图叠加）随画面一起转，新画的框自动换算回
原视频坐标存储与分析——证据数据始终对应同一物理位置。

**施工规格（开工时按此执行，预估 1 天 + 四角度测试）**：
1. 变换管点：VideoWidget 显示链 `原始帧 → 旋转(QImage::transformed) →
   亮度/对比度 LUT → m_frameImage`；旋转后须同步
   `overlay()->setVideoSize(旋转后尺寸)`。
2. 坐标映射（双向，OverlayWidget 核心改动）：
   - 显示向：存储坐标（原视频系）→ 旋转矩阵 → 旋转后画面系，用于画
     ROI/多边形/辅助线/标签点/截图叠加框。
   - 输入向：鼠标拖拽/框选坐标（旋转后画面系）→ 逆旋转 → 原视频系存储，
     供 Python 分析（分析永远走原始帧，不进旋转）。
   - 放大镜：源区域取原始帧坐标，放大视图内单独做旋转显示（保持放大器
     内部逻辑不动）。
   - 时间戳框选（timestampRoiReady 归一化坐标）：归一化前先做逆旋转。
3. 持久化：VideoState 增 `displayRotation`（0/90/180/270），随
   displayBrightness/Contrast 同链路存取；不入 .vla（证据文件格式不动）。
4. 快照：所见即所得——合成图视频部分用已旋转+已调节帧（现有
   currentFrame() 即得，无需额外工作）；PNG 元数据/操作日志注明旋转档。
5. 交互兜底：旋转 ≠0 时 A/B 打点、逐帧步进、拖拽 scrub 不受影响（时间轴
   与像素无关）；倍速/音量不变。
6. 验收矩阵：四角度 ×（画矩形/画多边形/辅助线/时间戳框选/放大镜/截图
   叠加/快照/切换视频状态恢复）+ 4K 与 1440p 性能抽查（transformed 一次
   拷贝 ~10-20ms，仅旋转非零时付出）。

## 7. 问题 A（音量曲线"短一截"）——根因找到并已修复

### 真机证据链
- `%TEMP%\lumenarc_audio.log`：用户 8-13 19:37~19:48 在 **4 小时拼接视频
  （明景拼接视频_4时.mp4，8kHz mono）** 与 **~52 分钟拼接视频** 之间来回切换——
  正是"长短互切"场景。
- `明景拼接视频_4时.mp4.vla(.spec 655MB)`：4 时视频做过**纯音频分析**
  （无语义亮度数据）——触发 buggy 路径的必要条件。

### 根因（两处叠加，缺一不可）
1. **残留时长填充**：`openVideoFile` 切视频时 `m_currentDurationMs=0`，但
   `ChartPanel::m_durationMs` 从不清零——恢复状态 `setData` 触发
   `onDataReplaced` 时图表时长仍是**上一个视频的**。纯音频快照走
   `xMax = max(m_durationMs, dataMax=0)` = 旧短视频时长 → 音量曲线只填充前半段。
2. **时长到达后不重填**：引擎 `durationChanged`（正确时长）到达时
   `ChartPanel::setDuration` 只在 `!snapshot().isEmpty()`（有亮度数据）时
   重填；纯音频快照 `isEmpty()==true` → 只 setRange 不填数据 → **曲线永远
   停在旧视频时长处**。重新分析触发 setData 才恢复——与用户描述逐字吻合。

### 反证实验（钉死根因）
新增回归场景（vla_load_test `[bugA]`）：stale 52min → setData(4h 纯音频)
→ setDuration(4h)。**旧代码**：曲线末端 3,120,000ms = 恰好 52min（B 的时长），
4h 数据只画 21.7%，setDuration 后仍截断——复现用户现象；**新代码**：两步后均
覆盖 99.99%（14,398,720/14,400,000ms，差值为 8000 点下采样 stride）。

### 修复（src/chartpanel.cpp 两处）
1. `setDuration`：重填条件 `!isEmpty()` → `!isEmpty() || hasAudio()`。
2. `onDataReplaced` X 上限：`dataMax` 计入 `audio.durationMs()`——即使拿到
   残留/错误的时长，音量曲线也始终按音频全长铺满。

回归：vla 3×PASS+[bugA] 新增 PASS / case 239 / e2e 51 / piecewise 96 /
preprocess 170 / ui_chain 23 / calibration 73 全绿。

## 8. 问题 B（拼接顺序"混乱"）——代码定案：当前构建已正确；加固两项

### 排查结论（本机实证）
1. 用真实 04 目录 79 个 `20260722-HHMMSS[M].mp4` 验证**全部三条路径**：
   - 默认路径 `buildListOrderGroups → sortFilesByNameTime`：单调递增 ✓；
   - 自动排序路径 `smartSort`：OCR 引擎本机可用（rapidocr 已装），对真实文件
     实测 OCR 读数正确（`04:00:07` conf 0.95），OCR/文件名证据排序一致 ✓；
   - 导入顺序（对话框字母序）对本目录 = 时间序 ✓（即使旧代码也不会乱）。
2. 04 目录无 8-13 后的拼接产物；用户最近的拼接产物停在 8-04（修复前）。
3. **本机有 5+ 个旧版 LumenArc.exe**（`LumenArc_v1.0` 旧构建 7-29、v0.5 7-25
   等），桌面/任务栏无快捷方式——用户打开任意旧副本都无法看到修复。
   `LumenArc_v1.0`（7-29）早于 §17 非强制化与全部排序工作。

**结论**：f75c00f 起排序逻辑正确；用户复现最大可能是跑了旧构建
（或 8-13 19:31 前的当次构建）。代码侧未发现仍可乱序的路径。

### 加固（本轮实施）
1. **导入页即排序**（preprocesswindow.cpp `addFiles`）：文件进入待拼接列表
   立即按 `parseFilenameTimestamp` 排序——导入页顺序 = 校对页顺序 = 拼接
   顺序，所见即所得，用户第一眼即可确认。无时间戳文件保持相对序排末尾。
2. **标题栏构建时间戳**（mainwindow.cpp）：`Lumen Arc v1.3.1 (build Aug 13
   2026 20:42)` 常驻，截图即可辨识运行的是哪个构建——根治"旧 exe 扯皮"。

### 已知取舍（follow-up，不本轮处理）
- 导入页拖拽改序后，`buildListOrderGroups` 仍按文件名时间重排（f75c00f
  语义：文件名有时间 → 时间说了算）。若需"拖拽优先于文件名排序"，加
  userReordered 标志旁路即可，等用户明确表示需要再做。

### 给用户的验证口径
1. 完全退出旧进程 → 运行
   `C:\code\LumenArc\LumenArc_v1.0 remake\build\Release\LumenArc.exe`
   （标题栏应见 `build Aug 13 2026 20:xx`，无此行=旧构建）。
2. 问题 A：开 4 时视频→音频分析→切短视频→切回：音量曲线应铺满全程。
3. 问题 B：导入 04 目录：导入页表格第一行应即 `20260722-040007M`，末行
   `20260722-045938`；拼接产物按此时序。

# ============================================================================
# 工作记录（2026-08-14 下午，第三批）——Q1 方案 A 旋转功能全量实施
# ============================================================================

## 12. 显示旋转 90° 步进（Q1 方案 A 落地，HEAD 待提交）

### 依据
§11 Q1 已拍板：方案 A（覆盖物随转 + 双向坐标映射），施工规格六项按原记录执行。

### 已实现
1. **VideoWidget 显示链**：`原始帧 → 旋转(QImage::transformed) → LUT → m_frameImage`
   （setDisplayRotation，0/90/180/270 顺时针吸附归一化；默认零开销直通）。
   新增 `rawFrame()` 公开原始帧访问器。
2. **OverlayWidget 双向映射（核心）**：旋转烘焙进 mapToVideo/mapFromVideo——
   widget↔显示系缩放 + storedToDisplay/displayToStored 精确整数互逆，
   覆盖层 ~40 处调用点零改动。m_videoWidth/Height 保持【原视频尺寸】，
   显示尺寸由 displayVideoSize() 按档位推导（90/270 宽高互换）。
   ⚠ 对施工规格第 1 条的有意偏离：规格写「overlay()->setVideoSize(旋转后尺寸)」，
   实施改为「overlay 保持原视频尺寸 + 映射函数内部旋转」——同一显示语义，
   但消除逐调用点转换的漏改风险（规格意图双向映射，路径更安全）。
3. **放大镜**：MagnifierWidget::setDisplayRotation——源区域/光标/内部 overlay
   仍全部工作在原视频系（内部逻辑不动），ContentWidget 在显示前旋转裁剪图
   （帧与截图叠加同一档位）；recalcSourceRect 链路复用完成即时刷新。
   顺带修复预存 bug：createMagnifier 首帧原给 currentFrame()（已 LUT/旋转的
   显示帧，裁剪几何错误），改给 rawFrame()。
4. **钉图 PinnedWidget**：setDisplayRotation，裁剪后显示前旋转；缩放系数按
   旋转前区域高度 30px 基准等比应用到旋转后尺寸（竖长时间戳不变形）。
5. **截图叠加（融合）**：存储始终保持原视频系方位——VideoWidget 绘制缓存随
   档位旋转（缓存键含旋转档）；grabFrameSnapshot 捕获时逆旋转回原方位
   （LUT 保持烘焙＝所见即所得拍板 #2 不变）；放大镜叠加裁剪后同档旋转。
6. **时间戳框选**：beginTimestampRoiSelection 默认 ROI 改走 mapFromVideo
   （旋转映射）；normalizedRoi 天然正确（mapToVideo 已含逆旋转 + 按原视频
   尺寸归一化——规格第 2 条「归一化前先做逆旋转」自然成立）。
7. **放大镜平移**：MagnifierPan 位移向量新增 displayDeltaToStored（轴向随
   档位置换取反；位移无 -1 偏移、无需尺寸）。
8. **持久化**：VideoState += displayRotation，saveState/restoreState/无状态
   重置/清空列表全链路；hasData 补判三个 display* 字段（仅旋转/调节过的
   视频切走切回也能恢复）。不入 .vla（证据文件格式不动）。
9. **UI**：PlaybackAdjustPanel 新增旋转行（「⟳ 顺时针 90°」循环按钮 +
   档位标签），复位键连带旋转归零；rotationChanged 信号由 MainWindow
   同步到主画面/放大镜/钉图。MANUAL.md 补「画面调节」小节。
10. **证据快照**：currentFrame() 天然所见即所得（旋转+LUT 已含，OSD 文字
    在旋转后帧上正立烧录）；旋转档 ≠0 时写 PNG 元数据
    `LumenArc:displayRotation` + 保存提示注明档位（0 档不记，旧产物字节级一致）。
11. **交互兜底**：A/B 打点/逐帧/拖拽 scrub/倍速/音量均时间轴语义，与像素
    方位无关，零改动（规格第 5 条成立）。

### 验证
- 新增 ui_chain [rotation] 场景（offscreen，真实 VideoWidget+QTest 鼠标注入）：
  四角度 ×（拖拽创建矩形=手算角点锚定 / 点击命中+拖拽移动位移方向语义 /
  辅助线端点存储系 / 多边形闭合顶点 / 时间戳框选归一化 / 默认 ROI / 绘制
  不崩）。**60 checks, 0 failures**（基线 23 → 60）。
- 全回归绿：case 239 / case_e2e 51 / piecewise 96 / preprocess 170 /
  calibration 73 / ocr_atpositions 21 / vla PASS 含 [bugA]。
- offscreen 启动 5s 无崩溃。

### 排查中固化的重要知识（后来者勿再踩）
1. **QRect(p1,p2).normalized() 的 Qt 历史 quirk**：角点交换的轴（负尺寸）
   归一化时每端损失 1px。例：QRect((100,149),(200,119)).normalized()
   = (100,120,101,29) 而非 (100,119,101,31)。**预存行为**——无旋转时反向
   拖拽（右下→左上）一直如此；旋转只是把某些拖拽方向映射为交换轴。
   取证影响 ±1~2px 可忽略，不改产品代码；测试锚点需经同一 normalized()
   构造吸收。
2. **QTest::mouseDClick 在 offscreen 时间戳下不保证触发 QtGui 双击转换**
   （第二个 press 不会变成 MouseButtonDblClick）——测双击逻辑应手构
   QMouseEvent(MouseButtonDblClick) sendEvent，确定性 100%。
3. Release 构建 QT_NO_DEBUG_OUTPUT 把 qDebug 编译掉——诊断输出用
   fprintf(stderr)。
4. offscreen 冒烟测试后必须 powershell Stop-Process 杀净（git-bash 的
   kill/taskkill 转义不可靠）——残留实例锁 exe 导致 LNK1104。

### 待真机 GUI 确认（offscreen 覆盖不到，建议用户在场走一遍）
- 四角度 ×（放大镜观感/截图叠加对齐/钉图比例/快照合成观感/拖拽手感）；
- 4K/1440p 旋转播放性能抽查（transformed 每帧 ~10-20ms，仅旋转非零付出；
  若 4K 25fps 吃紧，follow-up 可做旋转+LUT 单趟合并，本轮从简）；
- 滑杆/旋转按钮与状态栏提示文案观感。

### 遗留（不本轮处理）
- 旋转+LUT 单趟合并优化（性能余量，见上）；
- 上一批待确项不变：面板滑杆手感、快照合成观感、OSD 字号比例。

# ============================================================================
# 工作记录（2026-08-14 下午，第四批）——放大镜调节修复 + 画面调节参数扩展
# ============================================================================

## 13. 放大镜不吃画面调节（用户实测发现）+ 调节参数扩展

### Bug 根因
放大镜从引擎 frameReady 直接取**原始帧**（mainwindow.cpp 转发链路），
从未走 VideoWidget 的 LUT 显示链——主画面调亮度/对比度时放大视图保持原样。
钉图（PinnedWidget）同类（同样吃引擎原始帧），一并修复。

### 修复
- 新增 `DisplayAdjust` 参数包 + 共享 LUT 构建器（`src/displayadjust.h/.cpp`）；
  `applyDisplayLut` 三处共用（VideoWidget/Magnifier/Pinned），消除三份逐像素拷贝。
- MagnifierWidget::setDisplayAdjust：ContentWidget 在**裁剪→旋转之后**查 LUT
  （与主画面同一显示链顺序）；暂停态拖滑杆即时预览（m_lastFullFrame 重裁链）。
- PinnedWidget::setDisplayLut：裁剪→旋转→LUT。
- MainWindow：adjustChanged 同时下发三视图；createMagnifier/pinnedRequested/
  状态恢复/无状态重置/清空列表全链路同步。
- VideoState 改嵌 `DisplayAdjust display`（替代散字段），hasData 判
  `!display.isIdentity()`；saveState 参数由 2 个 int 并为结构体。

### 参数扩展（本轮一并实现；均为显示层 LUT 可折叠项，逐帧零额外开销）
常用取证/分析画面调节调研结论（Amped FIVE / dTective / Input-Ace 同类工具）：
| 参数 | 价值 | 本轮 |
|---|---|---|
| **伽马** | 夜暗监控提亮暗部不冲淡高光（火调现场断电夜间素材刚需） | ✅ 0.30~3.00 |
| **色阶（黑点/白点）** | 烟雾/雾霾低对比拉伸（火场视频核心痛点） | ✅ 0~127 / 128~255 |
| **反色（负片）** | 高光区域细节辨认，小众但一行可折入 LUT | ✅ 复选框 |
| 锐化/去模糊 | 车牌/人脸增强；需卷积管线，4K CPU 成本高 | ⏸ 缓（建议 GPU 管线后做） |
| 水平/垂直翻转 | 镜像安装相机；改动坐标映射（二面体群） | ⏸ 缓（旋转已覆盖 90% 场景） |
| 饱和度/色相 | 亮度量化分析用处小 | ❌ 不做 |
| 伪彩色 | 亮度→色映射；与亮度曲线语义重复 | ❌ 不做 |
| 去隔行 | 老式隔行监控；引擎侧处理更合适 | ⏸ 缓 |

LUT 流水线固定顺序：`反色 → 色阶 → 伽马 → 亮度/对比度`，恒等时零开销；
纯亮度/对比度输出与既有 applyBrightnessContrast **逐位一致**（截尾舍入，
有回归断言钉死）。

### 验证
- ui_chain 新增 runDisplayAdjustScenario：LUT 数学 8 项（恒等空表/反色/伽马
  中间调提升且端点不动/色阶端点与中点/纯亮度对比度与旧公式逐位一致）+
  放大镜像素级 3 项（均匀灰帧 128 → γ2.0 后中心像素 ~180 → 复位回 128，
  offscreen grab() 实测）。**70 checks, 0 failures**（60→70）。
- 全回归绿：case 239 / e2e 51 / piecewise 96 / preprocess 170 /
  calibration 73 / ocr 21 / vla 含[bugA]；offscreen 启动 5s 无崩溃。

### 排查中固化
- edit 工具按调用事务化：同一次 call 里任一 oldText 失配 → 整笔回滚
  （本轮 MagnifierWidget::setDisplayAdjust 声明因此丢过一次，编译期即现形）。
- Qt6 QWidget::grab() 返回 QPixmap（需 .toImage()）。
- 头文件中声明返回 QImage 的函数需 `class QImage;` 前置声明。

### 待真机 GUI 确认
- 放大镜/钉图内伽马与色阶观感（滑杆手感、暂停预览）；反色在真实素材上的可用性。
# ============================================================================
# 工作记录（2026-08-14 下午，第五批）——放大镜标识框 + 快照全面化方案拍板（暂不开工）
# ============================================================================

## 14. 两项方案研究结论：用户已全部拍板（Q1-Q5 同意建议），暂不开工

> 本轮仅方案研究，未动代码。开工时按本节省实施，预估合计 1.5 天 + 四角度回归。

### 14.1 现状根因（排查结论，实施者必读）

**快照三问题（onSnapshotQuick，mainwindow.cpp:3648 起）**：
- **音量图被压缩**：`m_chartPanel->grab()` 按屏幕当前尺寸抓取（图表 dock 通常
  仅 ~200px 高），再 `scaledToWidth(视频宽)` 等比拉伸——曲线又糊又扁。
- **没有语谱图/放大镜**：合成时只拼了 ChartPanel，从未抓
  SpectrogramPanelEnhanced 与放大镜视图。
- **（附带发现）快照无 ROI/辅助线/多边形图形**：标注画在 OverlayWidget 子
  控件上，`currentFrame()` 只是裸帧（不含覆盖层）。

**放大镜标识框**：主画面此前无任何「放大位置」指示。数据链现成：
MagnifierWidget::m_sourceRect（原视频系）所有变化汇经 recalcSourceRect()；
旋转映射基建已就位（mapFromVideo 自动随转）。

### 14.2 拍板内容（2026-08-14 用户，Q1-Q5 全同意建议）

**Q1＝标识框样式：方案 A（四角括号 + 倍率徽章）**
- 金色（Theme::Accent）2px 四角括号 + 1px 黑色半透明衬影（亮底可读）；
  框内无填充无中线（零遮挡，取证第一原则）；倍率徽章深底金字「2.0×」，
  贴框外右上角（无空间改框内左上）。
- 仅绘制、不参与命中检测（不干扰 ROI 框选/拖拽）；放大镜 dock 开即显示、
  关即消失；光标跟随/缩放/平移时实时变化。

**Q2＝快照布局：竖向全宽堆叠**
```
┌────────────────────────────────┐
│ 视频帧（所见即所得：旋转+调节后画面，   │
│  OSD 烧录标签+时间码+文件名，          │
│  + 放大镜来源标识框烧录（Q4））        │
├────────────────────────────────┤
│ 亮度+音量曲线（全宽、足高重渲染，       │
│  含橙色时间光标）                   │
├────────────────────────────────┤
│ 语谱图（有音频分析时；同法全宽足高）     │
├────────────────────────────────┤
│ 放大镜视图（开启时）：原生裁剪分辨率，   │
│  右侧黑边烧录来源标注（坐标/倍率）      │
└────────────────────────────────┘
```

**Q3＝快照烧录覆盖层图形：烧录**。ROI 矩形/多边形/辅助线/标签点按模型颜色
全分辨率直接画到 videoPart 上（合成 PNG 是报告产物，不是原始证据；证据
文件不动）。实现：遍历模型用 storedToDisplay 映射（90° 步进精确整数式）
按显示系全分辨率绘制，不经屏幕坐标、不经 overlay grab，无损。

**Q4＝视频帧烧录放大镜来源标识框：烧录**。与屏上标识同款金色四角括号，
证据自解释（放大图从哪来）。

**Q5＝OSD 扩展：加第二行文件名**；调节参数不加（画面本身已是最终效果）。

### 14.3 施工规格（开工时按此执行）

**A. 放大镜标识框（预估 0.5 天）**
1. MagnifierWidget 加 `sourceRectChanged(QRect)` 信号，recalcSourceRect()
   末尾发射（覆盖光标跟随/滚轮缩放/中键平移/旋转/切视频全路径）。
2. OverlayWidget 加 `setMagnifierRect(QRect)`（原视频系，空=隐藏）+
   paintEvent 末尾绘制：四角括号（2px Accent + 1px 黑衬影偏移）+
   倍率徽章。零命中检测改动。
3. MainWindow：信号接线转发；removeMagnifier 时 overlay->setMagnifierRect({})。
4. 测试：ui_chain [rotation] 场景扩展——四角度下标识框显示坐标 ==
   mapFromVideo(sourceRect)（含旋转映射正确性）。

**B. 快照全面化（预估 1 天）**
1. 面板足高重渲染helper（治「被压缩」）：
   ```cpp
   panel->setUpdatesEnabled(false);
   const QSize old = panel->size();
   panel->resize(videoW, qMax(old.height(), 380));
   QImage img = panel->grab().toImage();   // 全宽原生渲染，曲线不糊
   panel->resize(old);
   panel->setUpdatesEnabled(true);
   ```
   ChartPanel 与 SpectrogramPanelEnhanced（QOpenGLWidget，grab 走
   framebuffer）同法。抓取条件沿用 `snapshot().hasAudio()`（语谱图）。
2. 放大镜段：MagnifierWidget 加 `currentMagnifiedImage()`（返回
   ContentWidget 当前 m_frameImage＝旋转+调节已应用的裁剪图，与放大视图
   逐位一致）；合成高度归一 ~320px 等比缩放，右侧黑边烧录
   「源区域 (x,y) w×h · N.N×」。
3. 覆盖层烧录：遍历 RegionModel/PolygonModel/GuideLineModel/labels，
   storedToDisplay → 按 videoPart 尺寸等比 → QPainter 直接画（模型原色）。
4. 来源标识框烧录：与 A 同款括号绘制函数（抽公共静态函数，屏上/快照复用）。
5. OSD：现有「标签+时间」保留，上方加一行文件名（白色小字，黑底阴影）。
6. 布局顺序见 14.2 图；各段之间不留白边，黑底。
7. 验收矩阵：有/无音频分析 × 有/无放大镜 × 四角度 × 有/无覆盖层；
   图表曲线清晰度目检（足高重渲染前后对比）；回归全绿 + ui_chain 扩展断言。

### 14.4 明确不做
- 快照不做 PiP（放大镜内嵌视频角）——遮挡画面，取证不妥。
- 不做屏幕分辨率整窗抓取（QScreen::grabWindow）——保真度低于原生分辨率
  合成，且会带入悬浮窗杂项。
- OSD 不烧调节参数（画面已是最终效果，参数冗余）。

# ============================================================================
# 工作记录（2026-08-14 傍晚，第六批）——§14 拍板落地：放大镜标识框 + 快照全面化
# ============================================================================

## 15. §14 方案全量实施（Q1-Q5 拍板内容落地，HEAD 见本批提交）

### 依据
§14.2/14.3 施工规格逐条执行（用户已拍板，本轮直接开工）。

### 已实现

**A. 放大镜来源标识框（规格 14.3.A）**
1. `MagnifierWidget::sourceRectChanged(QRect, qreal)` 新信号：
   `recalcSourceRect()` 末尾发射（光标跟随/中键平移/旋转/切视频全路径），
   `zoomAtPoint()` 直改 `m_sourceRect` 的旁路同样补发射；无条件发射、
   去抖由接收方 `setMagnifierRect` 同值短路承担（源端旧值比对会漏掉
   「取偶后矩形不变但倍率变化」的徽章更新）。
2. `OverlayWidget::setMagnifierRect(QRect, qreal)` + paintEvent 末尾绘制
   （最上层，仅指示、零命中检测改动）：金色（Theme::Accent）四角括号
   2px + 1px 黑色半透明衬影，框内无填充无中线；倍率徽章深底金字「2.0×」
   贴框外右上角（上方无空间改框内左上）。
3. MainWindow 接线：createMagnifier 连接信号 + 立即下发初始值（setVideoSize
   的信号早于 connect）；removeMagnifier 清 `setMagnifierRect({})`——切视频
   必走 removeMagnifier，标识框不会跨视频残留。
4. 测试：ui_chain 新场景 runMagnifierIndicatorScenario——四角度下
   `magnifierRectWidget()` == 手算角点锚点（经 normalized() 吸收 quirk）、
   zoomAtPoint/updateCursorPosition 信号传播、空矩形隐藏、
   drawMagnifierIndicator 像素级（括号臂金色/框内零遮挡/徽章在位）。

**B. 证据快照全面化（规格 14.3.B）**
1. 足高重渲染 helper `grabPanelFullHeight`：临时 resize 到视频全宽 +
   ≥380/320px 高 → grab → 还原（resize 同步派发 Resize 事件，曲线不糊不扁）。
   ChartPanel 条件沿用 `!snap.isEmpty() || hasAudio()`；SpectrogramPanelEnhanced
   条件 `hasAudio()`（QOpenGLWidget grab 走 framebuffer，失败为 Null 自动跳过）。
2. 放大镜段：`MagnifierWidget::currentMagnifiedImage()`（ContentWidget 当前
   m_frameImage = 旋转+LUT 已应用裁剪图）高度归一 320px 等比缩放（超宽兜底
   按宽适配），右侧黑边烧录「源区域 (x, y) w×h · N.N×」。
3. 覆盖层烧录（Q3）：新公共静态 `OverlayWidget::burnAnnotations`——ROI 矩形/
   多边形/辅助线按「rotateStoredToDisplay + 等比缩放」映射（与 mapFromVideo
   同一整数式，支持 scrub 降采样帧）全分辨率画到 videoPart；模型原色 +
   半透明填充 + R/P 序号标签，与屏上观感一致。
4. 来源标识框烧录（Q4）：同一静态 `drawMagnifierIndicator`（屏上/快照复用），
   线宽/字号随分辨率缩放（annoPen 1~4px、括号 2~8px、徽章 10~28px）。
5. OSD（Q5）：现有「标签(金)+时间」保留，上方新增文件名行（白色小字
   0.75×，黑底阴影）；标签为空时行位自动上收。
6. 布局（Q2）：视频帧 → 曲线 → 语谱图 → 放大镜段，竖向全宽堆叠，黑底无留白。
7. 明确不做项（§14.4）均未做：无 PiP、无整窗抓取、OSD 不烧调节参数。

### 顺带重构（等价行为）
- 旋转映射提炼公共静态：`displaySizeForRotation` / `rotateStoredToDisplay` /
  `mapStoredPointToFrame` / `mapStoredRectToFrame`，OverlayWidget 成员函数
  storedToDisplay/displayVideoSize 改调静态版——屏上映射与快照烧录逐位一致，
  消除两份公式漂移风险。

### 验证
- ui_chain 70 → **92 checks, 0 failures**（新增 22 项：标识框四角度锚定/
  信号传播/隐藏、括号绘制像素级、burnAnnotations rot0+rot90 像素级、
  currentMagnifiedImage 尺寸/像素/旋转互换）。
- 全回归绿：case 239 / case_e2e 51 / piecewise 96 / preprocess 170 /
  calibration 73 / ocr_atpositions 21 / vla 5×PASS 含 [bugA]。
- offscreen 启动 5s 无崩溃（残留进程已 powershell Stop-Process 杀净）。
- MANUAL.md：§二新增「证据快照」小节（四段内容表 + 保存位置），
  §九放大镜新增「来源标识框」段落。

### 排查中固化（后来者勿踩）
- 同一 normalized() quirk 再现身：映射角点在交换轴上构造锚点必须经
  `QRect(p1,p2).normalized()` 吸收（本轮 rot90/270 锚点初版各差 1px，
  复核系手工算术失误 + quirk 叠加；实现本身逐位正确）。

### 待真机 GUI 确认（offscreen 覆盖不到）
- 标识框观感：括号/徽章在亮底暗底素材上的可读性、跟随流畅度。
- 快照产物：曲线足高重渲染清晰度、语谱图段（真实 GL 环境）抓取、
  放大镜段黑边标注排版、四角度 × 有/无覆盖层 × 有/无放大镜矩阵走查。
- 上一批待确项不变：面板滑杆手感、OSD 字号比例、4K 旋转播放性能。
