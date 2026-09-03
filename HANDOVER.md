# LumenArc 工作交接文档（HANDOVER）

> **本文件只保留最近 5 次更新**；更早记录依修改顺序（时间序）存档于
> **WORK_HISTORY.md**（规则 R2）。

## 表头（每次写完 HANDOVER 与 WORK_HISTORY 后必须同步更新本表头——规则 R2）

- **当前 HEAD**：施工批（2026-09-03 §86 合成导出 P2：ROI/曲线滚动条+宫格布局+ffmpeg8 排雷+docx 原子写）
- **构建**：`cmd //c "build_tmp\build_target.bat ALL"`；测试：`QT_QPA_PLATFORM=offscreen`
  + PATH 含 `C:\code\Qt\6.8.0\msvc2022_64\bin`（配置：`build_tmp\reconfigure.bat`）
- **全回归基线**（18 套，v1.16.1 后）：mw 97 / ui_chain 103 / libav 26（含 av-align/denoise）/
  case 270 / engine（avgap/play/seek-matrix）/ denoise / docx / report / sitemap / sync 等全绿
- **云端**（CloudBase）：env `lumenarc-prod-d6gcdfb6a8873d906`；四函数已部署+HTTP 触发器已通；
  改函数后 `cd build_tmp/tcb_deploy && MSYS_NO_PATHCONV=1 tcb fn deploy <name> --force --yes`；
  AUTH_SECRET 在 build_tmp/tcb_deploy/.auth_secret（不入库）；详见 docs/cloudbase/README.md
- **当前保留批次**（新→旧，R2 限 5 批）：
  第七十三批 §86（合成导出 P2：ROI/曲线滚动条+宫格布局+ffmpeg8 排雷+docx 原子写）·
  第七十二批 §85（合成导出工作台：素材树+可播放预览+片段块时间线）·
  第七十一批 §84（P1：in-process 多段+证据/演示双模式）·
  第七十一批 §83（MLT melt 构建+能力矩阵实测）·
  第七十一批 §82（v1.16.2 引擎三连修+拼接补全）。
  （§81 账号 v1.4 署名写死 已归档 WORK_HISTORY——见下）
- **最近归档动作**：2026-09-03 §86 批——§81（账号 v1.4 署名写死）移入 WORK_HISTORY.md 末尾；
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

## 86. 合成导出 P2：ROI/曲线滚动条烧录 + 宫格布局 + ffmpeg8 aresample 排雷 + docx 原子写

- **compose_render 新模块**（src/infrastructure/compose_render.h/.cpp）：
  `loadComposeOverlay(vlaPath)`（TimelineModel::loadFromFile 一次性取 ROI/多边形/
  标签/亮度行+时间轴/音量通道）+ `drawRoiOverlay`（源像素坐标→KeepAspectRatio
  显示矩形映射，R1/R2 标号+半透明填充，RoiModel::regionColor 同色）+
  `drawChartStrip`（30s 窗口游标固定 2/3：亮度逐 ROI 行折线+音量绿曲线+
  标签同色虚线竖标+白色游标三角柄+窗口起止注记；无数据画占位文）。
- **引擎**：ComposeSeg +gridLayout(0 均分/1 主听路大窗)/burnRoi/burnChart；
  Params +vlaPathByPath（工作台填，引擎自载数据）；单段分支 stripOn 时视频区
  缩短 158px 装曲线条；lanes 分支 cellRectOf 支持主听路大窗布局。
- **工作台**：导出面板 +「ROI 烧录」「曲线滚动条」勾（演示模式，默认开）；
  宫格段编辑框 +布局下拉；块副标题显示 ▦N路·主路大窗。
- **排雷（真实病灶素材 e2e 逮到）**：bundled ffmpeg 8 的 aresample 已删 `ocl`
  选项（新名 `out_chlayout`）——buildAudioFilterChainMulti 的归一化链
  `ocl=stereo` 在 8kHz mono 源上直接 filter 报错导出失败；改
  `aresample=48000:out_chlayout=stereo:osf=s16`。**旧链（buildAudioFilterChain
  /Ranges）不做归一化未踩雷**（单源自一致）；教训：新滤镜参数必须以 bundled
  ffmpeg `-h filter=X` 实测为准。
- **docx 0MB 硬化**：ZipStoreWriter::writeTo 改原子写（同目录 .tmp→flush→
  大小复核→rename），失败不再留 0 字节残件；报告/点位图等全部 zip 产物受益。
  Release 0MB docx 根因未能本地复现（写入层失败回 false 本就有弹窗），
  先以原子写收口，待用户 Release 复测报告生成。
- **测试**：segment 102 checks 全绿——testComposeOverlay（.vla 回环+游标白线/
  曲线上墨/ROI 染色像素级断言）+ testComposeOverlayEndToEnd（导出后 ffmpeg
  抽帧验底部条带）+ testComposeRealAssetEndToEnd（增城病灶 LAMerged 91min
  PTS 抖动族：60s/120s 各取 5s、段2 2x → 产物 7.5s 精确+音轨归一化）；
  全回归 9 套绿；手册 PDF 重出（298KB）。

## 85. 合成导出工作台（P1.5 拍板 v2）：素材树+可播放预览+片段块时间线，废表格对话框

- **用户反馈表格对话框「不是剪辑的感觉」→ 拍板 v2**：①预览区可播放（B 方案）
  ②废表格改片段块（双击弹小编辑框精调）③非模态；素材分「多通道（机位同屏）/
  单视频（案内全量含前处理产物）」两类。
- **新窗 `src/composeworkbench.h/.cpp`**（ComposeWorkbenchWindow，~1100 行）：
  - 素材树：多通道节点=机位清单逐路勾选（≤4；未校时=临时路提示；合并轨组禁用
    同多机口径）；单视频节点=`buildCamInventory` 全量（视频+前处理产物 ⚙标记）
    + 案外当前视频伪条目。
  - 预览：单路=自建 FfmpegVideoEngine（QSettings 硬解口径同主窗工厂）；多通道=
    MultiCamSyncService+CamTileWidget 宫格（瓦片点击切主听路）。CamTileWidget
    同时当单路显示器用（frameReady/ack 契约现成）。窗口 hide/close 自动
    stopPreviews（引擎不残留）。
  - 走带：播放/暂停+进度条（多通道走 svc beginScrub/scrubTo/endScrub，单路 seek）；
    `I/O` 打点（域：单路=流内 ms、多通道=墙钟 ms）；「+ 加入时间线」无打点默认
    游标前 10s。
  - 时间线 ComposeTimelineWidget（cpp 内 Q_OBJECT，`#include "composeworkbench.moc"`）：
    块宽∝输出时长、色板按序、点选/拖拽排序（>24px 触发）/双击参数框（入出点文本
    h:mm:ss.mmm + 倍速 spin）/右键菜单/滚轮缩放 0.004~2px/ms。
  - 导出面板：演示/证据双模式（时间线含宫格段→证据自动禁用回演示）、校正时间/
    案件号/图表面板勾（图表面板 tooltip 注明仅单段当前视频原速）、案内 exports/
    默认路径、内嵌进度。
- **引擎扩 ComposeSeg**：`lanes`（SyncLaneData 快照）+`audioLane`+`displayName`，
  isLanes() 即多通道段；start() 校验——证据模式拒宫格段、宫格段 2~4 路、
  合并轨拒、未校时非临时路拒；runCompose 宫格分支（逐路 SeqDecoder+宫格绘制，
  瓦片画法与 runMultiCam 孪生但**刻意不共改其 v1.15.3 冻结路径**；多段模式无覆盖条）；
  音轨=主听路两端覆盖才映射（部分覆盖退化整段静音，细分留 P2）；OSD 基准=
  首条已校时路墙钟→北京时间，无则「未校时·墙钟 Ns」。
- **旧 ComposeExportDialog（表格版）删除**：文件/CMake/主窗引用全清；
  主窗 m_composeDlg→m_workbench，startComposeExport 分发逻辑不变
  （单段+当前视频+原速+图表面板→旧复合路径保留）。
- **测试**：segment_test 新增 lanes 宫格 e2e（双临时路同素材→时长/无音轨校验）
  +3 个校验拒绝用例；**修 harness 两个潜伏坑**：①`QSignalSpy::wait` 对同步
  （直连接）已发出的信号返回 false → 校验类用 `count()+first()`；②测试须在
  **仓库根目录**跑（相对路径 build_tmp/caltest 资产）——此前从 build/Release 跑
  时 e2e 曾静默 SKIP 未被察觉；给 segment_test 装了 qInstallMessageHandler 写
  build_tmp/segment_test_out.log（控制台吞输出环境的诊断通道）。
  基线更正：basic.mp4 **无音轨**（此前 compose e2e 的 hasAudio 预期写反，
  从根目录真跑后暴露修正）。88 checks 0 failures；全回归绿
  （mw/case/report/sitemap/ui_chain/libav/sync）。
- **遗留**：多通道段部分覆盖音轨细分 P2；宫格段无覆盖条（P2 可补）；
  工作台无独立「证据+宫格」组合（物理上矛盾）；块时间线无播放头联动（P2）。

## 84. 合成导出器 P1 落地（in-process 路线修订，取代分段导出入口）

- **路线修订（本批拍板级调整）**：原计划 MLT XML→melt 进程化渲染；侦察发现现有
  `SegmentExportEngine` 已是完整的进程内合成管线（libav 解码→QPainter 画布→rawvideo
  管道→ffmpeg.exe 子进程 x264/openh264/h264_mf）。**P1 改为扩展该引擎**：
  预览一致性最好、进度/取消在进程内、不吃 167MB melt 运行时；melt 保留作 P2+ 多轨备选。
- **引擎扩展**（segment_export_engine）：
  - `Params::ComposeSeg{sourcePath,inMs,outMs,rate}` + `segments` 非空=多段模式
    （plan/charts/PIP 忽略）；`calibrationByPath`（逐文件校正表）；
    `demoWatermark`（强制红标）/ `evidenceCopy`（证据直拷）；`operatorName/Org`。
  - `runCompose()`：逐段 open/seek（start_time 归一）→ QPainter 画布（layoutRects
    全幅视频区）→ OSD 左下（校正北京时间或流内回落+倍速+案件号）+ 右上强制红标
    「分析演示材料 · 非原始证据」→ rawvideo pipe → ffmpeg.exe（crf18/aac128k/-shortest）。
  - `runEvidenceCopy()`：逐段 `-ss/-to -c copy -avoid_negative_ts make_zero` →
    concat demuxer 拼接；流式 SHA-256（源+产物）→ 侧车 `<out>.forensic.json`
    （kind=evidence_segment_export/段区间/签署人/完整性声明"关键帧对齐·像素零改动"）。
  - `buildAudioFilterChainMulti()`：逐段输入标签，无音轨段 `anullsrc` 补静（长度=输出域
    时长/rate），全分支 `aresample=48000:ocl=stereo:osf=s16` 归一 → concat。
  - 纯函数 `composeSegOutFrames` / `buildEvidenceManifest` 可单测。
- **新对话框** `src/composeexportdialog.h/.cpp`：片段表（源视频下拉=案内视频+当前、
  入/出点 h:mm:ss.mmm 文本可编辑、倍速、输出时长列）、添加当前选段/游标起10s/删/
  上下移；模式单选（证据直拷/分析演示片）；演示选项（校正时间角标/案件号/图表面板）；
  输出路径（案内 exports/，LACompose_/LAEvidence_ 前缀）；内嵌进度+取消。
  逐文件校正表由对话框经 `TimelineModel::peekCalibrationFromVla(vlaPathFor(path))`
  预取填入 `calibrationByPath`；签署人取 CredentialStore（署名写死策略 v1.4）。
- **主窗接线**：工具栏「导出选段」→「合成导出」（onExportSegmentClip 重写，
  m_exportDlg→m_composeDlg）；`startComposeExport(pp)` 分发——
  **单段+源=当前视频+rate=1+勾图表面板+演示模式 → 旧 startSegmentExport 全保真路径
  （曲线/语谱/放大镜/标签 OSD/分段变速零回归）**，否则走新多段/证据管线。
  多机窗口（multicamplaybackwindow）仍用旧 SegmentExportDialog，不受影响。
- **测试**：segment_test 新增——testComposeHelpers（帧数数学/多源音频链/清单 JSON）、
  testComposeEndToEnd（2 段合成→时长+音轨校验）、testEvidenceEndToEnd（直拷+侧车
  JSON 字段校验）；全回归绿（segment/mw/case/libav/report/sitemap/ui_chain）。
- **文档**：MANUAL 七·2 整节重写为「合成导出（多段拼接 · 证据/演示双模式）」并同步
  速览表/工作流措辞（PDF 已重出）；CHANGELOG v1.16.2 条目；PENDING 勾销 P1 本体。
- **遗留**：P1 预览无独立合成预览窗（复用主视口手动核对）；MLT 运行时仍在
  build/Release/mlt/ 但 **P1 不打包 mlt/**（pack_release 不动）；P2 多机位同屏/
  曲线滚动条/ROI 烧录、P3 melt 瘦身见 PENDING。

## 83. MLT 合成引擎基建：melt.exe MSVC 构建跑通 + 能力矩阵实测（434a6ec）

- 定位拍板（2026-09-03）：合成导出器=**分析成果合成导出器**（非通用剪辑器）；
  P1=单轨片段序列+校正时间角标+证据/演示双模式；入口取代分段导出；不许跨案件混编。
- **melt 7.41.0 MSVC 构建**（build_tmp/mlt_src = MLT **master** tarball；v7.40 不兼容
  FFmpeg 8——`AVCodec::sample_fmts/pix_fmts` 已删，master 用 avcodec_get_supported_config）。
  vcpkg 装 libxml2（github 超时→curl 断点续传塞 downloads）/pkgconf/pthreads/dirent/
  dlfcn-win32/sdl2/libebur128；`-DVCPKG_MANIFEST_MODE=OFF`（否则自动编 ffmpeg）；
  `/utf-8` 绕 GBK 吃行（mlt_repository.c 注释含 UTF-8 省略号）；全局 /I 补 pthread.h。
  配方入 `tools/mlt/build_mlt.bat` + `docs/mlt/README.md`。
- **许可证白名单**：core/avformat/xml/plus=LGPL ✅；qt/plusgpl/glaxnimate/normalize/
  resample/rubberband/vidstab/xine/openfx=GPL ❌ 构建即排除（模块运行时 dlopen）。
- **能力矩阵像素级实测**（build_tmp/mlt_smoke）：✅片段 in/out 裁剪拼接、✅画中画、
  ✅PNG/PNG 序列 alpha 叠层（composite 半透明数值精确）；❌qtrle(argb) alpha 被
  chain_normalizers 吞、❌dynamictext/text 滤镜依赖 GPL qt/gtk 模块。
  → 叠层协议定为 **PNG 序列（QPainter 渲染）**。
- 运行 env：MLT_REPOSITORY/MLT_DATA/MLT_PROFILES_PATH；依赖 DLL 与 melt.exe 同目录
  （avdevice-63 易漏）；MLT XML 工程一律绝对路径（相对路径静默失败 rc=3）；
  CLI 文本角标参数走 GBK 控制台会乱码（集成一律生成 UTF-8 XML）。
- melt 为 GPL 二进制（x264），若随包发布需附源码说明（docs/mlt/README.md 已述）。

## 82. v1.16.2 前瞻：引擎三连修 + 拼接工况全景补全（24db060 / df56533）

- **①副屏全屏落错屏**（用户双屏 DISPLAY1 1920x1200 + 34G1Q 3440x1440@x=3840）：
  fsprobe 真机四策略对比，唯一正解=`winId()`+`windowHandle()->setScreen()`+
  **handle 级** setGeometry+showFullScreen（fullscreenvideowindow.cpp showOnScreen）。
  坑：QWidget::setGeometry 与 setScreen+move 均落主屏；探针须 qInstallMessageHandler
  写文件（无控制台）+quitOnLastWindowClosed(false)。
- **②LAMerged 卡顿+音谱错位**：音频包 PTS 极不规则（180/78/20ms 乱跳），空档补偿
  阈值播放 40ms/分析 20ms 每包误触发 → 双侧统一 **200ms**，分析侧新增对称重叠裁剪；
  实测 25s 实播零补偿日志（%TEMP%/lumenarc_audio.log audioDiag）。
- **③输出设备热跟随**：followDefaultAudioDevice() 每 16 包检测系统默认设备变化
  热切换续播（用户默认设备被副屏抢成 34G1Q NVIDIA HDMI 实锤）。
- **拼接工况 8 矩阵补全**：⑥变速≠1x 过拼接空档补偿（pad 量 skew/rate）；⑦异构段
  直拷（concat -c copy）双侧引擎 swr 输入侧随帧重建（reconfigSwrInput + 分析侧
  curInRate/Fmt/Mask 快照）。已知边界在案：>10s 空档截断、40-200ms 小空档不补、
  预览缓存 sws 不重建、纯视频段。
- 病灶文件（现回归素材）：cases/20260722-广州增城/preprocess/.../LAMerged_02-04-52
  _6m_03-39-11.mp4（91min、15fps、AAC 8kHz mono、PTS 抖动族）。
