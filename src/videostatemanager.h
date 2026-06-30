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

struct VideoState {
    QString filePath;
    AnalysisSnapshot snapshot;
    QVector<QRect> regions;
    QVector<QPolygon> polygons;
    QVector<GuideLine> guideLines;
    qint64 timeOffsetMs = 0;
    QRect magnifierRect;
    QVector<ChartLabel> labels;
    QRect pinnedRect;
    SnapshotFusionData snapshotFusion;
    qint64 abPointA = -1;
    qint64 abPointB = -1;
    bool abLoop = false;
    
    bool hasData() const { return !snapshot.isEmpty() || !regions.isEmpty() || !polygons.isEmpty() || snapshot.hasAudio(); }
};

class VideoStateManager : public QObject
{
    Q_OBJECT

public:
    explicit VideoStateManager(QObject *parent = nullptr);

    void saveState(const QString &videoPath,
                   const AnalysisSnapshot &snapshot,
                   const QVector<QRect> &regions,
                   qint64 timeOffsetMs,
                   const QRect &magnifierRect,
                   const QVector<ChartLabel> &labels,
                   const QRect &pinnedRect,
                   const SnapshotFusionData &snapshotFusion,
                   qint64 abPointA = -1,
                   qint64 abPointB = -1,
                   bool abLoop = false,
                   const QVector<QPolygon> &polygons = {},
                   const QVector<GuideLine> &guideLines = {});

    bool restoreState(const QString &videoPath, VideoState &state) const;
    bool hasState(const QString &videoPath) const;
    void removeState(const QString &videoPath);
    void clear();

private:
    QMap<QString, VideoState> m_states;
};
