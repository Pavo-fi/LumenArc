#include "videostatemanager.h"
#include <QDir>

VideoStateManager::VideoStateManager(QObject *parent)
    : QObject(parent)
{
}

void VideoStateManager::saveState(const QString &videoPath,
                                   const AnalysisSnapshot &snapshot,
                                   const QVector<QRect> &regions,
                                   const TimeCalibration &calibration,
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
                                   const QVector<int> &polygonRoiIds,
                                   const DisplayAdjust &display,
                                   int displayRotation)
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
    state.calibration = calibration;
    state.magnifierRect = magnifierRect;
    state.labels = labels;
    state.pinnedRect = pinnedRect;
    state.snapshotFusion = snapshotFusion;
    state.abPointA = abPointA;
    state.abPointB = abPointB;
    state.abLoop = abLoop;
    state.display = display;
    state.displayRotation = displayRotation;
    
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

bool VideoStateManager::magnifierRectOf(const QString &videoPath, QRect &out) const
{
    auto it = m_states.constFind(videoPath);
    if (it == m_states.constEnd())
        return false;
    out = it.value().magnifierRect;
    return out.isValid();
}

void VideoStateManager::removeState(const QString &videoPath)
{
    if (videoPath.isEmpty())
        return;
    m_states.remove(videoPath);
    // v1.18.x：键同一性归一（reviewer 2026-09-26）——saveState 存的是 openVideoFile
    // 收到的那串路径，而调用方可能拿到另一种写法（正/反斜杠、目录大小写）。
    // Windows 路径不区分大小写，用 cleanPath + CaseInsensitive 扫一遍抹干净。
    const QString norm = QDir::cleanPath(videoPath);
    for (auto it = m_states.begin(); it != m_states.end();) {
        if (QString::compare(QDir::cleanPath(it.key()), norm,
                             Qt::CaseInsensitive) == 0)
            it = m_states.erase(it);
        else
            ++it;
    }
}

void VideoStateManager::migrateKey(const QString &oldPath,
                                   const QString &newPath)
{
    auto it = m_states.find(oldPath);
    if (it == m_states.end())
        return;
    VideoState st = it.value();
    st.filePath = newPath;
    m_states.erase(it);
    m_states.insert(newPath, st);
}

void VideoStateManager::clear()
{
    m_states.clear();
}
