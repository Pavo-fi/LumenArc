# LumenArc v1.3.0 案件模块 — 版本方案

> 编制日期：2026-08-13
> 基线：`LumenArc_v1.0 remake` master `91f24c0`，最新标签 `v1.2.0`
> 设计依据：`docs/V1_ERA_TECH_PLAN_CN.md` §4（案件模块，Q-5~Q-10 已全部拍板，本文不重复论证，只做执行拆解）
> 版本号规则（2026-08 定）：第一位=大架构重构；第二位=功能优化；第三位=Bug 修复

---

## 一、前置收尾批次 v1.2.2（1~3 天，立刻可做，穿插进行）

校时管线已稳定（救复批次验证矩阵全绿），封版 v1.2.2，对齐文档与 exe 版本号。

| # | 任务 | 落点 | 预估 |
|---|---|---|---|
| 1 | **封版 v1.2.2** | CMakeLists `project(VERSION 1.2.2)`、aboutdialog、app.rc、Info.plist；HANDOVER 首部最新标签 + 第十章"当前基线"改 v1.2.2；`git tag v1.2.2` + Release 打包 | 0.5 天 |
| 2 | **GO 预检加"第三点确认"** | 现状：预检取首尾两点拟合 overallRate，任一点被 OCR 错读 → 误判变速 → 白跑 8 分钟重建。修法：预检增采中点（或 1/3+2/3 两点），三点共线校验（残差超阈值 → 提示"OSD 疑似错读，请重新框选/人工确认后再 GO"），`timesettingsdialog.cpp` quickCheck 链路 + `ocr_atpositions` 测试场景补用例 | 1 天 |
| 3 | **GO 完成 Windows toast 通知** | 重建 ~8 分钟、用户最小化等待场景：`QSystemTrayIcon::showMessage`（任务完成/失败两态），仅在窗口最小化或非激活时弹；TimeSettingsDialog 任务完成回调处接入 | 0.5 天 |
| 4 | **ui_chain_test 补进 CMake** | tests/ui_chain_test_main.cpp 已有源码无 target：CMakeLists 仿照 `lumenarc_calibration_test` 加 `add_executable(lumenarc_ui_chain_test ...)` + 链接依赖；本机无 cmake，需在有 cmake 环境 configure 验证 + CI 自检步骤挂载 | 0.5 天 |
| 5 | （挂 v1.4，不在本批）校时窗口 ⚠ 错读点随报告显式标注"OSD 疑似错读，时间不可信" | 依赖 v1.4 报告模块的报告上下文，记入 v1.4.0 范围 | — |

**出口**：v1.2.2 标签 + Release 包；预检三点用例绿；ui_chain_test 在 CI 跑通。

---

## 二、v1.3.0 定位

**案件 = 证据容器**：视频、ROI/标签/辅助线、分析结果、前处理产物、校时证据统一入案；案件列表/归档/打包移交；.vla 关联案件存储；**框选记忆（现 QSettings）迁入案件目录**。

已拍板决策摘要（V1_ERA §4，执行时不得翻案）：

- **目录制案件**（Q-7）：`case.json`（格式 v1，魔数+版本+迁移，F1 合规）+ `videos/`（全案件 .vla 集中）+ `evidence/` + `preprocess/` + `reports/` + `snapshots/` + `manifest.json`（逐文件 相对路径+size+sha256）
- **视频引用不复制**（NFR2 源文件只读延续）：登记 `originalPath + size + mtime + sha256`，失踪给"重新定位"对话框
- **移交打包 = 轻量包唯一形态**（Q-8：不打视频）+ 包内 README.txt 移交说明
- **哈希必算**（Q-9）：登记即时 size/mtime；SHA-256 闲时后台队列（QThreadPool 单线程、低 IO 优先级、逐文件完成即提示 ⏳→✓）；「统一计算哈希」按钮；「校验案件完整性」复核
- **起始页必做**（Q-10）：无视频时主区显示最近案件 + 新建/打开；CaseDock **替代**视频列表（案件模式），独立模式视频列表照旧
- **多机时间线对齐视图**（Q-5）：自 v1.2.0 移入本版本，复用 ClipTimelineWidget 绘制模式，只读视图
- **独立模式（无案件）行为完全不变** —— 兼容性底线
- **格式号**：.vla 保持现状（实际已 v9：time_calibration 含分段重建）不变，案件仅持引用；case.json 新增 v1

## 三、任务分解与里程碑（2.5 周）

| 里程碑 | 任务 | 预估 |
|---|---|---|
| **M1 domain+app 层**（第 1 周） | ① `domain/case_model.h`（CaseMeta/CaseVideoRef/CasePreprocessRef）+ case.json 读写 + F1/F2 迁移框架 + 单测（2 人日）② `app/CaseManager`：create/open/close/save、addVideo（V### 分配）、闲时哈希队列 + 逐文件提示、computeAllHashes、verifyIntegrity、recentCases（QSettings 最近 10 条）、case.json.lock 双开防护（2.5 人日） | 4.5 人日 |
| **M2 挂接 + 主 UI**（第 2 周） | ③ MainWindow/VideoListPanel/PreprocessWindow 挂接：案件模式 .vla 写 `videos/V###.vla`（不再写视频同目录）、dirty 提示沿用现有模式、Preprocess finalize 自动 importPreprocessSession、**独立/案件双模式回归**（3 人日）④ CaseDock 证据树（视频 V###+校时徽标+哈希状态 / 前处理会话 / 报告 / 截图）+ 案件对话框 + 起始页（3.5 人日）⑤ **框选记忆迁移**：`calibration/roi_*` 从 QSettings 迁入 case.json（CaseVideoRef 增加 timestampRoi 字段；案件模式读写案件、独立模式保留 QSettings 原路径不变；旧 QSettings 数据只读迁移一次后停用）（0.5 人日） | 7 人日 |
| **M3 收尾封版**（第 3 周前半） | ⑥ 多机时间线对齐视图（Q-5 移入）⑦ 轻量包移交 exportPackage（含 README.txt、可取消、半成品清理）+ 集成测试 ⑧ MANUAL 新章 + README + 手工点检清单 ⑨ 全回归 + 版本号升 1.3.0 + 打标签 | 3 人日 |

**新增测试**（沿用四件套模式）：`tests/case_test_main.cpp` + `lumenarc_case_test` target —— case.json 往返、版本迁移（v1 上限/未知字段）、哈希队列完成序、完整性校验漏检/误报、轻量包内容清单、双模式 .vla 路径分流。

## 四、验收标准

1. 自动：既有全量（engine 28 项 + calibration 73 / piecewise 96 / ocr 21 / preprocess 168 / ui_chain 16 / reconstruction / vla）+ 新增 case_test 全绿；
2. **双模式底线**：独立模式全流程（打开→分析→保存 .vla 到视频同目录→重载）与 v1.2.2 逐点一致；
3. 手工：新建案件→加 3 视频（含中文路径）→校时→框选记忆随案复用→关案重开（ROI/标签/校时/框选全在）→删掉一个源视频重开（重新定位对话框）→打包移交→换目录解开（重新定位后完整可用）→校验完整性（篡改一个文件必报）；
4. 哈希后台队列在 4K 播放期间不引入卡顿（jitter 场景回归）。

## 五、风险与对策

| 风险 | 对策 |
|---|---|
| 双模式行为分歧污染独立模式 | 独立模式代码路径零改动原则；挂接点全部走 CaseManager 空指针分支；双模式回归清单 |
| case.json 并发写损坏 | 单案件实例 + .lock 文件；写盘沿用 .vla 的 QtConcurrent 异步+完成回执模式 |
| 闲时哈希 IO 抢占播放 | 单线程低 IO 优先级、GUI 空闲泵出；jitter 回归兜底 |
| 视频重新定位误匹配 | 文件名+大小模糊匹配 + 人工确认；重定位不改 vla 内任何时间数据（取证红线） |
| 框选记忆迁移丢数据 | 迁移只读复制，QSettings 旧值保留一个版本周期；独立模式行为不变 |

## 六、与 v1.4.0 的衔接

报告模块（v1.4.0，1.5-2 周）以案件为数据骨架：案件信息（CaseMeta）+ 校时时间线 + 亮度/音频分析 + 截图标签 + 前处理证据 + 结论栏 → DOCX（python-docx，bundled Python）输出至 `reports/`。**v1.3.0 必须为报告备齐**：CaseMeta 调查员/单位字段、哈希清单（报告强制列出所引用视频哈希，未算可补算）、`reports/` 目录与 CaseMeta.reports 登记位、⚠错读点标注数据通道（v1.2.2 收尾第 5 项落地于报告章节）。

## 七、文档维护清单（本版本落地时同步）

- HANDOVER.md：第十章"当前基线"→v1.2.2；第一梯队 v1.3.0 行标 ✅；新增第二十二章（案件模块实施记录）；.vla 格式号表述修正（v9 已在库，v1.8.0 通道化升 v10 待重估）
- `docs/V1_ERA_TECH_PLAN_CN.md` §1.3 对照表：v1.2.x 实际写到 v9（分段重建），v1.3.0 行 .vla 列改为"v9 不变"

---

*方案结束*
