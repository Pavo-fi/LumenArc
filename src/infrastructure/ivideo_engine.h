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

    /// 设置拖拽预览代理源（全 I 帧低分代理，帧号与原片 1:1）。
    /// 引擎在暂停/拖拽 seek 时用代理快速出精确帧；不支持的引擎忽略。
    virtual void setProxySource(const QString &proxyPath) { Q_UNUSED(proxyPath) }
    /// 代理是否已就绪（UI 用于判断拖拽 seek 是否无需节流）
    virtual bool proxyActive() const { return false; }
    /// 设置拖拽模式：拖拽中 seek 走 demuxer 重定向 + 连续解码；松手后走一次性精确 seek
    virtual void setScrubMode(bool on) { Q_UNUSED(on) }

signals:
    void frameReady(const QImage &image);
    void positionChanged(qint64 timeMs);
    void durationChanged(qint64 durationMs);
    void stateChanged(PlaybackState state);
    void videoSizeChanged(int w, int h);
};
