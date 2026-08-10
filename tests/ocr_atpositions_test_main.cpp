/**
 * @file ocr_atpositions_test_main.cpp
 * @brief TimestampOcrEngine::runAtPositions 冒烟测试（v1.2.0 任务 2 C++ 半链路）
 *
 * 对合成走秒素材（tests 旁 build_tmp/caltest/synth.mp4，drawtext 渲染
 * 墙钟 = 2026-07-22 00:00:00 UTC + pts）在 3 个流内位置取样，断言：
 *   1) 每个请求位置产出一个测点；2) 识别墙钟与素材真值误差 ≤ 2s；
 *   3) relMs 为 showinfo 实测修正值（允差 ±1s）；4) 证据帧 PNG 存在。
 * 用法：lumenarc_ocr_atpositions_test <synth.mp4> [pythonExe]
 *   工作目录须为 build/Release（依赖旁置 probe_timestamps.py / python/ / ffmpeg/）
 */
#include <QCoreApplication>
#include <QEventLoop>
#include <QFileInfo>
#include <QDir>
#include <QDateTime>
#include <cstdio>
#include "infrastructure/timestamp_ocr_engine.h"

static int g_checks = 0, g_failures = 0;
#define CHECK(cond, msg) do { ++g_checks; if (!(cond)) { ++g_failures; \
    fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); } } while (0)

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "usage: %s <synth.mp4> [pythonExe]\n", argv[0]);
        return 2;
    }
    const QString video = QString::fromLocal8Bit(argv[1]);
    if (!QFileInfo::exists(video)) {
        fprintf(stderr, "video not found: %s\n", qPrintable(video));
        return 2;
    }
    QCoreApplication app(argc, argv);

    TimestampOcrEngine engine;
    if (argc >= 3)
        engine.setPythonExecutable(QString::fromLocal8Bit(argv[2]));

    const QString evDir = QDir::temp().absoluteFilePath(QStringLiteral("ocr_at_test"));
    QDir().mkpath(evDir);

    QVector<TimeCalibration::Sample> samples;
    QString error;
    QObject::connect(&engine, &TimestampOcrEngine::atPositionsFinished,
                     [&](const QVector<TimeCalibration::Sample> &s) {
                         samples = s;
                         app.quit();
                     });
    QObject::connect(&engine, &TimestampOcrEngine::atPositionsFailed,
                     [&](const QString &e) {
                         error = e;
                         app.quit();
                     });

    // 素材：30s，drawtext 渲染本地墙钟 2026-07-22 00:00:00 + pts（与真实
    // 摄像头 OSD 一致：显示本地时间，OCR 也按本地解析）
    const qint64 baseEpochMs = QDateTime(QDate(2026, 7, 22), QTime(0, 0, 0))
                                   .toMSecsSinceEpoch();
    const QVector<qint64> positions{2000, 15000, 27000};
    engine.runAtPositions(video, positions, 30000, evDir);
    app.exec();

    CHECK(error.isEmpty(), qPrintable(QStringLiteral("engine failed: ") + error));
    CHECK(samples.size() == positions.size(), "one sample per requested position");
    for (int i = 0; i < samples.size() && i < positions.size(); ++i) {
        const auto &s = samples[i];
        // 识别墙钟真值：base + 实测流内位置（不是请求位置）
        const qint64 truth = baseEpochMs + s.streamMs;
        const qint64 errMs = qAbs(s.wallMs - truth);
        CHECK(errMs <= 2000,
              qPrintable(QStringLiteral("wall truth err %1ms at pos %2")
                             .arg(errMs).arg(i)));
        CHECK(qAbs(s.streamMs - positions[i]) <= 1000,
              qPrintable(QStringLiteral("measured relMs deviates %1ms from request at %2")
                             .arg(qAbs(s.streamMs - positions[i])).arg(i)));
        CHECK(!s.frameImgPath.isEmpty() && QFileInfo::exists(s.frameImgPath),
              qPrintable(QStringLiteral("evidence frame missing at %1").arg(i)));
        CHECK(s.conf >= 0.5,
              qPrintable(QStringLiteral("conf too low %1 at %2").arg(s.conf).arg(i)));
    }
    // 三点拟合应得到 rate≈1.0（合成素材无漂移）
    if (samples.size() >= 2) {
        const auto fr = TimeCalibration::fit(samples);
        CHECK(fr.ok, "fit ok on synthetic samples");
        CHECK(std::fabs(fr.rate - 1.0) < 1e-3, "synthetic rate ~1.0");
    }

    // ---- ROI 模式（v1.2.1）：框选左上角时间戳区域，应同样识别成功 ----
    {
        QVector<TimeCalibration::Sample> roiSamples;
        QString roiError;
        QObject::connect(&engine, &TimestampOcrEngine::atPositionsFinished,
                         [&](const QVector<TimeCalibration::Sample> &s) {
                             roiSamples = s;
                             app.quit();
                         });
        QObject::connect(&engine, &TimestampOcrEngine::atPositionsFailed,
                         [&](const QString &e) {
                             roiError = e;
                             app.quit();
                         });
        // synth drawtext 在 x=40,y=40（1280x720）→ 归一化左上角区域
        engine.runAtPositions(video, positions, 30000, evDir,
                              QRectF(0.0, 0.0, 0.55, 0.18));
        app.exec();
        CHECK(roiError.isEmpty(),
              qPrintable(QStringLiteral("roi engine failed: ") + roiError));
        CHECK(roiSamples.size() == positions.size(),
              "roi: one sample per requested position");
        for (int i = 0; i < roiSamples.size() && i < positions.size(); ++i) {
            const qint64 truth = baseEpochMs + roiSamples[i].streamMs;
            const qint64 errMs = qAbs(roiSamples[i].wallMs - truth);
            CHECK(errMs <= 2000,
                  qPrintable(QStringLiteral("roi wall truth err %1ms at pos %2")
                                 .arg(errMs).arg(i)));
        }
    }

    fprintf(stderr, "ocr_atpositions_test: %d checks, %d failures\n",
            g_checks, g_failures);
    return g_failures ? 1 : 0;
}
