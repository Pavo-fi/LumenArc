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
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QImage>
#include <QVector>
#include <QRandomGenerator>
#include <cstdio>
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
    printf("[setup] duration=%lldms fps=%.2f size=%dx%d firstFramePos=%lld\n",
           rec.duration, engine.fps(), engine.videoWidth(), engine.videoHeight(),
           rec.lastPos);

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
        if (audioBytes <= 0) {
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
