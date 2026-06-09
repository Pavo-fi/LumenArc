/**
 * @file vlc_video_engine.h
 * @brief libVLC 视频引擎实现：I420 回调 + 异步 RGB 渲染
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include "ivideo_engine.h"
#include <QMutex>
#include <QThread>
#include <QByteArray>
#include <QTimer>

struct libvlc_instance_t;
struct libvlc_media_player_t;
struct libvlc_media_t;

class RenderWorker;

/**
 * @brief libVLC-based implementation of IVideoEngine with asynchronous YUV->RGB conversion.
 */
class VlcVideoEngine : public IVideoEngine
{
    Q_OBJECT

public:
    explicit VlcVideoEngine(QObject *parent = nullptr);
    ~VlcVideoEngine();

    /// @brief 加载视频文件并初始化 libVLC 上下文
    bool load(const QString &filePath) override;
    /// @brief 开始播放
    void play() override;
    /// @brief 暂停播放
    void pause() override;
    /// @brief 停止播放并释放媒体
    void stop() override;
    /// @brief 跳转到指定时间（毫秒）
    void seek(qint64 timeMs) override;

    /// @brief 返回当前播放位置（毫秒）
    qint64 position() const override;
    /// @brief 返回视频总时长（毫秒）
    qint64 duration() const override;
    /// @brief 返回当前播放状态
    PlaybackState state() const override;

    /// @brief 返回视频原始宽度（像素）
    int videoWidth() const override;
    /// @brief 返回视频原始高度（像素）
    int videoHeight() const override;

    /// @brief 返回视频帧率
    float fps() const override;
    /// @brief 返回当前音量
    int volume() const override;
    /// @brief 设置音量（0-100）
    void setVolume(int vol) override;
    /// @brief 设置播放速率
    void setRate(float rate) override;
    /// @brief 返回当前播放速率
    float rate() const override;

private slots:
    void onPollPosition();

signals:
    /// Internal signal used to ferry raw I420 data to the render thread.
    void frameDataReady(QByteArray data, int width, int height);

private:
    static void *lockCallback(void *opaque, void **planes);
    static void unlockCallback(void *opaque, void *picture, void *const *planes);
    static void displayCallback(void *opaque, void *picture);
    static unsigned formatCallback(void **opaque, char *chroma, unsigned *width, unsigned *height,
                                   unsigned *pitches, unsigned *lines);
    static void cleanupCallback(void *opaque);

    libvlc_instance_t *m_vlcInstance = nullptr;
    libvlc_media_player_t *m_mediaPlayer = nullptr;
    libvlc_media_t *m_media = nullptr;

    QTimer *m_pollTimer = nullptr;
    mutable QMutex m_mutex;

    int m_videoWidth = 0;
    int m_videoHeight = 0;
    qint64 m_lastReportedDuration = 0;

    QByteArray m_videoBuffer;   // Buffer allocated for VLC I420 lock callback

    QThread *m_renderThread = nullptr;
    RenderWorker *m_renderWorker = nullptr;
};
