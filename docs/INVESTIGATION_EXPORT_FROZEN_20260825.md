# 问题档案：选段导出完成后弹窗无法关闭 / 软件卡死

> 记录日期：2026-08-25（v1.15.3 周期）
> 经手人：LumenArc 开发（初查）｜下一步：更强专家接手
> 状态：🟡 用户拍板绕过（2026-08-25 收官）——用户实测确认「完成提示弹窗一直
> 卡住无法关闭」后拍板**弃用弹窗**，完成提示改走导出面板结果条+📂按钮+状态栏
> （见提交链末）。弹窗本体为何在该机器上关不掉未深究（曾尝试 exec/open/Accept/
> NonModal 四轮均无效）；若日后需弹窗再启用，按本文档第六节继续排查。

---

## 一、症状（用户原话记录）

1. 「导出的文件不对 而且导出完毕以后没有提示」（先前）
2. 「导出完毕后应该有个打开所在文件夹的选项，现在导出完一个就卡住不能再导出了」
3. 「修错了 现在根本关不了这个弹窗」
4. 「还是没修好 导出完成后弹窗没有消失也无法关闭 导致整个软件卡住」

核心症状：**导出完成 → 完成弹窗（QMessageBox）出现 → 按钮点击无效 / 弹窗不关闭 / 主界面卡死**。

## 二、时间线（关键！验证 exe 版本）

| 提交 | 内容 | exe 重链状态 |
|---|---|---|
| `9fe030c` | 导出完整提示初版：`QMessageBox::information`（静态） | ✅ 重链 |
| `5420a7d` | 连环弹窗修复（finished 槽重复 connect 堆叠→改一次性 connect）+ 弹窗加「打开所在文件夹」`ActionRole` | ✅ 重链 |
| `be5f70d` | `ActionRole`→`AcceptRole`（ActionRole 点击不关闭对话框） | ✅ 重链 |
| `8770a41` | **改为非模态 `box->open()` + `AcceptRole` + clicked→`accept()`** | ✅ 重链 |
| `d16d99b` | 去掉 OSD「演示副本」前缀（无关本问题） | ❌ **LNK1104 未重链**（用户 app 占用） |

⚠️ **用户是否测试过 `8770a41`（open() 版）无法确认**——该提交后仅提示过一次重链，用户反馈「还是没有解决」可能仍基于旧 exe。接手第一步应先确认复现者运行的 exe 版本。

## 三、已排查并确认的内容

1. **「导出冻结（画面静止）」是独立问题，已修复验证**（根因：DVR 流时间戳从 `start_time=62585s` 起算，导出引擎按 0 起算的流内毫秒比较 → curPtsMs 恒压过 target → 全产物首帧；修复：`startMs=fmt->start_time/1000` 归一 seek 与帧位置）。档案提交 `db9f9bb`。**与弹窗卡死无关，勿混**。
2. **finished 槽重复 connect**（`Qt::UniqueConnection` 对 lambda 无效，第 N 次导出完成弹 N 个模态窗）——已修（一次性 connect，产物路径 `m_lastExportPath` 成员传递）。提交 `5420a7d`。
3. `QMessageBox::ActionRole` 按钮点击默认**不关闭对话框**（Qt 语义）——已改 AcceptRole。

## 四、未闭环疑点（专家重点）

1. **`QMessageBox::exec()` 在 finished 槽（跨线程 queued 至主线程）内执行，按钮点击后 exec 不返回 / 主线程卡死**——原因未明。已改为 `open()` 非模态仍未见用户确认。
2. 理论上 `connect(m_segmentExporter, finished, this, lambda)` 跨线程 AutoConnection = QueuedConnection，槽应于主线程事件循环执行；未验证是否真的 queued（`emit finished` 发生在工作线程 `run()` 末尾）。
3. **从未本地复现**：开发机无法 GUI 操作导出流程，所有修复基于代码走查 + 引擎级测试（`lumenarc_segment_test` 的 `testEndToEnd`/临时 D15 用例均不弹窗）。
4. 待验证的旁路嫌疑：
   - 导出面板（非模态）与完成弹窗的**模态/激活窗口层级**冲突；
   - `m_exportDlg` 内 `setExportRunning(false)` 时序（finished 槽先 `setResult` 后弹窗）；
   - Windows 通知/explorer.exe `startDetached` 是否干扰；
   - 工作线程在 `emit finished` 后是否还有残留（`proc.waitForFinished(-1)` 在主循环后、`run()` 末尾）占用。

## 五、相关代码位置（接手锚点）

- 完成弹窗：`src/mainwindow.cpp` `startSegmentExport()` 内 `if (!m_segmentExporter) { ... }` 块（一次性 connect 的 finished 槽）
- 导出引擎线程：`src/infrastructure/segment_export_engine.cpp` `start()` / `run()` / 结尾 `emit finished(true, p.outputPath)`
- 导出面板：`src/segmentexportdialog.cpp`（`setResult` / `setExportRunning` / `m_openFolderBtn`）
- 引擎忙判定：`SegmentExportEngine::start()` 首行 `if (m_running)` → emit finished(false, "引擎忙")

## 六、接手建议步骤

1. 确认复现者 exe ≥ `8770a41`（含非模态 open() 版）；若否先重链重测。
2. 本地或录屏复现一次，抓主线程栈（WinDbg / Qt Creator attach）——定位是 exec 未返回还是事件循环无响应。
3. 在 finished 槽首尾加 `qDebug` 落日志文件（`qInstallMessageHandler` 或环境变量 QT_LOGGING），确认槽是否执行、弹窗代码是否进入、`open()` 后是否立即返回。
4. 验证 `connect` 的 receiver 线程与槽执行线程（可临时用 `QThread::currentThread()` 打印）。
5. 如确认 `open()` 版仍卡：怀疑点收敛到 QMessageBox 在"主线程事件循环被导出线程/管道等待占用"场景的行为，考虑彻底弃用 QMessageBox，改自绘轻量非模态提示（或系统托盘 toast）。