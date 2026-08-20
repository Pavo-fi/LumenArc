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
    int     kind = SortEvidenceKind::None;   // SortEvidenceKind::*
    QString rawText;            // 取证原文
};

struct FileCtx {
    QString filePath;
    QString channel;
    qint64  durationMs = 0;
    bool    durationDubious = false;
    Evidence ocr, fname, absStart, creation, mtime;
    QString thumbFirst, thumbLast;
    QString rawStart, rawEnd;
    qint64  ocrEndMs = 0;
    qint64  ocrEndFrameRelMs = 0;   // 尾帧证据流内实测位置（首尾交叉验证用）
};

/// 权重最高且有值者优先；同权冲突取 conf 高者
const Evidence *bestEvidence(const FileCtx &c)
{
    const Evidence *best = nullptr;
    for (const Evidence *e : {&c.ocr, &c.fname, &c.absStart, &c.creation, &c.mtime}) {
        if (e->epochMs <= 0)
            continue;
        if (!best || e->weight > best->weight
            || (e->weight == best->weight && e->conf > best->conf))
            best = e;
    }
    return best;
}

/// OCR 与流内绝对时间的交叉校验（§5.2.5 同级偏差提示，不改序）
void appendCrossChecks(const FileCtx &c, SortGroup &g, int indexA)
{
    if (c.ocr.epochMs > 0 && c.absStart.epochMs > 0) {
        const qint64 dev = qAbs(c.ocr.epochMs - c.absStart.epochMs);
        if (dev > kCrossCheckToleranceMs) {
            SortWarning w;
            w.type = SortWarningType::EvidenceConflict;
            w.indexA = indexA;
            w.deltaMs = dev;
            w.detail = QStringLiteral(
                "画面识别与流内录制时间偏差 %1s，请人工核对")
                           .arg(dev / 1000.0, 0, 'f', 1);
            g.warnings.append(w);
        }
    }
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
            c.fname = {ft.epochMs, 0.8, 0.9, OcrResult::None,
                       SortEvidenceKind::Filename, ft.rawText};
        if (p.absStartEpochMs > 0)
            // 录像机固件写入的录制时刻（DHAV 等）：元数据级证据，
            // 权重大于容器 creation_time 标签、小于文件名与 OCR（§5.2.1）
            c.absStart = {p.absStartEpochMs, 0.6, 0.8, OcrResult::None,
                          SortEvidenceKind::AbsStart, QString()};
        if (p.creationTimeMs > 0)
            c.creation = {p.creationTimeMs, 0.5, 0.6, OcrResult::None,
                          SortEvidenceKind::Creation, p.creationTimeRaw};
        if (p.fileMtimeMs > 0)
            c.mtime = {p.fileMtimeMs, 0.2, 0.3, OcrResult::None,
                       SortEvidenceKind::Mtime, QString()};
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
        // 证据材料（截图/原文/尾帧墙钟）与首帧解析成败解耦：即使首帧 OCR
        // 解析失败，尾帧证据与原文仍可用于人工判读连续性（取证可见性）
        c.thumbFirst = o.firstFrameImg;
        c.thumbLast = o.lastFrameImg;
        c.rawStart = o.rawStartText;
        c.rawEnd = o.rawEndText;
        c.ocrEndMs = o.wallEndMs;
        c.ocrEndFrameRelMs = o.endFrameRelMs;
        if (o.wallStartMs > 0) {
            // 人工手输与 OCR 同为证据①；人工值 conf 视为 1.0（用户即真相）
            const double conf = o.source == OcrResult::Manual ? 1.0 : o.conf;
            c.ocr = {o.wallStartMs, 1.0, conf, o.source,
                     o.source == OcrResult::Manual ? SortEvidenceKind::Manual
                                                   : SortEvidenceKind::Ocr,
                     o.rawStartText};
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
        QVector<FileCtx> files = git.value();   // 可改：首尾裁决会修订证据
        SortGroup g;
        g.channel = git.key().isEmpty()
            ? QStringLiteral("(默认组)") : git.key();

        // --- v1.12.0 首尾帧 OCR 交叉验证（2026-08-20 越秀案实测实锤）---
        // OSD 单位数字误读会使首/尾一端墙钟跳变（rate 8.69/0.049 级异常），
        // 幻影跨度级联放大：错位排序 → 误修剪 → 健康段被「完全重叠」误丢弃。
        // 首帧 wallStart 与尾帧推算起点（尾帧墙钟 − 尾帧流内实测位置）是两路
        // 独立证据；分歧 > max(15s, 尾帧位置×10%) → 两套候选各排一次序，取全组
        // 连续性误差 Σ|Δ| 小者（与证据①②裁决同法）；平局保留首帧并标存疑。
        // 被否端弃用（防污染 sidecar 速率与修剪计算），原文留痕于警告 detail。
        if (files.size() > 1) {
            for (int fi = 0; fi < files.size(); ++fi) {
                FileCtx &c = files[fi];
                if (c.ocr.epochMs <= 0 || c.ocrEndMs <= 0
                    || c.ocrEndFrameRelMs <= 0)
                    continue;   // 缺一端或尾帧位置未知（bonus 帧）→ 无法交叉
                const qint64 tailImplied = c.ocrEndMs - c.ocrEndFrameRelMs;
                const qint64 dev = qAbs(tailImplied - c.ocr.epochMs);
                const qint64 tol = qMax<qint64>(kHeadTailCheckFloorMs,
                                                c.ocrEndFrameRelMs / 10);
                if (dev <= tol)
                    continue;   // 首尾一致（含真变速合理分歧）
                const qint64 headStart = c.ocr.epochMs;
                auto keyWith = [&](qint64 start) {
                    return std::function<qint64(const FileCtx &)>(
                        [&, start](const FileCtx &x) {
                            if (&x == &c)
                                return start;
                            const Evidence *e = bestEvidence(x);
                            return e ? e->epochMs
                                     : std::numeric_limits<qint64>::max();
                        });
                };
                const auto keyH = keyWith(headStart);
                const auto keyT = keyWith(tailImplied);
                const qint64 errH = continuityErrorWithKey(
                    files, orderBy(files, keyH), keyH);
                const qint64 errT = continuityErrorWithKey(
                    files, orderBy(files, keyT), keyT);
                SortWarning w;
                w.type = SortWarningType::EvidenceConflict;
                w.deltaMs = dev;
                if (errT < errH) {
                    // 尾帧更可信：起点改采尾帧推算，保留尾帧锚点
                    c.ocr.epochMs = tailImplied;
                    w.detail = QStringLiteral(
                        "首帧识别(%1)与尾帧推算(%2)矛盾，按连续性采用尾帧 "
                        "(Σ|Δ|: 首=%3ms, 尾=%4ms)")
                        .arg(c.rawStart, c.rawEnd).arg(errH).arg(errT);
                } else {
                    // 首帧保留；尾帧证据弃用（防速率/修剪污染）
                    c.ocrEndMs = 0;
                    c.ocrEndFrameRelMs = 0;
                    w.detail = QStringLiteral(
                        "首帧识别(%1)与尾帧推算(%2)矛盾，按连续性采用首帧 "
                        "(Σ|Δ|: 首=%3ms, 尾=%4ms)")
                        .arg(c.rawStart, c.rawEnd).arg(errH).arg(errT);
                    if (errH == errT)
                        g.suspicious = true;   // 无法裁决 → 强制人工确认
                }
                g.warnings.append(w);
            }
        }

        if (files.size() == 1) {
            // 单文件组：跳过排序逻辑，直接可拼接（§10.2）
            SortEntry e;
            e.filePath = files[0].filePath;
            const Evidence *ev = bestEvidence(files[0]);
            e.startMs = ev ? ev->epochMs : 0;
            e.durationMs = files[0].durationMs;
            e.endMs = e.startMs + e.durationMs;
            e.startSource = ev ? ev->source : OcrResult::None;
            e.sourceKind = ev ? ev->kind : SortEvidenceKind::None;
            e.conf = ev ? ev->conf : 0.0;
            e.thumbnailFirst = files[0].thumbFirst;
            e.thumbnailLast = files[0].thumbLast;
            e.rawStartText = files[0].rawStart;
            e.rawEndText = files[0].rawEnd;
            // v1.12.0 单文件组首尾交叉验证：无邻段可裁决，分歧超容差 →
            // 保留首帧（排序主证据）、弃用尾帧（防速率污染）并标存疑
            qint64 ocrEnd = files[0].ocrEndMs;
            qint64 ocrEndRel = files[0].ocrEndFrameRelMs;
            if (e.startMs > 0 && ocrEnd > 0 && ocrEndRel > 0) {
                const qint64 dev = qAbs(ocrEnd - ocrEndRel - e.startMs);
                const qint64 tol = qMax<qint64>(kHeadTailCheckFloorMs,
                                                ocrEndRel / 10);
                if (dev > tol) {
                    ocrEnd = 0;
                    ocrEndRel = 0;
                    g.suspicious = true;
                    SortWarning w;
                    w.type = SortWarningType::EvidenceConflict;
                    w.indexA = 0;
                    w.deltaMs = dev;
                    w.detail = QStringLiteral(
                        "首帧识别(%1)与尾帧推算(%2)矛盾（无邻段可裁决），"
                        "已保留首帧、弃用尾帧，请人工核对")
                        .arg(files[0].rawStart, files[0].rawEnd);
                    g.warnings.append(w);
                }
            }
            e.ocrEndMs = ocrEnd;
            e.ocrEndFrameRelMs = ocrEndRel;
            g.ordered.append(e);
            appendCrossChecks(files[0], g, 0);
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
        bool anyOcr = false, anyAbsStart = false, anyMtimeOnly = false;
        for (int pos = 0; pos < order.size(); ++pos) {
            const FileCtx &c = files[order[pos]];
            const Evidence *ev = bestEvidence(c);
            SortEntry e;
            e.filePath = c.filePath;
            e.startMs = ev ? ev->epochMs : 0;
            e.durationMs = c.durationMs;
            e.endMs = e.startMs + c.durationMs;
            e.startSource = ev ? ev->source : OcrResult::None;
            e.sourceKind = ev ? ev->kind : SortEvidenceKind::None;
            e.conf = ev ? ev->conf : 0.0;
            e.thumbnailFirst = c.thumbFirst;
            e.thumbnailLast = c.thumbLast;
            e.ocrEndMs = c.ocrEndMs;
            e.ocrEndFrameRelMs = c.ocrEndFrameRelMs;
            e.rawStartText = c.rawStart;
            e.rawEndText = c.rawEnd;
            g.ordered.append(e);
            appendCrossChecks(c, g, pos);

            if (c.ocr.epochMs > 0)
                anyOcr = true;
            if (c.absStart.epochMs > 0)
                anyAbsStart = true;
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

        // --- P-60 未识别段兜底（方案 §4.4，拍板 Q1A）：最优证据仅 mtime 级/
        // 无证据的段，存在唯一「时间空位」使前后连续 → 夹缝插入；无解/多解
        // → 端点外延（mtime 近端优先，无 mtime 放尾；首段时长越界只能放尾）。
        // 推算段标 Estimated（估算，提示级；占比参与 canAutoProceed 判定）。
        // 已推算段即时并入骨架，支持相邻多段链式推算。
        int unestimatedLeft = 0;
        {
            QVector<int> uid;              // 未识别段下标（ordered 内）
            QVector<SortEntry> skel;       // 骨架（mtime 级以上证据段）
            QMap<QString, qint64> mtimes;
            for (const FileCtx &c : files)
                if (c.mtime.epochMs > 0)
                    mtimes.insert(c.filePath, c.mtime.epochMs);
            for (int i = 0; i < g.ordered.size(); ++i) {
                const SortEntry &e = g.ordered[i];
                const bool identified = e.startMs > 0
                    && e.sourceKind != SortEvidenceKind::None
                    && e.sourceKind != SortEvidenceKind::Mtime;
                if (identified)
                    skel.append(e);
                else
                    uid.append(i);
            }
            if (!uid.isEmpty() && !skel.isEmpty()) {
                const auto byStart = [](const SortEntry &a, const SortEntry &b) {
                    return a.startMs < b.startMs;
                };
                std::stable_sort(skel.begin(), skel.end(), byStart);
                for (int i : uid) {
                    SortEntry e = g.ordered[i];
                    if (e.durationMs > 0) {
                        // 唯一空位探测：相邻骨架对间隙恰好容得下本段
                        qint64 slot = -1;
                        int slotCount = 0;
                        for (int s = 1; s < skel.size(); ++s) {
                            const qint64 gap = skel[s].startMs - skel[s - 1].endMs;
                            if (qAbs(gap - e.durationMs) <= kContinuityToleranceMs) {
                                slot = skel[s - 1].endMs;
                                ++slotCount;
                            }
                        }
                        if (slotCount == 1) {
                            e.startMs = slot;   // 夹缝插入
                        } else {
                            // 端点外延
                            const qint64 back = skel.last().endMs;
                            const qint64 front = skel.first().startMs - e.durationMs;
                            const qint64 mt = mtimes.value(e.filePath, 0);
                            e.startMs = (front > 0 && mt > 0
                                         && qAbs(mt - front) < qAbs(mt - back))
                                        ? front : back;
                        }
                        e.endMs = e.startMs + e.durationMs;
                        e.sourceKind = SortEvidenceKind::Estimated;
                        e.conf = 0.5;
                        skel.append(e);
                        std::stable_sort(skel.begin(), skel.end(), byStart);
                    } else {
                        ++unestimatedLeft;   // 时长未知无法推算：原位保留
                        skel.append(e);
                    }
                }
                g.ordered = skel;
                for (int pos = 0; pos < g.ordered.size(); ++pos) {
                    if (g.ordered[pos].sourceKind != SortEvidenceKind::Estimated)
                        continue;
                    SortWarning w;
                    w.type = SortWarningType::EstimatedPlacement;
                    w.indexA = pos;
                    w.detail = QStringLiteral("未识别到时间，位置为推算（请核对）");
                    g.warnings.append(w);
                }
            } else {
                unestimatedLeft = uid.size();
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
        if (!anyOcr && !anyAbsStart)
            g.suspicious = true;        // 无 OCR 且无流内时间 → 强制人工确认
        if (anyMtimeOnly && unestimatedLeft > 0)
            g.suspicious = true;        // 未识别段未能推算归位 → 人工确认
        out.append(g);
    }
    return out;
}

int estimatedCount(const SortGroup &group)
{
    int n = 0;
    for (const auto &e : group.ordered)
        if (e.sourceKind == SortEvidenceKind::Estimated)
            ++n;
    return n;
}

bool canAutoProceed(const QVector<SortGroup> &groups, bool trimOverlap)
{
    // P-60 拍板：Q5A 多机位（多分组）一律人工确认；Q1A 估算段 ≤2 且 ≤20%
    // v1.12.0（2026-08-20 拍板）：默认修剪策略下重叠自动处置（Q-17 留痕），
    // 不再阻断 GO 直通；用户选「保留原样」（trimOverlap=false）时恢复阻断
    if (groups.size() != 1)
        return false;
    const SortGroup &g = groups[0];
    if (g.suspicious)
        return false;
    for (const auto &w : g.warnings) {
        if (trimOverlap && w.type == SortWarningType::Overlap)
            continue;   // 默认修剪处置，不阻断
        if (isBlockingWarning(w.type))
            return false;
    }
    const int est = estimatedCount(g);
    const int total = g.ordered.size();
    return est <= 2 && est * 5 <= total;
}

QVector<SortProblem> collectSortProblems(const QVector<SortGroup> &groups,
                                         bool trimOverlap)
{
    QVector<SortProblem> out;
    for (int gi = 0; gi < groups.size(); ++gi) {
        const SortGroup &g = groups[gi];
        for (const auto &w : g.warnings) {
            if (w.type != SortWarningType::Overlap)
                continue;
            if (trimOverlap)
                continue;   // v1.12.0：默认修剪自动处置并留痕，不出问题卡
            SortProblem p;
            p.kind = SortProblem::OverlapPair;
            p.groupIndex = gi;
            p.indexA = w.indexA;
            p.indexB = w.indexB;
            if (w.indexA >= 0 && w.indexA < g.ordered.size())
                p.fileA = g.ordered[w.indexA].filePath;
            if (w.indexB >= 0 && w.indexB < g.ordered.size())
                p.fileB = g.ordered[w.indexB].filePath;
            p.detail = w.detail;
            out.append(p);
        }
        QVector<int> uid;   // 未识别且未推算归位的段
        for (int i = 0; i < g.ordered.size(); ++i) {
            const auto &e = g.ordered[i];
            if (e.sourceKind == SortEvidenceKind::None
                || e.sourceKind == SortEvidenceKind::Mtime)
                uid.append(i);
        }
        if (g.suspicious && uid.size() > 3) {
            // 大批量未识别：单张组级卡（框一段全批套用 / 确认按文件信息排）
            SortProblem p;
            p.kind = SortProblem::SuspiciousGroup;
            p.groupIndex = gi;
            p.fileA = g.ordered[uid.first()].filePath;   // 框选样本段
            p.detail = QStringLiteral("本组 %1 段未识别").arg(uid.size());
            out.append(p);
        } else {
            for (int i : uid) {
                SortProblem p;
                p.kind = SortProblem::Unidentified;
                p.groupIndex = gi;
                p.indexA = i;
                p.fileA = g.ordered[i].filePath;
                out.append(p);
            }
            if (g.suspicious) {
                SortProblem p;
                p.kind = SortProblem::SuspiciousGroup;
                p.groupIndex = gi;
                p.detail = QStringLiteral("存疑：证据不足或互相矛盾");
                out.append(p);
            }
        }
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
