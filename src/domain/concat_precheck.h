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
