/**
 * @file sort_model.h
 * @brief 前处理-智能排序数据模型（纯数据，无 Qt Widgets 依赖）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 契约见 docs/PREPROCESSING_TECH_DESIGN_CN.md §6.1。
 */
#pragma once

#include <QString>
#include <QVector>
#include "ocr_result.h"

enum class SortWarningType {
    Overlap,            // 相邻段重叠（deltaMs < 0）
    Gap,                // 相邻段缺口（deltaMs > 容差）
    EvidenceConflict,   // 证据①②冲突，已按连续性误差裁决
    LowConfidence,      // 排序依据置信度低
    DurationDubious,    // 时长存疑（截断文件）
    ManualInput         // 含人工手输时间戳
};

struct SortWarning {
    SortWarningType type;
    int     indexA = -1, indexB = -1;   // 组内序号（-1 = 与位置无关）
    qint64  deltaMs = 0;                // 重叠为负，缺口为正
    QString detail;
};

struct SortEntry {
    QString filePath;
    qint64  startMs = 0;            // 墙钟起点（排序依据，派生）
    qint64  endMs = 0;              // startMs + 时长
    qint64  durationMs = 0;
    OcrResult::Source startSource = OcrResult::None;
    double  conf = 0.0;
    QString thumbnailFirst, thumbnailLast;
};

struct SortGroup {
    QString channel;                // 通道/组名
    QVector<SortEntry> ordered;
    QVector<SortWarning> warnings;
    bool    suspicious = false;     // 必须人工确认
};
