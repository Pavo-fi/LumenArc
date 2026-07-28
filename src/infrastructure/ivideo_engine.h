/**
 * @file ivideo_engine.h
 * @brief 视频播放引擎抽象接口，定义播放/暂停/停止/跳转等操作
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include <QObject>
#include <QImage>
#include <QSize>

/// GPU 零拷贝帧信息（实验，v1.6）：引擎已在本机 GPU 完成解码+色彩转换，
/// UI 侧直接导入共享纹理采样。sharedHandle 为同进程跨设备句柄（Win/D3D11
/// keyed-mutex 纹理）；slot 为环形槽位（0..1）。软解/非 Windows 引擎不产生。
struct GpuFrameInfo {
    quint64 sharedHandle = 0;
    int slot = 0;
    QSize size;
};

enum class PlaybackState
{
    Idle,
    Loading,
    Playing,
    Paused,
    Stopped,
    Ended
};

/**
 * @brief Abstract interface for video playback engines.
 *
 * Decouples the UI from concrete implementations (libVLC, FFmpeg, etc.).
 */
class IVideoEngine : public QObject
{
    Q_OBJECT

public:
    explicit IVideoEngine(QObject *parent = nullptr) : QObject(parent) {}
    virtual ~IVideoEngine() = default;

    virtual bool load(const QString &filePath) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(qint64 timeMs) = 0;

    virtual qint64 position() const = 0;
    virtual qint64 duration() const = 0;
    virtual PlaybackState state() const = 0;

    virtual int videoWidth() const = 0;
    virtual int videoHeight() const = 0;

    virtual float fps() const = 0;
    virtual int volume() const = 0;
    virtual void setVolume(int vol) = 0;
    virtual void setRate(float rate) = 0;
    virtual float rate() const = 0;

    /// 倍速时是否支持音频输出（不支持的引擎在 rate!=1.0 时应静音并在 UI 明示）
    virtual bool supportsRateAudio() const { return true; }

    /// 设置拖拽模式：拖拽中 seek 走追逐解码；松手走一次性精确 seek
    virtual void setScrubMode(bool on) { Q_UNUSED(on) }
    /// 拖拽追逐目标（原子写入，免锁免节流）：拖拽期间 UI 高频调用；
    /// 引擎 scrub 循环围绕该目标连续解码/demux 级追赶，而非每个位置 seek+flush。
    virtual void setScrubTarget(qint64 timeMs) { Q_UNUSED(timeMs) }
    /// UI 已消费一帧（有界化 frameReady 队列：引擎在积压时丢帧而不是排队，VLC 式）
    virtual void ackFrame() {}
    /// 当前使用的硬解适配器名称（软解返回空串）
    virtual QString hardwareAdapterName() const { return QString(); }
    /// 请求 GPU 零拷贝帧路径（实验，v1.6）。返回 false=不支持（VLC 引擎/非 Windows）。
    /// 真实状态经 gpuFramesActiveChanged 信号异步确认（工作线程内评估）。
    virtual bool setGpuFramesEnabled(bool on) { Q_UNUSED(on) return false; }

signals:
    void frameReady(const QImage &image);
    /// GPU 零拷贝帧（实验）：激活时替代大部分 frameReady；UI 消费后须 ackFrame()
    void frameTextureReady(const GpuFrameInfo &info);
    /// GPU 帧路径实际生效状态（工作线程评估后发出）
    void gpuFramesActiveChanged(bool active);
    void positionChanged(qint64 timeMs);
    void durationChanged(qint64 durationMs);
    void stateChanged(PlaybackState state);
    void videoSizeChanged(int w, int h);
};

Q_DECLARE_METATYPE(GpuFrameInfo)
