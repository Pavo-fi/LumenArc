#ifndef SPEED_PLAN_H
#define SPEED_PLAN_H

/// @file speed_plan.h
/// @brief 选段分段变速方案（P-68 拍板 Q4 改判：倍速分段可调——
///        不关键段快放掠过、关键段常速/慢放）纯函数域模型。
///
/// 模型：选段 [aMs, bMs] 内以 splits（流内时刻，严格落在开区间内、升序去重）
/// 切成 N+1 个分速段，每段一个倍率 rate ∈ {0.25, 0.5, 1, 2, 4, 8}。
/// 输出时间轴 = 各段「源时长 ÷ rate」依次拼接；源↔输出映射为分段线性连续函数
/// （段内均匀变速，段边界两侧速率跳变）。导出与（后续）预览共用同一映射，
/// 保证「所见即所得」。
///
/// 纯函数、无 Qt 依赖（仅用 QVector/qMin 等基础），供单测直接覆盖。

#include <QVector>
#include <QtGlobal>
#include <algorithm>
#include <cmath>

namespace speedplan {

/// 允许倍率档（与播放倍速芯片同档，拍板沿用）
inline const QVector<double> &allowedRates()
{
    static const QVector<double> kRates = {0.25, 0.5, 1.0, 2.0, 4.0, 8.0};
    return kRates;
}

struct SpeedPlan {
    qint64 aMs = -1;              ///< 选段起点（流内 ms，含）
    qint64 bMs = -1;              ///< 选段终点（流内 ms，含）
    QVector<qint64> splits;       ///< 分速边界（升序、严格在 (a,b) 内）
    QVector<double> rates;        ///< 每段倍率，长度 = splits.size()+1

    bool isValid() const { return aMs >= 0 && bMs > aMs; }

    /// 规范化：边界裁剪到 (a,b)、升序去重；rates 补齐/截断到段数（缺省 1.0，
    /// 非法倍率收编到最近允许档）。返回是否发生了改动。
    bool normalize()
    {
        bool changed = false;
        if (!isValid()) {
            splits.clear();
            rates.clear();
            return true;
        }
        QVector<qint64> kept;
        for (qint64 s : splits) {
            if (s > aMs && s < bMs && !kept.contains(s))
                kept.append(s);
            else
                changed = true;
        }
        std::sort(kept.begin(), kept.end());
        if (kept != splits) { splits = kept; changed = true; }
        const int nSeg = splits.size() + 1;
        while (rates.size() < nSeg) { rates.append(1.0); changed = true; }
        if (rates.size() > nSeg) { rates.resize(nSeg); changed = true; }
        const auto &allowed = allowedRates();
        for (double &r : rates) {
            bool ok = false;
            for (double ar : allowed)
                if (qAbs(ar - r) < 1e-9) { ok = true; break; }
            if (!ok) {   // 收编最近档
                double best = 1.0, bestD = 1e18;
                for (double ar : allowed) {
                    const double d = qAbs(ar - r);
                    if (d < bestD) { bestD = d; best = ar; }
                }
                r = best;
                changed = true;
            }
        }
        return changed;
    }

    int segmentCount() const { return qMax(1, splits.size() + 1); }

    /// 段 i 的源区间 [start, end)（末段 end=bMs）
    void segmentBounds(int i, qint64 *startMs, qint64 *endMs) const
    {
        const qint64 s = (i <= 0) ? aMs : splits.at(i - 1);
        const qint64 e = (i >= splits.size()) ? bMs : splits.at(i);
        if (startMs) *startMs = s;
        if (endMs) *endMs = e;
    }

    double segmentRate(int i) const
    {
        return (i >= 0 && i < rates.size()) ? rates.at(i) : 1.0;
    }

    /// 输出总时长（ms，浮点）：Σ (len_i / rate_i)
    double outputDurationMs() const
    {
        if (!isValid())
            return 0.0;
        double acc = 0.0;
        for (int i = 0; i < segmentCount(); ++i) {
            qint64 s, e;
            segmentBounds(i, &s, &e);
            acc += double(e - s) / segmentRate(i);
        }
        return acc;
    }

    /// 输出时刻（ms，自 0 起）→ 源流内时刻（ms，浮点，[aMs, bMs] 内夹取）。
    /// 超出输出总长返回 bMs。
    double sourceMsAtOutputMs(double outMs) const
    {
        if (!isValid() || outMs <= 0.0)
            return double(aMs);
        double acc = 0.0;
        for (int i = 0; i < segmentCount(); ++i) {
            qint64 s, e;
            segmentBounds(i, &s, &e);
            const double outLen = double(e - s) / segmentRate(i);
            if (outMs < acc + outLen) {
                const double u = (outLen > 0.0) ? (outMs - acc) / outLen : 0.0;
                return s + u * double(e - s);
            }
            acc += outLen;
        }
        return double(bMs);
    }

    /// 输出时刻所处段的倍率（OSD 动态倍速显示用）
    double rateAtOutputMs(double outMs) const
    {
        if (!isValid())
            return 1.0;
        double acc = 0.0;
        for (int i = 0; i < segmentCount(); ++i) {
            qint64 s, e;
            segmentBounds(i, &s, &e);
            acc += double(e - s) / segmentRate(i);
            if (outMs < acc)
                return segmentRate(i);
        }
        return segmentRate(segmentCount() - 1);
    }

    /// 逆映射：源时刻 → 输出时刻（覆盖条/游标定位用；段内线性插值，
    /// 源时刻越界夹取到端点）
    double outputMsAtSourceMs(double srcMs) const
    {
        if (!isValid() || srcMs <= aMs)
            return 0.0;
        double acc = 0.0;
        for (int i = 0; i < segmentCount(); ++i) {
            qint64 s, e;
            segmentBounds(i, &s, &e);
            if (srcMs < e) {
                const double u = (e > s) ? (srcMs - s) / double(e - s) : 0.0;
                return acc + u * double(e - s) / segmentRate(i);
            }
            acc += double(e - s) / segmentRate(i);
        }
        return outputDurationMs();
    }

    /// 输出总帧数（输出 fps = 源 fps；ceil 保证尾帧覆盖到 bMs）
    qint64 outputFrameCount(double outFps) const
    {
        if (outFps <= 0.0)
            return 0;
        return qint64(std::ceil(outputDurationMs() * outFps / 1000.0));
    }

    /// 输出第 k 帧（0 起）对应的源时刻（ms）
    double sourceMsAtOutputFrame(qint64 k, double outFps) const
    {
        if (outFps <= 0.0)
            return double(aMs);
        return sourceMsAtOutputMs(double(k) * 1000.0 / outFps);
    }
};

/// 标签时刻集合 → 默认分速边界（选段内标签即「关键时刻」标记，拍板复用为
/// 分速边界初值；对话框可再增删）。返回已规范化的 plan。
inline SpeedPlan planFromLabels(qint64 aMs, qint64 bMs,
                                const QVector<qint64> &labelTimesMs,
                                double defaultRate = 1.0)
{
    SpeedPlan p;
    p.aMs = aMs;
    p.bMs = bMs;
    for (qint64 t : labelTimesMs)
        if (t > aMs && t < bMs)
            p.splits.append(t);
    p.rates.fill(defaultRate, p.splits.size() + 1);
    p.normalize();
    return p;
}

} // namespace speedplan

#endif // SPEED_PLAN_H
