/**
 * @file live_source.cpp
 * @brief 监控直播源地址组装实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-11
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "domain/live/live_source.h"

namespace live {

namespace {

/// 是否 RFC 3986 unreserved（无需转义）
bool isUnreserved(QChar c)
{
    const char16_t u = c.unicode();
    return (u >= u'A' && u <= u'Z')
        || (u >= u'a' && u <= u'z')
        || (u >= u'0' && u <= u'9')
        || u == u'-' || u == u'.' || u == u'_' || u == u'~';
}

} // namespace

bool LiveSourceConfig::isComplete() const
{
    if (!urlOverride.trimmed().isEmpty())
        return true;
    return !host.trimmed().isEmpty()
        && port > 0 && port <= 65535
        && channel >= 1
        && !username.isEmpty();
}

QString LiveSourceConfig::displayName() const
{
    if (!urlOverride.trimmed().isEmpty())
        return urlOverride.trimmed();
    const QString kindText = (kind == StreamKind::Sub)
        ? QStringLiteral("子码流") : QStringLiteral("主码流");
    return QStringLiteral("%1 · 通道 %2 · %3")
        .arg(host.trimmed())
        .arg(channel)
        .arg(kindText);
}

QString percentEncodeUserInfo(const QString &s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar c : s) {
        if (isUnreserved(c)) {
            out.append(c);
        } else {
            // userinfo 需要转义为 UTF-8 字节的百分号形式
            const QByteArray utf8 = QString(c).toUtf8();
            for (const char b : utf8) {
                out.append(QLatin1Char('%'));
                out.append(QString::number(static_cast<unsigned char>(b), 16)
                               .rightJustified(2, QLatin1Char('0')).toUpper());
            }
        }
    }
    return out;
}

QString buildDahuaRtspUrl(const LiveSourceConfig &cfg)
{
    if (!cfg.urlOverride.trimmed().isEmpty())
        return cfg.urlOverride.trimmed();
    if (!cfg.isComplete())
        return QString();

    const QString user = percentEncodeUserInfo(cfg.username);
    const QString pass = percentEncodeUserInfo(cfg.password);
    const int subtype = (cfg.kind == StreamKind::Sub) ? 1 : 0;

    return QStringLiteral("rtsp://%1:%2@%3:%4/cam/realmonitor?channel=%5&subtype=%6")
        .arg(user, pass, cfg.host.trimmed())
        .arg(cfg.port)
        .arg(cfg.channel)
        .arg(subtype);
}

QString resolveStreamUrl(const LiveSourceConfig &cfg)
{
    if (!cfg.urlOverride.trimmed().isEmpty())
        return cfg.urlOverride.trimmed();
    return buildDahuaRtspUrl(cfg);
}

QString maskedStreamUrl(const LiveSourceConfig &cfg)
{
    if (!cfg.urlOverride.trimmed().isEmpty()) {
        const QString u = cfg.urlOverride.trimmed();
        // 掩码 URL 中 user:pass@ 的密码段
        const int at = u.indexOf(QLatin1Char('@'));
        const int scheme = u.indexOf(QStringLiteral("://"));
        if (at > 0 && scheme >= 0) {
            const int colon = u.indexOf(QLatin1Char(':'), scheme + 3);
            if (colon > 0 && colon < at)
                return u.left(colon + 1) + QStringLiteral("***") + u.mid(at);
        }
        return u;
    }
    if (!cfg.isComplete())
        return cfg.displayName();

    const QString user = percentEncodeUserInfo(cfg.username);
    const int subtype = (cfg.kind == StreamKind::Sub) ? 1 : 0;
    return QStringLiteral("rtsp://%1:***@%2:%3/cam/realmonitor?channel=%4&subtype=%5")
        .arg(user, cfg.host.trimmed())
        .arg(cfg.port)
        .arg(cfg.channel)
        .arg(subtype);
}

QString transportName(Transport t)
{
    return t == Transport::Udp ? QStringLiteral("udp") : QStringLiteral("tcp");
}

} // namespace live
