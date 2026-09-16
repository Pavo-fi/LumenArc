/**
 * @file live_recorder.h
 * @brief 直播录制器：调用外部 ffmpeg 以流拷贝方式无损落盘
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-11
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计说明：
 * - 现场录像机输出已是 H.264/H.265，"边收边转码"纯属浪费 CPU 且损画质，
 *   因此录制采用 `-c:v copy` 流拷贝：不解码、不重编码、与源画面逐比特一致。
 * - 音频按 AAC 转码（录像机常见 G.711/PCM 无法直接进 MP4）；无音轨时可选 map。
 * - 停止走 ffmpeg 交互命令 'q'（写 trailer 后正常退出），避免强杀产生坏文件。
 * - 容器由输出扩展名决定（.mp4 / .mkv）；MKV 对异常码流/异常退出更耐受。
 */
#pragma once

#include <QObject>
#include <QProcess>
#include <QString>

class LiveRecorder : public QObject
{
    Q_OBJECT

public:
    explicit LiveRecorder(QObject *parent = nullptr);
    ~LiveRecorder() override;

    /// 开始录制到 outPath（扩展名决定容器）。
    /// @return false 时 *error 给出可读原因（进程未启动/参数非法）
    bool start(const QString &url, const QString &outPath, bool transportTcp, QString *error);

    /// 请求优雅停止（异步；结果经 finished 信号报告）
    void stop();

    bool isRecording() const;
    QString outputPath() const { return m_outPath; }

    /// 默认录制目录（系统"视频"目录下 /LumenArc监控录制）
    static QString defaultRecordDir();

    /// 依据目录/通道/时间生成默认文件名（扩展名随 container 参数）
    static QString suggestFileName(const QString &host, int channel,
                                   const QString &container);

signals:
    void started(const QString &path);
    /// ok=false 时 message 为失败原因（stderr 摘要）
    void finished(const QString &path, bool ok, const QString &message);

private:
    void onFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void onErrorOccurred(QProcess::ProcessError error);
    void appendStderr();

    QProcess *m_proc = nullptr;
    QString m_outPath;
    QString m_errTail;
    bool m_stopRequested = false;
};
