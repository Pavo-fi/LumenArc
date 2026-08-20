/**
 * @file run_summary.cpp
 * @brief 完成页证据摘要事实计算实现（口径见 run_summary.h 头注释）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-20
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "run_summary.h"
#include "smart_sorter.h"   // kContinuityToleranceMs

#include <QMap>
#include <QSet>
#include <cmath>

RunSummaryFacts computeRunSummary(const QVector<SortGroup> &groups,
                                  const QVector<CutPlan> &cutPlans)
{
    RunSummaryFacts f;

    // 修剪/丢弃查表
    QMap<QString, qint64> trimStart;
    QSet<QString> dropped;
    for (const CutPlan &p : cutPlans) {
        if (p.dropped)
            dropped.insert(p.file);
        else if (p.trimmed)
            trimStart.insert(p.file, p.keepStartMs);
    }
    f.droppedFiles = QStringList(dropped.begin(), dropped.end());
    f.trimmedCount = trimStart.size();
    for (auto it = trimStart.constBegin(); it != trimStart.constEnd(); ++it)
        f.trimmedStreamMs += it.value();

    for (const SortGroup &g : groups) {
        qint64 prevWallEnd = -1;   // 前段墙钟止（速率感知口径）
        for (const SortEntry &e : g.ordered) {
            ++f.segments;
            switch (e.sourceKind) {
            case SortEvidenceKind::Ocr:       ++f.ocrCount; break;
            case SortEvidenceKind::AbsStart:  ++f.absStartCount; break;
            case SortEvidenceKind::Filename:  ++f.filenameCount; break;
            case SortEvidenceKind::Manual:    ++f.manualCount; break;
            case SortEvidenceKind::Estimated: ++f.estimatedCount; break;
            default:                          ++f.unknownCount; break;
            }
            if (dropped.contains(e.filePath))
                continue;   // 整段丢弃：不入覆盖、不更新参照止点

            // 段速率（与 writeSidecar 同口径）：首尾 OCR 双锚点可估，否则 1.0
            double rate = 1.0;
            if (e.startMs > 0 && e.ocrEndMs > e.startMs && e.ocrEndFrameRelMs > 0)
                rate = double(e.ocrEndMs - e.startMs) / double(e.ocrEndFrameRelMs);
            const qint64 trim = trimStart.value(e.filePath, 0);
            const qint64 keptStreamMs = qMax<qint64>(0, e.durationMs - trim);
            if (e.startMs <= 0 || keptStreamMs <= 0)
                continue;   // 无墙钟段：无法定义覆盖/缺口（与 sidecar 一致跳过）
            // 修剪段墙钟起点后移 trim×rate（与 writeSidecar 同）
            const qint64 wallStart = e.startMs + static_cast<qint64>(
                std::llround(rate * double(trim)));
            const qint64 wallEnd = wallStart + static_cast<qint64>(
                std::llround(rate * double(keptStreamMs)));

            if (f.coverageStartMs == 0 || wallStart < f.coverageStartMs)
                f.coverageStartMs = wallStart;
            if (wallEnd > f.coverageEndMs)
                f.coverageEndMs = wallEnd;

            if (prevWallEnd > 0 && wallStart - prevWallEnd > kContinuityToleranceMs) {
                RunSummaryFacts::Gap gap;
                gap.group = g.channel;
                gap.fromMs = prevWallEnd;
                gap.toMs = wallStart;
                f.gaps.append(gap);
            }
            prevWallEnd = wallEnd;
        }
    }
    return f;
}
