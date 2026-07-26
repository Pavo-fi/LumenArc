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

signals:
    void progressUpdated(int analyzed, int total, qreal percent);
    void analysisFinished(const AnalysisSnapshot &result);
    void analysisFailed(const QString &error);
};
