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

// ---------------------------------------------------------------------------
// 序列化（.vla v8 META 的 time_calibration 对象；F5 三处同步）
// ---------------------------------------------------------------------------
QString TimeCalibration::sourceToString(Source s)
{
    switch (s) {
    case Source::Manual:    return QStringLiteral("manual");
    case Source::Ocr:       return QStringLiteral("ocr");
    case Source::AbsStart:  return QStringLiteral("absstart");
    case Source::Inherited: return QStringLiteral("inherited");
    case Source::None:      break;
    }
    return QStringLiteral("none");
}

TimeCalibration::Source TimeCalibration::sourceFromString(const QString &s)
{
    if (s == QLatin1String("manual"))    return Source::Manual;
    if (s == QLatin1String("ocr"))       return Source::Ocr;
    if (s == QLatin1String("absstart"))  return Source::AbsStart;
    if (s == QLatin1String("inherited")) return Source::Inherited;
    return Source::None;
}

QJsonObject TimeCalibration::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("source")] = sourceToString(source);
    o[QStringLiteral("offsetMs")] = static_cast<double>(offsetMs);
    o[QStringLiteral("rate")] = rate;
    o[QStringLiteral("rateApplied")] = rateApplied;
    o[QStringLiteral("conf")] = conf;
    o[QStringLiteral("dateKnown")] = dateKnown;
    o[QStringLiteral("sigmaRate")] = sigmaRate;
    o[QStringLiteral("calibratedAtMs")] = static_cast<double>(calibratedAtMs);
    o[QStringLiteral("truthOffsetMs")] = static_cast<double>(truthOffsetMs);
    o[QStringLiteral("truthSet")] = truthSet;
    o[QStringLiteral("truthCheckedAtMs")] = static_cast<double>(truthCheckedAtMs);
    if (!truthNote.isEmpty())
        o[QStringLiteral("truthNote")] = truthNote;
    QJsonArray arr;
    for (const Sample &s : samples) {
        QJsonObject so;
        so[QStringLiteral("streamMs")] = static_cast<double>(s.streamMs);
        so[QStringLiteral("wallMs")] = static_cast<double>(s.wallMs);
        so[QStringLiteral("rawText")] = s.rawText;
        so[QStringLiteral("frameImg")] = s.frameImgPath;
        so[QStringLiteral("conf")] = s.conf;
        so[QStringLiteral("used")] = s.used;
        arr.append(so);
    }
    if (!arr.isEmpty())
        o[QStringLiteral("samples")] = arr;
    return o;
}

TimeCalibration TimeCalibration::fromJson(const QJsonObject &o)
{
    TimeCalibration c;
    c.source = sourceFromString(o[QStringLiteral("source")].toString());
    c.offsetMs = static_cast<qint64>(o[QStringLiteral("offsetMs")].toDouble());
    c.rate = o[QStringLiteral("rate")].toDouble(1.0);
    c.rateApplied = o[QStringLiteral("rateApplied")].toBool();
    c.conf = o[QStringLiteral("conf")].toDouble();
    c.dateKnown = o[QStringLiteral("dateKnown")].toBool();
    c.sigmaRate = o[QStringLiteral("sigmaRate")].toDouble();
    c.calibratedAtMs = static_cast<qint64>(o[QStringLiteral("calibratedAtMs")].toDouble());
    c.truthOffsetMs = static_cast<qint64>(o[QStringLiteral("truthOffsetMs")].toDouble());
    c.truthSet = o[QStringLiteral("truthSet")].toBool();
    c.truthCheckedAtMs = static_cast<qint64>(o[QStringLiteral("truthCheckedAtMs")].toDouble());
    c.truthNote = o[QStringLiteral("truthNote")].toString();
    const QJsonArray arr = o[QStringLiteral("samples")].toArray();
    for (const QJsonValue &v : arr) {
        const QJsonObject so = v.toObject();
        Sample s;
        s.streamMs = static_cast<qint64>(so[QStringLiteral("streamMs")].toDouble(-1));
        s.wallMs = static_cast<qint64>(so[QStringLiteral("wallMs")].toDouble());
        s.rawText = so[QStringLiteral("rawText")].toString();
        s.frameImgPath = so[QStringLiteral("frameImg")].toString();
        s.conf = so[QStringLiteral("conf")].toDouble();
        s.used = so[QStringLiteral("used")].toBool(true);
        c.samples.append(s);
    }
    return c;
}
