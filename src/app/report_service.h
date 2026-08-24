/**
 * @file report_service.h
 * @brief P-28 报告模块：报告数据聚合服务
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-23
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include <functional>

#include "domain/report_data.h"

class CaseManager;
class VideoStateManager;

class ReportService
{
public:
    /// 从案件 + 视频状态聚合报告数据。computeHashes=false 跳过哈希补算
    /// （自检/补录对话框快开）；最终生成传 true（MD5+SHA-256 单遍同步算）。
    static ReportData collect(CaseManager *cm, VideoStateManager *vsm,
                              bool computeHashes = true);
    /// 带进度回调版（工作线程可跑：仅文件 IO/QProcess，不碰控件）：
    /// cb(阶段名, 0~1)；cb 返回 false = 用户取消（聚合提前返回，cancelled 置真）
    static ReportData collect(CaseManager *cm, VideoStateManager *vsm,
                              bool computeHashes,
                              const std::function<bool(const QString &, double)> &cb,
                              bool *cancelled = nullptr);
    /// 图表/曲线整段光栅渲染（GUI 线程专用——离屏控件；填 row.chartPng，
    /// 图存案内 reports/assets/）。五（三）光亮/烟气分析附图数据源。
    static void renderChartImages(CaseManager *cm, VideoStateManager *vsm,
                                  ReportData &rd);

};
