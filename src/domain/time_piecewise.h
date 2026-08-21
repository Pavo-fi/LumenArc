/**
 * @file time_piecewise.h
 * @brief 分段时间映射（v1.2.1 时间重建）：变速/抽帧文件的"查表法"校时
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-08
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 背景：DVR 抽帧录像（待机低帧率/异动全帧率）导出时按固定帧率重打 PTS，
 * 导致"流内时间"被压缩（B3一单元客梯.mp4：流内 74min = OSD 103min）。
 * 单条仿射直线 wallMs = offset + rate×streamMs 在变速边界处失效。
 *
 * 本模块：分段线性模型（段内斜率恒定、边界处突变），配合"两级采样"
 * （粗采样分段 + 边界加密）实现整片时间重建。纯函数、无 UI 依赖。
 */
#pragma once

#include <QVector>
#include <QJsonObject>
#include <QJsonArray>
#include <QtGlobal>
#include <cmath>

/**
 * @brief 一个变速段：覆盖 [streamStartMs, 下一段 streamStartMs)，末段到 EOF。
 *
 * wallStartMs 是段首测点的 OSD 实测读数（永不修正）；rate 为段内最小二乘
 * 斜率（OSD 秒 / 流内秒）。
 */
struct TimeSegment
{
    qint64 streamStartMs = 0;
    qint64 wallStartMs = 0;
    double rate = 1.0;
};

/// 检测输入测点（与 TimeCalibration::Sample 字段对齐；独立结构避免循环依赖）
struct PiecewiseSample
{
    qint64 streamMs = -1;
    qint64 wallMs = 0;
    bool   used = true;
    double conf = 0.0;
};

/// 检测报告（进 .vla 留档 + UI 展示；domain 不放用户文案）
struct PiecewiseDetectReport
{
    int     segmentCount = 0;
    int     boundaryCount = 0;
    bool    speedVariant = false;   ///< 变速文件（有边界 或 整体 |rate−1| 超容差）
    double  overallRate = 1.0;      ///< 全片首尾斜率
    qint64  totalStreamSpanMs = 0;  ///< 全片流内跨度
    double  totalWallSpanSec = 0.0; ///< OSD 总跨度（秒）
    bool    audioConsistent = true; ///< 音频时长校验结论（未知时保持 true）
    bool    audioKnown = false;     ///< 是否取得音频时长做过校验
    int     outlierCount = 0;       ///< 剔除的 OCR 异常测点数（尖峰野点）
    QVector<int> outlierIdx;        ///< 异常测点在输入 samples 中的索引（留档/UI）
};

/// 时间缺口（拼接产物监控常态：缺段/关机/漏录；流内连续而墙钟跳变）
struct PiecewiseGap
{
    qint64 streamPosMs = 0;   ///< 流内位置（= 后段起点；缺口本身不占流内时长）
    qint64 wallFromMs = 0;    ///< 缺口墙钟起点（前段墙钟终点，按前段 rate 推算）
    qint64 wallToMs = 0;      ///< 缺口墙钟终点（后段墙钟起点，锚定值）
    qint64 gapWallMs = 0;     ///< 缺口墙钟时长（wallToMs − wallFromMs）
};

/// 粗采样分析结果（供两级采样编排：边界区间 → 加密取样位置）
struct CoarseAnalysis
{
    QVector<int> outlierIdx;                 ///< 野点（输入 samples 中的索引）
    QVector<QPair<qint64, qint64>> ranges;  ///< 边界区间 [loMs, hiMs]（流内）
    QVector<double> jumps;                  ///< 与 ranges 平行：跳变幅度 |r2−r1|
    bool hasBoundary() const { return !ranges.isEmpty(); }
};

/**
 * @brief 分段时间映射表（查表法）。
 *
 * wallMsOf/streamMsOf：任意流内位置 → 墙钟（二分找段 + 段内线性），
 * 对任意变速/分段跳变成立，不假定全局线性。
 */
struct PiecewiseTimeMap
{
    QVector<TimeSegment> segments;
    qint64 streamEndMs = 0;         ///< 视频流末尾（末段右边界）

    bool isValid() const { return !segments.isEmpty(); }
    int  size() const { return segments.size(); }

    /// 流内位置 → 墙钟（epoch 毫秒）
    qint64 wallMsOf(qint64 streamMs) const;
    /// 墙钟 → 流内位置（反解；墙钟超出映射范围时夹取边界段；
    /// v1.12.3 起缺口内墙钟夹取到缺口后一段起点——「跳过没录的」）
    qint64 streamMsOf(qint64 wallMs) const;

    // ---- 缺口语义（v1.12.3：拼接产物分段锚点 + 缺段的显示/反解口径）----
    /// 段 i 的墙钟终点（按其 rate 推算至下一段流内起点；末段用 streamEndMs，
    /// streamEndMs<=末段起点时返回 -1 表示无上界）
    qint64 segmentWallEndMs(int i) const;
    /// 墙钟是否落在缺口内（无素材区间；toleranceMs 吸收 rate 换算噪声）
    bool inGap(qint64 wallMs, qint64 toleranceMs = 2000) const;
    /// 缺口清单：相邻段「前段墙钟终点 + tolerance < 后段墙钟起点」的缝隙
    QVector<PiecewiseGap> gaps(qint64 toleranceMs = 2000) const;

    // ---- 参数（集中定义，便于调优）----
    static constexpr double kRateJumpThreshold = 0.10;   ///< 粗点斜率差判边界
    static constexpr int    kMinSegmentPoints  = 2;      ///< 段内最少测点数
    static constexpr int    kMaxSegments       = 32;     ///< 段数上限
    static constexpr qint64 kBoundaryMergeMs   = 2000;   ///< 精化后边界合并窗口
    static constexpr double kNormalRateDev     = 0.01;   ///< 判"正常录像"的 |rate−1| 容差
    /// 边界判定最小点对间隔（粗采样密度；加密点不参与判定）
    static constexpr qint64 kBoundaryJudgeMinGapMs = 30000;
    /// 边界精化搜索窗口（粗边界 ±；须 ≥ 最大粗点间隔，否则窗口可能为空）
    static constexpr qint64 kRefineWindowMs = 120000;

    /**
     * @brief 分段检测（纯函数，可单测）。
     *
     * 两级设计：
     *   - 粗分析：只对间隔 ≥ kBoundaryJudgeMinGapMs 的点对算斜率；
     *     尖峰野点（OCR 错读：前后斜率异号）自动剔除；
     *     其余跳变（同号单调）为真边界；
     *   - 精化：每个粗边界 ±kRefineWindowMs 内枚举分割点，取
     *     "左右两段最小二乘组合残差最小"者为精化边界（加密点定位，±1s）。
     *
     * 流程：过滤→排序→去重→粗分析（野点/边界）→精化→合并→分段→拟合。
     * 输入测点应含边界加密点（区间内 2s 步长）。
     */
    static PiecewiseTimeMap detect(const QVector<PiecewiseSample> &samples,
                                   qint64 streamEndMs,
                                   PiecewiseDetectReport *report = nullptr);

    /**
     * @brief 粗采样分析（供 CalibrationService 两级采样编排）：
     * 剔除尖峰野点，输出待加密的边界区间。与 detect 内部粗分析同源。
     */
    static CoarseAnalysis analyzeCoarse(const QVector<PiecewiseSample> &samples);

    // ---- 序列化（TimeCalibration JSON 的 piecewise 字段共用）----
    QJsonArray toJson() const;
    static PiecewiseTimeMap fromJson(const QJsonArray &arr, qint64 streamEndMs);
};
