#pragma once

#include <QObject>
#include <QMap>
#include <QVector>
#include <QRect>
#include <QPolygon>
#include <QString>
#include "domain/analysis_snapshot.h"
#include "domain/timeline_model.h"
#include "domain/guide_line.h"
#include "domain/time_calibration.h"

struct VideoState {
    QString filePath;
    AnalysisSnapshot snapshot;
    QVector<QRect> regions;
    QVector<int> regionRoiIds;      // 与 regions 一一对应（保持分析数据 roiId 对齐）
    QVector<QPolygon> polygons;
    QVector<int> polygonRoiIds;     // 与 polygons 一一对应
    QVector<GuideLine> guideLines;
    QVector<ChartGuideData> chartGuideLines;   // 图表辅助线（逐视频，防跨视频泄漏）
    TimeCalibration calibration;    // 校时模型（SSOT，仿射+北京时间偏移）
    QRect magnifierRect;
    QVector<ChartLabel> labels;
    QRect pinnedRect;
    SnapshotFusionData snapshotFusion;
    qint64 abPointA = -1;
    qint64 abPointB = -1;
    bool abLoop = false;
    int displayBrightness = 0;   ///< 播放画面调节（2026-08-14，逐视频记忆）
    int displayContrast = 0;
    int displayRotation = 0;     ///< 显示旋转档位（0/90/180/270，Q1 方案 A）
    
    bool hasData() const {
        return !snapshot.isEmpty() || !regions.isEmpty() || !polygons.isEmpty() || snapshot.hasAudio()
               || !guideLines.isEmpty() || !chartGuideLines.isEmpty() || !labels.isEmpty() || snapshotFusion.isValid()
               || abPointA >= 0 || abPointB >= 0 || calibration.isValid()
               || displayBrightness != 0 || displayContrast != 0 || displayRotation != 0;
    }
};

class VideoStateManager : public QObject
{
    Q_OBJECT

public:
    explicit VideoStateManager(QObject *parent = nullptr);

    void saveState(const QString &videoPath,
                   const AnalysisSnapshot &snapshot,
                   const QVector<QRect> &regions,
                   const TimeCalibration &calibration,
                   const QRect &magnifierRect,
                   const QVector<ChartLabel> &labels,
                   const QRect &pinnedRect,
                   const SnapshotFusionData &snapshotFusion,
                   qint64 abPointA = -1,
                   qint64 abPointB = -1,
                   bool abLoop = false,
                   const QVector<QPolygon> &polygons = {},
                   const QVector<GuideLine> &guideLines = {},
                   const QVector<ChartGuideData> &chartGuideLines = {},
                   const QVector<int> &regionRoiIds = {},
                   const QVector<int> &polygonRoiIds = {},
                   int displayBrightness = 0,
                   int displayContrast = 0,
                   int displayRotation = 0);

    bool restoreState(const QString &videoPath, VideoState &state) const;
    bool hasState(const QString &videoPath) const;
    void removeState(const QString &videoPath);
    /// 键迁移（v1.3.0 M3 任务13：重定位后内存状态跟随新路径）
    void migrateKey(const QString &oldPath, const QString &newPath);
    void clear();

private:
    QMap<QString, VideoState> m_states;
};
