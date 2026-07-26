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
        while (rec.frameCount == before && guard.elapsed() < 10000) {
            waitFor(loop, 10000);
        }
        QObject::disconnect(conn);

        if (rec.frameCount == before) {
            printf("[FAIL] seek %lldms: no frame delivered within 10s\n", target);
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
    } else {
        printf("[FAIL] unknown scenario %s\n", qPrintable(scenario));
        return 1;
    }

    printf(failures == 0 ? "[RESULT] PASS\n" : "[RESULT] FAIL (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
