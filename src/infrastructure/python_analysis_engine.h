/**
 * @file python_analysis_engine.h
 * @brief Python + OpenCV 离线分析引擎，QProcess 异步执行
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include "ianalysis_engine.h"
#include <QMap>

class QProcess;

/**
 * @brief Python + OpenCV offline analysis engine.
 *
 * Runs analyze_video.py in a QProcess and parses JSON output.
 * Supports multi-process analysis via --processes flag, multi-video merge
 * via --videos (B2), and integrated audio analysis (B1).
 */
class PythonAnalysisEngine : public IAnalysisEngine
{
    Q_OBJECT

public:
    struct VideoInfo {
        float fps = 30.0f;
        qint64 totalFrames = 0;
    };

    explicit PythonAnalysisEngine(QObject *parent = nullptr);
    ~PythonAnalysisEngine();

    void setPythonExecutable(const QString &path);
    QString pythonExecutable() const { return m_pythonPath; }
    void setScriptPath(const QString &path);
    void setNoiseReduction(qreal strength) { m_noiseReduction = strength; }
    qreal noiseReduction() const { return m_noiseReduction; }

    void startAnalysis(const QString &videoPath, const QVector<QRect> &regions,
                       const QVector<QPolygon> &polygons = {},
                       const QStringList &extraVideos = {},
                       const QVector<int> &rectRoiIds = {},
                       const QVector<int> &polygonRoiIds = {}) override;
    void startAudioAnalysis(const QString &videoPath);
    void cancelAnalysis() override;
    bool isRunning() const override;

    /// Get video FPS and total frames by calling analyze_video.py --check-fps
    VideoInfo getVideoInfo(const QString &videoPath);

    /// Find ffmpeg executable (bundled or system)
    static QString findFfmpegPath();

private slots:
    void onReadyReadStdout();
    void onReadyReadStderr();
    void onFinished(int exitCode);

private:
    QString m_pythonPath;
    QString m_scriptPath;
    QProcess *m_process = nullptr;
    QByteArray m_outputBuffer;
    QByteArray m_stderrBuffer;
    QMap<QString, VideoInfo> m_videoInfoCache;  // B7: cache to avoid repeated blocking calls
    qreal m_noiseReduction = 0.0;
    qreal m_lastProgressPct = 0.0;  // 进度单调化（startAnalysis/startAudioAnalysis 时归零）
    // Stored for current analysis run (populated in startAnalysis, used in onFinished)
    QVector<int> m_pendingRectRoiIds;
    QVector<int> m_pendingPolygonRoiIds;
};
