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
/// 非实时机录屏）；产物角标可明示「倍速 · 区间墙钟 · 案件号」（v1.15.3：
/// 去掉「演示副本」前缀——取证导出物不标演示字样）。

#include <QObject>
#include <QImage>
#include <QSize>
#include <QString>
#include <QHash>
#include <QJsonObject>
#include "domain/speed_plan.h"
#include "domain/analysis_snapshot.h"   // ChartLabel（导出标签竖标+OSD 5s 烧录）
#include "domain/time_calibration.h"
#include "domain/sync_model.h"

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

        // ---- 多机模式（P-68 第 10 条：多机同步播放同款选段导出）----
        // lanes 非空 → 多机模式：plan 的 aMs/bMs/splits 全部墙钟域；
        // 每路按 syncStreamOf 换算取帧，缺席时刻画「无画面」占位。
        QVector<SyncLaneData> lanes;
        int audioLane = -1;            ///< 音轨来源路（-1 = 无音轨）

        // ---- 放大镜 PIP（真机反馈：导出画面应包含放大镜画面）----
        /// 多机逐路放大（与 lanes 对齐；zoom>1 生效，center 归一化源坐标）
        struct LaneZoomSpec {
            qreal zoom = 1.0;
            QPointF center {0.5, 0.5};
        };
        QVector<LaneZoomSpec> laneZooms;
        /// 单路放大镜（magnifierPip 且 srcRect 非空生效；原视频系裁剪区）
        bool magnifierPip = false;
        QRect magnifierSrcRect;
        int magnifierRotation = 0;
        qreal magnifierZoom = 1.0;

        /// 图表标签（单路模式）：曲线条打竖标 + 播到标签时刻 OSD 烧录
        /// 内容显示 5 秒隐去（真机反馈拍板）
        QVector<ChartLabel> labels;

        // ---- 合成导出 P1（2026-09-03 拍板：多段序列模式，取代分段导出入口）----
        struct ComposeSeg {
            QString sourcePath;          ///< 源文件（多段可多源）
            qint64 inMs = 0, outMs = 0;  ///< 源内毫秒区间 [in, out)
            double rate = 1.0;           ///< 段速率（0.25~8）
        };
        /// 非空 = 多段序列模式（plan/chartBase/specBase/labels/放大镜 PIP 全部忽略）
        QVector<ComposeSeg> segments;
        /// 按源路径取校正时间（无校正或缺失 → 角标回落流内时间）
        QHash<QString, TimeCalibration> calibrationByPath;
        /// 演示片强制角标「分析演示材料 · 非原始证据」（右上，不可关）
        bool demoWatermark = false;
        /// 证据模式：无损直拷 + 侧车清单 JSON（不经过合成管线，像素零改动）
        bool evidenceCopy = false;
        /// 证据清单签署人（账号档案姓名/单位；调用方从 CredentialStore 填）
        QString operatorName;
        QString operatorOrg;
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
    /// 映射版：逐段显式给源流内区间（多机墙钟→该路流内换算后调用）
    static QString buildAudioFilterChainRanges(const QVector<double> &rates,
                                               const QVector<QPair<qint64, qint64>> &streamRanges,
                                               const QString &inputLabel);
    /// atempo 级联分解：rate 拆成 ∈[0.5,2.0] 的因子串（0.25→"0.5,0.5"；8→"2,2,2"）
    static QStringList atempoChain(double rate);

    /// 多段模式音频链：逐段独立输入标签（inputLabels[i] 形如 "2:a"；空串 =
    /// 该段无音轨 → anullsrc 补等长静音）。全分支统一 aresample 到 48k 立体声
    /// （异构源直拷 concat 要求参数一致；8k 单声道 DVR 与 48k 立体声混拼防御）
    static QString buildAudioFilterChainMulti(const QVector<double> &rates,
                                              const QVector<QPair<qint64, qint64>> &streamRanges,
                                              const QStringList &inputLabels);
    /// 多段模式单段输出帧数（纯函数，进度总量用）
    static qint64 composeSegOutFrames(const Params::ComposeSeg &seg, double outFps);
    /// 证据模式清单 JSON（纯函数，单测直验）：侧车内容与产物/源哈希
    static QJsonObject buildEvidenceManifest(const QString &appVersion,
                                             const QString &caseNo,
                                             const QString &opName, const QString &opOrg,
                                             const QVector<Params::ComposeSeg> &segs,
                                             const QStringList &sourceSha256,
                                             const QString &outFileName,
                                             const QString &outSha256, qint64 outBytes);

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
    void runMultiCam();   // 多机模式（lanes 非空；墙钟域 plan）
    void runCompose();        // 合成导出 P1：多段序列（segments 非空）
    void runEvidenceCopy();   // 证据模式：无损直拷 + 侧车清单
    Params m_params;
    QThread *m_thread = nullptr;
    volatile bool m_cancelled = false;
    bool m_running = false;
};

#endif // SEGMENT_EXPORT_ENGINE_H
