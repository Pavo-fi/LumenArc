/**
 * @file timeline_model.h
 * @brief 线程安全时间序列模型 + .vla 文件序列化
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include <QObject>
#include <QReadWriteLock>
#include <QImage>
#include <QPolygon>
#include "analysis_snapshot.h"
#include "guide_line.h"
#include "time_calibration.h"
#include "speed_plan.h"

/// A/B 选段区间（P-68 入 .vla，拍板 Q5）
struct AbRegionData {
    qint64 a = -1;
    qint64 b = -1;
    bool loop = false;
    bool isValid() const { return a >= 0 && b > a; }
};

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
    void setData(QVector<qint64> timestamps, QVector<QVector<qreal>> values,
                 const AudioData &audio = AudioData());

    /// Replace all data with metadata (for analysis results that include ROI IDs).
    void setData(QVector<qint64> timestamps, QVector<QVector<qreal>> values,
                 QVector<DataEntry> dataEntries, const AudioData &audio = AudioData());

    /// 整体替换（v1.8.0：合并策略/恢复现场用；QHash 隐式共享廉价）
    void setSnapshot(const AnalysisSnapshot &snapshot);

    void clearData();

    /// Clear only luminance data (timestamps + values), preserve audio data.
    /// Used when ROI regions change but audio analysis should be retained.
    void clearLuminanceData();

    /// Remove luminance data for a specific region index.
    /// Used when an ROI region is deleted to keep data/region indices in sync.
    void removeRegionData(int index);

    /// Remove luminance data for a specific ROI by its stable ID and type.
    /// Rect and polygon ROIs have independent ID spaces, so the type is
    /// required to avoid removing the wrong series when IDs collide.
    void removeRegionDataByRoiId(int roiId, DataEntry::Type type);

    /// Returns a copy of the current snapshot (cheap thanks to implicit sharing).
    AnalysisSnapshot snapshot() const;

    /// Serialize current snapshot to .vla JSON file (v8 format).
    /// Saves timestamps, luminance data, ROIs (rect+polygon+guide_lines), time calibration, magnifier, labels, pinned, snapshot fusion.
    bool saveToFile(const QString &filePath, const QVector<QRect> &regions,
                    const TimeCalibration &calibration, const QRect &magnifier = QRect(),
                    const QVector<ChartLabel> &labels = {},
                    const QRect &pinned = QRect(),
                    const SnapshotFusionData &snapshotFusion = SnapshotFusionData(),
                    const QVector<QPolygon> &polygons = {},
                    const QVector<GuideLine> &guideLines = {},
                    const QVector<int> &regionRoiIds = {},
                    const QVector<int> &polygonRoiIds = {},
                    const AbRegionData &abRegion = AbRegionData(),
                    const speedplan::SpeedPlan &speedPlan = speedplan::SpeedPlan()) const;

    /// Deserialize snapshot from .vla JSON file. Emits dataReplaced() on success.
    /// Output params restore ROI, time calibration, magnifier, labels, pinned, snapshot fusion, polygons, guide lines.
    bool loadFromFile(const QString &filePath,
                      QVector<QRect> *regions = nullptr,
                      TimeCalibration *calibration = nullptr,
                      QRect *magnifier = nullptr,
                      QVector<ChartLabel> *labels = nullptr,
                      QRect *pinned = nullptr,
                      SnapshotFusionData *snapshotFusion = nullptr,
                      QVector<QPolygon> *polygons = nullptr,
                      QVector<GuideLine> *guideLines = nullptr,
                      QVector<int> *regionRoiIds = nullptr,
                      QVector<int> *polygonRoiIds = nullptr,
                      AbRegionData *abRegion = nullptr,
                      speedplan::SpeedPlan *speedPlan = nullptr);

    /// Serialize spectrogram data to binary .vla.spec file.
    /// Format: [uint32 nFrames][uint32 nFreqBins][float32 sampleRate][uint32 hopLength][float32 data...]
    static bool saveSpecToFile(const QString &filePath, const AudioData &audio);

    /// Deserialize spectrogram data from binary .vla.spec file.
    static bool loadSpecFromFile(const QString &filePath, AudioData &audio);

    /// 轻量读取 vla 的校时模型（不加载谱图/ROI 等重数据）：
    /// VLA2 只解 META chunk；旧 JSON 格式只读顶层校时字段。
    /// 开案批量校时徽标校验用（用户实测：旧 vla time_offset=0 误亮 ⏰）。
    static TimeCalibration peekCalibrationFromVla(const QString &filePath);

signals:
    void dataReplaced();
    void dataCleared();

private:
    mutable QReadWriteLock m_lock;
    AnalysisSnapshot m_snapshot;
    /// 亮度行删除（锁内调用；行删空时清共享时间轴，同旧版行为）
    void removeLuminanceRowLocked(int index);
};
