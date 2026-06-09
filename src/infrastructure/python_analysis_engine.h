/**
 * @file python_analysis_engine.h
 * @brief Python + OpenCV 离线分析引擎，QProcess 异步执行
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include "ianalysis_engine.h"

class QProcess;

/**
 * @brief Python + OpenCV offline analysis engine.
 *
 * Runs analyze_video.py in a QProcess and parses JSON output.
 */
class PythonAnalysisEngine : public IAnalysisEngine
{
    Q_OBJECT

public:
    explicit PythonAnalysisEngine(QObject *parent = nullptr);
    ~PythonAnalysisEngine();

    void setPythonExecutable(const QString &path);
    QString pythonExecutable() const { return m_pythonPath; }
    void setScriptPath(const QString &path);

    void startAnalysis(const QString &videoPath, const QVector<QRect> &regions) override;
    void cancelAnalysis() override;
    bool isRunning() const override;

private slots:
    void onReadyRead();
    void onFinished(int exitCode);

private:
    QString m_pythonPath;
    QString m_scriptPath;
    QProcess *m_process = nullptr;
    QByteArray m_outputBuffer;
    QByteArray m_stderrBuffer;  // Accumulate stderr for error reporting
};
