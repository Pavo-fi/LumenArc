/**
 * @file libav_analysis_engine.h
 * @brief 进程内 FFmpeg 分析引擎（v1.5.0 P3）：libav 原生亮度分析
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-15
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计（V1_ERA_TECH_PLAN §6）：
 *  - 进程内 avformat→avcodec→swscale GRAY8（BT.601 表，复刻 Python 快速路径
 *    的 ffmpeg format=gray 语义，Q-14 方案 A）
 *  - 全帧率亮度（解除 MAX_ANALYSIS_FRAMES=5000 抽稀上限）
 *  - ROI 统计语义与 analyze_video.py 逐字对齐：
 *      rect    → int(round()) 缩放 + clamp + 区域均值
 *      polygon → cv2.fillPoly 扫描线掩码语义 → 覆盖像素均值
 *  - 时间戳 = 帧真实 PTS（showinfo pts_time 语义，容器元数据 fps 无关）
 *  - 多视频合并（B2）：按各视频 total_frames/fps 累计偏移
 *  - 工作线程：QThread 承载，信号回投（不阻塞 UI）
 */
#pragma once

#include "ianalysis_engine.h"
#include <QVector>
#include <QRect>
#include <QPolygon>
#include <QPair>
#include <atomic>

struct AVFormatContext;
struct AVCodecContext;
struct AVFrame;
struct SwsContext;

/**
 * @brief 进程内 libav 离线分析引擎（IAnalysisEngine 第二实现）。
 */
class LibavAnalysisEngine : public IAnalysisEngine
{
    Q_OBJECT

public:
    explicit LibavAnalysisEngine(QObject *parent = nullptr);
    ~LibavAnalysisEngine() override;

    void startAnalysis(const QString &videoPath, const QVector<QRect> &regions,
                       const QVector<QPolygon> &polygons = {},
                       const QStringList &extraVideos = {},
                       const QVector<int> &rectRoiIds = {},
                       const QVector<int> &polygonRoiIds = {}) override;
    void startAudioAnalysis(const QString &videoPath);
    void cancelAnalysis() override;
    bool isRunning() const override;

    VideoTiming videoTiming(const QString &videoPath) override;

    /// 多边形扫描线光栅化结果：每行一条覆盖区间（cv2.fillPoly 扫描线语义）。
    struct RoiSpan {
        int y = 0;    ///< 行号 [0, height)
        int x1 = 0;   ///< 区间起点（含）
        int x2 = 0;   ///< 区间终点（不含）
    };

    /// ROI 多边形扫描线光栅化：对每条扫描线输出覆盖 span 列表
    /// （供 ROI 均值统计与单测共用）。
    static QVector<RoiSpan> rasterizePolygonSpans(
        const QPolygon &poly, int width, int height);

    /// 矩形 ROI 缩放取整语义（analyze_video.py _build_roi_masks 逐字对齐）：
    /// 返回 clamp 到 [0,w]x[0,h] 的整数区间。
    static QRect scaleRectToFrame(const QRect &roi, int outW, int outH,
                                  int origW, int origH);

signals:
    /// 内部：请求工作线程启动亮度分析（startAnalysis 时发射）
    void beginAnalysis();
    /// 内部：请求工作线程启动音频分析（startAudioAnalysis 时发射）
    void beginAudio();

private:
    struct RoiSpec {
        enum Kind { Rect, Polygon };
        Kind kind = Rect;
        int roiId = -1;
        QRect rect;        // kind==Rect
        QPolygon polygon;  // kind==Polygon
    };

    // ---- 分析任务（工作线程执行） ----
    void runLuminanceTask();
    void runAudioTask();

    // ---- libav 封装 ----
    bool openVideo(const QString &path, AVFormatContext **fmt,
                   AVCodecContext **dec, int *vstream);
    void closeVideo(AVFormatContext *fmt, AVCodecContext *dec);
    /// 读到下一帧视频帧（跳过非视频包），成功返回 true 且 frame 已解码。
    bool readNextVideoFrame(AVFormatContext *fmt, AVCodecContext *dec,
                            int vstream, AVFrame *frame);
    /// 单视频全帧率亮度分析（含 ROI 统计）。失败返回 false。
    /// totalFramesEst 用于进度心跳（<=0 不发逐帧进度）。
    bool analyzeLuminanceOne(const QString &path, const QVector<RoiSpec> &rois,
                             qint64 totalFramesEst,
                             QVector<qint64> *outTs, QVector<QVector<qreal>> *outLums);

    /// 单视频音频分析：PCM(24000 mono) → RMS + STFT（对齐 analyze_video.py 语义）。
    /// 成功返回 true 且 out 已填。
    bool analyzeAudioOne(const QString &path, AudioData *out);

    // 线程
    class Worker;
    Worker *m_worker = nullptr;      // 持有 QThread（engine 生命周期内常驻）

    std::atomic<bool> m_cancel{false};
    std::atomic<bool> m_running{false};

    // 任务参数（startAnalysis 时填好，worker 读取）
    QString m_videoPath;
    QStringList m_extraVideos;
    QVector<RoiSpec> m_rois;

    // v1.7.1：videoTiming 缓存（切换视频路径上多次调用，大文件每次
    // open+find_stream_info 是秒级开销——用户实测切换卡顿）
    struct TimingCacheEntry {
        VideoTiming timing;
        qint64 sizeBytes = 0;
        qint64 mtimeMs = 0;
    };
    QHash<QString, TimingCacheEntry> m_timingCache;
};
