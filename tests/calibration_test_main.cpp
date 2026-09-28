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
 *  - 漂移显著性门控（3σ 与 10秒/天双阈）
 *  - 野点残差警告与剔除重拟合
 *  - 异常速率（OCR 误读）拒绝应用
 *  - 退化路径：空测点/全排除/同一流内位置
 *  - wallMsOf/streamMsOf 往返一致性、v7 旧格式迁移
 */
#include "domain/time_calibration.h"
#include "domain/time_piecewise.h"   // v1.18.x：变速路由阈值判据
#include "domain/truth_time_parse.h"

#include <QCoreApplication>
#include <QDateTime>
#include <cstdio>
#include <cmath>

static int g_failures = 0;
static int g_checks = 0;

#define CHECK(cond, msg) do { \
    ++g_checks; \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, msg); \
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
    CHECK(!fr.ok && fr.pointsUsed == 0, "!fr.ok && fr.pointsUsed == 0");

    // 全部排除 → 同样无效
    auto s1 = pt(0, kOff), s2 = pt(60000, kOff + 60000);
    s1.used = false; s2.used = false;
    fr = TimeCalibration::fit({s1, s2});
    CHECK(!fr.ok && fr.pointsUsed == 0, "!fr.ok && fr.pointsUsed == 0");

    // 失败拟合不得改写现有校时值
    TimeCalibration c;
    c.offsetMs = 12345;
    c.applyFit(fr);
    CHECK(c.offsetMs == 12345 && c.rate == 1.0 && !c.rateApplied, "c.offsetMs == 12345 && c.rate == 1.0 && !c.rateApplied");
}

static void testSinglePoint()
{
    auto fr = TimeCalibration::fit({pt(5000, kOff + 5000)});
    CHECK(fr.ok && fr.pointsUsed == 1, "fr.ok && fr.pointsUsed == 1");
    CHECK(fr.rate == 1.0 && !fr.rateSignificant, "fr.rate == 1.0 && !fr.rateSignificant");
    CHECK(fr.offsetMs == kOff, "fr.offsetMs == kOff");

    TimeCalibration c;
    c.applyFit(fr);
    CHECK(!c.rateApplied, "!c.rateApplied");
    CHECK(c.wallMsOf(5000) == kOff + 5000, "c.wallMsOf(5000) == kOff + 5000");
    CHECK(c.wallMsOf(0) == kOff, "c.wallMsOf(0) == kOff");
}

static void testTwoPointsConservative()
{
    // 两点、1 小时间隔、真实漂移 86.4 秒/天：
    // 拟合能解出速率，但 ±1s 假设误差下不显著 → 不应用（保守，正确行为）
    const double rate = 1.001;
    auto fr = TimeCalibration::fit({
        pt(0, kOff),
        pt(3600000, kOff + static_cast<qint64>(std::llround(rate * 3600000.0)))});
    CHECK(fr.ok && fr.pointsUsed == 2, "fr.ok && fr.pointsUsed == 2");
    CHECK(std::fabs(fr.rate - rate) < 1e-6, "std::fabs(fr.rate - rate) < 1e-6");
    CHECK(fr.sigmaRate > 0, "fr.sigmaRate > 0");
    CHECK(!fr.rateSignificant, "!fr.rateSignificant");   // 两点无法区分漂移与 OCR 误差
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
    CHECK(fr.ok && fr.pointsUsed == 3, "fr.ok && fr.pointsUsed == 3");
    CHECK(std::fabs(fr.rate - rate) < 1e-6, "std::fabs(fr.rate - rate) < 1e-6");
    CHECK(fr.rateSignificant && fr.rateSane, "fr.rateSignificant && fr.rateSane");
    CHECK(fr.warning == TimeCalibration::FitWarning::None, "fr.warning == TimeCalibration::FitWarning::None");

    TimeCalibration c;
    c.applyFit(fr);
    CHECK(c.rateApplied, "c.rateApplied");
    CHECK(std::fabs(c.driftSecondsPerDay() - 34.56) < 0.5, "std::fabs(c.driftSecondsPerDay() - 34.56) < 0.5");
    // 各测点报时误差 ≤ 2ms
    CHECK(std::fabs(c.wallMsOf(0) - kOff) <= 2, "std::fabs(c.wallMsOf(0) - kOff) <= 2");
    CHECK(std::fabs(c.wallMsOf(x2) - (kOff + std::llround(rate * x2))) <= 2, "std::fabs(c.wallMsOf(x2) - (kOff + std::llround(rate * x2))) <= 2");
    // 修正前后对比：不修正时中段误差 ≈ 563ms 量级，修正后 ≤ 2ms
    const qint64 naive = kOff + x2;              // rate=1 的推算
    const qint64 truth = kOff + std::llround(rate * x2);
    CHECK(truth - naive > 1000, "truth - naive > 1000");                 // 漂移真实存在（>1s）
}

static void testNoDriftNotSignificant()
{
    const qint64 x1 = 1410000, x2 = 2820000;
    auto fr = TimeCalibration::fit({
        pt(0, kOff), pt(x1, kOff + x1), pt(x2, kOff + x2)});
    CHECK(fr.ok, "fr.ok");
    CHECK(fr.rate == 1.0, "fr.rate == 1.0");
    CHECK(!fr.rateSignificant, "!fr.rateSignificant");
}

static void testMinThresholdBoundary()
{
    // 微小漂移需要足够长的跨度才能解析（OSD 秒级量化）：
    // 47 分钟跨度上 40 秒/天只产生 ~1.3ms 漂移，物理上不可检出。
    // 用 24 小时跨度测试 10 秒/天阈值的门控行为（v1.15.3 拍板收紧 30→10）：
    const qint64 x1 = 43200000, x2 = 86400000;   // 12h / 24h
    // 5 秒/天：可测（24h 漂移 5s）但低于 10 秒/天下限 → 不显著
    const double rLow = 1.0 + 5.0 / 86400000.0;
    auto fr = TimeCalibration::fit({
        pt(0, kOff),
        pt(x1, kOff + static_cast<qint64>(std::llround(rLow * x1))),
        pt(x2, kOff + static_cast<qint64>(std::llround(rLow * x2)))});
    CHECK(fr.ok && !fr.rateSignificant, "fr.ok && !fr.rateSignificant");
    CHECK(std::fabs(fr.rate - rLow) < 1e-9, "std::fabs(fr.rate - rLow) < 1e-9");     // 拟合仍精确解出（供报告）

    // 40 秒/天：超过 10 秒/天下限且 σ≈0 → 显著
    const double rHigh = 1.0 + 40.0 / 86400000.0;
    fr = TimeCalibration::fit({
        pt(0, kOff),
        pt(x1, kOff + static_cast<qint64>(std::llround(rHigh * x1))),
        pt(x2, kOff + static_cast<qint64>(std::llround(rHigh * x2)))});
    CHECK(fr.ok && fr.rateSignificant, "fr.ok && fr.rateSignificant");
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
    CHECK(fr.ok, "fr.ok");
    CHECK(std::fabs(fr.rate - rate) < 1e-4, "std::fabs(fr.rate - rate) < 1e-4");
    CHECK(fr.rateSignificant, "fr.rateSignificant");
    CHECK(fr.sigmaRate > 0 && fr.sigmaOffsetMs > 0, "fr.sigmaRate > 0 && fr.sigmaOffsetMs > 0");
    CHECK(fr.maxResidualMs <= 250, "fr.maxResidualMs <= 250");
}

static void testOutlierExcludeRefit()
{
    const double rate = 1.0004;
    const qint64 x1 = 940000, x2 = 1880000, x3 = 2820000;
    // v1.18.x：阀值 3s→10s 后，野点用例需真属于「错读量级」——取 30s（与 §97 野点用例同量级；
    // 秒级偏差（≤10s）属分段速率波动，不应被当错读剔）
    auto outlier = pt(x2, kOff + static_cast<qint64>(std::llround(rate * x2)) + 30000);
    QVector<TimeCalibration::Sample> samples = {
        pt(0, kOff),
        pt(x1, kOff + static_cast<qint64>(std::llround(rate * x1))),
        outlier,
        pt(x3, kOff + static_cast<qint64>(std::llround(rate * x3)))};

    auto fr = TimeCalibration::fit(samples);
    CHECK(fr.ok, "fr.ok");
    CHECK(fr.maxResidualMs > TimeCalibration::kOutlierResidualMs, "fr.maxResidualMs > TimeCalibration::kOutlierResidualMs");
    CHECK(fr.warning == TimeCalibration::FitWarning::OutlierSuspected, "fr.warning == TimeCalibration::FitWarning::OutlierSuspected");

    // 剔除野点重拟合 → 恢复干净
    samples[2].used = false;
    fr = TimeCalibration::fit(samples);
    CHECK(fr.ok && fr.pointsUsed == 3, "fr.ok && fr.pointsUsed == 3");
    CHECK(fr.warning == TimeCalibration::FitWarning::None, "fr.warning == TimeCalibration::FitWarning::None");
    CHECK(std::fabs(fr.rate - rate) < 1e-6, "std::fabs(fr.rate - rate) < 1e-6");
    CHECK(fr.rateSignificant, "fr.rateSignificant");
}

static void testInsaneRateRejected()
{
    // OCR 误读日期（wall 翻倍）→ 速率荒谬，拒绝应用但保留拟合值供诊断
    auto fr = TimeCalibration::fit({
        pt(0, kOff), pt(3600000, kOff + 2 * 3600000)});
    CHECK(fr.ok && fr.rate == 2.0, "fr.ok && fr.rate == 2.0");
    CHECK(!fr.rateSane, "!fr.rateSane");
    CHECK(fr.warning == TimeCalibration::FitWarning::RateInsane, "fr.warning == TimeCalibration::FitWarning::RateInsane");

    TimeCalibration c;
    c.applyFit(fr);
    CHECK(!c.rateApplied, "!c.rateApplied");              // 拒绝应用
    CHECK(c.effectiveRate() == 1.0, "c.effectiveRate() == 1.0");    // 换算按 1.0
    CHECK(c.rate == 2.0, "c.rate == 2.0");               // 拟合值仍留档（报告/诊断）
}

static void testExcludeDownToSingle()
{
    auto s1 = pt(0, kOff);
    auto s2 = pt(1410000, kOff + 1410000);
    auto s3 = pt(2820000, kOff + 2820000);
    s2.used = false; s3.used = false;
    auto fr = TimeCalibration::fit({s1, s2, s3});
    CHECK(fr.ok && fr.pointsUsed == 1, "fr.ok && fr.pointsUsed == 1");
    CHECK(fr.rate == 1.0 && fr.offsetMs == kOff, "fr.rate == 1.0 && fr.offsetMs == kOff");
}

static void testSameStreamPosition()
{
    // 两个测点同一流内位置 → sxx=0 退化单点
    auto fr = TimeCalibration::fit({pt(5000, kOff + 5000), pt(5000, kOff + 5100)});
    CHECK(fr.ok && fr.pointsUsed == 1, "fr.ok && fr.pointsUsed == 1");
    CHECK(fr.rate == 1.0, "fr.rate == 1.0");
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
    CHECK(std::fabs(c.streamMsOf(wall) - x) <= 1, "std::fabs(c.streamMsOf(wall) - x) <= 1");

    // rateApplied=false → 双向都按 1.0（一致退化）
    c.rateApplied = false;
    CHECK(c.wallMsOf(x) == kOff + x, "c.wallMsOf(x) == kOff + x");
    CHECK(c.streamMsOf(kOff + x) == x, "c.streamMsOf(kOff + x) == x");
}

static void testLegacyMigration()
{
    auto c = TimeCalibration::fromLegacyOffset(3600000);
    CHECK(c.source == TimeCalibration::Source::Manual, "c.source == TimeCalibration::Source::Manual");
    CHECK(!c.dateKnown, "!c.dateKnown");
    CHECK(c.rate == 1.0 && !c.rateApplied, "c.rate == 1.0 && !c.rateApplied");
    CHECK(c.wallMsOf(5000) == 3605000, "c.wallMsOf(5000) == 3605000");

    // 用户实测回归：time_offset=0 的旧数据 = 未校时，不产生空模型
    // （空模型会让案件徽标误亮 ⏰ 而图表毫无变化）
    auto zero = TimeCalibration::fromLegacyOffset(0);
    CHECK(zero.source == TimeCalibration::Source::None,
          "legacy: zero offset -> None");
    CHECK(!zero.isEffective(), "legacy: zero offset not effective");
    CHECK(!zero.isValid(), "legacy: zero offset not valid");
    // 空模型通用判定：source=Manual 但全零 → 不有效
    TimeCalibration empty;
    empty.source = TimeCalibration::Source::Manual;
    CHECK(!empty.isEffective(), "empty manual model not effective");
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
    CHECK(c.wallMsOf(0) == kOff, "c.wallMsOf(0) == kOff");
    // 北京时间 = 监控 + 330s（全局应用）
    CHECK(c.beijingMsOf(0) == kOff + 330000, "c.beijingMsOf(0) == kOff + 330000");
    CHECK(c.beijingMsOf(2820000) == kOff + 2820000 + 330000, "c.beijingMsOf(2820000) == kOff + 2820000 + 330000");
    // 未校验时 truthOffsetMs=0 → 北京时间 == 监控时间（安全退化）
    TimeCalibration d;
    d.offsetMs = kOff;
    CHECK(d.beijingMsOf(1000) == d.wallMsOf(1000), "d.beijingMsOf(1000) == d.wallMsOf(1000)");
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
    CHECK(r.source == c.source, "r.source == c.source");
    CHECK(r.offsetMs == c.offsetMs, "r.offsetMs == c.offsetMs");
    CHECK(std::fabs(r.rate - c.rate) < 1e-15, "std::fabs(r.rate - c.rate) < 1e-15");
    CHECK(r.rateApplied == c.rateApplied, "r.rateApplied == c.rateApplied");
    CHECK(r.dateKnown == c.dateKnown, "r.dateKnown == c.dateKnown");
    CHECK(r.truthSet && r.truthOffsetMs == 300000, "r.truthSet && r.truthOffsetMs == 300000");
    CHECK(r.truthNote == c.truthNote, "r.truthNote == c.truthNote");
    CHECK(r.samples.size() == 2, "r.samples.size() == 2");
    CHECK(r.samples[0].rawText == c.samples[0].rawText, "r.samples[0].rawText == c.samples[0].rawText");
    CHECK(r.samples[0].frameImgPath == c.samples[0].frameImgPath, "r.samples[0].frameImgPath == c.samples[0].frameImgPath");
    CHECK(r.samples[1].used == false, "r.samples[1].used == false");
    // 空对象 → None
    CHECK(TimeCalibration::fromJson({}).source == TimeCalibration::Source::None, "TimeCalibration::fromJson({}).source == TimeCalibration::Source::None");
}

// ---------------------------------------------------------------------------
// v1.12.5 北京时间对时：校时图片两框 OCR 原文解析（用户拍板约定，增城案
// 典型照片实证）+ 对时留档字段序列化
// ---------------------------------------------------------------------------
static void testTruthTimeParse()
{
    const QDate day(2026, 7, 22);
    const qint64 expectMon = QDateTime(QDate(2026, 7, 22), QTime(12, 25, 47),
                                       Qt::LocalTime).toMSecsSinceEpoch();
    const qint64 expectBj  = QDateTime(QDate(2026, 7, 22), QTime(12, 39, 41),
                                       Qt::LocalTime).toMSecsSinceEpoch();

    // ① 单行完整（监控 OSD：中文年月日补零 + 星期）
    {
        const auto r = parseTruthTimeText(
            {QStringLiteral("2026年07月22日 星期三 12:25:47")}, day);
        CHECK(r.ok && r.dateFromText && r.wallMs == expectMon, "r.ok && r.dateFromText && r.wallMs == expectMon");
        CHECK(r.matchedText.contains(QStringLiteral("12:25:47")), "condition failed (L346)");
    }
    // ① 单行完整（横杠格式 + 毫秒小数）
    {
        const auto r = parseTruthTimeText(
            {QStringLiteral("2026-07-22 12:25:47.500")}, day);
        CHECK(r.ok && r.wallMs == expectMon + 500, "r.ok && r.wallMs == expectMon + 500");
    }
    // ② 跨行组合（授时网页：不补零中文日期行 + 纯时间行；含噪声行）
    {
        const auto r = parseTruthTimeText(
            {QStringLiteral("标准北京时间"),
             QStringLiteral("现在是2026年7月22日星期三，第30周"),
             QStringLiteral("12:39:41"),
             QStringLiteral("你的设备时间慢了750毫秒")}, day);
        CHECK(r.ok && r.dateFromText && r.wallMs == expectBj, "r.ok && r.dateFromText && r.wallMs == expectBj");
        CHECK(r.matchedText.contains(QStringLiteral("第30周")), "condition failed (L362)");
    }
    // ③ 纯时间 + 假定日期（框 1 同日）
    {
        const auto r = parseTruthTimeText({QStringLiteral("12:39:41")}, day);
        CHECK(r.ok && !r.dateFromText && r.wallMs == expectBj, "r.ok && !r.dateFromText && r.wallMs == expectBj");
    }
    // ③ 无假定日期 → nomatch
    {
        const auto r = parseTruthTimeText({QStringLiteral("12:39:41")}, QDate());
        CHECK(!r.ok && r.error == QStringLiteral("nomatch"), "condition failed (L372)");
    }
    // 拒识：仅时分（手机状态栏 12:39）→ noseconds
    {
        const auto r = parseTruthTimeText({QStringLiteral("12:39")}, day);
        CHECK(!r.ok && r.error.startsWith(QStringLiteral("noseconds:")), "condition failed (L377)");
    }
    // 拒识：全角冒号归一化后命中（12：25：47）
    {
        const auto r = parseTruthTimeText(
            {QString::fromUtf8("2026年07月22日 12\uff1a25\uff1a47")}, day);
        CHECK(r.ok && r.wallMs == expectMon, "r.ok && r.wallMs == expectMon");
    }
    // 拒识：值域非法（25:99:99 之类误读）→ invalid
    {
        const auto r = parseTruthTimeText(
            {QStringLiteral("2026年07月22日 25:99:99")}, QDate());
        CHECK(!r.ok && r.error.startsWith(QStringLiteral("invalid:")), "condition failed (L389)");
    }
    // 拒识：毫无时间文本 → nomatch
    {
        const auto r = parseTruthTimeText(
            {QStringLiteral("标准北京时间"), QStringLiteral("本时间同步国家授时中心精确到毫秒")},
            day);
        CHECK(!r.ok && r.error == QStringLiteral("nomatch"), "condition failed (L396)");
    }
    // 防碎片错配：112:39:41 不应在内层误命中 12:39:41
    {
        const auto r = parseTruthTimeText({QStringLiteral("112:39:41")}, day);
        CHECK(!r.ok, "!r.ok");
    }
}

static void testTruthArchiveRoundTrip()
{
    // v1.12.5 对时留档字段 JSON 往返（含老文件无字段的兼容退化）
    TimeCalibration c;
    c.source = TimeCalibration::Source::Ocr;
    c.offsetMs = kOff;
    c.dateKnown = true;
    c.truthSet = true;
    c.truthOffsetMs = -834000;   // 监控快 13 分 54 秒 → 北京 = 监控 + (-834s)
    c.truthSource = QStringLiteral("photo");
    c.truthImagePath = QStringLiteral("D:/cases/x/calibration/abc.jpg");
    c.truthMonitorBox = QRect(100, 50, 800, 40);
    c.truthBeijingBox = QRect(1500, 900, 400, 120);
    c.truthMonitorText = QStringLiteral("2026年07月22日 星期三 12:25:47");
    c.truthBeijingText = QStringLiteral("现在是2026年7月22日星期三，第30周 | 12:39:41");

    const TimeCalibration r = TimeCalibration::fromJson(c.toJson());
    CHECK(r.truthSet && r.truthOffsetMs == -834000, "r.truthSet && r.truthOffsetMs == -834000");
    CHECK(r.truthSource == QStringLiteral("photo"), "condition failed (L423)");
    CHECK(r.truthImagePath == c.truthImagePath, "r.truthImagePath == c.truthImagePath");
    CHECK(r.truthMonitorBox == QRect(100, 50, 800, 40), "r.truthMonitorBox == QRect(100, 50, 800, 40)");
    CHECK(r.truthBeijingBox == QRect(1500, 900, 400, 120), "r.truthBeijingBox == QRect(1500, 900, 400, 120)");
    CHECK(r.truthMonitorText == c.truthMonitorText, "r.truthMonitorText == c.truthMonitorText");
    CHECK(r.truthBeijingText == c.truthBeijingText, "r.truthBeijingText == c.truthBeijingText");

    // 老文件（无新字段）→ 空值安全退化
    TimeCalibration legacy;
    legacy.source = TimeCalibration::Source::Manual;
    legacy.offsetMs = kOff;
    legacy.truthSet = true;
    legacy.truthOffsetMs = 5000;
    const TimeCalibration lr = TimeCalibration::fromJson(legacy.toJson());
    CHECK(lr.truthSource.isEmpty() && lr.truthImagePath.isEmpty(), "lr.truthSource.isEmpty() && lr.truthImagePath.isEmpty()");
    CHECK(!lr.truthMonitorBox.isValid() && !lr.truthBeijingBox.isValid(), "!lr.truthMonitorBox.isValid() && !lr.truthBeijingBox.isValid()");
    CHECK(lr.truthOffsetMs == 5000 && lr.truthSet, "lr.truthOffsetMs == 5000 && lr.truthSet");
}

// ---------------------------------------------------------------------------
// ---------------------------------------------------------------------------
// v1.18.x（2026-09-24 顺德公安导出件实测 1.139×）：非实时导出/变速件的判据
// ---------------------------------------------------------------------------
/// 「自洽的大倍率」= 非实时导出件（可确认采用）；散点/荒谬倍率 = OCR 误读（拒绝）
static void testRateChangeSelfConsistent()
{
    const qint64 base = kOff;
    // 三点共线、倍率 1.139（画面时间比播放进度快 13.9%）→ 自洽
    const auto frOk = TimeCalibration::fit({
        pt(0, base),
        pt(600000, base + qint64(600000 * 1.139)),
        pt(1200000, base + qint64(1200000 * 1.139))});
    CHECK(frOk.ok && frOk.warning == TimeCalibration::FitWarning::RateInsane,
          "1.139x -> RateInsane (beyond 1% sane bound)");
    CHECK(TimeCalibration::rateChangeSelfConsistent(frOk),
          "1.139x collinear -> self-consistent (non-realtime export)");

    // 同一倍率但中间点被读错 30 秒 → 不成直线 → 不自洽（按误读处理）
    const auto frOutlier = TimeCalibration::fit({
        pt(0, base),
        pt(600000, base + qint64(600000 * 1.139) + 30000),
        pt(1200000, base + qint64(1200000 * 1.139))});
    CHECK(!TimeCalibration::rateChangeSelfConsistent(frOutlier),
          "outlier residual 30s -> not self-consistent");

    // 两点：无残差信息 → 交用户确认（自洽）
    const auto frTwo = TimeCalibration::fit({pt(0, base), pt(600000, base + 660000)});
    CHECK(frTwo.ok && TimeCalibration::rateChangeSelfConsistent(frTwo),
          "two points large rate -> ask user (self-consistent)");

    // 荒谬倍率（日期/上下午读错典型 ~2x）→ 超可确认上限，一律按误读拒绝
    const auto frDouble = TimeCalibration::fit({pt(0, base), pt(600000, base + 1200000)});
    CHECK(qAbs(frDouble.rate - 2.0) < 1e-9
              && !TimeCalibration::rateChangeSelfConsistent(frDouble),
          "2.0x beyond confirmable bound -> still rejected as misread");
}

/// v1.18.x：单点错读（年份 2026→2022）自动剔除——三点里一个错点必须先剔再拟合
static void testFitDroppingWorstOutlier()
{
    const qint64 base = kOff;          // 2026-09-20 15:00:13 附近
    // 真实关系：wall = base + 1.139×stream（平台加速导出，顺德公安件实测）
    auto mk = [&](qint64 stream) {
        return pt(stream, base + qint64(double(stream) * 1.139));
    };
    QVector<TimeCalibration::Sample> s = {mk(1080), mk(3080), mk(2739326)};
    // 中间那个点年份读错 4 年（2026→2022）
    s[1].wallMs = base + qint64(3080.0 * 1.139)
                  - qint64(4LL * 365 * 86400 * 1000);
    const auto raw = TimeCalibration::fit(s);
    CHECK(raw.warning != TimeCalibration::FitWarning::None,
          "含错读点：直接拟合必报警（残差大/倍率荒谬）");
    int dropped = -1;
    const auto robust = TimeCalibration::fitDroppingWorstOutlier(s, &dropped);
    CHECK(dropped == 1, "稳健拟合剔除错读点（下标 1）");
    CHECK(robust.ok && robust.pointsUsed == 2, "剔点后仍有 2 点可用");
    CHECK(std::fabs(robust.rate - 1.139) < 0.002,
          qPrintable(QStringLiteral("剔点后速率≈1.139（实测 %1）")
                         .arg(robust.rate, 0, 'f', 4)));

    // 无错读：不得误剔
    int dropped2 = -1;
    const auto clean = QVector<TimeCalibration::Sample>{mk(1080), mk(3080), mk(2739326)};
    const auto r2 = TimeCalibration::fitDroppingWorstOutlier(clean, &dropped2);
    CHECK(dropped2 == -1 && r2.pointsUsed == 3, "无错读时不剔点");
}

/// 预检路由阈值必须与「正常录像」容差一致：否则 1%~15% 压缩件掉进无解缝隙
static void testVariableRateRouting()
{
    CHECK(PiecewiseTimeMap::isVariableRate(1.139),
          "1.139x -> variable-rate (time reconstruction)");
    CHECK(PiecewiseTimeMap::isVariableRate(0.98), "0.98x -> variable-rate");
    CHECK(!PiecewiseTimeMap::isVariableRate(1.005), "1.005x -> normal recording");
    CHECK(!PiecewiseTimeMap::isVariableRate(0.995), "0.995x -> normal recording");
}

/// v1.18.x（2026-09-26 顺德件「校时后还是错的」根因回归）：第 1 步结果落工作面的
/// 唯一门控。第 2 步「对真实时间」必须走同一实现——否则它落库旧工作面，把刚算出的
/// 三点结果静默丢弃（时间轴永远按 rate=1.0 走）。
static void testApplyFitDecision()
{
    const qint64 base = 1789887613000;   // 09-20 15:00:13（实测复现值）
    const auto mk = [&](qint64 stream) {
        return pt(stream, base + qint64(double(stream) * 1.139));
    };

    // ① 自洽大倍率（非实时导出件）→ 应用速率 + 标变速
    TimeCalibration plan;
    plan.source = TimeCalibration::Source::Ocr;
    plan.dateKnown = true;
    plan.samples = QVector<TimeCalibration::Sample>{mk(1080), mk(1370722), mk(2739326)};
    plan.applyFit(TimeCalibration::fit(plan.samples));
    CHECK(!plan.rateApplied, "域层 applyFit 仍判 insane（不改语义）");
    TimeCalibration::applyFitDecision(plan, /*noDriftCorrection=*/false);
    CHECK(plan.rateApplied && plan.speedVariant,
          "自洽 1.139x → 应用速率并标变速（按此倍率校时同款）");
    CHECK(std::fabs(plan.rate - 1.139) < 0.002, "落库速率≈1.139");

    // ② 「不校正时钟快慢」勾选 → 只定基准，速率不应用
    TimeCalibration nodrift = plan;
    nodrift.rateApplied = false;
    nodrift.speedVariant = false;
    TimeCalibration::applyFitDecision(nodrift, /*noDriftCorrection=*/true);
    CHECK(!nodrift.rateApplied, "勾选不校正 → rateApplied=false（用户意图优先）");

    // ③ 不自洽的大倍率（野点 30s）→ 不应用速率
    TimeCalibration bad;
    bad.source = TimeCalibration::Source::Ocr;
    bad.samples = QVector<TimeCalibration::Sample>{
        mk(1080), pt(1370722, base + qint64(1370722.0 * 1.139) + 30000),
        mk(2739326)};
    bad.applyFit(TimeCalibration::fit(bad.samples));
    TimeCalibration::applyFitDecision(bad, /*noDriftCorrection=*/false);
    CHECK(!bad.rateApplied, "不自洽大倍率 → 不应用速率（仍当误读拒）");

    // ④ 正常录像（rate≈1）→ 保持 applyFit 判定（显著才应用）
    TimeCalibration normal;
    normal.source = TimeCalibration::Source::Ocr;
    normal.samples = QVector<TimeCalibration::Sample>{
        pt(0, base), pt(600000, base + 600000), pt(1200000, base + 1200000)};
    normal.applyFit(TimeCalibration::fit(normal.samples));
    TimeCalibration::applyFitDecision(normal, /*noDriftCorrection=*/false);
    CHECK(!normal.rateApplied, "钟准（无显著漂移）→ 不应用速率，只定基准");
}

/// v1.18.x 回归（顺德件「重新导入还是不行」的第 2 步落库路径）：
/// 第 2 步（对真实时间）落库前必须并入未应用的第 1 步结果，
/// 否则旧工作面（实测：继承来的 2 点 rate=1.0）被当结果写进 .vla。
/// v1.18.x 现场值回归（2026-09-26 顺德 JA382 平台导出件，应用实抽的三帧原值）：
/// 该片**内部分段速率有波动**（逐段 1.10~1.17），三点对全局直线有 1~3 秒残差——
/// 旧门限 3s 会把这个正常点当「错读」剔掉（剔完只剩 2 点，共线校验失效）。
/// P-98 秒级跳变对齐表：插值、反解、优先级、序列化往返
static void testTickAnchorTable()
{
    // 构造一段「加速导出」序列：流内每 ~900ms 走 1 个画面秒，含 +2 跳秒
    const qint64 w0 = 1789887613000;   // 15:00:13
    TimeCalibration c;
    c.source = TimeCalibration::Source::Ocr;
    c.dateKnown = true;
    c.offsetMs = w0;
    c.rate = 1.14;
    c.rateApplied = true;              // 仿射也有值 → 验证 tick 优先级
    c.samples = QVector<TimeCalibration::Sample>{pt(1080, w0), pt(2739326, w0 + 3120000)};
    QVector<qint64> streams{1080, 2000, 2900, 3800, 4700};
    for (int i = 0; i < streams.size(); ++i)
        c.tickAnchors.append(qMakePair(streams[i], w0 + i * 1000));
    c.tickSkippedSeconds = 323.0;

    CHECK(c.tickMode(), "tick: 表非空即生效");
    CHECK(c.isEffective(), "tick: 有效校时");
    // 段内线性插值（锚点之间）
    CHECK(c.wallMsOf(1080) == w0, "tick: 首锚精确");
    CHECK(c.wallMsOf(4700) == w0 + 4000, "tick: 末锚精确");
    CHECK(c.wallMsOf(2450) == w0 + 1000 + 500, "tick: 段内插值（2500 处应 +1500）");
    // 范围外按**端部相邻段斜率**延伸（不夹取成常量；P2-1：不能用 1.0）
    // 本用例锚点间隔 900~920ms→1s，端部斜率 ≈1.111
    {
        const qint64 d0 = c.wallMsOf(0) - w0;      // 负值：首锚前
        const qint64 d1 = c.wallMsOf(6000) - (w0 + 4000);
        CHECK(d0 < 0 && d0 > -1300 && d0 < -1080,
              qPrintable(QStringLiteral("tick: 首锚前按斜率延伸（%1ms）").arg(d0)));
        CHECK(d1 > 1300 && d1 < 1600,
              qPrintable(QStringLiteral("tick: 末锚后按斜率延伸（%1ms）").arg(d1)));
    }
    // 反解往返
    for (qint64 s : {qint64(1080), qint64(2450), qint64(4700)}) {
        const qint64 back = c.streamMsOf(c.wallMsOf(s));
        CHECK(std::llabs(back - s) <= 1,
              qPrintable(QStringLiteral("tick: 往返 %1 → %2").arg(s).arg(back)));
    }
    // 仿射被秒级表覆盖（同样输入两者结果不同）
    TimeCalibration affine = c;
    affine.tickAnchors.clear();
    CHECK(affine.wallMsOf(2450) != c.wallMsOf(2450),
          "tick: 秒级表优先于仿射");

    // 序列化往返（delta 编码）
    const TimeCalibration r = TimeCalibration::fromJson(c.toJson());
    CHECK(r.tickAnchors.size() == c.tickAnchors.size(), "tick: 往返锚点数一致");
    bool same = r.tickAnchors.size() == c.tickAnchors.size();
    for (int i = 0; same && i < r.tickAnchors.size(); ++i)
        same = (r.tickAnchors.at(i) == c.tickAnchors.at(i));
    CHECK(same, "tick: 往返锚点逐项一致");
    CHECK(std::fabs(r.tickSkippedSeconds - 323.0) < 0.5, "tick: 跳秒数往返");
    CHECK(r.wallMsOf(2450) == c.wallMsOf(2450), "tick: 往返后换算一致");

    // 少于 2 项不生效（退回仿射/分段）
    TimeCalibration one;
    one.source = TimeCalibration::Source::Ocr;
    one.dateKnown = true;
    one.offsetMs = w0;
    one.tickAnchors.append(qMakePair(qint64(0), w0));
    CHECK(!one.tickMode(), "tick: 单项不成表");
}

/// v1.18.x 现场值回归（2026-09-27 calib_debug.log 实录）：ROI 切掉年份末位 →
/// OCR 间歇把 2026 读成 2023 → 旧逻辑「残差最大剔除」反而剔掉正确的 15:00:17，
/// 剩下 2 点算出 34583 倍荒谬速率 → 整单被拒（用户看到「V14 校时不成功」）。
/// 修：日期合理性过滤（中位数 ±1 天）先剔错读点，再用剩下的点拟合。
static void testImplausibleDateDropped()
{
    const auto ms = [](int y, int mo, int d, int h, int mi, int sec) {
        return QDateTime(QDate(y, mo, d), QTime(h, mi, sec), Qt::LocalTime)
            .toMSecsSinceEpoch();
    };
    QVector<TimeCalibration::Sample> s{
        pt(1080, ms(2023, 9, 20, 15, 0, 13)),      // ← 错读（应为 2026）
        pt(5080, ms(2026, 9, 20, 15, 0, 17)),
        pt(2739326, ms(2026, 9, 20, 15, 52, 13))};

    // 未过滤时：旧逻辑剔掉的是「正确点」，速率荒谬（复现现场）
    int dropped = -1;
    const auto bad = TimeCalibration::fitDroppingWorstOutlier(s, &dropped);
    CHECK(!bad.rateSane,
          qPrintable(QStringLiteral("现场复现：未过滤时速率荒谬（%1）").arg(bad.rate)));

    // 过滤后：错读点被剔，剩下两点拟合出正确倍率
    QVector<TimeCalibration::Sample> sane = s;
    const int n = TimeCalibration::dropImplausibleDates(&sane);
    CHECK(n == 1 && sane.size() == 2,
          qPrintable(QStringLiteral("日期过滤：剔 %1 点，剩 %2").arg(n).arg(sane.size())));
    const auto good = TimeCalibration::fit(sane);
    CHECK(good.ok && std::fabs(good.rate - 1.1396) < 0.005,
          qPrintable(QStringLiteral("过滤后倍率≈1.1396（实测 %1）").arg(good.rate)));
    CHECK(TimeCalibration::rateChangeSelfConsistent(good),
          "过滤后判自洽（非实时导出件）→ 可自动应用");

    // 真变速/正常件不受影响（日期一致时一点不剔）
    QVector<TimeCalibration::Sample> same{
        pt(1080, ms(2026, 9, 20, 15, 0, 13)),
        pt(1370722, ms(2026, 9, 20, 15, 26, 14)),
        pt(2739326, ms(2026, 9, 20, 15, 52, 13))};
    CHECK(TimeCalibration::dropImplausibleDates(&same) == 0,
          "日期一致时不过滤（不动正常/变速件）");
}

/// v1.18.x 现场值回归（2026-09-28 顺德 V18 实录）：框切掉小时前导位 →
/// OSD 15:00:02 读成 `5:00:02`（单位数小时）→ 与低置信尾点一起把中间**正确**的
/// 15:33:54 挤成离群 → 剩下错头点当锚 → 时间轴偏 10 小时（用户截图实证）。
static void testShortHourStructInvalid()
{
    const auto ms = [](int h, int mi, int sec) {
        return QDateTime(QDate(2026, 9, 20), QTime(h, mi, sec), Qt::LocalTime)
            .toMSecsSinceEpoch();
    };
    // 结构检查：单位数小时判可疑；正常两位小时不受影响
    CHECK(TimeCalibration::rawTextHourShort(
              QStringLiteral("2026年09月20 星期日 5:00:02")),
          "结构检查：`5:00:02` 判为可疑（前导位被切）");
    CHECK(!TimeCalibration::rawTextHourShort(
              QStringLiteral("2026年09月20 星期日 15:00:02")),
          "结构检查：`15:00:02` 正常");
    CHECK(!TimeCalibration::rawTextHourShort(
              QStringLiteral("2026年09月20 星期日 16:31:18")),
          "结构检查：`16:31:18` 正常");

    QVector<TimeCalibration::Sample> s{
        pt(0, ms(5, 0, 2)),          // 错读（真值 15:00:02）
        pt(2030931, ms(15, 33, 54)), // 正确
        pt(4657834, ms(16, 31, 18))};
    s[0].rawText = QStringLiteral("2026年09月20 星期日 5:00:02");
    s[1].rawText = QStringLiteral("2026年09月20 星期日 15:33:54");
    s[2].rawText = QStringLiteral("2026年09月20 星期日 16:31:18");

    // 未过滤：旧逻辑把「正确的中间点」剔掉，剩下错点当锚（复现现场 10 小时偏差）
    int dropped = -1;
    const auto bad = TimeCalibration::fitDroppingWorstOutlier(s, &dropped);
    CHECK(dropped != 0,
          qPrintable(QStringLiteral("现场复现：未过滤时剔掉的是 %1 号点（非错读点）")
                         .arg(dropped)));

    // ★ 修复（V18 真正需要的）：用其余两点外推 → 在 {5, 15} 里挑近的 → 15:00:02
    QVector<TimeCalibration::Sample> fixed = s;
    const int nf = TimeCalibration::repairShortHourSamples(&fixed);
    CHECK(nf == 1, qPrintable(QStringLiteral("短小时修复：修 %1 条").arg(nf)));
    CHECK(fixed.at(0).wallMs == ms(15, 0, 2),
          qPrintable(QStringLiteral("修后首点=15:00:02（实测 %1）")
                         .arg(QDateTime::fromMSecsSinceEpoch(fixed.at(0).wallMs)
                                  .toString(QStringLiteral("HH:mm:ss")))));
    CHECK(fixed.at(0).rawText.contains(QStringLiteral("5:00:02")),
          "修复保留 OCR 原文（取证口径），仅改派生值并标 ⚠");
    CHECK(fixed.at(0).ocrSuspicious, "修复点标 ocrSuspicious（UI 显示 ⚠）");
    const auto f3 = TimeCalibration::fit(fixed);
    CHECK(f3.ok && f3.rate > 1.0 && f3.rate < 1.4,
          qPrintable(QStringLiteral("三点（含修复点）速率≈1.18（实测 %1）").arg(f3.rate)));
    // 三点不共线（前段速率≈1.00、后段≈1.31），最小二乘直线在片头有 ~2 分钟残差
    // ——这是**片内变速**，由自动触发的秒级跳变对齐收拾；本断言只锁「不再差 10 小时」。
    const double offMin3 = (f3.offsetMs - ms(15, 0, 2)) / 60000.0;
    CHECK(std::fabs(offMin3) < 5.0,
          qPrintable(QStringLiteral("修后锚点回到 15:00 附近（实测差 %1 分钟；"
                                    "修复前 -600 分钟）").arg(offMin3, 0, 'f', 1)));

    // 对照：不走修复、只剔结构无效点 → 剩两条正确点，锚点不再来自错读
    QVector<TimeCalibration::Sample> sane = s;
    const int n = TimeCalibration::dropStructurallyInvalid(&sane);
    CHECK(n == 1 && sane.size() == 2,
          qPrintable(QStringLiteral("结构过滤：剔 %1 点，剩 %2").arg(n).arg(sane.size())));
    const auto good = TimeCalibration::fit(sane);
    CHECK(good.ok && good.rate > 0.5 && good.rate < 2.0,
          qPrintable(QStringLiteral("过滤后速率落入合理域（实测 %1）").arg(good.rate)));
    // 锚点不再落在错读的 05:00 上：剩下两点都在**后半段**（该段速率 ≈1.31），
    // 外推到片头天然有 ~10 分钟残差 —— 这属「片内变速」，由自动触发的
    // **秒级跳变对齐**收拾；本断言只锁住「不再炸成 10 小时」。
    const double offMin = (good.offsetMs - ms(15, 0, 2)) / 60000.0;
    CHECK(std::fabs(offMin) < 60.0,
          qPrintable(QStringLiteral("过滤后锚点落在 15:00 附近（实测差 %1 分钟；"
                                    "修复前为 -600 分钟）").arg(offMin, 0, 'f', 1)));
    CHECK(std::fabs(offMin) > 1.0,
          "tick: 残差 >1 分钟 → 属片内变速（应由秒级对齐接手，用例留档）");
}

static void testFieldRateWobbleNotDropped()
{
    // 应用 20:17 那次实际抽到的三帧（均人工核过可读）
    const auto mkWall = [](int h, int m, int s) {
        return QDateTime(QDate(2026, 9, 20), QTime(h, m, s), Qt::LocalTime)
            .toMSecsSinceEpoch();
    };
    QVector<TimeCalibration::Sample> s{
        pt(3080, mkWall(15, 0, 15)),
        pt(923201, mkWall(15, 17, 41)),
        pt(2737326, mkWall(15, 52, 11))};

    const auto fr = TimeCalibration::fit(s);
    CHECK(fr.ok && fr.pointsUsed == 3, "现场件：三点均可用");
    CHECK(std::fabs(fr.rate - 1.1397) < 0.001,
          qPrintable(QStringLiteral("现场件：全局倍率≈1.1397（实测 %1）").arg(fr.rate)));
    CHECK(fr.maxResidualMs < TimeCalibration::kOutlierResidualMs,
          qPrintable(QStringLiteral("现场件：秒级残差（%1ms）不得超阀值")
                         .arg(fr.maxResidualMs)));
    int dropped = -1;
    const auto robust = TimeCalibration::fitDroppingWorstOutlier(s, &dropped);
    CHECK(dropped == -1 && robust.pointsUsed == 3,
          "现场件：秒级速率波动不得被当错读剔除（剔了就没共线校验了）");
    CHECK(TimeCalibration::rateChangeSelfConsistent(fr),
          "现场件：判为自洽（非实时导出件）→ 可自动应用");
}

static void testAbsorbPendingFit()
{
    const qint64 base = 1789887613000;
    const auto mk = [&](qint64 stream) {
        return pt(stream, base + qint64(double(stream) * 1.139));
    };

    // 未应用的第 1 步结果：三点共线 1.139×
    TimeCalibration fit;
    fit.source = TimeCalibration::Source::Ocr;
    fit.dateKnown = true;
    fit.samples = QVector<TimeCalibration::Sample>{mk(1080), mk(1370722), mk(2739326)};
    fit.applyFit(TimeCalibration::fit(fit.samples));

    // 旧工作面：实测那种「2 点相隔 2 秒、rate=1.0」（重新导入继承来的）
    TimeCalibration stale;
    stale.source = TimeCalibration::Source::Ocr;
    stale.dateKnown = true;
    stale.samples = QVector<TimeCalibration::Sample>{mk(3080), mk(5080)};
    stale.applyFit(TimeCalibration::fit(stale.samples));

    // ① pending → 并入，且速率被应用（时间轴从「每一刻都偏」回到正确）
    TimeCalibration working = stale;
    CHECK(TimeCalibration::absorbPendingFit(working, fit, /*fitPending=*/true,
                                            /*noDriftCorrection=*/false),
          "pending 三点结果 → 并入工作面");
    CHECK(working.rateApplied && working.speedVariant,
          "并入后速率已应用（非实时导出件 1.139×）");
    CHECK(working.samples.size() == 3, "并入的是第 1 步的三点结果，不是旧的两点");
    CHECK(std::fabs(working.rate - 1.139) < 0.002, "并入后 rate≈1.139");
    CHECK(working.wallMsOf(2739326)
              == base + qint64(2739326.0 * 1.139) + 0
              || std::llabs(working.wallMsOf(2739326)
                            - (base + qint64(2739326.0 * 1.139))) <= 1,
          "尾点墙钟=15:52:13（正确口径）");

    // ② 未勾选不校正 / 勾选 → 只定基准（沿用第 1 步结果但不改速率）
    TimeCalibration nodrift = stale;
    CHECK(TimeCalibration::absorbPendingFit(nodrift, fit, true, true),
          "勾选不校正也仍然并入基准");
    CHECK(!nodrift.rateApplied, "勾选不校正 → rateApplied=false");
    CHECK(nodrift.samples.size() == 3, "勾选不校正 → 仍然是第 1 步的三点");

    // ③ 无 pending（用户只做了第 1 步手动录入）→ 不动工作面
    TimeCalibration manual = stale;
    manual.offsetMs = 123456;
    CHECK(!TimeCalibration::absorbPendingFit(manual, fit, /*fitPending=*/false, false),
          "无 pending → 不并入");
    CHECK(manual.offsetMs == 123456, "不并入时工作面保持原样");

    // ④ 分段重建结果不是「三点待应用」→ 不并入（走 piecewiseMode 分支）
    TimeCalibration pw = fit;
    pw.piecewise.segments.append(TimeSegment{0, base, 1.0});
    pw.piecewise.streamEndMs = 2739326;
    pw.piecewiseApplied = true;
    TimeCalibration pwWorking = stale;
    CHECK(!TimeCalibration::absorbPendingFit(pwWorking, pw, true, false),
          "分段模式 → 不并入");

    // ⑤ 非法 fit → 不并入
    TimeCalibration empty;
    TimeCalibration keep = stale;
    CHECK(!TimeCalibration::absorbPendingFit(keep, empty, true, false),
          "空/非法第 1 步结果 → 不并入");
}

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
    testRateChangeSelfConsistent();
    testApplyFitDecision();
    testAbsorbPendingFit();
    testFieldRateWobbleNotDropped();
    testImplausibleDateDropped();
    testShortHourStructInvalid();
    testTickAnchorTable();
    testFitDroppingWorstOutlier();
    testVariableRateRouting();
    testExcludeDownToSingle();
    testSameStreamPosition();
    testRoundTrip();
    testLegacyMigration();
    testTruthOffset();
    testJsonRoundTrip();
    testTruthTimeParse();
    testTruthArchiveRoundTrip();

    printf("calibration_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
