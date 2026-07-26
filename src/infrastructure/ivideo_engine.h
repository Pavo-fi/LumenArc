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

signals:
    void frameReady(const QImage &image);
    void positionChanged(qint64 timeMs);
    void durationChanged(qint64 durationMs);
    void stateChanged(PlaybackState state);
    void videoSizeChanged(int w, int h);
};
