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

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>

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
    // v1.12.5 对时留档（有值才写；老读取端不识新增键，向后兼容）
    if (!truthSource.isEmpty())
        o[QStringLiteral("truthSource")] = truthSource;
    if (!truthImagePath.isEmpty())
        o[QStringLiteral("truthImagePath")] = truthImagePath;
    if (truthMonitorBox.isValid()) {
        const QRect r = truthMonitorBox;
        o[QStringLiteral("truthMonitorBox")] = QStringLiteral("%1,%2,%3,%4")
            .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
    }
    if (truthBeijingBox.isValid()) {
        const QRect r = truthBeijingBox;
        o[QStringLiteral("truthBeijingBox")] = QStringLiteral("%1,%2,%3,%4")
            .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
    }
    if (!truthMonitorText.isEmpty())
        o[QStringLiteral("truthMonitorText")] = truthMonitorText;
    if (!truthBeijingText.isEmpty())
        o[QStringLiteral("truthBeijingText")] = truthBeijingText;
    QJsonArray arr;
    for (const Sample &s : samples) {
        QJsonObject so;
        so[QStringLiteral("streamMs")] = static_cast<double>(s.streamMs);
        so[QStringLiteral("wallMs")] = static_cast<double>(s.wallMs);
        so[QStringLiteral("rawText")] = s.rawText;
        so[QStringLiteral("frameImg")] = s.frameImgPath;
        so[QStringLiteral("conf")] = s.conf;
        so[QStringLiteral("used")] = s.used;
        so[QStringLiteral("ocrSuspicious")] = s.ocrSuspicious;
        arr.append(so);
    }
    if (!arr.isEmpty())
        o[QStringLiteral("samples")] = arr;
    // v1.2.1：分段重建（变速/抽帧文件查表校时）
    if (piecewise.isValid()) {
        o[QStringLiteral("piecewise")] = piecewise.toJson();
        o[QStringLiteral("piecewiseApplied")] = piecewiseApplied;
        o[QStringLiteral("speedVariant")] = speedVariant;
        o[QStringLiteral("boundaryCount")] = boundaryCount;
        o[QStringLiteral("totalWallSpanSec")] = totalWallSpanSec;
        o[QStringLiteral("audioConsistent")] = audioConsistent;
        o[QStringLiteral("audioKnown")] = audioKnown;
    }
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
    // v1.12.5 对时留档（老文件无此字段 → 空，行为不变）
    c.truthSource = o[QStringLiteral("truthSource")].toString();
    c.truthImagePath = o[QStringLiteral("truthImagePath")].toString();
    const auto parseRect = [](const QJsonValue &v) {
        const QStringList p = v.toString().split(QLatin1Char(','));
        if (p.size() != 4)
            return QRect();
        return QRect(p[0].toInt(), p[1].toInt(), p[2].toInt(), p[3].toInt());
    };
    c.truthMonitorBox = parseRect(o[QStringLiteral("truthMonitorBox")]);
    c.truthBeijingBox = parseRect(o[QStringLiteral("truthBeijingBox")]);
    c.truthMonitorText = o[QStringLiteral("truthMonitorText")].toString();
    c.truthBeijingText = o[QStringLiteral("truthBeijingText")].toString();
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
        s.ocrSuspicious = so[QStringLiteral("ocrSuspicious")].toBool();
        c.samples.append(s);
    }
    // v1.2.1：分段重建字段（老文件无此字段 → piecewise 无效，行为不变）
    const QJsonArray parr = o[QStringLiteral("piecewise")].toArray();
    if (!parr.isEmpty()) {
        c.piecewise = PiecewiseTimeMap::fromJson(parr, 0);
        c.piecewiseApplied = o[QStringLiteral("piecewiseApplied")].toBool();
        c.speedVariant = o[QStringLiteral("speedVariant")].toBool();
        c.boundaryCount = o[QStringLiteral("boundaryCount")].toInt();
        c.totalWallSpanSec = o[QStringLiteral("totalWallSpanSec")].toDouble();
        c.audioConsistent = o[QStringLiteral("audioConsistent")].toBool(true);
        c.audioKnown = o[QStringLiteral("audioKnown")].toBool();
    }
    return c;
}

bool loadSidecarCalibration(const QString &videoPath,
                                     TimeCalibration *out, QString *warning)
{
    if (warning)
        warning->clear();
    const QString sidecarPath = videoPath + QStringLiteral(".lumencal.json");
    QFile f(sidecarPath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    if (root[QStringLiteral("version")].toInt() != 1)
        return false;
    const QJsonArray segs = root[QStringLiteral("segments")].toArray();
    if (segs.isEmpty())
        return false;

    // offset = 首段墙钟起点（Q-4：缺口时仍按首段线性，给警告）
    const qint64 wall0 = static_cast<qint64>(
        segs.first().toObject()[QStringLiteral("wallStartMs")].toDouble());
    if (wall0 <= 0)
        return false;

    // rate = 各段实测速率中位数（仅统计有实测的段）
    QVector<double> rates;
    for (const QJsonValue &v : segs) {
        const double r = v.toObject()[QStringLiteral("rate")].toDouble(1.0);
        if (std::fabs(r - 1.0) > 1e-12 && r > 0.5 && r < 2.0)
            rates.append(r);
    }
    double rate = 1.0;
    if (!rates.isEmpty()) {
        std::sort(rates.begin(), rates.end());
        rate = rates[rates.size() / 2];
    }

    TimeCalibration cal;
    cal.source = TimeCalibration::Source::Inherited;
    cal.offsetMs = wall0;
    cal.rate = rate;
    cal.rateApplied =
        std::fabs(rate - 1.0) > TimeCalibration::kMinSignificantRateDev;
    cal.dateKnown = true;
    cal.conf = 0.8;
    cal.calibratedAtMs = QDateTime::currentMSecsSinceEpoch();

    // v1.12.0（2026-08-20 拍板：校时反映到前处理产物时间轴）：分段锚点 →
    // 分段映射（查表校时）。拼接产物流内连续而墙钟在缺口处跳变，单条仿射
    // 必然在缺口后失真（此前 Q-4 只能警告"首段之后可能不准"）；分段映射
    // 使每段墙钟均按其画面时间锚定，缺口跳变即真实监控常态（仍进警告）。
    // 无墙钟段（wallStartMs<=0）不入表——其区间由前段延伸覆盖，与旧行为一致。
    {
        PiecewiseTimeMap pw;
        for (const QJsonValue &v : segs) {
            const QJsonObject s = v.toObject();
            TimeSegment ts;
            ts.streamStartMs = static_cast<qint64>(
                s[QStringLiteral("streamStartMs")].toDouble());
            ts.wallStartMs = static_cast<qint64>(
                s[QStringLiteral("wallStartMs")].toDouble());
            ts.rate = s[QStringLiteral("rate")].toDouble(1.0);
            if (ts.rate <= 0.0)
                ts.rate = 1.0;
            if (ts.wallStartMs > 0)
                pw.segments.append(ts);
            // v1.12.3：末段右边界（sidecar 逐段带 streamEndMs）→ 末段墙钟
            // 终点可算（segmentWallEndMs/gaps 完备）
            const qint64 segStreamEnd = static_cast<qint64>(
                s[QStringLiteral("streamEndMs")].toDouble());
            if (segStreamEnd > pw.streamEndMs)
                pw.streamEndMs = segStreamEnd;
        }
        if (!pw.segments.isEmpty()) {
            cal.piecewise = pw;
            cal.piecewiseApplied = true;
            for (const auto &t : pw.segments)
                if (std::fabs(t.rate - 1.0) > 1e-3) {
                    cal.speedVariant = true;   // 含抽帧/变速段（报告标注）
                    break;
                }
        }
    }

    // 缺口警告（Q-4：必进报告，UI 同步提示）
    const QJsonArray gaps = root[QStringLiteral("gaps")].toArray();
    if (warning && !gaps.isEmpty()) {
        qint64 maxGap = 0;
        for (const QJsonValue &v : gaps)
            maxGap = qMax(maxGap, qAbs(static_cast<qint64>(
                v.toObject()[QStringLiteral("gapWallMs")].toDouble())));
        *warning = QStringLiteral("gaps:%1:%2")
            .arg(gaps.size()).arg(maxGap);   // C1：类型化前缀，UI 解析展示
    }

    if (out)
        *out = cal;
    return true;
}
