# Lumen Arc v0.3 开发计划（最终版）

## 1. 项目概述

**目标版本：** 0.3.0  
**核心功能：**
- Python 算法优化，多进程提速亮度分析
- 后台异步分析，不影响视频浏览
- 多视频合并分析，时间轴无缝衔接
- 音频频谱图显示
- 音量折线图显示
- ffmpeg 便携版打包

**技术栈：**
- C++17 + Qt6 + libVLC (视频播放)
- Python 3.8+ + OpenCV + numpy (分析引擎)
- ffmpeg (音频提取，便携版打包)

---

## 2. 决策确认

| 决策点 | 结论 |
|--------|------|
| ffmpeg | v0.3 打包到便携版 `portable/ffmpeg/` 目录，Python 脚本通过 `--ffmpeg-path` 接收路径 |
| 频谱图 | 独立 QWidget 面板（`SpectrogramPanel`），与亮度图表共享 X 轴（`QValueAxis::rangeChanged` 信号同步） |
| 视频列表 | 左侧 QDockWidget（`VideoListPanel`），QListWidget 拖拽排序 |
| ROI 策略 | 所有视频共享同一组 ROI |
| 多进程 | Python 内部 `multiprocessing.Pool`，C++ 端单 QProcess |
| .vla 兼容 | v4 格式向后兼容 v3，`loadFromFile()` 根据 version 字段区分 |

---

## 3. 开发阶段

### 阶段 0：环境准备

| 步骤 | 操作 | 验证 |
|------|------|------|
| 0.1 | 复制 `LumenArc_v0.2` → `LumenArc_v0.3` | 目录存在，文件完整 |
| 0.2 | 更新 `CMakeLists.txt` 版本号为 `0.3.0` | cmake 配置通过 |

---

### 阶段 1：Python 脚本重构（`analyze_video.py`）

**目标：** 支持多进程分段、音频分析、帧率检查

| 步骤 | 修改内容 | 验证 |
|------|----------|------|
| 1.1 | 添加 `argparse` 参数解析：`video_path`, `roi_json`, `--start-frame`, `--end-frame`, `--audio-only`, `--check-fps`, `--processes`, `--ffmpeg-path` | 命令行参数正确解析 |
| 1.2 | 添加帧率检查功能：`--check-fps` 返回视频 FPS，供 C++ 端校验 | `python analyze_video.py video.mp4 --check-fps` 输出 FPS |
| 1.3 | 添加音频提取：用 `subprocess` 调用 `ffmpeg` 提取 WAV | `ffmpeg -i video.mp4 -vn -ac 1 -ar 16000 -f wav output.wav` |
| 1.4 | 添加纯 numpy 语谱图计算：STFT + 对数刻度 | 输出与 librosa.stft 误差 < 1% |
| 1.5 | 添加纯 numpy 音量计算：RMS 滑动窗口 | 输出归一化音量数组 |
| 1.6 | 修改亮度分析支持 `--start-frame` / `--end-frame` 分段 | 分段结果与全量结果一致 |
| 1.7 | 扩展 JSON 输出格式，增加 `audio` 和 `fps` 字段 | JSON 格式正确 |
| 1.8 | 添加 `--audio-only` 模式：只提取音频，不分析亮度 | 音频分析独立运行 |
| 1.9 | 添加 `--processes` 多进程：`multiprocessing.Pool` 分段并行亮度分析 | 4 进程结果与单进程一致 |
| 1.10 | 添加 `--ffmpeg-path` 参数：优先使用该路径，其次脚本同目录，最后 PATH | 三种路径都能找到 ffmpeg |

**JSON 输出格式（v0.3）：**
```json
{
  "timestamps": [0, 33, 66, ...],
  "luminances": [[120.5, 121.0, ...]],
  "frame_step": 1,
  "total_frames": 1800,
  "fps": 30.0,
  "audio": {
    "volume": [0.3, 0.5, 0.8, ...],
    "spectrogram": [[0.1, 0.2, ...], ...],
    "sample_rate": 16000,
    "hop_length": 512,
    "n_fft": 2048,
    "time_resolution_ms": 32.0
  }
}
```

**纯 numpy 实现：**

```python
def compute_spectrogram(audio, sr=16000, n_fft=2048, hop_length=512):
    """短时傅里叶变换，返回对数刻度语谱图"""
    window = np.hanning(n_fft)
    n_frames = 1 + (len(audio) - n_fft) // hop_length
    spectrogram = np.zeros((n_fft // 2 + 1, n_frames))
    for i in range(n_frames):
        start = i * hop_length
        frame = audio[start:start + n_fft] * window
        spectrum = np.abs(np.fft.rfft(frame))
        spectrogram[:, i] = spectrum
    # 对数刻度（避免 log(0)）
    spectrogram = np.log10(spectrogram + 1e-10)
    return spectrogram

def compute_volume(audio, sr=16000, frame_length=2048, hop_length=512):
    """RMS 音量计算，归一化到 0-1"""
    volumes = []
    for i in range(0, len(audio) - frame_length, hop_length):
        frame = audio[i:i + frame_length]
        rms = np.sqrt(np.mean(frame ** 2))
        volumes.append(rms)
    volumes = np.array(volumes)
    if volumes.max() > 0:
        volumes = volumes / volumes.max()
    return volumes.tolist()
```

**多进程合并逻辑（Python 内部）：**
```python
def analyze_segment(args):
    video_path, roi_json, start_frame, end_frame = args
    # 调用 analyze_luminance(video_path, roi_json, start_frame, end_frame)
    return result

if __name__ == '__main__':
    if args.processes > 1:
        segments = split_frames(total_frames, args.processes)
        with Pool(args.processes) as pool:
            results = pool.map(analyze_segment, segments)
        merged = merge_results(results)
    else:
        merged = analyze_single(args)

    if not args.audio_only:
        audio = analyze_audio(args)  # 主进程执行
        merged['audio'] = audio

    print(json.dumps(merged))
```

**依赖变更：**
```
# 保持不变
opencv-python>=4.8.0
numpy>=1.24.0

# 新增系统依赖（便携版打包，不需要 Python 库）
ffmpeg  # 命令行工具，打包在 portable/ffmpeg/ 目录
```

---

### 阶段 2：C++ 数据结构扩展

**目标：** 扩展 `AnalysisSnapshot` 支持音频数据

| 步骤 | 修改文件 | 修改内容 | 验证 |
|------|----------|----------|------|
| 2.1 | `analysis_snapshot.h` | 添加 `AudioData` 结构体 | 编译通过 |
| 2.2 | `analysis_snapshot.h` | `AnalysisSnapshot` 添加 `AudioData audio` 成员 | 编译通过 |
| 2.3 | `analysis_snapshot.h` | 添加 `hasAudio()` 方法 | 编译通过 |
| 2.4 | `analysis_snapshot.h` | 添加 `volumePointsForViewport()` 方法 | 编译通过 |
| 2.5 | `analysis_snapshot.h` | 添加 `spectrogramForViewport()` 方法 | 编译通过 |
| 2.6 | `analysis_snapshot.h` | 修改 `exportToCsv()` 包含音频数据列 | CSV 包含音量列 |
| 2.7 | `timeline_model.h/cpp` | 修改 `setData()` 接受音频数据 | 编译通过 |
| 2.8 | `timeline_model.cpp` | 修改 `saveToFile()` 输出 v4 格式 | .vla 包含 audio 字段 |
| 2.9 | `timeline_model.cpp` | 修改 `loadFromFile()` 支持 v3/v4 加载 | v3 文件正常加载（audio 为空） |

**新增数据结构：**
```cpp
// analysis_snapshot.h
struct AudioData {
    QVector<qreal> volume;           // 归一化音量 0-1
    QVector<QVector<qreal>> spectrogram;  // [频率bin][时间帧]
    qreal sampleRate = 16000;
    int hopLength = 512;
    int nFft = 2048;
    qreal timeResolutionMs = 32.0;   // 每帧对应毫秒数

    bool isEmpty() const { return volume.isEmpty(); }

    QVector<QPointF> volumePointsForViewport(qint64 tMin, qint64 tMax,
                                              const QVector<qint64> &timestamps) const;

    QVector<QVector<qreal>> spectrogramForViewport(qint64 tMin, qint64 tMax,
                                                     const QVector<qint64> &timestamps) const;
};
```

**.vla v4 格式变更：**
```json
{
  "version": 4,
  "...": "...(v3 所有字段保留)",
  "audio": {
    "volume": [0.3, 0.5, ...],
    "spectrogram": [[...], ...],
    "sample_rate": 16000,
    "hop_length": 512,
    "n_fft": 2048
  }
}
```

---

### 阶段 3：Python 分析引擎重构（多进程 + ffmpeg）

**目标：** 自适应多进程、ffmpeg 路径传递、取消机制

| 步骤 | 修改文件 | 修改内容 | 验证 |
|------|----------|----------|------|
| 3.1 | `python_analysis_engine.h` | 添加 `getVideoFps()` 方法 | 编译通过 |
| 3.2 | `python_analysis_engine.h` | 添加 `m_cancelled` 标志 | 编译通过 |
| 3.3 | `python_analysis_engine.cpp` | 实现帧率检查：调用 `python analyze_video.py --check-fps` | 返回正确 FPS |
| 3.4 | `python_analysis_engine.cpp` | 构造 ffmpeg 路径：`appDir + "/ffmpeg/ffmpeg.exe"` 或同目录 | 路径正确 |
| 3.5 | `python_analysis_engine.cpp` | 自适应进程数：根据视频时长决定进程数 | 短视频单进程，长视频多进程 |
| 3.6 | `python_analysis_engine.cpp` | 修改 `startAnalysis()` 传递 `--processes` 和 `--ffmpeg-path` | 参数正确 |
| 3.7 | `python_analysis_engine.cpp` | 解析 JSON 中的 `audio` 字段填充 `AudioData` | AudioData 正确 |
| 3.8 | `python_analysis_engine.cpp` | 优化取消机制：`m_cancelled = true` + `proc->kill()` | 取消后可重新分析 |

**自适应策略：**
```cpp
int computeProcessCount(qint64 totalFrames, float fps) {
    qint64 durationSec = totalFrames / fps;
    int maxProcs = QThread::idealThreadCount();
    if (durationSec < 30)  return 1;  // 短视频：单进程
    if (durationSec < 120) return qMin(2, maxProcs);  // 中等：2进程
    return qMin(4, maxProcs);               // 长视频：最多4进程
}
```

**ffmpeg 路径构造：**
```cpp
QString findFfmpegPath() {
    QString appDir = QCoreApplication::applicationDirPath();
    // 优先检查便携版打包位置
    QString bundled = appDir + "/ffmpeg/ffmpeg.exe";
    if (QFile::exists(bundled)) return bundled;
    // 同目录
    QString sameDir = appDir + "/ffmpeg.exe";
    if (QFile::exists(sameDir)) return sameDir;
    // 系统 PATH
    return "ffmpeg";
}
```

---

### 阶段 4：多视频管理（VideoListPanel）

**目标：** 支持添加多个视频，首尾相接分析

| 步骤 | 修改文件 | 修改内容 | 验证 |
|------|----------|----------|------|
| 4.1 | `mainwindow.h` | 添加 `VideoEntry` 结构体和 `m_videoList` | 编译通过 |
| 4.2 | 新建 `videolistpanel.h` | `VideoListPanel : QDockWidget`，含 `QListWidget` + 拖拽排序 | 编译通过 |
| 4.3 | 新建 `videolistpanel.cpp` | 实现添加/移除/清空/拖拽排序/时长显示 | 功能可用 |
| 4.4 | `mainwindow.cpp` | 构造函数创建 `VideoListPanel`，dock 到左侧 | 面板显示 |
| 4.5 | `mainwindow.cpp` | 修改 `onOpenFile()` 支持多选文件 | 可选择多个视频 |
| 4.6 | `mainwindow.cpp` | 添加"添加视频"菜单/按钮 | UI 可用 |
| 4.7 | `mainwindow.cpp` | 实现帧率检查：分析前校验所有视频 FPS | 不一致时弹窗警告 |
| 4.8 | `mainwindow.cpp` | 修改 `onAnalyze()` 支持多视频分析 | 多视频依次分析 |
| 4.9 | `mainwindow.cpp` | 修改 `onAnalysisFinished()` 处理多视频结果 | 时间轴连续显示 |
| 4.10 | `mainwindow.cpp` | 修改 `openVideoFile()` 支持拖入多个文件 | 拖入多个视频自动添加 |

**数据结构：**
```cpp
struct VideoEntry {
    QString filePath;
    qint64 durationMs = 0;
    float fps = 30.0f;
    qint64 timeOffsetMs = 0;  // 在合并时间轴中的起始位置
};
```

**VideoListPanel 设计：**
```
VideoListPanel (QDockWidget, 左侧 dock)
├── QListWidget (InternalMove 拖拽排序)
│   ├── video1.mp4  [00:30] 30fps  (当前)
│   ├── video2.mp4  [01:15] 30fps
│   └── video3.mp4  [00:45] 30fps
├── 按钮栏：[添加] [移除] [清空]
└── 状态标签：共 3 个视频 | 总时长 02:30
```

**信号/槽：**
```cpp
signals:
    void videoAdded(const QString &path);
    void videoRemoved(int index);
    void videoReordered();
    void videoSelected(int index);  // 双击切换播放
```

**帧率检查逻辑：**
```cpp
bool checkFrameRates(const QList<VideoEntry> &videos) {
    float firstFps = videos.first().fps;
    for (const auto &v : videos) {
        if (qAbs(v.fps - firstFps) > 0.1f) return false;
    }
    return true;
}
```

---

### 阶段 5：后台异步分析（UI 不阻塞）

**目标：** 分析时可继续浏览视频

| 步骤 | 修改文件 | 修改内容 | 验证 |
|------|----------|----------|------|
| 5.1 | `mainwindow.h` | 移除 `m_progressDlg`，添加 `m_statusLabel` + `m_progressBar` + `m_cancelBtn` | 编译通过 |
| 5.2 | `mainwindow.cpp` | 移除模态 `QProgressDialog` | 编译通过 |
| 5.3 | `mainwindow.cpp` | 添加状态栏进度显示（QStatusBar + QProgressBar） | 状态栏显示进度 |
| 5.4 | `mainwindow.cpp` | 添加工具栏"取消分析"按钮 | 按钮可用 |
| 5.5 | `mainwindow.cpp` | 修改 `onAnalysisProgress()` 更新状态栏 | 进度实时更新 |
| 5.6 | `mainwindow.cpp` | 分析期间保持视频播放功能 | 可播放/暂停/拖动 |
| 5.7 | `mainwindow.cpp` | 切换视频时自动取消当前分析 | 切换后可重新分析 |

**状态栏布局：**
```
[状态栏] 已分析 500 帧 (25.0%)  [======>          ]  [取消分析]
```

---

### 阶段 6：频谱图显示（独立 SpectrogramPanel）

**目标：** 在亮度曲线下方显示频谱图

| 步骤 | 修改文件 | 修改内容 | 验证 |
|------|----------|----------|------|
| 6.1 | 新建 `spectrogrampanel.h` | `SpectrogramPanel : QWidget`，含 `m_audioData` 缓存 | 编译通过 |
| 6.2 | 新建 `spectrogrampanel.cpp` | 实现 `paintEvent()`：QImage 热力图渲染 | 编译通过 |
| 6.3 | `spectrogrampanel.cpp` | 实现 `setSpectrogramData(AudioData)` | 编译通过 |
| 6.4 | `spectrogrampanel.cpp` | 实现 `onXAxisRangeChanged(xMin, xMax)` 槽 | 编译通过 |
| 6.5 | `spectrogrampanel.cpp` | 颜色映射：蓝→绿→黄→红 | 频谱图颜色正确 |
| 6.6 | `spectrogrampanel.cpp` | Y 轴标签：频率（Hz） | 标签正确 |
| 6.7 | `mainwindow.cpp` | 垂直 QSplitter 添加 `SpectrogramPanel`（ChartPanel 下方） | 布局正确 |
| 6.8 | `mainwindow.cpp` | 连接 `ChartPanel::m_axisX::rangeChanged` → `SpectrogramPanel::onXAxisRangeChanged` | X 轴同步 |
| 6.9 | `mainwindow.cpp` | `onDataReplaced()` 中更新频谱图数据 | 数据加载后显示 |

**频谱图面板设计：**
```cpp
class SpectrogramPanel : public QWidget {
    Q_OBJECT
public:
    explicit SpectrogramPanel(QWidget *parent = nullptr);
    void setSpectrogramData(const AudioData &audio);
    void clear();

public slots:
    void onXAxisRangeChanged(qreal xMin, qreal xMax);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QImage renderSpectrogram(int width, int height);
    QColor spectrogramColor(qreal value);

    AudioData m_audioData;
    qreal m_viewXMin = 0;
    qreal m_viewXMax = 0;
    QImage m_cachedImage;
    bool m_needsRedraw = true;
};
```

**颜色映射：**
```cpp
QColor SpectrogramPanel::spectrogramColor(qreal value) {
    // value 范围：假设 -10 到 5（对数刻度）
    qreal normalized = qBound(0.0, (value + 10.0) / 15.0, 1.0);
    int h = static_cast<int>((1.0 - normalized) * 240);  // 蓝(240)→红(0)
    return QColor::fromHsv(h, 255, 200);
}
```

**X 轴同步：**
```cpp
// ChartPanel 构造函数中：
connect(m_axisX, &QValueAxis::rangeChanged,
        m_spectrogramPanel, &SpectrogramPanel::onXAxisRangeChanged);

// SpectrogramPanel 槽：
void SpectrogramPanel::onXAxisRangeChanged(qreal xMin, qreal xMax) {
    m_viewXMin = xMin;
    m_viewXMax = xMax;
    m_needsRedraw = true;
    update();  // 触发 paintEvent
}
```

**布局调整：**
```cpp
// MainWindow 构造函数中：
m_spectrogramPanel = new SpectrogramPanel(this);
m_splitter->addWidget(m_videoWidget);        // [0] stretch=2
m_splitter->addWidget(m_chartPanel);         // [1] stretch=1
m_splitter->addWidget(m_spectrogramPanel);   // [2] stretch=0.5
m_splitter->setStretchFactor(0, 2);
m_splitter->setStretchFactor(1, 1);
m_splitter->setStretchFactor(2, 0);
```

---

### 阶段 7：音量折线图显示（ChartPanel 内）

**目标：** 在亮度曲线旁显示音量折线

| 步骤 | 修改文件 | 修改内容 | 验证 |
|------|----------|----------|------|
| 7.1 | `chartpanel.h` | 添加 `m_volumeSeries` (QLineSeries*) | 编译通过 |
| 7.2 | `chartpanel.h` | 添加 `m_axisYVolume` (QValueAxis*) | 编译通过 |
| 7.3 | `chartpanel.cpp` | 在 `rebuildSeries()` 中创建音量系列 | 编译通过 |
| 7.4 | `chartpanel.cpp` | 在 `onDataReplaced()` 中填充音量数据 | 音量折线显示 |
| 7.5 | `chartpanel.cpp` | 设置音量系列样式：绿色半透明 | 视觉区分明显 |
| 7.6 | `chartpanel.cpp` | 右侧 Y 轴标签 "Volume (0-1)" | 轴标签正确 |

**音量系列创建：**
```cpp
void ChartPanel::rebuildSeries() {
    // ... 现有亮度系列代码 ...

    // 音量系列
    m_volumeSeries = new QLineSeries();
    m_volumeSeries->setName("Volume");
    QPen volumePen(QColor(76, 175, 80, 180));
    volumePen.setWidth(1);
    m_volumeSeries->setPen(volumePen);

    if (!m_axisYVolume) {
        m_axisYVolume = new QValueAxis();
        m_axisYVolume->setRange(0, 1);
        m_axisYVolume->setTitleText("Volume");
        m_axisYVolume->setLabelsVisible(false);
        m_chart->addAxis(m_axisYVolume, Qt::AlignRight);
    }

    m_chart->addSeries(m_volumeSeries);
    m_volumeSeries->attachAxis(m_axisX);
    m_volumeSeries->attachAxis(m_axisYVolume);
}
```

---

### 阶段 8：版本更新和 ffmpeg 打包

| 步骤 | 修改文件 | 修改内容 | 验证 |
|------|----------|----------|------|
| 8.1 | `CMakeLists.txt` | 版本号 `0.3.0`，macOS Bundle 版本同步更新 | cmake 配置通过 |
| 8.2 | `mainwindow.cpp` | 窗口标题 "Lumen Arc v0.3 Beta" | 标题显示正确 |
| 8.3 | `CMakeLists.txt` | POST_BUILD 复制 ffmpeg 目录到输出目录 | ffmpeg.exe 存在 |
| 8.4 | `setup_python_deps.bat` | 添加 ffmpeg 检测和下载指引 | 脚本可用 |
| 8.5 | `setup_deps.py` | 添加 ffmpeg 下载功能（从 GitHub releases 下载 static build） | 下载成功 |
| 8.6 | `build_mac.sh` | 添加 ffmpeg 复制步骤 | macOS 打包正确 |
| 8.7 | `portable/` | 添加 `ffmpeg/ffmpeg.exe`（通过脚本下载，不入 git） | 便携版可用 |

**ffmpeg 打包策略：**
```
portable/
├── LumenArc.exe
├── analyze_video.py
├── ffmpeg/
│   └── ffmpeg.exe          ← 新增
├── libvlc.dll
├── ... (Qt DLLs)
```

**CMakeLists.txt 添加：**
```cmake
# Copy ffmpeg to build output
if(WIN32)
    set(FFMPEG_SRC "${CMAKE_CURRENT_SOURCE_DIR}/portable/ffmpeg")
    if(EXISTS "${FFMPEG_SRC}/ffmpeg.exe")
        add_custom_command(TARGET ${PROJECT_NAME} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
                "${FFMPEG_SRC}"
                "$<TARGET_FILE_DIR:${PROJECT_NAME}>/ffmpeg"
            COMMENT "Copying ffmpeg..."
        )
    endif()
endif()
```

---

## 4. 文件修改清单汇总

| 文件 | 操作 | 修改量 |
|------|------|--------|
| `analyze_video.py` | **重构** | 大量（+350 行） |
| `src/domain/analysis_snapshot.h` | **扩展** | 中等（+80 行） |
| `src/domain/timeline_model.h` | **扩展** | 少量（+10 行） |
| `src/domain/timeline_model.cpp` | **扩展** | 中等（+50 行） |
| `src/infrastructure/python_analysis_engine.h` | **扩展** | 少量（+15 行） |
| `src/infrastructure/python_analysis_engine.cpp` | **重构** | 中等（+100 行） |
| `src/mainwindow.h` | **扩展** | 中等（+30 行） |
| `src/mainwindow.cpp` | **重构** | 大量（+200 行） |
| `src/chartpanel.h` | **扩展** | 少量（+10 行） |
| `src/chartpanel.cpp` | **扩展** | 中等（+80 行） |
| `src/videolistpanel.h` | **新建** | 少量（+40 行） |
| `src/videolistpanel.cpp` | **新建** | 中等（+120 行） |
| `src/spectrogrampanel.h` | **新建** | 少量（+30 行） |
| `src/spectrogrampanel.cpp` | **新建** | 中等（+150 行） |
| `CMakeLists.txt` | **修改** | 少量（+20 行） |
| `setup_python_deps.bat` | **修改** | 少量（+10 行） |
| `setup_deps.py` | **修改** | 少量（+30 行） |

**总计新增代码：约 1300-1500 行**

---

## 5. 依赖变更

| 依赖 | 状态 | 说明 |
|------|------|------|
| Python 3.8+ | 不变 | 已有 |
| OpenCV (opencv-python) | 不变 | 已有 |
| numpy | 不变 | 已有 |
| ffmpeg | **新增（便携版打包）** | 打包在 `portable/ffmpeg/` 目录 |
| Qt6::Widgets | 不变 | 已有 |
| Qt6::Charts | 不变 | 已有 |
| libVLC | 不变 | 已有 |

---

## 6. 执行顺序

```
阶段 0 (目录准备)
    ↓
阶段 1 (Python 脚本) ← 独立，可先行
    ↓
阶段 2 (C++ 数据结构) ← 依赖阶段 1 的 JSON 格式
    ↓
阶段 3 (分析引擎重构) ← 依赖阶段 1, 2
    ↓
阶段 4 (多视频管理) ← 依赖阶段 3
    ↓
阶段 5 (后台异步) ← 依赖阶段 3
    ↓
阶段 6 (频谱图) ← 依赖阶段 2
    ↓
阶段 7 (音量折线图) ← 依赖阶段 2
    ↓
阶段 8 (版本更新 + ffmpeg)
```

**阶段 4 和 5 可并行**（都依赖阶段 3，互不依赖）。
**阶段 6 和 7 可并行**（都依赖阶段 2，互不依赖）。

---

## 7. 风险点和应对

| 风险 | 应对措施 |
|------|----------|
| ffmpeg 未安装 | 启动时检测，弹窗提示安装路径；亮度分析不受影响 |
| 多进程内存溢出 | 自适应策略限制进程数，监控内存 |
| 频谱图渲染性能 | QImage 缓存 + `m_needsRedraw` 标志，仅 viewport 变化时重绘 |
| numpy STFT 性能 | 离线分析可接受，必要时可加进度提示 |
| .vla 向后兼容 | version 字段区分 v3/v4，加载时降级处理 |
| Windows multiprocessing | 当前脚本已有 `if __name__ == '__main__':` 守卫 |

---

## 8. 新增文件清单

| 文件 | 类型 | 说明 |
|------|------|------|
| `src/videolistpanel.h` | 新建 | 视频列表面板头文件 |
| `src/videolistpanel.cpp` | 新建 | 视频列表面板实现 |
| `src/spectrogrampanel.h` | 新建 | 频谱图面板头文件 |
| `src/spectrogrampanel.cpp` | 新建 | 频谱图面板实现 |
| `portable/ffmpeg/ffmpeg.exe` | 外部下载 | 便携版 ffmpeg（不入 git） |

---

## 9. 技术选型总结

| 功能 | 选型 | 理由 |
|------|------|------|
| 多进程分析 | Python multiprocessing.Pool | 进程管理简单，数据共享方便 |
| 音频提取 | subprocess 调用 ffmpeg | 依赖最少，最稳定 |
| 频谱图计算 | 纯 numpy FFT | 零额外依赖，性能可接受 |
| 音量计算 | 纯 numpy RMS | 简单高效 |
| 频谱图渲染 | 独立 QWidget + QImage | 性能最优，与 Qt Charts 解耦 |
| 音量折线 | QLineSeries + 右侧 Y 轴 | 与亮度曲线共用 X 轴 |
| 视频列表 | QDockWidget + QListWidget | 与 MagnifierWidget 风格一致 |
| 帧率检查 | 导入时检测 | 提前发现问题 |
| 多视频合并 | 首尾相接，毫秒时间戳 | 不受帧率影响 |
| ffmpeg 打包 | 便携版目录 | 开箱即用，不依赖系统 PATH |
