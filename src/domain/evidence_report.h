/**
 * @file evidence_report.h
 * @brief 前处理-证据报告 CSV 构建（纯逻辑 domain，可 headless 单测）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 列定义见 docs/PREPROCESSING_TECH_DESIGN_CN.md §9.2。
 * 取证原则：原始列逐字保留观测值；派生列显式标注；
 * 相机时钟偏差只报告不篡改（绝不静默修正）。
 */
#pragma once

#include <QString>
#include <QMap>
#include <QVector>
#include "probe_result.h"
#include "ocr_result.h"
#include "sort_model.h"

struct EvidenceReportInput {
    QVector<ProbeResult> probes;
    QVector<OcrResult> ocrs;
    QVector<SortGroup> groups;
    QMap<QString, QString> actions;     // file -> 处理动作（转码/拼接/未处理）
    QMap<QString, QString> outputs;     // file -> 输出文件
};

/// RFC4180 CSV（规范 F6），UTF-8 with BOM（Excel 兼容）。
QString buildEvidenceCsv(const EvidenceReportInput &input);
