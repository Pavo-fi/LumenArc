/**
 * @file sitemap_test_main.cpp
 * @brief P-74 点位图：SiteMapData JSON 回环 + 图框成品渲染断言
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-23
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#include "domain/site_map.h"
#include "infrastructure/site_map_render.h"

#include <QCoreApplication>
#include <QPainter>
#include <QTemporaryDir>

static int g_checks = 0;
static int g_failures = 0;
#define CHECK(cond, msg) do { \
    ++g_checks; \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); \
    } \
} while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ---- JSON 回环 ----
    SiteMapData d;
    d.baseImageRel = QStringLiteral("reports/assets/sitemap_base.png");
    SiteMapPoint p1;
    p1.laneRef = QStringLiteral("V001");
    p1.label = QStringLiteral("明景");
    p1.x = 0.32; p1.y = 0.61;
    p1.headingDeg = 215; p1.spreadDeg = 75; p1.radiusPct = 18;
    d.points << p1 << SiteMapPoint{};   // 第二点位默认构造
    const QByteArray json = QJsonDocument(d.toJson()).toJson();
    const SiteMapData back = SiteMapData::fromJson(
        QJsonDocument::fromJson(json).object());
    CHECK(back.points.size() == 2, "roundtrip count");
    CHECK(back.points[0].laneRef == QStringLiteral("V001"), "roundtrip laneRef");
    CHECK(qAbs(back.points[0].x - 0.32) < 1e-9, "roundtrip x");
    CHECK(qAbs(back.points[0].headingDeg - 215) < 1e-9, "roundtrip heading");
    CHECK(back.points[1].spreadDeg == 60, "default spread");

    // ---- 边界夹取 ----
    {
        QJsonObject o;
        o["x"] = 1.7; o["y"] = -0.3; o["spreadDeg"] = 400.0; o["radiusPct"] = 1.0;
        const SiteMapPoint p = SiteMapPoint::fromJson(o);
        CHECK(p.x == 1.0 && p.y == 0.0, "xy clamped");
        CHECK(p.spreadDeg == 180.0, "spread clamped");
        CHECK(p.radiusPct == 3.0, "radius clamped");
    }

    // ---- 案内存取 ----
    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "tmp dir");
    CHECK(d.save(tmp.path()), "save ok");
    const SiteMapData loaded = SiteMapData::load(tmp.path());
    CHECK(loaded.points.size() == 2, "load count");
    CHECK(loaded.baseImageRel == d.baseImageRel, "load baseImage");
    CHECK(SiteMapData::load(tmp.path() + QStringLiteral("/nonexist"))
              .points.isEmpty(), "load missing = empty");

    // ---- 图框成品渲染 ----
    QImage base(800, 600, QImage::Format_RGB888);
    base.fill(QColor(230, 230, 220));
    QPainter bp(&base);
    bp.fillRect(100, 100, 300, 200, QColor(180, 200, 220));   // 假建筑块
    bp.end();
    QHash<QString, QColor> colors;
    colors[QStringLiteral("V001")] = QColor(230, 159, 0);
    SiteMapData rd;
    SiteMapPoint rp;
    rp.laneRef = QStringLiteral("V001");
    rp.label = QStringLiteral("明景");
    rp.x = 0.5; rp.y = 0.5; rp.headingDeg = 45; rp.spreadDeg = 60; rp.radiusPct = 20;
    rd.points << rp;
    const QImage out = sitemaprender::renderFramed(
        rd, base, colors, QStringLiteral("20270813-广州天河-a"),
        QStringLiteral("张三"), QStringLiteral("李四"),
        QStringLiteral("2026-08-23"));
    CHECK(out.width() == 2480 && out.height() == 1754, "framed size A4@150dpi");
    // 外框线探针（y=20 行中点应为黑）
    const QColor framePx = out.pixelColor(1240, 20);
    CHECK(framePx.red() < 80 && framePx.green() < 80, "outer frame ink");
    // 标题栏区域非空白（有墨）
    int ink = 0;
    for (int y = 1520; y < 1660; y += 4)
        for (int x = 1800; x < 2400; x += 4) {
            const QColor c = out.pixelColor(x, y);
            if (c.red() < 200)
                ++ink;
        }
    CHECK(ink > 40, "title block has ink");
    // 扇面色探针：点位在底图中心（内容区中央），扇面 45° 朝向右下方向域内
    // 中心附近（半径内、非中心点本身）应检出橙色系像素
    bool sawOrange = false;
    for (int y = 700; y < 1100 && !sawOrange; y += 3)
        for (int x = 1000; x < 1500; x += 3) {
            const QColor c = out.pixelColor(x, y);
            if (c.red() > 200 && c.green() > 120 && c.green() < 200 && c.blue() < 120) {
                sawOrange = true;
                break;
            }
        }
    CHECK(sawOrange, "sector color visible");
    // 无底图渲染不崩
    const QImage nb = sitemaprender::renderFramed(rd, QImage(), colors,
        QStringLiteral("X"), QString(), QString(), QString());
    CHECK(nb.width() == 2480, "no-base render ok");

    fprintf(stderr, "sitemap_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
