/**
 * @file timeline_model.h
 * @brief 线程安全时间序列模型 + .vla 文件序列化
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include <QObject>
#include <QReadWriteLock>
#include <QImage>
#include "analysis_snapshot.h"

/**
 * @brief Snapshot fusion parameters for VLA persistence.
 */
struct SnapshotFusionData {
    int brightness = 0;
    int contrast = 0;
    int opacity = 0;
    QImage imageData;  // full-frame snapshot image
    bool operator==(const SnapshotFusionData &o) const {
        return brightness == o.brightness && contrast == o.contrast && opacity == o.opacity;
    }
    bool isValid() const { return brightness != 0 || contrast != 0 || opacity != 0 || !imageData.isNull(); }
};

/**
 * @brief Thread-safe model for storing luminance time-series data.
 *
 * Replaces the data-storage half of the old AnalyzerCore.
 * Emits dataReplaced() with an immutable snapshot for bulk updates.
 * Supports serialization to/from .vla (Video Luminance Analysis) format.
 */
class TimelineModel : public QObject
{
    Q_OBJECT

public:
    explicit TimelineModel(QObject *parent = nullptr);

    /// Replace all data at once (efficient, no per-point locking).
    void setData(QVector<qint64> timestamps, QVector<QVector<qreal>> values);

    void clearData();

    /// Returns a copy of the current snapshot (cheap thanks to implicit sharing).
    AnalysisSnapshot snapshot() const;

    /// Serialize current snapshot to .vla JSON file (v3 format).
    /// Saves timestamps, luminance data, ROIs, time offset, magnifier, labels, pinned, snapshot fusion.
    bool saveToFile(const QString &filePath, const QVector<QRect> &regions,
                    qint64 timeOffsetMs = 0, const QRect &magnifier = QRect(),
                    const QVector<ChartLabel> &labels = {},
                    const QRect &pinned = QRect(),
                    const SnapshotFusionData &snapshotFusion = SnapshotFusionData()) const;

    /// Deserialize snapshot from .vla JSON file. Emits dataReplaced() on success.
    /// Output params restore ROI, time offset, magnifier, labels, pinned, snapshot fusion.
    bool loadFromFile(const QString &filePath,
                      QVector<QRect> *regions = nullptr,
                      qint64 *timeOffsetMs = nullptr,
                      QRect *magnifier = nullptr,
                      QVector<ChartLabel> *labels = nullptr,
                      QRect *pinned = nullptr,
                      SnapshotFusionData *snapshotFusion = nullptr);

signals:
    void dataReplaced();
    void dataCleared();

private:
    mutable QReadWriteLock m_lock;
    AnalysisSnapshot m_snapshot;
};
