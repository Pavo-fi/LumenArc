/**
 * @file filename_timestamp.cpp
 * @brief 文件名时间戳/通道号解析实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "filename_timestamp.h"

#include <QDateTime>
#include <QRegularExpression>

namespace {

// 值域校验（与 probe_timestamps.py 一致：时 0-23、分秒 0-59、月 1-12、日 1-31、
// 年 2000~now+1d）
bool validRange(int y, int mo, int d, int h, int mi, int s)
{
    const int maxYear = QDateTime::currentDateTime().date().year() + 1;
    return y >= 2000 && y <= maxYear
        && mo >= 1 && mo <= 12 && d >= 1 && d <= 31
        && h >= 0 && h <= 23 && mi >= 0 && mi <= 59 && s >= 0 && s <= 59;
}

qint64 toEpochMs(int y, int mo, int d, int h, int mi, int s, int ms)
{
    QDate date(y, mo, d);
    QTime time(h, mi, s, ms);
    if (!date.isValid() || !time.isValid())
        return 0;
    // 监控文件名时间为本地墙钟（无时区语义），按本地时间解释
    return QDateTime(date, time, Qt::LocalTime).toMSecsSinceEpoch();
}

} // namespace

FilenameTimestamp parseFilenameTimestamp(const QString &fileName)
{
    FilenameTimestamp out;

    static const QRegularExpression reM1(
        R"(CH(\d+)[_-](\d{8})[_-](\d{6}))",
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression reM5(
        R"((\d{8})[_-](\d{6})[_-](\d{3}))");
    static const QRegularExpression reM2(
        R"((\d{8})[_-](\d{6}))");
    static const QRegularExpression reM3(
        R"((\d{14}))");
    static const QRegularExpression reM4(
        R"((\d{4})-(\d{2})-(\d{2})[ _](\d{2})-(\d{2})-(\d{2}))");
    // 通道号补充模式（无时间戳时也可单独提供分组依据）
    static const QRegularExpression reChannelIpc(
        R"(IPC[_-]?(\d+))", QRegularExpression::CaseInsensitiveOption);

    auto trySet = [&](const QRegularExpressionMatch &m, int patternId,
                      int y, int mo, int d, int h, int mi, int s, int ms,
                      const QString &channel) -> bool {
        if (!validRange(y, mo, d, h, mi, s))
            return false;
        const qint64 epoch = toEpochMs(y, mo, d, h, mi, s, ms);
        if (epoch <= 0)
            return false;
        out.epochMs = epoch;
        out.patternId = patternId;
        out.rawText = m.captured(0);
        out.channel = channel;
        return true;
    };

    // M1: CH01_20240701_120000（含通道）
    auto m = reM1.match(fileName);
    if (m.hasMatch()) {
        const QString ch = QStringLiteral("CH") + m.captured(1);
        const QString &dt = m.captured(2), &tm = m.captured(3);
        if (trySet(m, 1, dt.left(4).toInt(), dt.mid(4, 2).toInt(), dt.mid(6, 2).toInt(),
                   tm.left(2).toInt(), tm.mid(2, 2).toInt(), tm.right(2).toInt(), 0, ch))
            return out;
    }
    // M5: 20240701_120000_500（含毫秒，先于 M2）
    m = reM5.match(fileName);
    if (m.hasMatch()) {
        const QString &dt = m.captured(1), &tm = m.captured(2);
        if (trySet(m, 5, dt.left(4).toInt(), dt.mid(4, 2).toInt(), dt.mid(6, 2).toInt(),
                   tm.left(2).toInt(), tm.mid(2, 2).toInt(), tm.right(2).toInt(),
                   m.captured(3).toInt(), QString()))
            return out;
    }
    // M2: 20240701-120000 / 20240701_120000
    m = reM2.match(fileName);
    if (m.hasMatch()) {
        const QString &dt = m.captured(1), &tm = m.captured(2);
        if (trySet(m, 2, dt.left(4).toInt(), dt.mid(4, 2).toInt(), dt.mid(6, 2).toInt(),
                   tm.left(2).toInt(), tm.mid(2, 2).toInt(), tm.right(2).toInt(), 0,
                   QString()))
            return out;
    }
    // M3: 20240701120000
    m = reM3.match(fileName);
    if (m.hasMatch()) {
        const QString &a = m.captured(1);
        if (trySet(m, 3, a.left(4).toInt(), a.mid(4, 2).toInt(), a.mid(6, 2).toInt(),
                   a.mid(8, 2).toInt(), a.mid(10, 2).toInt(), a.right(2).toInt(), 0,
                   QString()))
            return out;
    }
    // M4: 2024-07-01 12-00-00
    m = reM4.match(fileName);
    if (m.hasMatch()) {
        if (trySet(m, 4, m.captured(1).toInt(), m.captured(2).toInt(), m.captured(3).toInt(),
                   m.captured(4).toInt(), m.captured(5).toInt(), m.captured(6).toInt(), 0,
                   QString()))
            return out;
    }

    // 未命中时间戳：仍尝试捕获通道号（分组输入）
    m = reChannelIpc.match(fileName);
    if (m.hasMatch())
        out.channel = m.captured(1);
    return out;
}
