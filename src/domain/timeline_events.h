/**
 * @file timeline_events.h
 * @brief 报告时间轴装配：案件标签 + 案内快照 → 事件列表 + 阶段分段（纯逻辑，无头可测）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-30
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 用途：HTML 报告（ReportHtmlBuilder）的时间轴章节。事件有两个来源：
 *   ① ReportData::nodes —— 图表标签（用户分析时打的点，带墙钟与文字）；
 *   ② 案内 snapshots/ 快照 —— 时间编码在文件名里（见 snapshotWallMsFromName）。
 * 两者按墙钟 ±mergeWindowMs 归并：标签给标题、快照给画面。
 */
#pragma once

#include <QString>
#include <QVector>
#include <QPair>

#include "report_data.h"

/// 人工指定阶段的种子：从 wallMs 这个时刻（含）起进入名为 name 的阶段。
/// 一旦给了种子就不再用「间隔超阈值」的启发式——阶段划分本来就是主观的。
struct TimelinePhaseSeed {
    qint64  wallMs = 0;          ///< 阶段起始时刻（墙钟）
    QString name;                ///< 阶段名（空则回落「阶段N」）
};

/// 时间轴事件（渲染器只读）
struct TimelineEventItem {
    qint64  wallMs = 0;          ///< 墙钟（北京时间口径）
    QString title;               ///< 标题（标签文字 / 快照默认名）
    QString note;                ///< 说明（标签无说明时为空）
    QString sourceLabel;         ///< 来源机位
    QString source;              ///< snapshot | label
    QString imagePath;           ///< 画面绝对路径（空=无图）
    QString origName;            ///< 原始文件名（灯箱下载名）
};

/// 时间轴用的快照引用（调用方负责把机位名查好）
struct TimelineSnapshotRef {
    QString path;                ///< 快照绝对路径
    qint64  wallMs = -1;         ///< 墙钟；<= 0 = 无日期无法定位（不参与时间轴）
    QString sourceLabel;         ///< 来源机位（可空）
};

/// 阶段（人工种子优先；否则自动分段）
struct TimelinePhaseItem {
    QString id;                  ///< a / b / c …（与事件 phase 对应）
    QString name;                ///< 阶段一 / 阶段二 …
    QString shortName;           ///< 卡片角标用短名
    QString color;               ///< #rrggbb
    qint64  firstMs = 0;         ///< 本段首个事件时刻（渲染器自己格式化——
    qint64  lastMs = 0;          ///<   校时与否决定它是北京时间还是流内时刻）
    int     n = 0;               ///< 本段事件数
};

/// 装配结果
struct TimelineEvents {
    QVector<TimelineEventItem> events;   ///< 按墙钟升序，seq = 下标 + 1
    QVector<TimelinePhaseItem> phases;
    QVector<int> eventPhase;             ///< 与 events 等长：每事件所属阶段下标
    qint64 t0Ms = 0;                     ///< 基准时刻 = 首个事件墙钟
    qint64 totalSec = 0;                 ///< 基准 → 末事件的秒数

    bool isEmpty() const { return events.isEmpty(); }
};

/// 快照文件名 → 墙钟毫秒。识别 `<base>_yyyyMMdd_HHmmss[_n].ext`；
/// 未校时快照是 `<base>_tHH-MM-SS[_n].ext`（无日期可定位）→ 返回 -1。
qint64 snapshotWallMsFromName(const QString &fileName);

/// 装配事件与阶段。snapshots 为 (路径, 墙钟, 机位)；wallMs <= 0 的项不参与时间轴
/// （仍应出现在报告的附件快照表里，由调用方另行处理）。
/// mergeWindowMs：标签与快照归并窗口；phaseGapMs：阶段切分阈值（间隔超过阈值才算候选切点）；
/// maxPhases：阶段数上限——候选切点多于上限时，只取间隔最大的几个（避免长案件切出几十段）。
TimelineEvents buildTimelineEvents(const QVector<ReportNodeRow> &nodes,
                                   const QVector<TimelineSnapshotRef> &snapshots,
                                   qint64 mergeWindowMs = 1000,
                                   qint64 phaseGapMs = 20000,
                                   int maxPhases = 6,
                                   const QVector<TimelinePhaseSeed> &phaseSeeds = {});
