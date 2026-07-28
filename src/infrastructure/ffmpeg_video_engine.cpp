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
#include <libavutil/pixdesc.h>
#include <libavutil/hwcontext.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
}

#ifdef Q_OS_WIN
#include <dxgi.h>
#endif

// D3D11VA get_format 回调：优先硬解像素格式，否则回退软解
static enum AVPixelFormat getHwFormatD3D11(AVCodecContext *, const enum AVPixelFormat *fmts)
{
    for (const enum AVPixelFormat *p = fmts; *p != AV_PIX_FMT_NONE; ++p) {
        if (*p == AV_PIX_FMT_D3D11)
            return AV_PIX_FMT_D3D11;
    }
    return fmts[0];
}

QVector<FfmpegVideoEngine::D3D11AdapterInfo> FfmpegVideoEngine::availableAdapters()
{
    QVector<D3D11AdapterInfo> out;
#ifdef Q_OS_WIN
    IDXGIFactory1 *factory = nullptr;
    if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory))) || !factory)
        return out;
    IDXGIAdapter1 *adapter = nullptr;
    for (UINT i = 0; factory->EnumAdapters1(i, &adapter) != DXGI_ERROR_NOT_FOUND; ++i) {
        if (!adapter)
            continue;
        DXGI_ADAPTER_DESC1 desc{};
        adapter->GetDesc1(&desc);
        adapter->Release();
        if (desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
            continue;   // 跳过 WARP 软适配器
        out.append({static_cast<int>(i),
                    QString::fromWCharArray(desc.Description),
                    static_cast<qint64>(desc.DedicatedVideoMemory / (1024 * 1024))});
    }
    factory->Release();
#endif
    return out;
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

    // scrub 拖拽期间静音（追逐循环自行丢音频包，此处防其他路径漏入）
    if (m_scrubMode.load())
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

void FfmpegVideoEngine::setProxySource(const QString &proxyPath)
{
    m_pxPathPending = proxyPath;
    m_pxReady = false;
    postCommand(Command::None);   // 唤醒工作线程空闲分支去 openProxy
}

// ---------------------------------------------------------------------------
// 拖拽预览代理（F-P2）：全 I 帧低分代理，暂停/拖拽 seek 秒出精确帧
// ---------------------------------------------------------------------------

void FfmpegVideoEngine::openProxy(const QString &path)
{
    closeProxy();
    if (avformat_open_input(&m_pxFmt, path.toUtf8().constData(), nullptr, nullptr) < 0) {
        m_pxFmt = nullptr;
        return;
    }
    m_pxVstream = av_find_best_stream(m_pxFmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_pxVstream < 0) {
        closeProxy();
        return;
    }
    AVStream *vs = m_pxFmt->streams[m_pxVstream];
    const AVCodec *codec = avcodec_find_decoder(vs->codecpar->codec_id);
    if (!codec) {
        closeProxy();
        return;
    }
    m_pxDec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_pxDec, vs->codecpar);
    if (avcodec_open2(m_pxDec, codec, nullptr) < 0) {
        closeProxy();
        return;
    }
    // 代理接管拖拽显示后，旧预读缓存失去意义
    prefetchAbort();
    m_pfPendingFromMs = -1;
    m_frameCache.clear();
    m_pxReady = true;
}

void FfmpegVideoEngine::closeProxy()
{
    if (m_pxSws) { sws_freeContext(m_pxSws); m_pxSws = nullptr; }
    if (m_pxDec) { avcodec_free_context(&m_pxDec); m_pxDec = nullptr; }
    if (m_pxFmt) { avformat_close_input(&m_pxFmt); m_pxFmt = nullptr; }
    m_pxVstream = -1;
    m_pxReady = false;
}

bool FfmpegVideoEngine::proxyDisplayFrame(qint64 timeMs)
{
    if (!m_pxReady || !m_pxFmt || !m_pxDec)
        return false;

    // 代理为全 I 帧 mp4（有索引，pts 从 0 起，与原片相对时间 1:1）
    int64_t ts = static_cast<int64_t>(timeMs) * 1000;
    if (avformat_seek_file(m_pxFmt, -1, INT64_MIN, ts, ts, AVSEEK_FLAG_BACKWARD) < 0)
        av_seek_frame(m_pxFmt, -1, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_pxDec);

    qint64 halfFrame = m_fps > 0.0f ? static_cast<qint64>(500.0f / m_fps.load()) : 20;
    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool shown = false;

    while (!shown && !m_quit.load() && !hasPendingCommand()) {
        if (av_read_frame(m_pxFmt, pkt) < 0)
            break;
        if (pkt->stream_index == m_pxVstream
            && avcodec_send_packet(m_pxDec, pkt) >= 0) {
            while (avcodec_receive_frame(m_pxDec, frame) >= 0) {
                qint64 relMs = av_rescale_q(frame->best_effort_timestamp,
                                            m_pxFmt->streams[m_pxVstream]->time_base,
                                            AVRational{1, 1000});
                if (relMs >= timeMs - halfFrame) {
                    // 与主管线相同的精确帧规则：首个 >= 目标-半帧 的帧
                    if (!m_pxSws) {
                        m_pxSws = sws_getContext(frame->width, frame->height,
                            static_cast<AVPixelFormat>(frame->format),
                            frame->width, frame->height, AV_PIX_FMT_RGB24,
                            SWS_BILINEAR, nullptr, nullptr, nullptr);
                    }
                    if (m_pxSws) {
                        QImage img(frame->width, frame->height, QImage::Format_RGB888);
                        uint8_t *dst[4] = { img.bits(), nullptr, nullptr, nullptr };
                        int ls[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };
                        sws_scale(m_pxSws, frame->data, frame->linesize, 0,
                                  frame->height, dst, ls);
                        m_positionMs = relMs;
                        emit positionChanged(relMs);
                        emit frameReady(img);
                        shown = true;
                    }
                    av_frame_unref(frame);
                    break;
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    return shown;
}

bool FfmpegVideoEngine::scrubChasePxFrame()
{
    if (!m_pxFmt || !m_pxDec)
        return false;

    const qint64 frameMs = m_fps > 0.0f ? static_cast<qint64>(1000.0f / m_fps.load()) : 40;
    const qint64 halfFrame = frameMs / 2;
    constexpr qint64 SEEK_THRESHOLD_MS = 500;  // 前进超阈值才重定位 demux（小步前进靠连续解码追上）
    constexpr int SKIP_FLUSH_PACKETS = 4;      // 连续 demux 丢包超此数，补解码前 flush 防旧帧滞留

    qint64 target = m_scrubTargetMs.load();
    if (target < 0) {
        QThread::msleep(10);     // 刚进入 scrub，尚无目标
        return false;
    }

    const qint64 delta = target - m_positionMs.load();
    if (delta > SEEK_THRESHOLD_MS || delta < -2 * frameMs) {
        // 大跳/后退：代理为全 I 帧有索引 mp4，seek+flush 代价 ~毫秒级
        int64_t ts = target * 1000;
        if (avformat_seek_file(m_pxFmt, -1, INT64_MIN, ts, ts, AVSEEK_FLAG_BACKWARD) < 0)
            av_seek_frame(m_pxFmt, -1, ts, AVSEEK_FLAG_BACKWARD);
        avcodec_flush_buffers(m_pxDec);
    } else if (qAbs(delta) <= halfFrame) {
        QThread::msleep(8);      // 已落位且目标未变：短睡避免空转
        return false;
    }
    // 其余：小幅前进 → 下方连续解码自然追上（不 seek、不 flush，管线不断流）

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool shown = false;
    bool retarget = false;      // 目标中途大幅后退，交外层重定位
    int skipped = 0;

    while (!shown && !retarget && !m_quit.load() && !hasPendingCommand()) {
        target = m_scrubTargetMs.load();      // 追逐中目标持续移动，每包刷新
        if (target < 0)
            break;
        if (av_read_frame(m_pxFmt, pkt) < 0) {
            QThread::msleep(10);              // 代理 EOF：等新目标
            break;
        }
        if (pkt->stream_index != m_pxVstream) {
            av_packet_unref(pkt);
            continue;
        }
        // demux 级追赶：远落后目标的包不解码直接丢（全 I 帧无帧间依赖）
        if (pkt->pts != AV_NOPTS_VALUE) {
            qint64 pktMs = av_rescale_q(pkt->pts,
                                        m_pxFmt->streams[m_pxVstream]->time_base,
                                        AVRational{1, 1000});
            if (pktMs < target - 2 * frameMs) {
                av_packet_unref(pkt);
                ++skipped;
                continue;
            }
        }
        if (skipped > SKIP_FLUSH_PACKETS)
            avcodec_flush_buffers(m_pxDec);   // 大段跳过后清掉解码器滞留的跳段前旧帧
        skipped = 0;
        if (avcodec_send_packet(m_pxDec, pkt) >= 0) {
            while (avcodec_receive_frame(m_pxDec, frame) >= 0) {
                qint64 relMs = av_rescale_q(frame->best_effort_timestamp,
                                            m_pxFmt->streams[m_pxVstream]->time_base,
                                            AVRational{1, 1000});
                if (relMs > target + SEEK_THRESHOLD_MS) {
                    retarget = true;          // 目标大幅后退，当前帧已无意义
                } else if (relMs >= target - halfFrame) {
                    // 与主管线相同的精确帧规则：首个 >= 目标-半帧 的帧
                    if (!m_pxSws) {
                        m_pxSws = sws_getContext(frame->width, frame->height,
                            static_cast<AVPixelFormat>(frame->format),
                            frame->width, frame->height, AV_PIX_FMT_RGB24,
                            SWS_BILINEAR, nullptr, nullptr, nullptr);
                    }
                    if (m_pxSws) {
                        QImage img(frame->width, frame->height, QImage::Format_RGB888);
                        uint8_t *dst[4] = { img.bits(), nullptr, nullptr, nullptr };
                        int ls[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };
                        sws_scale(m_pxSws, frame->data, frame->linesize, 0,
                                  frame->height, dst, ls);
                        m_positionMs = relMs;
                        emit positionChanged(relMs);
                        emit frameReady(img);
                        shown = true;
                    }
                }
                av_frame_unref(frame);
                if (shown || retarget)
                    break;
            }
        }
        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    return shown;
}

bool FfmpegVideoEngine::ensureScrubDecoder()
{
    if (m_scDec)
        return true;
    if (!m_fmt || m_vstream < 0)
        return false;
    AVStream *vs = m_fmt->streams[m_vstream];
    const AVCodec *codec = avcodec_find_decoder(vs->codecpar->codec_id);
    if (!codec)
        return false;
    m_scDec = avcodec_alloc_context3(codec);
    if (!m_scDec)
        return false;
    avcodec_parameters_to_context(m_scDec, vs->codecpar);
    // 多线程软解：长 GOP 追赶吞吐优先（实测 D17 1440p ~3140fps）。
    // 代价是管线填充延迟（~thread_count 帧 ≈100ms），短追赶反而不如硬解——
    // 因此仅在索引估算追赶长度 >4s 时启用（见 scrubChaseMainFrame）
    m_scDec->thread_count = 0;
    m_scDec->pkt_timebase = vs->time_base;
    if (avcodec_open2(m_scDec, codec, nullptr) < 0) {
        avcodec_free_context(&m_scDec);
        return false;
    }
    return true;
}

bool FfmpegVideoEngine::scrubChaseMainFrame()
{
    if (!m_fmt || !m_vdec)
        return false;

    const qint64 frameMs = m_fps > 0.0f ? static_cast<qint64>(1000.0f / m_fps.load()) : 40;
    const qint64 halfFrame = frameMs / 2;
    // 前进 decode-through 阈值：有索引文件 <~2 GOP 直追比 seek 重启动便宜；
    // 无索引文件（PS/TS）直追无界，而 margin seek 有界（m_seekMarginMs），阈值收紧
    const qint64 SEEK_THRESHOLD_MS = m_indexed ? 20000
                                               : qMax<qint64>(4000, m_seekMarginMs * 2);

    qint64 target = m_scrubTargetMs.load();
    if (target < 0) {
        QThread::msleep(10);     // 刚进入 scrub，尚无目标
        return false;
    }

    // 追逐基准：解码位置（含未显示帧）> 本次 seek 目标 > 显示位置。
    // decodePos 未知（软解管线填充期无输出）时用 seek 目标——光标移动 <20s 时
    // 继续当前追赶而非重 seek（防止填充期 decodePos 停滞导致的 reseek 风暴）
    const qint64 anchor = (m_chaseDecodePosMs >= 0) ? m_chaseDecodePosMs
                        : (m_chaseSeekTargetMs >= 0) ? m_chaseSeekTargetMs
                        : m_positionMs.load();
    const qint64 delta = target - anchor;
    if (delta > SEEK_THRESHOLD_MS || delta < -2 * frameMs) {
        // 大跳/后退：主管线 seek + flush（落在上一个关键帧，下方解码追赶到目标窗口）
        // 追赶过滤由下方目标窗口完成，不走 drainDecoder 的 discard 路径
        scrubRedirectDemuxer(target);   // 内含 m_vdec/m_scDec flush
        // 每次 seek 默认先上硬解（短追赶低延迟 1-2ms/帧）；若本文件已学习到过
        // 长 GOP（上次实测追赶 >4s），直接用多线程软解，省去硬解尝试的二次 seek。
        // 首个解码帧实测追赶长度，硬解且 >4s 时切软解重 seek（见下方解码循环）
        m_chaseDec = (m_lastCatchupMs > 4000 && ensureScrubDecoder()) ? m_scDec : m_vdec;
        m_chaseDecodePosMs = -1;
        m_chaseSeekTargetMs = target;
        m_needMarginMeasure = false;   // margin 测量在 drainDecoder，chase 不用，防陈旧测量
        m_eof = false;
        m_stepOnce = false;
        m_clockValid = false;
    } else if (qAbs(target - m_positionMs.load()) <= halfFrame) {
        QThread::msleep(8);      // 已落位且目标未变：短睡避免空转
        return false;
    }
    // 其余：小幅前进 → 下方连续解码自然追上（纯前进不 seek 不 flush，天然安全）

    // 解码器连续性：decode-through 必须沿用 seek 时选定的解码器（参考帧状态）
    AVCodecContext *dec = m_chaseDec ? m_chaseDec : m_vdec;
    bool catchupMeasured = false;   // 本调用内是否已实测过追赶长度

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    bool shown = false;
    bool reseek = false;      // 目标大幅移动（后退或飞奔），交外层重定位

    while (!shown && !reseek && !m_quit.load() && !hasPendingCommand()) {
        target = m_scrubTargetMs.load();      // 追逐中目标持续移动，每包刷新
        if (target < 0)
            break;
        int ret = av_read_frame(m_fmt, pkt);
        if (ret == AVERROR_EOF) {
            avcodec_send_packet(dec, nullptr);   // 冲空拿尾部帧
            while (avcodec_receive_frame(dec, frame) >= 0) {
                qint64 relMs = ptsToRelMs(frame->best_effort_timestamp);
                m_chaseDecodePosMs = relMs;
                if (relMs >= target - halfFrame) {
                    displayFrame(frame);
                    shown = true;
                }
                av_frame_unref(frame);
                if (shown)
                    break;
            }
            if (!shown)
                QThread::msleep(10);              // 真 EOF：等新目标
            break;
        }
        if (ret < 0) {
            av_packet_unref(pkt);
            QThread::msleep(5);                   // demux 错误：退避后交外层
            break;
        }
        if (pkt->stream_index != m_vstream) {
            av_packet_unref(pkt);                 // 音频/其他流：scrub 期间丢弃（静音拖拽）
            continue;
        }
        if (avcodec_send_packet(dec, pkt) >= 0) {
            while (avcodec_receive_frame(dec, frame) >= 0) {
                qint64 relMs = ptsToRelMs(frame->best_effort_timestamp);
                if (!catchupMeasured) {
                    catchupMeasured = true;
                    m_lastCatchupMs = target - relMs;   // 学习本文件 GOP 长度
                    // 首帧实测追赶长度：>4s 且还在硬解 → 切多线程软解重 seek。
                    // 硬解短追赶低延迟（1-2ms/帧），软解长追赶高吞吐（~3000fps
                    // vs ~500-1000fps），一次额外 seek 代价 ≪ 长追赶节省
                    if (dec == m_vdec && m_lastCatchupMs > 4000 && ensureScrubDecoder()) {
                        av_frame_unref(frame);
                        av_packet_unref(pkt);
                        scrubRedirectDemuxer(target);   // flush 两个解码器
                        m_chaseDec = m_scDec;
                        dec = m_scDec;
                        m_chaseDecodePosMs = -1;
                        m_chaseSeekTargetMs = target;
                        catchupMeasured = false;
                        break;   // demux 已重定位，外层 while 继续读包喂软解
                    }
                }
                m_chaseDecodePosMs = relMs;   // 解码位置始终跟踪（含未显示帧）
                if (qAbs(target - relMs) > SEEK_THRESHOLD_MS) {
                    reseek = true;            // 目标飞奔远离，解码追不上 → 交外层重 seek
                } else if (relMs >= target - halfFrame) {
                    // 指哪播哪：只显示目标窗口帧；落后帧静默解码跳过（GOP 引用链
                    // 必须逐帧过，但不做 sws/emit——绝不播放中间帧，避免快进感）
                    displayFrame(frame);      // 含硬解回传/sws/positionChanged/frameReady
                    shown = true;
                }
                av_frame_unref(frame);
                if (shown || reseek)
                    break;
            }
        }
        av_packet_unref(pkt);
    }

    av_frame_free(&frame);
    av_packet_free(&pkt);
    return shown;
}

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
        // --- 状态标志 ---
        bool isScrubbing = m_scrubMode.load();
        bool isActive = (m_state.load() == static_cast<int>(PlaybackState::Playing))
                        || isScrubbing || m_stepOnce;

        // --- 处理命令 ---
        {
            QMutexLocker lock(&m_cmdMutex);
            if (m_pendingCmd == Command::None && !isActive) {
                // 空闲：代理路径打开待办 → 主管线沉淀补全 → 预读缓存 → 睡眠
                if (!m_pxPathPending.isEmpty()) {
                    QString path = m_pxPathPending;
                    m_pxPathPending.clear();
                    lock.unlock();
                    openProxy(path);
                    continue;
                }
                if (m_mainSeekPending
                    && m_monotonic.isValid()
                    && m_monotonic.elapsed() - m_lastSeekElapsed > 300) {
                    m_mainSeekPending = false;
                    qint64 pos = m_positionMs.load();
                    lock.unlock();
                    handleSeek(pos, true);
                    continue;
                }
                if (m_pfPendingFromMs >= 0) {
                    lock.unlock();
                    prefetchStart(m_pfPendingFromMs);
                    continue;
                }
                if (m_pfFmt) {
                    lock.unlock();
                    prefetchStep();
                    continue;
                }
                m_cmdCond.wait(&m_cmdMutex, 50);
            }
            Command cmd = m_pendingCmd;
            qint64 arg = m_cmdArg;
            m_pendingCmd = Command::None;
            lock.unlock();

            switch (cmd) {
            case Command::Play:
                prefetchAbort();
                m_mainSeekPending = false;
                handleSeek(m_positionMs.load(), true);
                if (m_eof)
                    handleSeek(0, true);
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
                m_mainSeekPending = false;
                handleSeek(0, true);
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
        if (m_state.load() != static_cast<int>(PlaybackState::Playing) && !m_scrubMode.load() && !m_stepOnce)
            continue;

        // --- Scrub 追逐解码：围绕原子目标连续解码（代理在走代理 960p，
        //     不在走主管线全分辨率——decode 速度足够，零等待零磁盘） ---
        if (m_scrubMode.load()) {
            if (m_pxReady)
                scrubChasePxFrame();
            else
                scrubChaseMainFrame();
            continue;
        }

        // --- 解复用 ---
        int ret = av_read_frame(m_fmt, pkt);
        if (ret == AVERROR_EOF) {
            if (!m_drainedAtEof) {
                avcodec_send_packet(m_vdec, nullptr);
                drainDecoder();
                m_drainedAtEof = true;
                continue;
            }
            if (m_stepOnce) {
                m_stepOnce = false;
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
            if (++consecutiveErrors > 100) {
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

    // D3D11VA 硬解（可选；任何一步失败都静默回退软解）
    m_hwActive = false;
    m_hwAdapterName.clear();
    bool wantHw = false;
    int hwErr = 0;
    if (m_hwDecodeEnabled.load()) {
        int idx = m_hwAdapterIndex.load();
        QVector<D3D11AdapterInfo> ads = availableAdapters();

        // 自动策略：偏好 Dedicated VRAM 最大者（独显）
        if (idx < 0 && !ads.isEmpty()) {
            int best = 0;
            for (int i = 1; i < ads.size(); ++i)
                if (ads[i].dedicatedVramMB > ads[best].dedicatedVramMB)
                    best = i;
            idx = ads[best].index;
        }

        // 逐个适配器尝试创建 D3D11VA 设备上下文
        QList<int> tryOrder;
        if (idx >= 0)
            tryOrder << idx;
        for (const auto &a : ads)
            if (!tryOrder.contains(a.index))
                tryOrder << a.index;

        for (int tryIdx : tryOrder) {
            QByteArray dev = QByteArray::number(tryIdx);
            hwErr = av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_D3D11VA,
                                           dev.constData(), nullptr, 0);
            if (hwErr >= 0) {
                for (const auto &a : ads)
                    if (a.index == tryIdx) { m_hwAdapterName = a.name; break; }
                break;
            }
        }

        // 所有适配器都失败：最后尝试系统默认（无 device 参数）
        if (hwErr < 0) {
            hwErr = av_hwdevice_ctx_create(&m_hwDeviceCtx, AV_HWDEVICE_TYPE_D3D11VA,
                                           nullptr, nullptr, 0);
            if (hwErr >= 0)
                m_hwAdapterName = "System default";
        }
        wantHw = (hwErr >= 0);
    }
    if (wantHw) {
        m_vdec->hw_device_ctx = av_buffer_ref(m_hwDeviceCtx);
        m_vdec->get_format = &getHwFormatD3D11;
        // hwaccel 与帧级多线程不兼容（会静默回退软解），硬解时单线程
        m_vdec->thread_count = 1;
    } else {
        m_vdec->thread_count = 0; // 软解自动多线程
    }

    if (avcodec_open2(m_vdec, codec, nullptr) < 0) {
        // 硬解上下文打开失败：去掉硬解重试软解
        if (m_vdec->hw_device_ctx) {
            av_buffer_unref(&m_vdec->hw_device_ctx);
            m_vdec->get_format = nullptr;
            if (avcodec_open2(m_vdec, codec, nullptr) < 0) {
                qWarning() << "FfmpegVideoEngine: cannot open decoder";
                return false;
            }
        } else {
            qWarning() << "FfmpegVideoEngine: cannot open decoder";
            return false;
        }
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
    closeProxy();
    m_pxPathPending.clear();
    m_mainSeekPending = false;
    prefetchAbort();
    m_frameCache.clear();
    m_pfPendingFromMs = -1;
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
    if (m_scDec) { avcodec_free_context(&m_scDec); m_scDec = nullptr; }
    m_chaseDec = nullptr;
    m_lastCatchupMs = 0;
    m_chaseSeekTargetMs = -1;
    if (m_hwDeviceCtx) { av_buffer_unref(&m_hwDeviceCtx); m_hwDeviceCtx = nullptr; }
    m_hwActive = false;
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

void FfmpegVideoEngine::handleSeek(qint64 timeMs, bool forceMainPipeline)
{
    if (!m_fmt)
        return;

    timeMs = qBound<qint64>(0, timeMs, m_durationMs > 0 ? m_durationMs.load() : timeMs);

    // --- 拖拽模式（Scrub）：只写原子追逐目标，worker scrub 循环围绕它
    //     连续解码追赶（代理/主管线路径由 worker 按 m_pxReady 自选）——
    //     不再每个 mouseMove 都 seek+flush（"帧幻灯片"的根因） ---
    if (m_scrubMode) {
        m_scrubTargetMs = timeMs;
        m_cmdCond.wakeAll();
        return;
    }

    m_scrubTargetMs = -1;  // 非 scrub 模式，重置追逐目标

    // --- 一次性 seek（非拖拽：点击/沉淀/播放启动） ---

    // 代理路径：一次性 seek + 显示精确帧
    if (!forceMainPipeline && m_pxReady
        && m_state.load() != static_cast<int>(PlaybackState::Playing)) {
        if (proxyDisplayFrame(timeMs)) {
            m_mainSeekPending = true;
            if (!m_monotonic.isValid())
                m_monotonic.start();
            m_lastSeekElapsed = m_monotonic.elapsed();
            m_eof = false;
            return;
        }
    }

    // 主管线 seek：demuxer 重定位 + flush
    scrubRedirectDemuxer(timeMs);

    // seek 落点可能早于目标（关键帧），解码后丢弃早于目标的帧（容差半帧）
    qint64 halfFrame = m_fps > 0.0f ? static_cast<qint64>(500.0f / m_fps.load()) : 20;
    m_discardBeforeRelMs = qMax<qint64>(0, timeMs - halfFrame);
    m_audioDiscardBeforeRelMs = m_discardBeforeRelMs;
    m_eof = false;
    m_clockValid = false;
    m_needMarginMeasure = !m_indexed;
    m_positionMs = timeMs;
    emit positionChanged(timeMs);

    // 非播放态：解码显示目标帧后停留
    if (m_state.load() != static_cast<int>(PlaybackState::Playing)) {
        m_stepOnce = true;
        tryDisplayFromCache(timeMs);
        m_pfPendingFromMs = timeMs;
    }
}

void FfmpegVideoEngine::scrubRedirectDemuxer(qint64 timeMs)
{
    qint64 demuxTargetMs = timeMs;
    if (!m_indexed)
        demuxTargetMs = qMax<qint64>(0, timeMs - m_seekMarginMs);

    m_demuxTargetRelMs = demuxTargetMs;
    m_needMarginMeasure = !m_indexed;
    int64_t ts = static_cast<int64_t>(m_startPtsMs + demuxTargetMs) * 1000;

    int ret = avformat_seek_file(m_fmt, -1, INT64_MIN, ts, ts, AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
        ret = avformat_seek_file(m_fmt, -1, INT64_MIN, ts, INT64_MAX,
                                 AVSEEK_FLAG_BYTE | AVSEEK_FLAG_BACKWARD);
    if (ret < 0)
        av_seek_frame(m_fmt, -1, ts, AVSEEK_FLAG_BACKWARD);

    // 始终清空解码器（frame-threading 下"不清空前进"会导致乱序帧）
    avcodec_flush_buffers(m_vdec);
    if (m_scDec)
        avcodec_flush_buffers(m_scDec);
    if (m_adec)
        avcodec_flush_buffers(m_adec);
    if (m_sink) {
        m_sink->reset();
        m_sinkIo = m_sink->start();
    }
    m_audioBytesWritten = 0;
    m_audioBaseRelMs = -1;
    m_smoothAudioClock = -1;
    m_lastRawAudioClock = -1;

    m_discardBeforeRelMs = -1;
    m_audioDiscardBeforeRelMs = -1;
    m_clockValid = false;
}

// ---------------------------------------------------------------------------
// 空闲预读缓存（F-D2）：独立第二 demux 上下文，解码 seek 点后 CACHE_SPAN_MS
// 的帧为 1080p QImage 缓存；任何新命令立即中止。
// ---------------------------------------------------------------------------

void FfmpegVideoEngine::prefetchStart(qint64 fromRelMs)
{
    prefetchAbort();
    if (m_pendingPath.isEmpty() || fromRelMs < 0)
        return;

    if (avformat_open_input(&m_pfFmt, m_pendingPath.toUtf8().constData(),
                            nullptr, nullptr) < 0) {
        m_pfFmt = nullptr;
        return;
    }
    m_pfVstream = av_find_best_stream(m_pfFmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
    if (m_pfVstream < 0) {
        prefetchAbort();
        return;
    }
    AVStream *vs = m_pfFmt->streams[m_pfVstream];
    const AVCodec *codec = avcodec_find_decoder(vs->codecpar->codec_id);
    if (!codec) {
        prefetchAbort();
        return;
    }
    m_pfDec = avcodec_alloc_context3(codec);
    avcodec_parameters_to_context(m_pfDec, vs->codecpar);
    if (avcodec_open2(m_pfDec, codec, nullptr) < 0) {
        prefetchAbort();
        return;
    }

    int64_t ts = static_cast<int64_t>(m_startPtsMs + fromRelMs) * 1000;
    avformat_seek_file(m_pfFmt, -1, INT64_MIN, ts, ts, AVSEEK_FLAG_BACKWARD);
    avcodec_flush_buffers(m_pfDec);
    m_pfEndMs = fromRelMs + CACHE_SPAN_MS;
    m_pfPendingFromMs = -1;
}

void FfmpegVideoEngine::prefetchStep()
{
    if (!m_pfFmt || !m_pfDec)
        return;

    AVPacket *pkt = av_packet_alloc();
    AVFrame *frame = av_frame_alloc();
    for (int n = 0; n < 4; ++n) {               // 每次最多处理 4 个包，保持响应
        if (m_quit.load() || hasPendingCommand())
            break;
        int ret = av_read_frame(m_pfFmt, pkt);
        if (ret < 0) {                          // EOF 或错误：冲空后结束预读
            avcodec_send_packet(m_pfDec, nullptr);
            while (avcodec_receive_frame(m_pfDec, frame) >= 0) {
                qint64 relMs = 0;               // 尾部帧直接入缓存
                AVStream *vs = m_pfFmt->streams[m_pfVstream];
                relMs = av_rescale_q(frame->best_effort_timestamp,
                                     vs->time_base, AVRational{1, 1000}) - m_startPtsMs;
                if (relMs <= m_pfEndMs && !m_frameCache.contains(relMs)) {
                    // 与主管线同规则缩放到 1080p
                    int w = frame->width, h = frame->height;
                    if (w > 1920) { h = h * 1920 / w; w = 1920; }
                    if (!m_swsPf) {
                        m_swsPf = sws_getContext(frame->width, frame->height,
                            static_cast<AVPixelFormat>(frame->format),
                            w, h, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
                    }
                    if (m_swsPf) {
                        QImage img(w, h, QImage::Format_RGB888);
                        uint8_t *dst[4] = { img.bits(), nullptr, nullptr, nullptr };
                        int ls[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };
                        sws_scale(m_swsPf, frame->data, frame->linesize, 0,
                                  frame->height, dst, ls);
                        if (m_frameCache.size() < CACHE_MAX_FRAMES)
                            m_frameCache.insert(relMs, img);
                    }
                }
                av_frame_unref(frame);
            }
            prefetchAbort();
            break;
        }
        if (pkt->stream_index != m_pfVstream) {
            av_packet_unref(pkt);
            continue;
        }
        if (avcodec_send_packet(m_pfDec, pkt) >= 0) {
            while (avcodec_receive_frame(m_pfDec, frame) >= 0) {
                AVStream *vs = m_pfFmt->streams[m_pfVstream];
                qint64 relMs = av_rescale_q(frame->best_effort_timestamp,
                                            vs->time_base, AVRational{1, 1000}) - m_startPtsMs;
                if (relMs > m_pfEndMs) {
                    av_frame_unref(frame);
                    prefetchAbort();
                    av_packet_unref(pkt);
                    av_frame_free(&frame);
                    av_packet_free(&pkt);
                    return;
                }
                if (!m_frameCache.contains(relMs)) {
                    int w = frame->width, h = frame->height;
                    if (w > 1920) { h = h * 1920 / w; w = 1920; }
                    if (!m_swsPf) {
                        m_swsPf = sws_getContext(frame->width, frame->height,
                            static_cast<AVPixelFormat>(frame->format),
                            w, h, AV_PIX_FMT_RGB24, SWS_BILINEAR, nullptr, nullptr, nullptr);
                    }
                    if (m_swsPf) {
                        QImage img(w, h, QImage::Format_RGB888);
                        uint8_t *dst[4] = { img.bits(), nullptr, nullptr, nullptr };
                        int ls[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };
                        sws_scale(m_swsPf, frame->data, frame->linesize, 0,
                                  frame->height, dst, ls);
                        if (m_frameCache.size() < CACHE_MAX_FRAMES)
                            m_frameCache.insert(relMs, img);
                    }
                }
                av_frame_unref(frame);
            }
        }
        av_packet_unref(pkt);
    }
    av_frame_free(&frame);
    av_packet_free(&pkt);
}

void FfmpegVideoEngine::prefetchAbort()
{
    if (m_swsPf) { sws_freeContext(m_swsPf); m_swsPf = nullptr; }
    if (m_pfDec) { avcodec_free_context(&m_pfDec); m_pfDec = nullptr; }
    if (m_pfFmt) { avformat_close_input(&m_pfFmt); m_pfFmt = nullptr; }
    m_pfVstream = -1;
}

bool FfmpegVideoEngine::tryDisplayFromCache(qint64 timeMs)
{
    if (m_frameCache.isEmpty())
        return false;
    // 与 drainDecoder 同规则：首个 >= timeMs - 半帧 的缓存帧即为精确帧
    qint64 halfFrame = m_fps > 0.0f ? static_cast<qint64>(500.0f / m_fps.load()) : 20;
    auto it = m_frameCache.lowerBound(timeMs - halfFrame);
    if (it == m_frameCache.end() || it.key() > timeMs + halfFrame)
        return false;
    m_positionMs = it.key();
    emit positionChanged(it.key());
    emit frameReady(it.value());
    evictCache(timeMs);
    return true;
}

void FfmpegVideoEngine::evictCache(qint64 centerMs)
{
    // 保留中心前后窗口，超出丢弃（内存有界）
    const qint64 keep = CACHE_SPAN_MS * 3;
    for (auto it = m_frameCache.begin(); it != m_frameCache.end();) {
        if (qAbs(it.key() - centerMs) > keep)
            it = m_frameCache.erase(it);
        else
            ++it;
    }
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

        // margin 自适应：测量 demux 落点与目标的偏差，动态收缩/扩大前移量
        if (m_needMarginMeasure) {
            m_needMarginMeasure = false;
            qint64 err = relMs - m_demuxTargetRelMs;
            if (err > 0)  // 落点过冲到 demux 目标之后：margin 不足，扩大
                m_seekMarginMs = qMin<qint64>(6000, m_seekMarginMs + err + 1000);
            else if (err < -3000)  // 落点过早：margin 过大，缓慢收缩
                m_seekMarginMs = qMax<qint64>(1000, -err + 1000);
        }

        bool discard = (m_discardBeforeRelMs >= 0 && relMs < m_discardBeforeRelMs);

        // seek 追赶阶段被新命令打断：立即放弃旧追赶，最新目标胜出
        if (discard && hasPendingCommand()) {
            av_frame_unref(frame);
            break;
        }

        if (!discard) {
            m_discardBeforeRelMs = -1;
            bool playing = m_state.load() == static_cast<int>(PlaybackState::Playing);
            bool scrubbing = m_scrubMode.load();

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

            if (playing || scrubbing || m_stepOnce) {
                displayFrame(frame);
                m_stepOnce = false;
                av_frame_unref(frame);
                if (scrubbing) {
                    // Scrub 模式：显示后继续解码下一帧（连续出帧）
                    // 新 seek 命令会 flush 解码器，打断此循环
                    if (hasPendingCommand())
                        break;
                    continue;
                }
                break;    // 非 scrub：暂停态只显示一帧；播放态显示后处理下一个包
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
    // 硬解帧需先回传 CPU（D3D11 纹理 → NV12 系统内存）
    AVFrame *swFrame = nullptr;
    const AVFrame *srcFrame = frame;
    if (frame->format == AV_PIX_FMT_D3D11) {
        m_hwActive = true;
        swFrame = av_frame_alloc();
        if (!swFrame || av_hwframe_transfer_data(swFrame, frame, 0) < 0) {
            if (swFrame)
                av_frame_free(&swFrame);
            return;
        }
        swFrame->best_effort_timestamp = frame->best_effort_timestamp;
        srcFrame = swFrame;
    }

    // 帧格式/尺寸变化时重建 swscale 上下文
    if (!m_sws || m_swsW != srcFrame->width || m_swsH != srcFrame->height
        || m_swsFmt != srcFrame->format) {
        if (m_sws)
            sws_freeContext(m_sws);
        m_sws = sws_getContext(srcFrame->width, srcFrame->height,
                               static_cast<AVPixelFormat>(srcFrame->format),
                               srcFrame->width, srcFrame->height, AV_PIX_FMT_RGB24,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_sws) {
            if (swFrame)
                av_frame_free(&swFrame);
            return;
        }
        m_swsW = srcFrame->width;
        m_swsH = srcFrame->height;
        m_swsFmt = srcFrame->format;
    }

    QImage img(srcFrame->width, srcFrame->height, QImage::Format_RGB888);
    uint8_t *dst[4] = { img.bits(), nullptr, nullptr, nullptr };
    int dstLinesize[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };
    sws_scale(m_sws, srcFrame->data, srcFrame->linesize, 0, srcFrame->height,
              dst, dstLinesize);

    if (swFrame)
        av_frame_free(&swFrame);

    qint64 relMs = ptsToRelMs(frame->best_effort_timestamp);
    m_positionMs = relMs;
    emit positionChanged(relMs);
    emit frameReady(img);
}
