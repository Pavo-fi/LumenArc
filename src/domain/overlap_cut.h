/**
 * @file overlap_cut.h
 * @brief 重叠段剪切计划（v1.7.0 M2，Q-17 拍板：剪后一段开头保前段完整）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-15
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 纯逻辑 domain（无 Qt Widgets 依赖，headless 单测）。
 * 输入：排序后的墙钟段列表（Sorting 产物）；输出：每文件流内保留区间。
 * 语义：相邻两段墙钟重叠时，后一段开头被剪（保前段完整，Q-17）；
 * 完全包含 → 后段整体丢弃；链式重叠逐段递推。
 */
#pragma once

#include <QString>
#include <QVector>

/// 排序后的一段（墙钟时间线）
struct WallSegment {
    QString file;          // 源文件（唯一键）
    qint64 wallStartMs = 0; // 墙钟起
    qint64 wallEndMs = 0;   // 墙钟止
    qint64 streamMs = 0;    // 流内时长（转码/拼接用）
};

/// 剪切计划：每文件流内保留区间
struct CutPlan {
    QString file;
    qint64 keepStartMs = 0;  // 流内保留起点（被剪时 >0）
    qint64 keepEndMs = 0;    // 流内保留终点（<=0 = 到文件尾）
    bool trimmed = false;    // 是否发生剪切
    bool dropped = false;    // 完全重叠被丢弃（不参与转码/拼接）
};

/// 生成剪切计划（输入须已按墙钟起排序）。
QVector<CutPlan> planOverlapCuts(const QVector<WallSegment> &segs);

/// 自动命名（v1.7.0 M4，Q4 拍板）：
/// `<通道>_<yyyyMMdd>_<HHmmss>-<HHmmss>.mp4`；通道缺失用组名；
/// 墙钟不可用（<=0）回退 `<通道>_<源文件名>.mp4`。
QString autoOutputName(const QString &channel, const QString &fallbackName,
                       qint64 wallStartMs, qint64 wallEndMs);
