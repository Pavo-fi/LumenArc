# 追光者 Lumen Arc — 量化分析技术说明

> 版本：v0.3 | 更新：2026-06-17

---

## 概述

Lumen Arc 的量化分析模块提供三个维度的视频数据量化：

1. **亮度分析**（Luminance）— 视频帧像素亮度的时间序列
2. **音量分析**（Volume）— 音频信号强度的时间序列
3. **语谱图分析**（Spectrogram）— 音频频率分布的时间-频率二维热力图

三者共享同一时间轴，可在同一图表中叠加对比分析。

---

## 一、亮度分析

### 1.1 原理

视频亮度分析将每帧图像转换为灰度值，计算指定区域（ROI）内的平均亮度。

**数学表达**：
```
L(t) = mean(Gray(ROI(x, y, w, h), frame_t))
```

其中 `Gray()` 是 BGR→灰度转换：`Y = 0.299R + 0.587G + 0.114B`

### 1.2 实现流程

```
视频文件 → OpenCV 逐帧读取 → BGR→Gray → ROI 裁剪 → 像素均值 → 时间戳+亮度值
```

**关键代码**：`analyze_video.py` → `analyze_luminance()`

| 参数 | 值 | 说明 |
|------|-----|------|
| 采样方式 | 逐帧读取 | `cap.read()` 直到 EOF |
| 灰度转换 | `cv2.cvtColor(frame, cv2.COLOR_BGR2GRAY)` | 标准 ITU-R BT.601 |
| ROI 裁剪 | NumPy 数组切片 `gray[y1:y2, x1:x2]` | 边界安全裁剪 |
| 大 patch 优化 | `cv2.resize(patch, (w/2, h/2))` | patch > 10000px 时降采样 |
| 帧率 | `cv2.CAP_PROP_FPS` | 自动检测 |
| 帧数限制 | 无硬限制 | 通过 `MAX_ANALYSIS_FRAMES=5000` 控制步长 |

**帧步长计算**：
```python
frame_step = max(1, estimated_count // MAX_ANALYSIS_FRAMES)
```
- 视频 30fps × 60s = 1800 帧 → `frame_step=1`（每帧都分析）
- 视频 30fps × 600s = 18000 帧 → `frame_step=3`（每 3 帧取 1 帧）

### 1.3 输出格式

```json
{
  "timestamps": [0, 33.3, 66.7, ...],     // ms
  "luminances": [[128.5, 130.2, ...]],     // 每个 ROI 一组
  "frame_step": 1,
  "total_frames": 1800,
  "fps": 30.0
}
```

### 1.4 MKV 兼容性修复

OpenCV 对 MKV 容器的 `CAP_PROP_FRAME_COUNT` 可能返回不正确的值。修复方案：不依赖帧数作为循环终止条件，改为 `cap.read()` 返回 `ret=False`（EOF）时退出。

---

## 二、音量分析

### 2.1 原理

音量分析计算音频信号的 RMS（均方根）能量，反映声音的响度变化。

**数学表达**：
```
V(t) = sqrt(mean(audio[t : t+frame_length]²))
```

归一化到 [0, 1]：
```
V_norm(t) = V(t) / max(V)
```

### 2.2 实现流程

```
视频文件 → ffmpeg 提取音频（44.1kHz 单声道 WAV）→ 读取 PCM → RMS 计算 → 归一化
```

**关键代码**：`analyze_video.py` → `compute_volume()`

| 参数 | 值 | 说明 |
|------|-----|------|
| 采样率 | 44100 Hz | CD 音质 |
| 声道 | 单声道 | ffmpeg `-ac 1` |
| 帧长度 | 2048 采样 | ≈46.4ms |
| 帧步长 | 512 采样 | ≈11.6ms |
| PCM 格式 | 16-bit signed / 8-bit unsigned | 自动检测 |

**RMS 计算**：
```python
for i in range(0, len(audio) - frame_length, hop_length):
    frame = audio[i:i + frame_length]
    volumes.append(np.sqrt(np.mean(frame ** 2)))
```

### 2.3 dB 转换（C++ 端）

趋势图显示时，线性音量值被转换为 dB 刻度：
```cpp
qreal db = (linear > 0.0001) ? 20.0 * std::log10(linear) : -80.0;
```

Y 轴动态范围：扫描实际最低 dB 值，加 5% margin，底部至少 -80 dB。

### 2.4 输出格式

```json
{
  "volume": [0.12, 0.15, 0.18, ...],  // 归一化 0-1
  "sample_rate": 44100,
  "hop_length": 512,
  "n_fft": 2048,
  "time_resolution_ms": 11.6
}
```

---

## 三、语谱图分析

### 3.1 原理

语谱图是音频信号的**短时傅里叶变换**（STFT）的时频表示。横轴为时间，纵轴为频率，颜色表示该频率在该时刻的能量强度。

**数学表达**：
```
S(f, t) = |Σ[n=0→N-1] audio[t·H + n] · w(n) · e^(-j2πfn/N)|
```

其中：
- `N` = FFT 大小（2048）
- `H` = 帧步长（512）
- `w(n)` = Hanning 窗函数
- 取对数：`S_log(f, t) = log10(S(f, t) + 10⁻¹⁰)`

### 3.2 实现流程

#### Python 端（数据计算）

```
音频 PCM → 分帧 → 加窗 → FFT → 取幅值 → log10 → 降采样 → 写入二进制文件
```

**关键代码**：`analyze_video.py` → `compute_spectrogram()`

| 参数 | 值 | 说明 |
|------|-----|------|
| FFT 大小 | 2048 | 频率分辨率 ≈21.5Hz |
| 帧步长 | 512 | 时间分辨率 ≈11.6ms |
| 窗函数 | Hanning | `np.hanning(n_fft)` |
| 频率 bins | 1025 | `n_fft/2 + 1` |
| 最大帧数 | 8000 | `MAX_SPEC_FRAMES`，超过时列降采样 |
| 输出格式 | float64 二进制 | 行优先（C 顺序） |

**STFT 计算**：
```python
window = np.hanning(n_fft)
for i in range(n_frames):
    start = i * hop_length
    frame = audio[start:start + n_fft] * window
    spectrogram[:, i] = np.abs(np.fft.rfft(frame))
spectrogram = np.log10(spectrogram + 1e-10)
```

#### C++ 端（GPU 渲染）

```
二进制文件 → QFile::readAll → memcpy 到 QVector → 归一化到 [0,1] → 上传为 GL_R32F 纹理 → Fragment Shader 采样 + 颜色映射
```

**渲染管线**：

```
┌─────────────┐    ┌──────────────┐    ┌──────────────┐
│ 频谱纹理     │───→│ Fragment      │───→│ 颜色 LUT     │───→ 屏幕
│ GL_R32F     │    │ Shader       │    │ GL_RGBA32F   │
│ W×H floats  │    │ 对数频率映射  │    │ 256×1 RGBA   │
└─────────────┘    └──────────────┘    └──────────────┘
```

**Fragment Shader 核心逻辑**：
```glsl
#version 330 core
uniform sampler2D spectrogramTexture;  // 频谱数据
uniform sampler2D colorLUTTexture;     // 颜色查找表
uniform float xMin, xMax;              // 时间范围 [0,1]
uniform float yMin, yMax;              // 频率范围 (Hz)
uniform float nyquist;                 // 采样率/2
uniform int useLogScale;               // 对数/线性频率

float logFreqMap(float t) {
    float logMin = log(yMin);
    float logMax = log(yMax);
    float freq = exp(logMin + t * (logMax - logMin));
    return freq / nyquist;  // 归一化到 [0,1]
}

void main() {
    float x = xMin + vTexCoord.x * (xMax - xMin);
    float y = (useLogScale == 1) ? logFreqMap(vTexCoord.y)
              : yMin/nyquist + vTexCoord.y * (yMax - yMin)/nyquist;
    x = clamp(x, 0.0, 1.0);
    y = clamp(y, 0.0, 1.0);
    float value = texture(spectrogramTexture, vec2(x, y)).r;
    fragColor = texture(colorLUTTexture, vec2(value, 0.5));
}
```

### 3.3 颜色映射

三种预设色阶，256 级插值：

| 色阶 | 颜色过渡 | 适用场景 |
|------|----------|----------|
| Thermal（默认） | 黑→深蓝→紫→红→橙→黄→白 | 通用，接近 iZotope RX |
| Inferno | 暗紫→红→橙→黄→亮黄 | 感知均匀，科学可视化 |
| Viridis | 紫→青→绿→黄 | 感知均匀，色盲友好 |

### 3.4 坐标轴

- **X 轴**：时间，与趋势图同步
- **Y 轴**：频率，默认线性 0-8000Hz
- 标签动态间距：`minLabelSpacing = 25px`
- 坐标轴颜色：`#FF981C`（橙色）

### 3.5 交互

| 操作 | 功能 |
|------|------|
| Ctrl+滚轮 | 频率轴缩放（以鼠标位置为中心） |
| Ctrl+拖拽 | 频率轴平移 |
| 双击 Y 轴区域 | 重置频率范围 |
| 点击光标线附近 | 拖拽光标（±10px） |
| 点击其他区域 | 跳转到该时间 |
| 鼠标悬停 | 显示时间/频率/值 tooltip |

### 3.6 底噪阈值

工具栏滑块控制 `m_minValue`（归一化下界），范围 [-10.0, 0.0] dB，默认 -6.0 dB。

- 值低于阈值的频谱点映射为 0（黑色/透明）
- 值高于阈值的频谱点按比例映射到色阶
- 实时调节，立即生效

### 3.7 数据传输优化

**二进制文件方案**：
- Python：`spec.astype(np.float64).tofile(path)` → 临时 `.spec` 文件
- C++：`QFile::readAll()` + `memcpy` → `QVector<QVector<qreal>>`
- 文件大小：1025 bins × 8000 frames × 8 bytes ≈ 65MB
- 读取时间：<100ms（SSD）

---

## 四、数据同步机制

### 4.1 时间轴对齐

三个分析维度共享同一时间轴：

```
视频时间轴:  |-- 0ms -------- 1000ms -------- 2000ms --|
亮度数据:    |  L₀  L₁  L₂ ...                        |  (帧率决定)
音量数据:    | V₀ V₁ V₂ ...                            |  (11.6ms 步长)
语谱图数据:  | S₀ S₁ S₂ ...                            |  (11.6ms 步长)
```

### 4.2 光标同步

```
┌──────────┐  seekRequested  ┌──────────┐  setCursorTime  ┌──────────┐
│ 视频播放  │ ←─────────────→ │ 趋势图    │ ──────────────→ │ 语谱图    │
│ VLC      │  positionChanged │ ChartPanel│                 │ Spectro  │
└──────────┘ ──────────────→ └──────────┘                 └──────────┘
```

- 趋势图拖拽光标 → 发出 `seekRequested` → VLC seek + 语谱图光标更新
- 语谱图拖拽光标 → 发出 `seekRequested` → VLC seek + 趋势图光标更新
- VLC 播放位置变化 → `onPositionChanged` → 同步两个面板（拖拽期间跳过）

### 4.3 X 轴范围同步

```
ChartPanel::xAxisRangeChanged ──→ SpectrogramPanelEnhanced::onXAxisRangeChanged
```

趋势图缩放/平移时，语谱图的 X 轴视窗同步更新。

---

## 五、性能参数

| 指标 | 值 | 说明 |
|------|-----|------|
| 亮度分析速度 | ~1000 帧/秒 | OpenCV 单线程 |
| 音频提取速度 | ~50x 实时 | ffmpeg |
| 频谱计算速度 | ~30x 实时 | NumPy FFT |
| 频谱数据传输 | <100ms | 二进制文件 memcpy |
| GPU 纹理上传 | <50ms | GL_R32F 8000×1025 |
| 渲染帧率 | 60fps | QOpenGLWidget |

---

## 六、文件格式

### 6.1 VLA 分析结果文件（.vla）

自定义 JSON 格式，包含完整分析数据：

```json
{
  "version": 4,
  "timestamps": [...],
  "luminances": [[...]],
  "audio": {
    "volume": [...],
    "spectrogram_file": "/tmp/xxx.spec",
    "spectrogram_shape": [1025, 8000],
    "sample_rate": 44100,
    "hop_length": 512,
    "n_fft": 2048,
    "time_resolution_ms": 11.6,
    "spec_min": -10.0,
    "spec_max": 5.0
  },
  "regions": [...],
  "time_offset": 0
}
```

### 6.2 频谱二进制文件（.spec）

- 格式：raw float64（8 bytes/值）
- 布局：行优先（C 顺序），`[freq_bins][time_frames]`
- 大小：`n_freq_bins × n_frames × 8` bytes
- 临时文件，C++ 读取后自动删除
