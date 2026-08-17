/**
 * @file integrity_checker.h
 * @brief NAL 数据完整性快检（§45：转码/拼接失败后的诊断工具）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-17
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 原理：对每个文件跑 `ffmpeg -c:v copy -bsf:v h264_mp4toannexb -f null`——
 * mp4toannexb 需要解析 NAL 长度字段，moov 索引与 mdat 数据错位（损坏文件）
 * 会报 "Invalid NAL unit size" / "Error splitting the input into NAL units"。
 * 常规播放/seek 可容错跳过坏帧，但无损拼接（concat -c copy）必然中止——
 * 本检查用于定位具体哪些文件损坏。默认不自动开启（用户拍板 §45）。
 */
#pragma once

#include <QObject>
#include <QStringList>
#include <QVector>

class QProcess;

class NalIntegrityChecker : public QObject
{
    Q_OBJECT

public:
    struct Result {
        QString filePath;
        qint64  errorCount = 0;   ///< stderr 中 NAL 错误行数
    };

    explicit NalIntegrityChecker(QObject *parent = nullptr);
    ~NalIntegrityChecker() override;

    /// 逐文件串行检查（QProcess 队列）；ffmpeg 不可用 emit failed
    void check(const QStringList &files, const QString &ffmpegPath);
    void cancel();

signals:
    void fileChecked(const QString &file, int idx, int total);
    void finished(const QVector<Result> &results);
    void failed(const QString &detail);

private slots:
    void onProcFinished(int exitCode);

private:
    void startNext();

    QProcess *m_proc = nullptr;
    QStringList m_queue;
    QVector<Result> m_results;
    QString m_ffmpeg;
    QByteArray m_lastStderr;
    int m_idx = 0;
    bool m_cancelled = false;
};