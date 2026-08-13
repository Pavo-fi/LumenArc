/**
 * @file cam_timeline.cpp
 * @brief 多机时间线对齐数据装配实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "cam_timeline.h"

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "domain/timeline_model.h"
#include "domain/time_calibration.h"

QVector<CamLane> buildCamLanes(const QString &caseDir,
                               const QVector<CaseVideoRef> &videos)
{
    QVector<CamLane> out;
    const QDir dir(caseDir);
    for (const auto &v : videos) {
        if (v.vlaRelPath.isEmpty())
            continue;
        const QString vlaPath = dir.absoluteFilePath(v.vlaRelPath);
        if (!QFile::exists(vlaPath))
            continue;
        TimelineModel model;
        TimeCalibration cal;
        if (!model.loadFromFile(vlaPath, nullptr, &cal) || !cal.isValid())
            continue;
        const auto snap = model.snapshot();
        if (snap.isEmpty())
            continue;
        const qint64 maxStream = snap.timestamps.last();
        CamLane lane;
        lane.videoId = v.id;
        lane.fileName = QFileInfo(v.originalPath).fileName();
        lane.streamDurationMs = maxStream;
        lane.wallStartMs = cal.wallMsOf(0);
        lane.wallEndMs = cal.wallMsOf(maxStream);
        // 防御：rate 物理上恒正，异常数据致终点早于起点时交换
        if (lane.wallEndMs < lane.wallStartMs)
            std::swap(lane.wallStartMs, lane.wallEndMs);
        lane.calibrationSummary = v.calibrationSummary;
        out.append(lane);
    }
    std::sort(out.begin(), out.end(),
              [](const CamLane &a, const CamLane &b) {
                  return a.wallStartMs < b.wallStartMs;
              });
    return out;
}
