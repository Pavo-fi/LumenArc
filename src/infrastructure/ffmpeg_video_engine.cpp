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
#include <algorithm>
#include <QElapsedTimer>
#include <QAudioSink>
#include <QAudioDevice>
#include <QAudioFormat>
#include <QMediaDevices>
#include <QFile>
#include <QTextStream>
#include <QDateTime>
#include <QStandardPaths>

// --- 临时诊断：音频健康日志（定位“特定文件无声”问题，定位后移除） ---
// 输出 %TEMP%/lumenarc_audio.log；仅工作线程调用，逐行 append
static void audioDiag(const QString &msg)
{
    static QFile f(QStandardPaths::standardLocations(QStandardPaths::TempLocation).first()
                   + QStringLiteral("/lumenarc_audio.log"));
    if (!f.isOpen())
        f.open(QIODevice::Append | QIODevice::Text);
    if (!f.isOpen())
        return;
    QTextStream ts(&f);
    ts << QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"))
       << ' ' << msg << '\n';
    ts.flush();
}

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

void FfmpegVideoEngine::unload()
{
    // 停工作线程（workerMain 退出路径自动 closeFile 释放全部上下文）
    if (m_thread) {
        m_quit = true;
        postCommand(Command::Stop);
        m_thread->quit();
        m_thread->wait(5000);
        m_thread = nullptr;
        m_quit = false;
    }
    // UI 可见状态全部归零：duration=0 → 全局快捷键（空格播放等）自动失效
    m_positionMs = 0;
    m_durationMs = 0;
    m_fps = 0;
    m_width = 0;
    m_height = 0;
    m_pendingPath.clear();
    m_state = static_cast<int>(PlaybackState::Idle);
    emit durationChanged(0);
    emit stateChanged(PlaybackState::Idle);
}

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
/// v1.7.1：音量 >100% 的 PCM 增益（削波保护）。按 sink 实际样本格式分支。
void FfmpegVideoEngine::applyVolumeGain(char *data, qint64 bytes, double gain)
{
    if (gain <= 1.0 || bytes <= 0)
        return;
    switch (m_outSampleFmt) {
    case AV_SAMPLE_FMT_S16: {
        int16_t *s = reinterpret_cast<int16_t *>(data);
        const qint64 n = bytes / 2;
        for (qint64 i = 0; i < n; ++i) {
            const int v = static_cast<int>(s[i]) * gain;
            s[i] = static_cast<int16_t>(qBound(-32768, v, 32767));
        }
        break;
    }
    case AV_SAMPLE_FMT_S32: {
        int32_t *s = reinterpret_cast<int32_t *>(data);
        const qint64 n = bytes / 4;
        for (qint64 i = 0; i < n; ++i) {
            const double v = static_cast<double>(s[i]) * gain;
            s[i] = static_cast<int32_t>(qBound(-2147483648.0, v, 2147483647.0));
        }
        break;
    }
    case AV_SAMPLE_FMT_FLT: {
        float *s = reinterpret_cast<float *>(data);
        const qint64 n = bytes / 4;
        for (qint64 i = 0; i < n; ++i) {
            const float v = static_cast<float>(s[i] * gain);
            s[i] = qBound(-1.0f, v, 1.0f);
        }
        break;
    }
    case AV_SAMPLE_FMT_U8: {
        uint8_t *s = reinterpret_cast<uint8_t *>(data);
        const qint64 n = bytes;
        for (qint64 i = 0; i < n; ++i) {
            const int v = static_cast<int>((static_cast<int>(s[i]) - 128) * gain) + 128;
            s[i] = static_cast<uint8_t>(qBound(0, v, 255));
        }
        break;
    }
    default:
        break;
    }
}

void FfmpegVideoEngine::setVolume(int vol)
{
    // v1.7.1：上限 500%（>100% 部分由 PCM 增益实现，sink 硬件音量封顶 100%）
    m_volume = qBound(0, vol, 500);
    if (m_sink)
        m_sink->setVolume(qMin(m_volume.load(), 100) / 100.0f);
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
    bool fmtSupported = device.isFormatSupported(fmt);
    if (!fmtSupported) {
        // 设备不支持原生率（实测 8kHz AAC → Realtek 不支持，日志 fmtSupported=0）：
        // 在常见配置中探测 Int16 可用格式，避免整体套用 preferredFormat
        // （preferred 可能是 Int64/Float，与重采样输出不一致 → 无声噪声）。
        bool found = false;
        const int rates[] = {48000, 44100, m_outSampleRate, 32000, 22050, 16000};
        for (int r : rates) {
            for (int c : {2, 1}) {
                QAudioFormat cand;
                cand.setSampleRate(r);
                cand.setChannelCount(c);
                cand.setSampleFormat(QAudioFormat::Int16);
                if (device.isFormatSupported(cand)) {
                    fmt = cand;
                    found = true;
                    break;
                }
            }
            if (found)
                break;
        }
        if (!found) {
            // 极端兜底：preferred 的率/声道 + Int16（格式仍与重采样一致）
            const QAudioFormat pf = device.preferredFormat();
            fmt.setSampleRate(pf.sampleRate());
            fmt.setChannelCount(pf.channelCount());
        }
        m_outSampleRate = fmt.sampleRate();
        m_outChannels = fmt.channelCount();
        fmtSupported = false;
    }
    m_outBytesPerSample = fmt.bytesPerSample();
    if (m_outBytesPerSample <= 0)
        m_outBytesPerSample = 2;
    audioDiag(QStringLiteral("ensureAudioOutput: dev='%1' in=%2Hz/%3ch fmtSupported=%4 "
                              "out=%5Hz/%6ch fmt=%7bps=%8")
              .arg(device.description()).arg(m_adec->sample_rate)
              .arg(m_adec->ch_layout.nb_channels).arg(fmtSupported)
              .arg(m_outSampleRate).arg(m_outChannels)
              .arg(int(fmt.sampleFormat())).arg(m_outBytesPerSample));

    // 重采样器：解码帧格式 → sink 实际格式（样本类型跟随 fmt，实测设备可能是 Int64）
    AVSampleFormat outFmt = AV_SAMPLE_FMT_S16;
    switch (fmt.sampleFormat()) {
    case QAudioFormat::UInt8:  outFmt = AV_SAMPLE_FMT_U8;  break;
    case QAudioFormat::Int16:  outFmt = AV_SAMPLE_FMT_S16; break;
    case QAudioFormat::Int32:  outFmt = AV_SAMPLE_FMT_S32; break;
    case QAudioFormat::Float:  outFmt = AV_SAMPLE_FMT_FLT; break;
    default:                   outFmt = AV_SAMPLE_FMT_S16; break;
    }
    m_outSampleFmt = outFmt;   // v1.7.1：PCM 增益按格式处理
    AVChannelLayout outLayout;
    av_channel_layout_default(&outLayout, m_outChannels);
    if (swr_alloc_set_opts2(&m_swr, &outLayout, outFmt, m_outSampleRate,
                            &m_adec->ch_layout, m_adec->sample_fmt,
                            m_adec->sample_rate, 0, nullptr) < 0 || !m_swr)
        return false;
    av_channel_layout_uninit(&outLayout);
    if (swr_init(m_swr) < 0) {
        swr_free(&m_swr);
        return false;
    }

    int bytesPerSec = m_outSampleRate * m_outChannels * m_outBytesPerSample;
    m_sink = new QAudioSink(device, fmt);
    m_sink->setBufferSize(bytesPerSec);              // 1s 设备缓冲
    m_sink->setVolume(qMin(m_volume.load(), 100) / 100.0f);
    // 推模式：工作线程无 Qt 事件循环，拉模式的设备回调不会被驱动
    m_sinkIo = m_sink->start();
    m_audioSinkOk = (m_sinkIo != nullptr);
    audioDiag(QStringLiteral("sink start: io=%1 state=%2 err=%3 bufSize=%4")
              .arg(m_sinkIo != nullptr).arg(m_sink->state()).arg(m_sink->error())
              .arg(bytesPerSec));
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

    // scrub 拖拽：片段音频——目标变更且节流窗口已过则重置 sink 布防新片段，
    // 只写目标后 ~100ms 音频，拖拽时听到与光标对齐的声音
    const bool scrubbing = m_scrubMode.load();
    if (scrubbing && !m_scrubSnippetAudio)
        return;   // 快拖（>4×）：片段音频及其收割阻塞追逐循环，关闭
    if (scrubbing) {
        const qint64 target = m_scrubTargetMs.load();
        if (target < 0)
            return;
        if (!m_monotonic.isValid())
            m_monotonic.start();
        const qint64 now = m_monotonic.elapsed();
        if (target != m_scrubAudioTarget
            && now - m_lastScrubAudioEmitElapsed >= SCRUB_AUDIO_EMIT_GAP_MS) {
            if (m_sink) {
                m_sink->reset();            // 丢弃陈旧缓冲，片段与光标对齐
                m_sinkIo = m_sink->start(); // reset 后需重新 start 才能写入（同 seek 路径）
            }
            m_scrubAudioTarget = target;
            m_lastScrubAudioEmitElapsed = now;
        }
        if (target != m_scrubAudioTarget)
            return;   // 节流中：丢弃本包
    }

    if (!ensureAudioOutput()) {
        if (!m_diagOutFailLogged) {
            m_diagOutFailLogged = true;
            audioDiag(QStringLiteral("ensureAudioOutput FAILED: adec=%1 astream=%2")
                      .arg(m_adec != nullptr).arg(m_astream));
        }
        return;
    }

    m_sink->setVolume(qMin(m_volume.load(), 100) / 100.0f);  // 音量原子量随包应用

    if (avcodec_send_packet(m_adec, pkt) < 0)
        return;

    const int inRate = m_adec->sample_rate > 0 ? m_adec->sample_rate : 44100;
    AVFrame *frame = av_frame_alloc();
    while (avcodec_receive_frame(m_adec, frame) >= 0) {
        // --- seek 对齐：丢弃/裁剪早于目标的音频帧（F-A） ---
        qint64 relMs = ptsToRelMsA(frame->best_effort_timestamp);
        qint64 frameDurMs = frame->nb_samples * 1000 / inRate;
        double skipFrac = 0.0;
        // scrub 模式由片段窗口自行定位，忽略 seek 丢弃点（否则回拖到 seek 点前会无声）
        if (!scrubbing && relMs >= 0 && m_audioDiscardBeforeRelMs >= 0) {
            if (relMs + frameDurMs <= m_audioDiscardBeforeRelMs) {
                av_frame_unref(frame);
                continue;                       // 整帧早于目标：丢弃
            }
            if (relMs < m_audioDiscardBeforeRelMs) {
                skipFrac = static_cast<double>(m_audioDiscardBeforeRelMs - relMs)
                           / frameDurMs;        // 部分重叠：按比例裁剪起始部分
            }
        }

        // scrub 片段窗口：只保留 [target, target+WINDOW] 内的帧
        if (scrubbing && (relMs < 0
                          || relMs + frameDurMs <= m_scrubAudioTarget
                          || relMs >= m_scrubAudioTarget + SCRUB_AUDIO_WINDOW_MS)) {
            av_frame_unref(frame);
            continue;
        }

        int outSamples = swr_get_out_samples(m_swr, frame->nb_samples);
        if (outSamples > 0) {
            const int frameBytes = m_outChannels * m_outBytesPerSample;
            QByteArray out(outSamples * frameBytes, Qt::Uninitialized);
            uint8_t *outBuf[1] = { reinterpret_cast<uint8_t *>(out.data()) };
            int converted = swr_convert(m_swr, outBuf, outSamples,
                                        const_cast<const uint8_t **>(frame->extended_data),
                                        frame->nb_samples);
            if (converted > 0) {
                qint64 bytes = static_cast<qint64>(converted) * frameBytes;
                qint64 offset = 0;
                if (skipFrac > 0.0) {
                    qint64 align = frameBytes;
                    offset = (static_cast<qint64>(bytes * skipFrac) / align) * align;
                    offset = qBound<qint64>(0, offset, bytes - align);
                }

                const char *payloadData = out.constData() + offset;
                qint64 payload = bytes - offset;

                // P-54b 播放降噪（v1.16.1）：scrub 片段旁路；DSP 在音量增益与
                // 变速重采样之前。内容流重索引对齐（输出样本 p 恒为输入 p 的
                // 降噪版），计算滞后 ≤kWin 样本由 1s 设备缓冲吸收 → 稳态零偏移
                QByteArray dspOut;
                bool dspActive = false;
                if (!scrubbing && m_pbDenoiseOn.load()
                    && m_pbDenoiseStrength.load() > 0.0) {
                    if (!m_pbDenoise
                        || m_pbDenoise->sampleRate() != m_outSampleRate
                        || m_pbDenoise->channels() != m_outChannels) {
                        // 采样率/声道变化才重建（seek 已在 seek 路径 reset）
                        m_pbDenoise = std::make_unique<SpectralGateStream>();
                        m_pbDenoise->configure(m_outSampleRate, m_outChannels,
                                               m_pbDenoiseStrength.load());
                    } else {
                        // 强度热更新：下一帧生效，不重建状态、无断音
                        m_pbDenoise->setStrength(m_pbDenoiseStrength.load());
                    }
                    // 内容锚点：首个喂入帧的 relMs 即输出流零点（若在写时刻
                    // 锚定会因 DSP 定稿滞后偏晚一窗）
                    if (m_audioBaseRelMs < 0) {
                        if (relMs >= 0)
                            m_audioBaseRelMs = qMax<qint64>(relMs, m_audioDiscardBeforeRelMs);
                        else
                            m_audioBaseRelMs = qMax<qint64>(0, m_audioDiscardBeforeRelMs);
                    }
                    QVector<int16_t> emitted;
                    m_pbDenoise->feed(
                        reinterpret_cast<const int16_t *>(payloadData),
                        static_cast<int>(payload / 2), emitted);
                    if (!emitted.isEmpty()) {
                        dspOut = QByteArray(
                            reinterpret_cast<const char *>(emitted.constData()),
                            int(emitted.size()) * 2);
                        payloadData = dspOut.constData();
                        payload = dspOut.size();
                        dspActive = true;
                    } else {
                        av_frame_unref(frame);
                        continue;   // 定稿前沿未推进（起步缓冲期 ~一窗）
                    }
                } else if (!scrubbing && !m_pbDenoiseOn.load() && m_pbDenoise) {
                    m_pbDenoise.reset();   // 关断即销毁（状态不残留；scrub 不动它）
                }

                // v1.7.1：音量 >100% 的 PCM 增益（削波保护；≤100% 走 sink 音量）
                if (m_volume.load() > 100)
                    applyVolumeGain(dspActive ? dspOut.data() : out.data() + offset,
                                    payload, m_volume.load() / 100.0f);

                if (scrubbing) {
                    // 片段直写：无背压等待（片段 ≤100ms），不打断追逐循环
                    if (m_sinkIo)
                        m_sinkIo->write(payloadData, payload);
                    m_audioBytesWritten += payload;
                    av_frame_unref(frame);
                    continue;
                }

                // 首个写入样本锚定音频时钟基点（F-A：声音与画面同一起点）
                if (m_audioBaseRelMs < 0) {
                    if (relMs >= 0)
                        m_audioBaseRelMs = qMax<qint64>(relMs, m_audioDiscardBeforeRelMs);
                    else
                        m_audioBaseRelMs = qMax<qint64>(0, m_audioDiscardBeforeRelMs);
                }

                // 倍速变速音频：重采样使音频时长随 rate 缩放（音调随速度变化）
                QByteArray resampled;
                const float rate = m_rate.load();
                if (qAbs(rate - 1.0f) > 0.01f) {
                    resampled = resampleAudioForRate(payloadData, payload, rate);
                    if (resampled.isEmpty()) {
                        av_frame_unref(frame);
                        continue;
                    }
                    payloadData = resampled.constData();
                    payload = resampled.size();
                }

                // 背压：设备缓冲满则短暂等待（播放面自然节流）
                int guard = 0;
                while (m_sink->bytesFree() < payload && guard++ < 100
                       && !m_quit.load()
                       && m_state.load() == static_cast<int>(PlaybackState::Playing)
                       && !hasPendingCommand()) {
                    QThread::msleep(2);
                }
                if (m_sinkIo)
                    m_sinkIo->write(payloadData, payload);
                m_audioBytesWritten += payload;
                if (m_tapPcm) {   // AV 追踪：落盘 PCM + 事件行
                    m_tapPcm->write(payloadData, payload);
                    tapAudioWrite(payload, m_sink ? m_sink->bytesFree() : -1);
                }
                if (guard >= 100)
                    audioDiag(QStringLiteral("BACKPRESSURE TIMEOUT: sink not draining "
                              "state=%1 err=%2 bytesFree=%3")
                              .arg(m_sink->state()).arg(m_sink->error())
                              .arg(m_sink->bytesFree()));
                {
                    const int16_t *s = reinterpret_cast<const int16_t *>(payloadData);
                    const int n = static_cast<int>(payload / 2);
                    for (int i = 0; i < n; ++i) {
                        const int a = s[i] < 0 ? -int(s[i]) : int(s[i]);
                        if (a > m_diagPeak)
                            m_diagPeak = a;
                    }
                    m_diagDecodedMs += converted * 1000 / m_outSampleRate;
                    if (m_diagDecodedMs >= 2000) {
                        audioDiag(QStringLiteral(
                            "play: state=%1 err=%2 bytesFree=%3 written=%4 outPeak=%5 (%6 dB)")
                            .arg(m_sink->state()).arg(m_sink->error())
                            .arg(m_sink->bytesFree()).arg(m_audioBytesWritten.load())
                            .arg(m_diagPeak)
                            .arg(m_diagPeak > 0
                                 ? QString::number(20.0 * log10(m_diagPeak / 32768.0), 'f', 1)
                                 : QStringLiteral("-inf")));
                        m_diagDecodedMs = 0;
                        m_diagPeak = 0;
                    }
                }
            }
        }
        av_frame_unref(frame);
    }
    av_frame_free(&frame);
}

QByteArray FfmpegVideoEngine::resampleAudioForRate(const char *pcm, qint64 bytes, float rate) const
{
    // 变速重采样：rate>1 抽稀（加速+高音调），rate<1 插值（减速+低音调）。
    // 线性插值，交错多声道，输出帧数 = floor(输入帧数 / rate)。
    const int ch = qMax(1, m_outChannels);
    const qint64 frameBytes = static_cast<qint64>(ch) * m_outBytesPerSample;
    const qint64 inFrames = frameBytes > 0 ? bytes / frameBytes : 0;
    if (inFrames < 2 || rate <= 0.0f)
        return {};
    const qint64 outFrames = static_cast<qint64>(inFrames / rate);
    if (outFrames <= 0)
        return {};
    QByteArray out(static_cast<int>(outFrames * frameBytes), Qt::Uninitialized);
    if (m_outBytesPerSample == 2) {
        const qint16 *in = reinterpret_cast<const qint16 *>(pcm);
        qint16 *dst = reinterpret_cast<qint16 *>(out.data());
        for (qint64 n = 0; n < outFrames; ++n) {
            const double pos = n * rate;
            const qint64 i0 = static_cast<qint64>(pos);
            const double frac = pos - i0;
            const qint64 i1 = qMin(i0 + 1, inFrames - 1);
            for (int c = 0; c < ch; ++c) {
                const double s = in[i0 * ch + c] * (1.0 - frac) + in[i1 * ch + c] * frac;
                dst[n * ch + c] = static_cast<qint16>(qBound(-32768.0, s, 32767.0));
            }
        }
    } else {
        // 非 Int16（Float/Int64 等）：按 32 位浮点视图插值
        const float *in = reinterpret_cast<const float *>(pcm);
        float *dst = reinterpret_cast<float *>(out.data());
        const qint64 inF = inFrames;
        for (qint64 n = 0; n < outFrames; ++n) {
            const double pos = n * rate;
            const qint64 i0 = static_cast<qint64>(pos);
            const double frac = pos - i0;
            const qint64 i1 = qMin(i0 + 1, inF - 1);
            for (int c = 0; c < ch; ++c) {
                const double s = in[i0 * ch + c] * (1.0 - frac) + in[i1 * ch + c] * frac;
                dst[n * ch + c] = static_cast<float>(s);
            }
        }
    }
    return out;
}

float FfmpegVideoEngine::rate() const { return m_rate.load(); }

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
    // 单线程软解：scrub 追赶专用。多线程（thread_count=0）flush 后线程池重建
    // 首帧代价 100ms+（实测 2304×1296），单线程首帧 ~30ms——scrub 每轮一次
    // seek+flush，单线程总吞吐更高（VLC 软解 scrub 同思路）
    m_scDec->thread_count = 1;
    m_scDec->pkt_timebase = vs->time_base;
    if (avcodec_open2(m_scDec, codec, nullptr) < 0) {
        avcodec_free_context(&m_scDec);
        return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// 拖拽滚动帧缓存：追赶解码副产品帧的环形缓存（仅软解帧，refcount clone）。
// 预算 192MB：1440p(NV12 5.5MB)≈34 帧≈1.4s，1080p≈64 帧≈2.6s，4K≈15 帧。
// 目标命中 → 直接显示（后退微调/抖动 0ms，免 seek 免重解码）。
// ---------------------------------------------------------------------------

void FfmpegVideoEngine::chaseCachePush(qint64 relMs, AVFrame *frame)
{
    AVFrame *clone = av_frame_clone(frame);
    if (!clone)
        return;
    const qint64 frameBytes = qint64(frame->width) * frame->height * 3 / 2;
    const qint64 budget = 192LL * 1024 * 1024;
    const int maxEntries = qBound(8, frameBytes > 0 ? int(budget / frameBytes) : 32, 64);
    while (m_chaseCache.size() >= maxEntries) {
        av_frame_free(&m_chaseCache.first().frame);
        m_chaseCache.removeFirst();
    }
    m_chaseCache.append({ relMs, clone });
}

AVFrame *FfmpegVideoEngine::chaseCacheFind(qint64 targetMs, qint64 halfFrameMs)
{
    // 显示窗口 [target-半帧, target+半帧]（与实时路径同语义）。
    // 上界是关键安全条件：目标已退出缓存覆盖范围时（如连续回退），
    // 若返回远在目标前方的帧，显示后位置仍远离目标 → 下次调用命中同一帧 →
    // 工作线程自旋 + positionChanged 洪泛 → UI 失去响应（实测复现）。
    // 缓存向量在回退重解码后可能非严格单调（旧副本+新副本），故全扫描取窗口内最近帧。
    AVFrame *best = nullptr;
    qint64 bestDist = INT64_MAX;
    for (const auto &e : m_chaseCache) {
        const qint64 dist = qAbs(e.relMs - targetMs);
        if (dist <= halfFrameMs && dist < bestDist) {
            bestDist = dist;
            best = e.frame;
        }
    }
    return best;
}

void FfmpegVideoEngine::chaseCacheClear()
{
    for (auto &e : m_chaseCache)
        av_frame_free(&e.frame);
    m_chaseCache.clear();
}

void FfmpegVideoEngine::diagScrubDisplay(const char *site, qint64 relMs, qint64 target)
{
    static qint64 lastElapsed = 0;
    const qint64 now = m_monotonic.elapsed();
    const qint64 roundMs = lastElapsed > 0 ? now - lastElapsed : 0;
    lastElapsed = now;
    audioDiag(QStringLiteral("scrub display[%1]: rel=%2 target=%3 err=%4 gopLearn=%5 "
              "chaseWall=%6 hopArmed=%7 vel=%8 round=%9ms")
              .arg(QString::fromLatin1(site)).arg(relMs).arg(target).arg(target - relMs)
              .arg(m_gopLearnMs.load()).arg(m_lastChaseWallMs).arg(m_hopFirstFrame ? 1 : 0)
              .arg(m_scrubVel, 0, 'f', 0).arg(roundMs));
}

void FfmpegVideoEngine::diagScrubFlush()
{
    const qint64 now = m_monotonic.elapsed();
    if (now - m_diagScrubLastLogElapsed >= 2000) {
        audioDiag(QStringLiteral(
            "scrub 2s: displays=%1 cacheHits=%2 reseeks=%3 uiDrops=%4 maxGap=%5ms inFlight=%6")
            .arg(m_diagScrubDisplays).arg(m_diagScrubCacheHits)
            .arg(m_diagScrubReseeks).arg(m_diagScrubDrops)
            .arg(m_diagScrubMaxGapMs).arg(m_framesInFlight.load()));
        m_diagScrubDisplays = m_diagScrubCacheHits = m_diagScrubReseeks
                            = m_diagScrubDrops = m_diagScrubMaxGapMs = 0;
        m_diagScrubLastLogElapsed = now;
    }
}

void FfmpegVideoEngine::diagScrubTick()
{
    const qint64 now = m_monotonic.elapsed();
    if (m_diagScrubLastDispElapsed >= 0) {
        const int gap = int(now - m_diagScrubLastDispElapsed);
        if (gap > m_diagScrubMaxGapMs)
            m_diagScrubMaxGapMs = gap;   // 相邻显示最大墙钟间隔（卡顿体感）
    }
    m_diagScrubLastDispElapsed = now;
    ++m_diagScrubDisplays;
    diagScrubFlush();
}

bool FfmpegVideoEngine::scrubChaseMainFrame()
{
    if (!m_fmt || !m_vdec)
        return false;
    if (!m_monotonic.isValid())
        m_monotonic.start();

    const qint64 frameMs = m_fps > 0.0f ? static_cast<qint64>(1000.0f / m_fps.load()) : 40;
    const qint64 halfFrame = frameMs / 2;
    // 前进 decode-through 阈值：按学习到的 GOP 长度自适应——临界点是
    // "直追成本 delta/吞吐 = seek 固定成本 + 半个 GOP 的追赶成本"。
    // 稀疏 GOP（D17 10s）早 seek 更快；密 GOP 直追便宜。无索引文件收紧（margin 有界）
    // 超长 GOP（>6s，如监控恢复文件 15s GOP）：关键帧跳显模式，阈值收紧——
    // 目标跑过 2.5s 即重 seek 跳显下一关键帧，不做 GOP 全程解码追赶
    // （15s GOP 硬解追赶 ≈1s+ 墙钟停顿，是这类文件拖拽跳跃卡顿的物理根源）
    // 超长 GOP（>6s）且实测追赶墙钟耗时 >120ms（解码器喂不饱）：关键帧跳显
    // 模式，阈值收紧——目标跑过 2.5s 即重 seek 跳显下一关键帧，不做 GOP 全程
    // 解码追赶（慢解码 + 长 GOP = 秒级墙钟停顿，是拖拽跳跃卡顿的物理根源）。
    // 快解码器（如 D17 1440p 软解 ~3000fps，10s GOP 追赶仅 ~80ms）不跳显，
    // 保持精确追赶——按实测墙钟自适应，不按文件类型一刀切
    const qint64 gopEst = m_gopLearnMs.load() > 0 ? m_gopLearnMs.load() : 20000;
    // 跳显判定（双通道，都用 reseek 落点实测的 GOP m_gopLearnMs，避免被
    // 直追期的 m_lastCatchupMs 噪声污染）：
    // ① sparseGop——超长 GOP 且追赶墙钟慢（>120ms）：跳显阈值 3s
    // ② denseVelHop——密 GOP（≤2s）+ 快拖超实测解码吞吐（~31×）：跳显阈值 300ms，
    //    误差 ≤1 GOP 在快拖中不可见；长 GOP 快解码文件（D17 10s GOP 追赶仅 ~80ms）
    //    不进此通道，保持精确追赶
    const bool sparseGop = m_gopLearnMs.load() > 6000 && m_lastChaseWallMs > 120;
    // 快拖跳显：目标速度超实测解码吞吐（~31×）时布防——但跳显与否最终由
    // 显示点的实测误差决定（m_gopLearnMs 是“目标在 GOP 内的偏移”而非 GOP
    // 长度，不能用于判定；误差 >2s 说明 GOP 其实很长，放弃跳显让精确追赶兜底）
    const bool denseVelHop = qAbs(m_scrubVel) > 30.0 && !sparseGop;
    const bool hopMode = sparseGop || denseVelHop;
    const qint64 SEEK_THRESHOLD_MS = hopMode ? 2500
                                     : m_indexed ? qBound<qint64>(4000LL, gopEst, 20000LL)
                                                 : qMax<qint64>(4000, m_seekMarginMs * 2);

    qint64 target = m_scrubTargetMs.load();
    if (target < 0) {
        QThread::msleep(10);     // 刚进入 scrub，尚无目标
        return false;
    }

    // 目标速度 EMA → 片段音频闸（>4× 快拖关闭片段音频与其收割）
    {
        const qint64 nowV = m_monotonic.elapsed();
        if (m_velLastTargetMs >= 0) {
            const qint64 dt = nowV - m_velLastElapsed;
            if (dt > 0) {
                const double inst = double(target - m_velLastTargetMs) / double(dt);
                m_scrubVel = 0.5 * m_scrubVel + 0.5 * inst;
            }
        }
        m_velLastTargetMs = target;
        m_velLastElapsed = nowV;
        m_scrubSnippetAudio = (qAbs(m_scrubVel) < 4.0);
    }

    // 滚动缓存命中：目标帧已在追赶副产品缓存中（后退微调/抖动/重复目标），
    // 直接显示，零 seek 零解码——这是慢拖手感的主要来源。
    // 显示经 ~30Hz 节拍闸：闸未开短睡等下一拍，保证显示墙钟时刻均匀
    if (qAbs(target - m_positionMs.load()) > halfFrame) {
        if (AVFrame *hit = chaseCacheFind(target, halfFrame)) {
            const qint64 now = m_monotonic.elapsed();
            if (now - m_lastScrubDisplayElapsed >= scrubGateIntervalMs()) {
                ++m_diagScrubCacheHits;   // 临时诊断
                diagScrubDisplay("cache", ptsToRelMs(hit->best_effort_timestamp), target);
                displayFrame(hit);
                m_lastScrubDisplayElapsed = now;
            } else {
                QThread::msleep(2);   // 节拍闸未开：短睡等下一拍，避免空转
            }
            return true;
        }
    }

    // 追逐基准：解码位置（含未显示帧）> 本次 seek 目标 > 显示位置。
    // decodePos 未知（软解管线填充期无输出）时用 seek 目标——光标移动 <20s 时
    // 继续当前追赶而非重 seek（防止填充期 decodePos 停滞导致的 reseek 风暴）
    const qint64 anchor = (m_chaseDecodePosMs >= 0) ? m_chaseDecodePosMs
                        : (m_chaseSeekTargetMs >= 0) ? m_chaseSeekTargetMs
                        : m_positionMs.load();
    const qint64 delta = target - anchor;
    bool wantReseek = (delta > SEEK_THRESHOLD_MS || delta < -2 * frameMs);
    // 跳显抑制：上一跳显示的关键帧距目标 gap（≈GOP 长），目标未越过
    // "上一跳目标 + gap"（≈下一关键帧）前不重 seek——否则 decodePos 停在
    // 关键帧、delta 永远超阈，退化成同一关键帧的 seek+重复显示风暴
    if (wantReseek && delta > SEEK_THRESHOLD_MS && m_hopTargetMs >= 0
        && target < m_hopTargetMs + m_hopGapMs)
        wantReseek = false;
    bool didReseek = false;
    if (wantReseek) {
        didReseek = true;
        ++m_diagScrubReseeks;   // 临时诊断
        // 大跳/后退：主管线 seek + flush（落在上一个关键帧，下方解码追赶到目标窗口）
        // 追赶过滤由下方目标窗口完成，不走 drainDecoder 的 discard 路径
        m_lastReseekWallMs = m_monotonic.elapsed();   // 诊断：seek 段计时起点
        scrubRedirectDemuxer(target);   // 内含 m_vdec/m_scDec flush
        // 超长 GOP / 密 GOP 超吞吐快拖：布防关键帧跳显。仅前进布防——
        // 后退步进是精细回看，照常追赶到精确目标窗口
        m_hopFirstFrame = hopMode && (delta > SEEK_THRESHOLD_MS);
        m_hopTargetMs = -1;             // 方向/落点变更：旧抑制锚点失效
        // 短追赶用硬解（thread_count=1 无管线填充延迟）；
        // 长 GOP（实测 >4s）用多线程软解（~3000fps 吞吐）
        // scrub chase 优先硬解（主管线 m_vdec）：2304×1296 IDR 硬解 ~5ms vs 软解
        // ~110ms（实测），总 ~40ms/轮 → ~25fps 拖拽显示；软解（m_scDec）仅
        // 硬解不可用时兜底。退出 scrub 后主管线先经 scrubRedirectDemuxer
        // （scrubMode=false）正常 flush，状态干净。
        m_chaseDec = m_hwDecodeEnabled
                     ? m_vdec
                     : (ensureScrubDecoder() ? m_scDec : m_vdec);
        m_chaseStartElapsed = m_monotonic.elapsed();   // 追赶墙钟计时起点
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
    bool harvesting = false;  // 片段音频收割中：目标帧已显示，继续读包补全 100ms 音频窗口
    int harvestBudget = 0;    // 收割剩余包预算（防高码流空转）

    while (!reseek && !m_quit.load() && !hasPendingCommand()) {
        if (shown && !harvesting)
            break;                        // 显示完成且无收割任务 → 交外层
        target = m_scrubTargetMs.load();      // 追逐中目标持续移动，每包刷新
        if (target < 0)
            break;
        int ret = av_read_frame(m_fmt, pkt);
        if (ret == AVERROR_EOF) {
            avcodec_send_packet(dec, nullptr);   // 冲空拿尾部帧
            while (avcodec_receive_frame(dec, frame) >= 0) {
                qint64 relMs = ptsToRelMs(frame->best_effort_timestamp);
                m_chaseDecodePosMs = relMs;
                if (dec == m_scDec)
                    chaseCachePush(relMs, frame);   // 软解帧入滚动缓存（refcount 零拷贝）
                if (!shown && relMs >= target - halfFrame) {
                    diagScrubDisplay("eof-drain", relMs, target);
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
        if (harvesting && --harvestBudget <= 0)
            harvesting = false;                   // 预算耗尽，交外层
        if (pkt->stream_index != m_vstream) {
            if (pkt->stream_index == m_astream) {
                processAudioPacket(pkt);   // scrub 片段音频（拖拽有声）
                if (harvesting && m_scrubAudioTarget >= 0) {
                    // 音频已覆盖满布防窗口 [target, target+100ms] → 收割完成
                    const qint64 apts = ptsToRelMsA(pkt->pts);
                    if (apts >= m_scrubAudioTarget + SCRUB_AUDIO_WINDOW_MS)
                        harvesting = false;
                }
            }
            av_packet_unref(pkt);
            continue;
        }
        if (avcodec_send_packet(dec, pkt) >= 0) {
            while (avcodec_receive_frame(dec, frame) >= 0) {
                qint64 relMs = ptsToRelMs(frame->best_effort_timestamp);
                if (!catchupMeasured) {
                    catchupMeasured = true;
                    // 学习本文件 GOP 长度（用 seek 发起时的目标，避免快拖中
                    // 光标已前移数十秒的污染——实测 gopLearn 被写成 96s）
                    const qint64 seekT = m_chaseSeekTargetMs >= 0
                        ? m_chaseSeekTargetMs : target;
                    m_lastCatchupMs = seekT - relMs;
                    if (didReseek)
                        m_gopLearnMs = seekT - relMs;
                    // 首帧实测追赶长度：>4s 且还在硬解 → 切多线程软解重 seek。
                    // 硬解短追赶低延迟，软解长追赶高吞吐，一次额外 seek ≪ 长追赶节省
                    if (dec == m_vdec && m_gopLearnMs.load() > 4000 && !m_hopFirstFrame
                        && ensureScrubDecoder()) {
                        av_frame_unref(frame);
                        av_packet_unref(pkt);
                        scrubRedirectDemuxer(target);   // flush 两个解码器
                        m_chaseDec = m_scDec;
                        dec = m_scDec;
                        m_chaseDecodePosMs = -1;
                        m_chaseSeekTargetMs = target;
                        // 追赶墙钟从重 seek 点重计：不把硬解空耗算进软解样本，
                        // 防胀大的墙钟样本把 sparseGop 误判布防（D17 回归）
                        m_chaseStartElapsed = m_monotonic.elapsed();
                        catchupMeasured = false;
                        break;   // demux 已重定位，外层 while 继续读包喂软解
                    }
                }
                m_chaseDecodePosMs = relMs;   // 解码位置始终跟踪（含未显示帧）
                // 关键帧跳显（超长 GOP 前进飞奔）：seek 落点首帧即关键帧，追赶 >3s
                // 直接显示本帧（Premiere 式预览，即时反馈）。不置 shown——
                // decode-through 继续向目标窗口推进，decodePos 前进防 reseek 空转
                if (m_hopFirstFrame && !shown) {
                    m_hopFirstFrame = false;
                    // 误差以“seek 发起时的目标”为基准：快拖时解码追赶期间光标
                    // 已前移数十秒，用当前 target 判定会把跳显全部误拒（实测
                    // merged 快拖 err≈96s > errCap→hop-reject→不显示=拖拽无连续帧）
                    const qint64 seekTarget = m_chaseSeekTargetMs >= 0
                        ? m_chaseSeekTargetMs : target;
                    const qint64 err = seekTarget - relMs;
                    // 跳显误差闸随拖拽速度缩放：可接受的落后量 = 光标 100ms 墙钟
                    // 内走过的距离（v×100ms）。100× 快拖时落后 10s 时间轴 = 100ms
                    // 墙钟延迟，人眼不可察——这个文件可 seek 的 IDR 每 10s 一个
                    // （拼接视频 stss 稀疏），固定 2s 上限会把跳显全部拒绝，
                    // 退化成同关键帧 seek 风暴（实测 99 次/s，冻屏元凶）
                    const qint64 errCap = qMax<qint64>(
                        2000, qint64(qAbs(m_scrubVel) * 100.0));
                    const bool hopOk = sparseGop ? (err > 3000)
                                                 : (err > 300 && err <= errCap);
                    if (hopOk) {
                        if (relMs == m_positionMs.load()) {
                            // 同一关键帧重复命中（边界速度/悬停）：免 sws/emit/重绘，
                            // 仅刷新抑制锚点——每次重复跳显省 ~10ms 墙钟
                            m_hopTargetMs = seekTarget;
                            m_hopGapMs = err;
                        } else {
                            const qint64 tNow = m_monotonic.elapsed();
                            diagScrubDisplay("hop", relMs, seekTarget);
                            if (qEnvironmentVariableIsSet("LUMEN_DIAG_TIMING"))
                                audioDiag(QStringLiteral("  chase: seek=%1ms dec=%2")
                                          .arg(m_lastReseekWallMs > 0
                                               ? tNow - m_lastReseekWallMs : -1)
                                          .arg(dec == m_scDec ? "soft" : "hw"));
                            displayFrame(frame);
                            m_lastScrubDisplayElapsed = m_monotonic.elapsed();
                            m_hopTargetMs = seekTarget;   // 抑制锚点：目标越过 seekTarget+gap 才允许下一跳
                            m_hopGapMs = err;
                            // 快拖/超长 GOP（hopMode）：decode-through 追不上已跑远
                            // 的目标，继续解码纯属浪费——立即交外层 reseek 下一关键帧
                            if (hopMode)
                                shown = true;
                        }
                    } else {
                        // 拒绝也必须设抑制锚点：否则目标未越过下一关键帧前无限
                        // 重 seek 同一落点（日志实测同帧 10-20 次/ms 的 seek 循环）
                        m_hopTargetMs = seekTarget;
                        m_hopGapMs = err;
                        if (!sparseGop && err > 2000)
                            diagScrubDisplay("hop-reject", relMs, seekTarget);
                    }
                }
                if (dec == m_scDec)
                    chaseCachePush(relMs, frame);   // 软解帧入滚动缓存（refcount 零拷贝）
                if (qAbs(target - relMs) > SEEK_THRESHOLD_MS) {
                    reseek = true;            // 目标飞奔远离，解码追不上 → 交外层重 seek
                } else if (!shown && relMs > target + 2 * halfFrame) {
                    // 目标回退到解码位置之后：交外层后退重定位。
                    // 无上界窗口会显示超前帧（“快进感”，oscillate 回归复现）
                    reseek = true;
                } else if (!shown && relMs >= target - halfFrame) {
                    // 显示节拍闸（~30Hz 墙钟）：拖拽显示由解码事件驱动改为均匀
                    // 节拍驱动，消除解码吞吐波动/GOP 切换导致的卡顿与跳跃感
                    const qint64 now = m_monotonic.elapsed();
                    const qint64 gateWait = scrubGateIntervalMs()
                                            - (now - m_lastScrubDisplayElapsed);
                    if (dec == m_vdec && gateWait > 0) {
                        // 硬解帧不入缓存（持 GPU 表面）：持帧等到下一拍再判
                        QThread::msleep(gateWait);
                        target = m_scrubTargetMs.load();   // 等待期间目标可能已前移
                        if (target < 0)
                            break;
                    }
                    if (relMs >= target - halfFrame
                        && m_monotonic.elapsed() - m_lastScrubDisplayElapsed
                               >= scrubGateIntervalMs()) {
                        // 指哪播哪：只显示目标窗口帧；落后帧静默解码跳过（GOP 引用链
                        // 必须逐帧过，但不做 sws/emit——绝不播放中间帧，避免快进感）
                        diagScrubDisplay("window", relMs, target);
                        displayFrame(frame);      // 含硬解回传/sws/positionChanged/frameReady
                        m_lastScrubDisplayElapsed = m_monotonic.elapsed();
                        if (m_chaseStartElapsed >= 0) {
                            // 实测追赶墙钟耗时（EMA）：关键帧跳显模式的自适应依据。
                            // 仅在本文件 GOP 已知（解码器选择有据可依）后采样——
                            // GOP 未知时的硬解长追赶非最优路径，一次性慢样本会
                            // 把 sparseGop 锁死在布防态；长 GOP 只记录软解追赶耗时
                            const qint64 wall = m_monotonic.elapsed() - m_chaseStartElapsed;
                            if (m_gopLearnMs.load() > 0 && (dec == m_scDec || m_gopLearnMs.load() <= 4000))
                                m_lastChaseWallMs = m_lastChaseWallMs > 0
                                                    ? (m_lastChaseWallMs + wall) / 2 : wall;
                            m_chaseStartElapsed = -1;
                        }
                        shown = true;
                        if (m_astream >= 0 && m_scrubSnippetAudio) {
                            harvesting = true;    // 有音轨且慢拖：收割补全片段音频窗口
                            harvestBudget = 80;
                        }
                    } else if (dec == m_scDec && relMs >= target + SCRUB_LOOKAHEAD_MS) {
                        // 软解帧已入滚动缓存：闸未开期间超前解码量有界，
                        // 交外层——后续节拍由函数入口的缓存命中路径服务
                        break;
                    }
                    // 其余：闸未开静默续解（软解帧已缓存；硬解目标已跑过此帧则丢弃续追）
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

void FfmpegVideoEngine::setPlaybackDenoise(bool on, double strength)
{
    m_pbDenoiseOn.store(on);
    m_pbDenoiseStrength.store(strength);
}

// ---- AV 追踪探针（LUMENARC_AUDIO_TAP=目录；默认关闭零开销）----
void FfmpegVideoEngine::tapEnsure()
{
    if (m_tapPcm || m_tapLog)
        return;
    static const QByteArray dir = qgetenv("LUMENARC_AUDIO_TAP");  // 只查一次
    if (dir.isEmpty())
        return;
    m_tapPcm = new QFile(QString::fromUtf8(dir) + "/tap.pcm", this);
    m_tapLog = new QFile(QString::fromUtf8(dir) + "/tap.csv", this);
    if (!m_tapPcm->open(QIODevice::WriteOnly) || !m_tapLog->open(QIODevice::WriteOnly)) {
        delete m_tapPcm; delete m_tapLog;
        m_tapPcm = nullptr; m_tapLog = nullptr;
        return;
    }
    m_tapLog->write("kind,monoMs,arg0,arg1,arg2\n");
    m_tapLog->write(QStringLiteral("meta,rate=%1,ch=%2,fmt=%3\n")
                        .arg(m_outSampleRate).arg(m_outChannels)
                        .arg(int(m_outSampleFmt)).toUtf8());
}

void FfmpegVideoEngine::tapAudioWrite(qint64 payload, qint64 bytesFreeAfter)
{
    if (!m_tapLog)
        return;
    const qint64 mono = m_monotonic.isValid() ? m_monotonic.elapsed() : -1;
    // kind=A: arg0=本次写入字节 arg1=写后缓冲余量 arg2=累计写入
    m_tapLog->write(QStringLiteral("A,%1,%2,%3,%4\n")
                        .arg(mono).arg(payload).arg(bytesFreeAfter)
                        .arg(m_audioBytesWritten.load()).toUtf8());
}

void FfmpegVideoEngine::tapVideoDisplay(qint64 relMs)
{
    if (!m_tapLog)
        return;
    const qint64 mono = m_monotonic.isValid() ? m_monotonic.elapsed() : -1;
    // kind=V: arg0=显示帧的流内毫秒
    m_tapLog->write(QStringLiteral("V,%1,%2,0,0\n").arg(mono).arg(relMs).toUtf8());
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
        static bool prevScrubbing = false;
        if (prevScrubbing && !isScrubbing)
            chaseCacheClear();   // 退出拖拽：释放滚动缓存内存（沉淀 seek 已补全精确帧）
        if (!prevScrubbing && isScrubbing) {
            m_lastScrubDisplayElapsed = 0;   // 进入拖拽：首帧立即显示（节拍闸复位）
            m_hopTargetMs = -1;              // 跳显抑制锚点复位
            m_velLastTargetMs = -1;          // 速度估计器复位（片段音频闸）
            m_scrubVel = 0.0;
            m_scrubSnippetAudio = true;
            m_scrubGateExtraMs = 0;            // 节拍自适应增量复位
        }
        prevScrubbing = isScrubbing;
        bool isActive = (m_state.load() == static_cast<int>(PlaybackState::Playing))
                        || isScrubbing || m_stepOnce;

        // --- 处理命令 ---
        {
            QMutexLocker lock(&m_cmdMutex);
            if (m_pendingCmd == Command::None && !isActive) {
                // 空闲：预读缓存 → 睡眠
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
                handleSeek(m_positionMs.load());
                if (m_eof)
                    handleSeek(0);
                m_clockValid = false;
                m_state = static_cast<int>(PlaybackState::Playing);
                resumeAudio();
                emit stateChanged(PlaybackState::Playing);
                audioDiag(QStringLiteral("cmd Play: sink=%1 state=%2 err=%3")
                          .arg(m_sink != nullptr)
                          .arg(m_sink ? m_sink->state() : -1)
                          .arg(m_sink ? m_sink->error() : -1));
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
                m_stepOnce = false;   // 清空列表后不再显示加载中的首帧（回到空状态）
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

        // --- Scrub 追逐解码：围绕原子目标连续解码（VLC 路线：
        //     多线程软解 + 低固定开销 seek，主管线全分辨率，无代理） ---
        if (m_scrubMode.load()) {
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
    // v1.7.1：流分析限时 10s → 1s（大文件 open 的秒级开销；1s 足够
    // 拿到 DVR/监控素材的流信息，用户实测切换视频卡顿 10 秒级）
    av_dict_set(&opts, "analyzeduration", "1000000", 0);   // 1s
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
        // P-57 预览降清档：lowres 低分辨率解码（与硬解互斥，仅软解应用；
        // 仅预览降清，不碰源数据）
        if (m_previewLowres > 0)
            m_vdec->lowres = m_previewLowres;
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

    // --- 实测 PTS 节奏校准 fps：部分 DVR 导出文件容器元数据 fps 翻倍/错误
    // （如实际 15fps 报 30fps），avg_frame_rate 同样源自元数据不可靠。
    // 读前 48 个视频包的 PTS 求中位间隔得真实节奏，偏差 >4% 时以实测为准。 ---
    {
        QVector<qint64> ptsList;
        ptsList.reserve(48);
        AVPacket *probePkt = av_packet_alloc();
        while (ptsList.size() < 48 && av_read_frame(m_fmt, probePkt) >= 0) {
            if (probePkt->stream_index == m_vstream && probePkt->pts != AV_NOPTS_VALUE)
                ptsList.append(av_rescale_q(probePkt->pts, vs->time_base, AVRational{1, 1000}));
            av_packet_unref(probePkt);
        }
        av_packet_free(&probePkt);
        // 倒回文件起始（probe 消耗了 demux 位置），解码器同步清空。
        // seek 必须用流时基（见 scrubRedirectDemuxer 注释：-1/AV_TIME_BASE 超前回归）
        seekToRelMs(m_fmt, m_vstream, m_startPtsMs, 0, m_indexed);
        avcodec_flush_buffers(m_vdec);
        if (ptsList.size() >= 8) {
            std::sort(ptsList.begin(), ptsList.end());   // B 帧重排后 pts 非单调
            QVector<qint64> deltas;
            for (int i = 1; i < ptsList.size(); ++i)
                if (ptsList[i] - ptsList[i - 1] > 0)
                    deltas.append(ptsList[i] - ptsList[i - 1]);
            if (!deltas.isEmpty()) {
                std::sort(deltas.begin(), deltas.end());
                float measured = 1000.0f / static_cast<float>(deltas[deltas.size() / 2]);
                if (measured >= 3.0f && measured <= 240.0f
                    && m_fps > 0.0f && qAbs(measured - m_fps.load()) / m_fps.load() > 0.04f)
                    m_fps = measured;
            }
        }
    }

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
    audioDiag(QStringLiteral("openFile: %1 audio=%2")
              .arg(filePath)
              .arg(m_astream >= 0
                   ? QStringLiteral("%1 %2Hz %3ch")
                     .arg(avcodec_get_name(m_fmt->streams[m_astream]->codecpar->codec_id))
                     .arg(m_fmt->streams[m_astream]->codecpar->sample_rate)
                     .arg(m_fmt->streams[m_astream]->codecpar->ch_layout.nb_channels)
                   : QStringLiteral("none")));

    m_discardBeforeRelMs = -1;
    m_audioDiscardBeforeRelMs = -1;
    m_audioBaseRelMs = -1;
    if (m_pbDenoise)
        m_pbDenoise->reset();   // P-54b：开新文件/重播，DSP 状态重建
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
    chaseCacheClear();
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
    m_diagDecodedMs = 0;
    m_diagPeak = 0;
    m_diagOutFailLogged = false;
    if (m_sws) { sws_freeContext(m_sws); m_sws = nullptr; }
    m_swsW = m_swsH = 0; m_swsFmt = -1;
    if (m_vdec) { avcodec_free_context(&m_vdec); m_vdec = nullptr; }
    if (m_scDec) { avcodec_free_context(&m_scDec); m_scDec = nullptr; }
    m_chaseDec = nullptr;
    m_lastCatchupMs = 0;
    m_hopFirstFrame = false;
    m_hopTargetMs = -1;
    m_hopGapMs = 0;
    m_lastChaseWallMs = 0;
    m_chaseStartElapsed = -1;
    m_gopLearnMs = 0;
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

void FfmpegVideoEngine::handleSeek(qint64 timeMs)
{
    if (!m_fmt)
        return;

    timeMs = qBound<qint64>(0, timeMs, m_durationMs > 0 ? m_durationMs.load() : timeMs);

    // --- 拖拽模式（Scrub）：只写原子追逐目标，worker scrub 循环围绕它
    //     连续解码追赶——不再每个 mouseMove 都 seek+flush（"帧幻灯片"的根因） ---
    if (m_scrubMode) {
        m_scrubTargetMs = timeMs;
        m_cmdCond.wakeAll();
        return;
    }

    m_scrubTargetMs = -1;  // 非 scrub 模式，重置追逐目标

    // --- 一次性 seek（非拖拽：点击/播放启动） ---

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

// ---------------------------------------------------------------------------
// 精确 seek（流时基）
// ---------------------------------------------------------------------------
bool FfmpegVideoEngine::seekToRelMs(AVFormatContext *fmt, int vstream,
                                    qint64 startPtsMs, qint64 relMs,
                                    bool indexed)
{
    if (!fmt || vstream < 0 || vstream >= static_cast<int>(fmt->nb_streams))
        return false;
    AVStream *st = fmt->streams[vstream];
    const qint64 targetMs = startPtsMs + qMax<qint64>(0, relMs);
    // 目标 PTS（流时基）。毫秒→流时基用 av_rescale_q 精确换算。
    const int64_t pts = av_rescale_q(targetMs, AVRational{1, 1000}, st->time_base);
    if (avformat_seek_file(fmt, vstream, INT64_MIN, pts, pts,
                           AVSEEK_FLAG_BACKWARD) >= 0)
        return true;
    if (av_seek_frame(fmt, vstream, pts, AVSEEK_FLAG_BACKWARD) >= 0)
        return true;
    if (!indexed)
        return avformat_seek_file(fmt, vstream, INT64_MIN, pts, INT64_MAX,
                                  AVSEEK_FLAG_BYTE | AVSEEK_FLAG_BACKWARD) >= 0;
    return false;
}

void FfmpegVideoEngine::scrubRedirectDemuxer(qint64 timeMs)
{
    qint64 demuxTargetMs = timeMs;
    if (!m_indexed)
        demuxTargetMs = qMax<qint64>(0, timeMs - m_seekMarginMs);

    m_demuxTargetRelMs = demuxTargetMs;
    m_needMarginMeasure = !m_indexed;

    // ffmpeg 回归（N-125752 实测）：avformat_seek_file(fmt, -1, …) 用 AV_TIME_BASE
    // 换算会严重超前（merged 55min 视频 seek 1500s 落 3307s）；显式指定视频流 +
    // 流时基精确（±1 GOP ≈2.5s，chase 追赶即可）。引擎所有 seek 必须走此路径。
    const int ret = seekToRelMs(m_fmt, m_vstream, m_startPtsMs, demuxTargetMs,
                                m_indexed);
    (void)ret;
    if (qEnvironmentVariableIsSet("LUMEN_DIAG_TIMING")) {
        const qint64 tSeek = m_monotonic.elapsed() - m_lastReseekWallMs;
        audioDiag(QStringLiteral("  seekToRelMs took %1ms").arg(tSeek));
    }

    // 始终清空解码器（frame-threading 下"不清空前进"会导致乱序帧）
    avcodec_flush_buffers(m_vdec);
    if (m_scDec)
        avcodec_flush_buffers(m_scDec);
    if (m_adec)
        avcodec_flush_buffers(m_adec);
    if (qEnvironmentVariableIsSet("LUMEN_DIAG_TIMING")) {
        audioDiag(QStringLiteral("  flushes took %1ms")
                  .arg(m_monotonic.elapsed() - m_lastReseekWallMs));
    }
    // scrub 期间音频静音，跳过 WASAPI sink 重置（每次 seek 省 10-50ms 固定开销）
    if (m_sink && !m_scrubMode.load()) {
        m_sink->reset();
        m_sinkIo = m_sink->start();
    }
    m_audioBytesWritten = 0;
    m_audioBaseRelMs = -1;
    m_smoothAudioClock = -1;
    m_lastRawAudioClock = -1;
    if (m_pbDenoise)
        m_pbDenoise->reset();   // P-54b：seek 内容跳变，OLA/底噪状态重建

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

    seekToRelMs(m_pfFmt, m_pfVstream, m_startPtsMs, fromRelMs, m_indexed);
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
    qint64 relMs = ptsToRelMs(frame->best_effort_timestamp);

    tapEnsure();          // AV 追踪探针懒开（env 未设恒 null）
    tapVideoDisplay(relMs);

    // 有界化 frameReady 队列（VLC vout 式）：UI 未消费 ≥2 帧时丢帧而不是排队——
    // 防止 Qt 信号队列积压导致画面滞后/回放感（拖拽与低配机 4K 播放场景）
    if (m_framesInFlight.load() >= 2) {
        m_positionMs = relMs;
        emit positionChanged(relMs);
        if (m_scrubMode.load()) {
            ++m_diagScrubDrops;   // 临时诊断：UI 背压丢帧（画面冻结的嫌疑指标）
            m_scrubGateExtraMs = qMin(m_scrubGateExtraMs + 5, 20);   // 节拍自适应放宽
            diagScrubFlush();     // 画面全丢时 tick 不运行，由这里驱动周期落盘
        }
        return;
    }

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

    // 帧格式/尺寸变化时重建 swscale 上下文。scrub 跳显预览降采样输出
    // （2304×1296 全尺寸回传+sws 每帧 50-100ms → 拖拽帧率上不去；预览
    // 1280 宽够看清内容，松手后精确 seek 回到全分辨率）
    const int outW = m_scrubMode.load() ? qMin(srcFrame->width, 1280) : srcFrame->width;
    const int outH = qMax(1, outW * srcFrame->height / srcFrame->width);
    if (!m_sws || m_swsW != srcFrame->width || m_swsH != srcFrame->height
        || m_swsFmt != srcFrame->format || m_swsOutW != outW || m_swsOutH != outH) {
        if (m_sws)
            sws_freeContext(m_sws);
        m_sws = sws_getContext(srcFrame->width, srcFrame->height,
                               static_cast<AVPixelFormat>(srcFrame->format),
                               outW, outH, AV_PIX_FMT_RGB24,
                               SWS_BILINEAR, nullptr, nullptr, nullptr);
        if (!m_sws) {
            if (swFrame)
                av_frame_free(&swFrame);
            return;
        }
        m_swsW = srcFrame->width;
        m_swsH = srcFrame->height;
        m_swsFmt = srcFrame->format;
        m_swsOutW = outW;
        m_swsOutH = outH;
    }

    QImage img(outW, outH, QImage::Format_RGB888);
    uint8_t *dst[4] = { img.bits(), nullptr, nullptr, nullptr };
    int dstLinesize[4] = { static_cast<int>(img.bytesPerLine()), 0, 0, 0 };
    sws_scale(m_sws, srcFrame->data, srcFrame->linesize, 0, srcFrame->height,
              dst, dstLinesize);

    if (swFrame)
        av_frame_free(&swFrame);

    m_positionMs = relMs;
    ++m_framesInFlight;   // UI 侧 VideoWidget::onFrameReady 调 ackFrame() 归还
    emit positionChanged(relMs);
    emit frameReady(img);
    if (m_scrubMode.load()) {
        if (m_scrubGateExtraMs > 0)
            --m_scrubGateExtraMs;   // 成功显示：节拍增量逐帧衰减回 25ms
        diagScrubTick();   // 临时诊断：scrub 显示计数 + 最大间隔
    }
}
