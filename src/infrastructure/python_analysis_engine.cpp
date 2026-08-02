/**
 * @file python_analysis_engine.cpp
 * @brief Python 分析引擎实现：QProcess 管理/JSON 解析/进度回调/多进程/音频
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "python_analysis_engine.h"
#include "i18n.h"
#include <QProcess>
#include <QStringList>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QFile>
#include <QCoreApplication>
#include <QThread>
#include <QDebug>
#include <QSettings>
#include <algorithm>
#include <cstring>

PythonAnalysisEngine::PythonAnalysisEngine(QObject *parent)
    : IAnalysisEngine(parent)
{
    m_scriptPath = QDir::toNativeSeparators(
        QCoreApplication::applicationDirPath() + "/analyze_video.py");
}

PythonAnalysisEngine::~PythonAnalysisEngine()
{
    cancelAnalysis();
}

void PythonAnalysisEngine::setPythonExecutable(const QString &path)
{
    m_pythonPath = path;
}

void PythonAnalysisEngine::setScriptPath(const QString &path)
{
    m_scriptPath = path;
}

IAnalysisEngine::VideoTiming PythonAnalysisEngine::videoTiming(const QString &videoPath)
{
    VideoTiming t;
    const VideoInfo info = getVideoInfo(videoPath);
    if (info.fps > 0.0f)
        t.fps = info.fps;
    if (info.fps > 0.0f && info.totalFrames > 0)
        t.durationMs = static_cast<qint64>((static_cast<qreal>(info.totalFrames) / info.fps) * 1000.0);
    return t;
}

QString PythonAnalysisEngine::detectPythonPath()
{
    QString appDir = QCoreApplication::applicationDirPath();

#ifdef Q_OS_WIN
    // 0. Bundled Python (Windows)
    QString bundledPy = appDir + "/python/python.exe";
    if (QFile::exists(bundledPy))
        return QDir::toNativeSeparators(bundledPy);
#endif

#ifdef Q_OS_MACOS
    // 0. Bundled Python (inside .app bundle)
    QString bundledPy = appDir + "/python/bin/python3";
    if (QFile::exists(bundledPy))
        return bundledPy;
#endif

    // 1. Environment variable (cross-platform)
    QString env = qEnvironmentVariable("PYTHON_PATH");
    if (!env.isEmpty() && QFile::exists(env))
        return env;

#ifdef Q_OS_WIN
    // 2. Registry-based detection via QSettings
    QStringList registryKeys = {
        "HKEY_CURRENT_USER\\Software\\Python\\PythonCore",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore"
    };
    for (const QString &regKey : registryKeys) {
        QSettings settings(regKey, QSettings::NativeFormat);
        QStringList versions = settings.childGroups();
        std::sort(versions.begin(), versions.end(), std::greater<QString>());
        for (const QString &ver : versions) {
            settings.beginGroup(ver);
            QString installPath = settings.value("InstallPath").toString();
            settings.endGroup();
            if (!installPath.isEmpty()) {
                QString pyPath = installPath + "/python.exe";
                if (QFile::exists(pyPath))
                    return QDir::toNativeSeparators(pyPath);
                pyPath = installPath + "/python3.exe";
                if (QFile::exists(pyPath))
                    return QDir::toNativeSeparators(pyPath);
            }
        }
    }

    // 3. Common Windows install paths
    QStringList winCandidates = {
        "C:/Python313/python.exe",
        "C:/Python312/python.exe",
        "C:/Python311/python.exe",
        "C:/Python310/python.exe",
        "C:/Program Files/Python313/python.exe",
        "C:/Program Files/Python312/python.exe",
        "C:/Program Files/Python311/python.exe",
        "C:/Program Files/Python310/python.exe",
        "python.exe"
    };
    for (const QString &c : winCandidates) {
        if (QFile::exists(c))
            return c;
    }

    // 4. Windows py.exe launcher
    QProcess probe;
    probe.start("py", {"-3", "-c", "import sys; print(sys.executable)"});
    if (probe.waitForFinished(3000) && probe.exitCode() == 0) {
        QString pyPath = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
        if (!pyPath.isEmpty() && QFile::exists(pyPath))
            return pyPath;
    }
#endif

#ifdef Q_OS_MACOS
    // 2. which python3
    QProcess probe;
    probe.start("which", {"python3"});
    if (probe.waitForFinished(3000) && probe.exitCode() == 0) {
        QString pyPath = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
        if (!pyPath.isEmpty() && QFile::exists(pyPath))
            return pyPath;
    }

    // 3. Common macOS install paths
    QStringList macCandidates = {
        "/opt/homebrew/bin/python3",
        "/usr/local/bin/python3",
        "/usr/bin/python3"
    };
    for (const QString &c : macCandidates) {
        if (QFile::exists(c))
            return c;
    }
#endif

    return QString();
}

QString PythonAnalysisEngine::findFfmpegPath()
{
    QString appDir = QCoreApplication::applicationDirPath();
    // Bundled locations first (Windows .exe / macOS & Linux 无扩展名)
    const QStringList candidates = {
        appDir + "/ffmpeg/ffmpeg.exe",
        appDir + "/ffmpeg.exe",
        appDir + "/ffmpeg/ffmpeg",
        appDir + "/ffmpeg",
    };
    for (const QString &p : candidates)
        if (QFile::exists(p))
            return p;
    // System PATH
    return "ffmpeg";
}

PythonAnalysisEngine::VideoInfo PythonAnalysisEngine::getVideoInfo(const QString &videoPath)
{
    // B7: Return cached result if available (avoids blocking main thread again)
    if (m_videoInfoCache.contains(videoPath))
        return m_videoInfoCache[videoPath];

    VideoInfo info;
    if (m_pythonPath.isEmpty() || !QFile::exists(m_pythonPath))
        return info;
    if (!QFile::exists(m_scriptPath))
        return info;

    QProcess proc;
    proc.setProgram(m_pythonPath);
    proc.setArguments({ m_scriptPath, videoPath, "--check-fps" });
    proc.start();
    if (!proc.waitForFinished(10000))
        return info;

    if (proc.exitCode() != 0)
        return info;

    QByteArray output = proc.readAllStandardOutput().trimmed();
    QJsonDocument doc = QJsonDocument::fromJson(output);
    if (!doc.isObject())
        return info;

    QJsonObject obj = doc.object();
    info.fps = obj["fps"].toDouble(30.0);
    info.totalFrames = static_cast<qint64>(obj["total_frames"].toDouble());

    // B7: Cache the result for future calls
    m_videoInfoCache[videoPath] = info;
    return info;
}

static int computeProcessCount(qint64 totalFrames, float fps)
{
    if (fps <= 0) fps = 30.0f;
    qint64 durationSec = totalFrames / fps;
    int maxProcs = QThread::idealThreadCount();
    if (maxProcs < 1) maxProcs = 1;

    if (durationSec < 30)  return 1;
    if (durationSec < 120) return qMin(2, maxProcs);
    if (durationSec < 600) return qMin(4, maxProcs);
    return qMin(8, maxProcs);   // v1.1：ffmpeg 分块管道，上限 4→8
}

void PythonAnalysisEngine::startAnalysis(const QString &videoPath, const QVector<QRect> &regions,
                                          const QVector<QPolygon> &polygons,
                                          const QStringList &extraVideos,
                                          const QVector<int> &rectRoiIds,
                                          const QVector<int> &polygonRoiIds)
{
    if (m_process) {
        emit analysisFailed("Analysis is already running.");
        return;
    }

    if (m_pythonPath.isEmpty()) {
        emit analysisFailed(lang("Python 可执行文件路径未配置。\n"
                                "请设置 PYTHON_PATH 环境变量，\n"
                                "或确保 python.exe 在系统 PATH 中。",
                                "Python executable path is not configured.\n"
                                "Please set the PYTHON_PATH environment variable, "
                                "or ensure python.exe is in PATH."));
        return;
    }

    if (!QFile::exists(m_pythonPath)) {
        emit analysisFailed(QString(lang("未找到 Python 可执行文件：\n%1",
                                          "Python executable not found:\n%1")).arg(m_pythonPath));
        return;
    }

    // Ensure script path is set before checking existence
    if (m_scriptPath.isEmpty()) {
        m_scriptPath = QDir::toNativeSeparators(
            QCoreApplication::applicationDirPath() + "/analyze_video.py");
    }

    if (!QFile::exists(m_scriptPath)) {
        emit analysisFailed(QString(lang("未找到分析脚本：\n%1",
                                          "Analysis script not found:\n%1")).arg(m_scriptPath));
        return;
    }

    QJsonArray roiArray;
    for (int i = 0; i < regions.size(); ++i) {
        QJsonObject obj;
        obj["type"] = "rect";
        if (i < rectRoiIds.size())
            obj["roi_id"] = rectRoiIds[i];
        obj["x"] = regions[i].x();
        obj["y"] = regions[i].y();
        obj["w"] = regions[i].width();
        obj["h"] = regions[i].height();
        roiArray.append(obj);
    }
    for (int i = 0; i < polygons.size(); ++i) {
        QJsonObject obj;
        obj["type"] = "polygon";
        if (i < polygonRoiIds.size())
            obj["roi_id"] = polygonRoiIds[i];
        QJsonArray pointsArray;
        for (const QPoint &pt : polygons[i]) {
            QJsonArray ptArr;
            ptArr.append(pt.x());
            ptArr.append(pt.y());
            pointsArray.append(ptArr);
        }
        obj["points"] = pointsArray;
        roiArray.append(obj);
    }
    QString roiJson = QString::fromUtf8(QJsonDocument(roiArray).toJson(QJsonDocument::Compact));

    // B2: Build arguments. Multi-video uses --videos; single video uses positional args.
    // The Python script merges all videos onto one continuous timeline internally.
    QStringList args;
    if (extraVideos.isEmpty()) {
        args = { m_scriptPath, videoPath, roiJson };
    } else {
        QStringList allVideos;
        allVideos << videoPath;
        for (const QString &p : extraVideos)
            if (!p.isEmpty())
                allVideos << p;
        args = { m_scriptPath, allVideos[0], roiJson, "--videos" };
        for (int i = 1; i < allVideos.size(); ++i)
            if (!allVideos[i].isEmpty())
                args << allVideos[i];
    }

    // v0.3: Compute actual process count based on the primary video's duration.
    // (For multi-video, per-video process count is still driven by the primary;
    // the script applies it to each video in sequence.)
    VideoInfo vInfo = getVideoInfo(videoPath);
    int procCount = computeProcessCount(vInfo.totalFrames, vInfo.fps);
    args << "--processes" << QString::number(procCount);

    // Add --ffmpeg-path
    QString ffmpegPath = findFfmpegPath();
    args << "--ffmpeg-path" << ffmpegPath;

    m_outputBuffer.clear();
    m_stderrBuffer.clear();
    m_lastProgressPct = 0.0;   // 新一轮分析，进度单调基线归零

    // Store roiIds for use in onFinished
    m_pendingRectRoiIds = rectRoiIds;
    m_pendingPolygonRoiIds = polygonRoiIds;

    m_process = new QProcess(this);
    m_process->setProgram(m_pythonPath);
    m_process->setArguments(args);

    // B12: separate stdout/stderr handlers avoid double-fire timing fragility.
    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &PythonAnalysisEngine::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &PythonAnalysisEngine::onReadyReadStderr);
    connect(m_process, &QProcess::finished,
            this, [this](int exitCode, QProcess::ExitStatus) {
                onFinished(exitCode);
            });

    m_process->start();
}

void PythonAnalysisEngine::startAudioAnalysis(const QString &videoPath)
{
    if (m_process) {
        emit analysisFailed("Analysis is already running.");
        return;
    }

    if (m_pythonPath.isEmpty() || !QFile::exists(m_pythonPath)) {
        emit analysisFailed(lang("Python 可执行文件路径未配置。", "Python executable path is not configured."));
        return;
    }

    if (!QFile::exists(m_scriptPath)) {
        emit analysisFailed(QString(lang("未找到分析脚本：\n%1", "Analysis script not found:\n%1")).arg(m_scriptPath));
        return;
    }

    QStringList args = { m_scriptPath, videoPath, "--audio-only" };

    if (m_noiseReduction > 0) {
        args << "--noise-reduction" << QString::number(m_noiseReduction, 'f', 1);
    }

    QString ffmpegPath = findFfmpegPath();
    args << "--ffmpeg-path" << ffmpegPath;

    m_outputBuffer.clear();
    m_stderrBuffer.clear();
    m_lastProgressPct = 0.0;   // 新一轮分析，进度单调基线归零

    m_process = new QProcess(this);
    m_process->setProgram(m_pythonPath);
    m_process->setArguments(args);

    connect(m_process, &QProcess::readyReadStandardOutput,
            this, &PythonAnalysisEngine::onReadyReadStdout);
    connect(m_process, &QProcess::readyReadStandardError,
            this, &PythonAnalysisEngine::onReadyReadStderr);
    connect(m_process, &QProcess::finished,
            this, [this](int exitCode, QProcess::ExitStatus) {
                onFinished(exitCode);
            });

    m_process->start();
}

void PythonAnalysisEngine::cancelAnalysis()
{
    QProcess *proc = m_process;
    if (!proc)
        return;

    m_process = nullptr;
    disconnect(proc, nullptr, this, nullptr);

    // M2: Try graceful termination first, then force kill
    proc->terminate();
    if (!proc->waitForFinished(3000)) {
        proc->kill();
        proc->waitForFinished(2000);
    }
    proc->deleteLater();

    emit analysisFailed("Analysis cancelled by user.");
}

bool PythonAnalysisEngine::isRunning() const
{
    return m_process != nullptr;
}

void PythonAnalysisEngine::onReadyReadStdout()
{
    if (!m_process)
        return;
    QByteArray out = m_process->readAllStandardOutput();
    if (!out.isEmpty())
        m_outputBuffer.append(out);

    // Also drain stderr to prevent pipe blocking
    QByteArray err = m_process->readAllStandardError();
    if (!err.isEmpty())
        m_stderrBuffer.append(err);
}

void PythonAnalysisEngine::onReadyReadStderr()
{
    if (!m_process)
        return;

    // Accumulate into m_stderrBuffer, then parse complete lines from it
    m_stderrBuffer.append(m_process->readAllStandardError());

    // Process complete lines from buffer (preserve incomplete last line)
    int newlineIdx;
    while ((newlineIdx = m_stderrBuffer.indexOf('\n')) >= 0) {
        QByteArray line = m_stderrBuffer.left(newlineIdx).trimmed();
        m_stderrBuffer.remove(0, newlineIdx + 1);

        QString lineStr = QString::fromUtf8(line);
        if (lineStr.startsWith("PROGRESS:")) {
            QStringList parts = lineStr.mid(9).split('|');
            if (parts.size() >= 3) {
                qreal pct = parts[2].toDouble();
                // v1.1：单调化兜底——旧脚本/多阶段输出可能回退，进度条与文本只增不减
                pct = qMax(pct, m_lastProgressPct);
                m_lastProgressPct = pct;
                emit progressUpdated(parts[0].toInt(), parts[1].toInt(), pct);
            }
        }
    }

    // Drain stdout alongside stderr to prevent Python from blocking on
    // print(json) when the pipe buffer fills up (deadlock prevention).
    m_outputBuffer.append(m_process->readAllStandardOutput());
}

static AudioData parseAudioData(const QJsonObject &audioObj)
{
    AudioData audio;

    QJsonArray volArray = audioObj["volume"].toArray();
    audio.volume.reserve(volArray.size());
    for (const auto &v : volArray)
        audio.volume.append(v.toDouble());

    // Read spectrogram from binary file (fast path)
    QString specFile = audioObj["spectrogram_file"].toString();
    if (!specFile.isEmpty()) {
        QJsonArray shape = audioObj["spectrogram_shape"].toArray();
        int nFreqBins = shape.size() > 0 ? shape[0].toInt() : 0;
        int nFrames = shape.size() > 1 ? shape[1].toInt() : 0;
        if (nFreqBins > 0 && nFrames > 0) {
            QFile file(specFile);
            if (file.open(QIODevice::ReadOnly)) {
                qint64 expectedSize = static_cast<qint64>(nFreqBins) * nFrames * sizeof(double);
                if (file.size() == expectedSize) {
                    QByteArray data = file.readAll();
                    const double *src = reinterpret_cast<const double*>(data.constData());
                    audio.spectrogram.resize(nFreqBins);
                    for (int f = 0; f < nFreqBins; ++f) {
                        audio.spectrogram[f].resize(nFrames);
                        memcpy(audio.spectrogram[f].data(), &src[f * nFrames],
                               nFrames * sizeof(double));
                    }
                } else {
                    qWarning() << "Spectrogram file size mismatch:" << file.size()
                               << "expected:" << expectedSize;
                }
                file.close();
            } else {
                qWarning() << "Failed to open spectrogram file:" << specFile;
            }
            // Clean up temp file
            QFile::remove(specFile);
        }
    }

    // Fallback: read from JSON (legacy / multi-video merged path)
    if (audio.spectrogram.isEmpty()) {
        QJsonArray specArray = audioObj["spectrogram"].toArray();
        audio.spectrogram.reserve(specArray.size());
        for (const auto &binVal : specArray) {
            QJsonArray binArray = binVal.toArray();
            QVector<qreal> bin;
            bin.reserve(binArray.size());
            for (const auto &v : binArray)
                bin.append(v.toDouble());
            audio.spectrogram.append(std::move(bin));
        }
    }

    audio.sampleRate = audioObj["sample_rate"].toDouble(16000);
    audio.hopLength = audioObj["hop_length"].toInt(512);
    audio.nFft = audioObj["n_fft"].toInt(1280);
    audio.timeResolutionMs = audioObj["time_resolution_ms"].toDouble(
        1000.0 * audio.hopLength / audio.sampleRate);
    audio.specMin = audioObj["spec_min"].toDouble(0);
    audio.specMax = audioObj["spec_max"].toDouble(0);

    return audio;
}

void PythonAnalysisEngine::onFinished(int exitCode)
{
    QProcess *proc = m_process;
    if (!proc)
        return;

    // Clear member pointer before emitting signals to prevent re-entrant issues
    m_process = nullptr;

    if (exitCode != 0) {
        QString err = QString::fromUtf8(m_stderrBuffer).trimmed();
        if (err.isEmpty()) {
            err = QString::fromUtf8(proc->readAllStandardError()).trimmed();
        }
        if (err.isEmpty()) {
            err = QString(lang("进程以代码 %1 退出，无标准错误输出。\n"
                               "Python: %2\n脚本: %3",
                               "Process exited with code %1. No stderr output.\n"
                               "Python: %2\nScript: %3"))
                      .arg(exitCode)
                      .arg(m_pythonPath)
                      .arg(m_scriptPath);
        }
        emit analysisFailed(err);
    } else {
        QByteArray finalOut = proc->readAllStandardOutput();
        if (!finalOut.isEmpty())
            m_outputBuffer.append(finalOut);

        QJsonDocument doc = QJsonDocument::fromJson(m_outputBuffer);
        if (!doc.isObject()) {
            emit analysisFailed("Invalid analysis result format.");
            proc->deleteLater();
            return;
        }

        QJsonObject obj = doc.object();
        QJsonArray timestampsArray = obj["timestamps"].toArray();
        QJsonArray luminancesArray = obj["luminances"].toArray();

        QVector<qint64> timestamps;
        timestamps.reserve(timestampsArray.size());
        for (const auto &v : timestampsArray)
            timestamps.append(static_cast<qint64>(v.toDouble()));

        int expectedPoints = timestamps.size();
        QVector<QVector<qreal>> values;
        values.reserve(luminancesArray.size());
        for (const auto &regionArr : luminancesArray) {
            QJsonArray arr = regionArr.toArray();
            QVector<qreal> series;
            series.reserve(expectedPoints);
            for (int i = 0; i < expectedPoints; ++i) {
                if (i < arr.size())
                    series.append(arr[i].toDouble());
                else
                    series.append(0.0);
            }
            values.append(std::move(series));
        }

        // Parse audio data if present
        AudioData audio;
        if (obj.contains("audio")) {
            audio = parseAudioData(obj["audio"].toObject());
            if (audio.hasVolume() && !audio.hasSpectrogram()) {
                qWarning() << "Audio parsed: volume has" << audio.volume.size()
                           << "points but spectrogram is empty (file may have been deleted)";
            }
        }

        AnalysisSnapshot snapshot;
        snapshot.timestamps = std::move(timestamps);
        snapshot.values = std::move(values);

        // Populate dataEntries from stored roiIds
        int totalRois = snapshot.values.size();
        int rectCount = m_pendingRectRoiIds.size();
        for (int i = 0; i < totalRois; ++i) {
            DataEntry entry;
            if (i < rectCount) {
                entry.type = DataEntry::Rect;
                entry.roiId = (i < m_pendingRectRoiIds.size()) ? m_pendingRectRoiIds[i] : -1;
            } else {
                entry.type = DataEntry::Polygon;
                int pi = i - rectCount;
                entry.roiId = (pi < m_pendingPolygonRoiIds.size()) ? m_pendingPolygonRoiIds[pi] : -1;
            }
            snapshot.dataEntries.append(entry);
        }
        m_pendingRectRoiIds.clear();
        m_pendingPolygonRoiIds.clear();

        snapshot.audio = audio;
        emit progressUpdated(100, 100, 100.0);
        emit analysisFinished(snapshot);
    }

    proc->deleteLater();
}
