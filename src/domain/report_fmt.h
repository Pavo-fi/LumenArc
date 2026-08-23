/**
 * @file report_fmt.h
 * @brief P-28 报告模块：人读格式化工具（header-only，service/builder/测试共用）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-23
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include "time_calibration.h"

#include <QDateTime>
#include <QString>
#include <QStringList>

namespace reportfmt {

inline QString fmtWall(qint64 ms)
{
    if (ms <= 0)
        return QString();
    return QDateTime::fromMSecsSinceEpoch(ms)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

inline QString fmtDuration(qint64 ms)
{
    const qint64 s = ms / 1000;
    const qint64 h = s / 3600, m = (s % 3600) / 60, sec = s % 60;
    if (h > 0)
        return QStringLiteral("%1 时 %2 分 %3 秒").arg(h).arg(m).arg(sec);
    if (m > 0)
        return QStringLiteral("%1 分 %2 秒").arg(m).arg(sec);
    return QStringLiteral("%1 秒").arg(sec);
}

inline QString fmtSizeMB(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024)
        return QStringLiteral("%1 GB").arg(bytes / 1024.0 / 1024 / 1024, 0, 'f', 2);
    return QStringLiteral("%1 MB").arg(bytes / 1024.0 / 1024, 0, 'f', 1);
}

/// 时间差人读：正=监控慢（显示落后北京时间），负=快
inline QString fmtTimeDiff(qint64 diffMs)
{
    if (diffMs == 0)
        return QStringLiteral("一致");
    const QString dir = diffMs > 0 ? QStringLiteral("慢") : QStringLiteral("快");
    qint64 a = qAbs(diffMs);
    const qint64 h = a / 3600000; a %= 3600000;
    const qint64 m = a / 60000;   a %= 60000;
    const double s = a / 1000.0;
    QStringList parts;
    if (h) parts << QStringLiteral("%1 小时").arg(h);
    if (m) parts << QStringLiteral("%1 分").arg(m);
    if (s > 0.0005 || parts.isEmpty())
        parts << QStringLiteral("%1 秒").arg(s, 0, 'f', 1);
    return dir + QStringLiteral(" ") + parts.join(QStringLiteral(" "));
}

inline QString calibWayText(TimeCalibration::Source source)
{
    using S = TimeCalibration::Source;
    switch (source) {
    case S::Manual:        return QStringLiteral("手动标定");
    case S::Ocr:           return QStringLiteral("OCR 自动识别");
    case S::AbsStart:      return QStringLiteral("文件名绝对起始时间");
    case S::Inherited:     return QStringLiteral("前处理产物继承");
    case S::CrossCamEvent: return QStringLiteral("多机同事件间接校时");
    case S::None:          break;
    }
    return QStringLiteral("未校时");
}

} // namespace reportfmt
