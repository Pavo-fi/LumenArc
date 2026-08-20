/**
 * @file sidecar_test_main.cpp
 * @brief 校时 sidecar（.lumencal.json 分段锚点）headless 单测
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-20
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 覆盖（v1.12.0，2026-08-20 拍板：校时反映到前处理产物时间轴）：
 *  - writeSidecar：修剪段墙钟起点后移 trim×rate、整段丢弃/失败剔除、
 *    转码段实测时长生效、缺口表
 *  - loadSidecar：构造 PiecewiseTimeMap（查表校时）——缺口处墙钟跳变、
 *    抽帧段（rate≠1）段内按速率推进、speedVariant 标注
 *  - 兼容路径：无修剪/无剔除的经典 sidecar 行为不变
 * 无媒体依赖（sidecar 是纯 JSON 文件读写）。
 */
#include "app/calibration_service.h"
#include "domain/sort_model.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <cstdio>

static int g_failures = 0;
static int g_checks = 0;
#define CHECK(cond) do { \
    ++g_checks; \
    if (!(cond)) { \
        ++g_failures; \
        fprintf(stderr, "FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); \
    } \
} while (0)

static qint64 epochOf(int y, int mo, int d, int h, int mi, int s)
{
    return QDateTime(QDate(y, mo, d), QTime(h, mi, s), Qt::LocalTime)
        .toMSecsSinceEpoch();
}

static SortEntry mkEntry(const QString &name, qint64 wallStart, qint64 durMs,
                         qint64 ocrEnd = 0)
{
    SortEntry e;
    e.filePath = name;
    e.startMs = wallStart;
    e.endMs = wallStart > 0 ? wallStart + durMs : 0;
    e.ocrEndMs = ocrEnd;
    e.durationMs = durMs;
    e.startSource = wallStart > 0 ? OcrResult::Ocr : OcrResult::None;
    e.sourceKind = wallStart > 0 ? SortEvidenceKind::Ocr : SortEvidenceKind::None;
    e.conf = 0.95;
    return e;
}

/// 场景 A（20260819 广州越秀案实测特征）：正常段 + 抽帧段（rate≈1.89）被修剪
/// 10s + 整段丢弃 + 转码失败剔除；转码段实测时长 51s（CFR 偏差）。
static void testTrimSkipActual()
{
    const qint64 W = epochOf(2026, 8, 19, 3, 19, 52);
    QVector<SortEntry> ordered;
    ordered << mkEntry(QStringLiteral("a.mp4"), W, 60000, W + 60000);
    // 抽帧段：61s 流内装 115.29s 墙钟 → rate = 1.89
    ordered << mkEntry(QStringLiteral("b.mp4"), W + 300000, 61000,
                       W + 300000 + 115290);
    ordered << mkEntry(QStringLiteral("c.mp4"), W + 400000, 61000,
                       W + 400000 + 61000);   // 整段重叠丢弃
    ordered << mkEntry(QStringLiteral("d.mp4"), W + 500000, 61000,
                       W + 500000 + 61000);   // 转码失败剔除

    const QString dir = QDir::temp().absolutePath()
        + QStringLiteral("/lumenarc_sidecar_test");
    QDir().mkpath(dir);
    const QString out = dir + QStringLiteral("/merged.mp4");
    QFile::remove(out + QStringLiteral(".lumencal.json"));

    QString err;
    CHECK(CalibrationService::writeSidecar(
              out, ordered, &err,
              {{QStringLiteral("b.mp4"), 10000}},                      // trim 10s
              {QStringLiteral("c.mp4"), QStringLiteral("d.mp4")},      // skips
              {{QStringLiteral("b.mp4"), 51000}}));                    // 实测 51s
    CHECK(err.isEmpty());
    CHECK(QFile::exists(out + QStringLiteral(".lumencal.json")));

    TimeCalibration cal;
    QString warning;
    CHECK(CalibrationService::loadSidecar(out, &cal, &warning));
    CHECK(cal.isValid());
    CHECK(cal.source == TimeCalibration::Source::Inherited);
    CHECK(cal.piecewiseMode());
    CHECK(cal.piecewise.segments.size() == 2);          // c/d 剔除
    CHECK(cal.speedVariant);                            // b 段 rate 1.89
    CHECK(warning.startsWith(QStringLiteral("gaps:"))); // 缺口警告保留

    // 锚点 1：首段（rate 1.0）
    CHECK(cal.wallMsOf(0) == W);
    CHECK(cal.wallMsOf(30000) == W + 30000);

    // 锚点 2：b 段流内起点 60000（a 实测 60s），墙钟起点 = W+300000+1.89×10000
    const qint64 wallB = W + 300000 + 18900;
    CHECK(qAbs(cal.wallMsOf(60000) - wallB) <= 2000);
    // 缺口跳变：边界前后墙钟跳 ≈ 258.9s（a 墙钟止 W+60000 → b 墙钟起）
    CHECK(cal.wallMsOf(60000) - cal.wallMsOf(59999) > 200000);
    // b 段内按抽帧速率推进：+1s 流内 → +1.89s 墙钟
    CHECK(qAbs(cal.wallMsOf(61000) - (wallB + 1890)) <= 2000);
    // b 段末尾不越过下一边界（b 实测 51s：EOF 前仍按 b 速率延伸）
    CHECK(qAbs(cal.wallMsOf(60000 + 50000) - (wallB + 1890 * 50)) <= 3000);
}

/// 场景 B（兼容路径）：无修剪/无剔除/无实测 —— 连续三段 rate 1.0，行为与
/// 旧版等价（连续墙钟、无缺口警告、无变速标注），但分段模式已生效。
static void testClassicContinuous()
{
    const qint64 W = epochOf(2026, 8, 19, 3, 0, 0);
    QVector<SortEntry> ordered;
    ordered << mkEntry(QStringLiteral("s0.mp4"), W, 60000, W + 60000);
    ordered << mkEntry(QStringLiteral("s1.mp4"), W + 60000, 60000, W + 120000);
    ordered << mkEntry(QStringLiteral("s2.mp4"), W + 120000, 60000, W + 180000);

    const QString dir = QDir::temp().absolutePath()
        + QStringLiteral("/lumenarc_sidecar_test");
    QDir().mkpath(dir);
    const QString out = dir + QStringLiteral("/merged_classic.mp4");
    QFile::remove(out + QStringLiteral(".lumencal.json"));

    QString err;
    CHECK(CalibrationService::writeSidecar(out, ordered, &err));
    CHECK(err.isEmpty());

    TimeCalibration cal;
    QString warning;
    CHECK(CalibrationService::loadSidecar(out, &cal, &warning));
    CHECK(cal.piecewiseMode());
    CHECK(cal.piecewise.segments.size() == 3);
    CHECK(!cal.speedVariant);
    CHECK(warning.isEmpty());                    // 连续 → 无缺口警告
    CHECK(cal.wallMsOf(0) == W);
    CHECK(cal.wallMsOf(60000) == W + 60000);     // 边界连续
    CHECK(cal.wallMsOf(175000) == W + 175000);
    // 无墙钟段不入表：全 None 组回退为空映射（与旧 affine 回退一致）
}

/// 场景 C（v1.12.0 速率分母修复）：尾帧墙钟对应尾帧流内实测位置而非总时长。
/// 时长 61s、尾帧在 58s 处、首尾墙钟恰差 58s 的正常段，旧代码按 61s 分母
/// 算出 rate≈0.951（系统性偏慢，越秀批普遍出现 0.94）；修复后 rate=1.0，
/// 不标 speedVariant。
static void testRateDenominator()
{
    const qint64 W = epochOf(2026, 8, 19, 3, 0, 0);
    QVector<SortEntry> ordered;
    auto e1 = mkEntry(QStringLiteral("n0.mp4"), W, 61000, W + 58000);
    e1.ocrEndFrameRelMs = 58000;    // 尾帧在流内 58s 处：rate = 58s/58s = 1.0
    auto e2 = mkEntry(QStringLiteral("n1.mp4"), W + 61000, 61000, W + 119000);
    e2.ocrEndFrameRelMs = 58000;
    ordered << e1 << e2;

    const QString dir = QDir::temp().absolutePath()
        + QStringLiteral("/lumenarc_sidecar_test");
    QDir().mkpath(dir);
    const QString out = dir + QStringLiteral("/merged_rate.mp4");
    QFile::remove(out + QStringLiteral(".lumencal.json"));

    QString err;
    CHECK(CalibrationService::writeSidecar(out, ordered, &err));
    TimeCalibration cal;
    QString warning;
    CHECK(CalibrationService::loadSidecar(out, &cal, &warning));
    CHECK(cal.piecewiseMode());
    CHECK(cal.piecewise.segments.size() == 2);
    CHECK(!cal.speedVariant);       // rate=1.0：不标变速（修复前 0.951 会标）
    // 段内推进速率 1:1
    CHECK(cal.wallMsOf(30000) == W + 30000);
    CHECK(qAbs(cal.wallMsOf(61000 + 10000) - (W + 61000 + 10000)) <= 1000);
    // 段尾（58s 尾帧之后延伸 3s 到边界）不越过下段起点
    CHECK(cal.wallMsOf(60000) <= W + 61000 + 3000);
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    Q_UNUSED(app)
    testTrimSkipActual();
    testClassicContinuous();
    testRateDenominator();
    fprintf(stderr, "checks: %d failures: %d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
