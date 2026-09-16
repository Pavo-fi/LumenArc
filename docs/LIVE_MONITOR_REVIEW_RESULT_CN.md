# 监控直播（RTSP 接入）— 评审结果

> 对应清单：`docs/LIVE_MONITOR_REVIEW_CN.md`　评审日期：2026-09-12
> 方式：逐行精读 5 个新文件 + 集成点核对 + 自动化证据复现

## 0. 自动化证据复现

| 证据 | 清单声明 | 复现结果 |
|---|---|---|
| `lumenarc_live_source_test` | 23/0 | ✅ **23 checks / 0 failures**（一致） |
| `lumenarc_mw_test` 回归 | 117/0 | ✅ **129 checks / 0 failures**（用例数已增至 129，0 失败） |
| `LumenArc.exe` | 通过 | ✅ 产物存在，时间戳 17:09:32 晚于最新源码改动 17:09:03（本 shell 无 cmake，未现场重编，采信增量构建证据） |
| `FfmpegVideoEngine` 未动 | 一行未动 | ✅ `git status` 修改列表中无任何文件播放引擎文件 |

## 1. 线程安全与生命周期

| 项 | 结论 | 证据 |
|---|---|---|
| 1.1 m_url 持锁 | ✅ 符合 | 写 `live_stream_engine.cpp:70`、读 `:237`、清空 `:126` 均在 QMutexLocker 内，无裸访问 |
| 1.2 中断回调无锁 | ✅ 符合 | `:195` `interruptCb` 仅 `isAbort()` 两个原子读，无锁/无分配/无 emit |
| 1.3 m_abort 双重语义 | ✅ 可接受 | `stop()`→`play()` 竞态终态为播放中；UI 层另有 m_active 门控 |
| 1.4 计数不下溢 | ✅ 符合 | `ackFrame()` CAS 循环 `while (v > 0)`（`:181-190`），不会为负 |
| 1.5 丢帧不计数 | ✅ 符合 | `displayFrame()` 上限判断（`:435`）在 `fetch_add` 与 `emit` 之前 |
| 1.6 退出释放 | ✅ 符合 | `workerMain()` 循环外 `closeStream()`+`setState(Idle)`；内层每轮 `readLoop()` 后也立即 `closeStream()` |
| 1.7 terminate 泄漏 | ⚠️ 确认存在 | `unload()` `:116-119`，属清单已登记取舍，待 §7.6 裁决 |
| 1.8 重载可用 | ✅ 符合 | `ensureThread()` 先 `m_quit.store(false)` 再重建线程 |

## 2. 资源与有界性

| 项 | 结论 | 证据 |
|---|---|---|
| 2.1 openStream 失败路径 | ✅ 符合 | 7 个失败分支逐一核对：alloc 失败无泄漏；`open_input` 失败 `if (fmt)` 判空后 close；其余均走 `closeStream()` |
| 2.2 重连不累积 | ✅ 符合 | `readLoop()` 返回立即 `closeStream()`，之后才 sleep/重连 |
| 2.3 sws 复用释放 | ✅ 符合 | `sws_getCachedContext` + `closeStream()` 内 `sws_freeContext` |
| 2.4 QProcess 无双重释放 | ✅ 符合 | `onFinished`（`live_recorder.cpp:176`）与 `onErrorOccurred`（`:211`）均有 `if (!m_proc) return;` 守卫，置空 + `deleteLater`；`start()` 失败路径（`:134-141`）同样处理 |
| 2.5 stderr 有界 | ✅ 符合 | `kErrTailMax=4000`（`:22`），`right(4000)` 截尾（`:171`） |

## 3. GUI 阻塞

| 项 | 结论 | 证据 |
|---|---|---|
| 3.1 stop 不阻塞 | ✅ 符合 | 仅 `write("q\n")`+`closeWriteChannel`，8s 单次定时器兜底强杀（`:157`） |
| 3.2 waitForStarted(5000) | ⚠️ 确认存在 | `live_recorder.cpp:134`，待 §7.5 裁决 |
| 3.3 析构阻塞 | ⚠️ 确认存在 | `:36-38`（3000+1000ms），待 §7.5 裁决 |
| 3.4 连接无阻塞 | ✅ 符合 | `onConnectClicked()` 只 `load()`/`play()`，连接在工作线程 |

## 4. 正确性与门控

| 项 | 结论 | 证据 |
|---|---|---|
| 4.1.1 失败可见 | ✅ 符合 | 首次弹窗（m_errorShown）+ 状态栏；重连只刷状态栏不刷屏 |
| 4.1.2 录制失败带原因 | ✅ 符合 | `finished(path,false,stderr摘要)` → 弹窗 |
| 4.1.3 av_dict_set 未检查 | ⚠️ 确认（低风险） | `openStream()` 8 处均未检查返回值，OOM 才触发，与 T3 登记一致 |
| 4.2.1 辅助线复位 | ✅ 符合 | `setSessionUi(false)`（`livemonitorwindow.cpp:385-387`）`setChecked(false)` 触发 `toggled` → `setGuideLineMode(false)`；全路径无 QSignalBlocker |
| 4.2.2 录制中关窗确认 | ✅ 符合 | `closeEvent()` Yes/No，No 时 ignore |
| 4.2.3 断开先停录制 | ✅ 符合 | `onDisconnectClicked()` 先 `m_recorder->stop()`（`:322`） |
| 4.2.4 录制按钮防重入 | ✅ 逻辑符合 | 停止即禁用（`:504`），`finished` 恢复（`:547`），8s 强杀保证 `finished` 必达；M7 仍待实测 |

§0 修改项核对：mainwindow.h slot+QPointer ✅、mainwindow_ui.cpp 菜单 ✅、CMakeLists 源/头/测试目标 ✅、MANUAL 第八章（L677）✅、README 功能表（L25）✅。
§6 设计取舍与代码一致（无音频/软解/第二路流录制/密码不落盘/IDR 等待/`-loglevel warning` 均已核实）。
§8 欠债 T1–T4 描述与代码一致。

## 5. 清单之外的新发现

| # | 级别 | 问题 | 位置 | 建议 |
|---|---|---|---|---|
| **N1** | 中 | **`unload()` 的 UI 阻塞清单未覆盖**：`onDisconnectClicked()`（`:323`）与 `closeEvent()`（`:592`）在 UI 线程同步调 `m_engine->unload()`，内含 `wait(5000)`——中断回调正常时秒回，但 worker 卡死（正是 1.7 场景）时 UI 阻塞最长约 6s | `live_stream_engine.cpp:116` | 与 §7.5 一并裁决；或断开改异步（先 disable UI，`destroyed`/状态信号回来后再收尾） |
| **N2** | 低 | **连接失败后 loading 动画不停**：`onConnectClicked()` 用 SingleShot 连接首帧关 loading，但 `onStreamError()` 没有 `m_video->setLoading(false)`——连接失败且用户不点"断开"时旋转动画一直转，误导为仍在连接 | `livemonitorwindow.cpp:340` 附近 | `onStreamError()` 首行补 `m_video->setLoading(false);`（一行修复） |
| **N3** | 低 | `onStreamError()` 时 `m_active` 仍为 true，`showStatus` 按 m_active 上色 → "连接失败"显示**绿色** | `livemonitorwindow.cpp:395` | showStatus 增加状态参数，或失败路径显式红色 |
| **N4** | 低（潜在，当前不可达） | 引擎 API 层竞态：会话活跃时直接 `load()+play()`，worker 内层循环可能以**旧 URL** 重连（url 是外层循环顶部快照，`play()` 先复位 abort/started 则不 break）。当前 UI 门控（连接按钮禁用）使其不可达 | `live_stream_engine.cpp:230-260` | 引擎注释写明契约，或 `load()` 后强制 worker 重读 m_url |
| **N5** | 提示 | 首次连接失败也会走 `reconnecting(1)`，状态栏显示"连接中断，正在重连"——对从未连上的场景措辞不准 | `live_stream_engine.cpp:245` | 区分"连接中（第 N 次尝试）"与"断线重连"两套文案 |
| **N6** | 提示 | `pause()` 无条件 `setState(Paused)`，空闲时调用状态机会跳到 Paused；当前 UI 不会触发 | `live_stream_engine.cpp:87` | 可加 `if (state==Playing)` 守卫 |

## 6. 评审结论

| 项 | 结论 |
|---|---|
| 是否可交付 | ☑ **修复后交付**（仅 N2 一行建议先修；N1/N3 不阻塞，可与 §7.5 一起裁决） |
| 必须先修 | N2（loading 动画不停，一行修复） |
| 待项目组裁决 | 清单 §7.1–7.6 维持开放；另加 N1（unload 阻塞） |
| 手工测试 | §5 的 M1–M9（mediamtx 本地流）与 R1–R8（真机）仍须执行，本次评审未覆盖 |
| 评审人 / 日期 | AI 评审（pi）/ 2026-09-12 |

**总体评价**：线程模型、背压有界性、资源释放路径质量高，清单中 §1/§2/§4 全部逐项核实通过，无与清单声明不符之处；两个测试目标的声明数据复现一致。发现的问题集中在 UI 层边角（loading 状态、状态颜色、文案），没有内存安全或数据正确性缺陷。

---

## 7. 整改记录（2026-09-12）

### 7.1 已整改项

| 项 | 级别 | 整改内容 | 位置 |
|---|---|---|---|
| **N2** | 中（必须修） | `onStreamError()` 首行新增 `m_video->setLoading(false)`，连接失败立即停掉旋转动画 | `livemonitorwindow.cpp:349-352` |
| **N3** | 低 | 新增 `enum class StatusTone { Idle, Ok, Bad }`；`showStatus(text, tone)` 色调由调用方显式指定，不再用 `m_active` 推导（失败=红、成功=绿、中性=灰） | `livemonitorwindow.h:64-71`、`livemonitorwindow.cpp:409-421` |
| **N5** | 提示 | 新增 `m_everConnected`；`onReconnecting()` 按是否成功出过图区分文案与色调：首次="正在连接（第 N 次尝试）…"（灰），断线="连接中断，正在重连（第 N 次）…"（红） | `livemonitorwindow.cpp:378-390` |
| **N6** | 提示 | `pause()` 仅在 `state==Playing` 时置 `Paused`，空闲/加载中调用不再污染状态机 | `live_stream_engine.cpp:93-101` |
| **N4** | 低（潜在） | 引入地址**代际号** `m_generation`：`load()` 自增；worker 只在本代际内重连，代际变化立即交回外层重读 `m_url`。彻底消除“会话活跃时换源 → 用旧 URL 重连”的错乱，不再依赖 UI 门控 | `live_stream_engine.h:74-77`、`live_stream_engine.cpp:70-74 / 227-282` |
| **N1** | 中 | 断开与关窗改走**非阻塞** `stop()`（仅置标志+唤醒，不再同步 join）；`stop()` 已置 `m_started=false`+`m_abort=true`，`readLoop` 立即退出并释放 RTSP 连接。线程最终回收统一在 `~LiveMonitorWindow → unload()`，UI 不再因 worker 卡死而阻塞数秒 | `livemonitorwindow.cpp:325-334`、`603-609` |

同时按 N4 要求在 `LiveStreamEngine` 类注释中写明了调用契约（换址必须 `load()`；`stop()` 非阻塞、`unload()` 阻塞用于析构）。

### 7.2 未整改项（维持开放，待项目组裁决）

| 项 | 原因 |
|---|---|
| §1.7 `terminate()` 兜底泄漏 | 属清单 §7.6 裁决项；正常路径下中断回调会立即解除阻塞，5s 超时属异常场景。未擅自改动 |
| §3.2 `waitForStarted(5000)` | 属清单 §7.5 裁决项；需决定是否改为异步 `started` 信号驱动 |
| §3.3 析构 `waitForFinished(3000+1000)` | 同上；退出路径的收尾等待是否可接受 |
| §4.1.3 `av_dict_set` 返回值 | 低风险（OOM 才触发），与欠债 T3 合并跟踪 |
| §8 欠债 T1 / T2 | SSOT 与接线重复；按 R9 待“第三份”出现时抽取 |

### 7.3 整改后验证证据

| 证据 | 结果 |
|---|---|
| `LumenArc.exe` 重新构建 | ✅ 通过（18:18:48），改动文件无新增 warning |
| `lumenarc_live_source_test` | ✅ 23 checks / 0 failures |
| `lumenarc_mw_test` 回归 | ✅ 117 checks / 0 failures |

> 关于 checks 计数：本次复现为 **117**，评审记录为 129。差异来自条件开关用例——`lumenarc_mw_test` 中有依赖环境变量的套件（本 shell 运行时输出 `[luma-chain] SKIP (LUMENARC_REPRO_VIDEO not set)`）。两者 **failures 均为 0**，结论一致。

### 7.4 仍待执行

- 清单 §5.1 的 **M1–M9**（mediamtx 本地流）与 §5.2 的 **R1–R8**（真机 DH-NVR2208-S1）**尚未执行**；本次整改只做了静态核对与构建/单测验证，未覆盖运行时行为。
- 其中 **M7**（停止录制后立即再录）、**M6**（拔网线重连文案）、**M9**（错误态颜色与 loading 停止）正好对应本次整改的 N1/N2/N3/N5，建议现场优先跑这三例。

### 7.5 整改后结论

| 项 | 结论 |
|---|---|
| 必须先修项 | **N2 已修复** |
| 建议修项 | N1/N3/N4/N5/N6 **已全部修复** |
| 是否可交付 | ☑ 静态验证通过；**待 M1–M9 / R1–R8 运行时验证后放行** |
| 整改人 / 日期 | AI 整改（pi）/ 2026-09-12 |
