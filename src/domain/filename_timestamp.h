/**
 * @file filename_timestamp.h
 * @brief 前处理-文件名时间戳/通道号解析（纯逻辑，可 headless 单测）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 模式与优先级见 docs/PREPROCESSING_TECH_DESIGN_CN.md §5.3.1。
 * 实现优先级 M1 > M5 > M2 > M3 > M4：M5（含毫秒）必须先于 M2，
 * 否则 "20240701_120000_500" 会被 M2 截胡丢失毫秒（设计意图：保留毫秒）。
 */
#pragma once

#include <QString>

struct FilenameTimestamp {
    qint64  epochMs = 0;      // 本地墙钟毫秒；0 = 未命中
    QString channel;          // 通道号捕获（如 "CH01"/"03"；可空）
    QString rawText;          // 命中原文（取证留档）
    int     patternId = 0;    // 1..5（M1..M5）；0 = 未命中

    bool hit() const { return patternId > 0; }
};

/// 解析文件名（basename，不含目录）。多模式优先级命中即止，值域校验同 OCR 侧。
FilenameTimestamp parseFilenameTimestamp(const QString &fileName);
