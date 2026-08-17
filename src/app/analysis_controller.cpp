/**
 * @file analysis_controller.cpp
 * @brief 分析控制器实现（P-31 T3；装配逻辑自 MainWindow 纯移动）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-17
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "analysis_controller.h"
#include "analysis_task_service.h"
#include "domain/task_registry.h"
#include "domain/roi_model.h"
#include "domain/timeline_model.h"
#include "infrastructure/libav_analysis_engine.h"
#include "i18n.h"

AnalysisController::AnalysisController(RoiModel *roiModel, TimelineModel *timeline,
                                       QObject *parent)
    : QObject(parent)
{
    // v1.8.0 P-25：libav 为唯一分析引擎（A/B 对拍基线，默认运行 2+ 版）
    m_engine = new LibavAnalysisEngine(this);

    // 任务注册表（R8）：前置条件闭包只捕 domain 模型，不捕 UI 控件（R1）
    auto &reg = TaskRegistry::instance();
    AnalysisTaskDesc lum;
    lum.taskId = AnalysisChannels::luminance();
    lum.displayNameZh = QStringLiteral("亮度分析");
    lum.displayNameEn = QStringLiteral("Luminance");
    lum.preconditionError = [roiModel]() -> QString {
        if (!roiModel || (roiModel->regionCount() == 0 && roiModel->polygonCount() == 0))
            return lang("请先在视频上绘制至少一个 ROI 区域。",
                        "Please draw at least one ROI on the video.");
        return QString();
    };
    lum.producedChannels = {AnalysisChannels::luminance()};
    reg.registerTask(lum);

    AnalysisTaskDesc aud;
    aud.taskId = AnalysisChannels::audio();
    aud.displayNameZh = QStringLiteral("音频分析");
    aud.displayNameEn = QStringLiteral("Audio");
    aud.producedChannels = {AnalysisChannels::audio()};
    reg.registerTask(aud);

    m_service = new AnalysisTaskService(m_engine, timeline, this);
}
