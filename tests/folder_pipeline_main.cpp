/**
 * @file folder_pipeline_main.cpp
 * @brief 现场素材文件夹无头（headless）全流程测试驱动
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-19
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计目的：对任意素材文件夹复现前处理完整链路（无 GUI）：
 *   ① 排除重复文件   —— 大小预分组 + SHA-256 内容指纹（案件指纹同语义）
 *   ② 智能理顺时间顺序 —— 探测(MediaProbeEngine) + OSD OCR(TimestampOcrEngine)
 *                        + smartSort（证据层级 OCR>文件名>creation_time>mtime）
 *   ③ 转码拼接       —— 时间轴重叠修剪(planOverlapCuts, Q-17) +
 *                        filesNeedingTranscode 最小转码路由 + ConcatEngine
 *
 * 用法：
 *   lumenarc_folder_pipeline <素材文件夹> [输出目录] [选项]
 * 选项：
 *   --no-ocr      跳过 OCR（排序退化为文件名/creation_time/mtime 证据）
 *   --no-trim     不做时间轴重叠修剪
 *   --recursive   递归扫描子目录（默认仅顶层）
 *   --dry-run     只跑到路由计划为止（不转码、不拼接）
 *   --limit N     仅处理去重后的前 N 个文件（冒烟用）
 *
 * 退出码：0 成功；1 处理失败/校验超差；2 用法错误；3 OCR/排序证据不足（存疑）。
 */

#include "infrastructure/media_probe_engine.h"
#include "infrastructure/timestamp_ocr_engine.h"
#include "infrastructure/concat_engine.h"
#include "infrastructure/transcode_engine.h"
#include "app/calibration_service.h"
#include "domain/concat_precheck.h"
#include "domain/concat_naming.h"
#include "domain/dedupe_plan.h"
#include "domain/overlap_cut.h"
#include "domain/smart_sorter.h"

#include <QCoreApplication>
#include <QCollator>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QEventLoop>
#include <QMap>
#include <QSet>
#include <QTimer>
#include <QVector>
#include <algorithm>
#include <cstdio>

#ifdef Q_OS_WIN
#  include <windows.h>
#endif

namespace {

QStringList g_report;   // 报告行（同步打印 + 落盘）

void logLine(const QString &s)
{
    fprintf(stderr, "%s\n", s.toUtf8().constData());
    g_report << s;
}

QString fmtWall(qint64 ms)
{
    if (ms <= 0)
        return QStringLiteral("-");
    return QDateTime::fromMSecsSinceEpoch(ms, Qt::LocalTime)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString fmtDur(qint64 ms)
{
    return QStringLiteral("%1:%2:%3")
        .arg(ms / 3600000, 2, 10, QLatin1Char('0'))
        .arg(ms / 60000 % 60, 2, 10, QLatin1Char('0'))
        .arg(ms / 1000 % 60, 2, 10, QLatin1Char('0'));
}

const char *evidenceName(int kind)
{
    using namespace SortEvidenceKind;
    switch (kind) {
    case Ocr:       return "OCR";
    case Manual:    return "Manual";
    case Filename:  return "Filename";
    case AbsStart:  return "AbsStart";
    case Creation:  return "Creation";
    case Mtime:     return "Mtime";
    case Estimated: return "Estimated";
    default:        return "None";
    }
}

const char *warnName(SortWarningType t)
{
    switch (t) {
    case SortWarningType::Overlap:            return "Overlap";
    case SortWarningType::Gap:                return "Gap";
    case SortWarningType::EvidenceConflict:   return "EvidenceConflict";
    case SortWarningType::LowConfidence:      return "LowConfidence";
    case SortWarningType::DurationDubious:    return "DurationDubious";
    case SortWarningType::ManualInput:        return "ManualInput";
    case SortWarningType::EstimatedPlacement: return "EstimatedPlacement";
    }
    return "?";
}

/// SHA-256 内容指纹（与案件指纹 computeSha256 同算法同语义）
QString sha256File(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f))
        return QString();
    return QString::fromLatin1(h.result().toHex());
}

/// 输出路径自动避让（§5.5.2：禁止覆盖既有文件）
QString allocateOutput(const QString &dir, const QString &base)
{
    for (int i = 0;; ++i) {
        const QString name = i == 0 ? base + QStringLiteral(".mp4")
                                    : QStringLiteral("%1_%2.mp4").arg(base).arg(i + 1);
        const QString path = dir + QLatin1Char('/') + name;
        if (!QFile::exists(path))
            return path;
    }
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
#ifdef Q_OS_WIN
    SetConsoleOutputCP(CP_UTF8);
#endif

    // ---- 参数解析 ----
    QStringList positional;
    bool noOcr = false, noTrim = false, recursive = false, dryRun = false;
    int limit = 0;
    for (int i = 1; i < argc; ++i) {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == QLatin1String("--no-ocr"))         noOcr = true;
        else if (a == QLatin1String("--no-trim"))   noTrim = true;
        else if (a == QLatin1String("--recursive")) recursive = true;
        else if (a == QLatin1String("--dry-run"))   dryRun = true;
        else if (a == QLatin1String("--limit") && i + 1 < argc)
            limit = QString::fromLocal8Bit(argv[++i]).toInt();
        else positional << a;
    }
    if (positional.isEmpty()) {
        fprintf(stderr,
            "usage: %s <素材文件夹> [输出目录] [--no-ocr] [--no-trim] "
            "[--recursive] [--dry-run] [--limit N]\n", argv[0]);
        return 2;
    }
    const QDir srcDir(positional[0]);
    if (!srcDir.exists()) {
        fprintf(stderr, "source dir not found: %s\n",
                positional[0].toUtf8().constData());
        return 2;
    }
    const QString outDirPath = positional.size() > 1
        ? positional[1]
        : srcDir.absoluteFilePath(QStringLiteral("LumenArc_Headless_%1")
              .arg(QDateTime::currentDateTime().toString(
                  QStringLiteral("yyyyMMdd_HHmmss"))));
    QDir().mkpath(outDirPath);
    QDir().mkpath(outDirPath + QStringLiteral("/work"));

    logLine(QStringLiteral("== LumenArc 无头全流程测试 =="));
    logLine(QStringLiteral("素材目录: %1").arg(srcDir.absolutePath()));
    logLine(QStringLiteral("输出目录: %1").arg(outDirPath));

    // ---- ① 扫描 ----
    static const QSet<QString> kVideoExt = {
        QStringLiteral("mp4"),  QStringLiteral("dav"), QStringLiteral("avi"),
        QStringLiteral("wmv"),  QStringLiteral("flv"), QStringLiteral("ts"),
        QStringLiteral("mts"),  QStringLiteral("m2ts"), QStringLiteral("mov"),
        QStringLiteral("mkv"),  QStringLiteral("mpg"), QStringLiteral("mpeg"),
        QStringLiteral("3gp"),  QStringLiteral("webm")};
    QStringList files;
    if (recursive) {
        QDirIterator it(srcDir.absolutePath(), QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            if (kVideoExt.contains(it.fileInfo().suffix().toLower()))
                files << it.fileInfo().absoluteFilePath();
        }
    } else {
        const QFileInfoList entries =
            srcDir.entryInfoList(QDir::Files, QDir::Name);
        for (const QFileInfo &fi : entries)
            if (kVideoExt.contains(fi.suffix().toLower()))
                files << fi.absoluteFilePath();
        // 顶层之外的子目录素材提示跳过（默认不递归）
        const QFileInfoList dirs =
            srcDir.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QFileInfo &d : dirs)
            logLine(QStringLiteral("[SCAN] 跳过子目录（--recursive 可纳入）: %1")
                        .arg(d.fileName()));
    }
    // 自然序（2.mp4 < 10.mp4），保证“保留首个”确定性
    QCollator collator;
    collator.setNumericMode(true);
    std::sort(files.begin(), files.end(),
              [&](const QString &a, const QString &b) {
                  return collator.compare(QFileInfo(a).fileName(),
                                          QFileInfo(b).fileName()) < 0;
              });
    logLine(QStringLiteral("[SCAN] 发现视频文件 %1 个").arg(files.size()));
    if (files.isEmpty()) {
        logLine(QStringLiteral("[SCAN] 无可处理视频，结束"));
        return 2;
    }

    // ---- ② 排除重复文件（domain/dedupe_plan：尺寸预分组，仅同尺寸碰撞组
    //      算 SHA-256 指纹；指纹一致判重、保留首个；哈希失败保守保留） ----
    logLine(QStringLiteral("---- 阶段1: 排除重复文件 ----"));
    QVector<DedupeEntry> dedupeEntries;
    dedupeEntries.reserve(files.size());
    for (const QString &f : files)
        dedupeEntries.append({f, QFileInfo(f).size(), QString()});
    {
        const QStringList needHash = filesNeedingHash(dedupeEntries);
        QMap<QString, QString> hashes;
        for (const QString &f : needHash)
            hashes.insert(f, sha256File(f));
        for (auto &e : dedupeEntries)
            e.sha256 = hashes.value(e.filePath);
    }
    const DedupePlan dedupe = planDedupe(dedupeEntries);
    for (const auto &d : dedupe.duplicates)
        logLine(QStringLiteral("[DEDUP] 重复排除: %1（SHA-256 与 %2 相同）")
                    .arg(QFileInfo(d.filePath).fileName(),
                         QFileInfo(d.keptPath).fileName()));
    const int dupCount = dedupe.duplicates.size();
    QStringList kept = dedupe.kept;
    logLine(QStringLiteral("[DEDUP] 保留 %1 个 / 排除重复 %2 个")
                .arg(kept.size()).arg(dupCount));
    if (limit > 0 && kept.size() > limit) {
        logLine(QStringLiteral("[LIMIT] 仅处理前 %1 个（冒烟模式）").arg(limit));
        kept = kept.mid(0, limit);
    }

    // ---- ③ 探测 ----
    logLine(QStringLiteral("---- 阶段2: 媒体探测 ----"));
    QVector<ProbeResult> probes;
    QMap<QString, ProbeResult> probeByFile;
    int probeFail = 0;
    for (const QString &f : kept) {
        const ProbeResult r = MediaProbeEngine::probeOne(f);
        probes.append(r);
        probeByFile.insert(f, r);
        if (!r.ok()) {
            ++probeFail;
            logLine(QStringLiteral("[PROBE] 失败: %1 — %2")
                        .arg(QFileInfo(f).fileName(), r.probeError));
        }
    }
    {
        QMap<QString, int> codecCnt, resCnt;
        QMap<double, int> fpsCnt;
        for (const auto &r : probes) {
            if (!r.ok()) continue;
            codecCnt[r.videoCodec] += 1;
            resCnt[QStringLiteral("%1x%2").arg(r.width).arg(r.height)] += 1;
            fpsCnt[qRound(r.fps * 10.0) / 10.0] += 1;
        }
        QStringList parts;
        for (auto it = codecCnt.constBegin(); it != codecCnt.constEnd(); ++it)
            parts << QStringLiteral("%1×%2").arg(it.key()).arg(it.value());
        for (auto it = resCnt.constBegin(); it != resCnt.constEnd(); ++it)
            parts << QStringLiteral("%1×%2").arg(it.key()).arg(it.value());
        for (auto it = fpsCnt.constBegin(); it != fpsCnt.constEnd(); ++it)
            parts << QStringLiteral("%1fps×%2").arg(it.key()).arg(it.value());
        logLine(QStringLiteral("[PROBE] 完成 %1 个（失败 %2）: %3")
                    .arg(probes.size()).arg(probeFail).arg(parts.join(QStringLiteral("、"))));
    }

    // ---- ④ OCR（证据①，像素级真相） ----
    QVector<OcrResult> ocrResults;
    if (!noOcr) {
        logLine(QStringLiteral("---- 阶段3: OSD 时间戳 OCR ----"));
        TimestampOcrEngine ocr;
        QString availErr;
        if (!ocr.available(&availErr)) {
            logLine(QStringLiteral("[OCR] 引擎不可用，跳过（退化为次级证据）: %1")
                        .arg(availErr));
        } else {
            QEventLoop loop;
            QTimer guard;
            guard.setSingleShot(true);
            QObject::connect(&ocr, &TimestampOcrEngine::ocrProgress, &app,
                             [](int done, int total, const QString &cur) {
                                 fprintf(stderr, "\r[OCR] %d/%d %s          ",
                                         done, total,
                                         QFileInfo(cur).fileName().toUtf8().constData());
                             });
            QObject::connect(&ocr, &TimestampOcrEngine::ocrFinished, &loop,
                             [&](const QVector<OcrResult> &r) {
                                 ocrResults = r;
                                 loop.quit();
                             });
            QObject::connect(&ocr, &TimestampOcrEngine::ocrFailed, &app,
                             [](const QString &f, const QString &e) {
                                 fprintf(stderr, "\n[OCR] 单段失败（可继续）: %s — %s\n",
                                         QFileInfo(f).fileName().toUtf8().constData(),
                                         e.toUtf8().constData());
                             });
            QObject::connect(&ocr, &TimestampOcrEngine::engineError, &loop,
                             [&](PreprocessError, const QString &d) {
                                 fprintf(stderr, "\n[OCR] 引擎错误: %s\n",
                                         d.toUtf8().constData());
                                 loop.quit();
                             });
            QObject::connect(&guard, &QTimer::timeout, &loop, [&]() {
                fprintf(stderr, "\n[OCR] 超时守卫触发，中止等待\n");
                ocr.cancel();
                loop.quit();
            });
            QMap<QString, qint64> trusted;
            for (const auto &r : probes)
                if (r.ok() && r.durationMs > 0 && !r.durationDubious)
                    trusted.insert(r.filePath, r.durationMs);
            guard.start(30 * 60 * 1000);   // 大批量兜底 30min
            ocr.run(kept, outDirPath + QStringLiteral("/work/ocr"),
                    trusted, outDirPath + QStringLiteral("/work/ocr_evidence"),
                    false);
            loop.exec();
            guard.stop();
            int ocrOk = 0;
            for (const auto &r : ocrResults)
                if (r.wallStartMs > 0 && r.ocrError.isEmpty())
                    ++ocrOk;
            fprintf(stderr, "\n");
            logLine(QStringLiteral("[OCR] 完成：识别成功 %1 / %2")
                        .arg(ocrOk).arg(kept.size()));
        }
    } else {
        logLine(QStringLiteral("---- 阶段3: OCR 已禁用（--no-ocr） ----"));
    }

    // ---- ⑤ 智能理顺时间顺序 ----
    logLine(QStringLiteral("---- 阶段4: 智能排序 ----"));
    const QVector<SortGroup> groups = smartSort(probes, ocrResults);
    logLine(QStringLiteral("[SORT] 共 %1 个分组").arg(groups.size()));
    bool anyBlocking = false, anySuspicious = false;
    for (const auto &g : groups) {
        logLine(QStringLiteral("[SORT] 组「%1」%2 段 %3")
                    .arg(g.channel).arg(g.ordered.size())
                    .arg(g.suspicious ? QStringLiteral("【存疑】") : QString()));
        for (int i = 0; i < g.ordered.size(); ++i) {
            const auto &e = g.ordered[i];
            logLine(QStringLiteral("  %1. %2  起=%3  时长=%4  依据=%5  conf=%6")
                        .arg(i + 1, 3)
                        .arg(QFileInfo(e.filePath).fileName())
                        .arg(fmtWall(e.startMs), fmtDur(e.durationMs),
                             QLatin1String(evidenceName(e.sourceKind)))
                        .arg(e.conf, 0, 'f', 2));
        }
        for (const auto &w : g.warnings) {
            logLine(QStringLiteral("  WARN %1 delta=%2ms %3")
                        .arg(QLatin1String(warnName(w.type)))
                        .arg(w.deltaMs).arg(w.detail));
            // v1.12.0：默认修剪（!--no-trim）下重叠自动处置，不算阻断
            const bool handled = !noTrim && w.type == SortWarningType::Overlap;
            anyBlocking = anyBlocking || (isBlockingWarning(w.type) && !handled);
        }
        anySuspicious = anySuspicious || g.suspicious;
    }

    // ---- ⑥ 时间轴重叠修剪（Q-17：剪后段开头、保前段完整；完全包含→整段丢弃） ----
    QVector<CutPlan> cutPlans;
    QMap<QString, QPair<qint64, qint64>> trimRange;
    QSet<QString> droppedFiles;
    if (!noTrim) {
        logLine(QStringLiteral("---- 阶段5: 时间轴重叠修剪 ----"));
        for (const auto &g : groups) {
            QVector<WallSegment> segs;
            for (const auto &e : g.ordered) {
                if (e.startMs <= 0)
                    continue;   // 无墙钟段不可靠，跳过修剪
                WallSegment s;
                s.file = e.filePath;
                s.wallStartMs = e.startMs;
                s.wallEndMs = e.ocrEndMs > 0 ? e.ocrEndMs : e.endMs;
                s.streamMs = e.durationMs > 0 ? e.durationMs
                             : probeByFile.value(e.filePath).durationMs;
                if (s.wallEndMs <= s.wallStartMs)
                    s.wallEndMs = s.wallStartMs + s.streamMs;
                segs.append(s);
            }
            cutPlans += planOverlapCuts(segs);
        }
        for (const CutPlan &p : cutPlans) {
            if (p.dropped) {
                droppedFiles.insert(p.file);
                logLine(QStringLiteral("[TRIM] 完全重叠整段丢弃: %1")
                            .arg(QFileInfo(p.file).fileName()));
            } else if (p.trimmed) {
                trimRange.insert(p.file, {p.keepStartMs, p.keepEndMs});
                logLine(QStringLiteral("[TRIM] 剪开头: %1 保留流内 %2s 起")
                            .arg(QFileInfo(p.file).fileName())
                            .arg(p.keepStartMs / 1000.0, 0, 'f', 1));
            }
        }
        logLine(QStringLiteral("[TRIM] 修剪 %1 段 / 丢弃 %2 段")
                    .arg(trimRange.size()).arg(droppedFiles.size()));
        // 修剪后重叠物理移除：阻断级 Overlap 警告视为已解决
        anyBlocking = false;
    } else {
        logLine(QStringLiteral("---- 阶段5: 重叠修剪已禁用（--no-trim） ----"));
    }

    // ---- ⑦ 转码路由（只转确实需要的文件） + 统一参数 ----
    logLine(QStringLiteral("---- 阶段6: 转码路由 ----"));
    // 统一 CFR = 全局最大 avg fps（拼接前置要求；低帧率段插帧）
    float maxFps = 0.0f;
    for (const auto &r : probes)
        if (r.ok() && r.fps > maxFps)
            maxFps = r.fps;
    const float unifiedFps = qBound(1.0f, qRound(maxFps * 10.0f) / 10.0f, 60.0f);

    QStringList transcodeQueue;
    QMap<QString, bool> groupCopyAudio;
    for (const auto &g : groups) {
        QVector<ProbeResult> ordered;
        for (const auto &e : g.ordered)
            if (!droppedFiles.contains(e.filePath))
                ordered.append(probeByFile.value(e.filePath));
        const QStringList needTx = filesNeedingTranscode(ordered);
        for (const auto &e : g.ordered) {
            if (droppedFiles.contains(e.filePath))
                continue;
            if (needTx.contains(e.filePath) || trimRange.contains(e.filePath)) {
                if (!transcodeQueue.contains(e.filePath))
                    transcodeQueue << e.filePath;
            }
        }
        // 组级音频策略：全部 AAC 且参数一致 → 直拷；否则重编码
        bool uniform = true;
        const ProbeResult *first = nullptr;
        for (const auto &p : ordered) {
            if (!p.ok()) continue;
            if (!first) { first = &p; continue; }
            if (p.audioCodec != first->audioCodec
                || p.audioSampleRate != first->audioSampleRate
                || p.audioChannels != first->audioChannels) {
                uniform = false;
                break;
            }
        }
        groupCopyAudio.insert(g.channel,
            uniform && first && first->audioStreams > 0
            && first->audioCodec == QLatin1String("aac"));
    }
    logLine(QStringLiteral("[ROUTE] 需转码 %1 个 / 直接拼接 %2 个 / 丢弃 %3 个 / 统一CFR=%4fps")
                .arg(transcodeQueue.size())
                .arg(kept.size() - transcodeQueue.size() - droppedFiles.size())
                .arg(droppedFiles.size())
                .arg(unifiedFps));

    if (dryRun) {
        logLine(QStringLiteral("[DRY-RUN] 计划阶段结束，不执行转码/拼接"));
        for (const QString &f : transcodeQueue)
            logLine(QStringLiteral("  转码: %1").arg(QFileInfo(f).fileName()));
    } else {
    // ---- ⑧ 转码（串行，磁盘 IO 密集） ----
    logLine(QStringLiteral("---- 阶段7: 转码 ----"));
    QMap<QString, QString> transcoded;
    QStringList transcodeFailed;
    const QString txDir = outDirPath + QStringLiteral("/transcoded");
    if (!transcodeQueue.isEmpty())
        QDir().mkpath(txDir);
    int txIdx = 0;
    for (const QString &file : transcodeQueue) {
        ++txIdx;
        const ProbeResult p = probeByFile.value(file);
        // 组内分辨率不一致 → 归一到主导档位（跨相机混拼）
        QString channel;
        qint64 ws = 0, we = 0;
        QMap<QString, int> resCnt;
        for (const auto &g : groups)
            for (const auto &e : g.ordered) {
                if (droppedFiles.contains(e.filePath))
                    continue;
                if (e.filePath == file) {
                    channel = g.channel;
                    ws = e.startMs;
                    we = e.ocrEndMs > 0 ? e.ocrEndMs : e.endMs;
                }
                const ProbeResult &pp = probeByFile.value(e.filePath);
                if (pp.width > 0 && pp.height > 0)
                    ++resCnt[QStringLiteral("%1x%2").arg(pp.width).arg(pp.height)];
            }
        QString safe = channel;
        safe.replace(QRegularExpression(QStringLiteral(R"([\/:*?"<>|()（）])")),
                     QStringLiteral("_"));
        if (safe.isEmpty())
            safe = QStringLiteral("merged");
        const QString base = QFileInfo(
            autoOutputName(safe, QFileInfo(file).completeBaseName()
                                   + QStringLiteral("_lumen"), ws, we))
                .completeBaseName();

        TranscodeRequest req;
        req.input = file;
        req.output = allocateOutput(txDir, base);
        req.durationMs = p.durationMs;
        req.crf = 18;
        req.fps = unifiedFps;
        if (unifiedFps > 0.0f && unifiedFps <= 240.0f)
            req.keyframeInterval = qMax(1, qRound(2.0f * unifiedFps));
        req.deinterlace = p.fieldOrder > 1;
        req.copyAudio = groupCopyAudio.value(channel, false);
        req.losslessPcm = !req.copyAudio
            && p.audioCodec.startsWith(QLatin1String("pcm_"));
        req.audioSampleRate = p.audioSampleRate.toInt();
        req.audioChannels = p.audioChannels.toInt();
        if (resCnt.size() > 1) {
            QString bestKey;
            int bestCnt = 0;
            for (auto it = resCnt.constBegin(); it != resCnt.constEnd(); ++it)
                if (it.value() > bestCnt) { bestCnt = it.value(); bestKey = it.key(); }
            const int x = bestKey.indexOf(QLatin1Char('x'));
            req.outWidth = bestKey.left(x).toInt();
            req.outHeight = bestKey.mid(x + 1).toInt();
        }
        const auto tr = trimRange.value(file, {0, 0});
        if (tr.first > 0) {
            req.trimStartMs = tr.first;
            req.trimEndMs = tr.second;
            req.durationMs = qMax<qint64>(0, req.durationMs - tr.first);
        }

        logLine(QStringLiteral("[TX %1/%2] %3 -> %4%5%6")
                    .arg(txIdx).arg(transcodeQueue.size())
                    .arg(QFileInfo(file).fileName(), QFileInfo(req.output).fileName(),
                         req.copyAudio ? QStringLiteral("（音频直拷）")
                         : req.losslessPcm ? QStringLiteral("（PCM无损解压）") : QString(),
                         req.trimStartMs > 0
                             ? QStringLiteral("（修剪 %1s 起）")
                                   .arg(req.trimStartMs / 1000.0, 0, 'f', 1)
                             : QString()));

        TranscodeEngine eng;
        QEventLoop loop;
        bool okDone = false;
        QString failDetail;
        QObject::connect(&eng, &TranscodeEngine::finished, &loop,
                         [&](const QString &out) {
                             transcoded.insert(file, out);
                             okDone = true;
                             loop.quit();
                         });
        QObject::connect(&eng, &TranscodeEngine::failed, &loop,
                         [&](PreprocessError e, const QString &d) {
                             failDetail = QStringLiteral("err=%1 %2").arg(int(e)).arg(d);
                             loop.quit();
                         });
        eng.run(req);
        loop.exec();
        if (!okDone) {
            transcodeFailed << file;
            logLine(QStringLiteral("[TX] 失败（跳过该段拼接）: %1 — %2")
                        .arg(QFileInfo(file).fileName(), failDetail));
        }
    }
    logLine(QStringLiteral("[TX] 转码完成 %1 / 失败 %2")
                .arg(transcoded.size()).arg(transcodeFailed.size()));

    // ---- ⑨ 拼接 ----
    logLine(QStringLiteral("---- 阶段8: 拼接 ----"));
    QStringList outputs;
    QMap<QString, QString> groupOutputs;   // 组名 → 实际跑 concat 的产出
    qint64 totalExpectedMs = 0;
    for (const auto &g : groups) {
        QStringList orderedFiles;
        QVector<qint64> offsets;
        qint64 acc = 0;
        for (const auto &e : g.ordered) {
            if (droppedFiles.contains(e.filePath)
                || transcodeFailed.contains(e.filePath))
                continue;
            const QString src = transcoded.value(e.filePath, e.filePath);
            orderedFiles << src;
            const qint64 trim = trimRange.value(e.filePath, {0, 0}).first;
            qint64 d = e.durationMs > 0 ? e.durationMs
                       : probeByFile.value(e.filePath).durationMs;
            d = qMax<qint64>(0, d - trim);
            offsets.append(acc);
            acc += d;
        }
        if (orderedFiles.isEmpty()) {
            logLine(QStringLiteral("[CONCAT] 组「%1」无可拼接段，跳过").arg(g.channel));
            continue;
        }
        const QString outBase = concatOutputName(
            g.channel, g.ordered.first().filePath, g.ordered.last().filePath);
        const QString outPath = allocateOutput(outDirPath, outBase);
        totalExpectedMs += acc;

        if (orderedFiles.size() == 1
            && orderedFiles.first() == g.ordered.first().filePath
            && !trimRange.contains(g.ordered.first().filePath)) {
            // 单段且原样无损：直接登记源为产出（与协调器单文件捷径一致）
            logLine(QStringLiteral("[CONCAT] 组「%1」仅 1 段且无需处理，直接采用: %2")
                        .arg(g.channel, QFileInfo(orderedFiles.first()).fileName()));
            outputs << orderedFiles.first();
            continue;
        }

        logLine(QStringLiteral("[CONCAT] 组「%1」%2 段 -> %3（预计 %4）")
                    .arg(g.channel).arg(orderedFiles.size())
                    .arg(QFileInfo(outPath).fileName(), fmtDur(acc)));
        ConcatEngine eng;
        QEventLoop loop;
        bool okDone = false;
        QString failDetail;
        QObject::connect(&eng, &ConcatEngine::progress, &app,
                         [](int pct, const QString &) {
                             fprintf(stderr, "\r[CONCAT] %d%%   ", pct);
                         });
        QObject::connect(&eng, &ConcatEngine::finished, &loop,
                         [&](const QString &out) {
                             outputs << out;
                             groupOutputs.insert(g.channel, out);
                             okDone = true;
                             loop.quit();
                         });
        QObject::connect(&eng, &ConcatEngine::failed, &loop,
                         [&](PreprocessError e, const QString &d) {
                             failDetail = QStringLiteral("err=%1 %2").arg(int(e)).arg(d);
                             loop.quit();
                         });
        ConcatRequest req;
        req.orderedFiles = orderedFiles;
        req.outputPath = outPath;
        req.workDir = outDirPath + QStringLiteral("/work/concat");
        req.totalDurationMs = acc;
        req.normalizeTimestamps = false;   // 重叠已修剪，无归一化需求
        req.segmentOffsetsMs = offsets;
        eng.run(req);
        loop.exec();
        fprintf(stderr, "\r                          \r");
        if (!okDone)
            logLine(QStringLiteral("[CONCAT] 组「%1」失败: %2").arg(g.channel, failDetail));
    }

    // ---- ⑩ 产出校验 ----
    logLine(QStringLiteral("---- 阶段9: 产出校验 ----"));
    bool verifyOk = true;
    for (const QString &out : outputs) {
        const ProbeResult r = MediaProbeEngine::probeOne(out);
        if (!r.ok()) {
            logLine(QStringLiteral("[VERIFY] 产出探测失败: %1 — %2")
                        .arg(QFileInfo(out).fileName(), r.probeError));
            verifyOk = false;
            continue;
        }
        logLine(QStringLiteral("[VERIFY] %1  %2/%3 %4x%5 %6fps 时长=%7")
                    .arg(QFileInfo(out).fileName())
                    .arg(r.container, r.videoCodec)
                    .arg(r.width).arg(r.height)
                    .arg(r.fps, 0, 'f', 2)
                    .arg(fmtDur(r.durationMs)));
    }

    // ---- ⑪ 校时 sidecar + 锚点验证（v1.12.0：校时反映到产物时间轴） ----
    logLine(QStringLiteral("---- 阶段10: 校时 sidecar（分段锚点） ----"));
    for (const auto &g : groups) {
        const QString out = groupOutputs.value(g.channel);
        if (out.isEmpty())
            continue;
        // 与协调器 finalize 同口径：修剪/丢弃/失败剔除 + 转码段实测时长
        QMap<QString, qint64> trimStarts;
        QSet<QString> skips;
        QMap<QString, qint64> actualMs;
        for (const CutPlan &p : cutPlans) {
            if (p.dropped)
                skips.insert(p.file);
            else if (p.trimmed)
                trimStarts.insert(p.file, p.keepStartMs);
        }
        for (const QString &f : transcodeFailed)
            skips.insert(f);
        for (const auto &e : g.ordered) {
            if (skips.contains(e.filePath))
                continue;
            const QString part = transcoded.value(e.filePath);
            if (part.isEmpty())
                continue;
            const ProbeResult pr = MediaProbeEngine::probeOne(part);
            if (pr.ok() && pr.durationMs > 0)
                actualMs.insert(e.filePath, pr.durationMs);
        }
        QString serr;
        if (!CalibrationService::writeSidecar(out, g.ordered, &serr,
                                              trimStarts, skips, actualMs)) {
            logLine(QStringLiteral("[CAL] sidecar 写入失败: %1").arg(serr));
            verifyOk = false;
            continue;
        }
        // 读回验证：分段模式生效 + 前两个墙钟段锚点抽查（段内 0.5s 处）
        TimeCalibration cal;
        QString calWarn;
        if (!CalibrationService::loadSidecar(out, &cal, &calWarn)
            || !cal.piecewiseMode()) {
            logLine(QStringLiteral("[CAL] sidecar 读回失败或分段模式未生效: %1")
                        .arg(QFileInfo(out).fileName()));
            verifyOk = false;
            continue;
        }
        qint64 cursor = 0;
        int anchors = 0;
        for (const auto &e : g.ordered) {
            if (skips.contains(e.filePath))
                continue;
            const qint64 trim = trimStarts.value(e.filePath, 0);
            double rate = 1.0;
            if (e.startMs > 0 && e.ocrEndMs > e.startMs && e.durationMs > 0)
                rate = double(e.ocrEndMs - e.startMs) / double(e.durationMs);
            const qint64 kept = qMax<qint64>(0, e.durationMs - trim);
            if (kept <= 0)
                continue;
            const qint64 streamDur = actualMs.value(e.filePath, 0) > 0
                ? actualMs.value(e.filePath) : kept;
            if (e.startMs > 0 && anchors < 2) {
                const qint64 expect = e.startMs
                    + static_cast<qint64>(std::llround(rate * double(trim + 500)));
                const qint64 got = cal.wallMsOf(cursor + 500);
                const bool okA = qAbs(got - expect) <= 2000;
                logLine(QStringLiteral("[CAL] 锚点 %1 %2: 流内+%3s → %4（期望 %5）%6")
                            .arg(++anchors)
                            .arg(QFileInfo(e.filePath).fileName())
                            .arg((cursor + 500) / 1000)
                            .arg(fmtWall(got), fmtWall(expect))
                            .arg(okA ? QString() : QStringLiteral(" 【超差】")));
                if (!okA)
                    verifyOk = false;
            }
            cursor += streamDur;
        }
        logLine(QStringLiteral("[CAL] %1 分段校时已随附（%2 段锚点%3）")
                    .arg(QFileInfo(out).fileName())
                    .arg(cal.piecewise.segments.size())
                    .arg(calWarn.isEmpty()
                             ? QString()
                             : QStringLiteral("，%1").arg(calWarn)));
    }

    // ---- 汇总 ----
    logLine(QStringLiteral("==== 汇总 ===="));
    logLine(QStringLiteral("输入 %1 → 去重后 %2（排除重复 %3）→ 分组 %4")
                .arg(files.size()).arg(kept.size()).arg(dupCount).arg(groups.size()));
    logLine(QStringLiteral("转码 %1（失败 %2）/ 修剪 %3 / 丢弃重叠 %4 / 产出 %5 个文件")
                .arg(transcoded.size()).arg(transcodeFailed.size())
                .arg(trimRange.size()).arg(droppedFiles.size()).arg(outputs.size()));
    for (const QString &out : outputs)
        logLine(QStringLiteral("产出: %1").arg(out));

    const bool pass = verifyOk && !outputs.isEmpty() && transcodeFailed.isEmpty();
    logLine(pass ? QStringLiteral("RESULT: PASS") : QStringLiteral("RESULT: FAIL"));

    // 报告落盘
    QFile rep(outDirPath + QStringLiteral("/headless_pipeline_report.txt"));
    if (rep.open(QIODevice::WriteOnly | QIODevice::Text)) {
        rep.write(g_report.join(QStringLiteral("\n")).toUtf8());
        rep.close();
    }

    if (!pass)
        return 1;
    if (anyBlocking)
        return 3;   // 存在未修剪的阻断级重叠
    if (anySuspicious)
        return 3;   // 排序证据不足存疑
    return 0;
    } // !dryRun

    // dry-run 报告落盘
    QFile rep(outDirPath + QStringLiteral("/headless_pipeline_report.txt"));
    if (rep.open(QIODevice::WriteOnly | QIODevice::Text)) {
        rep.write(g_report.join(QStringLiteral("\n")).toUtf8());
        rep.close();
    }
    logLine(QStringLiteral("RESULT: DRY-RUN OK"));
    return 0;
}
