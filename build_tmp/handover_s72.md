# ============================================================================
# 工作记录（2026-08-22，第六十三批）——P-73 多机同事件间接校时（v1.13.2）
# ============================================================================

## 72. 同事件对时：以已校时路为参考给未校时路生成正式校时（取证链入档）

### 拍板（用户三连）

①一步到位（单锚偏移+多锚仿射同期）；②事件名必填（报告可读性依赖）；
③确认卡独立取证链小节。追加拍板：**允许多跳链**（现实考量）+ **成环禁止**
+ 累积容差逐跳如实呈现。

### 架构

- **域层 `src/domain/event_calib.h`**（纯函数，无头可测）：
  - `EventAnchor`{refLaneId/refStreamMs/refWallMs(快照)/targetStreamMs/
    eventName/markedAtMs/toleranceMs}——参考墙钟取**录入时刻快照**，防参考
    校时后改导致链断；
  - `fitAnchors`：0 锚→EVENTCALIB_NO_ANCHOR（C1 类型化）；1 锚→偏移型；
    ≥2→最小二乘仿射，rate≤0 或 |rate−1|>0.2 → EVENTCALIB_BAD_RATE 拒收
    （对错事件防御）；目标时刻全同退化偏移型；
  - `wouldCreateCycle`：上游可达性递归（多锚点多参考逐边）；
  - `expandChain`/`cumulativeToleranceMs`：取证链展开+逐跳容差累加；
  - `frameToleranceMs`=500/min(fps)（较粗路半帧）。
- **TimeCalibration**：Source 增 `CrossCamEvent`（序列化 "crosscamevent"）+
  `eventAnchors` 成员随 cal JSON 一体入 .vla（`event_anchors` 键，老读取端
  忽略未知键向后兼容）——**无需改 project_io/timeline_model 模式**。
- **MultiCamSyncService::applyLaneCalibration**：换 cal+转正（temporary 摘帽）
  +偏移清零+重算区间+广播。
- **MultiCamPlaybackWindow 对时模式**：工具行「同事件对时」（≥2 路全载且
  有校时路可见；与对齐会话互斥）；进入强制**每路独立进度条**（合并条无法
  分路定位事件帧，rebuildTimelineArea 加 m_eventCalib 条件）+滑条四处理复用
  对齐路径（m_aligning||m_eventCalib）；右列 310px 面板：参考/目标路下拉
  （间接路标「（间接）」）、双路帧步进钮、事件名（必填校验）、锚点表
  （残差列）、生成校时并预览（播放联动即生效）、保存（确认卡模态：拟合
  摘要+**独立取证链小节**+累积容差）、退出（未保存=会话级，明示）。
- **落盘**：ProjectIO 载旧 .vla 全字段→仅替换 calibration→写回（保其他
  分析成果）；路径分流 suggestSavePath（案件 videos/V###.vla / 独立源旁）。

### 置信度与诚实性

间接校准天然低一档：单锚 conf=0.6；≥2 锚低残差（≤200ms）0.8，否则 0.7；
残差逐锚列出；链上每跳容差与累积容差在确认卡如实声明。

### 测试

- sync 103→**135**：拟合 6 组（空/单/双/带噪/退化/病态拒收+容差）+
  成环守卫 5 组（多跳允许/A←C 成环/自环/无锚新参考）+ 链展开+累积容差 +
  JSON 回环（source/锚点/中文事件名/无锚老数据兼容/rate 换算生效）；
- **变异验证**：偏移符号反转 → ecfit 红，回退绿；
- 全回归 15 套绿。版本 → **v1.13.2**。

### 排雷记

- heredoc 超长被截断（bash 报 PYEOF 未终结）——大块代码改用 write 写
  build_tmp 零件再 python 拼装；
- sed 批量版本号会误改历史注释（v1.13.1→v1.13.2 把 §71 注释洗版）——
  教训：版本 sed 后必须回查注释行，本批已逐行修正（仅标题栏保留）。
