/**
 * @file concat_precheck.h
 * @brief 前处理-拼接前一致性校验（纯逻辑 domain，可 headless 单测）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 规则见 docs/PREPROCESSING_TECH_DESIGN_CN.md §5.4.2（含评审 R-7/R-8 修正）。
 */
#pragma once

#include <QString>
#include <QVector>
#include "probe_result.h"

enum class PrecheckLevel { Ok, Warn, Block };

struct PrecheckItem {
    QString checkName;
    PrecheckLevel level = PrecheckLevel::Ok;
    QString detail;
};

struct PrecheckResult {
    QVector<PrecheckItem> items;
    bool hasBlock() const;
    bool hasWarn() const;
};

/// 对同组有序文件做两两并集校验。输入为探测结果（有序）。
PrecheckResult concatPrecheck(const QVector<ProbeResult> &orderedGroup);

/// 逐文件转码判定（现场反馈：转码不是强制步骤，只转确实需要的文件）：
/// - 探测失败 / 编解码不在 MP4 白名单 → 该文件需要转码；
/// - 组内参数不一致（编码/分辨率/像素格式/音轨数/帧率大偏差）→ 整组转码
///   （无法无损拼接）；
/// - 其余 → 直接无损拼接。
/// 返回需要转码的文件路径。
QStringList filesNeedingTranscode(const QVector<ProbeResult> &orderedGroup);
