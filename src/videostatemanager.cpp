#include "videostatemanager.h"

VideoStateManager::VideoStateManager(QObject *parent)
    : QObject(parent)
{
}

void VideoStateManager::saveState(const QString &videoPath,
                                   const AnalysisSnapshot &snapshot,
                                   const QVector<QRect> &regions,
                                   qint64 timeOffsetMs,
                                   const QRect &magnifierRect,
                                   const QVector<ChartLabel> &labels,
                                   const QRect &pinnedRect,
                                   const SnapshotFusionData &snapshotFusion,
                                   qint64 abPointA,
                                   qint64 abPointB,
                                   bool abLoop,
                                   const QVector<QPolygon> &polygons,
                                   const QVector<GuideLine> &guideLines,
                                   const QVector<ChartGuideData> &chartGuideLines,
                                   const QVector<int> &regionRoiIds,
                                   const QVector<int> &polygonRoiIds)
{
    if (videoPath.isEmpty())
        return;
    
    VideoState state;
    state.filePath = videoPath;
    state.snapshot = snapshot;
    state.regions = regions;
    state.regionRoiIds = regionRoiIds;
    state.polygons = polygons;
    state.polygonRoiIds = polygonRoiIds;
    state.guideLines = guideLines;
    state.chartGuideLines = chartGuideLines;
    state.timeOffsetMs = timeOffsetMs;
    state.magnifierRect = magnifierRect;
    state.labels = labels;
    state.pinnedRect = pinnedRect;
    state.snapshotFusion = snapshotFusion;
    state.abPointA = abPointA;
    state.abPointB = abPointB;
    state.abLoop = abLoop;
    
    m_states[videoPath] = state;
}

bool VideoStateManager::restoreState(const QString &videoPath, VideoState &state) const
{
    auto it = m_states.constFind(videoPath);
    if (it == m_states.constEnd())
        return false;
    
    state = it.value();
    return state.hasData();
}

bool VideoStateManager::hasState(const QString &videoPath) const
{
    return m_states.contains(videoPath);
}

void VideoStateManager::removeState(const QString &videoPath)
{
    m_states.remove(videoPath);
}

void VideoStateManager::clear()
{
    m_states.clear();
}
