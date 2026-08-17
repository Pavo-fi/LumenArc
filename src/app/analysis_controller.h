/**
 * @file analysis_controller.h
 * @brief 分析控制器（P-31 T3）：引擎/任务服务生命周期 + 注册表装配
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-17
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 自 MainWindow 抽出（行为冻结）：引擎实例构造（P-25 后恒 libav）、
 * TaskRegistry 注册（R8）、AnalysisTaskService 装配。UI 状态驱动（按钮/进度条）
 * 仍由 MainWindow 响应 taskService 信号完成（R1：本控制器不碰 widget）。
 */
#pragma once

#include <QObject>

class IAnalysisEngine;
class AnalysisTaskService;
class RoiModel;
class TimelineModel;

class AnalysisController : public QObject
{
    Q_OBJECT

public:
    explicit AnalysisController(RoiModel *roiModel, TimelineModel *timeline,
                                QObject *parent = nullptr);

    IAnalysisEngine *engine() const { return m_engine; }
    AnalysisTaskService *taskService() const { return m_service; }

private:
    IAnalysisEngine *m_engine = nullptr;        // 持有（parent 本对象）
    AnalysisTaskService *m_service = nullptr;   // 持有
};
