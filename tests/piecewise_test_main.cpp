/**
 * @file piecewise_test_main.cpp
 * @brief 分段时间重建 domain 纯逻辑 headless 单测（v1.2.1）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-08
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 覆盖：
 *  - 单段（正常录像）：无边界、speedVariant=false、rate≈1.0
 *  - 三段拼接（斜率 1.0/2.0/1.0）：边界位置误差 ≤ 2s、段率准确
 *  - 段内噪声（±1s OSD 量化）不误报边界
 *  - 整体变速单段：speedVariant=true
 *  - 退化：空输入/单点/全同位置
 *  - wallMsOf/streamMsOf 往返一致性
 *  - JSON 序列化往返
 */
#include "domain/time_piecewise.h"

#include <QCoreApplication>
#include <cstdio>
#include <cmath>
#include <cstdlib>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { \
    ++g_checks; \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static PiecewiseSample pt(qint64 streamMs, qint64 wallMs, double conf = 0.95)
{
    PiecewiseSample s;
    s.streamMs = streamMs;
    s.wallMs = wallMs;
    s.conf = conf;
    return s;
}

/// 造段：从 wallStart 起，段内按 rate 每 dtMs 一个点（wall = wallStart + rate×Δs）
static void fillSegment(QVector<PiecewiseSample> &out, qint64 wallStart,
                        double rate, qint64 fromMs, qint64 toMs, qint64 stepMs)
{
    qint64 w = wallStart;
    for (qint64 s = fromMs; s <= toMs; s += stepMs) {
        out.append(pt(s, w + static_cast<qint64>(std::llround(
                            rate * static_cast<double>(s - fromMs)))));
    }
}

// ---------------------------------------------------------------------------
static void testEmptyAndSingle()
{
    PiecewiseDetectReport rep;
    auto map = PiecewiseTimeMap::detect({}, 60000, &rep);
    CHECK(!map.isValid());
    CHECK(rep.segmentCount == 0);

    map = PiecewiseTimeMap::detect({pt(1000, 1784700002000LL)}, 60000, &rep);
    CHECK(map.isValid());
    CHECK(map.size() == 1);
    CHECK(map.segments[0].rate == 1.0);
    CHECK(map.segments[0].streamStartMs == 1000);
    CHECK(!rep.speedVariant);

    // 无效测点过滤
    map = PiecewiseTimeMap::detect({pt(-1, 0), pt(1000, 0)}, 60000, &rep);
    CHECK(!map.isValid());
}

static void testSingleSegmentNormal()
{
    // 正常录像：25fps，rate=1.0，跨度 1 小时
    QVector<PiecewiseSample> pts;
    const qint64 base = 1784700002000LL;
    fillSegment(pts, base, 1.0, 0, 3600000, 60000);
    PiecewiseDetectReport rep;
    auto map = PiecewiseTimeMap::detect(pts, 3600000, &rep);
    CHECK(map.isValid());
    CHECK(map.size() == 1);
    CHECK(std::fabs(map.segments[0].rate - 1.0) < 1e-9);
    CHECK(!rep.speedVariant);
    CHECK(rep.boundaryCount == 0);
    CHECK(std::fabs(rep.totalWallSpanSec - 3600.0) < 1.0);
}

static void testOverallSpeedVariant()
{
    // 整体变速（无内部边界）：OSD 走 2 倍
    QVector<PiecewiseSample> pts;
    const qint64 base = 1784700002000LL;
    fillSegment(pts, base, 2.0, 0, 1800000, 60000);   // 30min 流内 = 60min OSD
    PiecewiseDetectReport rep;
    auto map = PiecewiseTimeMap::detect(pts, 1800000, &rep);
    CHECK(map.isValid());
    CHECK(map.size() == 1);
    CHECK(rep.speedVariant);
    CHECK(std::fabs(rep.overallRate - 2.0) < 1e-6);
}

static void testThreeSegmentBoundaries()
{
    // 三段：rate 1.0 (0–20min) / 2.0 (20–40min) / 1.0 (40–60min)
    const qint64 base = 1784700002000LL;
    QVector<PiecewiseSample> pts;
    fillSegment(pts, base, 1.0, 0, 1200000, 60000);                 // 段1
    const qint64 seg2StartWall = base + 1200000;                     // OSD 连续
    fillSegment(pts, seg2StartWall, 2.0, 1200000, 2400000, 60000);   // 段2
    const qint64 seg3StartWall = seg2StartWall + 2.0 * 1200000;      // 段2 OSD 跨度 40min
    fillSegment(pts, seg3StartWall, 1.0, 2400000, 3600000, 60000);   // 段3
    // 边界加密点（模拟两级采样：±5s 内 1s 步长）
    for (qint64 off = -5000; off <= 5000; off += 1000) {
        // 边界1：off<0 属段1（rate 1.0），off>0 属段2（rate 2.0）
        const double r1 = off < 0 ? 1.0 : 2.0;
        pts.append(pt(1200000 + off, base + 1200000
                          + static_cast<qint64>(std::llround(r1 * off))));
        // 边界2：off<0 属段2（rate 2.0），off>0 属段3（rate 1.0）
        const double r2 = off < 0 ? 2.0 : 1.0;
        pts.append(pt(2400000 + off, seg3StartWall
                          + static_cast<qint64>(std::llround(r2 * off))));
    }
    PiecewiseDetectReport rep;
    auto map = PiecewiseTimeMap::detect(pts, 3600000, &rep);
    CHECK(map.isValid());
    CHECK(rep.speedVariant);
    CHECK(map.size() == 3);
    if (map.size() == 3) {
        CHECK(std::fabs(map.segments[0].rate - 1.0) < 1e-6);
        CHECK(std::fabs(map.segments[1].rate - 2.0) < 1e-6);
        CHECK(std::fabs(map.segments[2].rate - 1.0) < 1e-6);
        // 边界位置误差 ≤ 2s（加密步长 1s 内）
        CHECK(std::llabs(map.segments[1].streamStartMs - 1200000) <= 2000);
        CHECK(std::llabs(map.segments[2].streamStartMs - 2400000) <= 2000);
    }
    CHECK(rep.boundaryCount == 2);
}

static void testNoiseNoFalseBoundary()
{
    // 单段 rate=1.0 + 每点 ±1s 随机噪声：不应误报边界
    const qint64 base = 1784700002000LL;
    QVector<PiecewiseSample> pts;
    for (qint64 s = 0; s <= 3600000; s += 60000) {
        const int jitter = (s / 60000) % 2 == 0 ? 1000 : -1000;
        pts.append(pt(s, base + s + jitter));
    }
    PiecewiseDetectReport rep;
    auto map = PiecewiseTimeMap::detect(pts, 3600000, &rep);
    CHECK(map.isValid());
    CHECK(map.size() == 1);
    CHECK(!rep.speedVariant);
    CHECK(std::fabs(map.segments[0].rate - 1.0) < 1e-3);
}

static void testRoundTrip()
{
    // 分段 wallMsOf/streamMsOf 往返一致
    const qint64 base = 1784700002000LL;
    QVector<PiecewiseSample> pts;
    fillSegment(pts, base, 1.0, 0, 1000000, 60000);
    fillSegment(pts, base + 1000000, 2.0, 1000000, 2000000, 60000);
    auto map = PiecewiseTimeMap::detect(pts, 2000000);
    CHECK(map.isValid());
    CHECK(map.size() == 2);
    for (qint64 s = 0; s <= 2000000; s += 37001) {
        const qint64 w = map.wallMsOf(s);
        const qint64 sBack = map.streamMsOf(w);
        CHECK(std::llabs(sBack - s) <= 2000);   // 秒级量化内往返
    }
    // 末段反解（超出末段墙钟夹取）
    CHECK(map.streamMsOf(base + 1000000 + 2 * 500000) == 1500000);
}

static void testJsonRoundTrip()
{
    const qint64 base = 1784700002000LL;
    QVector<PiecewiseSample> pts;
    fillSegment(pts, base, 1.0, 0, 1000000, 60000);
    fillSegment(pts, base + 1000000, 1.5, 1000000, 2000000, 60000);
    auto map = PiecewiseTimeMap::detect(pts, 2000000);
    const QJsonArray arr = map.toJson();
    auto map2 = PiecewiseTimeMap::fromJson(arr, 2000000);
    CHECK(map2.size() == map.size());
    if (map2.size() == map.size()) {
        for (int i = 0; i < map.size(); ++i) {
            CHECK(map2.segments[i].streamStartMs == map.segments[i].streamStartMs);
            CHECK(map2.segments[i].wallStartMs == map.segments[i].wallStartMs);
            CHECK(std::fabs(map2.segments[i].rate - map.segments[i].rate) < 1e-9);
        }
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testEmptyAndSingle();
    testSingleSegmentNormal();
    testOverallSpeedVariant();
    testThreeSegmentBoundaries();
    testNoiseNoFalseBoundary();
    testRoundTrip();
    testJsonRoundTrip();
    fprintf(stderr, "piecewise_test: %d checks, %d failures\n",
            g_checks, g_failures);
    return g_failures ? 1 : 0;
}
