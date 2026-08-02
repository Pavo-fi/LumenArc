/**
 * @file concat_engine.h
 * @brief 前处理-无损拼接引擎：concat demuxer + 流拷贝（QProcess 封装）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计见 docs/PREPROCESSING_TECH_DESIGN_CN.md §5.4。
 * 零画质损失（-c copy）；伪 MP4 兜底：-avoid_negative_ts make_zero /
 * -fflags +genpts；输出 faststart 即拖即播。
 */
#pragma once

#include <QObject>
#include <QByteArray>
#include "domain/preprocess_task.h"

class QProcess;
class QTimer;

class ConcatEngine : public QObject
{
    Q_OBJECT

public:
    explicit ConcatEngine(QObject *parent = nullptr);
    ~ConcatEngine() override;

    void run(const ConcatRequest &req);
    void cancel();
    bool isRunning() const;

    /// 生成 list.txt 内容（供证据留档与单测）
    static QString buildListFile(const QStringList &orderedFiles);

signals:
    void progress(int percent, const QString &file);
    void finished(const QString &outputPath);
    void failed(PreprocessError error, const QString &detail);

private slots:
    void onProgressLine();
    void onFinished(int exitCode);

private:
    void startConcat(const QStringList &files, const QString &outputPath);
    void cleanupPartial();

    QProcess *m_process = nullptr;
    QTimer *m_watchdog = nullptr;
    QByteArray m_stdoutBuf;
    QByteArray m_stderrBuf;
    ConcatRequest m_req;
    QString m_actualOutput;
    bool m_cancelled = false;
    int m_lastPercent = -1;
    // 时间戳归一化两段式：先逐段 remux，再拼接
    bool m_normalizing = false;
    QStringList m_normFiles;
    int m_normIndex = -1;
};
