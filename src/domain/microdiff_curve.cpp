/**
 * @file microdiff_curve.cpp
 * @brief 微变变化率曲线纯计算实现（domain）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-10
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 中位数取法：直方图（|D| 256 格 / D 511 格），偶数 n 取双中位均值——
 * 与 Python 标定原型（numpy.percentile 50）一致。
 */
#include "microdiff_curve.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <vector>

#include <QtGlobal>

namespace microdiff {

namespace {

/// @brief 直方图中第 rank 小值所在格（rank 从 0 起；rank ≥ n 时返回末格）
int histRank(const std::vector<int> &hist, int rank)
{
    int c = 0;
    for (int i = 0; i < int(hist.size()); ++i) {
        c += hist[i];
        if (rank < c)
            return i;
    }
    return int(hist.size()) - 1;
}

/// @brief 双中位均值（偶数 n 兼容 numpy 语义）：rankA=(n-1)/2, rankB=n/2
double histMedian(const std::vector<int> &hist, int64_t n, double lo)
{
    if (n <= 0)
        return 0.0;
    const int a = histRank(hist, int((n - 1) / 2));
    const int b = histRank(hist, int(n / 2));
    return lo + (a + b) / 2.0;
}

} // namespace

FrameStats roiFrameStats(const int16_t *d, int w, int h)
{
    FrameStats out;
    if (!d || w <= 0 || h <= 0)
        return out;
    const int64_t n = int64_t(w) * h;

    std::vector<int> histAbs(256, 0);   // |D| ∈ [0, 255]
    std::vector<int> histSign(511, 0);  // D ∈ [-255, 255] → bin = D + 255

    // 8×8 分块（与标定原型一致）：nx/ny ≤ 8，块边界整除铺满
    const int nx = qMax(1, qMin(8, w / 8));
    const int ny = qMax(1, qMin(8, h / 8));
    const int bw = w / nx;   // ≥1（nx ≤ w/8 或 nx=1 时 bw=w）
    const int bh = h / ny;

    std::vector<int64_t> blkSum(nx * ny, 0);
    std::vector<int> blkCnt(nx * ny, 0);

    for (int y = 0; y < h; ++y) {
        // 钳制：w/nx 整除不全时最后一列/行落在第 nx/ny 块——不钳会越界写堆
        // （227/8=28 → x=224..226 得 8；209/8=26 → y=208 得 8）。
        const int by = qMin(ny - 1, y / bh);
        const int16_t *row = d + int64_t(y) * w;
        for (int x = 0; x < w; ++x) {
            const int dv = row[x];           // [-255, 255]
            const int ad = dv < 0 ? -dv : dv;
            histAbs[ad]++;
            histSign[dv + 255]++;
            const int bx = qMin(nx - 1, x / bw);
            const int b = by * nx + bx;
            blkSum[b] += ad;
            blkCnt[b]++;
        }
    }

    out.medD = histMedian(histAbs, n, 0.0);
    out.signedD = histMedian(histSign, n, -255.0);
    for (int b = 0; b < nx * ny; ++b)
        if (blkCnt[b] > 0)
            out.blkMax = std::max(out.blkMax, double(blkSum[b]) / double(blkCnt[b]));
    return out;
}

QVector<SecondRow> aggregateSeconds(const QVector<FrameStats> &frames,
                                    const QVector<qint64> &frameTsMs)
{
    QVector<SecondRow> rows;
    const int n = qMin(frames.size(), frameTsMs.size());
    if (n <= 0)
        return rows;
    rows.reserve(n / 20 + 2);

    double sMed = 0.0, sBlk = 0.0, sSgn = 0.0;
    int count = 0;
    qint64 curSecond = -1;

    auto flush = [&]() {
        if (count <= 0)
            return;
        SecondRow r;
        r.tsMs = curSecond * 1000;
        r.medD = sMed / count;
        r.blkMax = sBlk / count;
        r.signedD = sSgn / count;
        rows.append(r);
    };

    for (int i = 0; i < n; ++i) {
        const qint64 sec = frameTsMs[i] / 1000;   // 流内 ts ≥ 0
        if (sec != curSecond) {
            flush();
            sMed = sBlk = sSgn = 0.0;
            count = 0;
            curSecond = sec;
        }
        sMed += frames[i].medD;
        sBlk += frames[i].blkMax;
        sSgn += frames[i].signedD;
        ++count;
    }
    flush();
    return rows;
}

OnsetOutcome detectOnset(const QVector<SecondRow> &rows, int roiId,
                         qint64 baseStartMs, qint64 baseDurMs,
                         int minBaselineSamples)
{
    OnsetOutcome out;
    out.onset.roiId = roiId;
    const qint64 baseEnd = baseStartMs + baseDurMs;

    // ---- 干净段统计（逐秒 blkMax 的 μ/σ）----
    double sum = 0.0;
    int n = 0;
    for (const SecondRow &r : rows) {
        if (r.tsMs >= baseStartMs && r.tsMs < baseEnd) {
            sum += r.blkMax;
            ++n;
        }
    }
    out.baselineSamples = n;
    if (n > 0)
        out.mu = sum / n;
    if (n > 1) {
        double ss = 0.0;
        for (const SecondRow &r : rows)
            if (r.tsMs >= baseStartMs && r.tsMs < baseEnd) {
                const double d = r.blkMax - out.mu;
                ss += d * d;
            }
        out.sigma = std::sqrt(ss / n);   // 总体标准差
    }
    out.baselineOk = n >= minBaselineSamples;
    if (!out.baselineOk)
        return out;   // 阈值不可信：由调用方报"基准段太短/无有效帧"
    out.threshold = out.mu + 3.0 * out.sigma;

    // ---- 首帧微变：基准段**结束之后**首次过阈且连续 ≥3 秒保持 ----
    // 必须从基准段之后起搜：基准段之前的时段用户并未标记为“干净”（典型是基准段
    // 前数小时/数十分钟的不同光照），拿它跟基准求差必然大幅超阈——从 0 秒起搜会
    // 把日出/开关灯之类的全局亮度漂移误报成“首帧微变”（实测：本素材首秒 blkMax≈54，
    // 而真火 onset 在基准段结束后 56 秒）。
    for (int i = 0; i < rows.size(); ++i) {
        if (rows[i].tsMs < baseEnd)
            continue;
        if (rows[i].blkMax <= out.threshold)
            continue;
        int j = i;
        while (j < rows.size() && rows[j].blkMax > out.threshold)
            ++j;
        if (j - i >= 3) {
            out.onset.tsMs = rows[i].tsMs;
            out.onset.mu = out.mu;
            out.onset.sigma = out.sigma;
            out.onset.threshold = out.threshold;
            const double s = rows[i].signedD;
            out.onset.direction = (s < 0.0) ? -1 : (s > 0.0 ? 1 : 0);
            return out;
        }
        i = j - 1;   // 整段过阈不足 3 秒：起点不可能成立，跳过该段
    }
    return out;   // 未检出（tsMs = -1）
}

} // namespace microdiff