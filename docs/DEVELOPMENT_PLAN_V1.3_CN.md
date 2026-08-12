# LumenArc v1.3.0 案件模块 — 施工方案（终稿）

> 编制：2026-08-13（六模块讨论全部拍板后定稿）
> 基线：`LumenArc_v1.0 remake` master `v1.2.2`
> 设计溯源：`docs/V1_ERA_TECH_PLAN_CN.md` §4 + 本文 §8 拍板记录（冲突处以本文为准）

---

## 1. 版本定位与底线

**案件 = 证据容器**：视频、ROI/标签/辅助线、分析结果、前处理产物、校时证据统一入案；案件列表/归档/打包移交；视频指纹（SHA-256）保证完整性。预估 2.5~3 周。

**三条底线**：
1. **独立模式（不建案件）行为与 v1.2.2 逐点一致**——既有全部测试零修改通过；
2. **取证红线**：源视频只读（引用制）；重新定位只改引用路径，绝不动 .vla 内任何数据；显示/时间轴/导出三者一致；
3. **视频是否"在案"决定其行为**（per-video membership），不是全局开关。

## 2. 目标形态

### 2.1 案件目录

```
<案件根目录>/<编号>-<名称>/
├── case.json                 # 档案登记表（magic+formatVersion=1+迁移，原子写，.lock 防双开）
├── manifest.json             # 完整性清单（机器维护，用户无感）
├── videos/V###.vla           # 全部 .vla 集中
├── evidence/calibration/<V###>/   # 校时证据帧（体积优化后，见 3.6）
├── preprocess/<yyyyMMdd_HHmmss>/{report.csv, operations.log, sidecars/*.lumencal.json}
├── reports/                  # v1.4.0 产物
├── snapshots/
└── sources/V###__原名.mp4    # 【仅完整包】视频副本
```

案件根目录：独立设置项（QSettings `case/rootDir`），默认 `<程序目录>/cases/`。

### 2.2 case.json 数据模型（终稿）

```cpp
struct CaseVideoRef {
    QString id;                 // "V001" 单调递增，移除后不复用
    QString originalPath;       // 原始绝对路径（重定位只改它）
    QString vlaRelPath;         // videos/V001.vla
    qint64  sizeBytes = 0, mtimeMs = 0;
    QString sha256;             // 空=未算
    QRectF  timestampRoi;       // 时间戳框选记忆（随案，替代 QSettings）
    bool    hasCalibration = false;      // 徽标缓存（.vla 为校时 SSOT，不复制数据）
    QString calibrationSummary;          // 徽标文案，写 .vla 时同步刷新
    QString bundledRelPath;     // 【仅完整包】sources/V###__原名.mp4
};
struct CasePreprocessRef {
    QString sessionDirRelPath, reportCsvRelPath;
    QVector<CaseVideoRef> outputRefs;   // 拼接输出（引用制，同视频待哈希）
    QStringList sidecarRelPaths;        // 复制入案的 .lumencal.json
};
struct CaseMeta {               // formatVersion=1；F1/F3：版本上限报错、未知字段忽略
    QString caseNo;             // 自动生成 YYYYMMDD-城市区县-x（x=a,b,c…），创建后固定
    QString title, investigator, unit;          // 必填
    qint64  incidentTimeMs = 0; // 案发时间（必填，QDateEdit）
    QString city, district;     // 案发地点=城市+区县（必填，编号索引源）
    QString locationDetail;     // 详细地址（选填）
    QString description;        // 备注（选填）
    qint64  createdMs = 0, modifiedMs = 0;
    QString lastVideoId;        // uiState：开案恢复现场
    QVector<CaseVideoRef> videos;
    QVector<CasePreprocessRef> preprocessSessions;
    QStringList reports;
    QHash<QString,QString> extraFields;
};
```

### 2.3 代码落点（R1/R4 合规）

| 文件 | 层 | 内容 |
|---|---|---|
| `domain/case_model.h/.cpp` | domain | 数据结构 + case.json 读写 + 迁移框架（不依赖 Widgets） |
| `app/case_manager.{h,cpp}` | app | 生命周期/视频登记/哈希队列(QThreadPool×1低优先级)/完整性校验/导出/重定位；**唯一 SSOT** |
| `casedock.{h,cpp}` | ui | 案件面板证据树（替代视频列表，仅案件模式） |
| `casedialogs.{h,cpp}` | ui | 新建/属性/批量重定位/校验报告/导出对话框 |
| `startpagewidget.{h,cpp}` | ui | 起始页 |
| `multicamview.{h,cpp}` | ui | 多机时间线对齐只读视图 |
| `tests/case_test_main.cpp` | test | 新增测试 harness |

## 3. 里程碑与任务卡

### M1：domain + app 层（第 1 周，5.5 人日）

| # | 任务 | 要点 | 预估 |
|---|---|---|---|
| 1 | case_model + case.json 读写 + 迁移 + 单测 | magic/版本上限/未知字段忽略；原子写(tmp→rename) | 2 d |
| 2 | CaseManager 骨架 | create/open(读+迁移+.lock)/close/save/dirty；recentCases(QSettings×10)；信号 caseOpened/Closed/dirtyChanged | 1 d |
| 3 | 视频登记 + 哈希队列 | addVideo(V###递增不复用/size+mtime登记/入队)；QThreadPool×1+LowestPriority+1MB分块；hashProgress逐文件；开案自动补缺失；变更(size/mtime)标⚠重算 | 1.5 d |
| 4 | 完整性校验 | 快扫+差异重算；报告数据(一致/变更/缺失)；全部重算入口；manifest 刷新 | 1 d |
| 5 | **证据帧体积优化** | probe_timestamps.py：ROI裁剪(或命中crop块)+只留命中帧+首尾全帧2张+JPEG q90；引擎参数；实测 904MB→≤20MB | 1 d（含验证） |

### M2：挂接 + 主 UI（第 2 周，7 人日）

| # | 任务 | 要点 | 预估 |
|---|---|---|---|
| 6 | .vla 路径分流 | `CaseManager::vlaPathFor(videoPath)`；三处挂接（自动保存 mainwindow.cpp:2467 / 缓存探测 :1651 / 手动存取 :1733,1768）；入案视频缓存探测不弹询问 | 1 d |
| 7 | 打开/添加行为 | Ctrl+O 自动入案；「临时打开(不入案)」菜单项；拖入面板入案；重复路径拒绝/同内容仅提示；源旁已有 .vla 询问导入(默认是,复制) | 1 d |
| 8 | 校时/前处理挂接 | CalibrationService 证据目录 override（入案→evidence/calibration/V###）；前处理横幅「案件模式：成果自动导入《XX》」+【导入案件(默认路径)】/【独立输出(自选)】；finalize 自动登记+sidecar 复制归类 sidecars/ | 1.5 d |
| 9 | 框选记忆迁移 | 入案读写 case.json timestampRoi；注册表旧值只读复制一次(保留一版)；独立模式照旧 | 0.5 d |
| 10 | CaseDock + 模式出口 | 证据树四组；徽标(⏳✓⚠✗+校时✓)；右键(重定位/移除/算指纹/复制指纹/资源管理器显示)；标题栏✕退出+菜单关闭(Ctrl+W)+状态栏📁标识；窗口标题带案件名；退出不中断播放 | 2 d |
| 11 | 案件对话框 + 起始页 | 新建(编号自动生成预览/必填校验/城市+区县标准化)/属性/根目录设置；起始页三钮+最近10条+空态引导文案 | 1 d |

### M3：移交 + 视图 + 收尾（第 3 周前半，4 人日）

| # | 任务 | 要点 | 预估 |
|---|---|---|---|
| 12 | 导出移交包 | 完整包默认/轻量可选；导前自检(未算可即补/缺失可即定位/知情可仍要导出)；空间预检；后台可取消+半成品清理；完整包拷 sources/+包内 case.json+manifest+导后快校；README.txt | 1.5 d |
| 13 | 批量重新定位 | 文件夹名+大小模糊匹配→人工确认；**定位后强制指纹比对**，不一致默认拒绝、显式【仍要采用】留档；VideoStateManager 键迁移 | 1 d |
| 14 | 多机时间线对齐视图 | 只读；各路 .vla 校时→墙钟块位/重叠/缺口；复用 ClipTimelineWidget 画法；<2 路已校时置灰；**末位，吃紧可砍** | 1 d |
| 15 | 文档与封版 | MANUAL 新章/README/手工点检清单；全回归；版本号 1.3.0 五处；打标签 | 0.5 d |

## 4. 测试用例表

### 4.1 新增 `lumenarc_case_test`（四件套模式，offscreen）

| 组 | 用例 |
|---|---|
| 模型 | case.json 往返全字段 / 版本上限报错 / 未知字段忽略+提示 / v1 迁移链 / 原子写(崩溃无半文件) |
| 编号 | 自动生成 YYYYMMDD-城市区县-x / 同日同区县递增 a→b / 创建后改时间地点编号不变 / V### 移除不复用 |
| 生命周期 | 建-开-存-关 / dirty 追踪 / .lock 双开拒绝+残留锁提示 / 最近案件 10 条溢出 |
| 哈希 | 入队-完成-回写 / 开案补缺失 / size 变更标⚠重算 / 取消(关案)安全 |
| 校验 | 全一致 / 篡改一个文件必报(变更) / 删一个必报(缺失) / 全部重算路径 |
| 分流 | 入案 .vla 落 videos/V###.vla / 未入案落视频旁(与 v1.2.2 一致) / 三处挂接点各验 |
| 打包 | 完整包(含 sources+README+包内 manifest) / 轻量包(无 sources) / 取消半成品清理 |
| 重定位 | 名+大小匹配 / 指纹一致接受 / 不一致默认拒绝+仍要采用留档 / 键迁移后状态可恢复 |
| 迁移 | QSettings 框选记忆只读复制一次 / 案件内读写优先 / 独立模式照旧 |

### 4.2 回归（零修改通过为准绳）

engine 28 项 / calibration 73 / piecewise 96 / preprocess 168 / ui_chain 23 / ocr 21 / vla / reconstruction(B3 素材环境)。

### 4.3 手工矩阵

新建→加 3 视频(含中文路径)→校时→框选记忆随案→关案重开(ROI/标签/校时/框选/现场全恢复)→删源视频重开(重定位)→完整包换机零操作可用→轻量包重定位后可用→篡改一个文件校验必报→4K 播放中哈希队列不引入卡顿→双模式逐点比对→多机视图(≥2 已校时)。

## 5. 风险与对策

| 风险 | 对策 |
|---|---|
| 独立模式被污染 | 分流集中 CaseManager 空指针分支；回归零修改准绳；双模式手工清单 |
| case.json 损坏/并发 | 原子写 + .lock + 损坏备份 case.json.corrupt-<ts> 明确报错 |
| 哈希 IO 抢播放 | 单线程最低优先级 1MB 分块；jitter 场景回归 |
| 证据帧优化改变 OCR 行为 | 优化只动存图路径/格式，不动识别输入；ocr_atpositions 21 + B3 回归兜底 |
| 重定位误匹配 | 名+大小模糊+人工确认+指纹强制比对，默认拒绝 |
| 完整包大拷贝中断 | 后台可取消+半成品清理+空间预检+导后快校 |

## 6. 与 v1.4.0 的衔接钩子

CaseMeta 编号/名称/调查员/单位/案发时间/地点（报告头部）；CaseVideoRef 哈希清单（报告强制列出，未算可即补）；reports/ 目录与 reports 登记位；多机视图可截屏入报告（后话）；⚠错读点"OSD 疑似错读，时间不可信"随报告标注（v1.2.2 遗留项，落报告章节）。

## 7. 文档维护清单（封版时）

HANDOVER 第十章基线→v1.3.0、第一梯队✅、新增第二十三章（案件模块实施记录）；`V1_ERA_TECH_PLAN_CN.md` §1.3 对照表修正（.vla 实际已 v9）。

## 8. 拍板记录（2026-08-13，与旧文冲突处以本节为准）

1. 移交包：**完整包默认/轻量包可选**（修订 Q-8"轻量唯一"）；不做添加时拷入案件
2. 校时数据不入 case.json（.vla SSOT，仅存徽标缓存）
3. 案件字段：原有 + 案发时间 + 案发地点（城市+区县，标准化必填）；编号自动 `YYYYMMDD-城市区县-x` 创建后固定；不设状态字段（后用 extraFields 补）
4. 案件根目录设置，默认 `<程序目录>/cases/`
5. V### 单调递增不复用；源旁 .vla 询问导入(默认是,复制)；Ctrl+O 自动入案；「临时打开(不入案)」；移除默认不删数据；固定按编号排序
6. 模式出口三处（面板✕/Ctrl+W/状态栏）；per-video membership 规则
7. 哈希四触发点；变更不弹窗；校验默认快扫+差异重算
8. 重定位强制指纹比对，不一致默认拒绝、仍要采用留档
9. 导出前自检可仍要导出；完整包导后自动快校
10. 起始页含空态引导文案；多机视图做（末位可砍）
11. sidecar 复制入案归类 sidecars/；前处理横幅【导入案件/独立输出】选择
12. 框选记忆迁案（注册表旧值留一版）；v1.2.x 旧证据帧目录不动
13. 证据帧体积优化全做（ROI裁剪+只留命中帧+首尾全帧2张+JPEG q90，904MB→≤20MB）

---

*方案结束，可开工。*
