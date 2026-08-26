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

/// P-69 合并轨段：同编号（机位标签）多文件并成一路后，每段=一个文件
/// + 自己的校时（.vla SSOT）+ 真实时长。段在墙钟轴上的区间由 cal 派生。
struct SyncSegment
{
    QString path;                ///< 段文件绝对路径（引用制，源文件只读）
    QString srcId;               ///< 来源条目 id（V###/P###，溯源/报告用）
    TimeCalibration cal;         ///< 该段校时（calibrated 才允许入合并轨）
    qint64 durationMs = 0;       ///< 段真实时长（.vla 已分析值/ffprobe/引擎回报）

    qint64 wallStartMs() const { return cal.wallMsOf(0) + cal.truthOffsetMs; }
    qint64 wallEndMs() const {
        return cal.wallMsOf(durationMs) + cal.truthOffsetMs;
    }
    bool coversWall(qint64 wallMs) const {
        return wallMs >= wallStartMs() && wallMs < wallEndMs();
    }
};

/// 同步播放单路数据（校时路读 .vla 校时；临时路 = 会话级偏移，不落盘）
struct SyncLaneData
{
    QString id;                  ///< 案内 videoId；临时路 "T1"/"T2"...；合并轨 "M_"+标签
    QString path;                ///< 播放绝对路径（引用制，源文件只读）；合并轨=首段路径
    QString displayName;         ///< 瓦片/OSD 显示名（机位标签或文件名）
    bool calibrated = false;     ///< 有真校时（.vla SSOT）
    bool temporary = false;      ///< 临时进路（会话级对齐）
    TimeCalibration cal;         ///< calibrated 时有效（合并轨不用，段各自持 cal）
    qint64 tempOffsetMs = 0;     ///< temporary：wallMs = streamMs + tempOffsetMs
    qint64 durationMs = 0;       ///< 真实时长（引擎加载后回报填充）；合并轨=Σ段时长
    QVector<SyncSegment> segments;  ///< P-69：非空 = 合并轨（虚拟流内轴=各段首尾相接）

    bool isMerged() const { return !segments.isEmpty(); }
    /// 合并轨段虚拟轴前缀和：段 k 占 [cum[k], cum[k]+dur[k])
    QVector<qint64> segCumDurations() const {
        QVector<qint64> cum;
        cum.reserve(segments.size());
        qint64 acc = 0;
        for (const auto &s : segments) {
            cum.append(acc);
            acc += qMax<qint64>(0, s.durationMs);
        }
        return cum;
    }
};

/// v1.16.0：本路是否已完成「北京时间对时」（直接照片/OCR 或经同事件对时
/// 从已校真路间接获得——落路后 cal.truthOffsetMs 非 0）。瓦片/时间线挂
/// 「已对时」明显标识用。合并轨：任一段有真即视为有（段各自持 cal）。
inline bool syncLaneHasTruth(const SyncLaneData &l)
{
    if (l.isMerged()) {
        for (const auto &s : l.segments)
            if (s.cal.truthOffsetMs != 0)
                return true;
        return false;
    }
    return l.cal.truthOffsetMs != 0;
}

/// P-69 合并轨：墙钟命中的段号（重叠 → 「先起步者赢」= wallStart 最小者；
/// 同起步取段号小者）；无覆盖返回 -1
inline int syncSegmentAt(const SyncLaneData &l, qint64 wallMs)
{
    int best = -1;
    for (int k = 0; k < l.segments.size(); ++k) {
        if (!l.segments[k].coversWall(wallMs))
            continue;
        if (best < 0
            || l.segments[k].wallStartMs() < l.segments[best].wallStartMs())
            best = k;
    }
    return best;
}

/// P-69 合并轨：墙钟 → 虚拟流内毫秒（无覆盖时取最近段外推，调用方以
/// syncLaneCovers 判缺口后再用）
inline qint64 syncMergedStreamOf(const SyncLaneData &l, qint64 wallMs)
{
    const QVector<qint64> cum = l.segCumDurations();
    int k = syncSegmentAt(l, wallMs);
    if (k < 0) {
        // 最近段外推（缺口内游标语义：钉在最近段端点，画面静止由覆盖判定兜底）
        qint64 bestDist = std::numeric_limits<qint64>::max();
        for (int i = 0; i < l.segments.size(); ++i) {
            const qint64 d = (wallMs < l.segments[i].wallStartMs())
                ? l.segments[i].wallStartMs() - wallMs
                : wallMs - l.segments[i].wallEndMs();
            if (d < bestDist) {
                bestDist = d;
                k = i;
            }
        }
        if (k < 0)
            return 0;
        const qint64 real = qBound<qint64>(
            0, l.segments[k].cal.streamMsOf(wallMs - l.segments[k].cal.truthOffsetMs),
            qMax<qint64>(0, l.segments[k].durationMs));
        return cum[k] + real;
    }
    const auto &seg = l.segments[k];
    const qint64 real = qBound<qint64>(
        0, seg.cal.streamMsOf(wallMs - seg.cal.truthOffsetMs),
        qMax<qint64>(0, seg.durationMs));
    return cum[k] + real;
}

/// P-69 合并轨：虚拟流内毫秒 → (段号, 段内真实流内毫秒)
inline QPair<int, qint64> syncMergedSegmentOf(const SyncLaneData &l, qint64 virtMs)
{
    const QVector<qint64> cum = l.segCumDurations();
    for (int k = l.segments.size() - 1; k >= 0; --k) {
        if (virtMs >= cum[k])
            return {k, virtMs - cum[k]};
    }
    return {0, 0};
}

/// 流内毫秒 → 统一墙钟轴毫秒
/// v1.12.7：该路做过北京时间校对（truthSet）时，墙钟轴取北京时间口径
/// （wallMsOf + truthOffsetMs，与单路分析/图表/CSV 的 beijingMsOf 同义）——
/// 多机合并时间线与瓦片 OSD 此前漏用 truth 偏移（用户实测反馈）。
inline qint64 syncWallOf(const SyncLaneData &l, qint64 streamMs)
{
    if (l.isMerged()) {
        // 合并轨：虚拟轴 → (段, 段内真实流内) → 段校时换算
        const auto pr = syncMergedSegmentOf(l, streamMs);
        const auto &seg = l.segments[pr.first];
        return seg.cal.wallMsOf(pr.second) + seg.cal.truthOffsetMs;
    }
    if (l.calibrated)
        return l.cal.wallMsOf(streamMs) + l.cal.truthOffsetMs;
    return streamMs + l.tempOffsetMs;
}

/// 统一墙钟轴毫秒 → 该路流内毫秒（可能为负/超界，调用方用 syncLaneCovers 判缺口）
inline qint64 syncStreamOf(const SyncLaneData &l, qint64 wallMs)
{
    if (l.isMerged())
        return syncMergedStreamOf(l, wallMs);
    if (l.calibrated)
        return l.cal.streamMsOf(wallMs - l.cal.truthOffsetMs);
    return wallMs - l.tempOffsetMs;
}

/// 该路墙钟覆盖起点（流内 0 对应的墙钟；合并轨=最早段起点）
inline qint64 syncLaneWallStart(const SyncLaneData &l)
{
    if (l.isMerged()) {
        qint64 t0 = std::numeric_limits<qint64>::max();
        for (const auto &s : l.segments)
            t0 = qMin(t0, s.wallStartMs());
        return l.segments.isEmpty() ? 0 : t0;
    }
    return syncWallOf(l, 0);
}

/// 该路墙钟覆盖终点（真实时长对应的墙钟；durationMs 未就绪时退化为起点；
/// 合并轨=最晚段终点）
inline qint64 syncLaneWallEnd(const SyncLaneData &l)
{
    if (l.isMerged()) {
        qint64 t1 = 0;
        for (const auto &s : l.segments)
            t1 = qMax(t1, s.wallEndMs());
        return t1;
    }
    return syncWallOf(l, qMax<qint64>(l.durationMs, 0));
}

/// 该路在 wallMs 是否有画面（流内 [0, duration] 内；duration 未就绪视为有；
/// 合并轨=任一段覆盖即真，重叠由「先起步者赢」在取帧层解决）
inline bool syncLaneCovers(const SyncLaneData &l, qint64 wallMs)
{
    if (l.isMerged())
        return syncSegmentAt(l, wallMs) >= 0;
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
