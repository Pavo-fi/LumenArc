/**
 * @file smart_sorter.h
 * @brief 前处理-智能排序（纯函数 domain 逻辑，无 UI/无 IO，可 headless 单测）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计见 docs/PREPROCESSING_TECH_DESIGN_CN.md §5.3。
 * 证据层级：① OCR（像素级真相）> ② 文件名 > ③ creation_time > ④ mtime。
 * 矛盾裁决：两套候选分别排序，取连续性误差 Σ|Δ| 小者；无法裁决标存疑。
 */
#pragma once

#include <QVector>
#include <QMap>
#include "probe_result.h"
#include "ocr_result.h"
#include "sort_model.h"

/// 连续性容差（|Δ| ≤ 容差视为连续）
constexpr qint64 kContinuityToleranceMs = 2000;
/// 证据①②交叉验证容差（§5.2.5：偏差 > 容差则降置信 + WARN）
constexpr qint64 kCrossCheckToleranceMs = 120000;   // 2 min

QVector<SortGroup> smartSort(const QVector<ProbeResult> &probes,
                             const QVector<OcrResult> &ocrs,
                             const QMap<QString, QString> &channelOverrides = {});

/// 拖拽微调/人工改序后重算组内连续性警告（Overlap/Gap），保留其他类型警告
void recomputeContinuityWarnings(SortGroup &group);
