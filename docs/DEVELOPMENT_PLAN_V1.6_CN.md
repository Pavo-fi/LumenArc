# v1.6.0 GPU 显示管线 Stage 1 施工方案（PENDING P-29）

> **状态：已拍板（2026-08-17 用户确认：Q1 方案 A 内嵌 GL 子控件 / Q2 GL_LINEAR 保留 NEAREST 开关 / Q3 默认 auto 三态 / Q4 不顺带处理大 GOP）。待施工。**
> 基线：HEAD `537bde1`（2026-08-17，§46 收口后）。
> 定位（V1_ERA_TECH_PLAN §7 / Q-16）：**只做 Stage 1**——VideoWidget 视频帧绘制
> 改 GPU 纹理缩放（QOpenGLWidget），消除 1440p+ 快拖时 QPainter CPU 缩放
> ~10ms/帧导致的 UI 背压（uiDrops）；**Stage 2 零拷贝不立项**（Q-16：Stage 1
> 达标即免）。预估 1.5-2 周。
> 本方案沿用 v1.3.0/v1.7.0 施工文档体例：现状盘点 → 拍板点 → 设计 → 任务卡
> → 测试与验收 → 风险 → 工作量。

---

## 1. 背景与动机（为什么现在做）

### 1.1 实测驱动（唯一立项依据）

| 来源 | 事实 |
|---|---|
| HANDOVER 12.6 遗留③ | 1440p+ 快拖时 QPainter CPU 缩放 ~10ms/帧 → UI 背压（uiDrops 高）→ 画面冻结感 |
| §46 补记（明景 1440p） | 2560×1440/20fps/30min/783MB 拖拽偶见卡顿；单文件 GOP=12.5s（DVR 拼接源典型）；1440p 每帧 CPU 缩放 ~10ms（**V1_ERA §7.1 记录的 P-29 GPU Stage 1 驱动场景**）→ 主因绘制背压、次因大 GOP |
| §46 补记收口 | 明景文件经转码导出（2s 关键帧）后用户实测"顺了很多"（P-53 ✅）——**但根治项仍是 P-29**：未转码的 1440p+ 原始素材快拖时 CPU 缩放开销不变 |

### 1.2 反面教训（为什么只做 Stage 1）

FUTURE_OPTIMIZATIONS §1 记录零拷贝管线（Stage 2）曾**完整实现后又移除**：
目标场景 CPU 光栅已够、复杂度高（跨设备同步/keyed-mutex）、截图叠加与放大镜
强制回退 CPU 帧。Q-16 拍板：**Stage 1 达标（1440p 快拖 uiDrops 归零）则
Stage 2 不立项**。本方案严格只做 Stage 1：引擎照旧产出 QImage，仅替换
"上屏缩放"这一段。

---

## 2. 现状盘点（代码事实）

### 2.1 当前显示链（逐环节，含文件落点）

```
[工作线程] FfmpegVideoEngine::displayFrame()          ffmpeg_video_engine.cpp:1892
  ├─ 有界化背压：m_framesInFlight ≥ 2 → 丢帧 + uiDrops++ + 节拍闸 +5ms（:1880）
  ├─ 硬解帧 av_hwframe_transfer_data 回传 CPU（NV12）
  ├─ sws_scale → RGB24 QImage（scrub 模式降采样至 ≤1280 宽；正常播放全尺寸）
  └─ emit frameReady(img)  [QueuedConnection，配额 m_framesInFlight ≤ 2]
        │
[UI 线程] VideoWidget::onFrameReady(image)             videowidget.cpp:1618
  ├─ m_rawFrameImage = image（COW 浅拷贝）
  ├─ 旋转/LUT 非恒等时 rebuildAdjustedFrame()：
  │    原始帧 → QImage::transformed(旋转) → applyDisplayLut（CPU 全帧处理）
  └─ update() + overlay->update()
        │
[UI 线程] VideoWidget::paintEvent()                    videowidget.cpp:1759
  ├─ videoDisplayRect()：letterbox 等比目标矩形（整数）
  ├─ painter.drawImage(target, m_frameImage)   ← ★ CPU 光栅缩放 ~10ms/帧@1440p
  └─ 截图融合叠加（m_adjustedSnapshot，按 opacity 混合，低频路径）
```

**结论**：热点只有一处——`paintEvent` 里 `drawImage` 的全帧缩放（2560×1440
RGB888 ≈ 11MB/帧 → 光栅引擎逐像素重采样）。上游 `onFrameReady` 是 O(1) 拷贝，
scrub 降采样已经把上传量控制在 ≤1280 宽；正常播放 1440p 全尺寸 + 20fps 也在
背压阈值内偶发丢帧。

### 2.2 CPU 帧消费方（Stage 1 必须零影响，全部走数据通道不走 grab）

| 消费方 | 代码落点 | 数据来源 | GL 化影响 |
|---|---|---|---|
| 放大镜 | mainwindow.cpp:3084 `rawFrame()` | QImage 成员 | 无 |
| 钉图 PinnedWidget | `frameSnapshotReady` 信号（grabFrameSnapshot） | QImage 成员 | 无 |
| 证据快照 | mainwindow.cpp:3808 `currentFrame()` | QImage 成员 | 无 |
| 截图融合 | `setSnapshot()` → paintEvent 绘制 | QImage 成员 | 需随视频帧同层绘制（见 §4.4） |
| ROI/辅助线/放大镜标识框/时间戳框选 | OverlayWidget（VideoWidget 顶层子控件） | QPainter 矢量 | 无（仍为普通 raster 控件） |

**关键核查**：全仓无任何 `QWidget::grab()` 作用于 VideoWidget 本体（快照全部走
上述数据通道）；QOpenGLWidget 的 grab 陷阱（WORK_HISTORY §2544/§2581，语谱图
面板踩过：grab 拿到旧 FBO/尺寸错位）**不会命中视频区**。

### 2.3 既有 GL 先例（可直接复用的模式）

`SpectrogramPanelEnhanced`（spectrogrampanel_enhanced.{h,cpp}）：
QOpenGLWidget + `QOpenGLFunctions_3_3_Core` + 自带 shader + `initializeOpenGLFunctions()`
失败 → `m_glError` 记录 + **CPU 离屏光栅化降级渲染**（:419，报告抓图即走此路）。
该模式已在发布包运行数版、CI（offscreen）下不崩——Stage 1 的 GL 生命周期/
降级策略照抄此经验。

### 2.4 诊断基线（验收对照用）

- 引擎 `scrub 2s:` 周期日志（ffmpeg_video_engine.cpp:656）：
  `displays / cacheHits / reseeks / uiDrops / maxGap / inFlight`
- `uiDrops` = UI 侧 inFlight≥2 被丢帧数（CPU 绘制背压的直接读数）
- `maxGap` = 相邻显示最大墙钟间隔（卡顿体感）
- 复现工具：`engine_test scrub-sweep`（长距离连续拖拽仿真）+ 1440p 真机素材
- 当前已知调参：显示节拍 25ms（自适应 +0~20ms）、超前量 800ms、片段音频闸 4×

---

## 3. 拍板点（Q 项，待用户确认）

### Q1 挂载形态：内嵌 GL 子控件（推荐）vs VideoWidget 整体改 QOpenGLWidget ✅ 建议 A

| 方案 | 说明 | 取舍 |
|---|---|---|
| **A（推荐）** | VideoWidget 保持 QWidget，新增子控件 `GlVideoSurface : QOpenGLWidget`（置底），承载视频帧 + 截图融合 + 黑边绘制 | 回退 = 隐藏 GL 子控件、VideoWidget 自绘老路径照旧（真正"一行开关"）；空态/加载卡片仍由 VideoWidget 绘制，与 GL 无交集；offscreen CI 不构造 GL 上下文即可全绿 |
| B | VideoWidget 直接继承 QOpenGLWidget | 改动集中，但 offscreen 无 GL 时连空态/加载态都不可绘，必须双路径 paintEvent，回退复杂；风险高 |

### Q2 渲染语义：GL 只做缩放上屏，旋转/LUT 保持 CPU（Stage 1 范围锁定）✅ 建议"只缩放"

- Stage 1 消费 `m_frameImage`（已由既有 `rebuildAdjustedFrame` 链完成旋转+LUT，
  **与 CPU 路径逐位一致**——证据链"所见即所得"语义零变化）。
- shader 内旋转（UV 变换）/LUT（256 级查表纹理）列为**后续性能增强**，不在
  本版：旋转素材（竖屏手机视频）当前本就支付 `transformed()` 成本，非本次
  背压主因；语义漂移风险不值当。
- 采样质量：GL 线性过滤 ≈ 降采样视觉质量。CPU 路径现状**未开**
  SmoothPixmapTransform（近似最近邻），GL 线性**更好**；验收时目检确认无
  观感回退（若用户偏好现状锐利感，可切 GL_NEAREST——拍板项）。

### Q3 开关与默认值 ✅ 建议默认 auto

- QSettings `video/gpuDisplay`：`auto`（默认，GL 初始化失败永久回退 CPU +
  日志）/ `on`（失败弹提示）/ `off`（强制 CPU）。
- CI 与 offscreen 测试恒走 CPU（auto 在无 GL 环境自动回退，无需特判）。

### Q4 明景类大 GOP 素材是否顺带处理 ✅ 不顺带（维持 §46 结论）

- §46 已用转码导出（2s 关键帧）缓解明景 1440p；P-29 落地后快拖缩放开销消失，
  大 GOP 的 seek 等待由既有跳显（hop）机制兜底——不在本版扩 scope。

---

## 4. 详细设计（方案 A）

### 4.1 新组件：ui 层 `GlVideoSurface`（新文件 videoglwidget.{h,cpp}）

```cpp
/// GL 视频面：QImage → GL 纹理上传 → 双线性缩放上屏。
/// 只替换 VideoWidget::paintEvent 中 painter.drawImage 一段；
/// 帧语义（旋转/LUT/快照）全部继承自 VideoWidget 现有 CPU 链。
class GlVideoSurface : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core {
    Q_OBJECT
public:
    /// 帧到达（VideoWidget::onFrameReady 转发；只置指针+脏标记，上传延迟到 paint）
    void presentFrame(const QImage &frame);
    /// 截图融合叠加（低频；参数变化时重建上传）
    void presentSnapshotOverlay(const QImage &snapshot, qreal opacity);
    void clearSurface();
    /// 上屏目标矩形（letterbox），由 VideoWidget 计算后下发（复用 videoDisplayRect 同一整数式）
    void setDisplayRect(const QRect &r);
    bool glHealthy() const;          // 降级判定结果（auto 模式供 VideoWidget 决策）
protected:
    void initializeGL() override;    // 建 shader/VAO/纹理对象（失败 → healthy=false 发信号）
    void resizeGL(int w, int h) override;
    void paintGL() override;         // ①清屏黑 ②纹理 blit 到 displayRect ③融合纹理混合
private:
    // 纹理复用：帧尺寸不变时 glTexSubImage2D 原地更新，不销毁重建
    QOpenGLTexture *m_frameTex = nullptr;   // RGB888, GL_RGB/UNSIGNED_BYTE
    QOpenGLTexture *m_snapTex = nullptr;
    QOpenGLShaderProgram *m_blit = nullptr; // 顶点 UV + 片段双线性采样
    QSize m_texSize;                 // 当前纹理分配尺寸（变帧重建）
    QPointer<const QImage> m_pending; // 待上传帧（COW 浅引用）
    bool m_frameDirty = true;
    QRect m_displayRect;
    qreal m_snapOpacity = 0.0;
signals:
    void glFailed(const QString &reason);   // VideoWidget 收到 → 永久回退 CPU
};
```

### 4.2 纹理上传要点（每帧 ~1ms 预算的来源）

| 项 | 做法 |
|---|---|
| 格式 | QImage::Format_RGB888 → `glTexImage2D(GL_TEXTURE_2D, …, GL_RGB, GL_UNSIGNED_BYTE)`（scrub 降采样帧 ≤1280×720 ≈ 2.7MB；全尺寸 1440p ≈ 11MB，PCIe 上传 ~1ms） |
| 行对齐 | QImage 行 4 字节对齐且 bytesPerLine ≥ width×3：`glPixelStorei(GL_UNPACK_ROW_LENGTH, img.bytesPerLine()/3)` 后按 width 传，避免整行拷贝 |
| 复用 | 帧尺寸不变（常见）→ 首帧 `glTexImage2D` 分配，后续 `glTexSubImage2D` 原地更新；尺寸变化才重建 |
| 时机 | `presentFrame` 只记引用置脏；`paintGL` 内惰性上传（隐式 makeCurrent，避免额外上下文切换） |
| 双线性 | `GL_LINEAR` min/mag + `GL_CLAMP_TO_EDGE`；NPOT 纹理 GL 3.3 core 原生支持 |
| 顶点 | 静态全屏 quad 一次 VAO；`displayRect` 换算为 clip space 传 uniform——**缩放全在 GPU 光栅化阶段**，CPU 侧每帧仅 ~11MB 内存拷贝进驱动 |

### 4.3 VideoWidget 改造（最小侵入，行为冻结）

```
onFrameReady(image)                    videowidget.cpp:1618（不变）
  ├─ m_rawFrameImage / rebuildAdjustedFrame  ← 旋转/LUT 语义原样（Q2）
  ├─ if (m_gl && m_gl->glHealthy())   m_gl->presentFrame(m_frameImage);   // 新
  │    （GL 面栈在 overlay 之下、VideoWidget 之上，尺寸同步 rect()）
  └─ update()                          // CPU 路径自绘照旧（回退开关）

paintEvent()                           videowidget.cpp:1759（保留，仅回退态执行）
  ├─ GL 激活时：跳过视频帧绘制（GL 子控件已画），仅当无帧/加载态绘制卡片与品牌空态
  └─ GL 回退时：与现状逐行一致（老代码即回退代码，零新分支语义）
```

- **空态/加载卡片**：GL 面仅在"有帧"时可见（`setVisible(hasFrame)`），加载
  旋转卡片与品牌 logo 仍由 VideoWidget raster 绘制——GL 初始化失败也不影响。
- **resizeEvent**：`m_gl->setGeometry(rect())` + `setDisplayRect(videoDisplayRect())`
  （与 `updateOverlayGeometry` 同点下发，目标矩形与 overlay 命中检测同一整数式）。
- **clearFrame/clearSnapshot**：同步 `clearSurface()`。

### 4.4 截图融合叠加（同层绘制，保住 z 序）

融合图当前画在视频帧**之上**（paintEvent 内 drawImage + opacity）。GL 激活时
视频帧由 GL 子控件绘制，父控件 raster 无法盖在其上（backing store 在子控件
之下）→ 融合图改由 `GlVideoSurface::presentSnapshotOverlay` 以**第二张纹理 +
uniform alpha** 混合绘制（参数变化才重传，播放期间零成本）。CPU 回退态保持
现状绘制。两条路径同一 `m_adjustedSnapshot` 缓存源，观感一致。

### 4.5 OverlayWidget 叠放（不变）

OverlayWidget 是普通 raster 子控件，栈序在 GL 面之上：QOpenGLWidget 非原生
窗口，Qt 合成管线保证"GL 底 + raster 顶"正常混合（语谱图面板与滑块同窗共存
已验证）。ROI 拖拽命中、时间戳框选、放大镜标识框全部不动。

### 4.6 降级与探测（红线：失败即回退，永久可用）

```
auto 模式启动序列：
  构造 VideoWidget（不建 GL）→ 首帧到达 → 惰性构造 GlVideoSurface
  → initializeGL 失败/上下文无效（RDP 无 GPU、老旧驱动、offscreen）：
       发 glFailed → VideoWidget 记录降级标志（本进程不再尝试）+ 状态日志
       「视频显示：CPU 软件渲染（GPU 不可用）」→ 画面照常（raster 路径）
```

- **CI 基线永远 CPU**：QT_QPA_PLATFORM=offscreen 下 auto 自动回退，11 套
  回归不引入 GL 依赖；ui_chain_test 增补"GL 构造失败不崩、回退后快照/框选
  照常"场景（offscreen 即天然失败注入）。
- 设置项入"播放设置"区（QSettings `video/gpuDisplay`，三态下拉）。

### 4.7 不改的东西（明确排除，防 scope 蔓延）

| 项 | 理由 |
|---|---|
| IVideoEngine 接口、frameReady 协议、inFlight 配额 | Stage 1 不碰引擎；零拷贝（Stage 2）Q-16 不立项 |
| 硬解回传（av_hwframe_transfer_data） | 数据仍在 CPU 侧成 QImage；Stage 2 范畴 |
| 放大镜/钉图/证据快照数据通道 | 全部 QImage 成员，与渲染方式解耦（§2.2） |
| 旋转/LUT 的 CPU 链 | Q2 拍板：语义逐位一致优先 |
| scrub 降采样（1280 宽）与节拍闸参数 | 引擎侧既有机制，显示提速后自然少丢帧，不调参 |

---

## 5. 任务卡（提交切分，T1/T3 纪律）

| # | 任务 | 内容 | 预估 |
|---|---|---|---|
| T1 | `videoglwidget.{h,cpp}` 新组件 + 单测可编译体 | GL 面类、纹理复用、shader、glFailed 信号；offscreen 下不构造 | 1.5 人日 |
| T2 | VideoWidget 挂接 + 回退开关 + 设置项 | presentFrame/setDisplayRect/融合纹理/生命周期；QSettings 三态；空态路径不动 | 1.5 人日 |
| T3 | ui_chain_test 增补 + 全回归 | GL 失败注入（offscreen）不崩、回退态快照/框选/旋转断言；11 套全绿 | 1 人日 |
| T4 | 真机性能验收 + 手工点检 | 1440p 快拖 uiDrops=0；放大镜/钉图/融合/框选/旋转/LUT/RDP 逐项（RELEASE_CHECKLIST_V1.6） | 1 人日 |
| T5 | 文档同步 | MANUAL（设置项+故障现象说明）、README、DOCS_MAP/PENDING 勾销、HANDOVER 批次 | 0.5 人日 |

合计 ~5.5 人日（1-1.5 周日历，含缓冲）。

---

## 6. 测试与验收

### 6.1 性能验收线（V1_ERA §7 原文 + 量化）

| 项 | 验收线 | 方法 |
|---|---|---|
| **uiDrops 归零** | 1440p 素材 30× 快拖连续 ≥30s，`scrub 2s:` 日志 uiDrops=0、maxGap ≤40ms | 明景重导出素材 + `scrub-sweep` + 人工快拖；对照 CPU 路径基线（当前 uiDrops 频发） |
| 播放 CPU 占用 | 1440p 20fps 正常播放，主线程 paint 耗时 ~10ms → <2ms；任务管理器进程 CPU 下降 | 真机 + 临时埋点（验收后移除） |
| 4K 冗余 | 4K 素材播放/拖拽不 worse-than-CPU | 手工（Stage 2 之外不设硬指标） |

### 6.2 功能回归（手工点检清单新增，入 RELEASE_CHECKLIST_V1.6_CN.md）

放大镜（含标识框+倍率徽章）/ 钉图 / 截图融合（opacity 拖动实时）/ 证据快照
（与屏幕一致）/ ROI 矩形+多边形拖拽命中 / 辅助线 / 时间戳框选（校时 UX）/
显示旋转四档 / 画面调节 LUT（暂停态拖滑杆预览）/ 加载卡片 / 品牌空态 /
窗口拉伸/分屏切换 / 多显示器与 DPI 变化 / RDP 会话（应自动回退 + 状态提示）/
CPU 强制开关往返。

### 6.3 自动化

- ui_chain_test：GL 不可用环境（offscreen）VideoWidget 全链路不崩 + 回退态
  快照数据通道断言（rawFrame/currentFrame 非空且与输入逐位一致）。
- 全回归 11 套常绿（CI 无 GL，天然全 CPU 基线——红线达成即被测）。

---

## 7. 风险登记

| # | 风险 | 等级 | 应对 |
|---|---|---|---|
| 1 | GL 上下文与 QOpenGLWidget 生命周期踩坑（窗口重组/DPI 切换崩溃） | 中 | 复用语谱图面板成熟模式；纹理/shader 全部 parent 化；RDP/DPI 入点检 |
| 2 | 融合叠加两条渲染路径观感不一致 | 低 | 同一 QImage 缓存源 + 相同 opacity 语义；点检并列目检 |
| 3 | 双线性采样观感与现状（最近邻）差异被用户视为"变糊/变锐" | 低 | Q2 拍板项；GL_NEAREST 一键切换兜底 |
| 4 | "做了又删"重演（Stage 2 诱惑） | 中 | Q-16 已拍板不立项；本方案 §4.7 显式排除；验收只看 uiDrops |
| 5 | 集成 GL 后 offscreen CI 意外构造上下文失败变红 | 低 | GL 惰性构造（首帧+auto 探测），测试环境永不进入 GL 分支 |
| 6 | 每帧 11MB 上传在老 PCIe/核显共享内存机型反而慢 | 低 | 上传与缩放分离计时验收；异常机型 auto 回退（glFailed 扩展慢速阈值） |

---

## 8. 拍板记录（2026-08-17 用户逐条确认，全部按建议）

1. ✅ Q1 挂载形态 = **A：内嵌 GlVideoSurface 子控件**（回退一行开关；空态/加载卡片仍 raster）。
2. ✅ Q2 渲染语义 = **只做缩放上屏**，旋转/LUT 保持 CPU 链；采样 **GL_LINEAR**，保留 GL_NEAREST 切换兑底。
3. ✅ Q3 开关 = QSettings `video/gpuDisplay`，**默认 auto**（失败永久回退 + 日志；on/off 可选）。
4. ✅ Q4 明景类大 GOP 素材不顺带处理（维持 §46 转码导出结论，跳显机制兑底）。
