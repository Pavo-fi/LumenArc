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

/// 机位清单条目（P-59 机位勾选面板）：案件库存全量（视频 + 前处理产物）
struct CamInventoryItem {
    QString id;                   ///< V###（视频）/ P###（前处理产物）
    QString displayName;          ///< 机位标签（cameraLabel），缺省文件名
    QString path;                 ///< 有效绝对路径（引用制，源文件只读）
    bool fromPreprocess = false;  ///< 前处理会话产物（拼接/转码输出）
    bool pathExists = false;      ///< 源文件在盘（缺失路禁选）
    bool calibrated = false;      ///< 已校时（.vla 实读 SSOT，R5——不看徽标缓存）
    SyncLaneData lane;            ///< calibrated 时已填妥，勾选即可入列
};

/// 装配机位勾选清单（P-59）：videos[] + preprocessSessions[].outputRefs[]
/// 全量登记（顺序：视频按入案序，随后前处理产物按会话序）；校时状态逐路
/// 实读案内 .vla（cal.isValid && isEffective）；displayName 优先机位标签。
QVector<CamInventoryItem> buildCamInventory(const CaseManager &cm);
