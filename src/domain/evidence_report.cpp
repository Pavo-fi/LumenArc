/**
 * @file evidence_report.cpp
 * @brief 证据报告 CSV 构建实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "evidence_report.h"
#include "preprocess_text.h"

#include <QDateTime>

namespace {

QString msToIso(qint64 epochMs)
{
    if (epochMs <= 0)
        return QString();
    return QDateTime::fromMSecsSinceEpoch(epochMs, Qt::LocalTime)
        .toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss.zzz"));
}

QString sourceText(OcrResult::Source s)
{
    switch (s) {
    case OcrResult::Ocr:    return QStringLiteral("OCR");
    case OcrResult::Manual: return QStringLiteral("Manual");
    default:                return QStringLiteral("None");
    }
}

} // namespace

QString buildEvidenceCsv(const EvidenceReportInput &input)
{
    using preprocess_text::csvEscape;
    QMap<QString, const ProbeResult *> probeByPath;
    for (const auto &p : input.probes)
        probeByPath.insert(p.filePath, &p);
    QMap<QString, const OcrResult *> ocrByPath;
    for (const auto &o : input.ocrs)
        ocrByPath.insert(o.filePath, &o);

    // 组内位置索引（序号/组/衔接警告）
    QMap<QString, int> orderIndex;
    QMap<QString, QString> groupName;
    QMap<QString, QString> linkWarn;
    for (const auto &g : input.groups) {
        for (int i = 0; i < g.ordered.size(); ++i) {
            orderIndex.insert(g.ordered[i].filePath, orderIndex.size());
            groupName.insert(g.ordered[i].filePath, g.channel);
        }
        for (const auto &w : g.warnings) {
            if ((w.type == SortWarningType::Overlap || w.type == SortWarningType::Gap)
                && w.indexB >= 0 && w.indexB < g.ordered.size()) {
                const QString &path = g.ordered[w.indexB].filePath;
                linkWarn[path] = linkWarn.value(path).isEmpty()
                    ? w.detail
                    : linkWarn[path] + QStringLiteral("; ") + w.detail;
            }
        }
    }

    QString out;
    out += QStringLiteral("\xEF\xBB\xBF");   // UTF-8 BOM（Excel 兼容）
    out += QStringLiteral(
        "序号,文件路径,SHA-256,组,首帧截图,尾帧截图,"
        "首帧OCR原始文本,首帧解析时间(派生),首帧依据,OCR置信度,"
        "尾帧OCR原始文本,尾帧解析时间(派生),尾帧依据,OCR置信度,"
        "文件名时间戳(原始),文件名解析时间(派生),creation_time(原始),"
        "流内起始墙钟(派生),"
        "时长(容器ms),时长(可信ms),衔接警告(派生),处理动作,输出文件\r\n");

    // 按排序结果顺序输出；未进入排序的（探测失败等）附在末尾
    QStringList allPaths;
    for (const auto &g : input.groups)
        for (const auto &e : g.ordered)
            allPaths << e.filePath;
    for (const auto &p : input.probes)
        if (!allPaths.contains(p.filePath))
            allPaths << p.filePath;

    int seq = 0;
    for (const QString &path : allPaths) {
        const ProbeResult *p = probeByPath.value(path, nullptr);
        const OcrResult *o = ocrByPath.value(path, nullptr);
        QStringList cols;
        cols << QString::number(++seq);
        cols << csvEscape(path);
        cols << (o ? o->sha256 : QString());
        cols << csvEscape(groupName.value(path));
        cols << csvEscape(o ? o->firstFrameImg : QString());
        cols << csvEscape(o ? o->lastFrameImg : QString());
        // 原始观测值逐字保留；派生值显式标注（§9.2）
        cols << csvEscape(o ? o->rawStartText : QString());
        cols << (o && o->wallStartMs > 0 ? msToIso(o->wallStartMs) : QString());
        cols << (o ? sourceText(o->source) : QString());
        cols << (o && o->wallStartMs > 0
                     ? QString::number(o->conf, 'f', 2) : QString());
        cols << csvEscape(o ? o->rawEndText : QString());
        cols << (o && o->wallEndMs > 0 ? msToIso(o->wallEndMs) : QString());
        cols << (o && o->wallEndMs > 0 ? sourceText(o->source) : QString());
        cols << (o && o->wallEndMs > 0
                     ? QString::number(o->conf, 'f', 2) : QString());
        cols << QString();   // 文件名时间戳原文（排序内部证据，见 HTML 报告明细）
        cols << QString();
        cols << csvEscape(p ? p->creationTimeRaw : QString());
        cols << (p && p->absStartEpochMs > 0 ? msToIso(p->absStartEpochMs)
                                             : QString());
        cols << (p && p->durationMs > 0 ? QString::number(p->durationMs) : QString());
        cols << (o && o->durationMs > 0 ? QString::number(o->durationMs) : QString());
        cols << csvEscape(linkWarn.value(path));
        cols << csvEscape(input.actions.value(path,
                QStringLiteral("未处理")));
        cols << csvEscape(input.outputs.value(path));
        out += cols.join(QLatin1Char(',')) + QStringLiteral("\r\n");
    }
    return out;
}
