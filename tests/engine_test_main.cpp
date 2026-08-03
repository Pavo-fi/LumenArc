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
#include <QMediaDevices>
#include <QAudioDevice>
#include <QAudioFormat>
extern "C" {
#include <libswresample/swresample.h>
}
#include <cstdio>
#include <cmath>
#include <numeric>
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
    int lastImgW = 0;
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
    if (args.contains(QStringLiteral("--sw")))
        engine.setHardwareDecode(false);   // 软解对比（诊断 scrub 回传瓶颈）
    Recorder rec;
    QObject::connect(&engine, &IVideoEngine::frameReady,
                     &app, [&](const QImage &img) { rec.frameCount++; rec.lastImgW = img.width(); engine.ackFrame(); });
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

    if (scenario == "audio-peak") {
        // 定位无声环节：复刻引擎音频通路（AAC decode → swr → s16），
        // 分段测量「解码原始电平」与「swr 输出电平」，并打印 swr 初始化结果
        AVFormatContext *fmt = nullptr;
        if (avformat_open_input(&fmt, file.toUtf8().constData(), nullptr, nullptr) < 0) {
            printf("[FAIL] cannot open\n");
            return 1;
        }
        avformat_find_stream_info(fmt, nullptr);
        int as = av_find_best_stream(fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
        if (as < 0) { printf("[ OK ] no audio stream\n"); return 0; }
        AVStream *st = fmt->streams[as];
        const AVCodec *ac = avcodec_find_decoder(st->codecpar->codec_id);
        AVCodecContext *dec = avcodec_alloc_context3(ac);
        avcodec_parameters_to_context(dec, st->codecpar);
        avcodec_open2(dec, ac, nullptr);
        printf("[info] codec=%s rate=%d ch_layout.order=%d nb_ch=%d sample_fmt=%d "
               "codecpar.ch=%d codecpar.layout.order=%d\n",
               ac->name, dec->sample_rate, dec->ch_layout.order,
               dec->ch_layout.nb_channels, dec->sample_fmt,
               st->codecpar->ch_layout.nb_channels, st->codecpar->ch_layout.order);

        // 引擎同款输出格式回退逻辑（ensureAudioOutput）
        QAudioDevice device = QMediaDevices::defaultAudioOutput();
        int outRate = dec->sample_rate > 0 ? dec->sample_rate : 44100;
        int outCh = qBound(1, dec->ch_layout.nb_channels, 2);
        QAudioFormat qfmt;
        qfmt.setSampleRate(outRate);
        qfmt.setChannelCount(outCh);
        qfmt.setSampleFormat(QAudioFormat::Int16);
        if (!device.isNull() && !device.isFormatSupported(qfmt)) {
            qfmt = device.preferredFormat();
            outRate = qfmt.sampleRate();
            outCh = qfmt.channelCount();
            printf("[info] device fallback: %dHz %dch\n", outRate, outCh);
        }
        SwrContext *swr = nullptr;
        AVChannelLayout outLayout;
        av_channel_layout_default(&outLayout, outCh);
        int sret = swr_alloc_set_opts2(&swr, &outLayout, AV_SAMPLE_FMT_S16, outRate,
                                       &dec->ch_layout, dec->sample_fmt,
                                       dec->sample_rate, 0, nullptr);
        printf("[info] swr_alloc ret=%d swr=%p\n", sret, (void *)swr);
        if (swr && swr_init(swr) < 0) {
            printf("[FAIL] swr_init failed\n");
            failures++;
            swr_free(&swr);
        }

        // 跳到 300s（音量高的段落）解码 10s，分级测电平
        av_seek_frame(fmt, as, 300LL * st->time_base.den / st->time_base.num,
                      AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(dec);
        AVPacket *pkt = av_packet_alloc();
        AVFrame *fr = av_frame_alloc();
        float inPeak = 0.0f;
        int16_t outPeak = 0;
        qint64 decodedMs = 0;
        while (decodedMs < 10000 && av_read_frame(fmt, pkt) >= 0) {
            if (pkt->stream_index == as) {
                if (avcodec_send_packet(dec, pkt) == 0) {
                    while (avcodec_receive_frame(dec, fr) >= 0) {
                        decodedMs += fr->nb_samples * 1000 / dec->sample_rate;
                        // 输入电平（fltp：每声道一个平面）
                        if (fr->format == AV_SAMPLE_FMT_FLTP) {
                            for (int c = 0; c < fr->ch_layout.nb_channels; ++c) {
                                const float *d = (const float *)fr->extended_data[c];
                                for (int i = 0; i < fr->nb_samples; ++i)
                                    inPeak = qMax(inPeak, qAbs(d[i]));
                            }
                        }
                        if (swr) {
                            int outSamples = swr_get_out_samples(swr, fr->nb_samples);
                            if (outSamples > 0) {
                                QByteArray out(outSamples * outCh * 2, Qt::Uninitialized);
                                uint8_t *ob[1] = { (uint8_t *)out.data() };
                                int conv = swr_convert(swr, ob, outSamples,
                                    (const uint8_t **)fr->extended_data, fr->nb_samples);
                                const int16_t *s = (const int16_t *)out.constData();
                                for (int i = 0; i < conv * outCh; ++i)
                                    outPeak = qMax<int16_t>(outPeak, qAbs(s[i]));
                            }
                        }
                        av_frame_unref(fr);
                    }
                }
            }
            av_packet_unref(pkt);
        }
        printf("[info] decode peak=%.4f (%.1f dB)  swr out peak=%d (%.1f dB)\n",
               inPeak, inPeak > 0 ? 20 * log10(inPeak) : -999.0,
               outPeak, outPeak > 0 ? 20 * log10(outPeak / 32768.0) : -999.0);
        if (inPeak > 0.001f && outPeak == 0) {
            printf("[FAIL] swr produced SILENCE from non-silent input → 无声根因\n");
            failures++;
        } else if (inPeak <= 0.001f) {
            printf("[FAIL] decoder itself produced silence → 解码环节根因\n");
            failures++;
        } else {
            printf("[ OK ] audio pipeline levels healthy\n");
        }
        av_frame_free(&fr);
        av_packet_free(&pkt);
        if (swr) swr_free(&swr);
        avcodec_free_context(&dec);
        avformat_close_input(&fmt);
        return failures ? 1 : 0;
    }

    if (scenario == "scrub-sweep") {
        // 长距离连续拖拽仿真：默认 300 步 ×40ms = 12s 从 5% 扫到 95%。
        // 统计显示帧数（节拍是否跟上）、相邻显示位置最大跳跃（观感跳跃）、
        // 引擎侧 diag 日志（%TEMP%/lumenarc_audio.log）有 reseek/uiDrop/maxGap
        engine.seek(0);
        pumpFor(500);
        engine.setScrubMode(true);
        const int steps = args.size() > 3 ? args[3].toInt() : 300;
        const int pumpMs = args.size() > 4 ? args[4].toInt() : 40;
        const int startPct = args.size() > 5 ? args[5].toInt() : 5;
        const int endPct = args.size() > 6 ? args[6].toInt() : 95;
        const qint64 t0 = rec.duration * startPct / 100;
        const qint64 t1 = rec.duration * endPct / 100;
        const int framesBefore = rec.frameCount;
        const int posBase = rec.positions.size();
        for (int i = 1; i <= steps; ++i) {
            engine.seek(t0 + (t1 - t0) * i / steps);
            pumpFor(pumpMs);
        }
        engine.setScrubMode(false);
        pumpFor(200);
        const int frames = rec.frameCount - framesBefore;
        qint64 maxPosJump = 0, sum = 0;
        for (int i = posBase + 1; i < rec.positions.size(); ++i) {
            const qint64 d = rec.positions[i] - rec.positions[i - 1];
            if (d > 0) { maxPosJump = qMax(maxPosJump, d); sum += d; }
        }
        printf("[info] sweep %d steps over %llds timeline: %d frames, "
               "avg pos step %lldms, max pos jump %lldms\n",
               steps, (t1 - t0) / 1000, frames,
               frames > 1 ? sum / (frames - 1) : 0LL, maxPosJump);
        if (frames < steps / 4) {   // 远低于 25ms 节拍 = 卡顿
            printf("[FAIL] scrub-sweep: too few frames (%d for %d steps)\n", frames, steps);
            failures++;
        } else {
            printf("[ OK ] sweep produced %d frames\n", frames);
        }
    } else if (scenario == "seek-matrix") {
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
    } else if (scenario == "scrub-then-play") {
        // 复现“拖拽后播放无声”：模拟一段连续 scrub，松手后从头播放，
        // 用设备自身时钟（audioClockMs）验证 sink 是否被 scrub 片段逻辑搞死
        engine.seek(0);
        pumpFor(500);
        engine.setScrubMode(true);
        for (int i = 1; i <= 30; ++i) {
            engine.seek(rec.duration * i / 31);
            pumpFor(40);
        }
        engine.setScrubMode(false);
        pumpFor(300);
        engine.seek(0);
        pumpFor(500);
        engine.play();
        pumpFor(3000);
        qint64 clk = engine.audioClockMs();
        printf("[info] scrub-then-play: audioClock=%lldms bytes=%lld hasAudio=%d\n",
               clk, engine.audioBytesWritten(), engine.hasAudio() ? 1 : 0);
        if (engine.hasAudio() && clk <= 0) {
            printf("[FAIL] audio clock dead after scrub (sink broken by scrub snippets)\n");
            failures++;
        } else {
            printf("[ OK ] audio alive after scrub (clock %lldms)\n", clk);
        }
        engine.pause();
    } else if (scenario == "scrub-sim") {
        // 模拟拖拽：匀速从 20%→80% 拖 dragMs，统计显示帧数/帧间隔。
        // 现场反馈：拼接产物“完全没有拖拽的连续帧”（seek 超前回归已修）
        int dragMs = args.size() > 3 ? args[3].toInt() : 2500;
        const qint64 start = rec.duration / 5;
        const qint64 span = rec.duration * 3 / 5;
        engine.seek(start);
        pumpFor(400);
        engine.setScrubMode(true);
        QElapsedTimer wall; wall.start();
        QVector<int> gapMs;
        int shown = 0, last = rec.frameCount;
        qint64 lastShowElapsed = 0;
        while (wall.elapsed() < dragMs) {
            const qreal f = qMin(1.0, wall.elapsed() / qreal(dragMs));
            engine.setScrubTarget(start + qint64(span * f));
            pumpFor(8);
            if (rec.frameCount != last) {
                const qint64 now = wall.elapsed();
                if (shown > 0)
                    gapMs.append(int(now - lastShowElapsed));
                lastShowElapsed = now;
                last = rec.frameCount;
                ++shown;
            }
        }
        engine.setScrubMode(false);
        const int expectMin = dragMs / 80;   // ≥12.5fps 显示
        printf("[info] scrub %dms: shown=%d frames (expect>=%d) maxGap=%dms avgGap=%dms "
               "imgW=%d\n",
               dragMs, shown, expectMin,
               gapMs.isEmpty() ? 0 : *std::max_element(gapMs.begin(), gapMs.end()),
               gapMs.isEmpty() ? 0
                   : std::accumulate(gapMs.begin(), gapMs.end(), 0) / gapMs.size(),
               rec.lastImgW);
        if (shown < expectMin) {
            printf("[FAIL] scrub shows too few frames (seek regression?)\n");
            failures++;
        } else {
            printf("[ OK ] scrub shows continuous frames\n");
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
        // 追踪精度：每个显示帧须落在拖拽目标附近。误差容忍与引擎同规则：
        // 精确帧 ±max(3帧, 3s)；快拖跳显帧允许落后（不超前）目标
        // ≤ 光标 100ms 墙钟走过的距离（v×100ms）——>30× 下跳显是预期行为
        qint64 frameMs = static_cast<qint64>(1000.0f / engine.fps());
        qint64 tol = qMax<qint64>(3 * frameMs, 3000);
        const qint64 stepMs = targets.size() > 1 ? targets[1] - targets[0] : 0;
        const qint64 velCap = stepMs * 100 / qMax(1, pumpMs);   // v×100ms
        int tracked = 0, shown = 0;
        for (int i = posBase; i < rec.positions.size(); ++i) {
            ++shown;
            bool hit = false;
            for (qint64 t : targets) {
                const qint64 d = t - rec.positions[i];
                if (qAbs(d) <= tol || (d > 0 && d <= velCap)) { hit = true; break; }
            }
            if (hit) ++tracked;
            else printf("  [dbg] off-target frame at %lldms (nearest target gap %lldms)\n",
                        rec.positions[i],
                        [&]{ qint64 m = INT64_MAX; for (qint64 t : targets)
                             m = qMin(m, qAbs(rec.positions[i] - t)); return m; }());
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
            for (qint64 t : targets) {
                // 跟踪判定：窗口帧 ±3s；超长 GOP 文件允许关键帧跳显帧
                // （最多落后目标一个 GOP ≈16s——Premiere 式预览语义，
                // 超前目标 >3s 的帧仍判失败：快进感不允许）
                const qint64 d = t - rec.positions[i];
                if (qAbs(d) <= qMax<qint64>(3 * frameMs, 3000)
                    || (d > 0 && d <= 16000)) { ++tracked; break; }
            }
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
