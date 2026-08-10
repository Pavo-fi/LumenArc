/**
 * @file timestamp_ocr_engine.h
 * @brief 前处理-OSD 时间戳 OCR 引擎：probe_timestamps.py 子进程封装
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计见 docs/PREPROCESSING_TECH_DESIGN_CN.md §5.2 / §6.2。
 * 评审 R-2：python/ffmpeg 路径不出现在接口签名中（R4），引擎内部自解析
 * （findFfmpegPath / detectPythonPath 同款，python 路径支持注入模式）。
 * 协议同 analyze_video.py 一族（规范 P1）：stdout 单行 JSON，
 * stderr PROGRESS:/ERROR:/WARNING: 前缀行。
 */
#pragma once

#include <QObject>
#include <QStringList>
#include <QMap>
#include <QVector>
#include <QRectF>
#include <QByteArray>
#include "domain/ocr_result.h"
#include "domain/preprocess_task.h"
#include "domain/time_calibration.h"

class QProcess;
class QTimer;

class TimestampOcrEngine : public QObject
{
    Q_OBJECT

public:
    explicit TimestampOcrEngine(QObject *parent = nullptr);
    ~TimestampOcrEngine() override;

    /// python 路径注入（同 PythonAnalysisEngine 模式）；空 = 自动探测
    void setPythonExecutable(const QString &path);
    QString pythonExecutable() const;

    /// 依赖可用性（python + 脚本 + rapidocr 导入检测，结果缓存）
    bool available(QString *errorDetail = nullptr);

    /// 批量 OCR（单 Python 进程，脚本内多进程池）。
    /// trustedDurationsMs：可信时长表（键=文件路径，缺省项脚本 ffprobe 兜底）。
    /// evidenceDir：证据帧持久目录（空 = 临时目录，任务结束清理）。
    /// framesOnlyFiles：仅截取证据帧、跳过 OCR 推理的文件（流内绝对时间
    /// 已可信，识别仅徒增耗时——现场反馈②）。
    void run(const QStringList &paths, const QString &workDir,
             const QMap<QString, qint64> &trustedDurationsMs,
             const QString &evidenceDir, bool withSha256,
             const QStringList &framesOnlyFiles = {});

    /// 校时取样（V1 方案 §3.2）：单文件多位置，每位置 ±0.25s 候选帧投票，
    /// 产出每位置一个墙钟测点（TimeCalibration::Sample，relMs 为实测真值）。
    /// evidenceDir：证据帧持久目录（空 = 临时目录，任务结束清理）。
    /// 与 run() 互斥（isRunning 守卫）。
    void runAtPositions(const QString &path, const QVector<qint64> &positionsMs,
                        qint64 trustedDurationMs, const QString &evidenceDir,
                        const QRectF &roi = QRectF());
    void cancel();
    bool isRunning() const;

signals:
    void ocrProgress(int done, int total, const QString &currentFile);
    void ocrFinished(const QVector<OcrResult> &results);
    void ocrFailed(const QString &file, const QString &error);   // 可继续（人工兜底）
    void engineError(PreprocessError error, const QString &detail);
    void atPositionsFinished(const QVector<TimeCalibration::Sample> &samples);
    void atPositionsFailed(const QString &error);

private slots:
    void onReadyReadStderr();
    void onFinished(int exitCode);

private:
    void failAll(PreprocessError error, const QString &detail);

    QString m_pythonPath;
    QProcess *m_process = nullptr;
    QTimer *m_watchdog = nullptr;
    QByteArray m_stdoutBuf;
    QByteArray m_stderrBuf;
    int m_total = 0;
    bool m_cancelled = false;
    bool m_atMode = false;      // true = runAtPositions 取样模式（onFinished 分流）
    int m_availability = -1;    // -1 未检测 / 0 不可用 / 1 可用
    QString m_availError;
};
