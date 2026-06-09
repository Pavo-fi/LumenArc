/**
 * @file vlc_video_engine.cpp
 * @brief libVLC 视频引擎实现：加载/播放/暂停/跳转/帧回调
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "vlc_video_engine.h"
#include <vlc/vlc.h>
#include <QDebug>
#include <QCoreApplication>
#include <QDir>
#include <cstring>

// =============================================================================
// RenderWorker: lives on a dedicated thread to convert I420 -> RGB off the
// VLC internal decoding thread.
// =============================================================================
class RenderWorker : public QObject
{
    Q_OBJECT
public:
    static QImage convertI420ToRGB(const uint8_t *data, int w, int h);

public slots:
    void process(QByteArray data, int w, int h);

signals:
    void converted(QImage image);
};

static inline int clamp255(int v)
{
    return v < 0 ? 0 : (v > 255 ? 255 : v);
}

QImage RenderWorker::convertI420ToRGB(const uint8_t *data, int w, int h)
{
    QImage colorImage(w, h, QImage::Format_RGB888);
    uchar *dst = colorImage.bits();
    int dstPitch = colorImage.bytesPerLine();
    int pitch = w;
    int ySize = w * h;
    const uint8_t *yPlane = data;
    const uint8_t *uPlane = data + ySize;
    const uint8_t *vPlane = data + ySize + (w / 2) * (h / 2);

    for (int y = 0; y < h; ++y) {
        uchar *dstRow = dst + y * dstPitch;
        const uint8_t *yRow = yPlane + y * pitch;
        const uint8_t *uRow = uPlane + (y / 2) * (pitch / 2);
        const uint8_t *vRow = vPlane + (y / 2) * (pitch / 2);

        for (int x = 0; x < w; ++x) {
            int yy = yRow[x] - 16;
            int uu = uRow[x / 2] - 128;
            int vv = vRow[x / 2] - 128;

            int r = (298 * yy + 409 * vv + 128) >> 8;
            int g = (298 * yy - 100 * uu - 208 * vv + 128) >> 8;
            int b = (298 * yy + 516 * uu + 128) >> 8;

            *dstRow++ = static_cast<uchar>(clamp255(r));
            *dstRow++ = static_cast<uchar>(clamp255(g));
            *dstRow++ = static_cast<uchar>(clamp255(b));
        }
    }
    return colorImage;
}

void RenderWorker::process(QByteArray data, int w, int h)
{
    QImage img = convertI420ToRGB(reinterpret_cast<const uint8_t *>(data.constData()), w, h);
    emit converted(img);
}

// --- Static callbacks ---

void *VlcVideoEngine::lockCallback(void *opaque, void **planes)
{
    auto *engine = static_cast<VlcVideoEngine *>(opaque);
    if (!engine || !planes)
        return nullptr;

    QMutexLocker lock(&engine->m_mutex);
    int w = engine->m_videoWidth;
    int h = engine->m_videoHeight;
    if (w <= 0 || h <= 0)
        return nullptr;

    int ySize = w * h;
    int uvSize = (w / 2) * (h / 2);
    int totalSize = ySize + 2 * uvSize;

    if (engine->m_videoBuffer.size() < totalSize)
        engine->m_videoBuffer.resize(totalSize);

    uint8_t *buf = reinterpret_cast<uint8_t *>(engine->m_videoBuffer.data());
    planes[0] = buf;
    planes[1] = buf + ySize;
    planes[2] = buf + ySize + uvSize;

    return buf;
}

void VlcVideoEngine::unlockCallback(void *opaque, void *picture, void *const *planes)
{
    Q_UNUSED(picture)
    auto *engine = static_cast<VlcVideoEngine *>(opaque);
    if (!engine || !planes || !planes[0])
        return;

    int w, h;
    {
        QMutexLocker lock(&engine->m_mutex);
        w = engine->m_videoWidth;
        h = engine->m_videoHeight;
    }
    if (w <= 0 || h <= 0)
        return;

    // Deep-copy the I420 data so VLC can reuse its buffer immediately.
    int ySize = w * h;
    int uvSize = (w / 2) * (h / 2);
    int totalSize = ySize + 2 * uvSize;

    QByteArray buffer(totalSize, Qt::Uninitialized);
    memcpy(buffer.data(), planes[0], totalSize);

    // Emit across threads (QueuedConnection to RenderWorker).
    emit engine->frameDataReady(std::move(buffer), w, h);
}

void VlcVideoEngine::displayCallback(void *opaque, void *picture)
{
    Q_UNUSED(opaque)
    Q_UNUSED(picture)
}

unsigned VlcVideoEngine::formatCallback(void **opaque, char *chroma, unsigned *width, unsigned *height,
                                        unsigned *pitches, unsigned *lines)
{
    auto *engine = static_cast<VlcVideoEngine *>(*opaque);
    if (!engine)
        return 0;

    memcpy(chroma, "I420", 4);

    unsigned w = *width;
    unsigned h = *height;

    pitches[0] = w;
    pitches[1] = w / 2;
    pitches[2] = w / 2;
    pitches[3] = 0;

    lines[0] = h;
    lines[1] = h / 2;
    lines[2] = h / 2;
    lines[3] = 0;

    {
        QMutexLocker lock(&engine->m_mutex);
        engine->m_videoWidth = static_cast<int>(w);
        engine->m_videoHeight = static_cast<int>(h);
    }

    emit engine->videoSizeChanged(engine->m_videoWidth, engine->m_videoHeight);
    return 1;
}

void VlcVideoEngine::cleanupCallback(void *opaque)
{
    Q_UNUSED(opaque)
}

// --- Member implementation ---

VlcVideoEngine::VlcVideoEngine(QObject *parent)
    : IVideoEngine(parent)
{
    const char *args[] = {
        "--no-video-title-show",
        "--no-osd"
    };
    m_vlcInstance = libvlc_new(sizeof(args) / sizeof(args[0]), args);
    if (!m_vlcInstance) {
        qWarning() << "Failed to create libvlc instance.";
    }

    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(33);
    connect(m_pollTimer, &QTimer::timeout, this, &VlcVideoEngine::onPollPosition);

    // Set up asynchronous render thread
    m_renderThread = new QThread(this);
    m_renderWorker = new RenderWorker();
    m_renderWorker->moveToThread(m_renderThread);
    connect(this, &VlcVideoEngine::frameDataReady,
            m_renderWorker, &RenderWorker::process, Qt::QueuedConnection);
    connect(m_renderWorker, &RenderWorker::converted,
            this, &IVideoEngine::frameReady, Qt::QueuedConnection);
    m_renderThread->start();
}

VlcVideoEngine::~VlcVideoEngine()
{
    stop();

    // Stop render thread BEFORE releasing VLC resources.
    // VLC callbacks may still reference engine data (m_videoBuffer, etc.).
    // Quitting the render thread first ensures no in-flight cross-thread
    // frameDataReady signals are being processed.
    if (m_renderThread) {
        m_renderThread->quit();
        m_renderThread->wait(2000);
        if (m_renderWorker) {
            m_renderWorker->moveToThread(QThread::currentThread());
            delete m_renderWorker;
            m_renderWorker = nullptr;
        }
    }

    if (m_mediaPlayer) {
        libvlc_media_player_release(m_mediaPlayer);
        m_mediaPlayer = nullptr;
    }
    if (m_vlcInstance) {
        libvlc_release(m_vlcInstance);
        m_vlcInstance = nullptr;
    }
}

/**
 * @brief 加载视频文件：创建VLC媒体/播放器/设置回调/启动渲染线程
 */
bool VlcVideoEngine::load(const QString &filePath)
{
    if (!m_vlcInstance) {
        qWarning() << "VLC instance not initialized";
        return false;
    }

    stop();

    if (m_mediaPlayer) {
        libvlc_media_player_release(m_mediaPlayer);
        m_mediaPlayer = nullptr;
    }

    QString nativePath = QDir::toNativeSeparators(filePath);
    m_media = libvlc_media_new_path(m_vlcInstance, nativePath.toUtf8().constData());
    if (!m_media) {
        qWarning() << "Failed to open media:" << nativePath;
        return false;
    }

    m_mediaPlayer = libvlc_media_player_new_from_media(m_media);
    libvlc_media_release(m_media);
    m_media = nullptr;

    if (!m_mediaPlayer) {
        qWarning() << "Failed to create media player";
        return false;
    }

    m_lastReportedDuration = 0;
    emit durationChanged(duration());

    // VLC may report 0 duration immediately after opening; poll for real value
    auto pollDuration = [this]() {
        qint64 dur = duration();
        if (dur > 0 && dur != m_lastReportedDuration) {
            m_lastReportedDuration = dur;
            emit durationChanged(dur);
        }
    };
    QTimer::singleShot(200, this, pollDuration);
    QTimer::singleShot(800, this, pollDuration);

    libvlc_video_set_callbacks(m_mediaPlayer,
                               &lockCallback,
                               &unlockCallback,
                               &displayCallback,
                               this);
    libvlc_video_set_format_callbacks(m_mediaPlayer,
                                      &formatCallback,
                                      &cleanupCallback);

    emit durationChanged(duration());
    return true;
}

/// @brief 开始播放：启动VLC播放器和轮询定时器
void VlcVideoEngine::play()
{
    if (!m_mediaPlayer)
        return;
    // Playback state guard: ignore if already playing
    PlaybackState s = state();
    if (s == PlaybackState::Playing)
        return;
    libvlc_media_player_play(m_mediaPlayer);
    m_pollTimer->start();
    emit stateChanged(PlaybackState::Playing);
}

/// @brief 暂停播放：调用VLC暂停，不立即停止轮询（异步过渡）
void VlcVideoEngine::pause()
{
    if (!m_mediaPlayer)
        return;
    PlaybackState s = state();
    if (s == PlaybackState::Paused)
        return;
    libvlc_media_player_pause(m_mediaPlayer);
    // Don't stop timer here — let onPollPosition detect the actual VLC state
    // so video and chart stay in sync during the async pause transition.
    emit stateChanged(PlaybackState::Paused);
}

void VlcVideoEngine::stop()
{
    if (!m_mediaPlayer)
        return;
    // Playback state guard: ignore if already stopped or idle
    PlaybackState s = state();
    if (s == PlaybackState::Stopped || s == PlaybackState::Idle)
        return;
    libvlc_media_player_stop(m_mediaPlayer);
    m_pollTimer->stop();
    emit stateChanged(PlaybackState::Stopped);
}

/**
 * @brief 跳转到指定时间：暂停态需要play→pause周期刷新画面
 */
void VlcVideoEngine::seek(qint64 timeMs)
{
    if (!m_mediaPlayer)
        return;

    PlaybackState s = state();

    // Ended/Stopped/Idle: VLC refuses set_time() in these states.
    // Stop first, set position, then restart playback.
    if (s == PlaybackState::Ended || s == PlaybackState::Stopped
        || s == PlaybackState::Idle) {
        libvlc_media_player_stop(m_mediaPlayer);
        libvlc_media_player_set_time(m_mediaPlayer,
                                     static_cast<libvlc_time_t>(timeMs));
        libvlc_media_player_play(m_mediaPlayer);
        m_pollTimer->start();
        emit stateChanged(PlaybackState::Playing);
        return;
    }

    libvlc_media_player_set_time(m_mediaPlayer,
                                 static_cast<libvlc_time_t>(timeMs));

    // Paused: VLC needs a brief play→pause cycle to display the seeked frame.
    if (s == PlaybackState::Paused) {
        libvlc_media_player_play(m_mediaPlayer);
        QTimer::singleShot(150, this, [this]() {
            if (m_mediaPlayer && state() == PlaybackState::Playing) {
                libvlc_media_player_pause(m_mediaPlayer);
                emit stateChanged(PlaybackState::Paused);
            }
        });
    }
}

qint64 VlcVideoEngine::position() const
{
    if (!m_mediaPlayer)
        return 0;
    return static_cast<qint64>(libvlc_media_player_get_time(m_mediaPlayer));
}

qint64 VlcVideoEngine::duration() const
{
    if (!m_mediaPlayer)
        return 0;
    return static_cast<qint64>(libvlc_media_player_get_length(m_mediaPlayer));
}

PlaybackState VlcVideoEngine::state() const
{
    if (!m_mediaPlayer)
        return PlaybackState::Idle;

    libvlc_state_t s = libvlc_media_player_get_state(m_mediaPlayer);
    switch (s) {
    case libvlc_Playing:
        return PlaybackState::Playing;
    case libvlc_Paused:
        return PlaybackState::Paused;
    case libvlc_Stopped:
        return PlaybackState::Stopped;
    case libvlc_Ended:
        return PlaybackState::Ended;
    case libvlc_NothingSpecial:
        return PlaybackState::Idle;
    default:
        return PlaybackState::Loading;
    }
}

int VlcVideoEngine::videoWidth() const
{
    QMutexLocker lock(&m_mutex);
    return m_videoWidth;
}

int VlcVideoEngine::videoHeight() const
{
    QMutexLocker lock(&m_mutex);
    return m_videoHeight;
}

/**
 * @brief 轮询VLC实际状态：同步图表光标/检测暂停停止结束
 */
void VlcVideoEngine::onPollPosition()
{
    if (!m_mediaPlayer)
        return;

    // Check actual VLC state to detect async pause/stop/ended transitions
    // Note: libvlc_Error is excluded because it can be a transient state
    // during rate changes (setRate), which would cause spurious pauses.
    libvlc_state_t vlcState = libvlc_media_player_get_state(m_mediaPlayer);
    if (vlcState == libvlc_Paused || vlcState == libvlc_Stopped
        || vlcState == libvlc_Ended) {
        m_pollTimer->stop();
        PlaybackState s;
        switch (vlcState) {
            case libvlc_Paused:  s = PlaybackState::Paused;  break;
            case libvlc_Ended:   s = PlaybackState::Ended;   break;
            default:             s = PlaybackState::Stopped;  break;
        }
        emit stateChanged(s);
        return;
    }

    qint64 t = position();
    emit positionChanged(t);

    qint64 dur = duration();
    if (dur > 0 && dur != m_lastReportedDuration) {
        m_lastReportedDuration = dur;
        emit durationChanged(dur);
    }
}

float VlcVideoEngine::fps() const
{
    if (!m_mediaPlayer)
        return 0.0f;
    float f = libvlc_media_player_get_fps(m_mediaPlayer);
    return f > 0.0f ? f : 30.0f;
}

int VlcVideoEngine::volume() const
{
    if (!m_mediaPlayer)
        return 0;
    int v = libvlc_audio_get_volume(m_mediaPlayer);
    return (v >= 0) ? v : 0;
}

void VlcVideoEngine::setVolume(int vol)
{
    if (!m_mediaPlayer)
        return;
    vol = qBound(0, vol, 100);
    libvlc_audio_set_volume(m_mediaPlayer, vol);
}

void VlcVideoEngine::setRate(float rate)
{
    if (!m_mediaPlayer)
        return;
    libvlc_media_player_set_rate(m_mediaPlayer, rate);
}

float VlcVideoEngine::rate() const
{
    if (!m_mediaPlayer)
        return 1.0f;
    return libvlc_media_player_get_rate(m_mediaPlayer);
}

#include "vlc_video_engine.moc"
