/**
 * @file report_docx_builder.h
 * @brief P-28 报告模块：ReportData → DOCX 章节映射（模板=用户火灾报告模板）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-23
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 拍板口径：只出 DOCX；静态目录无页码；章节一~七重排+落款；
 * 人工撰写位留白（下划线）；远期 HTML 渲染器同吃 ReportData。
 */

#pragma once

#include "domain/report_data.h"
#include <QString>

class ReportDocxBuilder
{
public:
    /// 构建 DOCX 到 outPath；成功返回空串，失败返回错误描述
    static QString build(const ReportData &rd, const QString &outPath);
};
