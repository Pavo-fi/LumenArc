# HTML 时间轴可视化 · 可行性研究与设计草案

- 状态：**已实施（M1 层）待真机验收**，立项 2026-09-30
  - 落地：`src/app/report_html_builder.*`、`src/domain/timeline_events.*`、`src/resources/report_template.html`、`tests/html_test_main.cpp`（78 断言）
  - 菜单：案件 → 「导出HTML报告(&H)...」；台账见 `docs/PENDING.md` P-81（P-26 复活）
  - 另带命令行 `lumenarc_html_sample`（`tests/html_sample_main.cpp`）：不打开案件，对一批视频/快照/事件表直接出展示件
  - 阶段划分：给了「阶段种子」（事件表 csv 第 5 列）就走人工阶段，否则按间隔启发式；
    实测启发式在真实实验数据上会切出 1/1/13/1/1 这种碎段 → 真出件建议给阶段名
  - 事件来源三条：**书签（= 图表标签 ChartLabel，随 .vla 存）** / 关键帧快照（文件名时间码） / 事件表 csv
  - **未校时的案件不给假日期**：标签只有流内毫秒时按「流内时刻」呈现（卡片带「流内」角标、
    页头注明「未校时」），低于 2000-01-01 的 wallMs 不再当 epoch 格式化成 1970 年
  - 滚动显现是锦上添花，不能当唯一开关：IntersectionObserver 不触发时有 load/定时/滚动 兜底
  - 未做：M2 软件内原生面板、M3 事件编辑 UI（目前只能靠事件表 csv 导入）
- 参考实现：`C:\Users\MJ\Desktop\20260920佛山顺德\11.文字材料\火灾模拟实验时间轴_2026-09-29.html`（12.3 MB，单文件版）
- 侦察依据：`docs/PENDING.md`、`src/app/report_service.*`、`src/app/report_docx_builder.*`、`src/mainwindow_ui.cpp`、`src/domain/report_data.h`、`CMakeLists.txt`

---

## 0. 结论摘要

1. **这件事技术上完全可行，而且项目早就为它留了缝**：`ReportData` 在 3 处注释里被定义为"渲染器的唯一输入"，明确预留了"远期 HTML 渲染器"接口（`domain/report_data.h:12`、`infrastructure/docx_writer.h:12-13`、`app/report_docx_builder.h:12`）。
2. **不需要引入 QtWebEngine**。软件内不做网页渲染，走「生成单文件 HTML → 系统浏览器打开」；想在软件内看，则用原生 QPainter 面板（另一条腿，见 §4.2）。
3. **真正的工作量不在 HTML，而在"事件数据"**。参考 HTML 的 `title`（"高锰酸钾包装袋被烧穿"）、`note`（一句话说明）、`phase`（起火/扩大/蔓延）**全部是人工写的**；LumenArc 现在没有存储这些的结构化字段，只有散落的快照文件名与图表标签。
4. **历史决策需要复核**：`docs/PENDING.md:30` 记录 **P-26「报告 HTML 版」已于 2026-08-16 被用户拍板砍掉**；`PENDING.md:37`（P-28）写明"只出 DOCX，远期 HTML 渲染器接口预留"。本次需求应明确为 **P-26 的窄口径复活（仅时间轴可视化，不是完整 HTML 报告）**，或作为新条目立项。
5. 建议分两层：**M1 数据+导出器（低风险，1 个新 builder + 1 处菜单）→ M2 软件内原生时间轴面板**。

---

## 1. 参考 HTML 拆解

### 1.1 数据模型（纯内联 JSON，无后端）

```js
DATA[]   = {seq, time:"16:24:13", sec, rel:"1分34秒", pct, title, note, phase:"a|b|c",
            thumb:"#3", full:"#3", orig:"原始文件名.png"}      // 17 条
PHASES[] = {id, name:"起火与初期发展", short, color:"#ff6b35", range:"16:24:13 — 16:25:51", n}
SUMMARY[]= {k:"起烟 → 明火", a, b, sec, rel}                     // 关键间隔速览表 9 行
TOTAL    = 235                                                    // 秒
IMG[]    = [base64...]                                            // 单文件版内嵌；多文件版为 null
function S(s){ return (IMG && s[0]==='#') ? IMG[+s.slice(1)] : s; } // 双形态兼容的唯一开关
```

`S()` 这一个函数就实现了「单文件 / 多文件」两种产物共用同一份模板 —— 这个设计值得原样继承。

### 1.2 交互清单（即功能验收项）

| 交互 | 实现要点 |
|---|---|
| 阶段筛选 | 顶部胶囊按钮，按 `phase` 过滤卡片 + 阶段分隔条 |
| 刻度条 | 每个节点一个可点刻度，带 tooltip（时间/相对时间/标题） |
| 自动播放 | 3.2 s 一个节点，依次跳转高亮 |
| 滚动显现 | `IntersectionObserver` 淡入 |
| 灯箱 | 点缩略图放大：上一张/下一张、Esc 关闭、左右方向键、下载原图（`orig` 名） |
| 关键节点速览 | 表格 + 进度条，展示"起烟 → X"的耗时 |
| 自检模式 | `?selftest=1` 输出卡片数/图片加载数/灯箱状态等到角标 |

### 1.3 双产物形态与体积实况

| 形态 | 本文件实测 | 适用场景 |
|---|---|---|
| 单文件（图片 base64 内嵌） | 12.3 MB / 17 张（每张 ≈ 0.9 MB base64） | 交付、发给办案人员，双击即看 |
| 多文件（`IMG=null`，引用相对路径） | HTML ≈ 30 KB + 图片目录 | 案内归档 |

> ⚠️ **体积红线**：按当前做法线性外推，**100 个节点 ≈ 70 MB 单文件 HTML，浏览器滚动会明显卡顿**。
> 必须做「缩略图 + 原图」两级：缩略图长边 ~640 px（后端 QImage 缩放，约 60–90 KB），原图仍走 base64 或落在 `assets/` 目录。这样 100 节点单文件可压到 **8–15 MB**。

---

## 2. LumenArc 现状

### 2.1 技术底座

- Qt 6（`CMakeLists.txt:23`：`Widgets Charts Multimedia Concurrent Network Test`），C++17。
- **全仓无 `QWebEngineView` / `QWebEnginePage` / `QtWebView` / `setHtml`**。唯一的 `QTextDocument` 用法在 `src/chartpanel.cpp:35`，只是量文本宽度。
- 打开外部内容一律 `QDesktopServices::openUrl()`（`mainwindow_ui.cpp:96,340,506`）。
- 颜色必须走 `Theme::` 令牌（`src/theme.h`），文案必须走 `lang("中文","English")`（`src/i18n.h:18`）。

### 2.2 官方预留的扩展缝（重要）

| 位置 | 注释含义 |
|---|---|
| `src/domain/report_data.h:12` | `ReportData` 是渲染器唯一输入 |
| `src/app/report_docx_builder.h:12` | 远期 HTML 渲染器同吃 `ReportData` |
| `src/infrastructure/docx_writer.h:12-13` | 同上，渲染器缝 |
| `src/domain/preprocess_task.h:98` | **死字段** `QString reportHtmlPath;`（全仓无赋值/无读取，纯钩子） |

### 2.3 现有报告/导出管线（HTML 直接挂上去）

```
案件菜单「生成分析报告(&G)」  mainwindow_ui.cpp:273-341
  └─ ReportPreflightDialog（自检+补录闸门）
  └─ 工作线程 ReportService::collect(cm, vsm, true, cb, &cancel)   report_service.cpp:132
       └─ 进度/取消回调 cb(阶段, 0~1)，返回 false = 取消
  └─ GUI 线程 ReportService::renderChartImages(rd)                 report_service.cpp:520
  └─ ReportDocxBuilder::build(rd, out)  → 空串=成功，非空=错误文案  report_docx_builder.cpp:31
  └─ 输出：<caseDir>/reports/火灾视频分析报告_<yyyyMMdd_HHmmss>.docx
  └─ 完成：QMessageBox + 「打开文件夹」→ QDesktopServices::openUrl
```

案件目录结构（`src/app/case_manager.cpp:114`）：`case.json` / `videos/` / `evidence/` / `preprocess/` / `reports/`(+`assets/`) / `snapshots/`。

### 2.4 现成可用的数据源

| 数据 | 来源 | 证据 |
|---|---|---|
| 事件（视频内标注） | `ChartLabel{timeMs, text, color}`，存 .vla `labels[]` | `domain/analysis_snapshot.h:25`、`domain/timeline_model.cpp:913` |
| 事件绝对时间换算 | `TimeCalibration::wallMsOf()` / `beijingMsOf()` | `domain/time_calibration.h:152,163` |
| 案件级节点表（已按墙钟升序） | `ReportData::nodes`（`ReportNodeRow{wallMs, sourceLabel, text}`） | `domain/report_data.h:64,103`、`report_service.cpp:422` |
| 机位道 | `CaseVideoRef.cameraLabel`、`CaseCameraGroup{camNo,name}` | `domain/case_model.h:44,63` |
| 机位墙钟区间 | `buildCamLanes()` → `CamLane{wallStartMs, wallEndMs}` | `app/cam_timeline.h:28` |
| 快照图 | `<caseDir>/snapshots/*.png`，时间码编码在文件名 | `report_service.cpp:499`、`mainwindow_snapshot.cpp:189` |
| 校时证据帧 / 曲线光栅 / 点位图 | `row.evidencePhotos` / `row.chartPng` / `rd.sitemapPng` | `report_data.h:38-40,63` |
| 跨机事件锚点 | `EventAnchor{refLaneId,eventName,toleranceMs}` | `domain/event_calib.h:30` |

### 2.5 缺口（本功能的核心成本）

1. **没有事件结构化数据**：`title` / `note` / `phase` 无处存。快照只有 PNG + 文件名，**连 sidecar JSON 都没有**（`mainwindow_snapshot.cpp:191`），无事件表、无统一 ID。
2. **快照时间只能从文件名反解**：`<视频basename>_<yyyyMMdd_HHmmss>.png`（未校时是 `tHH-MM-SS`）。可解析但脆弱（用户手改文件名即失效）。
3. **无缩略图**：`CaseDock` 是运行时 `QPixmap::scaled()` 现算（`src/casedock.h:85-89`），导出需要后端批量生成。
4. **无阶段分组**：没有 phase/阶段标记，参考 HTML 的 a/b/c 分组是人工划分的。

### 2.6 与历史规划的冲突点（必须先解决）

| 编号 | 原文 | 影响 |
|---|---|---|
| P-26 | 「报告 HTML 版 → ✅ 已砍（2026-08-16 用户拍板）」 | **直接冲突**：本次需求要么明确为"仅时间轴可视化、非完整报告"，要么正式复活 P-26 |
| P-28 | 「只出 DOCX（…）；远期 HTML 渲染器接口预留」 | **支持**：接口缝是官方预留给这件事的 |

---

## 3. 路线对比

| 路线 | 软件内展示 | 新依赖 | 打包体积 | 视觉/交互上限 | 工作量 | 结论 |
|---|---|---|---|---|---|---|
| **A. 生成单文件 HTML → 系统浏览器** | ✗（跳到浏览器） | 无 | 不变 | = 参考 HTML 全量 | 小 | ✅ **推荐做主出口** |
| **B. 内嵌 QtWebEngineWidgets** | ✅ WYSIWYG | **+1 个 Qt 模块** | +100~150 MB；portable 版/CI/签名全要重过 | = 参考 HTML | 中 | ❌ 否决。与轻量 portable 定位冲突，且 P-29 GPU 前科说明显示栈风险高 |
| **C. 原生 QPainter 时间轴面板** | ✅ 可联动播放器 | 无 | 不变 | 中（无 sticky/滚动动画，但可做得更像"取证工具"） | 中 | ✅ **推荐做第二层** |
| **D. QTextBrowser 预览** | 半成品 | 无 | 不变 | 极低（无 flex/grid/JS，图片要相对路径） | 小 | ⚠️ 只能当"图片列表 + 文字"降级预览，不建议 |

**推荐：A（导出）+ C（软件内查看），共享同一份数据模型。**

---

## 4. 推荐方案

### 4.1 M1 — 数据模型 + HTML 导出器（最小可用，低风险）

**新增 1 个 builder，镜像 DOCX builder 的签名**：

```cpp
// src/app/report_html_builder.h  （对照 app/report_docx_builder.h:17-19）
class ReportHtmlBuilder {
public:
    /// 成功返回空串；失败返回错误描述（与 ReportDocxBuilder::build 同口径）
    static QString build(const ReportData &rd, const QString &outPath,
                         const HtmlBuildOptions &opt = {});
};
struct HtmlBuildOptions {
    bool singleFile = true;      ///< true=图片 base64 内嵌；false=引用 ./assets/
    int  thumbMaxEdge = 640;     ///< 缩略图长边（体积红线，见 §1.3）
    int  jpegQuality  = 82;      ///< 缩略图转 JPEG 可再省 3-5×
};
```

**产物**：
- 单文件：`<caseDir>/reports/时间轴_<yyyyMMdd_HHmmss>.html`（图片 base64 内嵌）
- 多文件：`<caseDir>/reports/timeline_<ts>/index.html` + `assets/`，需要带走时用现成的 `ZipStoreWriter`（`infrastructure/zip_store_writer.h`）打成 zip

**UI 入口**：在 `mainwindow_ui.cpp:273-341` 的 `m_genReportAction` 之后新增独立菜单项「导出时间轴可视化(&T)...」，复用 `ReportService::collect` + 进度条 + 取消，然后在 builder 里多写一个文件。

**完成提示必须避开已知崩溃口径**：`docs/INVESTIGATION_EXPORT_FROZEN_20260825.md` 记录用户已拍板**弃用完成后的 `QMessageBox::exec()` 模态弹窗**（跨线程 finished 槽里 exec 会卡死）。所以：
- ✅ 状态栏提示 + `QDesktopServices::openUrl(QUrl::fromLocalFile(htmlPath))` 直接开浏览器
- ❌ 不要复制 `mainwindow_ui.cpp:328/333` 的 `critical`+`exec()` 写法（那处本身就是待整改的遗留）

### 4.2 M2 — 软件内原生时间轴面板

`TimelinePanel : public QWidget`（自绘，参照 `src/cliptimelinewidget.cpp` / `src/chartpanel.cpp`）：

- 竖向卡片流（`QScrollArea` + 自绘卡片），左侧时间轴竖线 + 阶段色带
- 缩略图用 `QImage` 缩放缓存，点击 → `signals: eventActivated(qint64 wallMs, QString videoId)`
- **这是 A 路线做不到、也是最有价值的一点**：接 `MainWindow` 的播放器，**点击事件直接 seek 到对应视频的那一帧**
- 阶段筛选、上一/下一事件、键盘 ←/→（与放大镜/播放器快捷键不冲突需检查）
- 数据源：`ReportData::nodes` 或新的 `TimelineEvent[]`（见 §5.1）

### 4.3 M3 — 事件编辑与阶段划分（可选，决定"好不好用"）

参考 HTML 的 `title/note/phase` 必须有人写。三条路，建议按顺序做：

1. **零新增录入**：直接复用 `ChartLabel.text` 当 title（用户在曲线面板打标签时就在写事件名）。
2. **快照补 sidecar**：保存快照时写 `<caseDir>/snapshots/<同名>.json`（wallMs、videoId、note）；或把快照登记进 `case.json`（`CaseMeta` 已有 `extraFields` 可先落）。
3. **阶段自动分段**：按相邻事件间隔阈值（如 > 20 s）自动切段并配色，用户可改名 —— 不做手工阶段编辑器。

---

## 5. 详细设计要点

### 5.1 事件数据模型（建议新增，落 `case.json` 或 `reports/timeline.json`）

```jsonc
{
  "version": 1,
  "generatedBy": "LumenArc v1.18.0", "generatedAt": "2026-09-30 10:12:33",
  "caseNo": "...", "caseTitle": "...",
  "events": [{
      "seq": 1, "wallMs": 1759134253000, "videoId": "V001", "cameraLabel": "C01 东侧",
      "title": "开始冒烟", "note": "…", "phase": "a",
      "snapshotPath": "snapshots/xxx_20260929_162413.png",
      "labelId": null, "source": "snapshot|chartLabel|manual"
  }],
  "phases": [{"id":"a","name":"起火与初期发展","color":"#ff6b35"}]
}
```

原则：**渲染器只吃这个 JSON**，与 `ReportData` 解耦；这样模板可独立演进，也让 M2 面板和 M1 导出共用一份数据。

### 5.2 模板化渲染（不要用 C++ 拼整页）

- 把参考 HTML 的 `<style>` 与 `<script>` 原样存为资源：`src/resources/timeline_template.html`，`resources.qrc` 注册（现已有 `logo.png` 在 `src/resources.qrc:4`）。
- builder 只做三件事：读模板 → 替换 `/*__DATA__*/`、`/*__PHASES__*/`、`/*__META__*/` 占位 → 按 `opt` 决定 `IMG` 是 base64 数组还是 `null`。
- **所有注入文本必须 `QString::toHtmlEscaped()`**（对照 `chartpanel.cpp:1861`），标题里的 `&`/`<` 会直接破版。
- 拼接用 `QByteArray::append` 或 `QStringList::join`，**不要 `s += ...` 循环**（12 MB 字符串反复重分配会翻倍吃内存）。
- 头部统计区（参考 HTML 的 `.stats`）建议放：案件号、生成时间、软件版本、节点总数、时间跨度、以及**导出文件的 SHA-256**（与 DOCX 报告同口径，保持取证可追溯）。

### 5.3 代码落点清单

| 动作 | 文件 |
|---|---|
| 新增 builder | `src/app/report_html_builder.h/.cpp` |
| 新增数据模型 | `src/domain/timeline_event.h`（或并入 `report_data.h`） |
| 新增模板资源 | `src/resources/timeline_template.html` + `src/resources.qrc` |
| 菜单入口 | `src/mainwindow_ui.cpp:273-341` 附近（或 `exportMenu`，`mainwindow_ui.cpp:421`） |
| 数据采集补充 | `src/app/report_service.cpp:499`（快照时间反解 / sidecar 读取） |
| CMake 登记 | `CMakeLists.txt` 的 `SOURCES` 段 |
| 测试 | `tests/html_timeline_test_main.cpp`（对齐 `tests/docx_test_main.cpp` 风格） |
| 文档 | 本文件 + `docs/PENDING.md` 增补条目 |

### 5.4 测试断言建议（纯 domain，可 headless 跑）

1. `toHtmlEscaped`：标题含 `<script>`/`&`/中文引号 → 输出转义正确
2. 节点按 `wallMs` 升序；同刻多节点稳定排序
3. 阶段分组数量 = `phases.n` 之和；空阶段不产生分隔条
4. 单文件模式：`IMG` 为数组且长度 == 唯一图片数；多文件模式：`IMG === null` 且路径为相对路径
5. base64 头 `data:image/jpeg;base64,` 正确；缩略图长边 ≤ `thumbMaxEdge`
6. `DATA.length == 0` 时产出合法空页（不崩、不出现 `undefined`）
7. 相对时间文案：39 s → `39秒`、94 s → `1分34秒`、3661 s → `1小时1分1秒`
8. 生成文件可被 `QXmlStreamReader`/正则校验闭合标签配对（模板未被文本破坏）

---

## 6. 风险与坑

| 风险 | 说明 | 对策 |
|---|---|---|
| **体积失控** | 100 节点按原样 ≈ 70 MB | 缩略图两级 + JPEG + 多文件模式给大数据集 |
| **完成弹窗卡死** | 已知 P0 级前科 | 禁 `exec()`，走 openUrl/状态栏 |
| **中文编码** | Windows 下 UTF-8 写出 | 模板声明 `<meta charset="UTF-8">`；用 `QFile` + `QByteArray` 写 UTF-8，勿经本地 8-bit 转换 |
| **离线可用性** | 办案环境可能无网 | 模板**零外部 CDN**（参考 HTML 已是纯内联，保持） |
| **证据效力** | HTML 是展示件不是证据 | 页脚写明"本页由 LumenArc 自动生成，仅供展示，证据以原始检材为准"+ 生成时间/版本/哈希 |
| **模板与代码漂移** | 版式改动要重编译 | 模板放 qrc 是折中；若想免编译换版式，可支持从 `reports/templates/` 覆盖 |
| **i18n** | 交付件是否要英文版 | M1 只做中文；`lang()` 预留 en 模板 |

---

## 7. 待用户拍板

| # | 问题 | 备选 |
|---|---|---|
| Q1 | 这个功能和已砍的 **P-26（报告 HTML 版）** 是什么关系？ | ①窄口径复活（仅时间轴）②新建条目 ③并入 P-28 远期渲染器 |
| Q2 | 首要目标是**导出交付**还是**软件内查看**？ | A 先 / C 先 / 两个都要 |
| Q3 | 事件标题/说明从哪来？ | ①复用图表标签 text ②新增快照 sidecar + 编辑 UI ③纯自动按间隔分段 |
| Q4 | 默认产物形态？ | 单文件（交付友好）/ 多文件+zip（大数据集） |
| Q5 | 缩略图参数 | 长边 640 / JPEG 82，是否认可 |
| Q6 | 是否复活 `reportHtmlPath` 死字段，还是另立 `timelineHtmlPath` | — |

---

## 8. 建议排期（按现有批次节奏）

| 批次 | 内容 | 预估 |
|---|---|---|
| ① | 数据模型 + 模板资源 + builder（单文件）+ 8 条单测 | 1 个批次 |
| ② | 菜单入口 + 进度/取消 + 多文件模式 + zip 打包 | 1 个批次 |
| ③ | 原生时间轴面板（M2）+ 点击 seek 联动播放器 | 1–2 个批次 |
| ④ | 事件 sidecar / 阶段自动分段（M3） | 视用户反馈 |

验收口径建议：用 `20260920佛山顺德` 这个案子的真实快照跑一次，产出 HTML 与参考文件逐项对照 §1.2 交互清单。
