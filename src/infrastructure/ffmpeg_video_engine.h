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
struct AVPacket;
struct AVFrame;

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

private:
    enum class Command { None, Play, Pause, Stop, Seek };

    void workerMain();                        // 工作线程主循环
    bool openFile(const QString &filePath);   // 工作线程内调用
    void closeFile();                         // 工作线程内调用
    void handleSeek(qint64 timeMs);           // 工作线程内调用
    void processVideoPacket(AVPacket *pkt);   // 送包 + 排干解码器
    bool drainDecoder();                      // 返回是否取到帧
    void displayFrame(AVFrame *frame);
    void paceUntil(qint64 ptsRelMs);          // 按时钟节奏等待（可被命令打断）
    void postCommand(Command cmd, qint64 arg = 0);
    bool hasPendingCommand();
    qint64 ptsToRelMs(int64_t pts) const;

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
};
