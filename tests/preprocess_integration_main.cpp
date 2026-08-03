/**
 * @file preprocess_integration_main.cpp
 * @brief 前处理引擎端到端集成测试（真实 ffmpeg/Python 子进程）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 用法：lumenarc_preprocess_integration <clips_dir> <out_dir> <base_epoch_s>
 * clips_dir 为 tools/m0_synth_benchmark.py 生成的素材目录（含 ground truth）。
 * 覆盖（design §12.3）：
 *  - 探测：编码/分辨率/帧率/伪 MP4 容器嗅探/时长
 *  - OCR 引擎端到端：wallStart 与 ground truth 偏差 ≤ 3s（合成素材）
 *  - 拼接：同参数段流拷贝 → 输出时长 = Σ段长 ±2s，可探测
 *  - 转码：伪 MP4 → MP4/H.264/yuv420p
 */
#include "infrastructure/media_probe_engine.h"
#include "infrastructure/timestamp_ocr_engine.h"
#include "infrastructure/concat_engine.h"
#include "infrastructure/transcode_engine.h"
#include "domain/concat_precheck.h"
#include "domain/smart_sorter.h"

#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QTimer>
#include <QDateTime>
#include <QFileInfo>
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

/// 合成素材 ground truth：drawtext gmtime 渲染 UTC 墙钟，OCR 管线按本地墙钟
/// 解析（监控 OSD 语义）→ truth = UTC 渲染值的本地解释
static qint64 localTruthMs(qint64 utcEpochMs)
{
    const QDateTime utc = QDateTime::fromMSecsSinceEpoch(utcEpochMs, Qt::UTC);
    return QDateTime(utc.date(), utc.time(), Qt::LocalTime).toMSecsSinceEpoch();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    // 诊断模式：--probe <files...> 打印真实探测结果（现场排障用）
    if (argc > 2 && (QLatin1String(argv[1]) == QLatin1String("--probe")
                     || QLatin1String(argv[1]) == QLatin1String("--sort"))) {
        const bool sortMode = QLatin1String(argv[1]) == QLatin1String("--sort");
        QVector<ProbeResult> probes;
        for (int i = 2; i < argc; ++i) {
            const QString f = QString::fromLocal8Bit(argv[i]);
            const ProbeResult r = MediaProbeEngine::probeOne(f);
            probes.append(r);
            const QString absStr = r.absStartEpochMs > 0
                ? QDateTime::fromMSecsSinceEpoch(r.absStartEpochMs, Qt::LocalTime)
                      .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))
                : QStringLiteral("-");
            fprintf(stderr,
                "[PROBE] %s | ok=%d %s/%s %dx%d fps=%.3f %s dur=%lldms "
                "startRel=%lldms absStart=%lldms(%s) audio=%d/%s kf=%dms(sparse=%d) err=%s\n",
                QFileInfo(f).fileName().toUtf8().constData(), int(r.ok()),
                r.container.toUtf8().constData(), r.videoCodec.toUtf8().constData(),
                r.width, r.height, r.fps, r.pixFmt.toUtf8().constData(),
                (long long)r.durationMs, (long long)r.startTimeMs,
                (long long)r.absStartEpochMs, absStr.toUtf8().constData(),
                r.audioStreams, r.audioCodec.toUtf8().constData(),
                r.keyframeIntervalMs, int(r.keyframeSparse),
                r.probeError.toUtf8().constData());
        }
        if (sortMode) {
            const QVector<SortGroup> groups = smartSort(probes, {});
            fprintf(stderr, "[SORT] %lld group(s)\n", (long long)groups.size());
            for (const auto &g : groups) {
                fprintf(stderr, "  group '%s' files=%d suspicious=%d\n",
                        g.channel.toUtf8().constData(), g.ordered.size(),
                        int(g.suspicious));
                for (int i = 0; i < g.ordered.size(); ++i) {
                    const auto &e = g.ordered[i];
                    fprintf(stderr, "    %2d. %s start=%lld(%s) kind=%d dur=%llds\n",
                            i + 1,
                            QFileInfo(e.filePath).fileName().toUtf8().constData(),
                            (long long)e.startMs,
                            e.startMs > 0
                                ? QDateTime::fromMSecsSinceEpoch(e.startMs, Qt::LocalTime)
                                      .toString(QStringLiteral("MM-dd HH:mm:ss"))
                                      .toUtf8().constData()
                                : "-",
                            e.sourceKind, (long long)(e.durationMs / 1000));
                }
                for (const auto &w : g.warnings)
                    fprintf(stderr, "    WARN type=%d delta=%lld %s\n", int(w.type),
                            (long long)w.deltaMs, w.detail.toUtf8().constData());
            }
        }
        return 0;
    }
    if (argc < 4) {
        fprintf(stderr, "usage: %s <clips_dir> <out_dir> <base_epoch_s>\n", argv[0]);
        return 2;
    }
    const QDir clipsDir(QString::fromUtf8(argv[1]));
    QDir outDir(QString::fromUtf8(argv[2]));
    outDir.mkpath(QStringLiteral("."));
    const qint64 baseEpochMs = qint64(QString::fromUtf8(argv[3]).toLongLong()) * 1000;

    const QString seg00 = clipsDir.filePath(QStringLiteral("seg_00_normal.mp4"));
    const QString seg01 = clipsDir.filePath(QStringLiteral("seg_01_normal.mp4"));
    const QString seg02 = clipsDir.filePath(QStringLiteral("seg_02_normal.mp4"));
    const QString seg06 = clipsDir.filePath(QStringLiteral("seg_06_pseudo_ts.mp4"));

    // --- 1. 探测 ---
    fprintf(stderr, "[IT] probing...\n");
    const ProbeResult p00 = MediaProbeEngine::probeOne(seg00);
    CHECK(p00.ok());
    CHECK(p00.videoCodec == QLatin1String("h264"));
    CHECK(p00.width == 1280 && p00.height == 720);
    CHECK(qAbs(p00.fps - 25.0) < 0.1);
    CHECK(qAbs(p00.durationMs - 30000) < 2000);
    CHECK(p00.container.contains(QLatin1String("mp4")));
    CHECK(p00.firstPktKeyframe);

    const ProbeResult p06 = MediaProbeEngine::probeOne(seg06);
    CHECK(p06.ok());
    CHECK(p06.container.contains(QLatin1String("mpegts")));   // 内容嗅探：伪 MP4
    CHECK(!p06.indexed);
    CHECK(qAbs(p06.durationMs - 30000) < 3000);

    // --- 2. OCR 引擎端到端 ---
    fprintf(stderr, "[IT] ocr engine...\n");
    TimestampOcrEngine ocr;
    QString availErr;
    if (!ocr.available(&availErr)) {
        fprintf(stderr, "[IT] OCR engine unavailable: %s (skipping OCR stage)\n",
                availErr.toUtf8().constData());
    } else {
        QVector<OcrResult> ocrResults;
        QEventLoop loop;
        QTimer guard;
        guard.setSingleShot(true);
        QObject::connect(&ocr, &TimestampOcrEngine::ocrFinished, &loop,
                         [&](const QVector<OcrResult> &r) { ocrResults = r; loop.quit(); });
        QObject::connect(&ocr, &TimestampOcrEngine::engineError, &loop,
                         [&](PreprocessError, const QString &) { loop.quit(); });
        QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
        guard.start(300000);
        QMap<QString, qint64> durations{{seg00, 30000}, {seg06, 30000}};
        ocr.run({seg00, seg06}, outDir.filePath(QStringLiteral("ocr_work")),
                durations, outDir.filePath(QStringLiteral("ocr_evidence")), false);
        loop.exec();
        CHECK(ocrResults.size() == 2);
        for (const auto &r : ocrResults) {
            const qint64 truth = localTruthMs(r.filePath.contains(QStringLiteral("seg_00"))
                ? baseEpochMs : baseEpochMs + 210000);
            fprintf(stderr, "[IT] ocr %s wallStart=%lld truth=%lld err=%s\n",
                    r.filePath.toUtf8().constData(), (long long)r.wallStartMs,
                    (long long)truth, r.ocrError.toUtf8().constData());
            CHECK(r.ocrError.isEmpty());
            CHECK(r.wallStartMs > 0 && qAbs(r.wallStartMs - truth) <= 3000);
            CHECK(QFileInfo::exists(r.firstFrameImg));   // 证据截图落盘
        }
    }

    // --- 3. 一致性校验 ---
    const PrecheckResult pc = concatPrecheck({
        MediaProbeEngine::probeOne(seg00), MediaProbeEngine::probeOne(seg01),
        MediaProbeEngine::probeOne(seg02)});
    CHECK(!pc.hasBlock());

    // --- 4. 拼接 ---
    fprintf(stderr, "[IT] concat...\n");
    ConcatEngine concat;
    const QString concatOut = outDir.filePath(QStringLiteral("concat_out.mp4"));
    {
        QEventLoop loop;
        QTimer guard;
        guard.setSingleShot(true);
        bool okDone = false;
        QObject::connect(&concat, &ConcatEngine::finished, &loop,
                         [&](const QString &) { okDone = true; loop.quit(); });
        QObject::connect(&concat, &ConcatEngine::failed, &loop,
                         [&](PreprocessError e, const QString &d) {
                             fprintf(stderr, "[IT] concat failed %d %s\n", int(e),
                                     d.toUtf8().constData());
                             loop.quit();
                         });
        QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
        guard.start(120000);
        ConcatRequest req;
        req.orderedFiles = {seg00, seg01, seg02};
        req.outputPath = concatOut;
        req.workDir = outDir.filePath(QStringLiteral("concat_work"));
        req.totalDurationMs = 90000;
        concat.run(req);
        loop.exec();
        CHECK(okDone);
    }
    const ProbeResult pcOut = MediaProbeEngine::probeOne(concatOut);
    CHECK(pcOut.ok());
    CHECK(qAbs(pcOut.durationMs - 90000) < 2000);   // 时长 = Σ段长 ±2s
    CHECK(pcOut.videoCodec == QLatin1String("h264"));

    // --- 5. 转码（伪 MP4 → MP4/H.264/yuv420p） ---
    fprintf(stderr, "[IT] transcode...\n");
    TranscodeEngine xcode;
    const QString xcodeOut = outDir.filePath(QStringLiteral("xcode_out.mp4"));
    {
        QEventLoop loop;
        QTimer guard;
        guard.setSingleShot(true);
        bool okDone = false;
        QObject::connect(&xcode, &TranscodeEngine::finished, &loop,
                         [&](const QString &) { okDone = true; loop.quit(); });
        QObject::connect(&xcode, &TranscodeEngine::failed, &loop,
                         [&](PreprocessError e, const QString &d) {
                             fprintf(stderr, "[IT] transcode failed %d %s\n", int(e),
                                     d.toUtf8().constData());
                             loop.quit();
                         });
        QObject::connect(&guard, &QTimer::timeout, &loop, &QEventLoop::quit);
        guard.start(300000);
        TranscodeRequest req;
        req.input = seg06;
        req.output = xcodeOut;
        req.durationMs = 30000;
        xcode.run(req);
        loop.exec();
        CHECK(okDone);
    }
    const ProbeResult pxOut = MediaProbeEngine::probeOne(xcodeOut);
    CHECK(pxOut.ok());
    CHECK(pxOut.container.contains(QLatin1String("mp4")));
    CHECK(pxOut.videoCodec == QLatin1String("h264"));
    CHECK(pxOut.pixFmt == QLatin1String("yuv420p"));
    CHECK(qAbs(pxOut.durationMs - 30000) < 2000);   // 时长 ±1s 级

    fprintf(stderr, "[IT] checks: %d failures: %d\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
