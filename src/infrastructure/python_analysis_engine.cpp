/**
 * @file python_analysis_engine.cpp
 * @brief Python 分析引擎实现：QProcess 管理/JSON 解析/进度回调
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "python_analysis_engine.h"
#include "i18n.h"
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QCoreApplication>
#include <QDebug>

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

void PythonAnalysisEngine::startAnalysis(const QString &videoPath, const QVector<QRect> &regions)
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

    if (!QFile::exists(m_scriptPath)) {
        emit analysisFailed(QString(lang("未找到分析脚本：\n%1",
                                          "Analysis script not found:\n%1")).arg(m_scriptPath));
        return;
    }

    if (m_scriptPath.isEmpty()) {
        m_scriptPath = QDir::toNativeSeparators(
            QCoreApplication::applicationDirPath() + "/analyze_video.py");
    }

    QJsonArray roiArray;
    for (const QRect &rc : regions) {
        QJsonObject obj;
        obj["x"] = rc.x();
        obj["y"] = rc.y();
        obj["w"] = rc.width();
        obj["h"] = rc.height();
        roiArray.append(obj);
    }
    QString roiJson = QString::fromUtf8(QJsonDocument(roiArray).toJson(QJsonDocument::Compact));

    m_outputBuffer.clear();
    m_stderrBuffer.clear();

    m_process = new QProcess(this);
    m_process->setProgram(m_pythonPath);
    m_process->setArguments({ m_scriptPath, videoPath, roiJson });

    connect(m_process, &QProcess::readyReadStandardOutput, this, &PythonAnalysisEngine::onReadyRead);
    connect(m_process, &QProcess::readyReadStandardError, this, &PythonAnalysisEngine::onReadyRead);
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
    proc->kill();
    proc->waitForFinished(2000);
    proc->deleteLater();

    emit analysisFailed("Analysis cancelled by user.");
}

bool PythonAnalysisEngine::isRunning() const
{
    return m_process != nullptr;
}

void PythonAnalysisEngine::onReadyRead()
{
    if (!m_process)
        return;

    QByteArray out = m_process->readAllStandardOutput();
    if (!out.isEmpty())
        m_outputBuffer.append(out);

    QByteArray err = m_process->readAllStandardError();
    if (!err.isEmpty()) {
        // Accumulate all stderr for error reporting
        m_stderrBuffer.append(err);

        // Also parse PROGRESS: lines inline
        QString errStr = QString::fromUtf8(err).trimmed();
        for (const QString &line : errStr.split('\n')) {
            if (line.startsWith("PROGRESS:")) {
                QStringList parts = line.mid(9).split('|');
                if (parts.size() >= 3) {
                    qreal pct = parts[2].toDouble();
                    emit progressUpdated(parts[0].toInt(), parts[1].toInt(), pct);
                }
            }
        }
    }
}

void PythonAnalysisEngine::onFinished(int exitCode)
{
    if (!m_process)
        return;

    if (exitCode != 0) {
        // Use accumulated stderr buffer — readAllStandardError() may be empty
        // because onReadyRead() already consumed the data.
        QString err = QString::fromUtf8(m_stderrBuffer).trimmed();
        if (err.isEmpty()) {
            // Fallback: try reading directly
            err = QString::fromUtf8(m_process->readAllStandardError()).trimmed();
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
        QByteArray finalOut = m_process->readAllStandardOutput();
        if (!finalOut.isEmpty())
            m_outputBuffer.append(finalOut);

        QJsonDocument doc = QJsonDocument::fromJson(m_outputBuffer);
        if (!doc.isObject()) {
            emit analysisFailed("Invalid analysis result format.");
            m_process->deleteLater();
            m_process = nullptr;
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

        AnalysisSnapshot snapshot;
        snapshot.timestamps = std::move(timestamps);
        snapshot.values = std::move(values);
        emit analysisFinished(snapshot);
    }

    m_process->deleteLater();
    m_process = nullptr;
}
