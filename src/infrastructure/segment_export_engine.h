#ifndef SEGMENT_EXPORT_ENGINE_H
#define SEGMENT_EXPORT_ENGINE_H

/// @file segment_export_engine.h
/// @brief 选段分段变速复合导出引擎（P-68，docs/SEGMENT_EXPORT_TECH_DESIGN_CN.md
///        拍板定稿）：源文件只读 → 进程内逐帧解码（分段变速映射）→ QImage 画布
///        合成（上视频 + 下亮度曲线/语谱[无数据自动隐藏] + 可选 OSD 角标）
///        → rawvideo 管道喂 ffmpeg 子进程（libx264 CRF18 + 音频分段
///        atrim/atempo/concat）→ 单个 MP4。
///
/// 取证红线：源文件永不写；离线确定性合成（每帧时间戳由 speedplan 纯函数决定，
/// 非实时机录屏）；产物角标可明示「演示副本 · 倍速 · 区间墙钟 · 案件号」。

#include <QObject>
#include <QImage>
#include <QSize>
#include <QString>
#include "domain/speed_plan.h"
#include "domain/time_calibration.h"

class QThread;

class SegmentExportEngine : public QObject
{
    Q_OBJECT

public:
    struct Params {
        QString sourcePath;            ///< 源视频（只读）
        QString outputPath;            ///< 产物 MP4 路径
        speedplan::SpeedPlan plan;     ///< 分段变速方案（已 normalize）
        double outFps = 25.0;          ///< 输出帧率 = 源帧率
        QSize canvas {1920, 1080};     ///< 固定 1080p（拍板 Q6）
        QImage chartBase;              ///< 亮度曲线底图（[A,B] 区间光栅化；空=隐藏面板）
        QImage specBase;               ///< 语谱底图（同上）
        bool burnOsd = true;           ///< 烧录信息角标（拍板 Q2：可选）
        QString caseLabel;             ///< 案件号（OSD 末段；空=不显示）
        TimeCalibration calibration;   ///< 有效则 OSD 显示区间墙钟（北京时间口径）
    };

    explicit SegmentExportEngine(QObject *parent = nullptr);
    ~SegmentExportEngine() override;

    /// 启动导出（异步：内部工作线程解码合成 + ffmpeg 子进程编码）。
    /// 参数非法（plan 无效/路径空/画布空）立即 finished(false, ...)。
    void start(const Params &p);
    /// 取消：杀子进程、清理半成品文件（幂等，线程安全）
    void cancel();
    bool isRunning() const;

    /// 纯函数：分段音频 filter_complex 链（atrim+atempo 级联[0.5,2] + concat），
    /// 供单测直验。inputLabel 形如 "1:a"。无音频/单段 1x 仍返回合法链。
    static QString buildAudioFilterChain(const speedplan::SpeedPlan &plan,
                                         const QString &inputLabel);
    /// atempo 级联分解：rate 拆成 ∈[0.5,2.0] 的因子串（0.25→"0.5,0.5"；8→"2,2,2"）
    static QStringList atempoChain(double rate);

    /// 布局计算（纯函数）：给定画布与可用面板数（0~2），返回
    /// {视频区, 曲线区, 语谱区}（不可用面板区为空矩形）。上下布局（拍板 Q1 改判）。
    static void layoutRects(const QSize &canvas, bool hasChart, bool hasSpec,
                            QRect *videoRect, QRect *chartRect, QRect *specRect);

signals:
    void progress(int doneFrames, int totalFrames);
    /// ok=false 时 message 为类型化错误（[SEGMENT_EXPORT] 前缀）；取消时为「已取消」
    void finished(bool ok, const QString &message);

private:
    void run();   // 工作线程体
    Params m_params;
    QThread *m_thread = nullptr;
    volatile bool m_cancelled = false;
    bool m_running = false;
};

#endif // SEGMENT_EXPORT_ENGINE_H
