/**
 * @file time_calibration.cpp
 * @brief TimeCalibration::fit 最小二乘拟合实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-05
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "domain/time_calibration.h"

TimeCalibration::FitResult TimeCalibration::fit(const QVector<Sample> &samples)
{
    FitResult fr;

    QVector<const Sample *> pts;
    for (const Sample &s : samples)
        if (s.used && s.streamMs >= 0)
            pts.append(&s);

    const int n = pts.size();
    fr.pointsUsed = n;
    if (n == 0)
        return fr;  // ok=false，调用方保持原校时值不变

    // 单点：现状语义（固定偏移，rate=1.0）
    if (n == 1) {
        fr.ok = true;
        fr.offsetMs = pts[0]->wallMs - pts[0]->streamMs;
        fr.rate = 1.0;
        return fr;
    }

    // 中心化最小二乘：epoch 毫秒量级 1e12，先中心化保数值精度
    double mx = 0.0, my = 0.0;
    for (const Sample *s : pts) {
        mx += static_cast<double>(s->streamMs);
        my += static_cast<double>(s->wallMs);
    }
    mx /= n;
    my /= n;

    double sxx = 0.0, sxy = 0.0;
    for (const Sample *s : pts) {
        const double dx = static_cast<double>(s->streamMs) - mx;
        const double dy = static_cast<double>(s->wallMs) - my;
        sxx += dx * dx;
        sxy += dx * dy;
    }

    // 全部测点同一流内位置 → 无法拟合速率，退化为单点
    if (sxx <= 0.0) {
        fr.ok = true;
        fr.pointsUsed = 1;
        fr.offsetMs = static_cast<qint64>(std::llround(my)) - pts[0]->streamMs;
        fr.rate = 1.0;
        return fr;
    }

    fr.ok = true;
    fr.rate = sxy / sxx;
    const double offsetD = my - fr.rate * mx;
    fr.offsetMs = static_cast<qint64>(std::llround(offsetD));

    // 残差与标准误
    double maxRes = 0.0, sumRes2 = 0.0;
    for (const Sample *s : pts) {
        const double r = (offsetD + fr.rate * static_cast<double>(s->streamMs))
                         - static_cast<double>(s->wallMs);
        sumRes2 += r * r;
        if (std::fabs(r) > maxRes)
            maxRes = std::fabs(r);
    }
    fr.maxResidualMs = maxRes;

    if (n >= 3) {
        // 残差方差（n-2 自由度）估计测量噪声
        const double var = sumRes2 / static_cast<double>(n - 2);
        fr.sigmaRate = std::sqrt(var / sxx);
        fr.sigmaOffsetMs = std::sqrt(var * (1.0 / n + mx * mx / sxx));
    } else {
        // n == 2：数据无法估计误差，用单点假设误差（保守）
        const double var = kAssumedPointErrorMs * kAssumedPointErrorMs;
        fr.sigmaRate = std::sqrt(2.0 * var / sxx);
        fr.sigmaOffsetMs = kAssumedPointErrorMs;
    }

    const double dev = std::fabs(fr.rate - 1.0);
    fr.rateSignificant = dev > qMax(3.0 * fr.sigmaRate, kMinSignificantRateDev);
    fr.rateSane = dev <= kMaxSaneRateDev;

    if (!fr.rateSane)
        fr.warning = FitWarning::RateInsane;
    else if (n >= 3 && maxRes > kOutlierResidualMs)
        fr.warning = FitWarning::OutlierSuspected;

    return fr;
}

void TimeCalibration::applyFit(const FitResult &fr)
{
    if (!fr.ok)
        return;
    offsetMs = fr.offsetMs;
    rate = fr.rate;
    sigmaRate = fr.sigmaRate;
    rateApplied = fr.rateSignificant && fr.rateSane;
}
