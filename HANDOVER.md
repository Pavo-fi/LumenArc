# LumenArc 工作交接文档（HANDOVER）

> **本文件只保留最近 5 次更新**；更早记录依修改顺序（时间序）存档于
> **WORK_HISTORY.md**（规则 R2）。

## 表头（每次写完 HANDOVER 与 WORK_HISTORY 后必须同步更新本表头——规则 R2）

- **当前 HEAD**：施工批（2026-09-07 §91 v1.17.0 MainWindow 拆解收官：四阶段五提交，
  P-76~P-80 全勾销）
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

