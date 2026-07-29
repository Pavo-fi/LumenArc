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
extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}
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

    if (scenario == "catchup-bench") {
        // 追赶解码吞吐基准：seek 到 startMs 后连续解码 spanMs 视频，
        // 对比 skip_frame 各模式（DEFAULT / NONREF 丢非引用帧）+ 首帧填充延迟
        // 用法: catchup-bench <file> [startMs=20000] [spanMs=10000]
        const qint64 startMs = args.size() > 3 ? args[3].toLongLong() : 20000;
        const qint64 spanMs  = args.size() > 4 ? args[4].toLongLong() : 10000;
        const struct { const char *name; AVDiscard d; bool lowDelay; int threads; } modes[] = {
            {"DEFAULT", AVDISCARD_DEFAULT, false, 0},
            {"NONREF", AVDISCARD_NONREF, false, 0},
            {"LOW_DELAY", AVDISCARD_DEFAULT, true, 0},
            {"THREADS8", AVDISCARD_DEFAULT, false, 8},
        };
        for (const auto &md : modes) {
            AVFormatContext *fmt = nullptr;
            if (avformat_open_input(&fmt, file.toUtf8().constData(), nullptr, nullptr) < 0
                || avformat_find_stream_info(fmt, nullptr) < 0) {
                printf("[FAIL] open %s\n", qPrintable(file));
                return 1;
            }
            int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
            AVStream *st = fmt->streams[vs];
            const AVCodec *codec = avcodec_find_decoder(st->codecpar->codec_id);
            AVCodecContext *dec = avcodec_alloc_context3(codec);
            avcodec_parameters_to_context(dec, st->codecpar);
            dec->thread_count = md.threads; // 0=自动多线程
            dec->pkt_timebase = st->time_base;
            dec->skip_frame = md.d;
            if (md.lowDelay)
                dec->flags |= AV_CODEC_FLAG_LOW_DELAY;
            avcodec_open2(dec, codec, nullptr);
            int64_t startPtsUs = fmt->start_time == AV_NOPTS_VALUE ? 0 : fmt->start_time;
            avformat_seek_file(fmt, -1, INT64_MIN, startPtsUs + startMs * 1000,
                               startPtsUs + startMs * 1000, AVSEEK_FLAG_BACKWARD);
            avcodec_flush_buffers(dec);
            AVPacket *pkt = av_packet_alloc();
            AVFrame *fr = av_frame_alloc();
            QElapsedTimer t; t.start();
            int pkts = 0, frames = 0;
            qint64 firstDtsMs = -1, fillMs = -1;
            while (av_read_frame(fmt, pkt) >= 0) {
                if (pkt->stream_index == vs) {
                    qint64 dtsMs = av_rescale_q(pkt->dts == AV_NOPTS_VALUE ? pkt->pts : pkt->dts,
                                                st->time_base, AVRational{1, 1000});
                    if (firstDtsMs < 0)
                        firstDtsMs = dtsMs;
                    if (dtsMs - firstDtsMs > spanMs) { av_packet_unref(pkt); break; }
                    ++pkts;
                    if (avcodec_send_packet(dec, pkt) == 0)
                        while (avcodec_receive_frame(dec, fr) >= 0) {
                            ++frames;
                            if (fillMs < 0)
                                fillMs = t.elapsed();   // 首帧输出 = 管线填充延迟
                            av_frame_unref(fr);
                        }
                }
                av_packet_unref(pkt);
            }
            printf("[info] %-8s wall=%lldms pkts=%d decoded=%d (skipped %d) "
                   "throughput=%.0f pkt/s fill=%lldms\n",
                   md.name, t.elapsed(), pkts, frames, pkts - frames,
                   pkts * 1000.0 / qMax<qint64>(1, t.elapsed()), fillMs);
            av_frame_free(&fr); av_packet_free(&pkt);
            avcodec_free_context(&dec); avformat_close_input(&fmt);
        }
        printf("[RESULT] PASS\n");
        return 0;
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
                             &app, [&](const QImage &) { ++frames; eng.ackFrame(); });
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
                     &app, [&](const QImage &) { rec.frameCount++; engine.ackFrame(); });
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
        int pumpMs = args.size() > 3 ? args[3].toInt() : 50;
        for (int i = 1; i <= 20; ++i) {
            qint64 t = rec.duration * i * 5 / 1000;
            targets.append(t);
            engine.seek(t);
            pumpFor(pumpMs);
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
        // 稀疏 GOP/无索引文件 + 大步进时，中间帧数量物理上受限于解码追赶速度，
        // 数量要求放宽至 ≥1；核心断言是下方的追踪精度（不允许快进感）与最终落位。
        // 密 GOP 文件应得 20/20；稀疏 GOP 文件的流畅拖拽由全 I 帧代理覆盖（scrub-chase）
        if (framesDuring < 1) {
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
    } else if (scenario == "scrub-oscillate") {
        // 拖拽振荡场景（滚动帧缓存验收）：模拟真实慢拖的手抖模式（+600ms/−300ms 交替，
        // 40ms 泵）。后退步进在缓存内应 0ms 命中（无缓存时每次后退 = seek+重解 GOP ~100ms）。
        // 断言 ① 显示帧全部精确跟踪目标（无快进感） ② 帧数显著高于纯 seek 水平
        // ③ 松手精确落位。
        engine.seek(0);
        pumpFor(500);
        QVector<qint64> targets;
        int posBase = rec.positions.size();
        int framesBefore = rec.frameCount;
        engine.setScrubMode(true);
        qint64 cur = rec.duration / 5;
        const qint64 fwd = 600, bwd = -300;
        for (int i = 0; i < 60; ++i) {
            cur += (i % 2 == 0) ? fwd : bwd;
            targets.append(cur);
            engine.seek(cur);
            pumpFor(40);
        }
        engine.setScrubMode(false);
        int framesDuring = rec.frameCount - framesBefore;
        qint64 frameMs = static_cast<qint64>(1000.0f / engine.fps());
        int tracked = 0, shown = 0;
        for (int i = posBase; i < rec.positions.size(); ++i) {
            ++shown;
            for (qint64 t : targets)
                if (qAbs(rec.positions[i] - t) <= qMax<qint64>(2 * frameMs, 100)) { ++tracked; break; }
        }
        printf("[info] scrub-oscillate: %d frames during 60 oscillating steps, tracked %d/%d\n",
               framesDuring, tracked, shown);
        if (framesDuring < 20) {   // 无缓存时 D17 后退步均 ~100ms → 帧数远低于此
            printf("[FAIL] scrub-oscillate: too few frames (%d), cache not helping\n", framesDuring);
            failures++;
        }
        if (shown > 0 && tracked * 10 < shown * 9) {
            printf("[FAIL] scrub-oscillate: %d/%d frames NOT on targets\n", shown - tracked, shown);
            failures++;
        }
        qint64 finalTarget = targets.last();
        int before = rec.frameCount;
        engine.seek(finalTarget);
        QElapsedTimer guard; guard.start();
        while (rec.frameCount == before && guard.elapsed() < 3000)
            pumpFor(20);
        if (qAbs(rec.lastPos - finalTarget) > frameMs) {
            printf("[FAIL] scrub-oscillate: final landed %lld target %lld\n", rec.lastPos, finalTarget);
            failures++;
        } else if (failures == 0) {
            printf("[ OK ] scrub-oscillate: %d frames, tracked, final err %lldms\n",
                   framesDuring, qAbs(rec.lastPos - finalTarget));
        }
    } else if (scenario == "scrub-backward") {
        // 持续回退拖拽（滚动缓存自旋回归）：前进 15 步建缓存 → 持续回退 15 步。
        // 缓存窗口 bug（已修）：目标退出缓存范围时返回远前方帧 → 同一帧反复显示 →
        // positionChanged 洪泛（UI 失去响应）。断言信号总量有界 + 跟踪精度 + 落位。
        engine.seek(rec.duration / 2);
        pumpFor(500);
        QVector<qint64> targets;
        int posBase = rec.positions.size();
        int framesBefore = rec.frameCount;
        engine.setScrubMode(true);
        qint64 cur = rec.duration / 2;
        for (int i = 0; i < 15; ++i) { cur += 800; targets.append(cur); engine.seek(cur); pumpFor(50); }
        for (int i = 0; i < 15; ++i) { cur -= 800; targets.append(cur); engine.seek(cur); pumpFor(50); }
        engine.setScrubMode(false);
        int posEmitted = rec.positions.size() - posBase;
        int framesDuring = rec.frameCount - framesBefore;
        qint64 frameMs = static_cast<qint64>(1000.0f / engine.fps());
        int tracked = 0, shown = 0;
        for (int i = posBase; i < rec.positions.size(); ++i) {
            ++shown;
            for (qint64 t : targets)
                if (qAbs(rec.positions[i] - t) <= qMax<qint64>(3 * frameMs, 3000)) { ++tracked; break; }
        }
        printf("[info] scrub-backward: %d frames, %d position signals during 30 steps, tracked %d/%d\n",
               framesDuring, posEmitted, tracked, shown);
        if (posEmitted > 200) {   // 自旋洪泛：bug 存在时此处为成千上万
            printf("[FAIL] scrub-backward: position signal storm (%d), cache spin suspected\n", posEmitted);
            failures++;
        }
        if (framesDuring < 10) {
            printf("[FAIL] scrub-backward: too few frames (%d)\n", framesDuring);
            failures++;
        }
        if (shown > 0 && tracked * 10 < shown * 9) {
            printf("[FAIL] scrub-backward: %d/%d frames NOT near targets\n", shown - tracked, shown);
            failures++;
        }
        qint64 finalTarget = targets.last();
        int before = rec.frameCount;
        engine.seek(finalTarget);
        QElapsedTimer guard; guard.start();
        while (rec.frameCount == before && guard.elapsed() < 3000)
            pumpFor(20);
        if (qAbs(rec.lastPos - finalTarget) > frameMs) {
            printf("[FAIL] scrub-backward: final landed %lld target %lld\n", rec.lastPos, finalTarget);
            failures++;
        } else if (failures == 0) {
            printf("[ OK ] scrub-backward: %d frames, signals bounded, final err %lldms\n",
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
