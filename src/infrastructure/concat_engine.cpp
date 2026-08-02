/**
 * @file concat_engine.cpp
 * @brief 无损拼接引擎实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "concat_engine.h"
#include "python_analysis_engine.h"
#include "domain/preprocess_text.h"

#include <QProcess>
#include <QTimer>
#include <QFile>
#include <QFileInfo>
#include <QDir>

ConcatEngine::ConcatEngine(QObject *parent)
    : QObject(parent)
{
}

ConcatEngine::~ConcatEngine()
{
    cancel();
}

QString ConcatEngine::buildListFile(const QStringList &orderedFiles)
{
    QString out;
    for (const QString &f : orderedFiles)
        out += preprocess_text::concatEscapePath(QFileInfo(f).absoluteFilePath())
            + QLatin1Char('\n');
    return out;
}

void ConcatEngine::run(const ConcatRequest &req)
{
    if (isRunning())
        return;
    m_req = req;
    m_cancelled = false;
    m_lastPercent = -1;
    m_normFiles.clear();
    m_normIndex = -1;

    QDir().mkpath(req.workDir);

    if (req.normalizeTimestamps && !req.segmentOffsetsMs.isEmpty()) {
        // 第一段式：逐段流拷贝 + -output_ts_offset 平移（§5.4.3，零重编码）
        m_normalizing = true;
        m_normIndex = 0;
        startConcat({}, {});    // 进入归一化分支
        return;
    }
    startConcat(req.orderedFiles, req.outputPath);
}

void ConcatEngine::cancel()
{
    m_cancelled = true;
    if (m_watchdog)
        m_watchdog->stop();
    if (m_process) {
        m_process->terminate();
        if (!m_process->waitForFinished(3000))
            m_process->kill();
        m_process->deleteLater();
        m_process = nullptr;
    }
    cleanupPartial();
}

bool ConcatEngine::isRunning() const
{
    return m_process && m_process->state() != QProcess::NotRunning;
}

void ConcatEngine::startConcat(const QStringList &files, const QString &outputPath)
{
    const QString ffmpeg = PythonAnalysisEngine::findFfmpegPath();
    QStringList args;
    if (m_normalizing) {
        // 逐段归一化 remux（流拷贝，不重编码）
        if (m_normIndex >= m_req.orderedFiles.size()) {
            // 归一化完成 → 进入拼接
            m_normalizing = false;
            startConcat(m_normFiles, m_req.outputPath);
            return;
        }
        const QString in = m_req.orderedFiles[m_normIndex];
        const QString out = m_req.workDir + QStringLiteral("/norm_%1.mp4")
                                .arg(m_normIndex, 3, 10, QLatin1Char('0'));
        const qint64 offsetMs = m_req.segmentOffsetsMs.value(m_normIndex);
        args << QStringLiteral("-i") << in
             << QStringLiteral("-c") << QStringLiteral("copy")
             << QStringLiteral("-output_ts_offset")
                 << QString::number(offsetMs / 1000.0, 'f', 3)
             << QStringLiteral("-avoid_negative_ts") << QStringLiteral("make_zero")
             << QStringLiteral("-y") << out;
        m_actualOutput = out;
    } else {
        // 主拼接命令（§5.4.1）
        const QString listPath = m_req.workDir + QStringLiteral("/concat_list.txt");
        QFile f(listPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            emit failed(PreprocessError::FileUnreadable,
                        QStringLiteral("cannot write %1").arg(listPath));
            return;
        }
        f.write(buildListFile(files).toUtf8());
        f.close();
        args << QStringLiteral("-f") << QStringLiteral("concat")
             << QStringLiteral("-safe") << QStringLiteral("0")
             << QStringLiteral("-i") << listPath
             << QStringLiteral("-c") << QStringLiteral("copy")
             << QStringLiteral("-avoid_negative_ts") << QStringLiteral("make_zero")
             << QStringLiteral("-fflags") << QStringLiteral("+genpts")
             << QStringLiteral("-movflags") << QStringLiteral("+faststart")
             << QStringLiteral("-progress") << QStringLiteral("pipe:1")
             << QStringLiteral("-nostats")
             << QStringLiteral("-y") << outputPath;
        m_actualOutput = outputPath;
    }

    m_stdoutBuf.clear();
    m_stderrBuf.clear();
    m_process = new QProcess(this);
    m_process->setProgram(ffmpeg);
    m_process->setArguments(args);
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &ConcatEngine::onProgressLine);
    connect(m_process, &QProcess::readyReadStandardError, this, [this]() {
        m_stderrBuf += m_process->readAllStandardError();
    });
    connect(m_process, qOverload<int, QProcess::ExitStatus>(&QProcess::finished),
            this, [this](int code, QProcess::ExitStatus) { onFinished(code); });

    if (!m_watchdog) {
        m_watchdog = new QTimer(this);
        m_watchdog->setSingleShot(true);
        connect(m_watchdog, &QTimer::timeout, this, [this]() {
            if (m_process)
                m_process->kill();
            emit failed(PreprocessError::Timeout, QStringLiteral("concat timeout"));
        });
    }
    // 超时：拼接默认 30 分钟（规范 C4）；归一化单段 30 分钟
    m_watchdog->start(30 * 60 * 1000);
    m_process->start();
    if (!m_process->waitForStarted(5000)) {
        emit failed(PreprocessError::ConcatFailed,
                    QStringLiteral("failed to start ffmpeg"));
    }
}

void ConcatEngine::onProgressLine()
{
    m_stdoutBuf += m_process->readAllStandardOutput();
    int nl;
    while ((nl = m_stdoutBuf.indexOf('\n')) >= 0) {
        const QByteArray line = m_stdoutBuf.left(nl).trimmed();
        m_stdoutBuf.remove(0, nl + 1);
        // R-5：out_time_ms 单位是微秒，parseFfmpegProgressMs 已换算为毫秒
        const qint64 ms = preprocess_text::parseFfmpegProgressMs(line);
        if (ms < 0 || m_req.totalDurationMs <= 0)
            continue;
        const int pct = qBound(0, int(ms * 100 / m_req.totalDurationMs), 99);
        if (pct != m_lastPercent) {
            m_lastPercent = pct;
            emit progress(pct, m_actualOutput);
        }
    }
}

void ConcatEngine::onFinished(int exitCode)
{
    if (m_watchdog)
        m_watchdog->stop();
    QProcess *proc = m_process;
    m_process = nullptr;
    if (proc)
        proc->deleteLater();
    if (m_cancelled)
        return;     // 取消由状态机接管（C1）

    if (exitCode != 0) {
        cleanupPartial();
        emit failed(PreprocessError::ConcatFailed,
                    QStringLiteral("exit %1: %2").arg(exitCode)
                        .arg(QString::fromUtf8(m_stderrBuf.right(500))));
        return;
    }
    if (m_normalizing) {
        m_normFiles.append(m_actualOutput);
        ++m_normIndex;
        startConcat({}, {});    // 下一段 / 进入拼接
        return;
    }
    emit progress(100, m_actualOutput);
    emit finished(m_actualOutput);
}

void ConcatEngine::cleanupPartial()
{
    // 半成品清理（§5.6）：仅删除本任务产物路径
    if (!m_actualOutput.isEmpty() && QFile::exists(m_actualOutput))
        QFile::remove(m_actualOutput);
    for (const QString &f : m_normFiles)
        if (QFile::exists(f))
            QFile::remove(f);
    m_normFiles.clear();
}
