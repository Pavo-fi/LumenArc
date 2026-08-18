# LumenArc 工作交接文档（HANDOVER）

> **本文件只保留最近 5 次更新**；更早记录依修改顺序（时间序）存档于
> **WORK_HISTORY.md**（规则 R2）。

## 表头（每次写完 HANDOVER 与 WORK_HISTORY 后必须同步更新本表头——规则 R2）

- **当前 HEAD**：施工批（2026-08-18 §49 亮度分析闪退排查 P-55；版本 v1.9.0）
- **构建**：`cmd //c "build_tmp\build_target.bat ALL"`；测试：`QT_QPA_PLATFORM=offscreen`
  + PATH 含 `C:\code\Qt\6.8.0\msvc2022_64\bin`（配置：`build_tmp\reconfigure.bat`）
- **全回归基线**（13 套，v1.9.0 施工批）：mw **26**（新增）/ task_registry 41 / case 248 /
  case_e2e 51 / piecewise 96 / preprocess 176 / ui_chain 92 / calibration 77 /
  roi_model 23 / vla +54 / libav 10 / v17 34
- **当前保留批次**（新→旧，R2 限 5 批）：
  第四十批 §49（亮度分析闪退排查，P-55 待复现信息）·
  第三十九批 §48（P-31 拆分四组件+R3/R5 收口，版本→1.9.0）·
  第三十八批 §47（P-30 任务化+通道化+.vla v10+P-25 退役，版本→1.8.0）·
  第三十七批 §46（命名 LAMerged / 路径集成页面 / NAL 快检，`b6ce835`+`a055a75`+`e4b104d`）·
  第三十六批 §45（拼接假成功兜底 + P-27 真机验证，`c1aa169`）
- **最近归档动作**：2026-08-18 §49 批——第三十五批（§44）移入 WORK_HISTORY.md 末尾；
  早前：第三十一批（§40）、第三十批（§39）、第五批（§14）至第二十九批（§38）。移入 WORK_HISTORY.md
  末尾；早前同批：第三十批（§39）、第五批（§14）至第二十九批（§38）共 25 批
  整体移入（原文未删改）。
- **常用参考导航**（已归档，查 WORK_HISTORY.md）：显示旋转 90° 方案 A
  （第三批 §12）、音频时间轴对齐与问题 A/B 定案（深夜批）、项目规则 R1/R2
  （08-13 晚批）、架构分层与红线 R 规则（二章）、构建部署 CI（九章）、
  升级计划表 v1.2~v1.9（十章）、测试体系 28 项矩阵（七章）、校时管线
  救复速览（〇章）、案件模块 M1-M3（二十三章）。
- **管理文档**（2026-08-16 §44 建立）：待办唯一登记处 `docs/PENDING.md`；
  文档体系与维护规矩 `docs/DOCS_MAP.md`（规矩 D1-D8，待办必登记 PENDING）。
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
# 工作记录（2026-08-17，第三十八批）——v1.8.0 P-30 施工：任务化+通道化+.vla v10+P-25 退役
# ============================================================================

## 47. P1a/P1b/P-25 全量落地（方案 DEVELOPMENT_PLAN_V1.8_CN.md，拍板记录见其 §9）

### 提交序列（T1/T3 纪律：重构与行为分开）

| commit | 内容 |
|---|---|
| `feat: P1a 任务化 + P1b 通道化` | TaskRegistry（domain 纯数据注册表，显示名中英双串存 domain 不引 i18n）+ AnalysisTaskService（app 层状态机 Idle→Running→终态；取消竞态 gating 替代错误文案比较 C1；错误码 kErrNoVideo/Precondition/Busy/UnknownTask/Engine）+ AnalysisSnapshot channels 字典（QHash<QString,ChannelData>；lumRows/lumEntries/audioData API；timestamps 保留成员=亮度共享时间轴）+ .vla v10（META channels 清单/旧计数字段双写；LUM/VOL/SPEC 字节零改动；未知通道 opaque 字节保全 codec/stored 原样带回；kCurrentVlaVersion=10）+ MainWindow 接线（引擎信号经服务聚合，删 AnalysisPhase 枚举=P-32）+ 引擎装配切 setLuminance/setAudio |
| `test: 任务/通道化测试` | task_registry_test 41 断言（注册/状态机全路径/取消后迟到 finished+failed 双重忽略/合并两序/前置条件/空路径）+ vla_load_test +54 断言（v10 文件级 META channels 断言/重载往返/v9→v10 迁移回存/F4 version=11 拒载/peek v10/未知通道 CH01 加载-回写-再载字节不变） |
| `refactor: 消费点切永久 API` | chartpanel/mainwindow/测试 → lumRows()/lumEntries()/audioData()，删迁移期兼容访问器（R10，P-33 收口） |
| `feat: P-25 Python 引擎退役` | 删 python_analysis_engine.{h,cpp}(664行)/analyze_video.py/CMake 6 处引用与 POST_BUILD 拷贝/设置菜单"分析引擎"子菜单/引擎构造 QSettings 分支；静态探测抽 ToolPaths（findFfmpegPath/detectPythonPath 原样迁移；消费方 timestamp_ocr/calibration/encoder_probe/transcode/concat/preprocesswindow/v17_test 全部改接）；mac workflow 删 analyze_video 自检行；cast 3 处随引擎删除归零（P-35 提前收口）；main.cpp 引擎构造改恒 libav |
| `docs+chore: 版本四处` | 1.3.1→1.8.0（CMake/app.rc/About/README 运行时说明）；MANUAL 引擎说明改写；RELEASE_CHECKLIST_V1.8 新建（A-E 28 项，B4/B5 已离线自动验证） |

### 关键实现决策

1. **状态机取消语义**：cancel() 立即回 Idle 并发 taskCancelled；此后引擎迟到的
   finished/failed 一律忽略（onEngineFinished 首行 `m_state != Running` 卫语句）——
   旧版"取消后引擎报 failed('Analysis cancelled by user.') 按文案判取消"的 C1 违例
   连根拔除；切换视频（B6 竞态）同走此门。
2. **v10 磁盘布局（拍板 Q1）**：数据块沿用既有标签，META 加 `channels` 清单
   （id/kind/计数，audio 带 spec_frames，opaque 带 chunk 标签+raw_length）。
   v9 读者按 F4 拒 v10（上界互斥）；v10 读者可读 v9 并内存升 channels，保存自然落 v10。
3. **未知通道 opaque（拍板 Q2）**：vlaUnpackAll 增 rawChunks 出参（codec/rawLen/stored
   原样）；加载端 v10 且块标签 ∉ {META,TMS,LUM,VOL,SPEC} → `opaque:<tag>` 通道
   （payload 语义保全 + stored 字节保全）；保存端原块回写（vlaPackRawChunk）。
   未来新通道数据块规约：4 ASCII 标签 `CH:` 前缀预留。
4. **合并策略迁移**：旧 onAnalysisFinished 的"亮度完成保留既有 audio / audio-only
   合入既有亮度"改为服务内 producedChannels 逐通道覆盖（setLuminance/setAudio
   未产出通道不动）；MainWindow 的语谱刷新改注册表驱动（producedChannels
   contains audio → setSpectrogramData）。
5. **P-25 退役评估落地结论**（方案 §5）：退役收益=维护面收窄（双引擎×双测试×
   cast 债清零），**体积收益≈0**——probe_timestamps.py（cv2/numpy/rapidocr）与
   P-28 报告（python-docx）租户保留 bundled Python；A/B 对拍测试改 SKIP（脚本
   不再随包），libav 语义对齐注释保留。发现并登记 **P-54**：降噪滑杆为 Python
   引擎专属谱减能力，v1.5 默认 libav 起即空操作，UI 存在误导待拍板清理。

### 验证

- 全回归 **12 套**绿：task_registry 41（新）/ case 248 / case_e2e 51 / piecewise 96
  / preprocess 176 / calibration 77 / roi_model 23 / v17 34 / ui_chain 92 / vla
  （+54 新断言）/ libav 10（A/B 部分 SKIP 为退役后预期）
- 待真机（RELEASE_CHECKLIST_V1.8 A-E）：①音频进度 0-100 唯一可见变化确认
  ②v9 老案件加载→重分析→保存→重开往返 ③CSV 列序冻结 ④设置菜单无引擎项
  ⑤OCR 校时存活 ⑥回归抽查五项
- PENDING 勾销：P-25/P-30/P-32/P-33/P-35；新增 P-54（降噪滑杆待拍板）

### 版本与文档

- 版本 1.3.1 → **1.8.0**（D4 四处一致）；DOCS_MAP 登记 V1.8 已实施 + 点检清单；
  V1_ERA §1.3 对照表 v10 行与本实施一致（方案编写时已同步）。
# ============================================================================

# ============================================================================
# ============================================================================
# 工作记录（2026-08-17，第三十七批）——命名/路径/快检三需求 + 后门对面失败排查
# ============================================================================

## 46. 三项用户需求实施 + 后门对面拼接失败根因

### 1. 后门对面拼接全部失败：源文件批量损坏（已排查）
- 后门对面两目录 326 个文件逐一 NAL 快检 → **33 个数据损坏**（20250726×31、
  20250727×2），约 10%，moov/mdat 错位（Invalid NAL unit size）
- 设备导出/拷贝问题；拼接遇坏文件中止（P-50 兜底正确报错而非静默 14KB）
- 坏文件清单已生成：`桌面\后门对面_损坏文件清单.txt`；需重新导出后重跑

### 2. 拼接产物自动命名（用户拍板，`b6ce835`）
- `LAMerged_<通道>_<首>_<尾>.mp4`：首/尾 = 组内排序后第一/最后一段文件名
  去扩展名；有通道号带通道前缀（默认组不带）；监控惯例 HHMMSS_通道 命名
  → 剥离尾部重复段（最后一个 _ 之后，如 _100）只留首视频
- 例：000131_100 + 024556_100 → `LAMerged_000131_100_024556.mp4`
- 实现：domain/concat_naming.{h,cpp} 纯函数 + preprocess_test +6 断言（176 全过）

### 3. 输出路径自定义集成到页面（`a055a75`，用户反馈"找不到入口"）
- 原路径行隐藏在设置页 → **移至页面顶部横幅**（说明卡片上方，始终可见）：
  「输出文件夹：[……] [浏览…]」
- 案件横幅改造两行：案件信息+模式按钮（无案件时隐藏该行）/ 输出文件夹行（常显）
- 点「独立输出（自选）」→ **自动弹出目录选择**（取消则维持原路径）；
  案件导入模式仍校验案内；无案件时编辑框自由填写（同旧行为）

### 4. NAL 数据完整性快检（用户拍板：默认不开启，失败后按需）（`e4b104d`）
- infrastructure/integrity_checker.{h,cpp}：串行 ffmpeg `-c copy -bsf
  h264_mp4toannexb -f null` 计数 NAL 错误
- 转码/拼接失败后弹窗提议「开启完整性快检…」→ 逐文件检查 → 坏文件状态列
  标红「❌ 数据损坏」+ 汇总弹窗 + `完整性快检报告_<时间戳>.txt` 落盘输出目录

### 验证
全回归 11 套绿（preprocess 176）。待真机：①后门对面剔除 33 坏文件后重跑拼接
（产物名应为 LAMerged_ 规则）；②页面顶部路径行/浏览/独立输出弹框；③故意跑一次
坏文件触发失败 → 开启快检 → 标红+报告。

> **§46 收口注记（2026-08-17 用户确认）**：三项待真机全部通过——后门对面
> 重新导出坏文件后重跑成功（产物 LAMerged_ 命名生效）；页面路径行/浏览/
> 独立输出弹框正常；坏文件触发失败后快检标红+报告正常（P-50~P-53 勾销）。

### 补记：明景 1440p 拖拽卡顿根因 + 关键帧探测窗口 bug（`9488a0d`）
- 现象：明景拼接视频_20260722172528（2560×1440/20fps/30min/783MB）拖拽偶见卡顿，
  其余文件无——单文件 GO 提示"已是合格 MP4 无需处理"
- 排查：文件本身正常（时间戳连续、seek ≤20ms、引擎 scrub 51帧/48ms 与 640×360
  无异）；GOP=12.5s（DVR 拼接源典型）；1440p 每帧 CPU 缩放 ~10ms（V1_ERA §7.1
  记录的 P-29 GPU Stage1 驱动场景）→ 主因绘制背压、次因大 GOP
- **bug 实锤**：探测 kf=0ms（sparse=0）→ 误判合格。根因：关键帧采样窗口 600 包
  ≈9s（音频包占比高），12.5s GOP 只采到 1 个关键帧 → keyPts<2 → kf 保持 0
- 修复：窗口 6000 包 + 采样 40s + 4 关键帧即止；仍不足且非短片（>10s）保守
  标记需转码。实测：明景 kf=12500ms(sparse=1)、普通监控 2000ms 不变；全回归绿
- 待真机：明景文件单文件 GO → 应转码导出（2s 关键帧）→ 拖拽流畅；P-29 GPU
  Stage1 仍为根治项

### 补记 2：转码画质实测结论（用户质疑"是否无损"，三重验证 `2026-08-17`）
- 逐字节对比（像素流 cmp，100 帧 553MB）：**源 vs CRF0 完全一致** → 转码管线
  （时间戳归零/CFR/封装）零失真；CRF0 vs CRF18 PSNR 48.7dB（>40dB 视觉无损）
- 结论如实记录：**转码数学上有损**（CRF18 视觉无损级，肉眼不可辨；源 3.16Mbps
  → 产物 4.5Mbps 反而更大）；真正无损路径 = 直接拼接 `-c copy`（流拷贝零损失）
- 参数无暗改：CRF18 为 v1 起设计默认（方案 §5.5.1），高级选项 CRF 滑杆 0-51
  公开可调；源文件只读引用制永不改动（取证红线）
- 取证口径：转码产物=回放/分析副本；绝对无损=保留源文件/拼接路径

# ============================================================================

