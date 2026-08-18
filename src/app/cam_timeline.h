/**
 * @file cam_timeline.h
 * @brief 多机时间线对齐数据装配（app 层，v1.3.0 M3 任务14）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计来源：docs/DEVELOPMENT_PLAN_V1.3_CN.md §3-M3 任务14。
 * 逐已校时视频读案内 .vla（校时 SSOT，R5/R6：禁止各处自行解析 case.json
 * 之外的重复真源——校时只信 .vla）→ wallMsOf(0)/wallMsOf(maxStream) 得
 * 墙钟块位；视图层（multicamview）只消费本结构，不碰 IO。
 */
#pragma once

#include <QString>
#include <QVector>
#include "domain/case_model.h"
#include "domain/sync_model.h"

class CaseManager;

/// 单路机位时间线条目（墙钟块位 [wallStartMs, wallEndMs]）
struct CamLane {
    QString videoId;
    QString fileName;
    qint64 wallStartMs = 0;       ///< 块位起点（校时换算的墙钟 epoch 毫秒）
    qint64 wallEndMs = 0;         ///< 块位终点（已分析最大流内时刻的墙钟）
    qint64 streamDurationMs = 0;  ///< .vla 已分析时长（块位宽度的数据源）
    QString calibrationSummary;   ///< 校时徽标缓存文案（tooltip 展示）
};

/// 装配案件多机时间线：跳过未校时/.vla 缺失/无分析数据的视频；
/// 返回按墙钟起点升序的条目集。
QVector<CamLane> buildCamLanes(const QString &caseDir,
                               const QVector<CaseVideoRef> &videos);

/// 装配多机同步播放路数据（P-57）：仅收**已校时**路（Q4：临时进路由
/// 窗口层另行加入，本函数只读案内 .vla 校时 SSOT）。与 buildCamLanes
/// 不同：不要求有分析数据（未跑亮度分析的已校时视频同样可播放），
/// 时长字段留 0（真实时长由引擎加载后回报，R4：app 层不碰探测实现）。
/// displayName 优先机位标签（cameraLabel），缺省文件名。按墙钟起点升序。
QVector<SyncLaneData> buildSyncLanesFromCase(const CaseManager &cm);
