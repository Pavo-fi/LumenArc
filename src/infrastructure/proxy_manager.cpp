/**
 * @file proxy_manager.cpp
 * @brief 拖拽预览代理管理器实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-07-26
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "proxy_manager.h"
#include <QStandardPaths>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QCryptographicHash>
#include <QDebug>
#include <QRegularExpression>

extern "C" {
#include <libavformat/avformat.h>
}

ProxyManager::ProxyManager(QObject *parent) : QObject(parent) {}

ProxyManager::~ProxyManager()
{
    cancel();
}

QString ProxyManager::cacheDirPath()
{
    QString dir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation)
                  + "/proxy";
    QDir().mkpath(dir);
    return dir;
}

QString ProxyManager::ffmpegPath()
{
    QString appDir = QCoreApplication::applicationDirPath();
    for (const QString &c : {appDir + "/ffmpeg/ffmpeg.exe", appDir + "/ffmpeg.exe"}) {
        if (QFile::exists(c))
            return c;
    }
    return "ffmpeg";
}

bool ProxyManager::needsProxy(const QString &videoPath)
{
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, videoPath.toUtf8().constData(), nullptr, nullptr) < 0)
        return false;
    bool need = false;
    // 无索引容器（PS/TS）：seek 天生昂贵，必须代理
    if (strncmp(fmt->iformat->name, "mpeg", 4) == 0) {
        need = true;
    } else {
        int vs = av_find_best_stream(fmt, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
        if (vs >= 0 && fmt->streams[vs]->codecpar->width > 1920)
            need = true;
    }
    avformat_close_input(&fmt);
    return need;
}

QString ProxyManager::hashFor(const QString &videoPath) const
{
    QFileInfo fi(videoPath);
    QString key = fi.canonicalFilePath() + "|" + QString::number(fi.size()) + "|"
                  + QString::number(fi.lastModified().toMSecsSinceEpoch());
    return QString::fromUtf8(
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString ProxyManager::existingProxy(const QString &videoPath) const
{
    QString path = cacheDirPath() + "/" + hashFor(videoPath) + ".mp4";
    return QFile::exists(path) ? path : QString();
}

void ProxyManager::requestProxy(const QString &videoPath)
{
    QString existing = existingProxy(videoPath);
    if (!existing.isEmpty()) {
        emit proxyReady(existing);
        return;
    }
    if (m_process && m_videoPath == videoPath)
        return;                 // 同一任务已在进行
    cancel();

    m_videoPath = videoPath;
    m_finalPath = cacheDirPath() + "/" + hashFor(videoPath) + ".mp4";
    // 临时文件必须保留 .mp4 扩展名（mp4 muxer 按扩展名推断格式）
    m_partPath = cacheDirPath() + "/" + hashFor(videoPath) + ".part.mp4";
    m_usedNvenc = false;
    m_triedNvenc = false;
    // 探测源时长用于进度百分比（失败则发 -1 表示仅"进行中"）
    m_srcDurationSec = 0;
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, videoPath.toUtf8().constData(), nullptr, nullptr) >= 0) {
        if (fmt->duration > 0)
            m_srcDurationSec = fmt->duration / 1000000.0;
        avformat_close_input(&fmt);
    }
    startTranscode(videoPath, m_partPath);
}

void ProxyManager::cancel()
{
    if (!m_process)
        return;
    QProcess *p = m_process;
    m_process = nullptr;
    disconnect(p, nullptr, this, nullptr);
    p->kill();
    p->waitForFinished(3000);
    p->deleteLater();
    // 清理半成品
    if (!m_partPath.isEmpty())
        QFile::remove(m_partPath);
}

void ProxyManager::startTranscode(const QString &videoPath, const QString &outPath)
{
    // 优先 NVENC（GPU 编码，速度快数倍且不占 CPU），失败回退 CPU 编码
    m_triedNvenc = true;
    startWithEncoder(videoPath, outPath, true);
}

void ProxyManager::startWithEncoder(const QString &videoPath, const QString &outPath,
                                    bool useNvenc)
{
    m_usedNvenc = useNvenc;
    QStringList args = {"-y", "-hide_banner", "-nostdin",
                        "-i", videoPath,
                        "-vf", "scale=960:-2",
                        "-an", "-sn", "-dn",
                        "-fps_mode", "passthrough"};   // 逐帧不丢：帧号 1:1
    if (useNvenc) {
        args << "-c:v" << "h264_nvenc" << "-preset" << "p1" << "-g" << "1";
    } else {
        args << "-c:v" << "libx264" << "-preset" << "ultrafast"
             << "-threads" << "2" << "-g" << "1" << "-keyint_min" << "1"
             << "-sc_threshold" << "0";
    }
    args << "-movflags" << "+faststart" << outPath;

    m_process = new QProcess(this);
    m_process->setProgram(ffmpegPath());
    m_process->setArguments(args);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &ProxyManager::onReadyRead);
    connect(m_process, &QProcess::finished,
            this, [this](int code, QProcess::ExitStatus) { onFinished(code); });
    m_process->start();
}

void ProxyManager::onReadyRead()
{
    if (!m_process)
        return;
    QByteArray data = m_process->readAllStandardError();
    // ffmpeg 进度行：time=HH:MM:SS.xx
    static const QRegularExpression re("time=(\\d+):(\\d+):(\\d+\\.\\d+)");
    int pct = -1;
    for (auto it = re.globalMatch(QString::fromUtf8(data)); it.hasNext();) {
        auto match = it.next();
        if (m_srcDurationSec > 0) {
            double secs = match.captured(1).toInt() * 3600
                          + match.captured(2).toInt() * 60
                          + match.captured(3).toDouble();
            pct = qBound(0, static_cast<int>(secs * 100.0 / m_srcDurationSec), 99);
        }
    }
    emit progressChanged(pct);
}

void ProxyManager::onFinished(int exitCode)
{
    QProcess *p = m_process;
    m_process = nullptr;
    if (!p)
        return;
    QByteArray errTail = p->readAllStandardError().right(2000);
    p->deleteLater();

    if (exitCode == 0 && QFile::exists(m_partPath) && QFileInfo(m_partPath).size() > 0) {
        QFile::remove(m_finalPath);
        if (QFile::rename(m_partPath, m_finalPath)) {
            enforceCacheLimit();
            emit proxyReady(m_finalPath);
            return;
        }
    }

    if (m_usedNvenc && m_triedNvenc) {
        // NVENC 失败（无 N 卡/驱动问题）：回退 CPU 编码重试
        qWarning() << "ProxyManager: NVENC failed, fallback to libx264";
        QFile::remove(m_partPath);
        m_usedNvenc = false;
        startWithEncoder(m_videoPath, m_partPath, false);
        return;
    }

    emit proxyFailed(QString::fromUtf8(errTail));
}

void ProxyManager::enforceCacheLimit()
{
    QDir dir(cacheDirPath());
    QFileInfoList files = dir.entryInfoList({"*.mp4"}, QDir::Files,
                                            QDir::Time);   // 最旧在前
    qint64 total = 0;
    for (const QFileInfo &fi : files)
        total += fi.size();
    while (total > CACHE_LIMIT_BYTES && !files.isEmpty()) {
        QFileInfo oldest = files.takeFirst();
        total -= oldest.size();
        QFile::remove(oldest.absoluteFilePath());
    }
}
