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
// P-98 秒级跳变对齐表：段内线性插值（相邻锚点约 1 秒）
// ---------------------------------------------------------------------------
qint64 TimeCalibration::tickWallOf(qint64 streamMs) const
{
    const int n = tickAnchors.size();
    if (n < kTickAnchorMin)
        return offsetMs;
    // 表外外推用**端部相邻段斜率**（加速导出件端部段斜率 ≈1.14，用 1.0 会低估
    // 墙钟推进；reviewer P2-1）
    if (streamMs <= tickAnchors.first().first) {
        const qint64 ds = tickAnchors.at(1).first - tickAnchors.first().first;
        const qint64 dw = tickAnchors.at(1).second - tickAnchors.first().second;
        const double r = (ds > 0) ? double(dw) / double(ds) : 1.0;
        return tickAnchors.first().second
               + static_cast<qint64>(std::llround(r * double(streamMs - tickAnchors.first().first)));
    }
    if (streamMs >= tickAnchors.last().first) {
        const int n = tickAnchors.size();
        const qint64 ds = tickAnchors.at(n - 1).first - tickAnchors.at(n - 2).first;
        const qint64 dw = tickAnchors.at(n - 1).second - tickAnchors.at(n - 2).second;
        const double r = (ds > 0) ? double(dw) / double(ds) : 1.0;
        return tickAnchors.last().second
               + static_cast<qint64>(std::llround(r * double(streamMs - tickAnchors.last().first)));
    }
    int lo = 0, hi = n - 1;                 // 最后一个 first <= streamMs
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (tickAnchors.at(mid).first <= streamMs)
            lo = mid;
        else
            hi = mid - 1;
    }
    const qint64 s0 = tickAnchors.at(lo).first;
    const qint64 w0 = tickAnchors.at(lo).second;
    const qint64 s1 = tickAnchors.at(lo + 1).first;
    const qint64 w1 = tickAnchors.at(lo + 1).second;
    if (s1 <= s0)
        return w0;
    return w0 + static_cast<qint64>(std::llround(
        double(w1 - w0) * double(streamMs - s0) / double(s1 - s0)));
}

qint64 TimeCalibration::tickStreamOf(qint64 wallMs) const
{
    const int n = tickAnchors.size();
    if (n < kTickAnchorMin)
        return 0;
    if (wallMs <= tickAnchors.first().second) {
        const qint64 ds = tickAnchors.at(1).first - tickAnchors.first().first;
        const qint64 dw = tickAnchors.at(1).second - tickAnchors.first().second;
        const double r = (dw > 0) ? double(ds) / double(dw) : 1.0;
        return tickAnchors.first().first
               + static_cast<qint64>(std::llround(r * double(wallMs - tickAnchors.first().second)));
    }
    if (wallMs >= tickAnchors.last().second) {
        const int n = tickAnchors.size();
        const qint64 ds = tickAnchors.at(n - 1).first - tickAnchors.at(n - 2).first;
        const qint64 dw = tickAnchors.at(n - 1).second - tickAnchors.at(n - 2).second;
        const double r = (dw > 0) ? double(ds) / double(dw) : 1.0;
        return tickAnchors.last().first
               + static_cast<qint64>(std::llround(r * double(wallMs - tickAnchors.last().second)));
    }
    int lo = 0, hi = n - 1;                 // 最后一个 second <= wallMs
    while (lo < hi) {
        const int mid = (lo + hi + 1) / 2;
        if (tickAnchors.at(mid).second <= wallMs)
            lo = mid;
        else
            hi = mid - 1;
    }
    const qint64 s0 = tickAnchors.at(lo).first;
    const qint64 w0 = tickAnchors.at(lo).second;
    const qint64 s1 = tickAnchors.at(lo + 1).first;
    const qint64 w1 = tickAnchors.at(lo + 1).second;
    if (w1 <= w0)
        return s0;
    return s0 + static_cast<qint64>(std::llround(
        double(s1 - s0) * double(wallMs - w0) / double(w1 - w0)));
}

void TimeCalibration::applyFitDecision(TimeCalibration &cal, bool noDriftCorrection)
{
    // v1.18.x（2026-09-26 顺德公安件实测「校时后还是错的」）：本函数是「第 1 步结果
    // 落到工作面」的唯一门控实现，onUseResult 与 onAdoptTruth* 共用——否则第 2 步
    // （对真实时间）会直接落库旧工作面，静默丢弃刚算出的三点结果。
    if (noDriftCorrection) {
        cal.rateApplied = false;
        return;
    }
    const FitResult fr = fit(cal.samples);
    if (fr.ok && fr.warning == FitWarning::RateInsane
        && rateChangeSelfConsistent(fr)) {
        // 自洽的大倍率 = 非实时导出件（画面时间 1.139× 播放进度），可确认采用
        cal.rateApplied = true;
        cal.speedVariant = true;
    }
}

bool TimeCalibration::absorbPendingFit(TimeCalibration &working,
                                       const TimeCalibration &fit,
                                       bool fitPending, bool noDriftCorrection)
{
    if (!fitPending || !fit.isValid() || fit.source != Source::Ocr
        || fit.piecewiseMode())
        return false;
    TimeCalibration cal = fit;
    applyFitDecision(cal, noDriftCorrection);
    working = cal;
    return true;
}

bool TimeCalibration::rateChangeSelfConsistent(const FitResult &fr)
{
    // |rate−1| 超可确认上限（50%）→ 必为错读（日期/上下午读错典型 ~2×）
    if (!fr.ok || std::fabs(fr.rate - 1.0) > kConfirmableRateDev)
        return false;
    if (fr.pointsUsed < 3)
        return true;   // 两点：无残差可判，交用户核对后确认
    return fr.maxResidualMs <= kOutlierResidualMs;
}

bool TimeCalibration::rawTextHourShort(const QString &rawText)
{
    // 找形如 H:MM:SS / HH:MM:SS 的时间段；H 只有一位 → 结构可疑。
    // （中文/全角冒号也认：OSD 常见全角；不引 QRegularExpression 保持 QtCore 轻依赖）
    static const QString kColons = QStringLiteral(":：");
    for (int i = 0; i + 4 < rawText.size(); ++i) {
        if (!rawText.at(i).isDigit())
            continue;
        // 单位数小时：前面不是数字（避免把 15 的 5 当小时）
        if (i > 0 && rawText.at(i - 1).isDigit())
            continue;
        if (!kColons.contains(rawText.at(i + 1)))
            continue;
        if (!rawText.at(i + 2).isDigit() || !rawText.at(i + 3).isDigit())
            continue;
        if (!kColons.contains(rawText.at(i + 4)))
            continue;
        // 到这里 = 单位数小时 + 两位分 + 冒号（够了；秒位不强求）
        return true;
    }
    return false;
}

int TimeCalibration::repairShortHourSamples(QVector<Sample> *samples)
{
    if (!samples || samples->size() < 2)
        return 0;
    // 干净点（两位小时）用来外推；不足两点则不动
    QVector<int> clean;
    for (int i = 0; i < samples->size(); ++i) {
        const Sample &s = samples->at(i);
        if (s.wallMs > 0
            && (s.rawText.isEmpty() || !rawTextHourShort(s.rawText)))
            clean.append(i);
    }
    if (clean.size() < 2)
        return 0;
    // 最小二乘（干净点）→ 期望墙钟
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    const double n = clean.size();
    for (int i : clean) {
        const double x = double(samples->at(i).streamMs);
        const double y = double(samples->at(i).wallMs);
        sx += x; sy += y; sxx += x * x; sxy += x * y;
    }
    const double den = n * sxx - sx * sx;
    const double rate = (std::fabs(den) > 1e-6) ? (n * sxy - sx * sy) / den : 1.0;
    const double off = (sy - rate * sx) / n;
    int fixed = 0;
    for (int i = 0; i < samples->size(); ++i) {
        Sample &s = (*samples)[i];   // at() 在 Qt6 返回 const 引用，需非 const 写入
        if (s.wallMs <= 0 || s.rawText.isEmpty()
            || !rawTextHourShort(s.rawText))
            continue;
        const QDateTime dt = QDateTime::fromMSecsSinceEpoch(s.wallMs);
        const int h = dt.time().hour();
        // 当日已过毫秒（含分/秒/毫秒）→ 拆成「当天零点」与「去掉小时后的余量」，
        // 换小时时**必须保留分秒毫秒**（首版误把整块减掉 → 15:00:02 变 15:00:00）。
        const qint64 msInDay = QTime(0, 0).msecsTo(dt.time());
        const qint64 dayStart = s.wallMs - msInDay;
        const qint64 todRest = msInDay - qint64(h) * 3600 * 1000;
        const double expect = off + rate * double(s.streamMs);
        qint64 best = -1;
        double bestDiff = 0;
        for (int cand : {h, h + 10}) {
            if (cand < 0 || cand > 23)
                continue;
            const qint64 w = dayStart + qint64(cand) * 3600 * 1000 + todRest;
            // 日期不变（同一天内的小时修正）；跨日由日期闸处理
            const double d = std::fabs(double(w) - expect);
            if (best < 0 || d < bestDiff) {
                best = w;
                bestDiff = d;
            }
        }
        // 只在与期望值 <2 小时时才采用（防把真·凌晨件改错）
        if (best > 0 && bestDiff < 2.0 * 3600 * 1000) {
            s.wallMs = best;
            s.ocrSuspicious = true;   // UI 标 ⚠：值由其余测点修正（原文留档）
            ++fixed;
        }
    }
    return fixed;
}

int TimeCalibration::dropStructurallyInvalid(QVector<Sample> *samples)
{
    if (!samples || samples->size() < 2)
        return 0;
    QVector<int> bad;
    for (int i = 0; i < samples->size(); ++i) {
        const Sample &s = samples->at(i);
        if (!s.rawText.isEmpty() && rawTextHourShort(s.rawText))
            bad.append(i);
    }
    if (bad.isEmpty() || samples->size() - bad.size() < 2)
        return 0;   // 剔了就凑不够两点 → 不动（宁可不剔也别把整单废掉）
    for (int i = bad.size() - 1; i >= 0; --i) {
        samples->remove(bad.at(i));
    }
    return bad.size();
}

int TimeCalibration::dropImplausibleDates(QVector<Sample> *samples,
                                          qint64 maxDevMs)
{
    if (!samples || samples->size() < 3 || maxDevMs <= 0)
        return 0;
    // 以墙钟中位数为中心（不受单个错读点影响）
    QVector<qint64> walls;
    for (const Sample &s : *samples)
        if (s.wallMs > 0)
            walls.append(s.wallMs);
    if (walls.size() < 3)
        return 0;
    std::sort(walls.begin(), walls.end());
    const qint64 median = walls.at(walls.size() / 2);
    int dropped = 0;
    for (int i = samples->size() - 1; i >= 0; --i) {
        const Sample &s = samples->at(i);
        if (s.wallMs <= 0)
            continue;
        if (std::llabs(s.wallMs - median) > maxDevMs) {
            samples->remove(i);
            ++dropped;
        }
    }
    return dropped;
}

TimeCalibration::FitResult TimeCalibration::fitDroppingWorstOutlier(
    const QVector<Sample> &samples, int *droppedIndex)
{
    if (droppedIndex)
        *droppedIndex = -1;
    const FitResult base = fit(samples);
    if (!base.ok || base.pointsUsed < 3 || base.maxResidualMs <= kOutlierResidualMs)
        return base;   // 无离群（或点太少）→ 原文

    // 找“用了的”样本里残差最大的那个（按首次拟合的 offset/rate 计残差）
    int worst = -1;
    double worstRes = 0.0;
    for (int i = 0; i < samples.size(); ++i) {
        const Sample &s = samples.at(i);
        if (!s.used || s.streamMs < 0)
            continue;
        const double pred = double(base.offsetMs)
            + base.rate * double(s.streamMs);
        const double res = std::fabs(pred - double(s.wallMs));
        if (res > worstRes) {
            worstRes = res;
            worst = i;
        }
    }
    if (worst < 0)
        return base;

    QVector<Sample> trimmed = samples;
    trimmed[worst].used = false;
    const FitResult red = fit(trimmed);
    // 剔除后仍需可用（≥2 点）且残差收敛——否则宁可不剔（防把真变速当错读）
    if (!red.ok || red.pointsUsed < 2
        || (red.maxResidualMs > kOutlierResidualMs && red.pointsUsed >= 3))
        return base;
    if (droppedIndex)
        *droppedIndex = worst;
    return red;
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
    case Source::CrossCamEvent: return QStringLiteral("crosscamevent");
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
    if (s == QLatin1String("crosscamevent")) return Source::CrossCamEvent;
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
    if (!eventAnchors.isEmpty()) {
        QJsonArray arr;
        for (const auto &a : eventAnchors)
            arr.append(a.toJson());
        o[QStringLiteral("event_anchors")] = arr;   // P-73 溯源链（老读取端忽略未知键）
    }
    o[QStringLiteral("truthOffsetMs")] = static_cast<double>(truthOffsetMs);
    o[QStringLiteral("truthSet")] = truthSet;
    o[QStringLiteral("truthCheckedAtMs")] = static_cast<double>(truthCheckedAtMs);
    if (!calibNote.isEmpty())
        o[QStringLiteral("calib_note")] = calibNote;   // v1.15.3 差值注记（只加不改）
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
    if (piecewise.isValid()) {        o[QStringLiteral("piecewise")] = piecewise.toJson();
        o[QStringLiteral("piecewiseApplied")] = piecewiseApplied;
        o[QStringLiteral("speedVariant")] = speedVariant;
        o[QStringLiteral("boundaryCount")] = boundaryCount;
        o[QStringLiteral("totalWallSpanSec")] = totalWallSpanSec;
        o[QStringLiteral("audioConsistent")] = audioConsistent;
        o[QStringLiteral("audioKnown")] = audioKnown;
    }
    // P-98 秒级跳变对齐表（delta 编码；老读取端忽略未知键，向后兼容）
    if (tickMode()) {
        QJsonObject tm;
        tm[QStringLiteral("stream0")] = static_cast<double>(tickAnchors.first().first);
        tm[QStringLiteral("wall0")] = static_cast<double>(tickAnchors.first().second);
        QJsonArray sd, wd;
        qint64 ps = tickAnchors.first().first;
        qint64 pw = tickAnchors.first().second;
        for (int i = 1; i < tickAnchors.size(); ++i) {
            sd.append(static_cast<double>(tickAnchors.at(i).first - ps));
            wd.append(static_cast<double>(tickAnchors.at(i).second - pw));
            ps = tickAnchors.at(i).first;
            pw = tickAnchors.at(i).second;
        }
        tm[QStringLiteral("streamDelta")] = sd;
        tm[QStringLiteral("wallDelta")] = wd;
        if (tickSkippedSeconds > 0.0)
            tm[QStringLiteral("skippedSeconds")] = tickSkippedSeconds;
        o[QStringLiteral("tick_map")] = tm;
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
    const QJsonArray ea = o[QStringLiteral("event_anchors")].toArray();
    for (const auto &v : ea)
        c.eventAnchors.append(eventcalib::EventAnchor::fromJson(v.toObject()));
    c.truthOffsetMs = static_cast<qint64>(o[QStringLiteral("truthOffsetMs")].toDouble());
    c.truthSet = o[QStringLiteral("truthSet")].toBool();
    c.truthCheckedAtMs = static_cast<qint64>(o[QStringLiteral("truthCheckedAtMs")].toDouble());
    c.truthNote = o[QStringLiteral("truthNote")].toString();
    c.calibNote = o[QStringLiteral("calib_note")].toString();   // v1.15.3
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
    // P-98 秒级跳变对齐表（老文件无此字段 → 不生效，行为不变）
    if (o.contains(QStringLiteral("tick_map"))) {
        const QJsonObject tm = o[QStringLiteral("tick_map")].toObject();
        const QJsonArray sd = tm[QStringLiteral("streamDelta")].toArray();
        const QJsonArray wd = tm[QStringLiteral("wallDelta")].toArray();
        if (!sd.isEmpty() && sd.size() == wd.size()) {
            qint64 ps = static_cast<qint64>(
                tm[QStringLiteral("stream0")].toDouble());
            qint64 pw = static_cast<qint64>(
                tm[QStringLiteral("wall0")].toDouble());
            if (ps >= 0 && pw > 0) {
                c.tickAnchors.append(qMakePair(ps, pw));
                for (int i = 0; i < sd.size(); ++i) {
                    ps += static_cast<qint64>(sd.at(i).toDouble());
                    pw += static_cast<qint64>(wd.at(i).toDouble());
                    c.tickAnchors.append(qMakePair(ps, pw));
                }
                c.tickSkippedSeconds =
                    tm[QStringLiteral("skippedSeconds")].toDouble(0.0);
            }
        }
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
