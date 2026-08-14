# LumenArc 工作交接文档（HANDOVER）

> **本文件只保留最近 5 次更新**；更早记录依修改顺序（时间序）存档于
> **WORK_HISTORY.md**（规则 R2）。

## 表头（每次写完 HANDOVER 与 WORK_HISTORY 后必须同步更新本表头——规则 R2）

- **当前 HEAD**：`c0a7b0e`（2026-08-14 §17 快照 v2 两连修）
- **构建**：`cmd //c "build_tmp\build_target.bat ALL"`；测试：`QT_QPA_PLATFORM=offscreen`
  + PATH 含 `C:\code\Qt\6.8.0\msvc2022_64\bin`
- **全回归基线**：case 239 / case_e2e 51 / piecewise 96 / preprocess 170 /
  ui_chain 92 / calibration 73 / ocr_atpositions 21 / vla 13×PASS 含 [bugA][snaprender]
- **当前保留批次**（新→旧）：
  第八批 §17（快照 v2 真机反馈两连修：刻度轨重建 + 语谱频率轴）·
  第七批 §16（快照翻车重做：离屏重渲染路线，dock resize+grab 废止）·
  第六批 §15（§14 拍板落地：放大镜标识框 + 快照全面化，`855d5cf`）·
  第五批 §14（放大镜标识框 + 快照全面化方案拍板 Q1-Q5）·
  第四批 §13（放大镜/钉图吃调节修复 + 伽马/色阶/反色扩展，`c022d70`）
- **最近归档动作**：2026-08-14 第八批——第三批（§12 显示旋转 90° 方案 A
  全量实施）移入 WORK_HISTORY.md，原文未删改。
- **常用参考导航**（已归档，查 WORK_HISTORY.md）：显示旋转 90° 方案 A
  （第三批 §12）、音频时间轴对齐与问题 A/B 定案（深夜批）、项目规则 R1/R2
  （08-13 晚批）、架构分层与红线 R 规则（二章）、构建部署 CI（九章）、
  升级计划表 v1.2~v1.9（十章）、测试体系 28 项矩阵（七章）、校时管线
  救复速览（〇章）、案件模块 M1-M3（二十三章）。

---

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

# ============================================================================
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
