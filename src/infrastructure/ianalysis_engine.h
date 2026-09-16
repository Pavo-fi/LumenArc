/**
 * @file ianalysis_engine.h
 * @brief 离线亮度分析引擎抽象接口
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include <QObject>
#include <QVector>
#include <QRect>
#include <QPolygon>
#include <QStringList>
#include "domain/analysis_snapshot.h"

/**
 * @brief Abstract interface for offline luminance analysis engines.
 */
class IAnalysisEngine : public QObject
{
    Q_OBJECT

public:
    explicit IAnalysisEngine(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~IAnalysisEngine() = default;

    /**
     * @brief Start luminance analysis.
     * @param videoPath  primary video (also the one whose ROI/timing anchors the timeline)
     * @param regions    ROI rectangles (shared across all videos)
     * @param polygons   ROI polygons (shared across all videos, v0.5)
     * @param extraVideos  additional videos to merge after the primary one (B2).
     *                     Empty = single-video analysis. The engine concatenates all
     *                     videos on a continuous timeline.
     */
    virtual void startAnalysis(const QString &videoPath, const QVector<QRect> &regions,
                               const QVector<QPolygon> &polygons = {},
                               const QStringList &extraVideos = {},
                               const QVector<int> &rectRoiIds = {},
                               const QVector<int> &polygonRoiIds = {}) = 0;
    virtual void cancelAnalysis() = 0;
    virtual bool isRunning() const = 0;

    /**
     * @brief Engine-neutral video timing info (fps + trusted duration).
     *
     * Capability raised to the interface per R2 (callers must not downcast
     * to concrete engines). Default: unknown (fps=0, durationMs=0).
     */
    struct VideoTiming {
        float fps = 0.0f;          ///< measured fps (<=0 = unknown)
        qint64 durationMs = 0;     ///< trusted duration in ms (<=0 = unknown)
    };
    virtual VideoTiming videoTiming(const QString &videoPath);

    /**
     * @brief Trusted duration in ms (0 = unknown). Convenience wrapper.
     * Sunk from MainWindow::trustedDurationFor (preprocess design §3.4).
     */
    virtual qint64 trustedDurationMs(const QString &videoPath);

    /**
     * @brief Start audio-only analysis (volume + spectrogram).
     * R2 收口：从 PythonAnalysisEngine 上移接口（v1.5.0-3）。
     * 默认实现：不支持音频的引擎经 analysisFailed 报错。
     */
    virtual void startAudioAnalysis(const QString &videoPath);

    /// P-54：设置音频降噪强度（谱门控 α，0=关闭；1.0=标准，≥2.0=强）。
    /// 在 startAudioAnalysis 之前调用生效；默认实现空操作。
    virtual void setAudioDenoiseStrength(double strength);

    /// @brief 微变变化率曲线参数（流内时间，ms）
    struct MicroDiffCurveParams
    {
        qint64 baseStartMs = 0;   ///< 用户标记的干净基准段起点
        qint64 baseDurMs = 0;     ///< 干净基准段时长
    };

    /**
     * @brief 开始微变变化率曲线分析（2026-09-10）。
     *
     * 两遍：Pass A 从干净基准段提取基准（复用微变显示同一提取器），
     * Pass B 全片扫描逐帧 ROI 微差统计 → 逐秒 blkMax/medD + 首帧微变判定。
     * 结果经既有 analysisFinished 回传（snapshot 仅含 microdiff 通道，
     * 任务服务按通道合并，亮度/音频通道保留）。
     * 默认实现：不支持（analysisFailed），与 startAudioAnalysis 一致。
     * 仅支持矩形 ROI（regions + rectRoiIds 平行）；多边形 ROI 不参与。
     */
    virtual void startMicroDiffAnalysis(const QString &videoPath,
                                        const QVector<QRect> &regions,
                                        const QVector<int> &rectRoiIds,
                                        const MicroDiffCurveParams &params);

signals:
    void progressUpdated(int analyzed, int total, qreal percent);
    void analysisFinished(const AnalysisSnapshot &result);
    void analysisFailed(const QString &error);
};

// --- default (no-capability) implementations -------------------------------
inline IAnalysisEngine::VideoTiming IAnalysisEngine::videoTiming(const QString &)
{
    return {};
}

inline qint64 IAnalysisEngine::trustedDurationMs(const QString &videoPath)
{
    const VideoTiming t = videoTiming(videoPath);
    return t.durationMs;
}

inline void IAnalysisEngine::startAudioAnalysis(const QString &)
{
    emit analysisFailed(QObject::tr("当前分析引擎不支持音频分析"));
}

inline void IAnalysisEngine::setAudioDenoiseStrength(double)
{
}

inline void IAnalysisEngine::startMicroDiffAnalysis(const QString &,
                                                     const QVector<QRect> &,
                                                     const QVector<int> &,
                                                     const MicroDiffCurveParams &)
{
    emit analysisFailed(QObject::tr("当前分析引擎不支持微变变化率曲线分析"));
}
