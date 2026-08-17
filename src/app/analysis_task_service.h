/**
 * @file analysis_task_service.h
 * @brief 分析任务服务（P1a，落地 R7/R8）：显式状态机 + 引擎编排
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-17
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计（DEVELOPMENT_PLAN_V1.8_CN.md §3.2）：
 *  - 替代 MainWindow::AnalysisPhase 硬编码两阶段枚举（消技术债 P-32）；
 *  - 状态机：Idle → Running → Finished/Failed/Cancelled；
 *    引擎信号（progressUpdated/analysisFinished/analysisFailed）仅在
 *    Running 态被接受——cancel 之后迟到的 finished/failed 一律忽略
 *    （竞态 gating，替代旧版按错误文案判断取消，C1 收口）；
 *  - 完成合并策略（§3.3，通道化统一语义）：按任务 producedChannels
 *    逐通道覆盖，未产出通道保持现状（亮度完成保留既有 audio；
 *    audio-only 完成保留既有亮度）；
 *  - UI 中性信号：按钮/进度条/气泡的控件操作全部留在 MainWindow，
 *    本服务只发 taskId + 语义化数据（R1：app 层不碰 widget）。
 */
#pragma once

#include <QObject>
#include <QVector>
#include <QRect>
#include <QPolygon>
#include "domain/analysis_snapshot.h"

class IAnalysisEngine;
class TimelineModel;

class AnalysisTaskService : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisTaskService(IAnalysisEngine *engine, TimelineModel *model,
                                 QObject *parent = nullptr);

    /// 是否有任务在运行（转发引擎状态 + 状态机自持）
    bool isRunning() const;
    /// 当前运行中的任务 id（Idle 返回空）
    QString runningTaskId() const;

    /// 任务失败错误码（C1：错误用类型表达，不用字符串比较）
    static const QString kErrNoVideo;        ///< 未打开视频
    static const QString kErrPrecondition;   ///< 前置条件不满足（如无 ROI）
    static const QString kErrBusy;           ///< 已有任务运行中
    static const QString kErrUnknownTask;    ///< 未注册的任务 id
    static const QString kErrEngine;         ///< 引擎失败（detail 带原始错误）

public slots:
    /**
     * @brief 启动任务。返回 false = 未启动（前置失败/忙/未知任务），
     *        此时已发 taskFailed(code, detail) 供 UI 提示（行为与旧版
     *        onAnalyze/onAudioAnalysis 的 QMessageBox 提示链等价）。
     */
    bool start(const QString &taskId, const QString &videoPath,
               const QVector<QRect> &regions,
               const QVector<QPolygon> &polygons,
               const QVector<int> &rectRoiIds,
               const QVector<int> &polygonRoiIds);

    /// 取消当前任务：转发引擎 cancelAnalysis；此后迟到的引擎信号被忽略
    void cancel();

signals:
    void taskStarted(const QString &taskId);
    /// percent 恒为 0~100（Q6 拍板：音频不再映射 70~100）
    void taskProgress(const QString &taskId, qreal percent, const QString &detail);
    /// 任务成功：合并已完成（model 已更新），snapshot 为合并后的全量状态
    void taskFinished(const QString &taskId, const AnalysisSnapshot &snapshot);
    /// 任务失败：code 见 kErr*；detail 为用户可读说明（引擎原始错误/前置原因）
    void taskFailed(const QString &taskId, const QString &code, const QString &detail);
    /// 任务被取消（含取消后引擎迟到 failed 的归并）
    void taskCancelled(const QString &taskId);

private slots:
    void onEngineProgress(int analyzed, int total, qreal percent);
    void onEngineFinished(const AnalysisSnapshot &snapshot);
    void onEngineFailed(const QString &error);

private:
    enum class State { Idle, Running };
    void finishRun();   // 状态复位（保留 m_activeTask 供终态信号已发后查询）

    IAnalysisEngine *m_engine = nullptr;   // 不持有（MainWindow 生命周期）
    TimelineModel *m_model = nullptr;      // 不持有
    State m_state = State::Idle;
    QString m_activeTask;
    bool m_cancelRequested = false;
};
