/**
 * @file live_recorder.cpp
 * @brief 直播录制器实现（ffmpeg 流拷贝子进程）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-11
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "infrastructure/live/live_recorder.h"
#include "infrastructure/tool_paths.h"

#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QTimer>

namespace {
constexpr int kErrTailMax = 4000;
constexpr int kGracefulStopMs = 8000;   // 写 trailer 的宽限时间，超时强杀
} // namespace

LiveRecorder::LiveRecorder(QObject *parent)
    : QObject(parent)
{
}

LiveRecorder::~LiveRecorder()
{
    if (m_proc && m_proc->state() != QProcess::NotRunning) {
        m_proc->write("q\n");
        m_proc->closeWriteChannel();
        if (!m_proc->waitForFinished(3000))
            m_proc->kill();
        m_proc->waitForFinished(1000);
    }
    delete m_proc;
    m_proc = nullptr;
}

bool LiveRecorder::isRecording() const
{
    return m_proc && m_proc->state() != QProcess::NotRunning;
}

QString LiveRecorder::defaultRecordDir()
{
    QString base = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
    if (base.isEmpty())
        base = QDir::currentPath();
    return QDir(base).filePath(QStringLiteral("LumenArc监控录制"));
}

QString LiveRecorder::suggestFileName(const QString &host, int channel,
                                      const QString &container)
{
    QString ext = container.trimmed();
    if (ext.isEmpty())
        ext = QStringLiteral("mp4");
    if (!ext.startsWith(QLatin1Char('.')))
        ext.prepend(QLatin1Char('.'));

    // 文件名安全化：主机名/IP 仅保留数字字母与 . _ -
    QString h;
    h.reserve(host.size());
    for (const QChar c : host) {
        if (c.isLetterOrNumber() || c == QLatin1Char('.') || c == QLatin1Char('_')
            || c == QLatin1Char('-'))
            h.append(c);
        else
            h.append(QLatin1Char('_'));
    }
    if (h.isEmpty())
        h = QStringLiteral("monitor");

    const QString stamp = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
    return QStringLiteral("监控_%1_ch%2_%3%4").arg(h).arg(channel).arg(stamp, ext);
}

bool LiveRecorder::start(const QString &url, const QString &outPath, bool transportTcp,
                         QString *error)
{
    const auto fail = [error](const QString &m) {
        if (error)
            *error = m;
        return false;
    };

    if (isRecording())
        return fail(QStringLiteral("已在录制中，请先停止当前录制"));
    if (url.trimmed().isEmpty())
        return fail(QStringLiteral("直播地址为空"));
    if (outPath.trimmed().isEmpty())
        return fail(QStringLiteral("输出路径为空"));

    const QFileInfo fi(outPath);
    if (!QDir().mkpath(fi.absolutePath()))
        return fail(QStringLiteral("无法创建录制目录：%1").arg(fi.absolutePath()));

    const bool mkv = fi.suffix().compare(QStringLiteral("mkv"), Qt::CaseInsensitive) == 0;
    const QString ffmpeg = ToolPaths::findFfmpegPath();

    // 流拷贝录制：视频不重编码（与源逐比特一致），音频转 AAC 以兼容 MP4。
    QStringList args;
    args << QStringLiteral("-hide_banner") << QStringLiteral("-loglevel")
         << QStringLiteral("warning") << QStringLiteral("-y");
    if (transportTcp)
        args << QStringLiteral("-rtsp_transport") << QStringLiteral("tcp");
    args << QStringLiteral("-i") << url.trimmed();
    args << QStringLiteral("-map") << QStringLiteral("0:v:0")
         << QStringLiteral("-c:v") << QStringLiteral("copy");
    args << QStringLiteral("-map") << QStringLiteral("0:a:0?")
         << QStringLiteral("-c:a") << QStringLiteral("aac")
         << QStringLiteral("-b:a") << QStringLiteral("128k");
    args << QStringLiteral("-f") << (mkv ? QStringLiteral("matroska") : QStringLiteral("mp4"));
    args << outPath;

    m_proc = new QProcess(this);
    m_proc->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_proc, &QProcess::finished, this, &LiveRecorder::onFinished);
    connect(m_proc, &QProcess::errorOccurred, this, &LiveRecorder::onErrorOccurred);
    connect(m_proc, &QProcess::readyReadStandardError, this, &LiveRecorder::appendStderr);

    m_outPath = outPath;
    m_errTail.clear();
    m_stopRequested = false;

    m_proc->start(ffmpeg, args);
    if (!m_proc->waitForStarted(5000)) {
        const QString err = m_proc->errorString();
        QProcess *p = m_proc;
        m_proc = nullptr;
        m_outPath.clear();
        p->deleteLater();
        return fail(QStringLiteral("无法启动录制进程（%1）：%2").arg(ffmpeg, err));
    }

    emit started(m_outPath);
    return true;
}

void LiveRecorder::stop()
{
    if (!m_proc || m_proc->state() == QProcess::NotRunning)
        return;
    m_stopRequested = true;
    // ffmpeg 交互命令：'q' = 正常收尾退出（写完 trailer 再关文件），
    // 强杀会留下无 moov 的坏 MP4
    m_proc->write("q\n");
    m_proc->closeWriteChannel();

    QTimer::singleShot(kGracefulStopMs, this, [this]() {
        if (m_proc && m_proc->state() != QProcess::NotRunning) {
            qWarning() << "LiveRecorder: ffmpeg 未在宽限期内收尾，强制结束";
            m_proc->kill();
        }
    });
}

void LiveRecorder::appendStderr()
{
    if (!m_proc)
        return;
    m_errTail += QString::fromUtf8(m_proc->readAllStandardError());
    if (m_errTail.size() > kErrTailMax)
        m_errTail = m_errTail.right(kErrTailMax);
}

void LiveRecorder::onFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    if (!m_proc)
        return;   // 已由 errorOccurred 路径收尾
    appendStderr();

    const QString path = m_outPath;
    const bool crashed = (exitStatus == QProcess::CrashExit);
    const QFileInfo fi(path);
    const bool fileOk = fi.exists() && fi.size() > 0;
    const bool ok = !crashed && exitCode == 0 && fileOk;

    QString msg;
    if (ok) {
        msg = QStringLiteral("录制完成：%1").arg(path);
    } else if (!m_errTail.trimmed().isEmpty()) {
        msg = m_errTail.trimmed();
    } else if (crashed) {
        msg = QStringLiteral("录制进程异常退出");
    } else if (!fileOk) {
        msg = QStringLiteral("录制文件为空或未生成（退出码 %1）").arg(exitCode);
    } else {
        msg = QStringLiteral("录制失败（退出码 %1）").arg(exitCode);
    }

    QProcess *p = m_proc;
    m_proc = nullptr;
    m_outPath.clear();
    m_errTail.clear();
    m_stopRequested = false;
    p->deleteLater();

    emit finished(path, ok, msg);
}

void LiveRecorder::onErrorOccurred(QProcess::ProcessError error)
{
    if (!m_proc)
        return;
    if (error != QProcess::FailedToStart)
        return;   // 运行期错误由 finished 统一报告

    const QString path = m_outPath;
    const QString msg = QStringLiteral("录制进程启动失败：%1").arg(m_proc->errorString());

    QProcess *p = m_proc;
    m_proc = nullptr;
    m_outPath.clear();
    m_errTail.clear();
    m_stopRequested = false;
    p->deleteLater();

    emit finished(path, false, msg);
}
