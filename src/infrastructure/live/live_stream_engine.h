/**
 * @file live_stream_engine.h
 * @brief 网络直播引擎：RTSP 等实时流的解码与送帧（无时长/无 seek，断线自愈）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-11
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计要点（与文件播放引擎 FfmpegVideoEngine 明确分离，R9/R10）：
 * - 直播没有时长、没有可寻址时间轴：duration 恒 0、seek 为无操作；
 * - 唯一时钟是墙钟（起播以来毫秒），不做 PTS 节奏、不做预读/scrub 缓存；
 * - 读流阻塞在 av_read_frame，用 AVIOInterruptCB + 原子标志实现秒级可中断；
 * - 网络断开/超时 → 按指数退避自动重连，重连期间 UI 收到 reconnecting 信号；
 * - 送帧有界（在飞 ≥2 即丢帧），直播宁可丢帧不排队，避免延时累积；
 * - 不含音频（监控复核场景以画面为准），supportsRateAudio=false。
 *
 * 调用契约（评审 N4）：
 *   每次更换地址必须调 load()；load() 会自增内部“代际号”，worker 在旧代际上
 *   不会重连旧地址，而是交回外层重新读取 m_url。因此在会话活跃时直接
 *    load()+play() 也是安全的（不会出现“用旧 URL 重连”的错乱）。
 *   停止会话用 stop()（非阻塞）或 unload()（阻塞回收线程，用于析构）。
 */
#pragma once

#include "infrastructure/ivideo_engine.h"
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

class LiveStreamEngine : public IVideoEngine
{
    Q_OBJECT

public:
    explicit LiveStreamEngine(QObject *parent = nullptr);
    ~LiveStreamEngine() override;

    bool load(const QString &url) override;
    void play() override;
    void pause() override;
    void stop() override;
    void unload() override;
    void seek(qint64 timeMs) override;      // 直播：无操作

    qint64 position() const override;       // 起播以来墙钟毫秒
    qint64 duration() const override { return 0; }   // 直播恒 0
    PlaybackState state() const override;

    int videoWidth() const override;
    int videoHeight() const override;
    float fps() const override;
    int volume() const override;
    void setVolume(int vol) override;
    void setRate(float rate) override;
    float rate() const override { return 1.0f; }
    bool supportsRateAudio() const override { return false; }
    void ackFrame() override;               // 越界安全的有界配额归还

    /// 传输协议：true=TCP（现场首选，穿透性好），false=UDP。load 前设置。
    void setTransportTcp(bool tcp) { m_transportTcp = tcp; }

signals:
    /// 连接成功、开始出图
    void streamStarted();
    /// 连接/解码失败（用户可见；message 为可读原因）
    void streamError(const QString &message);
    /// 断线重连（attempt 从 1 起）
    void reconnecting(int attempt);

private:
    void workerMain();
    bool openStream(const QString &url);
    void closeStream();
    void readLoop();
    bool displayFrame(AVFrame *frame);
    void ensureThread();
    void wakeAll();
    void sleepInterruptible(int ms);
    void setState(PlaybackState s) { m_state.store(static_cast<int>(s)); }
    bool isAbort() const
    {
        return m_abort.load(std::memory_order_relaxed)
            || m_quit.load(std::memory_order_relaxed);
    }
    static int interruptCb(void *opaque);

    // --- 控制面（UI 线程写，worker 读；m_mutex 保护 m_url）---
    mutable QMutex m_mutex;
    QWaitCondition m_cond;
    std::atomic<bool> m_quit{false};
    std::atomic<bool> m_abort{false};
    std::atomic<bool> m_started{false};
    std::atomic<bool> m_paused{false};
    std::atomic<bool> m_waitKeyframe{false};
    QString m_url;
    /// 地址代际号（评审 N4）：load() 自增，worker 只在本代际内重连；
    /// 代际变化即交回外层重读 m_url，避免用旧地址无限重连。
    std::atomic<quint64> m_generation{0};
    bool m_transportTcp = true;

    // --- 状态面（跨线程读写，原子）---
    std::atomic<int> m_state{static_cast<int>(PlaybackState::Idle)};
    std::atomic<int> m_width{0};
    std::atomic<int> m_height{0};
    std::atomic<float> m_fps{0.0f};
    std::atomic<int> m_volume{100};
    std::atomic<int> m_framesInFlight{0};
    std::atomic<qint64> m_positionMs{0};

    // --- 播放面（仅 worker 线程访问）---
    QThread *m_thread = nullptr;
    AVFormatContext *m_fmt = nullptr;
    AVCodecContext *m_vdec = nullptr;
    SwsContext *m_sws = nullptr;
    int m_vstream = -1;
    QElapsedTimer m_sessionClock;

    static constexpr int kMaxFramesInFlight = 2;
};
