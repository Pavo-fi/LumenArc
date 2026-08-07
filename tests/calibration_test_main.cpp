/**
 * @file calibration_test_main.cpp
 * @brief 校时仿射模型 domain 纯逻辑 headless 单测
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-05
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 覆盖（docs/V1_ERA_TECH_PLAN_CN.md §3.4）：
 *  - 单点/两点/三点拟合路径
 *  - 漂移显著性门控（3σ 与 30秒/天双阈）
 *  - 野点残差警告与剔除重拟合
 *  - 异常速率（OCR 误读）拒绝应用
 *  - 退化路径：空测点/全排除/同一流内位置
 *  - wallMsOf/streamMsOf 往返一致性、v7 旧格式迁移
 */
#include "domain/time_calibration.h"

#include <QCoreApplication>
#include <cstdio>
#include <cmath>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond) do { \
    ++g_checks; \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static TimeCalibration::Sample pt(qint64 streamMs, qint64 wallMs)
{
    TimeCalibration::Sample s;
    s.streamMs = streamMs;
    s.wallMs = wallMs;
    return s;
}

static const qint64 kOff = 1784700002000LL;  // 任意 epoch 起点

// ---------------------------------------------------------------------------
static void testEmptyAndExcluded()
{
    QVector<TimeCalibration::Sample> none;
    auto fr = TimeCalibration::fit(none);
    CHECK(!fr.ok && fr.pointsUsed == 0);

    // 全部排除 → 同样无效
    auto s1 = pt(0, kOff), s2 = pt(60000, kOff + 60000);
    s1.used = false; s2.used = false;
    fr = TimeCalibration::fit({s1, s2});
    CHECK(!fr.ok && fr.pointsUsed == 0);

    // 失败拟合不得改写现有校时值
    TimeCalibration c;
    c.offsetMs = 12345;
    c.applyFit(fr);
    CHECK(c.offsetMs == 12345 && c.rate == 1.0 && !c.rateApplied);
}

static void testSinglePoint()
{
    auto fr = TimeCalibration::fit({pt(5000, kOff + 5000)});
    CHECK(fr.ok && fr.pointsUsed == 1);
    CHECK(fr.rate == 1.0 && !fr.rateSignificant);
    CHECK(fr.offsetMs == kOff);

    TimeCalibration c;
    c.applyFit(fr);
    CHECK(!c.rateApplied);
    CHECK(c.wallMsOf(5000) == kOff + 5000);
    CHECK(c.wallMsOf(0) == kOff);
}

static void testTwoPointsConservative()
{
    // 两点、1 小时间隔、真实漂移 86.4 秒/天：
    // 拟合能解出速率，但 ±1s 假设误差下不显著 → 不应用（保守，正确行为）
    const double rate = 1.001;
    auto fr = TimeCalibration::fit({
        pt(0, kOff),
        pt(3600000, kOff + static_cast<qint64>(std::llround(rate * 3600000.0)))});
    CHECK(fr.ok && fr.pointsUsed == 2);
    CHECK(std::fabs(fr.rate - rate) < 1e-6);
    CHECK(fr.sigmaRate > 0);
    CHECK(!fr.rateSignificant);   // 两点无法区分漂移与 OCR 误差
}

static void testThreePointsDriftApplied()
{
    // 47 分钟跨度、每天偏快 34.56 秒（rate=1.0004），整毫秒取整的干净测点
    const double rate = 1.0004;
    const qint64 x1 = 1410000, x2 = 2820000;
    auto fr = TimeCalibration::fit({
        pt(0, kOff),
        pt(x1, kOff + static_cast<qint64>(std::llround(rate * x1))),
        pt(x2, kOff + static_cast<qint64>(std::llround(rate * x2)))});
    CHECK(fr.ok && fr.pointsUsed == 3);
    CHECK(std::fabs(fr.rate - rate) < 1e-6);
    CHECK(fr.rateSignificant && fr.rateSane);
    CHECK(fr.warning == TimeCalibration::FitWarning::None);

    TimeCalibration c;
    c.applyFit(fr);
    CHECK(c.rateApplied);
    CHECK(std::fabs(c.driftSecondsPerDay() - 34.56) < 0.5);
    // 各测点报时误差 ≤ 2ms
    CHECK(std::fabs(c.wallMsOf(0) - kOff) <= 2);
    CHECK(std::fabs(c.wallMsOf(x2) - (kOff + std::llround(rate * x2))) <= 2);
    // 修正前后对比：不修正时中段误差 ≈ 563ms 量级，修正后 ≤ 2ms
    const qint64 naive = kOff + x2;              // rate=1 的推算
    const qint64 truth = kOff + std::llround(rate * x2);
    CHECK(truth - naive > 1000);                 // 漂移真实存在（>1s）
}

static void testNoDriftNotSignificant()
{
    const qint64 x1 = 1410000, x2 = 2820000;
    auto fr = TimeCalibration::fit({
        pt(0, kOff), pt(x1, kOff + x1), pt(x2, kOff + x2)});
    CHECK(fr.ok);
    CHECK(fr.rate == 1.0);
    CHECK(!fr.rateSignificant);
}

static void testMinThresholdBoundary()
{
    // 微小漂移需要足够长的跨度才能解析（OSD 秒级量化）：
    // 47 分钟跨度上 40 秒/天只产生 ~1.3ms 漂移，物理上不可检出。
    // 用 24 小时跨度测试 30 秒/天阈值的门控行为：
    const qint64 x1 = 43200000, x2 = 86400000;   // 12h / 24h
    // 20 秒/天：可测（24h 漂移 20s）但低于 30 秒/天下限 → 不显著
    const double rLow = 1.0 + 20.0 / 86400000.0;
    auto fr = TimeCalibration::fit({
        pt(0, kOff),
        pt(x1, kOff + static_cast<qint64>(std::llround(rLow * x1))),
        pt(x2, kOff + static_cast<qint64>(std::llround(rLow * x2)))});
    CHECK(fr.ok && !fr.rateSignificant);
    CHECK(std::fabs(fr.rate - rLow) < 1e-9);     // 拟合仍精确解出（供报告）

    // 40 秒/天：超过下限且 σ≈0 → 显著
    const double rHigh = 1.0 + 40.0 / 86400000.0;
    fr = TimeCalibration::fit({
        pt(0, kOff),
        pt(x1, kOff + static_cast<qint64>(std::llround(rHigh * x1))),
        pt(x2, kOff + static_cast<qint64>(std::llround(rHigh * x2)))});
    CHECK(fr.ok && fr.rateSignificant);
}

static void testNoisyStrongDrift()
{
    // 残差噪声 ±200ms + 强漂移（172.8 秒/天）→ 仍显著
    const double rate = 1.002;
    const qint64 x1 = 1410000, x2 = 2820000;
    auto fr = TimeCalibration::fit({
        pt(0,  kOff + 200),
        pt(x1, kOff + static_cast<qint64>(std::llround(rate * x1)) - 150),
        pt(x2, kOff + static_cast<qint64>(std::llround(rate * x2)) + 100)});
    CHECK(fr.ok);
    CHECK(std::fabs(fr.rate - rate) < 1e-4);
    CHECK(fr.rateSignificant);
    CHECK(fr.sigmaRate > 0 && fr.sigmaOffsetMs > 0);
    CHECK(fr.maxResidualMs <= 250);
}

static void testOutlierExcludeRefit()
{
    const double rate = 1.0004;
    const qint64 x1 = 940000, x2 = 1880000, x3 = 2820000;
    auto outlier = pt(x2, kOff + static_cast<qint64>(std::llround(rate * x2)) + 10000); // 偏 10s
    QVector<TimeCalibration::Sample> samples = {
        pt(0, kOff),
        pt(x1, kOff + static_cast<qint64>(std::llround(rate * x1))),
        outlier,
        pt(x3, kOff + static_cast<qint64>(std::llround(rate * x3)))};

    auto fr = TimeCalibration::fit(samples);
    CHECK(fr.ok);
    CHECK(fr.maxResidualMs > TimeCalibration::kOutlierResidualMs);
    CHECK(fr.warning == TimeCalibration::FitWarning::OutlierSuspected);

    // 剔除野点重拟合 → 恢复干净
    samples[2].used = false;
    fr = TimeCalibration::fit(samples);
    CHECK(fr.ok && fr.pointsUsed == 3);
    CHECK(fr.warning == TimeCalibration::FitWarning::None);
    CHECK(std::fabs(fr.rate - rate) < 1e-6);
    CHECK(fr.rateSignificant);
}

static void testInsaneRateRejected()
{
    // OCR 误读日期（wall 翻倍）→ 速率荒谬，拒绝应用但保留拟合值供诊断
    auto fr = TimeCalibration::fit({
        pt(0, kOff), pt(3600000, kOff + 2 * 3600000)});
    CHECK(fr.ok && fr.rate == 2.0);
    CHECK(!fr.rateSane);
    CHECK(fr.warning == TimeCalibration::FitWarning::RateInsane);

    TimeCalibration c;
    c.applyFit(fr);
    CHECK(!c.rateApplied);              // 拒绝应用
    CHECK(c.effectiveRate() == 1.0);    // 换算按 1.0
    CHECK(c.rate == 2.0);               // 拟合值仍留档（报告/诊断）
}

static void testExcludeDownToSingle()
{
    auto s1 = pt(0, kOff);
    auto s2 = pt(1410000, kOff + 1410000);
    auto s3 = pt(2820000, kOff + 2820000);
    s2.used = false; s3.used = false;
    auto fr = TimeCalibration::fit({s1, s2, s3});
    CHECK(fr.ok && fr.pointsUsed == 1);
    CHECK(fr.rate == 1.0 && fr.offsetMs == kOff);
}

static void testSameStreamPosition()
{
    // 两个测点同一流内位置 → sxx=0 退化单点
    auto fr = TimeCalibration::fit({pt(5000, kOff + 5000), pt(5000, kOff + 5100)});
    CHECK(fr.ok && fr.pointsUsed == 1);
    CHECK(fr.rate == 1.0);
}

static void testRoundTrip()
{
    TimeCalibration c;
    c.source = TimeCalibration::Source::Ocr;
    c.offsetMs = kOff;
    c.rate = 1.0004;
    c.rateApplied = true;
    const qint64 x = 2820000;
    const qint64 wall = c.wallMsOf(x);
    CHECK(std::fabs(c.streamMsOf(wall) - x) <= 1);

    // rateApplied=false → 双向都按 1.0（一致退化）
    c.rateApplied = false;
    CHECK(c.wallMsOf(x) == kOff + x);
    CHECK(c.streamMsOf(kOff + x) == x);
}

static void testLegacyMigration()
{
    auto c = TimeCalibration::fromLegacyOffset(3600000);
    CHECK(c.source == TimeCalibration::Source::Manual);
    CHECK(!c.dateKnown);
    CHECK(c.rate == 1.0 && !c.rateApplied);
    CHECK(c.wallMsOf(5000) == 3605000);
}

static void testTruthOffset()
{
    // 北京时间校验：监控 06:00:02 时实际北京 06:05:32 → truthOffset = +330s
    TimeCalibration c;
    c.source = TimeCalibration::Source::Ocr;
    c.offsetMs = kOff;            // 监控钟模型：流内 0 = 监控 06:00:02
    c.dateKnown = true;
    c.truthOffsetMs = 330000;     // 人工校验得出
    c.truthSet = true;
    c.truthCheckedAtMs = kOff + 86400000;
    // 监控时间不变
    CHECK(c.wallMsOf(0) == kOff);
    // 北京时间 = 监控 + 330s（全局应用）
    CHECK(c.beijingMsOf(0) == kOff + 330000);
    CHECK(c.beijingMsOf(2820000) == kOff + 2820000 + 330000);
    // 未校验时 truthOffsetMs=0 → 北京时间 == 监控时间（安全退化）
    TimeCalibration d;
    d.offsetMs = kOff;
    CHECK(d.beijingMsOf(1000) == d.wallMsOf(1000));
}

static void testJsonRoundTrip()
{
    TimeCalibration c;
    c.source = TimeCalibration::Source::Ocr;
    c.offsetMs = kOff;
    c.rate = 1.0000004;
    c.rateApplied = true;
    c.conf = 0.95;
    c.dateKnown = true;
    c.sigmaRate = 1.2e-8;
    c.calibratedAtMs = kOff + 3600000;
    c.truthOffsetMs = 300000;
    c.truthSet = true;
    c.truthCheckedAtMs = kOff + 3700000;
    c.truthNote = QStringLiteral("与指挥中心对时");
    c.samples = {pt(0, kOff), pt(2820000, kOff + 2820000 + 1128)};
    c.samples[0].rawText = QStringLiteral("2026-07-22 06:00:02");
    c.samples[0].frameImgPath = QStringLiteral("evidence/a.png");
    c.samples[0].conf = 0.95;
    c.samples[1].used = false;

    const TimeCalibration r = TimeCalibration::fromJson(c.toJson());
    CHECK(r.source == c.source);
    CHECK(r.offsetMs == c.offsetMs);
    CHECK(std::fabs(r.rate - c.rate) < 1e-15);
    CHECK(r.rateApplied == c.rateApplied);
    CHECK(r.dateKnown == c.dateKnown);
    CHECK(r.truthSet && r.truthOffsetMs == 300000);
    CHECK(r.truthNote == c.truthNote);
    CHECK(r.samples.size() == 2);
    CHECK(r.samples[0].rawText == c.samples[0].rawText);
    CHECK(r.samples[0].frameImgPath == c.samples[0].frameImgPath);
    CHECK(r.samples[1].used == false);
    // 空对象 → None
    CHECK(TimeCalibration::fromJson({}).source == TimeCalibration::Source::None);
}

// ---------------------------------------------------------------------------
int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);

    testEmptyAndExcluded();
    testSinglePoint();
    testTwoPointsConservative();
    testThreePointsDriftApplied();
    testNoDriftNotSignificant();
    testMinThresholdBoundary();
    testNoisyStrongDrift();
    testOutlierExcludeRefit();
    testInsaneRateRejected();
    testExcludeDownToSingle();
    testSameStreamPosition();
    testRoundTrip();
    testLegacyMigration();
    testTruthOffset();
    testJsonRoundTrip();

    printf("calibration_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
