/**
 * @file report_preflight.h
 * @brief P-28 批次③：报告生成前自检（纯函数，吃 ReportData 出检查项）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-23
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 拍板（2026-08-23）：直接生成前须自检 + 软件内补录。❌ 阻断生成，
 * ⚠️ 提示放行（报告相应位置留白/占位）。
 */
#pragma once

#include "report_data.h"

struct ReportPreflightItem {
    enum Level { Ok = 0, Warn = 1, Block = 2 };
    int     level = Ok;
    QString text;
};

/// 自检项推导（纯函数；rd 以 computeHashes=false 聚合即可）
inline QVector<ReportPreflightItem> reportPreflight(const ReportData &rd)
{
    QVector<ReportPreflightItem> items;
    auto add = [&](int level, const QString &t) {
        items.append({level, t});
    };

    if (rd.videos.isEmpty()) {
        add(ReportPreflightItem::Block, QStringLiteral("案件内无视频检材"));
        return items;
    }

    // ---- 逐路检查 ----
    int calibrated = 0;
    for (const ReportVideoRow &v : rd.videos) {
        const QString name = v.cameraLabel + QStringLiteral("（") + v.id
                             + QStringLiteral("）");
        if (!v.fileExists) {
            add(ReportPreflightItem::Block,
                QStringLiteral("%1 源文件缺失：%2").arg(name, v.filePath));
            continue;
        }
        if (!v.hasCalib) {
            add(ReportPreflightItem::Warn,
                QStringLiteral("%1 未校时——报告校准表该行将为「未校时」").arg(name));
        } else {
            ++calibrated;
            if (v.evidencePhotos.isEmpty())
                add(ReportPreflightItem::Warn,
                    QStringLiteral("%1 无校准证据帧存档——校准截图节将缺此路").arg(name));
        }
        if (v.shootDir.isEmpty())
            add(ReportPreflightItem::Warn,
                QStringLiteral("%1 拍摄方向未填（可下方补录）").arg(name));
    }
    if (calibrated == 0)
        add(ReportPreflightItem::Block,
            QStringLiteral("全部视频均未校时——报告无时间基准，请至少校时一路"));

    // ---- 案件级 ----
    if (rd.extraFields.value(QStringLiteral("report/reviewer")).isEmpty())
        add(ReportPreflightItem::Warn, QStringLiteral("审核人未填（可下方补录）"));
    if (rd.extraFields.value(QStringLiteral("report/approver")).isEmpty())
        add(ReportPreflightItem::Warn, QStringLiteral("批准人未填（可下方补录）"));
    if (rd.sitemapPng.isEmpty())
        add(ReportPreflightItem::Warn,
            QStringLiteral("监控点位图未绘制——报告二（三）节将为占位提示"));
    if (rd.nodes.isEmpty())
        add(ReportPreflightItem::Warn,
            QStringLiteral("无关键节点标签——分析过程章节将留空（可在图表打标签后重新生成）"));
    if (rd.concatRecords.isEmpty())
        add(ReportPreflightItem::Ok, QStringLiteral("本案无前处理拼接产物"));
    else
        add(ReportPreflightItem::Ok,
            QStringLiteral("前处理拼接记录 %1 条将列为证据").arg(rd.concatRecords.size()));
    for (const QString &t : rd.limitationNotes)
        add(ReportPreflightItem::Warn, t);

    if (items.isEmpty())
        add(ReportPreflightItem::Ok, QStringLiteral("各项检查通过"));
    return items;
}

/// 是否存在阻断项
inline bool reportPreflightBlocked(const QVector<ReportPreflightItem> &items)
{
    for (const auto &it : items)
        if (it.level == ReportPreflightItem::Block)
            return true;
    return false;
}
