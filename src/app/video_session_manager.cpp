/**
 * @file video_session_manager.cpp
 * @brief 视频会话管理实现（P-31 T2-A；决策逻辑自 MainWindow 纯移动）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-17
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "video_session_manager.h"
#include "case_manager.h"
#include "calibration_service.h"
#include <QFile>

VideoSessionManager::VideoSessionManager(QObject *parent)
    : QObject(parent), m_states(new VideoStateManager(this))
{
}

void VideoSessionManager::saveCurrentState(const QString &videoPath,
                                           const VideoState &state)
{
    if (videoPath.isEmpty())
        return;
    m_states->saveState(videoPath, state.snapshot, state.regions, state.calibration,
                        state.magnifierRect, state.labels, state.pinnedRect,
                        state.snapshotFusion, state.abPointA, state.abPointB,
                        state.abLoop, state.polygons, state.guideLines,
                        state.chartGuideLines, state.regionRoiIds, state.polygonRoiIds,
                        state.display, state.displayRotation);
}

bool VideoSessionManager::hasState(const QString &videoPath) const
{
    return m_states->hasState(videoPath);
}

void VideoSessionManager::removeState(const QString &videoPath)
{
    m_states->removeState(videoPath);
}

void VideoSessionManager::migrateKey(const QString &oldPath, const QString &newPath)
{
    m_states->migrateKey(oldPath, newPath);
}

void VideoSessionManager::clear()
{
    m_states->clear();
}

VideoSessionManager::OpenPlan VideoSessionManager::planOpen(const QString &videoPath,
                                                            CaseManager *cases) const
{
    OpenPlan plan;
    // 内存现场优先（openVideoFile 既有顺序：先 restoreState 后探缓存）
    plan.hasMemoryState = m_states->restoreState(videoPath, plan.memoryState);
    // v1.3.0 路径分流：入案视频缓存 = 案件 videos/V###.vla；未入案照旧源旁
    const QString vlaPath = cases ? cases->vlaPathFor(videoPath) : QString();
    if (QFile::exists(vlaPath)) {
        plan.cacheVlaPath = vlaPath;
        plan.cacheIsCaseVideo = cases && cases->isCaseVideo(videoPath);
    }
    return plan;
}

bool VideoSessionManager::loadSidecarCalibration(const QString &videoPath,
                                                 TimeCalibration *outCal,
                                                 QString *outWarning)
{
    return CalibrationService::loadSidecar(videoPath, outCal, outWarning);
}
