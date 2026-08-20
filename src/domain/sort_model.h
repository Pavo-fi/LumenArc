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

/// 排序证据类型（display 由 UI 依此映射文案，domain 不写界面字符串）
namespace SortEvidenceKind {
enum {
    None = 0,
    Ocr = 1,        // 画面 OSD 识别（像素级真相）
    Manual = 2,     // 人工看图输入
    Filename = 3,   // 文件名时间
    AbsStart = 4,   // 流内绝对起始墙钟（DHAV 等录像机固件写入）
    Creation = 5,   // 容器 creation_time 标签
    Mtime = 6,      // 文件修改时间
    Estimated = 7   // P-60 夹缝插值/端点外延推算位（估算，提示级）
};
}

enum class SortWarningType {
    Overlap,            // 相邻段重叠（deltaMs < 0）
    Gap,                // 相邻段缺口（deltaMs > 容差）
    EvidenceConflict,   // 证据①②冲突，已按连续性误差裁决
    LowConfidence,      // 排序依据置信度低
    DurationDubious,    // 时长存疑（截断文件）
    ManualInput,        // 含人工手输时间戳
    EstimatedPlacement  // P-60 位置为推算（未识别段的夹缝/端点外延）
};

/// P-60 警告分级（方案 §4.3 自动放行硬标准）：阻断级 = 时间轴重叠冲突
/// （两段互撞必须人工裁决；其余警告均为提示级——缺口是监控常态、
/// 低置信/估算/人工均有留痕，不阻断一键直通）
inline bool isBlockingWarning(SortWarningType t)
{
    return t == SortWarningType::Overlap;
}

struct SortWarning {
    SortWarningType type;
    int     indexA = -1, indexB = -1;   // 组内序号（-1 = 与位置无关）
    qint64  deltaMs = 0;                // 重叠为负，缺口为正
    QString detail;
};

struct SortEntry {
    QString filePath;
    qint64  startMs = 0;            // 墙钟起点（排序依据，派生）
    qint64  endMs = 0;              // startMs + 时长（推算值）
    qint64  ocrEndMs = 0;           // 尾帧 OCR 实测墙钟（0=无；证据性高于 endMs 推算）
    qint64  durationMs = 0;
    OcrResult::Source startSource = OcrResult::None;
    int     sourceKind = SortEvidenceKind::None;   // SortEvidenceKind::*（UI 文案映射）
    double  conf = 0.0;
    QString thumbnailFirst, thumbnailLast;
    QString rawStartText, rawEndText;   // OCR 原文（逐字保留，取证展示用）
    qint64  ocrEndFrameRelMs = 0;   // 尾帧证据的流内实测位置（速率换算分母；0=未知）
};

struct SortGroup {
    QString channel;                // 通道/组名
    QVector<SortEntry> ordered;
    QVector<SortWarning> warnings;
    bool    suspicious = false;     // 必须人工确认
};
