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
#include "infrastructure/tool_paths.h"
#include <QProcess>

#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include "case_manager.h"
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

QVector<CamInventoryItem> buildCamInventory(const CaseManager &cm)
{
    QVector<CamInventoryItem> out;
    const QDir dir(cm.caseDir());
    auto addRef = [&](const CaseVideoRef &v, bool fromPreprocess) {
        CamInventoryItem it;
        it.id = v.id;
        it.fromPreprocess = fromPreprocess;
        it.path = cm.effectivePathFor(v);
        it.pathExists = !it.path.isEmpty() && QFile::exists(it.path);
        it.displayName = v.cameraLabel.isEmpty()
            ? QFileInfo(v.originalPath).fileName() : v.cameraLabel;
        // 校时实读案内 .vla（SSOT，R5；不看徽标缓存）。已校时路顺手填妥
        // SyncLaneData（勾选即可入列，避免开始后二次读盘）。
        // 路径分流与 CaseManager::vlaPathFor 同语义：登记路径优先；
        // vlaRelPath 空（旧数据产物）回落源旁 .vla
        QString vlaPath;
        if (!v.vlaRelPath.isEmpty())
            vlaPath = dir.absoluteFilePath(v.vlaRelPath);
        else if (it.pathExists)
            vlaPath = it.path + QStringLiteral(".vla");
        if (!vlaPath.isEmpty() && QFile::exists(vlaPath)) {
            TimelineModel model;
            TimeCalibration cal;
            if (model.loadFromFile(vlaPath, nullptr, &cal)
                && cal.isValid() && cal.isEffective()) {
                it.calibrated = true;
                // P-69：段时长首选源 = .vla 已分析最大流内时刻（免 ffprobe）
                const auto snap = model.snapshot();
                if (!snap.isEmpty())
                    it.analyzedDurationMs = snap.timestamps.last();
                it.lane.id = v.id;
                it.lane.path = it.path;
                it.lane.displayName = it.displayName;
                it.lane.calibrated = true;
                it.lane.temporary = false;
                it.lane.cal = cal;
                it.lane.durationMs = 0;  // 真实时长由引擎加载后回报（R4）
            }
        }
        // v1.12.3（越秀案实测）：前处理产物的分段校时先在 sidecar
        // （.lumencal.json），.vla 可能尚未落盘/已丢失——.vla 无效时回落
        // sidecar，产物勾选面板不再误报「未校时」（与主窗打开继承同口径）
        if (!it.calibrated && it.pathExists) {
            TimeCalibration cal;
            if (loadSidecarCalibration(it.path, &cal, nullptr)
                && cal.isValid() && cal.isEffective()) {
                it.calibrated = true;
                it.lane.id = v.id;
                it.lane.path = it.path;
                it.lane.displayName = it.displayName;
                it.lane.calibrated = true;
                it.lane.temporary = false;
                it.lane.cal = cal;
                it.lane.durationMs = 0;
            }
        }
        out.append(it);
    };
    for (const auto &v : cm.meta().videos)
        addRef(v, false);
    for (const auto &s : cm.meta().preprocessSessions)
        for (const auto &o : s.outputRefs)
            addRef(o, true);
    return out;
}

// ---------------------------------------------------------------------------
// P-69 编号合并轨：分组与段时长预读
// ---------------------------------------------------------------------------
// buildMergedGroups：纯函数已内联于 cam_timeline.h（sync_test 直编验证）

qint64 probeMediaDurationMs(const QString &path)
{
    const QString ffprobe = ToolPaths::findFfprobePath();
    if (ffprobe.isEmpty())
        return 0;
    QProcess proc;
    proc.start(ffprobe, {QStringLiteral("-v"), QStringLiteral("error"),
                         QStringLiteral("-show_entries"),
                         QStringLiteral("format=duration"),
                         QStringLiteral("-of"),
                         QStringLiteral("default=noprint_wrappers=1:nokey=1"),
                         path});
    if (!proc.waitForFinished(10000))
        return 0;   // C1：超时按未知处理（调用方回落）
    bool ok = false;
    const double sec = QString::fromUtf8(proc.readAllStandardOutput())
                           .trimmed().toDouble(&ok);
    return ok ? qint64(sec * 1000.0 + 0.5) : 0;
}
