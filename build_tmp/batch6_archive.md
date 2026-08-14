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
