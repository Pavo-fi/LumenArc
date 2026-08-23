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

#include "domain/report_data.h"

class CaseManager;
class VideoStateManager;

class ReportService
{
public:
    /// 从案件 + 视频状态聚合报告数据（哈希单遍补算 MD5+SHA-256，同步带日志）
    static ReportData collect(CaseManager *cm, VideoStateManager *vsm);

};
