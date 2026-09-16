/**
 * @file live_stream_engine.cpp
 * @brief 网络直播引擎实现（RTSP 拉流 → 解码 → 送帧）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-11
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "infrastructure/live/live_stream_engine.h"

#include <QDebug>

#include <mutex>

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libavutil/avutil.h>
#include <libavutil/error.h>
#include <libavutil/imgutils.h>
#include <libswscale/swscale.h>
}

namespace {

/// 全局网络子系统初始化（进程内一次；FFmpeg 文档允许重复调用，但没必要）
void ensureNetworkInit()
{
    static std::once_flag once;
    std::call_once(once, []() { avformat_network_init(); });
}

QString avErrText(int err)
{
    char buf[AV_ERROR_MAX_STRING_SIZE] = {0};
    av_strerror(err, buf, sizeof(buf));
    return QString::fromUtf8(buf);
}

} // namespace

LiveStreamEngine::LiveStreamEngine(QObject *parent)
    : IVideoEngine(parent)
{
}

LiveStreamEngine::~LiveStreamEngine()
{
    unload();
}

// ---------------------------------------------------------------------------
// IVideoEngine 接口（UI 线程）
// ---------------------------------------------------------------------------

bool LiveStreamEngine::load(const QString &url)
{
    const QString u = url.trimmed();
    if (u.isEmpty())
        return false;

    // 结束上一次会话（保留线程，便于快速切换/重连）
    m_started.store(false);
    m_abort.store(true);
    wakeAll();
    {
        QMutexLocker lk(&m_mutex);
        m_url = u;
        m_generation.fetch_add(1);   // 评审 N4：换代，使旧代际的 worker 交回外层重读
    }
    m_width.store(0);
    m_height.store(0);
    m_fps.store(0.0f);
    m_positionMs.store(0);
    m_framesInFlight.store(0);
    setState(PlaybackState::Loading);
    ensureThread();
    return true;
}

void LiveStreamEngine::play()
{
    m_paused.store(false);
    m_abort.store(false);
    m_started.store(true);
    ensureThread();
    wakeAll();
    if (m_state.load() == static_cast<int>(PlaybackState::Paused))
        setState(PlaybackState::Playing);
}

void LiveStreamEngine::pause()
{
    m_paused.store(true);
    wakeAll();
    // 评审 N6：空闲/加载中调 pause 不得把状态机推到 Paused——只置暂停标志，
    // 真正出图后由 readLoop 的暂停门控生效。
    if (m_state.load() == static_cast<int>(PlaybackState::Playing))
        setState(PlaybackState::Paused);
}

void LiveStreamEngine::stop()
{
    m_started.store(false);
    m_abort.store(true);
    wakeAll();
    setState(PlaybackState::Stopped);
}

void LiveStreamEngine::unload()
{
    m_started.store(false);
    m_paused.store(false);
    m_abort.store(true);
    m_quit.store(true);
    wakeAll();
    if (m_thread) {
        if (!m_thread->wait(5000)) {
            qWarning() << "LiveStreamEngine: worker did not stop in time, terminating";
            m_thread->terminate();
            m_thread->wait(1000);
        }
        delete m_thread;
        m_thread = nullptr;
    }
    {
        QMutexLocker lk(&m_mutex);
        m_url.clear();
    }
    m_width.store(0);
    m_height.store(0);
    m_fps.store(0.0f);
    m_framesInFlight.store(0);
    setState(PlaybackState::Idle);
}

void LiveStreamEngine::seek(qint64 timeMs)
{
    Q_UNUSED(timeMs);   // 直播没有可寻址时间轴
}

qint64 LiveStreamEngine::position() const
{
    return m_positionMs.load();
}

PlaybackState LiveStreamEngine::state() const
{
    return static_cast<PlaybackState>(m_state.load());
}

int LiveStreamEngine::videoWidth() const
{
    return m_width.load();
}

int LiveStreamEngine::videoHeight() const
{
    return m_height.load();
}

float LiveStreamEngine::fps() const
{
    return m_fps.load();
}

int LiveStreamEngine::volume() const
{
    return m_volume.load();
}

void LiveStreamEngine::setVolume(int vol)
{
    m_volume.store(qBound(0, vol, 100));   // 直播无音频输出，仅保存以便接口一致
}

void LiveStreamEngine::setRate(float rate)
{
    Q_UNUSED(rate);   // 直播不支持变速
}

void LiveStreamEngine::ackFrame()
{
    // 越界安全递减：closeStream/重连会把计数清零，在飞帧随后到达时不得减成负数
    int v = m_framesInFlight.load(std::memory_order_relaxed);
    while (v > 0) {
        if (m_framesInFlight.compare_exchange_weak(
                v, v - 1, std::memory_order_relaxed, std::memory_order_relaxed))
            return;
    }
}

// ---------------------------------------------------------------------------
// 线程与阻塞 IO
// ---------------------------------------------------------------------------

int LiveStreamEngine::interruptCb(void *opaque)
{
    auto *self = static_cast<LiveStreamEngine *>(opaque);
    return self && self->isAbort() ? 1 : 0;
}

void LiveStreamEngine::wakeAll()
{
    QMutexLocker lk(&m_mutex);
    m_cond.wakeAll();
}

void LiveStreamEngine::sleepInterruptible(int ms)
{
    QMutexLocker lk(&m_mutex);
    if (!m_quit.load() && m_started.load() && !m_abort.load())
        m_cond.wait(&m_mutex, static_cast<unsigned long>(qMax(0, ms)));
}

void LiveStreamEngine::ensureThread()
{
    if (m_thread)
        return;
    m_quit.store(false);
    m_thread = QThread::create([this]() { workerMain(); });
    m_thread->start();
}

// ---------------------------------------------------------------------------
// 工作线程
// ---------------------------------------------------------------------------

void LiveStreamEngine::workerMain()
{
    while (!m_quit.load()) {
        QString url;
        quint64 generation = 0;
        {
            QMutexLocker lk(&m_mutex);
            while (!m_quit.load() && !m_started.load())
                m_cond.wait(&m_mutex);
            if (m_quit.load())
                break;
            url = m_url;
            generation = m_generation.load();   // 本代际快照（评审 N4）
        }
        if (url.isEmpty()) {
            setState(PlaybackState::Idle);
            continue;
        }

        int attempt = 0;
        while (m_started.load() && !m_quit.load()
               && m_generation.load() == generation) {
            if (!openStream(url)) {
                if (!m_started.load() || m_quit.load())
                    break;
                emit reconnecting(++attempt);
                sleepInterruptible(qMin(8000, 500 * (1 << qMin(attempt, 4))));
                continue;
            }
            attempt = 0;
            m_abort.store(false);
            m_waitKeyframe.store(false);
            m_sessionClock.restart();
            m_positionMs.store(0);
            setState(PlaybackState::Playing);
            emit streamStarted();

            readLoop();          // 内部处理暂停；返回即断线/停止/中止
            closeStream();

            // 代际变化＝地址已更新：交回外层重读 m_url，绝不用旧地址重连
            if (!m_started.load() || m_quit.load() || m_abort.load()
                || m_generation.load() != generation)
                break;
            emit reconnecting(++attempt);
            sleepInterruptible(qMin(8000, 500 * (1 << qMin(attempt, 4))));
        }
        if (!m_quit.load())
            setState(PlaybackState::Stopped);
    }
    closeStream();
    setState(PlaybackState::Idle);
}

bool LiveStreamEngine::openStream(const QString &url)
{
    ensureNetworkInit();

    AVFormatContext *fmt = avformat_alloc_context();
    if (!fmt) {
        emit streamError(QStringLiteral("内存不足：无法创建解复用上下文"));
        return false;
    }
    fmt->interrupt_callback.callback = &LiveStreamEngine::interruptCb;
    fmt->interrupt_callback.opaque = this;

    AVDictionary *opts = nullptr;
    av_dict_set(&opts, "rtsp_transport", m_transportTcp ? "tcp" : "udp", 0);
    // 连接/读超时（微秒）：FFmpeg 7 用 timeout，旧版用 stimeout——两个都设，未知键被忽略
    av_dict_set(&opts, "timeout", "5000000", 0);
    av_dict_set(&opts, "stimeout", "5000000", 0);
    // 实时追帧：不要缓冲、不要重排延迟
    av_dict_set(&opts, "fflags", "nobuffer", 0);
    av_dict_set(&opts, "flags", "low_delay", 0);
    av_dict_set(&opts, "max_delay", "500000", 0);
    av_dict_set(&opts, "reorder_queue_size", "0", 0);
    av_dict_set(&opts, "probesize", "1000000", 0);
    av_dict_set(&opts, "analyzeduration", "1000000", 0);

    const int openRet = avformat_open_input(&fmt, url.toUtf8().constData(),
                                            nullptr, &opts);
    av_dict_free(&opts);
    if (openRet < 0) {
        if (fmt)
            avformat_close_input(&fmt);
        emit streamError(QStringLiteral("无法连接监控流：%1").arg(avErrText(openRet)));
        return false;
    }
    m_fmt = fmt;

    const int infoRet = avformat_find_stream_info(m_fmt, nullptr);
    if (infoRet < 0) {
        const QString msg = avErrText(infoRet);
        closeStream();
        emit streamError(QStringLiteral("读取流信息失败：%1").arg(msg));
        return false;
    }

    const AVCodec *dec = nullptr;
    m_vstream = av_find_best_stream(m_fmt, AVMEDIA_TYPE_VIDEO, -1, -1, &dec, 0);
    if (m_vstream < 0 || !dec) {
        closeStream();
        emit streamError(QStringLiteral("该流不包含可解码的视频轨道"));
        return false;
    }

    m_vdec = avcodec_alloc_context3(dec);
    if (!m_vdec) {
        closeStream();
        emit streamError(QStringLiteral("内存不足：无法创建解码上下文"));
        return false;
    }
    if (avcodec_parameters_to_context(m_vdec, m_fmt->streams[m_vstream]->codecpar) < 0) {
        closeStream();
        emit streamError(QStringLiteral("视频参数解析失败"));
        return false;
    }
    m_vdec->flags |= AV_CODEC_FLAG_LOW_DELAY;
    m_vdec->thread_count = 0;   // 自动线程数

    const int codecRet = avcodec_open2(m_vdec, dec, nullptr);
    if (codecRet < 0) {
        const QString msg = avErrText(codecRet);
        closeStream();
        emit streamError(QStringLiteral("解码器打开失败：%1").arg(msg));
        return false;
    }

    const AVRational fr = m_fmt->streams[m_vstream]->avg_frame_rate;
    m_fps.store(fr.num > 0 && fr.den > 0 ? static_cast<float>(av_q2d(fr)) : 25.0f);
    m_width.store(m_vdec->width);
    m_height.store(m_vdec->height);
    m_sws = nullptr;
    return true;
}

void LiveStreamEngine::closeStream()
{
    if (m_vdec)
        avcodec_free_context(&m_vdec);
    if (m_fmt)
        avformat_close_input(&m_fmt);
    if (m_sws) {
        sws_freeContext(m_sws);
        m_sws = nullptr;
    }
    m_vstream = -1;
    m_waitKeyframe.store(false);
    m_framesInFlight.store(0);
}

void LiveStreamEngine::readLoop()
{
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    if (!pkt || !frame) {
        av_packet_free(&pkt);
        av_frame_free(&frame);
        return;
    }

    bool justResumed = false;
    while (m_started.load() && !m_quit.load() && !m_abort.load()) {
        if (m_paused.load()) {
            {
                QMutexLocker lk(&m_mutex);
                while (!m_quit.load() && m_started.load() && m_paused.load()
                       && !m_abort.load())
                    m_cond.wait(&m_mutex);
            }
            justResumed = true;
            continue;
        }
        if (justResumed) {
            // 恢复播放：清解码器残留 + 等下一个关键帧，避免陈旧参考帧花屏
            if (m_vdec)
                avcodec_flush_buffers(m_vdec);
            m_waitKeyframe.store(true);
            justResumed = false;
        }

        const int ret = av_read_frame(m_fmt, pkt);
        if (ret < 0)
            break;   // EOF / 超时 / 网络错误 / 中断 → 交给外层决定重连或结束

        if (pkt->stream_index == m_vstream) {
            const bool key = (pkt->flags & AV_PKT_FLAG_KEY) != 0;
            if (m_waitKeyframe.load()) {
                if (!key) {
                    av_packet_unref(pkt);
                    continue;
                }
                m_waitKeyframe.store(false);
            }
            if (avcodec_send_packet(m_vdec, pkt) >= 0) {
                while (avcodec_receive_frame(m_vdec, frame) >= 0)
                    displayFrame(frame);
            }
        }
        av_packet_unref(pkt);
    }

    av_packet_free(&pkt);
    av_frame_free(&frame);
}

bool LiveStreamEngine::displayFrame(AVFrame *frame)
{
    const int w = frame->width;
    const int h = frame->height;
    if (w <= 0 || h <= 0)
        return false;
    // 直播背压：在飞帧达上限即丢弃本帧（宁可掉帧，不可累积延时）
    if (m_framesInFlight.load(std::memory_order_relaxed) >= kMaxFramesInFlight)
        return false;

    m_sws = sws_getCachedContext(m_sws, w, h,
                                 static_cast<AVPixelFormat>(frame->format),
                                 w, h, AV_PIX_FMT_RGB24,
                                 SWS_BILINEAR, nullptr, nullptr, nullptr);
    if (!m_sws)
        return false;

    QImage img(w, h, QImage::Format_RGB888);
    uint8_t *dst[4] = {img.bits(), nullptr, nullptr, nullptr};
    int dstLinesize[4] = {static_cast<int>(img.bytesPerLine()), 0, 0, 0};
    sws_scale(m_sws, frame->data, frame->linesize, 0, h, dst, dstLinesize);

    if (m_width.load() != w || m_height.load() != h) {
        m_width.store(w);
        m_height.store(h);
        emit videoSizeChanged(w, h);
    }

    m_positionMs.store(m_sessionClock.isValid() ? m_sessionClock.elapsed() : 0);
    m_framesInFlight.fetch_add(1, std::memory_order_relaxed);
    emit frameReady(img);
    return true;
}
