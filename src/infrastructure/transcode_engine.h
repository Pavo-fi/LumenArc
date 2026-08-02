/**
 * @file transcode_engine.h
 * @brief 前处理-统一转码引擎：任意容器 → MP4/H.264/AAC（QProcess 封装）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计见 docs/PREPROCESSING_TECH_DESIGN_CN.md §5.5。
 * v1 纯软件编码（libx264 veryfast CRF18）；单文件粒度，队列由
 * Coordinator 串行编排（磁盘 IO 密集，串行更稳）。
 */
#pragma once

#include <QObject>
#include <QByteArray>
#include "domain/preprocess_task.h"

class QProcess;
class QTimer;

class TranscodeEngine : public QObject
{
    Q_OBJECT

public:
    explicit TranscodeEngine(QObject *parent = nullptr);
    ~TranscodeEngine() override;

    void run(const TranscodeRequest &req);
    void cancel();
    bool isRunning() const;

    /// 命令行参数构建（供单测断言参数面，v1 无硬编分支 §5.5.3）
    static QStringList buildArgs(const TranscodeRequest &req,
                                 const QString &tempOutput);

signals:
    void progress(int percent, const QString &file);
    void finished(const QString &outputPath);
    void failed(PreprocessError error, const QString &detail);

private slots:
    void onProgressLine();
    void onFinished(int exitCode);

private:
    QProcess *m_process = nullptr;
    QTimer *m_watchdog = nullptr;
    QByteArray m_stdoutBuf;
    QByteArray m_stderrBuf;
    QString m_tempOutput;      // 独立临时输出 → 成功后原子改名（§5.5.2）
    QString m_finalOutput;
    bool m_cancelled = false;
    int m_lastPercent = -1;
    qint64 m_durationMs = 0;
};
