/**
 * @file preprocess_text.h
 * @brief 前处理-纯文本逻辑工具（header-only，可 headless 单测）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 集中放置可纯函数化的文本处理：
 *  - CSV RFC4180 转义（规范 F6）
 *  - concat demuxer 路径转义
 *  - ffmpeg -progress 输出解析（out_time_ms 单位是微秒，评审 R-5）
 *  - ISO8601 creation_time 解析 + 脏值过滤（§10.2）
 */
#pragma once

#include <QString>
#include <QDateTime>
#include <QRegularExpression>

namespace preprocess_text {

/// RFC4180 CSV 转义（规范 F6）：含逗号/引号/换行时双引号包裹，内部引号双写
inline QString csvEscape(const QString &field)
{
    if (field.contains(QLatin1Char(',') ) || field.contains(QLatin1Char('"'))
        || field.contains(QLatin1Char('\n')) || field.contains(QLatin1Char('\r'))) {
        QString out = field;
        out.replace(QLatin1Char('"'), QStringLiteral("\"\""));
        return QStringLiteral("\"") + out + QStringLiteral("\"");
    }
    return field;
}

/// concat demuxer list.txt 路径转义：统一正斜杠 + 单引号按规则转义
inline QString concatEscapePath(QString path)
{
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    path.replace(QStringLiteral("'"), QStringLiteral("'\\''"));
    return QStringLiteral("file '") + path + QStringLiteral("'");
}

/// 解析 ffmpeg -progress pipe:1 的一行。
/// out_time_ms 单位是【微秒】（ffmpeg 经典命名陷阱，评审 R-5）。
/// 返回毫秒；非时间行返回 -1。
inline qint64 parseFfmpegProgressMs(const QByteArray &line)
{
    if (line.startsWith("out_time_ms=")) {
        bool ok = false;
        const qint64 micros = line.mid(12).trimmed().toLongLong(&ok);
        return ok ? micros / 1000 : -1;
    }
    if (line.startsWith("out_time_us=")) {
        bool ok = false;
        const qint64 micros = line.mid(12).trimmed().toLongLong(&ok);
        return ok ? micros / 1000 : -1;
    }
    if (line.startsWith("out_time=")) {
        // 格式 HH:MM:SS.microsec
        const QByteArray v = line.mid(9).trimmed();
        const QList<QByteArray> parts = v.split(':');
        if (parts.size() == 3) {
            bool okH = false, okM = false, okS = false;
            const qint64 h = parts[0].toLongLong(&okH);
            const qint64 m = parts[1].toLongLong(&okM);
            const double s = parts[2].toDouble(&okS);
            if (okH && okM && okS)
                return (h * 3600 + m * 60) * 1000 + static_cast<qint64>(s * 1000.0);
        }
    }
    return -1;
}

/// ISO8601 creation_time 解析 + 脏值过滤（§10.2）。
/// 年份 2000 ~ now+1d 之外拒绝（如 2036-02-06 零值 bug）→ 返回 0。
/// 无时区标记按本地时间解释，tzMissing 置 true（调用方降置信 + 报告注明）。
inline qint64 parseCreationTimeMs(const QString &raw, bool *tzMissing = nullptr)
{
    if (tzMissing)
        *tzMissing = false;
    const QString s = raw.trimmed();
    if (s.isEmpty())
        return 0;
    QDateTime dt = QDateTime::fromString(s, Qt::ISODateWithMs);
    if (!dt.isValid())
        return 0;
    if (!s.endsWith(QLatin1Char('Z'), Qt::CaseInsensitive)
        && !s.contains(QRegularExpression(QStringLiteral("[+-]\\d{2}:?\\d{2}$")))) {
        // 无时区：按本地墙钟解释
        if (tzMissing)
            *tzMissing = true;
        dt = QDateTime(dt.date(), dt.time(), Qt::LocalTime);
    }
    const int year = dt.date().year();
    const int maxYear = QDateTime::currentDateTime().date().year() + 1;
    if (year < 2000 || year > maxYear)
        return 0;   // 脏值（含 2036 零值 bug）
    return dt.toMSecsSinceEpoch();
}

} // namespace preprocess_text
