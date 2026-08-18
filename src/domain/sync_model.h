/**
 * @file sync_model.h
 * @brief 多机同步播放的纯数据模型与判定函数（P-57，domain 层，零 Widgets 依赖）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-18
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计来源：docs/MULTICAM_PLAYBACK_TECH_DESIGN_CN.md（v0.3 已拍板）。
 * 本头文件只含纯函数：墙钟↔流内映射、时间线模式判定、纠偏决策——
 * 供 app 层 MultiCamSyncService 消费，全部可脱离 UI 单测（C3 等级：
 * 坐标/时间轴换算按"错误数据可能流入取证报告"评审）。
 */
#pragma once

#include <QString>
#include <QVector>
#include <QtGlobal>
#include <limits>
#include "time_calibration.h"

/// 同步播放单路数据（校时路读 .vla 校时；临时路 = 会话级偏移，不落盘）
struct SyncLaneData
{
    QString id;                  ///< 案内 videoId；临时路 "T1"/"T2"...
    QString path;                ///< 播放绝对路径（引用制，源文件只读）
    QString displayName;         ///< 瓦片/OSD 显示名（机位标签或文件名）
    bool calibrated = false;     ///< 有真校时（.vla SSOT）
    bool temporary = false;      ///< 临时进路（会话级对齐）
    TimeCalibration cal;         ///< calibrated 时有效
    qint64 tempOffsetMs = 0;     ///< temporary：wallMs = streamMs + tempOffsetMs
    qint64 durationMs = 0;       ///< 真实时长（引擎加载后回报填充）
};

/// 流内毫秒 → 统一墙钟轴毫秒
inline qint64 syncWallOf(const SyncLaneData &l, qint64 streamMs)
{
    if (l.calibrated)
        return l.cal.wallMsOf(streamMs);
    return streamMs + l.tempOffsetMs;
}

/// 统一墙钟轴毫秒 → 该路流内毫秒（可能为负/超界，调用方用 syncLaneCovers 判缺口）
inline qint64 syncStreamOf(const SyncLaneData &l, qint64 wallMs)
{
    if (l.calibrated)
        return l.cal.streamMsOf(wallMs);
    return wallMs - l.tempOffsetMs;
}

/// 该路墙钟覆盖起点（流内 0 对应的墙钟）
inline qint64 syncLaneWallStart(const SyncLaneData &l)
{
    return syncWallOf(l, 0);
}

/// 该路墙钟覆盖终点（真实时长对应的墙钟；durationMs 未就绪时退化为起点）
inline qint64 syncLaneWallEnd(const SyncLaneData &l)
{
    return syncWallOf(l, qMax<qint64>(l.durationMs, 0));
}

/// 该路在 wallMs 是否有画面（流内 [0, duration] 内；duration 未就绪视为有）
inline bool syncLaneCovers(const SyncLaneData &l, qint64 wallMs)
{
    const qint64 s = syncStreamOf(l, wallMs);
    if (s < 0)
        return false;
    if (l.durationMs > 0 && s > l.durationMs)
        return false;
    return true;
}

/// 时间线模式（拍板 Q4/§3.2 判定逻辑）：
/// 2 路且恰含 1 路临时进 → Separate（分开进度条）；
/// 2-4 路全部已校时 → Merged（合并时间线）；其余组合为非法（返回分离兜底，
/// 调用方在装配层应已拦截）。
enum class SyncTimelineMode { Merged, Separate };

inline SyncTimelineMode decideSyncMode(int laneCount, int tempCount)
{
    if (laneCount == 2 && tempCount == 1)
        return SyncTimelineMode::Separate;
    if (laneCount >= 2 && laneCount <= 4 && tempCount == 0)
        return SyncTimelineMode::Merged;
    return SyncTimelineMode::Separate;   // 兜底（不应到达）
}

/// 纠偏决策（§8 R-2 防抖）：偏差超阈且持续增长才 seek。
/// prevErrMs 传 INT64_MIN 表示首次采样（超阈即纠）。
/// 返回 true 时 targetOut = 应 seek 到的流内毫秒。
inline bool decideDriftCorrection(qint64 posMs, qint64 targetMs,
                                  qint64 prevErrMs, qint64 thresholdMs,
                                  qint64 *targetOut = nullptr)
{
    const qint64 err = posMs - targetMs;
    if (qAbs(err) <= thresholdMs)
        return false;
    if (prevErrMs != std::numeric_limits<qint64>::min()
        && qAbs(err) < qAbs(prevErrMs))
        return false;   // 偏差在收敛，引擎自由运行追赶中
    if (targetOut)
        *targetOut = targetMs;
    return true;
}
