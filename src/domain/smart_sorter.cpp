/**
 * @file smart_sorter.cpp
 * @brief 智能排序实现：证据分层 → 分组 → 组内排序 → 连续性校验 → 矛盾裁决
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "smart_sorter.h"
#include "filename_timestamp.h"

#include <QFileInfo>
#include <QMap>
#include <algorithm>

namespace {

struct Evidence {
    qint64  epochMs = 0;
    double  weight = 0.0;
    double  conf = 0.0;
    OcrResult::Source source = OcrResult::None;
    QString rawText;            // 取证原文
};

struct FileCtx {
    QString filePath;
    QString channel;
    qint64  durationMs = 0;
    bool    durationDubious = false;
    Evidence ocr, fname, creation, mtime;
    QString thumbFirst, thumbLast;
};

/// 权重最高且有值者优先；同权冲突取 conf 高者
const Evidence *bestEvidence(const FileCtx &c)
{
    const Evidence *best = nullptr;
    for (const Evidence *e : {&c.ocr, &c.fname, &c.creation, &c.mtime}) {
        if (e->epochMs <= 0)
            continue;
        if (!best || e->weight > best->weight
            || (e->weight == best->weight && e->conf > best->conf))
            best = e;
    }
    return best;
}

QVector<int> orderBy(const QVector<FileCtx> &files,
                     const std::function<qint64(const FileCtx &)> &key)
{
    QVector<int> idx(files.size());
    for (int i = 0; i < files.size(); ++i)
        idx[i] = i;
    std::stable_sort(idx.begin(), idx.end(), [&](int a, int b) {
        const qint64 ka = key(files[a]), kb = key(files[b]);
        if (ka != kb)
            return ka < kb;
        return files[a].filePath < files[b].filePath;   // 确定性次序
    });
    return idx;
}

/// 连续性误差总和：Σ|Δ|（Δ = B.start − A.end，含容差内的 0）
qint64 continuityError(const QVector<FileCtx> &files, const QVector<int> &order)
{
    qint64 sum = 0;
    for (int i = 1; i < order.size(); ++i) {
        const FileCtx &a = files[order[i - 1]];
        const FileCtx &b = files[order[i]];
        const Evidence *ea = bestEvidence(a);
        const qint64 aStart = ea ? ea->epochMs : 0;
        const qint64 aEnd = aStart + a.durationMs;
        // 注意：此处用"当前 best"估算，供裁决比较用，足够且确定
        const Evidence *eb = bestEvidence(b);
        const qint64 bStart = eb ? eb->epochMs : 0;
        sum += qAbs(bStart - aEnd);
    }
    return sum;
}

qint64 continuityErrorWithKey(const QVector<FileCtx> &files,
                              const QVector<int> &order,
                              const std::function<qint64(const FileCtx &)> &key)
{
    qint64 sum = 0;
    for (int i = 1; i < order.size(); ++i) {
        const FileCtx &a = files[order[i - 1]];
        const FileCtx &b = files[order[i]];
        sum += qAbs(key(b) - (key(a) + a.durationMs));
    }
    return sum;
}

} // namespace

QVector<SortGroup> smartSort(const QVector<ProbeResult> &probes,
                             const QVector<OcrResult> &ocrs,
                             const QMap<QString, QString> &channelOverrides)
{
    // --- 汇总每文件证据 ---
    QMap<QString, FileCtx> byPath;
    for (const ProbeResult &p : probes) {
        FileCtx c;
        c.filePath = p.filePath;
        c.durationMs = p.durationMs > 0 ? p.durationMs : 0;
        c.durationDubious = p.durationDubious;
        const FilenameTimestamp ft = parseFilenameTimestamp(
            QFileInfo(p.filePath).fileName());
        c.channel = ft.channel;
        if (ft.hit())
            c.fname = {ft.epochMs, 0.8, 0.9, OcrResult::None, ft.rawText};
        if (p.creationTimeMs > 0)
            c.creation = {p.creationTimeMs, 0.5, 0.6, OcrResult::None,
                          p.creationTimeRaw};
        if (p.fileMtimeMs > 0)
            c.mtime = {p.fileMtimeMs, 0.2, 0.3, OcrResult::None, QString()};
        byPath.insert(p.filePath, c);
    }
    for (const OcrResult &o : ocrs) {
        auto it = byPath.find(o.filePath);
        if (it == byPath.end()) {
            FileCtx c;
            c.filePath = o.filePath;
            it = byPath.insert(o.filePath, c);
        }
        FileCtx &c = it.value();
        if (o.wallStartMs > 0) {
            // 人工手输与 OCR 同为证据①；人工值 conf 视为 1.0（用户即真相）
            const double conf = o.source == OcrResult::Manual ? 1.0 : o.conf;
            c.ocr = {o.wallStartMs, 1.0, conf, o.source,
                     o.rawStartText};
            c.thumbFirst = o.firstFrameImg;
            c.thumbLast = o.lastFrameImg;
            // 尾帧交叉验证时长（durationMs 缺失时的兜底）
            if (c.durationMs <= 0 && o.durationMs > 0)
                c.durationMs = o.durationMs;
        }
    }

    // --- 分组（无通道信息 → 默认单组；人工覆盖优先） ---
    QMap<QString, QVector<FileCtx>> groups;
    for (const FileCtx &c : byPath) {
        const QString ov = channelOverrides.value(c.filePath);
        groups[ov.isEmpty() ? c.channel : ov].append(c);
    }

    QVector<SortGroup> out;
    for (auto git = groups.begin(); git != groups.end(); ++git) {
        const QVector<FileCtx> &files = git.value();
        SortGroup g;
        g.channel = git.key().isEmpty()
            ? QStringLiteral("(默认组)") : git.key();

        if (files.size() == 1) {
            // 单文件组：跳过排序逻辑，直接可拼接（§10.2）
            SortEntry e;
            e.filePath = files[0].filePath;
            const Evidence *ev = bestEvidence(files[0]);
            e.startMs = ev ? ev->epochMs : 0;
            e.durationMs = files[0].durationMs;
            e.endMs = e.startMs + e.durationMs;
            e.startSource = ev ? ev->source : OcrResult::None;
            e.conf = ev ? ev->conf : 0.0;
            e.thumbnailFirst = files[0].thumbFirst;
            e.thumbnailLast = files[0].thumbLast;
            g.ordered.append(e);
            if (!ev || ev->weight <= 0.2)
                g.suspicious = true;    // 仅有 mtime/无证据 → 人工确认
            out.append(g);
            continue;
        }

        // --- 组内排序（按最优证据） ---
        QVector<int> order = orderBy(files, [](const FileCtx &c) {
            const Evidence *e = bestEvidence(c);
            return e ? e->epochMs : std::numeric_limits<qint64>::max();
        });

        // --- 矛盾裁决（证据① vs ② 冲突时，连续性误差最小化） ---
        bool conflict = false;
        for (const FileCtx &c : files) {
            if (c.ocr.epochMs > 0 && c.fname.epochMs > 0
                && qAbs(c.ocr.epochMs - c.fname.epochMs) > kCrossCheckToleranceMs)
                conflict = true;
        }
        if (conflict) {
            auto keyOcr = [](const FileCtx &c) {
                return c.ocr.epochMs > 0 ? c.ocr.epochMs
                                         : std::numeric_limits<qint64>::max();
            };
            auto keyFname = [](const FileCtx &c) {
                return c.fname.epochMs > 0 ? c.fname.epochMs
                                           : std::numeric_limits<qint64>::max();
            };
            const QVector<int> orderOcr = orderBy(files, keyOcr);
            const QVector<int> orderFname = orderBy(files, keyFname);
            const qint64 errOcr = continuityErrorWithKey(files, orderOcr, keyOcr);
            const qint64 errFname = continuityErrorWithKey(files, orderFname, keyFname);
            if (errOcr <= errFname) {
                order = orderOcr;
            } else {
                order = orderFname;
                SortWarning w;
                w.type = SortWarningType::EvidenceConflict;
                w.detail = QStringLiteral(
                    "OCR 与文件名证据冲突，按连续性误差裁决采用文件名序 "
                    "(Σ|Δ|: OCR=%1ms, 文件名=%2ms)").arg(errOcr).arg(errFname);
                g.warnings.append(w);
            }
            if (errOcr == errFname)
                g.suspicious = true;    // 无法裁决 → 强制人工确认
        }

        // --- 组装 + 连续性校验 ---
        bool anyOcr = false, anyMtimeOnly = false;
        for (int pos = 0; pos < order.size(); ++pos) {
            const FileCtx &c = files[order[pos]];
            const Evidence *ev = bestEvidence(c);
            SortEntry e;
            e.filePath = c.filePath;
            e.startMs = ev ? ev->epochMs : 0;
            e.durationMs = c.durationMs;
            e.endMs = e.startMs + c.durationMs;
            e.startSource = ev ? ev->source : OcrResult::None;
            e.conf = ev ? ev->conf : 0.0;
            e.thumbnailFirst = c.thumbFirst;
            e.thumbnailLast = c.thumbLast;
            g.ordered.append(e);

            if (c.ocr.epochMs > 0)
                anyOcr = true;
            if (ev && ev->weight <= 0.2)
                anyMtimeOnly = true;
            if (ev && ev->conf < 0.8 && ev->weight >= 0.5) {
                SortWarning w;
                w.type = SortWarningType::LowConfidence;
                w.indexA = pos;
                w.detail = QStringLiteral("排序依据置信度低 (conf=%1)")
                               .arg(ev->conf, 0, 'f', 2);
                g.warnings.append(w);
            }
            if (c.durationDubious) {
                SortWarning w;
                w.type = SortWarningType::DurationDubious;
                w.indexA = pos;
                w.detail = QStringLiteral("时长存疑（可能为截断文件）");
                g.warnings.append(w);
            }
            if (ev && ev->source == OcrResult::Manual) {
                SortWarning w;
                w.type = SortWarningType::ManualInput;
                w.indexA = pos;
                w.detail = QStringLiteral("时间戳为人工手输");
                g.warnings.append(w);
            }
        }
        for (int pos = 1; pos < g.ordered.size(); ++pos) {
            const SortEntry &a = g.ordered[pos - 1];
            const SortEntry &b = g.ordered[pos];
            if (a.startMs <= 0 || b.startMs <= 0)
                continue;
            const qint64 delta = b.startMs - a.endMs;
            if (delta < 0) {
                SortWarning w;
                w.type = SortWarningType::Overlap;
                w.indexA = pos - 1;
                w.indexB = pos;
                w.deltaMs = delta;
                w.detail = QStringLiteral("与前段重叠 %1s")
                               .arg(-delta / 1000.0, 0, 'f', 1);
                g.warnings.append(w);
            } else if (delta > kContinuityToleranceMs) {
                SortWarning w;
                w.type = SortWarningType::Gap;
                w.indexA = pos - 1;
                w.indexB = pos;
                w.deltaMs = delta;
                w.detail = QStringLiteral("与前段缺口 %1s")
                               .arg(delta / 1000.0, 0, 'f', 1);
                g.warnings.append(w);
            }
        }
        if (!anyOcr)
            g.suspicious = true;        // 全组无 OCR → 强制人工确认
        if (anyMtimeOnly)
            g.suspicious = true;
        out.append(g);
    }
    return out;
}

void recomputeContinuityWarnings(SortGroup &group)
{
    // 保留非连续性类警告，重算 Overlap/Gap
    QVector<SortWarning> kept;
    for (const auto &w : group.warnings)
        if (w.type != SortWarningType::Overlap && w.type != SortWarningType::Gap)
            kept.append(w);
    group.warnings = kept;
    for (int pos = 1; pos < group.ordered.size(); ++pos) {
        const SortEntry &a = group.ordered[pos - 1];
        const SortEntry &b = group.ordered[pos];
        if (a.startMs <= 0 || b.startMs <= 0)
            continue;
        const qint64 delta = b.startMs - a.endMs;
        if (delta < 0) {
            SortWarning w;
            w.type = SortWarningType::Overlap;
            w.indexA = pos - 1;
            w.indexB = pos;
            w.deltaMs = delta;
            w.detail = QStringLiteral("与前段重叠 %1s").arg(-delta / 1000.0, 0, 'f', 1);
            group.warnings.append(w);
        } else if (delta > kContinuityToleranceMs) {
            SortWarning w;
            w.type = SortWarningType::Gap;
            w.indexA = pos - 1;
            w.indexB = pos;
            w.deltaMs = delta;
            w.detail = QStringLiteral("与前段缺口 %1s").arg(delta / 1000.0, 0, 'f', 1);
            group.warnings.append(w);
        }
    }
}
