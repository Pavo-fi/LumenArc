# 监控直播（RTSP 接入）— 代码评审清单

> 版本：v1.0　日期：2026-09-11
> 对应功能：`文件 → 接入监控直播`（大华 NVR 实时画面接入 + 辅助线/截图叠加 + 无损录制）
> 目标设备：大华 DH-NVR2208-S1（8 路 NVR，默认 192.168.1.108）
> 用途：由项目组人工逐项核对。**每项都写了"看哪一行、验什么不变量、期望结论"，不要只打勾不看。**

---

## 0. 评审范围

| 类型 | 文件 | 行数 |
|---|---|---|
| 新增 · domain | `src/domain/live/live_source.h` / `.cpp` | 127 |
| 新增 · infrastructure | `src/infrastructure/live/live_stream_engine.h` / `.cpp` | 460 |
| 新增 · infrastructure | `src/infrastructure/live/live_recorder.h` / `.cpp` | 227 |
| 新增 · ui | `src/livemonitorwindow.h` / `.cpp` | 603 |
| 新增 · 入口 | `src/mainwindow_live.cpp` | 29 |
| 新增 · 测试 | `tests/live_source_test_main.cpp` | — |
| 修改 | `CMakeLists.txt`（SOURCES/HEADERS/新测试目标） | — |
| 修改 | `src/mainwindow.h`（1 个 slot + 1 个 QPointer） | — |
| 修改 | `src/mainwindow_ui.cpp`（createMenus 内 1 条菜单） | — |
| 修改 | `MANUAL.md`（新增第八章）、`README.md`（功能表 1 行） | — |

**未改动**：`FfmpegVideoEngine`（文件播放引擎）一行未动 —— 这是刻意的，避免回归风险。

### 已完成的自动化证据（可直接复现）

```bash
cmake -S . -B build
cmake --build build --config Release --target lumenarc_live_source_test && ./build/Release/lumenarc_live_source_test.exe
cmake --build build --config Release --target LumenArc
cmake --build build --config Release --target lumenarc_mw_test
QT_QPA_PLATFORM=offscreen ./build/Release/lumenarc_mw_test.exe
```

| 证据 | 结果 |
|---|---|
| `LumenArc.exe` 编译链接 | 通过 |
| `lumenarc_live_source_test` | 23 checks / 0 failures |
| `lumenarc_mw_test` 回归 | 117 checks / 0 failures |
| 真实录像机联调 | **未做**（现场由用户执行，见 §5） |

---

## 1. 线程安全与生命周期（最高优先级）

> 直播引擎是"UI 线程 + 1 个工作线程"的双线程模型，这是本次改动风险最集中的地方。

- [ ] **1.1 `m_url` 的所有访问都在 `m_mutex` 保护下**
  - 位置：`live_stream_engine.cpp` `load()`（写）、`workerMain()` 开头（读）、`unload()`（清空）
  - 核对：三处是否都持锁；有无遗漏的裸读
  - 期望：全部持锁，无裸访问

- [ ] **1.2 中断回调是"无锁、无分配、无信号发射"的**
  - 位置：`interruptCb()`（`live_stream_engine.cpp` 约 195 行）
  - 核对：回调体内是否只有原子读；**绝不能**有 `QMutexLocker` / `emit` / 内存分配
  - 原因：FFmpeg 会在任意线程调用它，加锁即可能死锁
  - 期望：仅 `m_abort || m_quit` 的原子读

- [ ] **1.3 `m_abort` 的双重语义是否可接受**
  - 语义：既表示"中断阻塞 IO"，又表示"结束当前会话"；`play()` 会把它复位为 `false`
  - 核对：`stop()` 后立刻 `play()` 的竞态，最终状态是否仍是"播放中"
  - 期望：可接受（终态正确）；若判定不可接受，需拆成两个标志

- [ ] **1.4 `m_framesInFlight` 不会下溢**
  - 位置：`ackFrame()` 用 CAS 钳位到 0；`closeStream()` 会把计数清零
  - 风险：清零后"在飞帧"到达，`ackFrame()` 被多调 → 计数变负 → 背压上限永久失效（帧不再丢，内存/延迟无界）
  - 核对：CAS 循环是否为 `while (v > 0)`、`compare_exchange_weak` 是否用了正确的内存序
  - 期望：不会为负

- [ ] **1.5 丢帧路径不计数、不发射**
  - 位置：`displayFrame()` 中 `m_framesInFlight >= kMaxFramesInFlight(2)` 时直接 `return false`
  - 核对：该分支是否在 `fetch_add` 与 `emit` **之前**
  - 期望：丢帧时不递增、不 emit

- [ ] **1.6 线程退出路径全部释放资源**
  - 位置：`workerMain()` 尾部（循环外）有 `closeStream()` + `setState(Idle)`
  - 核对：`m_quit` 触发的所有出口是否都经过这句
  - 期望：是

- [ ] **1.7 ⚠️ `terminate()` 兜底会造成泄漏（已知取舍）**
  - 位置：`unload()` 中 `wait(5000)` 超时后 `m_thread->terminate()`
  - 风险：强杀时 worker 不会执行 `closeStream()`，`AVFormatContext`/`AVCodecContext`/`SwsContext` 泄漏
  - 核对：是否接受；正常路径下中断回调应当能立即解除阻塞，5s 超时属异常
  - 期望：**接受并记录**；若要求零泄漏，需改为"超时后仍等待 + 上报告警"

- [ ] **1.8 `unload()` 之后仍能重新 `play()`**
  - 位置：`ensureThread()` 会先 `m_quit.store(false)` 再重建线程
  - 核对：`unload()` → `load()` → `play()` 能否正常重新出图
  - 期望：可以

---

## 2. 资源与有界性（C5）

- [ ] **2.1 `openStream()` 每条失败路径都释放**
  - 位置：`live_stream_engine.cpp` `openStream()`，逐个 `return false` 分支
  - 核对：`avformat_open_input` 失败时 `fmt` 是否已被 FFmpeg 置空（是否只在非空时 `avformat_close_input`）；`m_vdec` 分配失败后的分支
  - 期望：无泄漏、无二次释放

- [ ] **2.2 重连循环不累积泄漏**
  - 核对：`readLoop()` 返回后是否**立即** `closeStream()`，之后才考虑重连
  - 期望：每轮重连都释放上一轮的上下文

- [ ] **2.3 sws 上下文复用与释放**
  - 位置：`sws_getCachedContext()`（同参数复用）+ `closeStream()` 内 `sws_freeContext()`
  - 期望：不每次分配、退出时释放

- [ ] **2.4 `LiveRecorder` 的 QProcess 无泄漏/无双重释放**
  - 位置：`onFinished()` / `onErrorOccurred()` 都 `deleteLater()` 并置 `m_proc = nullptr`
  - 核对：`onFinished` 开头是否有 `if (!m_proc) return;` 守卫（防两条路径都收尾）
  - 期望：无双重释放

- [ ] **2.5 ffmpeg stderr 累积有上界**
  - 位置：`appendStderr()` 只保留最后 4000 字符
  - 原因：录制几小时会持续产生 stderr，无上界即内存无界
  - 期望：有界

---

## 3. GUI 阻塞（C4：不得阻塞 >100ms）

- [ ] **3.1 `stop()` 不阻塞 UI**
  - 位置：`live_recorder.cpp` `stop()` 只做 `write("q\n")` + `closeWriteChannel()`，另有 8s 单次定时器兜底强杀
  - 期望：调用即返回

- [ ] **3.2 ⚠️ `start()` 里有 `waitForStarted(5000)`**
  - 位置：`live_recorder.cpp` `start()`
  - 风险：最坏阻塞 UI 5 秒（违反 C4 的 100ms 条款）
  - 缓解：属用户主动点击"开始录制"的显式动作
  - 期望：**明确裁决**——接受，或改为异步 `started` 信号驱动

- [ ] **3.3 ⚠️ 析构里有 `waitForFinished(3000)` + `waitForFinished(1000)`**
  - 位置：`LiveRecorder::~LiveRecorder()`
  - 风险：退出程序/关窗时最坏阻塞 4 秒
  - 期望：**明确裁决**

- [ ] **3.4 连接流程无阻塞调用**
  - 位置：`onConnectClicked()` 只调 `load()`/`play()`，真正的连接发生在工作线程
  - 期望：UI 立即返回，连接结果经信号回来

---

## 4. 正确性、失败可见性与门控

### 4.1 静默失败（C2）

- [ ] **4.1.1 连接/解码失败均用户可见**
  - 路径：引擎 `streamError` → 窗口弹窗（仅首次）+ 状态栏；重连只更新状态栏（防刷屏）
  - 期望：无"什么都不显示"的失败

- [ ] **4.1.2 录制失败可见且给出原因**
  - 路径：`finished(path, ok=false, message)` → 弹窗，message 取 ffmpeg stderr 摘要
  - 期望：用户能看到失败原因，不是静默无文件

- [ ] **4.1.3 `av_dict_set` 返回值未检查（低）**
  - 位置：`openStream()` 中连续多个 `av_dict_set`
  - 影响：仅 OOM 时失败
  - 期望：记录为已知低风险

### 4.2 UI 状态门控

- [ ] **4.2.1 断开时辅助线状态被复位**
  - 位置：`setSessionUi(false)` 中 `m_guideBtn->setChecked(false)`
  - **重点核对**：这依赖 `setChecked(false)` 触发 `toggled` 信号进而调 `onGuideLineToggled(false)` 关闭 overlay 的辅助线模式。Qt 的 `setChecked` **确实**会发 `toggled`，但请确认本代码路径没有被 `QSignalBlocker` 之类拦截
  - 期望：断开后 overlay 退出辅助线模式（画面上不再能画线）

- [ ] **4.2.2 录制中关闭窗口有确认**
  - 位置：`closeEvent()` 弹 Yes/No，No 时 `event->ignore()`
  - 期望：不会静默丢录像

- [ ] **4.2.3 断开连接时先停录制**
  - 位置：`onDisconnectClicked()` 先 `m_recorder->stop()`
  - 期望：不会留下未收尾的文件

- [ ] **4.2.4 录制进行中按钮不被重复点击**
  - 位置：点停止后立刻 `setEnabled(false)`，待 `finished` 恢复
  - 风险：若 ffmpeg 既不退出也不被杀，按钮永久禁用（8s 强杀兜底后应能恢复）
  - 期望：**实测确认**（见 §5 用例 M7）

---

## 5. 手工测试清单（按可执行性排序）

### 5.1 无真机也能做（本地起一个 RTSP 服务）

推荐用 mediamtx（或 VLC）起本地流，验证端到端链路：

```bash
# 方案一：mediamtx + ffmpeg 推一路测试流（需要本机有 mediamtx）
# 终端1：mediamtx
# 终端2：
ffmpeg -re -f lavfi -i testsrc=size=1280x720:rate=25 -c:v libx264 -preset ultrafast \
       -f rtsp rtsp://127.0.0.1:8554/test
# 然后在 LumenArc「完整地址」里填：rtsp://127.0.0.1:8554/test（无需用户名密码）
```

| # | 用例 | 期望 | 结果 |
|---|---|---|---|
| M1 | 连接本地测试流 | 状态栏「直播中」，显示 1280×720 / 25 fps，画面实时 | ☐ |
| M2 | 绘制辅助线 | 可在直播画面上拖拽画出水平/垂直线；「清除辅助线」清空 | ☐ |
| M3 | 「当前帧为参考」 | 右上角出现参考图浮窗，画面半透明叠加；调透明度实时生效 | ☐ |
| M4 | 录制 10 秒后停止 | 弹出「录制完成」；用 VLC/LumenArc 打开该文件可正常播放、时长达 ~10s | ☐ |
| M5 | 录制中关闭窗口 | 弹确认框；选「否」窗口不关；选「是」正常收尾 | ☐ |
| M6 | 拔网线/杀推流 5 秒后恢复 | 状态栏出现「连接中断，正在重连（第 N 次）」；恢复推流后自动出图 | ☐ |
| M7 | 停止录制后立即再点「开始录制」 | 按钮可用，能开始第二段录制 | ☐ |
| M8 | 断开后重连 | 「断开」→「连接」可再次出图；辅助线按钮已复位为未选中 | ☐ |
| M9 | 错误地址 | 弹一次错误框 + 排查清单；重连不重复弹框（只刷状态栏） | ☐ |

### 5.2 现场（真机 DH-NVR2208-S1）

| # | 步骤 | 期望 | 结果 |
|---|---|---|---|
| R1 | 网线直连；本机网卡设 `192.168.1.100/255.255.255.0`；`ping 192.168.1.108` | 能 ping 通 | ☐ |
| R2 | 填地址/端口554/用户/密码/通道1/主码流/TCP → 连接 | 出图，状态栏显示真实分辨率与帧率 | ☐ |
| R3 | 切换到通道 2–8 | 能出对应通道画面 | ☐ |
| R4 | 主码流 ↔ 子码流切换 | 两者都能出图；子码流分辨率更低 | ☐ |
| R5 | 长稳观察 ≥30 分钟 | 无内存持续增长、无画面卡死、无句柄泄漏（任务管理器观察） | ☐ |
| R6 | 录制 ≥5 分钟并停止 | 文件可播放、时长正确、音画正常（若该通道有声） | ☐ |
| R7 | 录制 5 分钟文件大小核对 | 与"码流码率 × 时间"量级相符（说明是流拷贝而非转码） | ☐ |
| R8 | 同时用浏览器/客户端打开该 NVR | 确认第 2 路并发连接未被拒绝 | ☐ |

---

## 6. 明确的设计取舍（**不是 bug，请勿按缺陷上报**）

1. **直播不支持进度拖拽 / 逐帧步进 / 倍速 / 亮度量化分析** —— 实时流没有时长与可寻址时间轴，强行支持会得出错误结果（违反"结果可信"第一原则）。
2. **直播不含音频** —— 本次设计取舍（`supportsRateAudio=false`）。若现场有录音取证需求，需追加音频路径。
3. **直播引擎走软解，未接 D3D11VA 硬解** —— 单路 1080p 无压力；若将来要多路 4K 需补硬解。
4. **录制会额外拉一路 RTSP**（不占用预览流）—— 好处是录制与预览互不影响、且不重编码；代价是 NVR 需允许 ≥2 路并发。
5. **密码不落盘** —— 取证工具的凭证不做明文持久化；每次启动需重输。
6. **关闭窗口即停止录制** —— 录制生命周期挂在窗口上（有确认框）。
7. **暂停恢复后等下一个 IDR 才出图** —— 避免陈旧参考帧花屏；弱网长 GOP 时可能有短暂等待。
8. **`hide_banner`/`-loglevel warning` 的 ffmpeg 子进程** —— 无进度反馈，只有起止与错误。

---

## 7. 需要项目组裁决的开放问题

- [ ] **7.1 是否需要调取 NVR 内已录的"历史录像"？** 当前只做实时。若要历史回放（按时间点取流），必须走大华 NetSDK 或 HTTP 接口，属**独立立项**，不在本次范围。
- [ ] **7.2 是否需要音频？** 现场是否有同步录音取证的诉求。
- [ ] **7.3 是否需要多路同时接入？** 当前一次一路（多开窗口可多路，但会有多份连接与内存）。
- [ ] **7.4 直播画面是否要纳入「案件/证据快照」体系？** 当前直播窗口独立于案件，不产生 `.vla`、不进案件目录。
- [ ] **7.5 §3.2 / §3.3 的两处阻塞**（`waitForStarted` / 析构 `waitForFinished`）是否必须消除以严格满足 C4 的 100ms 条款。
- [ ] **7.6 §1.7 的 `terminate()` 兜底泄漏**是否可接受。

---

## 8. 已知欠债（本次引入，如实登记）

| # | 欠债 | 位置 | 建议 |
|---|---|---|---|
| T1 | 直播源配置没有独立 SSOT，散落在窗口控件 + QSettings | `livemonitorwindow.cpp` | 若配置项继续增加，抽 `LiveSourceStore`（对齐 R5） |
| T2 | 截图叠加的接线逻辑与 `mainwindow_wiring.cpp::setupSnapshotFusionConnections` 有部分重复 | `livemonitorwindow.cpp` `buildUi()` | 出现第三份时按 R9 抽取共用装配函数 |
| T3 | `av_dict_set` 返回值未检查 | `live_stream_engine.cpp` `openStream()` | 低优先，OOM 才触发 |
| T4 | 直播引擎无硬解路径 | `live_stream_engine.cpp` | 多路 4K 需求出现时补 |

---

## 9. 评审结论

| 项 | 填写 |
|---|---|
| 是否可交付 | ☐ 可交付　☐ 修复后交付　☐ 打回 |
| 必须先修的项编号 | |
| 评审人 / 日期 | |
| 备注 | |
