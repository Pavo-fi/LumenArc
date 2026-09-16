/**
 * @file microdiff_curve.h
 * @brief 微变变化率曲线纯计算（domain）：逐帧 ROI 统计 / 逐秒聚合 / 首帧微变判定
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-10
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 纯数据与算子，无 QObject / 无 libav / 无 Widgets（R1/R2/R3）：
 * 解码循环由 infrastructure（LibavAnalysisEngine）编排，本文件只收"已算好的 D"。
 *
 * 单位与语义（与微变显示标定值一致，未加显示增益）：
 *   D  = 当前帧灰度 − 干净段基准（int16，[-255, 255]）
 *   medD   = ROI 内 |D| 中位数（灰度级）
 *   blkMax = 8×8 分块（不足 8px 的边并入邻块）的 块均|D| 最大值
 *   signedD = ROI 内 D（有符号）中位数：<0 烟挡光 / >0 火光增亮
 *
 * 判据三件套（标定自真实案件素材，实测记录见 HANDOVER.md §92）：
 *   blkMax > 干净段 μ+3σ + signedD 方向 + 连续 ≥3 秒过阈
 */
#pragma once

#include <QVector>

#include <cstdint>

#include "domain/analysis_snapshot.h"   // MicroDiffOnset

namespace microdiff {

/// @brief 单帧 ROI 微变统计
struct FrameStats
{
    double medD = 0;     ///< |D| 中位数（灰度级）
    double blkMax = 0;   ///< 8×8 分块 块均|D| 最大值
    double signedD = 0;  ///< D 有符号中位数（<0 烟挡光 / >0 火光增亮）
};

/// @brief 逐秒聚合行（该秒内逐帧统计的均值）
struct SecondRow
{
    qint64 tsMs = 0;     ///< 该秒起点（流内 ms）
    double medD = 0;
    double blkMax = 0;
    double signedD = 0;
};

/// @brief 判定结果（per ROI）
struct OnsetOutcome
{
    bool baselineOk = false;    ///< false = 干净段样本不足（阈值不可信，调用方应报错）
    int baselineSamples = 0;    ///< 干净段内逐秒样本数
    double mu = 0;              ///< 干净段 blkMax 均值
    double sigma = 0;           ///< 干净段 blkMax 标准差（总体）
    double threshold = 0;       ///< mu + 3*sigma
    MicroDiffOnset onset;       ///< onset.tsMs = -1 表示未检出
};

/**
 * @brief 单帧 ROI 微差统计（直方图法，O(W·H)，无分配）。
 *
 * @param d  当前帧与基准的差 D（行优先 w*h，值域 [-255, 255]）
 * @param w  ROI 宽（>0）
 * @param h  ROI 高（>0）
 *
 * 分块：nx = min(8, max(1, w/8))，ny 同理；块边界按 w/nx 整除铺满，
 * 与 Python 标定原型（np 8×8 网格）一致。
 */
FrameStats roiFrameStats(const int16_t *d, int w, int h);

/**
 * @brief 逐秒聚合：按 frameTsMs/1000 分组取均值（decode 序、ts 非降）。
 *
 * @param frames    逐帧统计（解码顺序）
 * @param frameTsMs 每帧流内 ms（与 frames 平行或更长；取两者最小长度）
 * @return 有帧的秒才出行；tsMs = 该秒起点（流内 ms）
 */
QVector<SecondRow> aggregateSeconds(const QVector<FrameStats> &frames,
                                    const QVector<qint64> &frameTsMs);

/**
 * @brief 首帧微变判定（per ROI）。
 *
 * 干净段 = [baseStartMs, baseStartMs + baseDurMs)；μ/σ 取该段内逐秒
 * blkMax 的均值/标准差（总体）；阈值 = μ + 3σ。
 * onset = **基准段结束之后**逐秒序列中首次 blkMax > 阈值且连续 ≥3 秒保持的秒
 * （基准段之前的超阈不计：那段时间用户并未标记为“干净”，跨光照/开关灯的
 * 全局漂移会误报为首帧微变）。
 * direction = 该秒 signedD 的符号（-1 烟挡光 / +1 火光增亮 / 0 未定）。
 *
 * @param rows             全片逐秒行（含干净段，不只干净段）
 * @param roiId            ROI id（写入 outcome.onset.roiId）
 * @param baseStartMs      干净段起点（流内 ms）
 * @param baseDurMs        干净段时长（ms，>0）
 * @param minBaselineSamples 干净段最少逐秒样本（低于则 baselineOk=false，默认 5）
 */
OnsetOutcome detectOnset(const QVector<SecondRow> &rows, int roiId,
                         qint64 baseStartMs, qint64 baseDurMs,
                         int minBaselineSamples = 5);

} // namespace microdiff