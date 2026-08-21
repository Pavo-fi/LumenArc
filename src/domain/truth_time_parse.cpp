/**
 * @file truth_time_parse.cpp
 * @brief TruthTimeParse 实现（约定见头文件；数字/中文混排均支持）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-21
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "domain/truth_time_parse.h"

#include <QDateTime>
#include <QRegularExpression>

namespace {

/// 日期：2026年07月22日 / 2026-07-22 / 2026/7/22 / 2026.07.22（日/号 可省）
const QRegularExpression &dateRe()
{
    static const QRegularExpression re(
        QStringLiteral("(\\d{4})\\s*[-/年.]\\s*(\\d{1,2})\\s*[-/月.]\\s*(\\d{1,2})\\s*[日号]?"));
    return re;
}

/// 带秒时间：12:25:47 / 12：25：47（全角冒号归一化后）/ 可带 .500 或 :500 毫秒；
/// 前后不得再贴数字/冒号（防 112:39:41 碎片错配）
const QRegularExpression &timeRe()
{
    static const QRegularExpression re(
        QStringLiteral("(?<![\\d:])(\\d{1,2})\\s*:\\s*(\\d{1,2})\\s*:\\s*(\\d{1,2})"
                       "(?:\\s*[.:]\\s*(\\d{1,3}))?(?![\\d:])"));
    return re;
}

/// 无秒时间（12:39，手机状态栏式）——命中即给「需含秒」指引
const QRegularExpression &timeNoSecRe()
{
    static const QRegularExpression re(
        QStringLiteral("(?<![\\d:])(\\d{1,2})\\s*:\\s*(\\d{2})(?!\\s*[:\\d])(?![\\d])"));
    return re;
}

QString normalize(QString t)
{
    t.replace(QStringLiteral("："), QStringLiteral(":"))
     .replace(QChar(0x3000), QLatin1Char(' '));   // 全角空格
    return t;
}

bool validDate(int y, int mo, int d)
{
    if (y < 2000 || y > QDate::currentDate().year() + 1)
        return false;
    return QDate(y, mo, d).isValid();
}

bool validTime(int h, int mi, int s)
{
    return h >= 0 && h <= 23 && mi >= 0 && mi <= 59 && s >= 0 && s <= 59;
}

TruthTimeParse makeResult(const QDate &date, int h, int mi, int s, int ms,
                          bool dateFromText, const QString &matched,
                          const QString &invalidSource)
{
    TruthTimeParse r;
    if (!validTime(h, mi, s)) {
        r.error = QStringLiteral("invalid:") + invalidSource;
        return r;
    }
    const QDateTime dt(date, QTime(h, mi, s, ms), Qt::LocalTime);
    if (!dt.isValid()) {
        r.error = QStringLiteral("invalid:") + invalidSource;
        return r;
    }
    r.ok = true;
    r.wallMs = dt.toMSecsSinceEpoch();
    r.dateFromText = dateFromText;
    r.matchedText = matched;
    return r;
}

} // namespace

TruthTimeParse parseTruthTimeText(const QStringList &ocrLines,
                                  const QDate &assumeDate)
{
    TruthTimeParse fail;

    // 每行：日期命中 + 带秒时间命中
    struct LineHit {
        QString raw;
        bool hasDate = false;
        int y = 0, mo = 0, d = 0;
        bool hasTime = false;
        int h = 0, mi = 0, s = 0, ms = 0;
    };
    QVector<LineHit> hits;
    hits.reserve(ocrLines.size());
    QString noSecondsText;   // 首个「时分无秒」行（错误指引用）
    QString invalidText;     // 首个形似但值域非法行

    for (const QString &raw0 : ocrLines) {
        const QString raw = normalize(raw0);
        LineHit lh;
        lh.raw = raw;
        const auto dm = dateRe().match(raw);
        if (dm.hasMatch()) {
            const int y = dm.captured(1).toInt();
            const int mo = dm.captured(2).toInt();
            const int d = dm.captured(3).toInt();
            if (validDate(y, mo, d)) {
                lh.hasDate = true;
                lh.y = y; lh.mo = mo; lh.d = d;
            } else if (invalidText.isEmpty())
                invalidText = raw;
        }
        const auto tm = timeRe().match(raw);
        if (tm.hasMatch()) {
            const int h = tm.captured(1).toInt();
            const int mi = tm.captured(2).toInt();
            const int s = tm.captured(3).toInt();
            int ms = 0;
            if (!tm.captured(4).isEmpty())
                ms = tm.captured(4).leftJustified(3, QLatin1Char('0')).left(3).toInt();
            if (validTime(h, mi, s)) {
                lh.hasTime = true;
                lh.h = h; lh.mi = mi; lh.s = s; lh.ms = ms;
            } else if (invalidText.isEmpty())
                invalidText = raw;
        } else if (noSecondsText.isEmpty()) {
            const auto ns = timeNoSecRe().match(raw);
            if (ns.hasMatch())
                noSecondsText = raw;
        }
        hits.append(lh);
    }

    // ① 单行完整（日期+带秒时间）：最可信
    for (const LineHit &lh : hits)
        if (lh.hasDate && lh.hasTime)
            return makeResult(QDate(lh.y, lh.mo, lh.d), lh.h, lh.mi, lh.s, lh.ms,
                              true, lh.raw, lh.raw);

    // ② 跨行组合：日期行 + 时间行（授时网页「现在是…第30周」+「12:39:41」）
    const LineHit *dateLine = nullptr;
    const LineHit *timeLine = nullptr;
    for (const LineHit &lh : hits) {
        if (!dateLine && lh.hasDate)
            dateLine = &lh;
        if (!timeLine && lh.hasTime)
            timeLine = &lh;
    }
    if (dateLine && timeLine)
        return makeResult(QDate(dateLine->y, dateLine->mo, dateLine->d),
                          timeLine->h, timeLine->mi, timeLine->s, timeLine->ms,
                          true, dateLine->raw + QStringLiteral(" | ") + timeLine->raw,
                          timeLine->raw);

    // ③ 纯时间 + 假定日期（框 1 同日；跨日疑义调用方处理）
    if (timeLine && assumeDate.isValid())
        return makeResult(assumeDate, timeLine->h, timeLine->mi, timeLine->s,
                          timeLine->ms, false, timeLine->raw, timeLine->raw);

    // 错误归因（C1 类型化）
    if (timeLine && !assumeDate.isValid()) {
        fail.error = QStringLiteral("nomatch");   // 有时间无日期且无可假定
        return fail;
    }
    if (!noSecondsText.isEmpty()) {
        fail.error = QStringLiteral("noseconds:") + noSecondsText;
        return fail;
    }
    if (!invalidText.isEmpty()) {
        fail.error = QStringLiteral("invalid:") + invalidText;
        return fail;
    }
    fail.error = QStringLiteral("nomatch");
    return fail;
}
