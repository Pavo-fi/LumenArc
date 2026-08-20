/**
 * @file dedupe_plan.h
 * @brief 前处理-素材去重计划（纯函数 domain 逻辑，无 UI/无 IO，可 headless 单测）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-19
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 现场反馈（2026-08-19 广州越秀案）：监控导出一批素材常混入内容完全相同的
 * 重复段（同一录像被导出/拷贝多次），不去重会让拼接产物出现重复画面。
 *
 * 判定规则（与案件指纹登记同语义，SHA-256 内容指纹）：
 * - 尺寸不同 → 内容必然不同，直接保留（免哈希，大文件友好）；
 * - 尺寸相同的碰撞组 → SHA-256 一致才判重，排除后出现的副本；
 * - 指纹缺失/计算失败 → 保守保留（规范 C2：不静默丢数据）；
 * - 完全同路径 → 必判重（调用方路径过滤的兜底）。
 * 保留副本 = 输入顺序首个（确定性）。
 */
#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

struct DedupeEntry {
    QString filePath;
    qint64  size = -1;      ///< 文件字节数（必填；-1 = 未知，永不判重）
    QString sha256;         ///< 小写 hex；空 = 未算（仅同尺寸碰撞组需要）
};

struct DuplicatePair {
    QString filePath;       ///< 被排除的重复文件
    QString keptPath;       ///< 被保留的首个副本
};

struct DedupePlan {
    QStringList kept;                  ///< 去重后保留（保持输入相对序）
    QVector<DuplicatePair> duplicates; ///< 被排除清单（报告/留痕用）
};

/// 第一阶段：按尺寸预分组，返回需要计算 SHA-256 的文件路径
/// （仅同尺寸碰撞组；调用方完成 IO 哈希后回填 sha256）。
QStringList filesNeedingHash(const QVector<DedupeEntry> &entries);

/// 第二阶段：生成去重计划（规则见文件头）。
DedupePlan planDedupe(const QVector<DedupeEntry> &entries);
