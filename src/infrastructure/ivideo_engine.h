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
    /// 彻底卸载当前视频（停线程 + 释放文件上下文 + duration 归零）。
    /// 清空列表等“回到初始状态”场景必须用它——仅 stop() 仍可被快捷键继续播放。
    virtual void unload() = 0;
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
    /// 预览降清档（lowres 低分辨率解码，P-57 多机播放性能治理用；
    /// 仅预览降清不碰源数据；下次 load 生效，默认无操作）
    virtual void setPreviewLowres(int level) { Q_UNUSED(level) }
    /// 当前预览降清档位（0=全分辨率）
    virtual int previewLowres() const { return 0; }
    /// UI 已消费一帧（有界化 frameReady 队列：引擎在积压时丢帧而不是排队，VLC 式）
    virtual void ackFrame() {}
    /// 当前使用的硬解适配器名称（软解返回空串）
    virtual QString hardwareAdapterName() const { return QString(); }
    /// 实测 GOP 长度（毫秒，reseek 落点间距学习；0=未知）。
    /// P-57 多机纠偏阈值 GOP 联动用（方案 §8 R-2：长 GOP seek 代价高，放宽阈值）
    virtual qint64 learnedGopMs() const { return 0; }

signals:
    void frameReady(const QImage &image);
    void positionChanged(qint64 timeMs);
    void durationChanged(qint64 durationMs);
    void stateChanged(PlaybackState state);
    void videoSizeChanged(int w, int h);
};
