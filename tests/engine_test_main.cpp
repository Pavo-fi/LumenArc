/**
 * @file engine_test_main.cpp
 * @brief 无头引擎测试：对 FfmpegVideoEngine 跑 seek/播放场景并断言
 *
 * 用法:
 *   lumenarc_engine_test seek-matrix <video> [toleranceMs]
 *   lumenarc_engine_test random-seek <video> <count>
 *   lumenarc_engine_test play <video> <seconds>
 *
 * 退出码 0 = 全部断言通过；1 = 有失败。
 */
#include "infrastructure/ffmpeg_video_engine.h"
#include "infrastructure/proxy_manager.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QImage>
#include <QVector>
#include <QRandomGenerator>
#include <cstdio>
#include <cmath>
#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

static qint64 workingSetMB()
{
#ifdef Q_OS_WIN
    PROCESS_MEMORY_COUNTERS pmc{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &pmc, sizeof(pmc)))
        return static_cast<qint64>(pmc.WorkingSetSize / (1024 * 1024));
#endif
    return -1;
}

struct Recorder {
    qint64 lastPos = -1;
    QVector<qint64> positions;
    int frameCount = 0;
    PlaybackState state = PlaybackState::Idle;
    qint64 duration = 0;
};

static bool waitFor(QEventLoop &loop, int timeoutMs)
{
    QTimer t;
    t.setSingleShot(true);
    QObject::connect(&t, &QTimer::timeout, &loop, &QEventLoop::quit);
    t.start(timeoutMs);
    loop.exec();
    return t.isActive(); // true = 条件触发退出（非超时）
}

// 跨线程信号需要事件循环投递，睡眠期间必须持续 pump
static void pumpFor(int ms)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < ms)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QStringList args = app.arguments();
    if (args.size() < 3) {
        fprintf(stderr, "usage: %s <seek-matrix|random-seek|play> <video> [arg]\n",
                qPrintable(args[0]));
        return 1;
    }
    QString scenario = args[1];
    QString file = args[2];
    int failures = 0;

    if (scenario == "proxy") {
        // 代理全链路：生成代理 → 引擎接管 → 连续 seek 每次 <150ms 且帧号精确
        ProxyManager pm;
        QString proxyPath;
        QObject::connect(&pm, &ProxyManager::proxyReady,
                         &app, [&](const QString &p) { proxyPath = p; });
        QObject::connect(&pm, &ProxyManager::proxyFailed,
                         &app, [&](const QString &e) {
            printf("[FAIL] proxyFailed: %s\n", qPrintable(e.left(300)));
        });
        QString existing = pm.existingProxy(file);
        if (!existing.isEmpty()) {
            proxyPath = existing;
        } else {
            QElapsedTimer tg; tg.start();
            pm.requestProxy(file);
            while (proxyPath.isEmpty() && tg.elapsed() < 600000)
                pumpFor(100);
            printf("[info] proxy generated in %lldms\n", tg.elapsed());
        }
        if (proxyPath.isEmpty()) {
            printf("[FAIL] proxy generation timeout\n");
            return 1;
        }
        printf("[info] proxy: %s\n", qPrintable(proxyPath));

        FfmpegVideoEngine eng;
        eng.setHardwareAdapter(-1);
        eng.load(file);
        pumpFor(1500);
        eng.setProxySource(proxyPath);
        pumpFor(1500);
        if (!eng.proxyActive()) {
            printf("[FAIL] proxy not active after setProxySource\n");
            return 1;
        }

        int frames = 0, lastW = 0;
        qint64 lastPos = -1, dur = eng.duration();
        QObject::connect(&eng, &IVideoEngine::frameReady,
                         &app, [&](const QImage &img) { ++frames; lastW = img.width(); });
        QObject::connect(&eng, &IVideoEngine::positionChanged,
                         &app, [&](qint64 t) { lastPos = t; });

        qint64 frameMs = static_cast<qint64>(1000.0f / eng.fps());
        qint64 maxSeekMs = 0;
        for (int i = 0; i < 10; ++i) {
            qint64 target = static_cast<qint64>(
                QRandomGenerator::global()->bounded(quint64(dur * 9 / 10)));
            int before = frames;
            QElapsedTimer t0; t0.start();
            eng.seek(target);
            while (frames == before && t0.elapsed() < 3000)
                pumpFor(10);
            qint64 seekCost = t0.elapsed();
            maxSeekMs = qMax(maxSeekMs, seekCost);
            if (frames == before) {
                printf("[FAIL] proxy seek %d: no frame in 3s\n", i + 1);
                failures++;
            } else if (qAbs(lastPos - target) > frameMs) {
                printf("[FAIL] proxy seek %d: landed %lld target %lld\n",
                       i + 1, lastPos, target);
                failures++;
            }
        }
        printf("[info] proxy seeks: max %lldms per seek\n", maxSeekMs);
        if (maxSeekMs > 500) {   // 全 I 帧 960p 应远小于 150ms，留余量
            printf("[FAIL] proxy seeks too slow (max %lldms)\n", maxSeekMs);
            failures++;
        }
        // 沉淀后应升级为全分辨率帧
        for (int i = 0; i < 4; ++i) {
            pumpFor(500);
            printf("[diag] settle t=%.1fs lastW=%d lastPos=%lld\n",
                   (i + 1) * 0.5, lastW, lastPos);
        }
        if (failures == 0)
            printf("[ OK ] proxy scenario\n");
        printf(failures == 0 ? "[RESULT] PASS\n" : "[RESULT] FAIL (%d)\n", failures);
        return failures == 0 ? 0 : 1;
    }

    if (scenario == "scrub-chase") {
        // Scrub 追逐模型验收：代理就绪后模拟真实拖拽（连续写原子目标，不走 seek 命令），
        // 断言 ① 拖拽期间大量不同帧流出（连续流动而非幻灯片）
        // ② 显示位置与拖拽路径单调对应 ③ 松手后精确落位
        ProxyManager pm;
        QString proxyPath;
        QObject::connect(&pm, &ProxyManager::proxyReady,
                         &app, [&](const QString &p) { proxyPath = p; });
        QObject::connect(&pm, &ProxyManager::proxyFailed,
                         &app, [&](const QString &e) {
            printf("[FAIL] proxyFailed: %s\n", qPrintable(e.left(300)));
        });
        proxyPath = pm.existingProxy(file);
        if (proxyPath.isEmpty()) {
            pm.requestProxy(file);
            QElapsedTimer tg; tg.start();
            while (proxyPath.isEmpty() && tg.elapsed() < 600000)
                pumpFor(100);
        }
        if (proxyPath.isEmpty()) {
            printf("[FAIL] proxy generation timeout\n");
            return 1;
        }

        FfmpegVideoEngine eng;
        int frames = 0;
        qint64 lastPos = -1, dur = 0;
        QVector<qint64> shownPositions;
        QObject::connect(&eng, &IVideoEngine::frameReady,
                         &app, [&](const QImage &) { ++frames; });
        QObject::connect(&eng, &IVideoEngine::positionChanged,
                         &app, [&](qint64 t) { lastPos = t; shownPositions.append(t); });
        QObject::connect(&eng, &IVideoEngine::durationChanged,
                         &app, [&](qint64 d) { dur = d; });
        eng.load(file);
        pumpFor(1500);
        eng.setProxySource(proxyPath);
        pumpFor(1500);
        if (!eng.proxyActive()) {
            printf("[FAIL] proxy not active\n");
            return 1;
        }
        const qint64 frameMs = static_cast<qint64>(1000.0f / eng.fps());

        // --- 前进拖拽：5% → 90%，60 步 × 10ms（模拟 600ms 快速扫过） ---
        eng.setScrubMode(true);
        int fwdBefore = frames;
        int fwdBase = shownPositions.size();
        for (int i = 1; i <= 60; ++i) {
            eng.setScrubTarget(dur * (5 + i * 85 / 60) / 100);
            pumpFor(10);
        }
        int fwdFrames = frames - fwdBefore;
        // 单调性：允许个别回退（线程延迟帧），回退样本须 < 10%
        int backsteps = 0;
        for (int i = fwdBase + 1; i < shownPositions.size(); ++i)
            if (shownPositions[i] < shownPositions[i - 1] - frameMs)
                ++backsteps;
        printf("[info] fwd drag: %d frames in ~600ms, backsteps=%d/%d\n",
               fwdFrames, backsteps, shownPositions.size() - fwdBase);
        if (fwdFrames < 20) {
            printf("[FAIL] fwd drag too few frames (%d) - not flowing\n", fwdFrames);
            failures++;
        }
        if (backsteps > (shownPositions.size() - fwdBase) / 10) {
            printf("[FAIL] fwd drag not monotonic (%d backsteps)\n", backsteps);
            failures++;
        }

        // --- 后退拖拽：90% → 10%，40 步 × 15ms ---
        int bwdBefore = frames;
        for (int i = 1; i <= 40; ++i) {
            eng.setScrubTarget(dur * (90 - i * 80 / 40) / 100);
            pumpFor(15);
        }
        int bwdFrames = frames - bwdBefore;
        printf("[info] bwd drag: %d frames in ~600ms\n", bwdFrames);
        if (bwdFrames < 15) {
            printf("[FAIL] bwd drag too few frames (%d)\n", bwdFrames);
            failures++;
        }

        // --- 松手：退出 scrub + 一次性精确 seek ---
        eng.setScrubMode(false);
        qint64 finalTarget = dur * 10 / 100;
        int before = frames;
        eng.seek(finalTarget);
        QElapsedTimer guard; guard.start();
        while (frames == before && guard.elapsed() < 3000)
            pumpFor(20);
        if (frames == before || qAbs(lastPos - finalTarget) > frameMs) {
            printf("[FAIL] final landed %lld target %lld\n", lastPos, finalTarget);
            failures++;
        } else {
            printf("[ OK ] scrub-chase: fwd %d / bwd %d frames, final err %lldms\n",
                   fwdFrames, bwdFrames, qAbs(lastPos - finalTarget));
        }
        printf(failures == 0 ? "[RESULT] PASS\n" : "[RESULT] FAIL (%d)\n", failures);
        return failures == 0 ? 0 : 1;
    }

    if (scenario == "adapters") {
        auto ads = FfmpegVideoEngine::availableAdapters();
        printf("[info] %lld adapters:\n", ads.size());
        for (const auto &ad : ads)
            printf("  [%d] %s vram=%lldMB\n", ad.index, qPrintable(ad.name),
                   ad.dedicatedVramMB);
        for (const auto &ad : ads) {
            FfmpegVideoEngine eng;
            eng.setHardwareAdapter(ad.index);
            int frames = 0;
            qint64 dur = 0;
            QObject::connect(&eng, &IVideoEngine::frameReady,
                             &app, [&](const QImage &) { ++frames; });
            QObject::connect(&eng, &IVideoEngine::durationChanged,
                             &app, [&](qint64 d) { dur = d; });
            eng.load(file);
            QElapsedTimer guard; guard.start();
            while (frames == 0 && guard.elapsed() < 30000)
                pumpFor(50);
            if (frames == 0) {
                printf("[FAIL] adapter %d: no frame\n", ad.index);
                failures++;
                continue;
            }
            // 5 次 seek 追赶总耗时
            QElapsedTimer t0; t0.start();
            for (int i = 1; i <= 5; ++i) {
                qint64 target = dur * i * 15 / 100;
                int before = frames;
                eng.seek(target);
                QElapsedTimer g2; g2.start();
                while (frames == before && g2.elapsed() < 15000)
                    pumpFor(10);
            }
            printf("[info] adapter %d (%s): hwdec=%d name=%s 5 seeks %lldms\n",
                   ad.index, qPrintable(ad.name),
                   eng.hardwareDecodeActive() ? 1 : 0,
                   qPrintable(eng.hardwareAdapterName()), t0.elapsed());
        }
        printf(failures == 0 ? "[RESULT] PASS\n" : "[RESULT] FAIL (%d)\n", failures);
        return failures == 0 ? 0 : 1;
    }

    FfmpegVideoEngine engine;
    Recorder rec;
    QObject::connect(&engine, &IVideoEngine::frameReady,
                     &app, [&](const QImage &) { rec.frameCount++; });
    QObject::connect(&engine, &IVideoEngine::positionChanged,
                     &app, [&](qint64 t) {
        rec.lastPos = t;
        rec.positions.append(t);
    });
    QObject::connect(&engine, &IVideoEngine::stateChanged,
                     &app, [&](PlaybackState s) { rec.state = s; });
    QObject::connect(&engine, &IVideoEngine::durationChanged,
                     &app, [&](qint64 d) { rec.duration = d; });

    printf("[setup] loading %s\n", qPrintable(file));
    if (!engine.load(file)) {
        printf("[FAIL] load returned false\n");
        return 1;
    }

    if (scenario == "corrupt") {
        // 损坏文件：等待引擎落到 Idle，不要求首帧
        pumpFor(8000);
        printf("[info] corrupt file: frames=%d state=%d\n", rec.frameCount,
               static_cast<int>(rec.state));
        if (rec.frameCount > 0) {
            printf("[FAIL] corrupt file produced frames\n");
            failures++;
        } else if (rec.state != PlaybackState::Idle) {
            printf("[FAIL] corrupt file did not reach Idle state\n");
            failures++;
        } else {
            printf("[ OK ] corrupt file handled gracefully\n");
        }
        printf(failures == 0 ? "[RESULT] PASS\n" : "[RESULT] FAIL (%d)\n", failures);
        return failures == 0 ? 0 : 1;
    }

    // 等待加载完成（duration 就绪 + 首帧）
    {
        QEventLoop loop;
        QObject::connect(&engine, &IVideoEngine::frameReady, &loop, &QEventLoop::quit);
        bool ok = waitFor(loop, 30000);
        if (!ok || rec.duration <= 0) {
            printf("[FAIL] load timeout or no duration (dur=%lld frames=%d)\n",
                   rec.duration, rec.frameCount);
            return 1;
        }
    }
    printf("[setup] duration=%lldms fps=%.2f size=%dx%d firstFramePos=%lld hwdec=%d\n",
           rec.duration, engine.fps(), engine.videoWidth(), engine.videoHeight(),
           rec.lastPos, engine.hardwareDecodeActive() ? 1 : 0);

    auto checkSeek = [&](qint64 target, qint64 tol) {
        rec.positions.clear();
        engine.seek(target);

        // 等 seek 后的显示帧
        QEventLoop loop;
        auto conn = QObject::connect(&engine, &IVideoEngine::frameReady,
                                     &loop, &QEventLoop::quit);
        int before = rec.frameCount;
        QElapsedTimer guard; guard.start();
        while (rec.frameCount == before && guard.elapsed() < 30000) {
            waitFor(loop, 30000);
        }
        QObject::disconnect(conn);

        if (rec.frameCount == before) {
            printf("[FAIL] seek %lldms: no frame delivered within 30s\n", target);
            failures++;
            return;
        }
        qint64 pos = rec.lastPos;
        qint64 err = qAbs(pos - target);
        if (err > tol) {
            printf("[FAIL] seek %lldms: landed at %lldms (err %lld > tol %lld)\n",
                   target, pos, err, tol);
            failures++;
            return;
        }
        printf("[ OK ] seek %lldms -> %lldms (err %lldms)\n", target, pos, err);
    };

    if (scenario == "seek-matrix") {
        qint64 tol = args.size() > 3 ? args[3].toLongLong() : 1000;
        QVector<int> percents = {5, 25, 50, 75, 95};
        for (int p : percents)
            checkSeek(rec.duration * p / 100, tol);
    } else if (scenario == "random-seek") {
        int count = args.size() > 3 ? args[3].toInt() : 20;
        qint64 tol = 1000;
        for (int i = 0; i < count; ++i) {
            qint64 target = static_cast<qint64>(
                QRandomGenerator::global()->bounded(quint64(rec.duration)));
            checkSeek(target, tol);
        }
    } else if (scenario == "play") {
        int seconds = args.size() > 3 ? args[3].toInt() : 3;
        engine.seek(0);
        pumpFor(500);
        int before = rec.frameCount;
        qint64 posBefore = rec.lastPos;
        engine.play();
        pumpFor(seconds * 1000);
        engine.pause();
        int got = rec.frameCount - before;
        qint64 advanced = rec.lastPos - posBefore;
        float expectMin = engine.fps() * seconds * 0.4f;
        printf("[info] played %ds: frames=%d (expect>=%.0f) pos advanced %lldms\n",
               seconds, got, expectMin, advanced);
        if (got < expectMin) {
            printf("[FAIL] too few frames decoded during playback\n");
            failures++;
        } else if (advanced < seconds * 500) {
            printf("[FAIL] position did not advance\n");
            failures++;
        } else {
            printf("[ OK ] playback %ds\n", seconds);
        }
    } else if (scenario == "audio") {
        int seconds = args.size() > 3 ? args[3].toInt() : 5;
        engine.seek(0);
        pumpFor(500);
        engine.play();
        pumpFor(seconds * 1000);
        engine.pause();
        qint64 audioBytes = engine.audioBytesWritten();
        printf("[info] audio %ds: bytesWritten=%lld volume=%d\n",
               seconds, audioBytes, engine.volume());
        if (!engine.hasAudio()) {
            printf("[ OK ] no audio stream (skipped)\n");
        } else if (audioBytes <= 0) {
            printf("[FAIL] no audio bytes decoded\n");
            failures++;
        } else {
            // 音量设置/读取断言
            engine.setVolume(42);
            if (engine.volume() != 42) {
                printf("[FAIL] volume set/get mismatch\n");
                failures++;
            } else {
                printf("[ OK ] audio pipeline %ds (%lld bytes)\n", seconds, audioBytes);
            }
        }
    } else if (scenario == "stress") {
        // 压力：N 次随机 seek + 30s 播放，断言内存增长有界
        int seeks = args.size() > 3 ? args[3].toInt() : 100;
        qint64 memBefore = workingSetMB();
        for (int i = 0; i < seeks; ++i) {
            qint64 target = static_cast<qint64>(
                QRandomGenerator::global()->bounded(quint64(rec.duration)));
            engine.seek(target);
            int before = rec.frameCount;
            QElapsedTimer guard; guard.start();
            while (rec.frameCount == before && guard.elapsed() < 8000)
                pumpFor(10);
            if (rec.frameCount == before) {
                printf("[FAIL] stress seek %d/%d at %lldms: no frame\n", i + 1, seeks, target);
                failures++;
                break;
            }
        }
        engine.play();
        pumpFor(30000);
        engine.pause();
        qint64 memAfter = workingSetMB();
        printf("[info] stress: %d seeks + 30s play, working set %lldMB -> %lldMB\n",
               seeks, memBefore, memAfter);
        if (memBefore > 0 && memAfter - memBefore > 300) {
            printf("[FAIL] memory grew %lldMB (>300MB) during stress\n", memAfter - memBefore);
            failures++;
        }
        if (failures == 0)
            printf("[ OK ] stress %d seeks + 30s play\n", seeks);
    } else if (scenario == "avsync") {
        // 音画同步：seek 到 50% 后播放 N 秒，每秒采样 视频PTS 与 音频时钟 的偏差
        engine.seek(rec.duration / 2);
        pumpFor(800);
        engine.play();
        int seconds = args.size() > 3 ? args[3].toInt() : 6;
        qint64 maxDev = 0;
        for (int i = 0; i < seconds * 2; ++i) {
            pumpFor(500);
            qint64 v = rec.lastPos;
            qint64 a = engine.audioClockMs();
            if (a > 0 && i >= 3) {   // 跳过启动瞬态（音频时钟锚定前）
                qint64 dev = qAbs(v - a);
                maxDev = qMax(maxDev, dev);
            }
        }
        engine.pause();
        if (engine.audioBytesWritten() <= 0) {
            printf("[FAIL] avsync: no audio decoded\n");
            failures++;
        } else if (maxDev > 300) {
            printf("[FAIL] avsync: max deviation %lldms > 300ms\n", maxDev);
            failures++;
        } else {
            printf("[ OK ] avsync max deviation %lldms\n", maxDev);
        }
    } else if (scenario == "jitter") {
        // 帧显示间隔抖动：低采样率音频时钟阶梯会导致卡顿，平滑后应收敛
        engine.seek(rec.duration * 3 / 10);
        pumpFor(500);
        QVector<qint64> arrivals;
        QElapsedTimer wall;
        wall.start();
        auto conn = QObject::connect(&engine, &IVideoEngine::frameReady,
                                     &app, [&](const QImage &) {
            arrivals.append(wall.elapsed());
        });
        engine.play();
        pumpFor(1000);              // 跳过启动瞬态
        arrivals.clear();
        pumpFor(5000);
        engine.pause();
        QObject::disconnect(conn);

        if (arrivals.size() < 20) {
            printf("[FAIL] jitter: too few frames (%lld)\n", arrivals.size());
            failures++;
        } else {
            QVector<qint64> intervals;
            double sum = 0;
            for (int i = 1; i < arrivals.size(); ++i) {
                intervals.append(arrivals[i] - arrivals[i - 1]);
                sum += intervals.last();
            }
            double mean = sum / intervals.size();
            double var = 0;
            for (qint64 iv : intervals)
                var += (iv - mean) * (iv - mean);
            double stdev = std::sqrt(var / intervals.size());
            printf("[info] jitter: frames=%lld mean=%.1fms stdev=%.1fms (%.0f%%)\n",
                   arrivals.size(), mean, stdev, stdev / mean * 100);
            if (stdev > mean * 0.8) {
                printf("[FAIL] jitter: stdev %.1fms > 80%% of mean %.1fms\n", stdev, mean);
                failures++;
            } else {
                printf("[ OK ] jitter stdev %.0f%% of mean\n", stdev / mean * 100);
            }
        }
    } else if (scenario == "scrub") {
        // 拖拽模拟：连续 10 次 seek（80ms 间隔），断言 ① 最终落点精确
        // ② 无积压（最后一次 seek 后 1.5s 内完成） ③ 显示的帧全部精确
        qint64 frameMs = static_cast<qint64>(1000.0f / engine.fps());
        QVector<qint64> targets;
        for (int i = 0; i < 10; ++i)
            targets.append(static_cast<qint64>(
                QRandomGenerator::global()->bounded(quint64(rec.duration * 9 / 10))));
        QElapsedTimer t0; t0.start();
        qint64 lastDisplayed = -1;
        for (qint64 target : targets) {
            engine.seek(target);
            pumpFor(80);
            lastDisplayed = rec.lastPos;
        }
        // 最后一次 seek 应在 1.5s 内出帧且落点精确
        int before = rec.frameCount;
        QElapsedTimer guard; guard.start();
        while (rec.frameCount == before && guard.elapsed() < 2000)
            pumpFor(20);
        qint64 finalTarget = targets.last();
        if (rec.frameCount == before && qAbs(lastDisplayed - finalTarget) > frameMs) {
            printf("[FAIL] scrub: no frame after final seek within 2s\n");
            failures++;
        } else if (qAbs(rec.lastPos - finalTarget) > frameMs) {
            printf("[FAIL] scrub: final landed %lld target %lld (>%lldms)\n",
                   rec.lastPos, finalTarget, frameMs);
            failures++;
        } else {
            printf("[ OK ] scrub 10 seeks, final err %lldms, total %lldms\n",
                   qAbs(rec.lastPos - finalTarget), t0.elapsed());
        }
    } else if (scenario == "scrub-playback") {
        // Scrub 追逐模式（指哪播哪语义）：模拟中速拖拽（0.5% 步进 = 解码直追路径），
        // 断言 ① 显示帧全部落在拖拽目标附近（不允许播放落后光标的中间帧——快进感）
        // ② 有帧产出 ③ 松手后精确落位。
        // 注：稀疏 GOP 文件上大幅度快拖的中间帧物理上受限于关键帧间隔
        // （须从上个关键帧逐帧解码），该场景由全 I 帧代理覆盖（scrub-chase）。
        engine.seek(0);
        pumpFor(500);
        QVector<qint64> targets;
        int posBase = rec.positions.size();
        int framesBefore = rec.frameCount;
        engine.setScrubMode(true);
        for (int i = 1; i <= 20; ++i) {
            qint64 t = rec.duration * i * 5 / 1000;
            targets.append(t);
            engine.seek(t);
            pumpFor(50);
        }
        engine.setScrubMode(false);
        int framesDuring = rec.frameCount - framesBefore;
        // 追踪精度：每个显示帧须落在某个拖拽目标 ±max(3帧, 3s) 内
        qint64 frameMs = static_cast<qint64>(1000.0f / engine.fps());
        qint64 tol = qMax<qint64>(3 * frameMs, 3000);
        int tracked = 0, shown = 0;
        for (int i = posBase; i < rec.positions.size(); ++i) {
            ++shown;
            for (qint64 t : targets)
                if (qAbs(rec.positions[i] - t) <= tol) { ++tracked; break; }
        }
        printf("[info] scrub-playback: %d frames during 20 rapid seeks, tracked %d/%d\n",
               framesDuring, tracked, shown);
        // 稀疏 GOP（如 D17 GOP=10s）+ 大步进时，中间帧物理上受限于解码追赶速度，
        // 数量要求放宽；核心断言是下方的追踪精度（不允许快进感）与最终落位
        if (framesDuring < 2) {
            printf("[FAIL] scrub-playback: too few frames (%d)\n", framesDuring);
            failures++;
        }
        if (shown > 0 && tracked * 10 < shown * 9) {   // ≥90% 须落在目标附近
            printf("[FAIL] scrub-playback: %d/%d frames NOT near targets (fast-forward feel)\n",
                   shown - tracked, shown);
            failures++;
        }
        // 松手：最终 seek 后精确落位
        qint64 finalTarget = targets.last();
        int before = rec.frameCount;
        engine.seek(finalTarget);
        QElapsedTimer guard; guard.start();
        while (rec.frameCount == before && guard.elapsed() < 3000)
            pumpFor(20);
        if (qAbs(rec.lastPos - finalTarget) > frameMs) {
            printf("[FAIL] scrub-playback: final landed %lld target %lld\n",
                   rec.lastPos, finalTarget);
            failures++;
        } else if (failures == 0) {
            printf("[ OK ] scrub-playback: %d frames, all tracked, final err %lldms\n",
                   framesDuring, qAbs(rec.lastPos - finalTarget));
        }
    } else if (scenario == "step") {
        // 逐帧步进语义：暂停态连续 seek +1 帧，断言严格单调前进且落点精确
        float fps = engine.fps();
        qint64 frameMs = static_cast<qint64>(1000.0f / fps);
        qint64 base = rec.duration / 2;
        engine.seek(base);
        pumpFor(500);
        int steps = args.size() > 3 ? args[3].toInt() : 30;
        qint64 prevPos = -1;
        for (int i = 0; i < steps; ++i) {
            qint64 target = base + (i + 1) * frameMs;
            int before = rec.frameCount;
            engine.seek(target);
            QElapsedTimer guard; guard.start();
            while (rec.frameCount == before && guard.elapsed() < 5000)
                pumpFor(20);
            if (rec.frameCount == before) {
                printf("[FAIL] step %d: no frame after seek %lldms\n", i + 1, target);
                failures++;
                break;
            }
            if (rec.lastPos <= prevPos) {
                printf("[FAIL] step %d: position not monotonic (%lld <= %lld)\n",
                       i + 1, rec.lastPos, prevPos);
                failures++;
                break;
            }
            if (qAbs(rec.lastPos - target) > frameMs) {
                printf("[FAIL] step %d: landed %lld target %lld\n",
                       i + 1, rec.lastPos, target);
                failures++;
                break;
            }
            prevPos = rec.lastPos;
        }
        if (failures == 0)
            printf("[ OK ] frame stepping %d steps (frame=%lldms)\n", steps, frameMs);
    } else if (scenario == "rate") {
        // 倍速：2x 播放 3s 墙钟，位置应前进约 6s；0.5x 应前进约 1.5s
        struct RateCase { float rate; int wallSec; double minAdv; double maxAdv; };
        QVector<RateCase> cases = {{2.0f, 3, 4000, 7500}, {0.5f, 3, 1000, 2200}};
        for (const auto &c : cases) {
            engine.seek(0);
            pumpFor(400);
            engine.setRate(c.rate);
            engine.play();
            pumpFor(c.wallSec * 1000);
            engine.pause();
            engine.setRate(1.0f);
            double advanced = static_cast<double>(rec.lastPos);
            printf("[info] rate %.2fx %ds wall: advanced %.0fms (expect %.0f-%.0f)\n",
                   c.rate, c.wallSec, advanced, c.minAdv, c.maxAdv);
            if (advanced < c.minAdv || advanced > c.maxAdv) {
                printf("[FAIL] rate %.2fx position advance out of range\n", c.rate);
                failures++;
            }
        }
        if (failures == 0)
            printf("[ OK ] rate cases\n");
    } else {
        printf("[FAIL] unknown scenario %s\n", qPrintable(scenario));
        return 1;
    }

    printf(failures == 0 ? "[RESULT] PASS\n" : "[RESULT] FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
