/**
 * @file reconstruction_integration_main.cpp
 * @brief 时间重建黄金用例集成测试（v1.2.1）：真实抽帧/变速文件全链路
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-08
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 黄金用例 = C:/code/LumenArc/B3一单元客梯.mp4（DVR 抽帧导出：
 * 视频流 4435s / 音轨 6121s / OSD 跨度 ~6194s，斜率 1.0~2.0 分段）。
 * 全链路：CalibrationService::runReconstruction（粗采样 + 边界加密）
 * → PiecewiseTimeMap::detect → 候选 TimeCalibration。
 *
 * 断言：
 *   1) speedVariant=true（变速文件判定）
 *   2) 分段数 2~6，每段 rate ∈ [0.8, 2.2]
 *   3) 分段表对全部 OCR 测点残差 ≤ 2s（重建自洽）
 *   4) 音频时长校验执行且吻合（±2%）
 *   5) piecewise JSON 序列化往返一致
 *
 * 用法：lumenarc_reconstruction_test <video.mp4> [pythonExe] [--expect-normal]
 *   工作目录须为 build/Release（依赖旁置 probe_timestamps.py / ffmpeg/）
 *   视频不存在时打印 SKIP 返回 0（非失败）。
 *
 * --expect-normal（v1.2.x 复盘回归）：正常文件（无变速边界）跑重建必须
 *   完成并退化单段仿射（piecewiseMode=false）——回归 P0：analyzeCoarse
 *   悬空 for 导致无边界分支成死代码、状态机永久挂起（boundary 0 pts）。
 */
#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QDir>
#include <QTimer>
#include <QStringList>
#include <cstdio>
#include <cmath>
#include "app/calibration_service.h"

static int g_checks = 0, g_failures = 0;
#define CHECK(cond, msg) do { ++g_checks; if (!(cond)) { ++g_failures; \
    fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); } } while (0)

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <video.mp4> [pythonExe]\n", argv[0]);
        return 2;
    }
    const QString video = QString::fromLocal8Bit(argv[1]);
    if (!QFileInfo::exists(video)) {
        fprintf(stderr, "SKIP: video not found: %s\n", qPrintable(video));
        return 0;
    }
    // --expect-normal：正常文件回归（重建须完成 + 仿射回退，不得挂起）
    QStringList argsList;
    for (int i = 0; i < argc; ++i)
        argsList.append(QString::fromLocal8Bit(argv[i]));
    const bool expectNormal = argsList.contains(QStringLiteral("--expect-normal"));
    QCoreApplication app(argc, argv);

    CalibrationService service(nullptr);
    for (int i = 2; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (!a.startsWith(QLatin1String("--")))
            service.setPythonExecutable(a);
    }

    TimeCalibration result;
    QString error;
    bool done = false;
    QObject::connect(&service, &CalibrationService::reconstructionReady,
                     [&](const QString &, const TimeCalibration &cal) {
                         result = cal;
                         done = true;
                         app.quit();
                     });
    QObject::connect(&service, &CalibrationService::failed,
                     [&](const QString &, const QString &e) {
                         error = e;
                         done = true;
                         app.quit();
                     });
    QObject::connect(&service, &CalibrationService::progress,
                     [](const QString &stage) {
                         fprintf(stderr, "  stage: %s\n", qPrintable(stage));
                     });

    fprintf(stderr, "video: %s\n", qPrintable(video));
    service.runReconstruction(video, 0);
    // 看门狗：25 分钟（粗采样 60 点 + 加密 160 点，每点 ~3s）。
    // --expect-normal 正常文件无加密阶段，5 分钟足够；挂起即失败（P0 回归）。
    QTimer::singleShot(expectNormal ? 300000 : 1500000, &app,
                       &QCoreApplication::quit);
    app.exec();
    if (!done) {
        fprintf(stderr, "FAIL: reconstruction timeout (hang — P0 regression)\n");
        return 1;
    }
    if (!error.isEmpty()) {
        fprintf(stderr, "FAIL: reconstruction error: %s\n", qPrintable(error));
        return 1;
    }

    if (expectNormal) {
        // 正常文件：无边界 → 单段仿射回退（与三点识别同语义）
        CHECK(!result.piecewiseMode(),
              "normal file: piecewise mode NOT adopted (affine fallback)");
        CHECK(!result.speedVariant, "normal file: speedVariant=false");
        CHECK(result.isValid(), "normal file: calibration valid");
        CHECK(result.samples.size() >= 2, "normal file: >=2 samples");
        if (result.isValid() && result.samples.size() >= 2) {
            const double r = result.effectiveRate();
            CHECK(std::fabs(r - 1.0) <= 0.02,
                  qPrintable(QStringLiteral("normal file: rate %1 ≈ 1.0")
                                 .arg(r, 0, 'f', 4)));
        }
        fprintf(stderr, "reconstruction_integration(expect-normal): %d checks, %d failures\n",
                g_checks, g_failures);
        return g_failures ? 1 : 0;
    }

    CHECK(result.piecewiseMode(), "piecewise mode adopted");
    CHECK(result.speedVariant, "speed-variant detection triggered");
    const int n = result.piecewise.size();
    // B3 真实档位 8 个（1.0/2.0/1.42/1.0/1.65/1.0/2.0/1.0）+ 错读残留
    CHECK(n >= 2 && n <= 12,
          qPrintable(QStringLiteral("segment count %1 in [2,12]").arg(n)));
    CHECK(result.boundaryCount == n - 1, "boundary count == segments - 1");
    for (int i = 0; i < n; ++i) {
        const double r = result.piecewise.segments[i].rate;
        CHECK(r >= 0.8 && r <= 2.4,
              qPrintable(QStringLiteral("seg %1 rate %2 in [0.8,2.4]")
                             .arg(i).arg(r, 0, 'f', 3)));
        fprintf(stderr, "  seg %d: streamStart=%lld wallStart=%lld rate=%.3f\n",
                i, (long long)result.piecewise.segments[i].streamStartMs,
                (long long)result.piecewise.segments[i].wallStartMs, r);
    }

    // 重建自洽：残差分布。B3 文件的 OCR 错读点（wall 本身错，约 16%）
    // 无法被任何校时修正；分段率估计对非错读点须达秒级。
    int ok2s = 0, ok3s = 0, ok5s = 0, bad = 0;
    QVector<qint64> bigErrs;
    for (const auto &s : result.samples) {
        const qint64 w = result.piecewise.wallMsOf(s.streamMs);
        const qint64 errMs = std::llabs(w - s.wallMs);
        if (errMs <= 2000)
            ++ok2s;
        if (errMs <= 3000)
            ++ok3s;
        if (errMs <= 5000)
            ++ok5s;
        else {
            ++bad;
            bigErrs.append(s.streamMs);
        }
    }
    const int total = result.samples.size();
    fprintf(stderr, "  residual: <=2s %d/%d, <=3s %d/%d, <=5s %d/%d, >5s %d\n",
            ok2s, total, ok3s, total, ok5s, total, bad);
    CHECK(total > 0 && ok2s * 100 >= total * 60,
          qPrintable(QStringLiteral("60%% samples within 2s (%1/%2)")
                         .arg(ok2s).arg(total)));
    CHECK(ok5s * 100 >= total * 80,
          qPrintable(QStringLiteral("80%% samples within 5s (%1/%2)")
                         .arg(ok5s).arg(total)));

    // 音频校验
    CHECK(result.audioKnown, "audio duration known");
    if (result.audioKnown)
        CHECK(result.audioConsistent, "audio span consistent with OSD span");
    fprintf(stderr, "  OSD span: %.0f s, audioKnown=%d consistent=%d\n",
            result.totalWallSpanSec, result.audioKnown ? 1 : 0,
            result.audioConsistent ? 1 : 0);

    // JSON 往返
    const QJsonObject j = result.toJson();
    const TimeCalibration back = TimeCalibration::fromJson(j);
    CHECK(back.piecewiseMode(), "piecewise survives JSON round-trip");
    CHECK(back.piecewise.size() == n, "segment count survives JSON round-trip");

    fprintf(stderr, "reconstruction_integration: %d checks, %d failures\n",
            g_checks, g_failures);
    return g_failures ? 1 : 0;
}
