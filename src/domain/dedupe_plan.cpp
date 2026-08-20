/**
 * @file dedupe_plan.cpp
 * @brief 前处理-素材去重计划实现（规则见 dedupe_plan.h 文件头）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-19
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#include "dedupe_plan.h"

#include <QMap>
#include <QSet>

QStringList filesNeedingHash(const QVector<DedupeEntry> &entries)
{
    QMap<qint64, int> sizeCnt;
    for (const auto &e : entries)
        if (e.size >= 0)
            ++sizeCnt[e.size];
    QStringList out;
    for (const auto &e : entries)
        if (e.size >= 0 && sizeCnt.value(e.size) > 1 && !out.contains(e.filePath))
            out << e.filePath;
    return out;
}

DedupePlan planDedupe(const QVector<DedupeEntry> &entries)
{
    DedupePlan plan;
    QSet<QString> seenPaths;             // 同路径兜底
    QSet<QString> seenFingerprint;       // "size:sha256"
    QMap<QString, QString> keptByFingerprint;
    for (const auto &e : entries) {
        if (e.filePath.isEmpty())
            continue;
        if (seenPaths.contains(e.filePath)) {
            plan.duplicates.append({e.filePath, e.filePath});
            continue;
        }
        // 指纹齐备（同尺寸碰撞组且哈希成功）才参与判重；否则保守保留
        const QString key = QStringLiteral("%1:%2").arg(e.size).arg(e.sha256);
        const bool canJudge = e.size >= 0 && !e.sha256.isEmpty();
        if (canJudge && seenFingerprint.contains(key)) {
            plan.duplicates.append({e.filePath, keptByFingerprint.value(key)});
            continue;
        }
        seenPaths.insert(e.filePath);
        if (canJudge) {
            seenFingerprint.insert(key);
            keptByFingerprint.insert(key, e.filePath);
        }
        plan.kept << e.filePath;
    }
    return plan;
}
