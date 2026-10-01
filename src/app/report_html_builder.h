/**
 * @file report_html_builder.h
 * @brief P-26 复活：ReportData → 离线单文件 HTML 报告（含时间轴可视化章节）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-30
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 与 ReportDocxBuilder 平级、同吃 ReportData（渲染器缝，P-28 拍板预留）。
 * 版式与交互在资源模板 report_template.html 里，本类只做「数据 → JSON → 套模板」：
 * 模板里的唯一占位符由本类替换为整份 JSON（占位符字面量见模板文件与 CONTRACT）。
 */
#pragma once

#include "domain/report_data.h"
#include "domain/timeline_events.h"

#include <QString>

struct HtmlBuildOptions {
    bool   singleFile = true;    ///< true=图片 base64 内嵌（交付单文件）；false=落 assets/
    int    thumbMaxEdge = 640;   ///< 缩略图长边（体积红线：100 节点原始内嵌约 70MB）
    int    thumbQuality = 82;    ///< 缩略图 JPEG 质量
    qint64 mergeWindowMs = 1000; ///< 图表标签与快照的归并窗口
    qint64 phaseGapMs = 20000;   ///< 时间轴阶段自动切分阈值
    int    maxPhases = 6;        ///< 阶段数上限（候选切点过多时只留间隔最大的几个）
    int    summaryMaxRows = 12;  ///< 速览表最多行数
    /// 人工阶段种子（非空则不用自动切分；来源：事件表 CSV 的阶段列等）
    QVector<TimelinePhaseSeed> phaseSeeds;
};

class ReportHtmlBuilder
{
public:
    /// 构建 HTML 到 outPath；成功返回空串，失败返回错误描述
    /// （与 ReportDocxBuilder::build 同口径）。多文件模式时图片写到
    /// <outPath 所在目录>/assets/。
    static QString build(const ReportData &rd, const QString &outPath,
                         const HtmlBuildOptions &opt = HtmlBuildOptions());

    /// 纯数据（不读图、不写文件）：ReportData → 模板 JSON。
    /// 图片引用退回文件名，images 恒为 null。供无头测试使用。
    static QByteArray buildJson(const ReportData &rd,
                                const HtmlBuildOptions &opt = HtmlBuildOptions());
};
