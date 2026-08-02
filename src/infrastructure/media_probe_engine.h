/**
 * @file media_probe_engine.h
 * @brief 前处理-视频探测引擎：libavformat 进程内轻量 demux 探测（不解码）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计见 docs/PREPROCESSING_TECH_DESIGN_CN.md §5.1。
 * 每文件一个探测任务跑在 QThreadPool（默认 4 线程），各自独立
 * AVFormatContext，无共享状态；信号一律 QueuedConnection 回 UI 线程（Q1）。
 * 内容嗅探探测格式（不按扩展名），与播放内核同源处理伪 MP4。
 */
#pragma once

#include <QObject>
#include <QStringList>
#include <QVector>
#include <QMutex>
#include <atomic>
#include "domain/probe_result.h"

class QThreadPool;

class MediaProbeEngine : public QObject
{
    Q_OBJECT

public:
    explicit MediaProbeEngine(QObject *parent = nullptr);
    ~MediaProbeEngine() override;

    /// 并行探测（QThreadPool）。进行中重复调用视为编程错误（忽略）。
    void probe(const QStringList &paths);
    void cancel();
    bool isRunning() const { return m_running.load(); }

    /// 单文件探测（同步，供内部任务与单测复用）。不抛异常。
    static ProbeResult probeOne(const QString &path);

signals:
    void probeProgress(int done, int total);
    void probeResultReady(const ProbeResult &result);
    void probeFinished(const QVector<ProbeResult> &results);
    void probeFailed(const QString &file, const QString &error);

private:
    friend class ProbeTask;
    void onTaskDone(const ProbeResult &result);

    QThreadPool *m_pool;
    QMutex m_resultsMutex;
    QVector<ProbeResult> m_results;
    std::atomic<int> m_done{0};
    std::atomic<int> m_total{0};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_cancelled{false};
};

Q_DECLARE_METATYPE(ProbeResult)
