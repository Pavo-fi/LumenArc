/**
 * @file time_piecewise.cpp
 * @brief 分段检测与查表换算实现（v1.2.1）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-08
 * @version 1.1
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 两级检测：
 *   - 粗分析：只对间隔 ≥ kBoundaryJudgeMinGapMs 的点对算斜率；
 *     OCR 错读（尖峰：前后斜率异号）自动剔除；其余跳变（同号单调）
 *     为真变速边界，输出区间 [lo, hi] 供加密取样。
 *   - 精化：粗边界 ±kRefineWindowMs 内枚举分割点，取"左右两段
 *     最小二乘组合残差最小"者为精化边界（加密点定位，±1 点间隔）。
 */
#include "domain/time_piecewise.h"

#include <algorithm>
#include <limits>

namespace {

struct Point {
    qint64 s = -1;
    qint64 w = 0;
    double conf = 0.0;
    int    origIdx = -1;   ///< 输入 samples 中的索引（野点上报用）
    bool   outlier = false;
};

/// 段拟合野点残差阈值（OCR 数字错读通常 > 10s；鲁棒拟合迭代剔除）
static constexpr double kRobustResidualMs = 10000.0;
/// 相邻段合并的 rate 差阈值（吸收假边界：错读点产生的同率两段）
static constexpr double kSegmentMergeRate = 0.05;
/// 段完整性：点数不足时的最小段宽（宽段仅 1 点也保留）
static constexpr qint64 kMinSegmentWidthMs = 30000;
/// 段 rate sanity：|rate−1| 超此值并入邻居（错读碎片段）
static constexpr double kMaxSaneRateDev = 1.5;

/// 段内最小二乘斜率（中心化；跳过 outlier 与 drop 标记）。
/// 返回 0 表示无法拟合（沿用 1.0）。
double fitRateCore(const QVector<Point> &pts, const QVector<bool> &drop)
{
    int n = 0;
    double mx = 0.0, my = 0.0;
    for (int i = 0; i < pts.size(); ++i) {
        if (pts[i].outlier || (i < drop.size() && drop[i]))
            continue;
        ++n;
        mx += static_cast<double>(pts[i].s);
        my += static_cast<double>(pts[i].w);
    }
    if (n < 1)
        return 1.0;
    mx /= n;
    my /= n;
    double sxx = 0.0, sxy = 0.0;
    for (int i = 0; i < pts.size(); ++i) {
        if (pts[i].outlier || (i < drop.size() && drop[i]))
            continue;
        const double dx = static_cast<double>(pts[i].s) - mx;
        const double dy = static_cast<double>(pts[i].w) - my;
        sxx += dx * dx;
        sxy += dx * dy;
    }
    if (sxx <= 0.0)
        return 1.0;   // 全部同位置：无法估速率
    return sxy / sxx;
}

/// 鲁棒拟合：迭代剔除残差 > kRobustResidualMs 的点（最多 2 轮）。
/// 返回 (rate, 被剔除数)；outlier 标记点恒被跳过。
double fitRate(const QVector<Point> &pts, int *droppedOut = nullptr)
{
    QVector<bool> drop(pts.size(), false);
    int dropped = 0;
    for (int round = 0; round < 2; ++round) {
        const double rate = fitRateCore(pts, drop);
        double off = 0.0;
        {
            int n = 0;
            double mx = 0.0, my = 0.0;
            for (int i = 0; i < pts.size(); ++i) {
                if (pts[i].outlier || drop[i])
                    continue;
                ++n;
                mx += static_cast<double>(pts[i].s);
                my += static_cast<double>(pts[i].w);
            }
            if (n > 0) {
                mx /= n;
                my /= n;
                off = my - rate * mx;
            }
        }
        // 找最大残差（跳过已剔除）
        int worst = -1;
        double worstRes = 0.0;
        for (int i = 0; i < pts.size(); ++i) {
            if (pts[i].outlier || drop[i])
                continue;
            const double r = std::fabs(off + rate * static_cast<double>(pts[i].s)
                                       - static_cast<double>(pts[i].w));
            if (r > worstRes) {
                worstRes = r;
                worst = i;
            }
        }
        if (worst < 0 || worstRes <= kRobustResidualMs)
            break;
        drop[worst] = true;
        ++dropped;
    }
    if (droppedOut)
        *droppedOut = dropped;
    return fitRateCore(pts, drop);
}

/// 段率估计：段内有效点 ≥ 2 用鲁棒最小二乘；单点段用段首→段尾点对斜率
/// （单点段 fitRate 返回 1.0 会造成 1.65 档窄段与 1.0 档邻居误合并）
double segRate(const QVector<Point> &pts, int l0, int l1)
{
    int usable = 0;
    for (int i = l0; i < l1; ++i)
        if (!pts[i].outlier)
            ++usable;
    if (usable >= 2)
        return fitRate(QVector<Point>(pts.constBegin() + l0,
                                      pts.constBegin() + l1));
    if (l1 < pts.size() && pts[l1].s > pts[l0].s)
        return static_cast<double>(pts[l1].w - pts[l0].w)
               / static_cast<double>(pts[l1].s - pts[l0].s);
    return 1.0;
}

/// 精化评估的剔除惩罚（每个被剔除点 +1e8；防止鲁棒剔除抹平档位差异）
static constexpr double kDropPenalty = 100000000.0;   // (10s)²

/// 鲁棒最小二乘残差平方和（跳过 outlier/drop；精化分割点评估用）
/// 迭代剔除残差 > kRobustResidualMs 的点（最多 2 轮），再算 SSE。
/// 返回 SSE' = SSE + dropped × kDropPenalty（剔除越少越好）。
double fitSse(const QVector<Point> &pts, int from, int to)
{
    QVector<Point> sub;
    for (int i = from; i < to; ++i)
        sub.append(pts[i]);
    QVector<bool> drop(sub.size(), false);
    int dropped = 0;
    for (int round = 0; round < 2; ++round) {
        const double r = fitRateCore(sub, drop);
        int n = 0;
        double sx = 0.0, sy = 0.0;
        for (int i = 0; i < sub.size(); ++i) {
            if (sub[i].outlier || drop[i])
                continue;
            ++n;
            sx += static_cast<double>(sub[i].s);
            sy += static_cast<double>(sub[i].w);
        }
        if (n == 0)
            return 0.0;
        const double off = sy / n - r * (sx / n);
        int worst = -1;
        double worstRes = 0.0;
        for (int i = 0; i < sub.size(); ++i) {
            if (sub[i].outlier || drop[i])
                continue;
            const double res = std::fabs(off + r * static_cast<double>(sub[i].s)
                                         - static_cast<double>(sub[i].w));
            if (res > worstRes) {
                worstRes = res;
                worst = i;
            }
        }
        if (worst < 0 || worstRes <= kRobustResidualMs)
            break;
        drop[worst] = true;
        ++dropped;
    }
    const double r = fitRateCore(sub, drop);
    int n = 0;
    double sx = 0.0, sy = 0.0;
    for (int i = 0; i < sub.size(); ++i) {
        if (sub[i].outlier || drop[i])
            continue;
        ++n;
        sx += static_cast<double>(sub[i].s);
        sy += static_cast<double>(sub[i].w);
    }
    if (n < 2)
        return 0.0;
    const double off = sy / n - r * (sx / n);
    double sse = 0.0;
    for (int i = 0; i < sub.size(); ++i) {
        if (sub[i].outlier || drop[i])
            continue;
        const double res = off + r * static_cast<double>(sub[i].s)
                           - static_cast<double>(sub[i].w);
        sse += res * res;
    }
    return sse + dropped * kDropPenalty;
}

/// 粗斜率（间隔 ≥ 阈值的相邻点对）与尖峰野点检测。
/// 尖峰特征：OCR 错读使点 i 的墙钟偏移 → 前后斜率一降一升（异号）；
/// 真边界两侧斜率同号单调（跨边界斜率落在两档之间）。
/// 迭代剔除：每轮只剔"最强尖峰"，重算斜率，防止误伤邻居（B3 实测）。
struct CoarseSlope {
    int    idx;      ///< 斜率所在点对右端点（pts 索引）
    double rate;
};
static void buildCoarseSlopes(const QVector<Point> &pts,
                              QVector<CoarseSlope> *slopesOut)
{
    QVector<CoarseSlope> &slopes = *slopesOut;
    slopes.clear();
    // 每个点向后找第一个间隔 ≥ 阈值的点组成点对（加密点插入后粗点
    // 不再相邻，不能只检查相邻点对）
    for (int i = 0; i < pts.size(); ++i) {
        for (int j = i + 1; j < pts.size(); ++j) {
            if (pts[j].s - pts[i].s
                < PiecewiseTimeMap::kBoundaryJudgeMinGapMs)
                continue;
            CoarseSlope cs;
            cs.idx = j;
            cs.rate = static_cast<double>(pts[j].w - pts[i].w)
                      / static_cast<double>(pts[j].s - pts[i].s);
            slopes.append(cs);
            break;
        }
    }
}

void markSpikeOutliers(QVector<Point> &pts,
                       QVector<CoarseSlope> *slopesOut,
                       QVector<int> *outlierIdxOut)
{
    QVector<int> &outliers = *outlierIdxOut;
    QVector<CoarseSlope> slopes;
    buildCoarseSlopes(pts, &slopes);
    // 迭代：每轮标记最强尖峰（|d1|+|d2| 最大），重算后继续
    for (;;) {
        int bestK = -1;
        double bestMag = 0.0;
        for (int k = 1; k + 1 < slopes.size(); ++k) {
            if (pts[slopes[k].idx].outlier)
                continue;
            const double d1 = slopes[k].rate - slopes[k - 1].rate;
            const double d2 = slopes[k + 1].rate - slopes[k].rate;
            if (d1 * d2 < 0.0
                && std::fabs(d1) > PiecewiseTimeMap::kRateJumpThreshold
                && std::fabs(d2) > PiecewiseTimeMap::kRateJumpThreshold) {
                // 真野点特征：单点墙钟错 Δ → 两侧斜率各偏 ±Δ/ds，
                // 即 d2 ≈ −2×d1（幅度比 1.2~2.8）。
                // 跨边界点（斜率切换）幅度比不满足 → 保留。
                const double ratio = std::fabs(d2 / d1);
                if (ratio < 1.2 || ratio > 2.8)
                    continue;
                const double mag = std::fabs(d1) + std::fabs(d2);
                if (mag > bestMag) {
                    bestMag = mag;
                    bestK = k;
                }
            }
        }
        if (bestK < 0)
            break;
        const int oi = slopes[bestK].idx;
        pts[oi].outlier = true;
        outliers.append(oi);
        buildCoarseSlopes(pts, &slopes);
    }
    // 剔除野点后：直接过滤原始斜率序列（保留各项独立点对斜率；
    // 重算"跨野点斜率"会丢失序列首项并引入混合斜率，导致边界漏检）
    QVector<CoarseSlope> clean;
    for (const CoarseSlope &cs : slopes)
        if (!pts[cs.idx].outlier)
            clean.append(cs);
    *slopesOut = clean;
}

} // namespace

// ---------------------------------------------------------------------------
qint64 PiecewiseTimeMap::wallMsOf(qint64 streamMs) const
{
    if (segments.isEmpty())
        return streamMs;
    // 找段：segments[i].streamStartMs <= streamMs < segments[i+1].streamStartMs
    int i = 0;
    for (; i + 1 < segments.size(); ++i)
        if (streamMs < segments[i + 1].streamStartMs)
            break;
    const TimeSegment &seg = segments[i];
    const double rate = seg.rate > 0.0 ? seg.rate : 1.0;
    return seg.wallStartMs + static_cast<qint64>(
               std::llround(rate * static_cast<double>(streamMs - seg.streamStartMs)));
}

qint64 PiecewiseTimeMap::streamMsOf(qint64 wallMs) const
{
    if (segments.isEmpty())
        return wallMs;
    // 找段：段 i 墙钟覆盖 [wallStart_i, wallStart_{i+1})，末段无上界
    for (int i = 0; i < segments.size(); ++i) {
        const TimeSegment &seg = segments[i];
        if (i + 1 < segments.size()) {
            const TimeSegment &next = segments[i + 1];
            if (wallMs >= next.wallStartMs)
                continue;
        }
        const double rate = seg.rate > 0.0 ? seg.rate : 1.0;
        return seg.streamStartMs + static_cast<qint64>(
                   std::llround(static_cast<double>(wallMs - seg.wallStartMs) / rate));
    }
    // 超出末段：用末段反算（夹取）
    const TimeSegment &last = segments.last();
    const double rate = last.rate > 0.0 ? last.rate : 1.0;
    return last.streamStartMs + static_cast<qint64>(
               std::llround(static_cast<double>(wallMs - last.wallStartMs) / rate));
}

// ---------------------------------------------------------------------------
CoarseAnalysis PiecewiseTimeMap::analyzeCoarse(const QVector<PiecewiseSample> &in)
{
    CoarseAnalysis out;

    // 过滤 + 排序 + 同位置去重（保留 conf 高者）
    QVector<Point> pts;
    for (int i = 0; i < in.size(); ++i) {
        const PiecewiseSample &s = in[i];
        if (!s.used || s.streamMs < 0 || s.wallMs <= 0)
            continue;
        Point p;
        p.s = s.streamMs;
        p.w = s.wallMs;
        p.conf = s.conf;
        p.origIdx = i;
        pts.append(p);
    }
    std::sort(pts.begin(), pts.end(),
              [](const Point &a, const Point &b) { return a.s < b.s; });
    QVector<Point> uniq;
    for (const Point &p : pts) {
        if (!uniq.isEmpty() && p.s - uniq.last().s <= 500) {
            if (p.conf > uniq.last().conf)
                uniq.last() = p;
            continue;
        }
        uniq.append(p);
    }
    pts = uniq;
    if (pts.size() < 2)
        return out;

    QVector<CoarseSlope> slopes;
    markSpikeOutliers(pts, &slopes, &out.outlierIdx);

    // 边界：干净粗斜率跳变 > 阈值 → 区间 [前点, 本点]
    for (int k = 1; k < slopes.size(); ++k) {
        const double jump = std::fabs(slopes[k].rate - slopes[k - 1].rate);
        if (jump > kRateJumpThreshold) {
            out.ranges.append(qMakePair(pts[slopes[k - 1].idx].s,
                                        pts[slopes[k].idx].s));
            out.jumps.append(jump);
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
PiecewiseTimeMap PiecewiseTimeMap::detect(const QVector<PiecewiseSample> &in,
                                          qint64 streamEndMs,
                                          PiecewiseDetectReport *report)
{
    PiecewiseTimeMap map;
    PiecewiseDetectReport rep;

    // 1. 过滤 + 排序 + 去重（同位置保留 conf 高者）
    QVector<Point> pts;
    for (int i = 0; i < in.size(); ++i) {
        const PiecewiseSample &s = in[i];
        if (!s.used || s.streamMs < 0 || s.wallMs <= 0)
            continue;
        Point p;
        p.s = s.streamMs;
        p.w = s.wallMs;
        p.conf = s.conf;
        p.origIdx = i;
        pts.append(p);
    }
    std::sort(pts.begin(), pts.end(),
              [](const Point &a, const Point &b) { return a.s < b.s; });
    QVector<Point> uniq;
    for (const Point &p : pts) {
        if (!uniq.isEmpty() && p.s - uniq.last().s <= 500) {
            if (p.conf > uniq.last().conf)
                uniq.last() = p;
            continue;
        }
        uniq.append(p);
    }
    pts = uniq;
    if (pts.isEmpty())
        return map;   // 无效（isValid()=false）

    rep.totalStreamSpanMs = pts.last().s - pts.first().s;
    rep.totalWallSpanSec = (pts.last().w - pts.first().w) / 1000.0;
    if (pts.size() == 1) {
        rep.segmentCount = 1;
        rep.overallRate = 1.0;
        TimeSegment seg;
        seg.streamStartMs = pts.first().s;
        seg.wallStartMs = pts.first().w;
        seg.rate = 1.0;
        map.segments.append(seg);
        map.streamEndMs = qMax(streamEndMs, pts.first().s);
        rep.speedVariant = false;
        if (report)
            *report = rep;
        return map;
    }

    // 2. 粗分析：粗斜率 + 尖峰野点剔除
    QVector<CoarseSlope> slopes;
    QVector<int> outlierIdx;
    markSpikeOutliers(pts, &slopes, &outlierIdx);

    // 3. 粗边界：干净粗斜率跳变 > 阈值。跳变发生在点对 k-1 与 k 之间，
    //    真实边界 ∈ [pts[slopes[k-1].idx].s, pts[slopes[k].idx].s]，
    //    记录区间 [loIdx, hiIdx]（精化只在该区间内枚举）。
    struct CoarseBoundary { int lo; int hi; };
    QVector<CoarseBoundary> coarseB;
    for (int k = 1; k < slopes.size(); ++k) {
        if (std::fabs(slopes[k].rate - slopes[k - 1].rate)
            > kRateJumpThreshold) {
            CoarseBoundary cb;
            cb.lo = slopes[k - 1].idx;
            cb.hi = slopes[k].idx;
            if (cb.lo == cb.hi)
                continue;   // 零宽区间：野点伪影，跳过
            coarseB.append(cb);
        }
    }

    // 4. 精化：在每个粗边界的跳变区间 [loIdx, hiIdx] 内枚举分割点（含
    //    加密点），取"左右两段组合残差最小"者；全部候选不可用（段点数
    //    不足）时兜底取区间左端点（跳变起点，语义正确）。
    QVector<int> refined;
    int prevRefined = 0;   // 前一边界的精化位置（左段起点，pts 索引）
    for (const CoarseBoundary &cb : coarseB) {
        const int ci = cb.lo;
        int lo = cb.lo, hi = cb.hi;
        // 收纳区间外 5s 内的加密点（生成时外扩 kBoundaryPadMs）
        while (lo > 0 && pts[cb.lo].s - pts[lo - 1].s <= 5000)
            --lo;
        while (hi + 1 < pts.size()
               && pts[hi + 1].s - pts[cb.hi].s <= 5000)
            ++hi;
        int leftStart = prevRefined;
        int rightEnd = pts.size();
        for (const CoarseBoundary &c2 : coarseB) {
            if (c2.lo >= cb.hi) {   // 含共享端点：右段只覆盖本边界单侧档位
                rightEnd = qMin(rightEnd, c2.lo);
                break;
            }
        }
        // 区间内无中间点（无加密点）：两点必成线，SSE 无信息量，
        // 直接用跳变左端点（语义正确），不做 SSE 评估。
        int midCount = 0;
        for (int j = cb.lo + 1; j < cb.hi; ++j)
            ++midCount;
        if (midCount == 0) {
            refined.append(ci);
            prevRefined = ci;
            continue;
        }
        int bestJ = ci;
        double bestSse = std::numeric_limits<double>::max();
        for (int j = lo; j <= hi; ++j) {
            if (j - leftStart < kMinSegmentPoints
                || rightEnd - j < kMinSegmentPoints)
                continue;
            const double sse = fitSse(pts, leftStart, j)
                               + fitSse(pts, j, rightEnd);
            if (sse < bestSse) {
                bestSse = sse;
                bestJ = j;
            }
        }
        refined.append(bestJ);
        prevRefined = bestJ;
    }

    // 5. 合并相邻边界（精化后间隔 < kBoundaryMergeMs，保留靠前者）
    QVector<int> bIdx;
    for (int i = 0; i < refined.size(); ++i) {
        if (!bIdx.isEmpty()
            && pts[refined[i]].s - pts[bIdx.last()].s < kBoundaryMergeMs)
            continue;
        bIdx.append(refined[i]);
    }

    // 6. 段完整性：段起点集合 = {0} ∪ bIdx；点数不足且段跨度 < 阈值才删
    //    （段跨度 = 本段首点到下一段首点；窄档位段仅 1 个粗点也保留）
    QVector<int> starts;
    starts.append(0);
    for (int b : bIdx)
        starts.append(b);
    for (int i = starts.size() - 1; i >= 1; --i) {
        const int segEnd = (i + 1 < starts.size()) ? starts[i + 1] : pts.size();
        // 段跨度 = 本段首点到下一段首点（或末点）；单点段也用真实跨度
        const qint64 widthMs = (segEnd < pts.size())
            ? pts[segEnd].s - pts[starts[i]].s
            : pts.last().s - pts[starts[i]].s;
        if (segEnd - starts[i] < kMinSegmentPoints
            && widthMs < kMinSegmentWidthMs)
            starts.removeAt(i);
    }
    // 整体变速兜底：无边界但全片率超容差 → 最大粗斜率跳变处为唯一边界
    if (starts.size() == 1 && pts.size() >= 4) {
        int best = -1;
        double bestJump = 0.0;
        for (int k = 1; k < slopes.size(); ++k) {
            const double j = std::fabs(slopes[k].rate - slopes[k - 1].rate);
            if (j > bestJump) {
                bestJump = j;
                best = slopes[k - 1].idx;   // 左端点语义（与粗边界一致）
            }
        }
        const qint64 ds = pts.last().s - pts.first().s;
        const double overall = ds > 0
            ? static_cast<double>(pts.last().w - pts.first().w)
                  / static_cast<double>(ds)
            : 1.0;
        if (best > 0 && bestJump > kRateJumpThreshold
            && std::fabs(overall - 1.0) > kNormalRateDev)
            starts.append(best);
    }

    // 6.5 段合并：相邻段 rate 差 < kSegmentMergeRate → 合并
    // （吸收错读点产生的"同率两段"假边界；真边界档位差 ≥ 0.1 不受影响）
    if (starts.size() > 1) {
        bool again = true;
        while (again && starts.size() > 1) {
            again = false;
            for (int i = starts.size() - 1; i >= 1; --i) {
                const int l0 = starts[i - 1], l1 = starts[i];
                const int r0 = starts[i], r1 = (i + 1 < starts.size())
                                                   ? starts[i + 1] : pts.size();
                if (l1 - l0 < kMinSegmentPoints || r1 - r0 < kMinSegmentPoints)
                    continue;
                const double rL = segRate(pts, l0, l1);
                const double rR = segRate(pts, r0, r1);
                if (std::fabs(rL - rR) < kSegmentMergeRate) {
                    starts.removeAt(i);
                    again = true;
                    break;
                }
            }
        }
    }

    // 6.7 段 rate sanity：rate 超出 [0.4, 2.5] 的段（错读碎片）并入率差更小的邻居
    if (starts.size() > 1) {
        bool again = true;
        while (again && starts.size() > 1) {
            again = false;
            for (int i = 0; i < starts.size(); ++i) {
                const int l0 = starts[i];
                const int l1 = (i + 1 < starts.size()) ? starts[i + 1]
                                                       : pts.size();
                if (l1 - l0 < kMinSegmentPoints)
                    continue;
                const double r = segRate(pts, l0, l1);
                if (r >= 0.4 && r <= 2.5)
                    continue;
                // 并入邻居（选率差小者）
                int target = -1;
                if (i > 0) {
                    const int p0 = starts[i - 1];
                    const double rP = segRate(pts, p0, l0);
                    target = i - 1;
                    double bestD = std::fabs(rP - r);
                    if (i + 1 < starts.size()) {
                        const int n1 = starts[i + 1];
                        const int n2 = (i + 2 < starts.size())
                                           ? starts[i + 2] : pts.size();
                        const double rN = segRate(pts, n1, n2);
                        if (std::fabs(rN - r) < bestD) {
                            bestD = std::fabs(rN - r);
                            target = i + 1;
                        }
                    }
                } else if (i + 1 < starts.size()) {
                    target = i + 1;
                }
                if (target >= 0) {
                    if (target == i + 1)
                        starts.removeAt(i);
                    else
                        starts.removeAt(i + 1);   // target == i-1：删右边界
                    again = true;
                    break;
                }
            }
        }
    }

    // 7. 段数上限：超限时反复合并"斜率差最小"的相邻段
    if (starts.size() > 1) {
        while (starts.size() > kMaxSegments) {
            int worst = -1;
            double worstJump = std::numeric_limits<double>::max();
            for (int i = 1; i + 1 < starts.size(); ++i) {
                const int b = starts[i];
                const int l0 = starts[i - 1], l1 = b;
                const int r0 = b, r1 = (i + 2 < starts.size())
                                           ? starts[i + 2] : pts.size();
                if (l1 - l0 < kMinSegmentPoints || r1 - r0 < kMinSegmentPoints)
                    continue;
                const double rL = segRate(pts, l0, l1);
                const double rR = segRate(pts, r0, r1);
                const double j = std::fabs(rL - rR);
                if (j < worstJump) {
                    worstJump = j;
                    worst = i;
                }
            }
            if (worst < 0)
                break;
            starts.removeAt(worst);   // 删边界 = 合并相邻两段
        }
    }

    // 8. 每段：rate = 段内最小二乘；wallStart/streamStart = 段首实测。
    //    段内有效点 < 2 时用段首→段尾（下一段首/末点）点对斜率。
    rep.boundaryCount = starts.size() - 1;
    rep.segmentCount = starts.size();
    for (int i = 0; i < starts.size(); ++i) {
        const int begin = starts[i];
        const int end = (i + 1 < starts.size()) ? starts[i + 1] : pts.size();
        QVector<Point> segPts(pts.constBegin() + begin, pts.constBegin() + end);
        TimeSegment seg;
        seg.streamStartMs = pts[begin].s;
        seg.wallStartMs = pts[begin].w;
        int usable = 0;
        for (const Point &p : segPts)
            if (!p.outlier)
                ++usable;
        if (usable >= 2) {
            seg.rate = fitRate(segPts);
        } else if (end < pts.size() && pts[end].s > pts[begin].s) {
            // 单点段：用段首→段尾（下一段首）点对斜率
            const double ds = static_cast<double>(pts[end].s - pts[begin].s);
            seg.rate = ds > 0.0
                ? static_cast<double>(pts[end].w - pts[begin].w) / ds : 1.0;
        }
        map.segments.append(seg);
    }
    map.streamEndMs = qMax(streamEndMs, pts.last().s);

    // 全片判定
    rep.overallRate = rep.totalStreamSpanMs > 0
        ? (pts.last().w - pts.first().w)
              / static_cast<double>(rep.totalStreamSpanMs)
        : 1.0;
    rep.speedVariant = rep.boundaryCount > 0
        || std::fabs(rep.overallRate - 1.0) > kNormalRateDev;
    rep.outlierCount = 0;
    for (const Point &p : pts)
        if (p.outlier)
            ++rep.outlierCount;

    if (report)
        *report = rep;
    return map;
}

// ---------------------------------------------------------------------------
QJsonArray PiecewiseTimeMap::toJson() const
{
    QJsonArray arr;
    for (const TimeSegment &s : segments) {
        QJsonObject o;
        o[QStringLiteral("streamStartMs")] = static_cast<double>(s.streamStartMs);
        o[QStringLiteral("wallStartMs")] = static_cast<double>(s.wallStartMs);
        o[QStringLiteral("rate")] = s.rate;
        arr.append(o);
    }
    return arr;
}

PiecewiseTimeMap PiecewiseTimeMap::fromJson(const QJsonArray &arr, qint64 streamEndMs)
{
    PiecewiseTimeMap map;
    map.streamEndMs = streamEndMs;
    for (const QJsonValue &v : arr) {
        const QJsonObject o = v.toObject();
        TimeSegment s;
        s.streamStartMs = static_cast<qint64>(o[QStringLiteral("streamStartMs")].toDouble());
        s.wallStartMs = static_cast<qint64>(o[QStringLiteral("wallStartMs")].toDouble());
        s.rate = o[QStringLiteral("rate")].toDouble(1.0);
        if (s.rate <= 0.0)
            s.rate = 1.0;
        map.segments.append(s);
    }
    return map;
}
