# LumenArc 文档体系与维护规矩（DOCS MAP）

> 本文档是项目全部文档的**唯一索引**：文档类型、清单、状态、维护规矩。
> 生成日期：2026-08-16。新增/修改文档必须同步本表（规矩 D2）。

---

## 一、文档类型（五类）

| 类型 | 放哪 | 谁看 | 生命周期 |
|---|---|---|---|
| **T1 用户文档** | 根目录 | 终端用户/调查员 | 随功能演进，发版必同步 |
| **T2 工程文档** | `docs/` | 开发者/维护者 | 施工前写方案，实施后标状态 |
| **T3 过程记录** | 根目录（HANDOVER/WORK_HISTORY） | 开发者交接 | HANDOVER 5 批窗口；WORK_HISTORY 只增不改 |
| **T4 管理文档** | `docs/`（PENDING/DOCS_MAP） | 所有人 | 每次批次后必更新 |
| **T5 合规/验证** | 根目录 | 合规审查 | LICENSE 不动；验证报告随结论更新 |

**放置纪律**：用户文档与过程记录在根目录；方案/设计/清单一律 `docs/`；
`build_tmp/` 禁止存放任何文档（历史重复档已于 2026-08-16 清理）。

## 二、文档清单（全量，25 份）

### T1 用户文档
| 文档 | 用途 | 状态 |
|---|---|---|
| README.md | 项目主页：功能/下载/快捷键/构建 | ✅ 通用文案，无需逐版改 |
| MANUAL.md | 操作手册（用户） | ✅ 已含 v1.5/v1.7 说明 |
| 追光者 Lumen Arc v1.0 — 操作手册.pdf | 发版同步产物 | ⚠️ 版本号滞后（发布时重生成） |
| docs/DEVELOPER_LETTER_CN.md | 致用户信（v0.5→v1.0 总结） | ✅ 历史性文档，不再更新 |
| docs/ANALYSIS_PRINCIPLES_CN.md | 亮度/语谱分析通俗原理 | ✅ 稳定 |

### T2 工程文档
| 文档 | 用途 | 状态 |
|---|---|---|
| DEVELOPMENT_STANDARDS.md | 开发规范 R1-R10/C1-C6/F1-F6 + 技术债表 | ✅ 现行 |
| docs/V1_ERA_TECH_PLAN_CN.md | v1 时代总体方案（v1.2~v1.9 + Q-1~Q-20 决策） | ⚠️ 已修正 §1.3 版本表（.vla 现 v9；v1.8 改 v10） |
| docs/DEVELOPMENT_PLAN_V1.3_CN.md | v1.3 案件模块施工方案 | ✅ 已实施封版（v1.3.0） |
| docs/DEVELOPMENT_PLAN_V1.7_CN.md | v1.7 前处理 v2 施工方案 | ✅ 已修正状态：已实施（dfb4f7c），Q1-Q6 回填 |
| docs/DEVELOPMENT_PLAN_V1.6_CN.md | v1.6 GPU 显示管线 Stage 1 施工方案（P-29） | ✅ 已拍板（2026-08-17，待施工） |
| docs/DEVELOPMENT_PLAN_V1.8_CN.md | v1.8 P1a/P1b 任务化+通道化施工方案（P-30，.vla v10 + Python 退役） | ✅ 已实施（2026-08-17 施工完成，待真机点检） |
| docs/RELEASE_CHECKLIST_V1.8_CN.md | v1.8 手工点检 A-E | ⚠️ 待真机执行（离线项已自动验证） |
| docs/DEVELOPMENT_PLAN_V1.9_CN.md | v1.9 P2 MainWindow 拆分施工方案（P-31，四组件 + R2/R3/R5 收口） | ✅ 已实施（2026-08-17 施工完成，待真机点检） |
| docs/RELEASE_CHECKLIST_V1.9_CN.md | v1.9 手工点检 A-F（行为冻结对照） | ⚠️ 待真机执行（离线项已自动验证） |
| docs/PREPROCESSING_TECH_DESIGN_CN.md | 前处理技术设计 | ✅ 现行 |
| docs/MULTICAM_PLAYBACK_TECH_DESIGN_CN.md | 多视频同步播放（多机时间线合并）技术方案（P-57） | ✅ v0.3 已拍板待施工（2026-08-18 两轮） |
| docs/SEGMENT_EXPORT_TECH_DESIGN_CN.md | 选段变速播放+图表/语谱一并导出技术方案（P-58） | ☐ 草案待拍板（2026-08-18） |
| docs/PREPROCESSING_UI_REDESIGN_CN.md | 前处理 UI 重设计 | ✅ 已落地（14.3） |
| docs/RELEASE_CHECKLIST_V1.3_CN.md | v1.3 手工点检 A-H | ⚠️ 结果未填写（待真机执行，见 PENDING P-48） |
| docs/FUTURE_OPTIMIZATIONS.md | 暂缓/移除功能记录（GPU 零拷贝、VLC） | ✅ 重启条件已写 |

### T3 过程记录
| 文档 | 用途 | 状态 |
|---|---|---|
| HANDOVER.md | 交接文档（最近 5 批 §39-43） | ✅ 现行 |
| WORK_HISTORY.md（3254 行） | 全部批次历史存档（〇~四十三批） | ✅ 只增不改 |

### T4 管理文档
| 文档 | 用途 | 状态 |
|---|---|---|
| **docs/PENDING.md** | 待办总表（唯一登记处） | ✅ 2026-08-16 重建 |
| **docs/DOCS_MAP.md** | 本文档 | ✅ 2026-08-16 新建 |

### T5 合规/验证
| 文档 | 用途 | 状态 |
|---|---|---|
| LICENSE（Apache 2.0）/ THIRD_PARTY_LICENSES | 合规 | ✅ |
| QUANTITATIVE_ANALYSIS.md | 亮度/语谱定量验证报告 | ✅ 历史性，新引擎验收时更新 |

## 三、维护规矩（D1-D8，违反视为文档 bug）

- **D1 待办单一登记处**：批次记录里写"待真机/待拍板/遗留"的每一项，**必须同时登记到 PENDING.md**；PENDING 是唯一勾销处，完成时注明批次。
- **D2 文档索引同步**：新增/删除/改名任何文档，必须同步 DOCS_MAP.md；本表"全量清单"不允许缺项。
- **D3 方案文档状态机**：标题处标注 `草案 → 待拍板 → 已拍板 → 施工中 → 已实施/已封版`；实施完成（或放弃）当批必须改状态，禁止"方案已过期但文档不说"。
- **D4 版本信息四处一致**（F5/Q5 延伸）：代码版本号、README、MANUAL、应用内关于对话框；.vla/case.json 格式号与 V1_ERA §1.3 对照表一致。
- **D5 点检清单不许留空**：RELEASE_CHECKLIST_<版本>.md 每项必须有结果；未走完的点检项禁止封版，遗留项必须转 PENDING。
- **D6 归档纪律**：HANDOVER 只留 5 批（R2），超出整体移入 WORK_HISTORY 末尾；归档动作与表头同步；build_tmp 不存文档。
- **D7 批次记录必含待办出口**：每批结尾"验证"段写清：已过/待真机（→PENDING）/遗留（→PENDING），不写"等反馈"这种无出口的话。
- **D8 文档命名**：方案 `DEVELOPMENT_PLAN_<版本>_CN.md`、点检 `RELEASE_CHECKLIST_<版本>_CN.md`、设计 `*_TECH_DESIGN_CN.md`；版本号改动时必须建新文件或重命名，禁止新内容塞旧文件名。

---

*本表由 2026-08-16 文档整理批次建立（一次性完成：分类→建索引→建待办总表→修滞后方案→清重复归档）。*
