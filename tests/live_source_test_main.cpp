/**
 * @file live_source_test_main.cpp
 * @brief 监控直播源地址组装 domain 纯逻辑 headless 单测
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-11
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 覆盖（C3：地址错误 = 连不上设备，属用户可见故障，必须断言）：
 *  - 主/子码流 RTSP 地址组装
 *  - userinfo 百分号编码（密码含 @ : / ? # 等破坏 URL 结构的字符）
 *  - urlOverride 优先级
 *  - 掩码地址不泄露密码
 *  - isComplete 必填校验
 *  - 传输协议名
 */
#include "domain/live/live_source.h"

#include <QCoreApplication>
#include <cstdio>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do { \
    ++g_checks; \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); \
    } \
} while (0)

#define CHECK_EQ(actual, expected, msg) do { \
    ++g_checks; \
    const QString _a = (actual); \
    const QString _e = (expected); \
    if (_a != _e) { \
        ++g_failures; \
        fprintf(stderr, "FAIL %s:%d %s\n  actual   = %s\n  expected = %s\n", \
                __FILE__, __LINE__, msg, \
                _a.toUtf8().constData(), _e.toUtf8().constData()); \
    } \
} while (0)

static live::LiveSourceConfig baseConfig()
{
    live::LiveSourceConfig c;
    c.host = QStringLiteral("192.168.1.108");
    c.port = 554;
    c.username = QStringLiteral("admin");
    c.password = QStringLiteral("admin123");
    c.channel = 1;
    c.kind = live::StreamKind::Main;
    return c;
}

static void testMainStreamUrl()
{
    const live::LiveSourceConfig c = baseConfig();
    CHECK_EQ(live::buildDahuaRtspUrl(c),
             QStringLiteral("rtsp://admin:admin123@192.168.1.108:554/cam/realmonitor?channel=1&subtype=0"),
             "主码流地址");
}

static void testSubStreamUrl()
{
    live::LiveSourceConfig c = baseConfig();
    c.channel = 7;
    c.kind = live::StreamKind::Sub;
    CHECK_EQ(live::buildDahuaRtspUrl(c),
             QStringLiteral("rtsp://admin:admin123@192.168.1.108:554/cam/realmonitor?channel=7&subtype=1"),
             "子码流地址（通道 7）");
}

static void testPercentEncoding()
{
    CHECK_EQ(live::percentEncodeUserInfo(QStringLiteral("abcXYZ019-._~")),
             QStringLiteral("abcXYZ019-._~"), "unreserved 原样保留");
    CHECK_EQ(live::percentEncodeUserInfo(QStringLiteral("p@ss:word/1")),
             QStringLiteral("p%40ss%3Aword%2F1"), "危险字符转义");

    // 含 @ 的密码不得破坏 user:pass@host 结构（只会出现一个 '@'）
    live::LiveSourceConfig c = baseConfig();
    c.password = QStringLiteral("a@b");
    const QString url = live::buildDahuaRtspUrl(c);
    CHECK_EQ(QString::number(url.count(QLatin1Char('@'))), QStringLiteral("1"),
             "密码中的 @ 必须被编码，URL 只保留一个 @");
    CHECK(url.contains(QStringLiteral("a%40b")), "密码已百分号编码");
}

static void testUrlOverride()
{
    live::LiveSourceConfig c = baseConfig();
    c.urlOverride = QStringLiteral("rtsp://user:pw@10.0.0.5:554/Streaming/Channels/101");
    CHECK_EQ(live::resolveStreamUrl(c), c.urlOverride, "urlOverride 优先");
    // override 时即便 host/user 为空也算完整
    live::LiveSourceConfig onlyOverride;
    onlyOverride.urlOverride = c.urlOverride;
    CHECK(onlyOverride.isComplete(), "仅填完整地址也视为配置完整");
    CHECK_EQ(live::buildDahuaRtspUrl(onlyOverride), c.urlOverride, "override 直通");
}

static void testMaskedUrlHidesPassword()
{
    const live::LiveSourceConfig c = baseConfig();
    const QString masked = live::maskedStreamUrl(c);
    CHECK(!masked.contains(QStringLiteral("admin123")), "掩码地址不得包含真实密码");
    CHECK(masked.contains(QStringLiteral("***")), "掩码地址含 ***");

    live::LiveSourceConfig ov = baseConfig();
    ov.urlOverride = QStringLiteral("rtsp://bob:secret@10.0.0.9:554/live");
    const QString ovMasked = live::maskedStreamUrl(ov);
    CHECK(!ovMasked.contains(QStringLiteral("secret")), "override 掩码隐藏密码");
    CHECK_EQ(ovMasked, QStringLiteral("rtsp://bob:***@10.0.0.9:554/live"), "override 掩码格式");
}

static void testCompleteness()
{
    live::LiveSourceConfig c = baseConfig();
    CHECK(c.isComplete(), "完整配置");

    live::LiveSourceConfig noHost = baseConfig();
    noHost.host.clear();
    CHECK(!noHost.isComplete(), "缺 host 不完整");

    live::LiveSourceConfig noUser = baseConfig();
    noUser.username.clear();
    CHECK(!noUser.isComplete(), "缺用户名不完整");

    live::LiveSourceConfig badPort = baseConfig();
    badPort.port = 0;
    CHECK(!badPort.isComplete(), "端口 0 不完整");

    live::LiveSourceConfig badCh = baseConfig();
    badCh.channel = 0;
    CHECK(!badCh.isComplete(), "通道 0 不完整");

    // 空密码允许（部分设备无密码）
    live::LiveSourceConfig emptyPw = baseConfig();
    emptyPw.password.clear();
    CHECK(emptyPw.isComplete(), "空密码仍视为完整");
}

static void testTransportName()
{
    CHECK_EQ(live::transportName(live::Transport::Tcp), QStringLiteral("tcp"), "TCP 名");
    CHECK_EQ(live::transportName(live::Transport::Udp), QStringLiteral("udp"), "UDP 名");
}

static void testDisplayName()
{
    live::LiveSourceConfig c = baseConfig();
    c.channel = 3;
    c.kind = live::StreamKind::Sub;
    const QString name = c.displayName();
    CHECK(name.contains(QStringLiteral("192.168.1.108")), "显示名含地址");
    CHECK(name.contains(QStringLiteral("3")), "显示名含通道");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    testMainStreamUrl();
    testSubStreamUrl();
    testPercentEncoding();
    testUrlOverride();
    testMaskedUrlHidesPassword();
    testCompleteness();
    testTransportName();
    testDisplayName();

    if (g_failures == 0)
        printf("lumenarc_live_source_test: %d checks passed\n", g_checks);
    else
        printf("lumenarc_live_source_test: %d/%d checks FAILED\n", g_failures, g_checks);
    return g_failures == 0 ? 0 : 1;
}
