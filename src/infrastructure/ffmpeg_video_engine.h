/**
 * @file ffmpeg_video_engine.h
 * @brief FFmpeg 自研播放引擎：demux/decode 线程、精确 seek、系统时钟节奏
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-07-25
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计要点：
 * - 单一工作线程完成 demux+decode+pace，命令（play/pause/stop/seek）经锁队列下发；
 * - position 一律为相对毫秒（PTS - start_time），UI 语义与 VLC 引擎一致；
 * - seek 后从关键帧解码并丢弃非目标帧，保证精确落点、无"弹回"；
 * - 暂停态 seek 通过 m_stepOnce 单帧显示刷新画面，无需 VLC 式 play/pause hack；
 * - 帧生产受时钟节奏自然限流，frameReady 跨线程 QueuedConnection 交付。
 */
#pragma once

#include "ivideo_engine.h"
#include <QThread>
#include <QMutex>
#include <QWaitCondition>
#include <QElapsedTimer>
#include <atomic>

struct AVFormatContext;
struct AVCodecContext;
struct SwsContext;
struct SwrContext;
struct AVPacket;
struct AVFrame;
class QAudioSink;
class QIODevice;

class FfmpegVideoEngine : public IVideoEngine
{
    Q_OBJECT

public:
    explicit FfmpegVideoEngine(QObject *parent = nullptr);
    ~FfmpegVideoEngine() override;

    bool load(const QString &filePath) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(qint64 timeMs) override;

    qint64 position() const override;
    qint64 duration() const override;
    PlaybackState state() const override;

    int videoWidth() const override;
    int videoHeight() const override;

    float fps() const override;
    int volume() const override;
    void setVolume(int vol) override;
    void setRate(float rate) override;
    float rate() const override;
    bool supportsRateAudio() const override { return false; } // 一期：倍速静音

private:
    enum class Command { None, Play, Pause, Stop, Seek };

    void workerMain();                        // 工作线程主循环
    bool openFile(const QString &filePath);   // 工作线程内调用
    void closeFile();                         // 工作线程内调用
    void handleSeek(qint64 timeMs);           // 工作线程内调用
    void processVideoPacket(AVPacket *pkt);   // 送包 + 排干解码器
    void processAudioPacket(AVPacket *pkt);   // 音频解码 → 重采样 → 环形缓冲
    bool ensureAudioOutput();                 // 惰性创建 QAudioSink（工作线程内）
    void suspendAudio();
    void resumeAudio();
    bool drainDecoder();                      // 返回是否取到帧
    void displayFrame(AVFrame *frame);
    bool paceUntil(qint64 ptsRelMs);          // true=显示该帧；false=过晚丢弃（倍速追帧）
    void postCommand(Command cmd, qint64 arg = 0);
    bool hasPendingCommand();
    qint64 ptsToRelMs(int64_t pts) const;
    qint64 ptsToRelMsA(int64_t pts) const;    // 音频流时基换算

public:
    /// 诊断/测试用：本次 seek 以来写入音频缓冲的字节数
    qint64 audioBytesWritten() const { return m_audioBytesWritten.load(); }
    /// 诊断/测试用：音频主时钟（相对毫秒）
    qint64 audioClockMs() const;

private:

    // --- 控制面（UI 线程读写，经锁/原子保护） ---
    mutable QMutex m_cmdMutex;
    QWaitCondition m_cmdCond;
    Command m_pendingCmd = Command::None;
    qint64 m_cmdArg = 0;
    std::atomic<bool> m_quit{false};
    std::atomic<int> m_state{static_cast<int>(PlaybackState::Idle)};
    std::atomic<qint64> m_positionMs{0};
    std::atomic<qint64> m_durationMs{0};
    std::atomic<int> m_width{0};
    std::atomic<int> m_height{0};
    std::atomic<float> m_fps{0.0f};
    std::atomic<float> m_rate{1.0f};
    std::atomic<int> m_volume{100};

    // --- 播放面（仅工作线程访问） ---
    QThread *m_thread = nullptr;
    QString m_pendingPath;          // load() 传入，openFile 在工作线程使用
    AVFormatContext *m_fmt = nullptr;
    AVCodecContext *m_vdec = nullptr;
    SwsContext *m_sws = nullptr;
    int m_swsW = 0, m_swsH = 0, m_swsFmt = -1;
    int m_vstream = -1;
    qint64 m_startPtsMs = 0;        // 流起始 PTS（绝对），用于相对时间换算
    bool m_indexed = true;          // 容器是否有 seek 索引（PS/TS 无索引）
    qint64 m_discardBeforeRelMs = -1; // seek 后丢弃早于该相对 PTS 的帧
    bool m_stepOnce = false;        // 暂停态 seek 后显示一帧
    bool m_clockValid = false;
    qint64 m_clockBasePtsMs = 0;    // 时钟基准（相对毫秒）
    qint64 m_clockBaseElapsed = 0;  // 时钟基准对应的单调时钟
    QElapsedTimer m_monotonic;      // 工作线程持久单调时钟
    bool m_eof = false;
    bool m_drainedAtEof = false;    // EOF 时解码器是否已冲空（frame threading 滞留帧）

    // --- 音频面（仅工作线程访问，计数器为原子供诊断读取） ---
    AVCodecContext *m_adec = nullptr;
    SwrContext *m_swr = nullptr;
    QAudioSink *m_sink = nullptr;
    QIODevice *m_sinkIo = nullptr;  // 推模式输出设备
    int m_astream = -1;
    int m_outSampleRate = 0;
    int m_outChannels = 0;
    bool m_audioMaster = false;     // 有可用音轨且 rate==1.0 时音频为主时钟
    std::atomic<qint64> m_audioBytesWritten{0};
    qint64 m_audioBaseRelMs = -1;   // 首个写入样本的 PTS（相对毫秒），-1=未锚定
    qint64 m_audioDiscardBeforeRelMs = -1; // seek 后丢弃早于该相对 PTS 的音频
    std::atomic<bool> m_audioSinkOk{false};
    qint64 m_lastAudioPlayedMs = -1;      // 上次观测到的音频时钟值
    qint64 m_lastAudioProgressElapsed = 0;// 音频时钟上次前进对应的单调时钟
};
