/**
 * @file ffmpeg_video_engine.cpp
 * @brief FFmpeg 自研播放引擎实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-07-25
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "ffmpeg_video_engine.h"
#include <QDebug>
#include <QElapsedTimer>
#include <QAudioSink>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QMediaDevices>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

FfmpegVideoEngine::FfmpegVideoEngine(QObject *parent)
    : IVideoEngine(parent)
{
}

FfmpegVideoEngine::~FfmpegVideoEngine()
{
    m_quit = true;
    postCommand(Command::Stop);
    if (m_thread) {
        m_thread->quit();
        m_thread->wait(5000);
    }
}

// ---------------------------------------------------------------------------
// IVideoEngine 接口（UI 线程）
// ---------------------------------------------------------------------------

bool FfmpegVideoEngine::load(const QString &filePath)
{
    if (filePath.isEmpty())
        return false;

    // 停掉旧线程（旧文件资源在工作线程内关闭）
    if (m_thread) {
        m_quit = true;
        postCommand(Command::Stop);
        m_thread->quit();
        m_thread->wait(5000);
        m_thread = nullptr;
        m_quit = false;
    }

    m_pendingPath = filePath;
    m_positionMs = 0;
    m_durationMs = 0;
    m_state = static_cast<int>(PlaybackState::Loading);

    m_thread = QThread::create([this]() { workerMain(); });
    m_thread->start();
    return true;
}

void FfmpegVideoEngine::play()  { postCommand(Command::Play); }
void FfmpegVideoEngine::pause() { postCommand(Command::Pause); }
void FfmpegVideoEngine::stop()  { postCommand(Command::Stop); }
void FfmpegVideoEngine::seek(qint64 timeMs) { postCommand(Command::Seek, timeMs); }

qint64 FfmpegVideoEngine::position() const { return m_positionMs.load(); }
qint64 FfmpegVideoEngine::duration() const { return m_durationMs.load(); }
PlaybackState FfmpegVideoEngine::state() const
{
    return static_cast<PlaybackState>(m_state.load());
}
int FfmpegVideoEngine::videoWidth() const { return m_width.load(); }
int FfmpegVideoEngine::videoHeight() const { return m_height.load(); }
float FfmpegVideoEngine::fps() const { return m_fps > 0.0f ? m_fps.load() : 30.0f; }
int FfmpegVideoEngine::volume() const { return m_volume.load(); }
void FfmpegVideoEngine::setVolume(int vol)
{
    m_volume = qBound(0, vol, 100);   // 原子量，播放面随音频包应用
}

// ---------------------------------------------------------------------------
// 音频输出（仅工作线程调用）
// ---------------------------------------------------------------------------

bool FfmpegVideoEngine::ensureAudioOutput()
{
    if (m_sink)
        return true;
    if (!m_adec)
        return false;

    QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (device.isNull())
        return false;

    m_outSampleRate = m_adec->sample_rate > 0 ? m_adec->sample_rate : 44100;
    m_outChannels = qBound(1, m_adec->ch_layout.nb_channels, 2);

    QAudioFormat fmt;
    fmt.setSampleRate(m_outSampleRate);
    fmt.setChannelCount(m_outChannels);
    fmt.setSampleFormat(QAudioFormat::Int16);
    if (!device.isFormatSupported(fmt)) {
        fmt = device.preferredFormat();
        m_outSampleRate = fmt.sampleRate();
        m_outChannels = fmt.channelCount();
    }

    // 重采样器：解码帧格式 → 输出格式
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, m_outChannels);
    if (swr_alloc_set_opts2(&m_swr, &outLayout, AV_SAMPLE_FMT_S16, m_outSampleRate,
                            &m_adec->ch_layout, m_adec->sample_fmt,
                            m_adec->sample_rate, 0, nullptr) < 0 || !m_swr)
        return false;
    av_channel_layout_uninit(&outLayout);
    if (swr_init(m_swr) < 0) {
        swr_free(&m_swr);
        return false;
    }

    int bytesPerSec = m_outSampleRate * m_outChannels * 2;
    m_sink = new QAudioSink(device, fmt);
    m_sink->setBufferSize(bytesPerSec);              // 1s 设备缓冲
    m_sink->setVolume(m_volume.load() / 100.0f);
    // 推模式：工作线程无 Qt 事件循环，拉模式的设备回调不会被驱动
    m_sinkIo = m_sink->start();
    m_audioSinkOk = (m_sinkIo != nullptr);
    // 音频帧时长估算：AAC 固定 1024 样本/帧（8kHz=128ms，16kHz=64ms，44.1k≈23ms）
    m_audioFrameMs = qMax<qint64>(1, 1024 * 1000 / m_adec->sample_rate);
    return m_audioSinkOk.load();
}

void FfmpegVideoEngine::suspendAudio()
{
    if (m_sink)
        m_sink->suspend();
}

void FfmpegVideoEngine::resumeAudio()
{
    if (m_sink)
        m_sink->resume();
}

qint64 FfmpegVideoEngine::audioClockMs() const
{
    if (!m_sink || m_audioBaseRelMs < 0)
        return 0;
    // 推模式下"写入量-缓冲量"会随 demux 速度漂移（runaway 反馈），
    // 唯一可信的已播放度量是设备自身时钟 elapsedUSecs()。
    return m_audioBaseRelMs + m_sink->elapsedUSecs() / 1000;
}

void FfmpegVideoEngine::processAudioPacket(AVPacket *pkt)
{
    if (!m_adec)
        return;

    // rate != 1.0：一期音频静音，解码直接丢弃
    if (qAbs(m_rate.load() - 1.0f) > 0.01f)
        return;

    if (!ensureAudioOutput())
        return;

    m_sink->setVolume(m_volume.load() / 100.0f);  // 音量原子量随包应用

    if (avcodec_send_packet(m_adec, pkt) < 0)
        return;

    const int inRate = m_adec->sample_rate > 0 ? m_adec->sample_rate : 44100;
    AVFrame *frame = av_frame_alloc();
    while (avcodec_receive_frame(m_adec, frame) >= 0) {
        // --- seek 对齐：丢弃/裁剪早于目标的音频帧（F-A） ---
        qint64 relMs = ptsToRelMsA(frame->best_effort_timestamp);
        qint64 frameDurMs = frame->nb_samples * 1000 / inRate;
        double skipFrac = 0.0;
        if (relMs >= 0 && m_audioDiscardBeforeRelMs >= 0) {
            if (relMs + frameDurMs <= m_audioDiscardBeforeRelMs) {
                av_frame_unref(frame);
                continue;                       // 整帧早于目标：丢弃
            }
            if (relMs < m_audioDiscardBeforeRelMs) {
                skipFrac = static_cast<double>(m_audioDiscardBeforeRelMs - relMs)
                           / frameDurMs;        // 部分重叠：按比例裁剪起始部分
            }
        }

        int outSamples = swr_get_out_samples(m_swr, frame->nb_samples);
        if (outSamples > 0) {
            QByteArray out(outSamples * m_outChannels * 2, Qt::Uninitialized);
            uint8_t *outBuf[1] = { reinterpret_cast<uint8_t *>(out.data()) };
            int converted = swr_convert(m_swr, outBuf, outSamples,
                                        const_cast<const uint8_t **>(frame->extended_data),
                                        frame->nb_samples);
            if (converted > 0) {
                qint64 bytes = static_cast<qint64>(converted) * m_outChannels * 2;
                qint64 offset = 0;
                if (skipFrac > 0.0) {
                    qint64 align = static_cast<qint64>(m_outChannels) * 2;
                    offset = (static_cast<qint64>(bytes * skipFrac) / align) * align;
                    offset = qBound<qint64>(0, offset, bytes - align);
                }

                // 首个写入样本锚定音频时钟基点（F-A：声音与画面同一起点）
                if (m_audioBaseRelMs < 0) {
                    if (relMs >= 0)
                        m_audioBaseRelMs = qMax<qint64>(relMs, m_audioDiscardBeforeRelMs);
                    else
                        m_audioBaseRelMs = qMax<qint64>(0, m_audioDiscardBeforeRelMs);
                }

                qint64 payload = bytes - offset;
                // 背压：设备缓冲满则短暂等待（播放面自然节流）
                int guard = 0;
                while (m_sink->bytesFree() < payload && guard++ < 100
                       && !m_quit.load()
                       && m_state.load() == static_cast<int>(PlaybackState::Playing)
                       && !hasPendingCommand()) {
                    QThread::msleep(2);
                }
                if (m_sinkIo)
                    m_sinkIo->write(out.constData() + offset, payload);
                m_audioBytesWritten += payload;
            }
        }
        av_frame_unref(frame);
    }
    av_frame_free(&frame);
}

float FfmpegVideoEngine::rate() const { return m_rate.load(); }

void FfmpegVideoEngine::setRate(float rate)
{
    m_rate = rate > 0.0f ? rate : 1.0f;
    m_clockValid = false; // 速率变化后重建时钟基准
}

// ---------------------------------------------------------------------------
// 命令下发
// ---------------------------------------------------------------------------

void FfmpegVideoEngine::postCommand(Command cmd, qint64 arg)
{
    QMutexLocker lock(&m_cmdMutex);
    m_pendingCmd = cmd;
    m_cmdArg = arg;
    m_cmdCond.wakeAll();
}

bool FfmpegVideoEngine::hasPendingCommand()
{
    QMutexLocker lock(&m_cmdMutex);
    return m_pendingCmd != Command::None;
}

// ---------------------------------------------------------------------------
// 工作线程
// ---------------------------------------------------------------------------

void FfmpegVideoEngine::workerMain()
{
    if (!openFile(m_pendingPath)) {
        m_state = static_cast<int>(PlaybackState::Idle);
        emit stateChanged(PlaybackState::Idle);
        closeFile();
        return;
    }

    m_state = static_cast<int>(PlaybackState::Stopped);
    emit stateChanged(PlaybackState::Stopped);
    emit durationChanged(m_durationMs.load());
    emit videoSizeChanged(m_width.load(), m_height.load());

    // 显示首帧（与 VLC 引擎加载后出图行为一致）
    m_stepOnce = true;

    AVPacket *pkt = av_packet_alloc();
    int consecutiveErrors = 0;

    while (!m_quit.load()) {
        // --- 处理命令 ---
        {
            QMutexLocker lock(&m_cmdMutex);
            if (m_pendingCmd == Command::None &&
                m_state.load() != static_cast<int>(PlaybackState::Playing) &&
                !m_stepOnce) {
                m_cmdCond.wait(&m_cmdMutex, 50);
            }
            Command cmd = m_pendingCmd;
            qint64 arg = m_cmdArg;
            m_pendingCmd = Command::None;
            lock.unlock();

            switch (cmd) {
            case Command::Play:
                if (m_eof)
                    handleSeek(0);   // EOF 后重播：从头开始
                m_clockValid = false;
                m_state = static_cast<int>(PlaybackState::Playing);
                resumeAudio();
                emit stateChanged(PlaybackState::Playing);
                break;
            case Command::Pause:
                if (m_state.load() == static_cast<int>(PlaybackState::Playing)) {
                    m_state = static_cast<int>(PlaybackState::Paused);
                    suspendAudio();
                    emit stateChanged(PlaybackState::Paused);
                }
                break;
            case Command::Stop:
                handleSeek(0);
                m_state = static_cast<int>(PlaybackState::Stopped);
                suspendAudio();
                emit stateChanged(PlaybackState::Stopped);
                break;
            case Command::Seek:
                handleSeek(arg);
                break;
            case Command::None:
                break;
            }
        }

        if (m_quit.load())
            break;
        if (m_state.load() != static_cast<int>(PlaybackState::Playing) && !m_stepOnce)
            continue;

        // --- 解复用 ---
        int ret = av_read_frame(m_fmt, pkt);
        if (ret == AVERROR_EOF) {
            // 先冲空解码器：frame threading 会滞留最后 N 帧（含文件末尾 seek 的目标帧）
            if (!m_drainedAtEof) {
                avcodec_send_packet(m_vdec, nullptr);
                drainDecoder();
                m_drainedAtEof = true;
                continue;
            }
            if (m_stepOnce) {
                m_stepOnce = false;      // seek 到末尾之外：无帧可显示
            } else {
                m_eof = true;
                m_state = static_cast<int>(PlaybackState::Ended);
                emit stateChanged(PlaybackState::Ended);
            }
            continue;
        }
        m_drainedAtEof = false;
        if (ret < 0) {
            if (m_quit.load())
                break;
            if (++consecutiveErrors > 100) {   // 连续读错误：判为结束
                m_eof = true;
                m_state = static_cast<int>(PlaybackState::Ended);
                emit stateChanged(PlaybackState::Ended);
            }
            av_packet_unref(pkt);
            continue;
        }
        consecutiveErrors = 0;

        if (pkt->stream_index == m_vstream)
            processVideoPacket(pkt);
        else if (pkt->stream_index == m_astream &&
                 m_state.load() == static_cast<int>(PlaybackState::Playing))
            processAudioPacket(pkt);
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    closeFile();
}

bool FfmpegVideoEngine::openFile(const QString &filePath)
{
    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "probesize", "50000000", 0);       // 50MB，利于无索引/伪扩展名文件
    av_dict_set(&opts, "analyzeduration", "10000000", 0); // 10s
    int ret = avformat_open_input(&m_fmt, filePath.toUtf8().constData(), nullptr, &opts);
    av_dict_free(&opts);
    if (ret < 0) {
        qWarning() << "FfmpegVideoEngine: cannot open" << filePath << "err" << ret;
        return false;
    }
    avformat_find_stream_info(m_fmt, nullptr);

    m_vstream = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_vstream < 0) {
        qWarning() << "FfmpegVideoEngine: no video stream";
        return false;
    }

    AVStream *vs = m_fmt->streams[m_vstream];
    const AVCodec *codec = avcodec_find_decoder(vs->codecpar->codec_id);
    if (!codec) {
        qWarning() << "FfmpegVideoEngine: no decoder for codec" << vs->codecpar->codec_id;
        return false;
    }
    m_vdec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_vdec, vs->codecpar);
    m_vdec->thread_count = 0; // 自动
    if (avcodec_open2(m_vdec, codec, nullptr) < 0) {
        qWarning() << "FfmpegVideoEngine: cannot open decoder";
        return false;
    }

    m_width = vs->codecpar->width;
    m_height = vs->codecpar->height;
    m_durationMs = m_fmt->duration > 0 ? m_fmt->duration / 1000 : 0;

    AVRational fr = vs->avg_frame_rate.num > 0 ? vs->avg_frame_rate : vs->r_frame_rate;
    if (fr.num > 0 && fr.den > 0)
        m_fps = static_cast<float>(av_q2d(fr));

    // 起始 PTS（绝对）：优先 stream start_time，其次 format start_time
    int64_t startPts = AV_NOPTS_VALUE;
    if (vs->start_time != AV_NOPTS_VALUE)
        startPts = vs->start_time;
    else if (m_fmt->start_time != AV_NOPTS_VALUE)
        startPts = av_rescale_q(m_fmt->start_time, AV_TIME_BASE_Q, vs->time_base);
    m_startPtsMs = (startPts != AV_NOPTS_VALUE)
            ? av_rescale_q(startPts, vs->time_base, AVRational{1, 1000})
            : 0;

    // --- 音频流（可选）：解码器 + 重采样器，输出端惰性创建 ---
    m_astream = av_find_best_stream(m_fmt, AVMEDIA_TYPE_AUDIO, -1, -1, nullptr, 0);
    if (m_astream >= 0) {
        AVStream *as = m_fmt->streams[m_astream];
        const AVCodec *acodec = avcodec_find_decoder(as->codecpar->codec_id);
        if (acodec) {
            m_adec = avcodec_alloc_context3(acodec);
            avcodec_parameters_to_context(m_adec, as->codecpar);
            if (avcodec_open2(m_adec, acodec, nullptr) < 0) {
                avcodec_free_context(&m_adec);
                m_adec = nullptr;
                m_astream = -1;
            }
        } else {
            m_astream = -1;
        }
    }
    m_audioMaster = (m_astream >= 0);

    m_discardBeforeRelMs = -1;
    m_audioDiscardBeforeRelMs = -1;
    m_audioBaseRelMs = -1;
    m_stepOnce = false;
    m_eof = false;
    m_clockValid = false;
    // PS/TS 等无索引容器 seek 只能按字节估算，落点有秒级误差
    // （FFmpeg 8 起 nb_index_entries 已私有化，改用 demuxer 名判定）
    m_indexed = (strncmp(m_fmt->iformat->name, "mpeg", 4) != 0);
    return true;
}

void FfmpegVideoEngine::closeFile()
{
    if (m_sink) { m_sink->stop(); delete m_sink; m_sink = nullptr; }
    m_sinkIo = nullptr;
    if (m_swr) { swr_free(&m_swr); m_swr = nullptr; }
    if (m_adec) { avcodec_free_context(&m_adec); m_adec = nullptr; }
    m_astream = -1;
    m_audioMaster = false;
    m_audioSinkOk = false;
    m_audioBytesWritten = 0;
    if (m_sws) { sws_freeContext(m_sws); m_sws = nullptr; }
    m_swsW = m_swsH = 0; m_swsFmt = -1;
    if (m_vdec) { avcodec_free_context(&m_vdec); m_vdec = nullptr; }
    if (m_fmt) { avformat_close_input(&m_fmt); m_fmt = nullptr; }
    m_vstream = -1;
}

qint64 FfmpegVideoEngine::ptsToRelMs(int64_t pts) const
{
    if (pts == AV_NOPTS_VALUE || !m_fmt || m_vstream < 0)
        return m_positionMs.load();
    qint64 absMs = av_rescale_q(pts, m_fmt->streams[m_vstream]->time_base,
                                AVRational{1, 1000});
    return absMs - m_startPtsMs;
}

qint64 FfmpegVideoEngine::ptsToRelMsA(int64_t pts) const
{
    if (pts == AV_NOPTS_VALUE || !m_fmt || m_astream < 0)
        return -1;
    qint64 absMs = av_rescale_q(pts, m_fmt->streams[m_astream]->time_base,
                                AVRational{1, 1000});
    return absMs - m_startPtsMs;
}

void FfmpegVideoEngine::handleSeek(qint64 timeMs)
{
    if (!m_fmt)
        return;

    timeMs = qBound<qint64>(0, timeMs, m_durationMs > 0 ? m_durationMs.load() : timeMs);

    // 无索引容器的字节估算 seek 可能过冲到目标之后，无法回退修正；
    // 故将 seek 目标前移一个 GOP 量级，随后解码丢弃到精确目标（见 drainDecoder）
    qint64 demuxTargetMs = timeMs;
    if (!m_indexed)
        demuxTargetMs = qMax<qint64>(0, timeMs - 2500);
    int64_t ts = static_cast<int64_t>(m_startPtsMs + demuxTargetMs) * 1000; // AV_TIME_BASE 微秒

    // 先按时间戳精确 seek（BACKWARD：落到目标之前的关键帧，再解码丢弃到目标）；
    // 失败则字节估算兜底（无索引容器）
    int ret = avformat_seek_file(m_fmt, -1, INT64_MIN, ts, ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
        ret = avformat_seek_file(m_fmt, -1, INT64_MIN, ts, INT64_MAX,
                                 AVSEEK_FLAG_BYTE | AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
        av_seek_frame(m_fmt, -1, ts, AVSEEK_FLAG_BACKWARD);

    avcodec_flush_buffers(m_vdec);
    if (m_adec)
        avcodec_flush_buffers(m_adec);
    if (m_sink) {
        m_sink->reset();               // 丢弃设备内残余音频
        m_sinkIo = m_sink->start();    // 重置后重新进入推模式
    }
    m_audioBytesWritten = 0;
    m_audioBaseRelMs = -1;   // 等待首个写入样本锚定
    m_smoothAudioClock = -1; // 重建平滑时钟
    m_lastRawAudioClock = -1;

    // seek 落点可能早于目标（关键帧），解码后丢弃早于目标的帧（容差半帧）
    qint64 halfFrame = m_fps > 0.0f ? static_cast<qint64>(500.0f / m_fps.load()) : 20;
    m_discardBeforeRelMs = qMax<qint64>(0, timeMs - halfFrame);
    m_audioDiscardBeforeRelMs = m_discardBeforeRelMs;
    m_eof = false;
    m_clockValid = false;
    m_positionMs = timeMs;
    emit positionChanged(timeMs);

    // 非播放态：解码显示目标帧后停留
    if (m_state.load() != static_cast<int>(PlaybackState::Playing))
        m_stepOnce = true;
}

void FfmpegVideoEngine::processVideoPacket(AVPacket *pkt)
{
    // 高倍速（>=4x）：只解码参考帧，降低解码压力（非参考帧丢弃不断链）
    m_vdec->skip_frame = (m_rate.load() >= 4.0f) ? AVDISCARD_NONREF : AVDISCARD_DEFAULT;

    // 送包；EAGAIN 说明解码器内部队列满，先排干再重试
    while (!m_quit.load()) {
        int ret = avcodec_send_packet(m_vdec, pkt);
        if (ret == AVERROR(EAGAIN)) {
            if (!drainDecoder())
                return;   // 排不出帧且需要中断（命令/退出），丢弃该包
            continue;
        }
        break;            // 成功或真实错误都结束送包
    }
    drainDecoder();
}

bool FfmpegVideoEngine::drainDecoder()
{
    AVFrame *frame = av_frame_alloc();
    bool gotFrame = false;

    while (!m_quit.load()) {
        int ret = avcodec_receive_frame(m_vdec, frame);
        if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF || ret < 0)
            break;

        gotFrame = true;
        qint64 relMs = ptsToRelMs(frame->best_effort_timestamp);
        bool discard = (m_discardBeforeRelMs >= 0 && relMs < m_discardBeforeRelMs);

        if (!discard) {
            m_discardBeforeRelMs = -1;
            bool playing = m_state.load() == static_cast<int>(PlaybackState::Playing);

            bool show = true;
            if (playing) {
                show = paceUntil(relMs);
                playing = m_state.load() == static_cast<int>(PlaybackState::Playing)
                          && !hasPendingCommand();
            }

            if (playing && !show) {
                av_frame_unref(frame);   // 倍速追帧：丢弃过晚的帧
                continue;
            }

            if (playing || m_stepOnce) {
                displayFrame(frame);
                m_stepOnce = false;
                av_frame_unref(frame);
                break;    // 暂停态只显示一帧；播放态显示后处理下一个包
            }
        }
        av_frame_unref(frame);

        // 非播放态且不需要单帧显示：继续排空，防止解码器堵塞
    }

    av_frame_free(&frame);
    return gotFrame;
}

bool FfmpegVideoEngine::paceUntil(qint64 ptsRelMs)
{
    if (!m_monotonic.isValid())
        m_monotonic.start();

    // 音频主时钟模式（rate==1.0 且有可用音轨）：视频帧等待音频时钟
    bool audioMaster = m_audioMaster && m_audioSinkOk.load()
                       && qAbs(m_rate.load() - 1.0f) < 0.01f;

    while (!m_quit.load()) {
        if (m_state.load() != static_cast<int>(PlaybackState::Playing))
            return true;
        if (hasPendingCommand())
            return true;  // 有命令（seek 等）待处理，立即返回主循环

        if (audioMaster) {
            // 新鲜度检查：音频时钟必须真的在前进（靠音频包持续喂入维持）。
            // 阈值随音频帧时长放宽（8kHz AAC 一帧 128ms，固定阈值会误判停滞）。
            qint64 rawAc = audioClockMs();
            qint64 now = m_monotonic.elapsed();
            if (rawAc != m_lastRawAudioClock) {
                m_lastRawAudioClock = rawAc;
                m_lastAudioProgressElapsed = now;
            }
            qint64 staleThreshold = qMax<qint64>(500, 4 * m_audioFrameMs);
            bool fresh = (now - m_lastAudioProgressElapsed) < staleThreshold;

            // 低通平滑：观测值阶梯前进（一个音频帧一步）， pacing 需要连续时钟。
            // 估计 = 平滑基准 + 墙钟增量；新观测按 1/4 增益渐进校正，大漂移直接重置。
            if (m_smoothAudioClock < 0) {
                m_smoothAudioClock = rawAc;
                m_smoothClockElapsed = now;
            } else {
                qint64 estimate = m_smoothAudioClock + (now - m_smoothClockElapsed);
                qint64 drift = rawAc - estimate;
                if (qAbs(drift) > 300) {
                    m_smoothAudioClock = rawAc;
                } else if (drift != 0) {
                    m_smoothAudioClock += drift / 4;
                }
                m_smoothClockElapsed = now;
            }
            qint64 ac = m_smoothAudioClock + (now - m_smoothClockElapsed);

            if (fresh) {
                qint64 delay = ptsRelMs - ac;
                if (delay <= 10)
                    return true;  // 到点或落后：显示（丢帧追齐由系统时钟路径处理）
                if (delay <= 250) {
                    QThread::msleep(static_cast<unsigned long>(qMin<qint64>(delay, 10)));
                    continue;
                }
            }
            audioMaster = false;  // 回退系统时钟
        }

        if (!m_clockValid) {
            m_clockBasePtsMs = ptsRelMs;
            m_clockBaseElapsed = m_monotonic.elapsed();
            m_clockValid = true;
            return true;
        }

        float r = m_rate.load();
        qint64 dueElapsed = static_cast<qint64>((ptsRelMs - m_clockBasePtsMs) / r);
        qint64 delay = dueElapsed - (m_monotonic.elapsed() - m_clockBaseElapsed);
        if (delay <= 0) {
            // 倍速播放解码跟不上：丢弃落后超过一帧间隔的帧以追赶时钟
            if (r > 1.0f && delay < -40.0 / r)
                return false;
            return true;
        }
        if (delay > 5000)
            delay = 5000; // PTS 跳变保护

        QThread::msleep(static_cast<unsigned long>(qMin<qint64>(delay, 10)));
    }
    return true;
}

void FfmpegVideoEngine::displayFrame(AVFrame *frame)
{
    // 帧格式/尺寸变化时重建 swscale 上下文
    if (!m_sws || m_swsW != frame->width || m_swsH != frame->height
        || m_swsFmt != frame->format) {
        if (m_sws)
            sws_freeContext(m_sws);
        m_sws = sws_getContext(frame->width, frame->height,
                               static_cast<AVPixelFormat>(frame->format),
                               frame->width, frame->height, AV_PIX_FMT_RGB24,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_sws)
            return;
        m_swsW = frame->width;
        m_swsH = frame->height;
        m_swsFmt = frame->format;
    }

    QImage img(frame->width, frame->height, QImage::Format_RGB888);
    uint8_t *dst[4] = { img.bits(), nullptr, nullptr, nullptr };
    int dstLinesize[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };
    sws_scale(m_sws, frame->data, frame->linesize, 0, frame->height,
              dst, dstLinesize);

    qint64 relMs = ptsToRelMs(frame->best_effort_timestamp);
    m_positionMs = relMs;
    emit positionChanged(relMs);
    emit frameReady(img);
}
