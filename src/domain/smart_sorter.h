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

/// v1.12.0 首尾帧 OCR 交叉验证下限容差（越秀案实测：OSD 单位数字误读会使
/// 单端墙钟跳变 ±10s/±60s/±600s…，rate 出现 8.69/0.049 级异常）；实际容差
/// = max(本值, 尾帧流内位置 × 10%)——容忍真变速（抽帧/快放）段的合理分歧，
/// 又足以捕捉数字级误读
constexpr qint64 kHeadTailCheckFloorMs = 15000;

QVector<SortGroup> smartSort(const QVector<ProbeResult> &probes,
                             const QVector<OcrResult> &ocrs,
                             const QMap<QString, QString> &channelOverrides = {});

/// 拖拽微调/人工改序后重算组内连续性警告（Overlap/Gap），保留其他类型警告
void recomputeContinuityWarnings(SortGroup &group);

/// P-60 估算段数（sourceKind == Estimated：夹缝插值/端点外延推算位）
int estimatedCount(const SortGroup &group);

/// P-60 自动放行硬标准（方案 §4.3）：单分组 && 无阻断级警告 && 无存疑 &&
/// 估算段 ≤2 且占比 ≤20%。满足 → GO 可一键直通拼接（UserConfirm 可跳过）
/// v1.12.0（2026-08-20 拍板）：trimOverlap=true（默认修剪策略）时 Overlap
/// 不再阻断——重叠段将被自动修剪并留痕（Q-17：剪后段开头保前段完整）；
/// trimOverlap=false（用户高级选项「保留原样」）时 Overlap 恢复阻断语义
///（顺序需人工裁决）。
bool canAutoProceed(const QVector<SortGroup> &groups, bool trimOverlap);

/// P-60 问题清单（方案 §4.5 B 路径确认页的数据源）
struct SortProblem {
    enum Kind { Unidentified = 0,     // 未识别且未能推算归位的段
                OverlapPair = 1,      // 时间轴重叠对（阻断级）
                SuspiciousGroup = 2 };// 存疑组（证据不足/矛盾未裁决/大批未识别）
    int kind = Unidentified;
    int groupIndex = -1;
    int indexA = -1, indexB = -1;     // 组内下标（组级为 -1）
    QString fileA, fileB;             // 冗余路径（UI 直取缩略图；可空）
    QString detail;                   // 工程师细节（卡片副行；主文案 UI 组）
};
/// 规则：Overlap 警告 → OverlapPair 卡（trimOverlap=true 时豁免——默认修剪
/// 已自动处置并留痕，无需人工裁决）；未识别段（None/Mtime 且未推算）≤3 →
/// 逐段 Unidentified 卡，>3 → 单张 SuspiciousGroup 卡（框一段全批套用）；
/// 其余存疑原因 → SuspiciousGroup 卡。估算段（Estimated）是提示级不算问题。
QVector<SortProblem> collectSortProblems(const QVector<SortGroup> &groups,
                                         bool trimOverlap);
