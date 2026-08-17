/**
 * @file analysis_task_service.cpp
 * @brief 分析任务服务实现（P1a 状态机 + 引擎编排 + 合并策略）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-17
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "analysis_task_service.h"
#include "domain/task_registry.h"
#include "domain/timeline_model.h"
#include "infrastructure/ianalysis_engine.h"
#include "i18n.h"

const QString AnalysisTaskService::kErrNoVideo = QStringLiteral("no_video");
const QString AnalysisTaskService::kErrPrecondition = QStringLiteral("precondition");
const QString AnalysisTaskService::kErrBusy = QStringLiteral("busy");
const QString AnalysisTaskService::kErrUnknownTask = QStringLiteral("unknown_task");
const QString AnalysisTaskService::kErrEngine = QStringLiteral("engine_error");

AnalysisTaskService::AnalysisTaskService(IAnalysisEngine *engine, TimelineModel *model,
                                         QObject *parent)
    : QObject(parent), m_engine(engine), m_model(model)
{
    connect(m_engine, &IAnalysisEngine::progressUpdated,
            this, &AnalysisTaskService::onEngineProgress);
    connect(m_engine, &IAnalysisEngine::analysisFinished,
            this, &AnalysisTaskService::onEngineFinished);
    connect(m_engine, &IAnalysisEngine::analysisFailed,
            this, &AnalysisTaskService::onEngineFailed);
}

bool AnalysisTaskService::isRunning() const
{
    return m_state == State::Running;
}

QString AnalysisTaskService::runningTaskId() const
{
    return isRunning() ? m_activeTask : QString();
}

bool AnalysisTaskService::start(const QString &taskId, const QString &videoPath,
                                const QVector<QRect> &regions,
                                const QVector<QPolygon> &polygons,
                                const QVector<int> &rectRoiIds,
                                const QVector<int> &polygonRoiIds)
{
    // 前置校验链（顺序与旧版 onAnalyze/onAudioAnalysis 一致，行为冻结）
    if (videoPath.isEmpty()) {
        emit taskFailed(taskId, kErrNoVideo,
                        lang("请先打开一个视频文件。", "Please open a video file first."));
        return false;
    }
    const AnalysisTaskDesc *desc = TaskRegistry::instance().find(taskId);
    if (!desc) {
        emit taskFailed(taskId, kErrUnknownTask,
                        lang("未知分析任务：%1", "Unknown analysis task: %1").arg(taskId));
        return false;
    }
    if (desc->preconditionError) {
        const QString err = desc->preconditionError();
        if (!err.isEmpty()) {
            emit taskFailed(taskId, kErrPrecondition, err);
            return false;
        }
    }
    if (isRunning() || (m_engine && m_engine->isRunning())) {
        emit taskFailed(taskId, kErrBusy,
                        lang("分析正在运行中。", "Analysis is already running."));
        return false;
    }

    m_state = State::Running;
    m_activeTask = taskId;
    m_cancelRequested = false;
    emit taskStarted(taskId);

    if (taskId == AnalysisChannels::luminance()) {
        m_engine->startAnalysis(videoPath, regions, polygons, {}, rectRoiIds, polygonRoiIds);
    } else if (taskId == AnalysisChannels::audio()) {
        m_engine->startAudioAnalysis(videoPath);
    } else {
        // 已注册但本版未接线引擎的任务（未来扩展点）：明确报错，不静默（C2）
        finishRun();
        emit taskFailed(taskId, kErrEngine,
                        lang("分析任务「%1」尚未接入引擎。", "Task \"%1\" has no engine binding yet.")
                            .arg(taskId));
        return false;
    }
    return true;
}

void AnalysisTaskService::cancel()
{
    if (m_state != State::Running)
        return;
    m_cancelRequested = true;
    if (m_engine)
        m_engine->cancelAnalysis();
    // 状态机立即回 Idle：此后引擎迟到的 finished/failed 一律忽略（竞态 gating，
    // 消灭"取消后旧结果写到新视频下"的 B6 竞态；引擎的 failed("cancelled") 到达时
    // 仅归并为一次 taskCancelled，不重复通知）
    finishRun();
    emit taskCancelled(m_activeTask);
}

void AnalysisTaskService::onEngineProgress(int analyzed, int total, qreal percent)
{
    if (m_state != State::Running)
        return;   // 迟到/非运行态信号：忽略
    const qreal pct = qBound(0.0, percent, 100.0);
    QString detail;
    if (m_activeTask == AnalysisChannels::luminance()) {
        detail = QString(lang("分析中 %1%（%2 个采样点）", "Analyzing %1% (%2 samples)"))
                     .arg(pct, 0, 'f', 1).arg(analyzed);
    } else {
        detail = QString(lang("音频分析阶段 %1/%2（%3%）", "Audio phase %1/%2 (%3%)"))
                     .arg(analyzed).arg(total).arg(pct, 0, 'f', 1);
    }
    emit taskProgress(m_activeTask, pct, detail);
}

void AnalysisTaskService::onEngineFinished(const AnalysisSnapshot &snapshot)
{
    if (m_state != State::Running)
        return;   // cancel 后迟到：忽略

    // 合并策略（§3.3 通道化统一语义）：按任务产出通道逐通道覆盖，未产出保持。
    // 亮度完成 → 保留既有 audio；audio-only 完成 → 保留既有亮度。
    AnalysisSnapshot merged = m_model->snapshot();
    if (m_activeTask == AnalysisChannels::luminance()) {
        merged.setLuminance(snapshot.timestamps, snapshot.lumRows(), snapshot.lumEntries());
        if (snapshot.hasAudio())
            merged.setAudio(snapshot.audioData());
    } else if (m_activeTask == AnalysisChannels::audio()) {
        if (snapshot.hasLuminance())   // 引擎同时带回了亮度（理论上不发生，防御）
            merged.setLuminance(snapshot.timestamps, snapshot.lumRows(), snapshot.lumEntries());
        merged.setAudio(snapshot.audioData());
    }
    m_model->setSnapshot(merged);

    finishRun();
    emit taskFinished(m_activeTask, merged);
}

void AnalysisTaskService::onEngineFailed(const QString &error)
{
    if (m_state != State::Running)
        return;   // cancel 归并已发 taskCancelled，此处静默（C1：不做文案比较）

    finishRun();
    emit taskFailed(m_activeTask, kErrEngine, error);
}

void AnalysisTaskService::finishRun()
{
    m_state = State::Idle;
}
