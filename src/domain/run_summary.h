/**
 * @file run_summary.h
 * @brief 前处理-完成页证据摘要事实计算（纯函数 domain，无 UI/无 IO，可 headless 单测）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-20
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * v1.12.0（2026-08-20 拍板：完成页证据清单加强——只做 A 卡，默认展开；
 * 缺口逐条列出缺了哪段墙钟时间）。
 *
 * 口径与 CalibrationService::writeSidecar 一致（完成页数字必须与产物
 * sidecar/报告互证，取证工具三处一致原则）：
 * - 段速率 = (ocrEndMs − startMs) / ocrEndFrameRelMs（缺尾帧证据 → 1.0）；
 * - 段墙钟跨度 = rate × (durationMs − 修剪量)；
 * - 整段丢弃（完全重叠）不计入覆盖、不更新前段参照止点；
 * - 缺口 = 后段墙钟起 − 前段墙钟止 > 2s 容差（kContinuityToleranceMs）。
 * 已知偏差：转码失败剔除/转码件实测时长在本层不可见（窗口/协调器口径），
 * 罕见路径下缺口边界可能与 sidecar 差一个 sub-second 级。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>
#include "sort_model.h"
#include "overlap_cut.h"

struct RunSummaryFacts {
    int segments = 0;           // 参与拼接的总段数（去重后、含被丢弃段）
    int ocrCount = 0;           // OCR 识别（含首尾交叉验证纠正段）
    int absStartCount = 0;      // 流内绝对时间
    int filenameCount = 0;      // 文件名时间
    int manualCount = 0;        // 人工手输
    int estimatedCount = 0;     // 夹缝插值/端点外延推算
    int unknownCount = 0;       // 无任何时间信息

    qint64 coverageStartMs = 0; // 全组最早墙钟起点（0=全批无墙钟）
    qint64 coverageEndMs = 0;   // 全组最晚墙钟止点（速率感知）

    struct Gap {                // 缺口（墙钟域）
        QString group;
        qint64 fromMs = 0;      // 缺口起 = 前段墙钟止
        qint64 toMs = 0;        // 缺口止 = 后段墙钟起
    };
    QVector<Gap> gaps;          // 按组内顺序

    int trimmedCount = 0;       // 重叠修剪段数（不含整段丢弃）
    qint64 trimmedStreamMs = 0; // 修剪流内总量（各段 keepStartMs 合计）
    QStringList droppedFiles;   // 整段丢弃（墙钟域完全包含）
};

/// 从排序结果 + 修剪计划计算完成页摘要事实（修剪计划可为空 = 未修剪）
RunSummaryFacts computeRunSummary(const QVector<SortGroup> &groups,
                                  const QVector<CutPlan> &cutPlans);
