# C 组 面板/模式 规范差距审计（LumenArc v1.3.0）

> 审计角色：规范差距审计（只读，不改代码）
> 裁决依据：`LumenArc_设计规范_v1.1.md` §6 / §8 / §9 / §10 / §11
> 审计范围：9 个源文件（C 组：案件/案件打开/片段时间线/显示调节/播放调节/快照/图表/频谱）
> 令牌基准（`src/theme.h`）：`Accent=#F0B429`（品牌金）、`TextMuted=#5C6270`、`TextSecond=#9AA0AB`、`Border=#333947`、`Danger=#E5484D`、`DataPalette`=Okabe-Ito 6 色
>
> 严重度图例：❌ 明确违规 · 🟡 偏离/边界 · ✅ 已核对且合规

---

## 0. 结论速览

| 章节 | ❌ | 🟡 | 主要文件 |
|---|---|---|---|
| §6 禁 emoji 控件 | 10 | 2 | `casedock.cpp`（核心）、`playbackadjustpanel.cpp`、`case_open_panel.cpp` |
| §8 数据可视化 | 4 | 7 | `roi_model.cpp`、`chartpanel.cpp`、`spectrogrampanel_enhanced.cpp`、`cliptimelinewidget.cpp` |
| §9 模式库 | 2 | 2 | `case_open_panel.cpp`、`videolistpanel.cpp`（违规）；`casedock.cpp`（✅ 做得好） |
| §10 i18n/文案 | 3 | 3 | `snapshotpanel.cpp`、`casedock.cpp`、`case_open_panel.cpp`、`playbackadjustpanel.cpp` |
| §11 动效 | 0 | 0 | 本组文件不含动效（N/A） |

**最高优先级（建议先修）**
1. `casedock.cpp` 大量禁用 emoji（🗑🔄📷⏰📦🗜📄📑📁）——规范 §14 追溯表声称"按钮已去 emoji"，**与当前代码不符**。
2. `snapshotpanel.cpp` 整面板硬编码英文、完全未走 `lang()`。
3. `videolistpanel.cpp`「清空」为破坏性操作却**无确认对话框**。
4. `roi_model.cpp::polygonColor()` 用了**非 DataPalette** 色板，破坏"序列色只能从 DataPalette 顺序取"与色盲友好一致性。
5. A/B 选段填充 / 播放游标 在图表与频谱两处均**偏离 §8 样式规格**。

---

## §6 禁止 emoji 用在控件上

**规范**：🗑🔄📷⏰📦 等装饰 emoji 不得出现在控件/按钮/树节点上；状态语义字符 ✓ ⚠ ✗ ⏳ 允许作为**文本徽标**保留。

### ❌ `src/casedock.cpp`（系统性违规）
| 行 | 位置 | 违规 emoji | 说明 |
|---|---|---|---|
| 79 | `m_btnDelete` 按钮 | 🗑 | `lang("🗑 删除选中", …)` —— 明确禁用 |
| 85 | `btnRefresh` 按钮 | 🔄 | `lang("🔄 刷新", …)` —— 明确禁用 |
| 199 | 机位组树节点 | 📷 | `📷 %1（%2 个文件）` |
| 221 / 265 | 校时标记 | ⏰ | 已校时前缀 ` ⏰` —— 明确禁用 |
| 225 / 272 | 包内副本 | 📦 | `📦 文件名` —— 明确禁用 |
| 270 | 机位名 | 📷 | ` 📷 cameraLabel` |
| 302 | 前处理会话 | 🗜 | `🗜 %1（%2 输出）` |
| 320 | sidecar 文件 | 📄 | `📄 文件名` |
| 339 | 报告文件 | 📑 | `📑 文件名` |
| 355 | 快照文件 | 📷 | `📷 文件名` |
| 414 | 标题标签 | 📁 | `📁 %1\n%2` |

> ⚠️ **追溯表失真**：规范 §14 明确写"§6 emoji 禁令 → casedock.cpp 等 → 按钮去 emoji"，但 v1.3.0 现网代码仍全部保留上述 emoji。属于"声称已修、实际未修"，应在追溯表中更正或补齐修复。

### 🟡 边界项（单字符排版符号，非彩色 emoji，风险低）
- `casedock.cpp:462,485` —— `▶ ` 正在播放前缀。单色排版符号、表意"播放中"，可接受；若追求严格一致建议改用状态徽标或纯文字。
- `playbackadjustpanel.cpp:55` —— `⟳ 顺时针 90°`。⟳ 为旋转语义符号（单色），低风险。
- `case_open_panel.cpp:144` —— `✕ 关闭`。✕ 关闭符号，低风险。

### ✅ 合规核对
- `casedock.cpp` 状态徽标 ✗ ⚠ ⏳ ✓ 均为**文本徽标**，符合 §6 允许项。

**修复建议**：`casedock.cpp` 按钮/树节点全部去 emoji，改用"图标（qrc SVG）+ 文字"或纯文字；校时/副本等状态用 §6 允许的语义字符或颜色徽标表达。

---

## §8 数据可视化

**规范要点**（v1.1 L191-197）：
- 序列色**只能**从 `DataPalette` 顺序取；第 7 条起循环并**改虚线**。
- 坐标轴/刻度文字：`TextMuted` Caption；网格线：`Border` 50% 透明、**只画横线**。
- 播放游标：`Accent` **1px 实线**；游标读数用 Mono。
- A/B 选段：`Accent` **15% 填充 + 1px 边界**。
- 语谱图色阶属"数据"、固定不随主题变。
- 不画饼图/3D/阴影渐变。

### ❌ 序列色：多边形 ROI 用了非 DataPalette 色板
- `src/domain/roi_model.cpp:221-234` `polygonColor()`：
  `#FF6464 / #FFB450 / #FFDC64 / #64DC82 / #64C8FF / #A082DC / #DC82B4`（浅红/杏黄/金/薄荷/天蓝/薰衣草/玫瑰）——**均不在 DataPalette（Okabe-Ito）**。
- 对照 `roi_model.cpp:115-128` `regionColor()`（矩形）：正确引用 Okabe-Ito ✅。
- **后果**：矩形 ROI 与多边形 ROI 用两套色板，同一图表内"区域 N"与"多边形 N"颜色规则不一致；且多边形色板非色盲友好，违反"序列色只能从 DataPalette 顺序取 / 色盲友好、新增序列不得出板"（L192 / L234）。
- **修复建议**：`polygonColor()` 改为复用 `DataPalette`（可用连续下标或独立偏移，保证不与矩形撞色），删除自定义色板。

### ❌ A/B 选段填充（图表）偏离规格
- `src/chartpanel.cpp:173-174`：
  ```cpp
  m_abHighlight->setBrush(QBrush(QColor(86, 180, 233, 25))); // 天蓝 ~9.8% 透明
  m_abHighlight->setPen(Qt::NoPen);
  ```
- 规范要 `Accent(金)` 15% 填充 + **1px 边界**；实际：颜色=天蓝(非金)、透明度≈10%(非15%)、**无 1px 边界**。
- **修复建议**：`setBrush(QBrush(QColor(Theme::Accent, 255*0.15)))` + `setPen(QPen(QColor(Theme::Accent), 1))`。

### ❌ 播放游标：颜色/宽度/线型（频谱）全偏
- `src/spectrogrampanel_enhanced.cpp:303-304` 与 `:499`：
  ```cpp
  QPen(QColor(0xFF, 0x98, 0x1C), 2, Qt::DashLine);   // 橙色 #FF981C，2px 虚线
  ```
- 规范：`Accent`(#F0B429 金) **1px 实线**。实际：颜色=橙(非金)、宽=2px、**虚线**。三项全偏。
- **修复建议**：`QPen(QColor(Theme::Accent), 1, Qt::SolidLine)`。

### ❌ 坐标轴/刻度文字：频谱用橙色（应 TextMuted）
- `src/spectrogrampanel_enhanced.cpp:478-499`（renderHeatmapImage）、`:562-616`（drawAxes）：
  `labelColor = #FF981C`（橙），`tickColor = #FF981C@180`；频率轴刻度、时间轴刻度、轴竖线全用橙色。
  单位提示 "Hz" 用 `#9AA0AB`(TextSecond)（:480）——更接近但仍非 TextMuted。
- 规范：坐标轴/刻度文字应 **TextMuted(#5C6270)** 弱化。橙色属品牌强调色，用于刻度会喧宾夺主。
- **修复建议**：刻度/轴文字统一 `Theme::TextMuted`，仅保留游标用 Accent。

### 🟡 坐标轴/刻度文字：图表过亮且非令牌色
- `src/chartpanel.cpp`：轴标题/轴标签/图例/游标读数/时间刻度均用 `#F5F0E8`（近白）：
  `:64`(legend)、`:80,81`(X 轴)、`:87,88`(Y 轴)、`:136,142`(游标读数)、`:1348,1397,1471,1490`(时间刻度)、`:1357,1407`(主刻度 `#9AA0AB`)、`:1439,1456`(次刻度 `#4A5060`)、`:1478,1497`(首尾刻度 `#A0A0A0`)、`:1418`(基线 `#3A4152`)。
- 规范：刻度文字应 TextMuted；网格线应 `Border` 50% 且**只画横线**。当前刻度文字过亮（近白）且多为硬编码灰（非令牌）。
- **修复建议**：刻度文字统一 `Theme::TextMuted`；如需横线网格用 `Border`+alpha128，去掉非令牌灰度。

### 🟡 播放游标：图表为 2px 虚线（色对、线型/宽度偏）
- `src/chartpanel.cpp:120-123`：
  ```cpp
  QPen cursorPen{QColor(Theme::Accent)}; // 品牌金 ✅
  cursorPen.setWidth(2);                  // ❌ 应 1px
  cursorPen.setStyle(Qt::DashLine);       // ❌ 应实线
  ```
- 颜色合规（Accent 金），但**宽度 2px、虚线**偏离"1px 实线"。
- **修复建议**：`setWidth(1); setStyle(Qt::SolidLine);`。

### 🟡 序列循环"改虚线"未实现
- `roi_model.cpp:127 / 233` `colors[index % colors.size()]` 会循环取色 ✅，但 `chartpanel.cpp:1007,1013` 系列画笔恒为 `pen.setWidth(1)` 实线，**第 7 条起未改虚线**。仅当 ROI>6 时显现。
- **修复建议**：对 `index >= DataPalette.size()` 的序列 `setStyle(Qt::DashLine)`。

### 🟡 片段时间线：group 0 用品牌金、下标错位
- `src/cliptimelinewidget.cpp:206-208`：
  ```cpp
  fill = clip->groupIndex == 0 ? QColor(Theme::Accent)
                               : Theme::DataPalette[clip->groupIndex % ...];
  ```
- group0→金（非 DataPalette[0]），groupN→DataPalette[N]（跳过 DP[0]）。违反"序列色只能从 DataPalette 顺序取"，且与图表"区域0=DP[0] 朱红"**跨面板不一致**。
- **修复建议**：统一 `DataPalette[clip->groupIndex % size]`，"主片段"高亮改由描边/加粗而非替换数据色表达。

### 🟡 硬编码非令牌色（图表其它）
- `chartpanel.cpp:1095` 微变"首帧"标记 `Qt::red`（纯红，非 Danger #E5484D）。
- `spectrogrampanel_enhanced.cpp:616` 错误提示 `#FF6464`（非 Danger）。
- `casedock.cpp:481` 正在播放高亮底 `QColor(0x2A,0x4A,0x6E)`（硬编码蓝，非令牌）。
- 均属"语义色之外的新颜色"（§1.3），建议收敛到令牌。

### ✅ 合规核对
- **无饼图/3D/面积图/阴影渐变**：全 src 未见 `QPieSeries/QBarSeries/QSurface3D/QLinearGradient/QRadialGradient/QConicalGradient`（L197 合规）。
- **语谱图色阶**（`buildColorLUT`，:520-555）提供 Thermal/Inferno/Viridis 三种**固定 LUT**、非彩虹/jet、不引用主题令牌 → 符合"色阶固定不随主题变"（L196）。
- **交互能力**：图例可点击切换系列（`chartpanel.cpp` legend toggle）、悬停高亮（LabelDotItem / guide hover）、Tooltip 全文 —— 均具备。

---

## §9 模式库（空态 / 加载 / 错误 / 破坏性确认）

### ❌ 空态：欢迎面板多句 + 超字数
- `src/app/case_open_panel.cpp:121-122`：
  > "暂无最近案件。\n点击「新建案件」开始：视频、ROI 分析、校时证据、前处理成果将统一入案管理，可校验完整性、打包移交。\n或选「独立模式」直接进入（与旧版一致）。"
- 规范：空态 **≤1 句 / ≤20 字**。当前 3 句、数十字，且夹带"（与旧版一致）"括号教学。
- **修复建议**：空态压缩为一句如"暂无最近案件。"，引导操作放按钮/副文案而非空态正文。

### ❌ 破坏性确认：「清空」视频列表无确认
- `src/videolistpanel.cpp:46`（`m_clearBtn = lang("清空","Clear")`）→ `:230 onClearClicked()` → `clearVideos()`（:~232）**直接清空、无任何 `QMessageBox`**。
- 全文件 `videolistpanel.cpp` 无 `QMessageBox/确认/不可恢复`。
- 规范：破坏性操作需确认。清空全部视频属不可恢复操作。
- **修复建议**：`onClearClicked` 先弹 `QMessageBox::warning` + `DestructiveRole`「清空」+ "此操作不可恢复！"，与 `casedock.cpp` 一致。

### ✅ 破坏性确认（做得好，可作模板）
- `src/casedock.cpp` 多处规范落地：删除选中视频(`:527,572`)、删除前处理会话(`:813,818`)、删除输出(`:842,846`)、删除文件(`:878,882`)——均为 `warning` 图标 + 文案"此操作不可恢复！" + 主按钮 `DestructiveRole`「删除」（动作本身命名，非"确定"）。

### 🟡 错误文案：个别弹窗标题未"结论先行"
- `casedock.cpp:712` 标题 `新建机位组`（应"新建机位组**失败**"）。
- `casedock.cpp:923,927` 标题 `机位组改名`（应"改名**失败**"）。
- 规范：错误提示**结论先行**。多数已合规（如"删除失败"），仅上述 2 处标题用"动作名"而非"失败结论"。
- **修复建议**：标题改为 `新建机位组失败` / `改名失败`。

### 🟡 ToolTip 超一行 25 字
- `casedock.cpp:82` 删除按钮 tooltip："删除选中的视频：源文件在案件内则一并删除；案件外仅删分析结果（不可恢复）" —— 远超 25 字且含括号。
- **修复建议**：压到一行 ≤25 字，细节移入 hover 二级提示或首次引导。

---

## §10 i18n / 文案

### ❌ 快照面板整块硬编码英文、未走 `lang()`
- `src/snapshotpanel.cpp:15,16,28,35,42`：
  `"Capture Frame"` / `"Clear"` / `"Brightness"` / `"Contrast"` / `"Opacity"` —— 全部为**硬编码英文**，无 `lang()`。
- 规范 §10.4：所有面向用户字符串须经 `lang()` 并配中英双语。此面板完全未接入 i18n。
- **修复建议**：全部改 `lang("中文","English")`。

### ❌ 树节点字符串内硬编码中文（未走 `lang()`）
- `src/casedock.cpp:199` `📷 %1（%2 个文件）` —— "个文件"为硬编码中文（在 `QStringLiteral` 中，非 `lang()`）。
- `src/casedock.cpp:302` `🗜 %1（%2 输出）` —— "输出"为硬编码中文。
- 规范：用户可见文本须 `lang()`。这两处随 emoji 去除应一并包进 `lang()`。

### 🟡 括号教学（§10.1 删一切括号教学）
- `case_open_panel.cpp:83` 按钮 `独立模式\n（不使用案件）` —— 按钮标签夹括号说明。
- `case_open_panel.cpp:111` 标题 `最近案件（双击打开）` —— 括号教交互。
- `case_open_panel.cpp:122` `（与旧版一致）` —— 括号教学。
- `playbackadjustpanel.cpp:44` 复选框 `反色（负片）` —— 括号释义（低风险，但属括号教学）。
- **修复建议**：去掉括号，交互提示改 ToolTip/首次引导；按钮标签只留"独立模式"。

### ✅ 合规核对
- `casedock.cpp` / `videolistpanel.cpp` / `case_open_panel.cpp` / `playbackadjustpanel.cpp` 的绝大多数用户串已正确使用 `lang()`（中英双语），基础 i18n 体系健全；问题集中在上述少数硬编码点。

---

## §11 动效

- 本组 9 个文件**不含动效**：`chartpanel.cpp:382 setDuration()` 是"设置视频时长以定 X 轴范围"的数据方法，**非动画**；`QPropertyAnimation` 全部位于 `mainwindow_*.cpp`（本组范围外）。
- **结论**：§11 动效时长/取证区域特效规则对本组文件 **N/A**，无可核对项。建议动效审计并入 mainwindow 组。

---

## 附：文件清单与行数
| 文件 | 行 | 主要问题章节 |
|---|---|---|
| `src/casedock.cpp` | 934 | §6(❌10)、§10(❌2/🟡)、§9(✅模板) |
| `src/chartpanel.cpp` | 2598 | §8(❌2/🟡) |
| `src/spectrogrampanel_enhanced.cpp` | 1103 | §8(❌2/🟡) |
| `src/videolistpanel.cpp` | 338 | §9(❌) |
| `src/domain/roi_model.cpp` | — | §8(❌ polygonColor) |
| `src/app/case_open_panel.cpp` | 199 | §9(❌空态)、§10(🟡) |
| `src/snapshotpanel.cpp` | 64 | §10(❌) |
| `src/cliptimelinewidget.cpp` | 278 | §8(🟡) |
| `src/playbackadjustpanel.cpp` | 185 | §6(🟡)、§10(🟡) |
| `src/displayadjust.cpp` | 29 | N/A（纯 LUT 计算，无 UI） |