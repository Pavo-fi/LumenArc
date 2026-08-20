/**
 * @file preprocessing_coordinator.cpp
 * @brief 前处理流程编排：探测→OCR→排序→确认→校验→(转码)→拼接→报告
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "preprocessing_coordinator.h"
#include "calibration_service.h"

#include "infrastructure/ianalysis_engine.h"
#include "infrastructure/media_probe_engine.h"
#include "infrastructure/timestamp_ocr_engine.h"
#include "infrastructure/concat_engine.h"
#include "infrastructure/transcode_engine.h"
#include "domain/smart_sorter.h"
#include "domain/concat_naming.h"
#include "domain/evidence_report.h"

#include <QDateTime>
#include <QCollator>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QRunnable>
#include <QThreadPool>
#include <QStorageInfo>
#include <QTextStream>
#include <algorithm>

namespace {

QString nowTag()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss"));
}

QString tsLog()
{
    return QDateTime::currentDateTime().toString(QStringLiteral("HH:mm:ss.zzz"));
}

/// 可信时长计算任务（时长存疑文件，QThreadPool 执行，不阻塞 GUI，C4）
class TrustedDurationTask : public QRunnable
{
public:
    TrustedDurationTask(PreprocessingCoordinator *coord, IAnalysisEngine *engine,
                        const QStringList &dubiousFiles)
        : m_coord(coord), m_engine(engine), m_files(dubiousFiles) {}

    void run() override
    {
        QMap<QString, qint64> durations;
        for (const QString &f : m_files) {
            if (m_engine)
                durations.insert(f, m_engine->trustedDurationMs(f));
        }
        // functor 式投递（Q_ARG 无法接受含逗号的模板类型）
        QMetaObject::invokeMethod(m_coord,
                                  [c = m_coord, d = durations]() {
                                      c->onTrustedDurationsReady(d);
                                  },
                                  Qt::QueuedConnection);
    }

private:
    PreprocessingCoordinator *m_coord;
    IAnalysisEngine *m_engine;
    QStringList m_files;
};

} // namespace

PreprocessingCoordinator::PreprocessingCoordinator(QObject *parent)
    : QObject(parent)
    , m_probeEngine(new MediaProbeEngine(this))
    , m_ocrEngine(new TimestampOcrEngine(this))
    , m_concatEngine(new ConcatEngine(this))
    , m_transcodeEngine(new TranscodeEngine(this))
    , m_transcodeEngine2(new TranscodeEngine(this))   // v1.7.0 M3：组级并行 N=2
{
    qRegisterMetaType<QVector<ProbeResult>>();
    qRegisterMetaType<QVector<OcrResult>>();
    qRegisterMetaType<QVector<SortGroup>>();
    qRegisterMetaType<PreprocessReport>();

    connect(m_probeEngine, &MediaProbeEngine::probeProgress, this,
            [this](int done, int total) {
                emit progress(total > 0 ? done * 15 / total : 0,
                              QStringLiteral("探测 %1/%2").arg(done).arg(total));
            });
    connect(m_probeEngine, &MediaProbeEngine::probeFinished,
            this, &PreprocessingCoordinator::onProbeFinished);
    connect(m_probeEngine, &MediaProbeEngine::probeFailed, this,
            [this](const QString &file, const QString &err) {
                log(QStringLiteral("[%1] 探测失败 %2: %3").arg(tsLog(), file, err));
            });

    connect(m_ocrEngine, &TimestampOcrEngine::ocrProgress, this,
            [this](int done, int total, const QString &) {
                emit progress(15 + (total > 0 ? done * 35 / total : 0),
                              QStringLiteral("OCR %1/%2").arg(done).arg(total));
            });
    connect(m_ocrEngine, &TimestampOcrEngine::ocrFinished,
            this, &PreprocessingCoordinator::onOcrFinished);
    connect(m_ocrEngine, &TimestampOcrEngine::ocrFailed, this,
            [this](const QString &file, const QString &err) {
                log(QStringLiteral("[%1] OCR 失败 %2: %3（可人工手输兜底）")
                        .arg(tsLog(), file, err));
            });
    connect(m_ocrEngine, &TimestampOcrEngine::engineError,
            this, &PreprocessingCoordinator::onOcrEngineError);

    connect(m_transcodeEngine, &TranscodeEngine::progress, this,
            [this](int pct, const QString &) {
                const int total = qMax(1, m_transcodeQueue.size());
                const int done = m_transcoded.size() + m_transcodeFailed.size();
                const int abs = 50 + (done * 40 / total) + pct * 40 / total / 100;
                // 双引擎并发时完成事件交错会致绝对百分比倒退——单调钳位
                if (abs > m_lastTxPercent) {
                    m_lastTxPercent = abs;
                    emit progress(abs,
                                  QStringLiteral("转码 %1/%2 (%3%)")
                                      .arg(done + 1).arg(total).arg(pct));
                }
            });
    for (TranscodeEngine *eng : {m_transcodeEngine, m_transcodeEngine2}) {
        connect(eng, &TranscodeEngine::finished,
                this, &PreprocessingCoordinator::onTranscodeOneFinished);
        connect(eng, &TranscodeEngine::failed,
                this, &PreprocessingCoordinator::onTranscodeOneFailed);
    }

    connect(m_concatEngine, &ConcatEngine::progress, this,
            [this](int pct, const QString &) {
                const int total = qMax(1, m_concatQueue.size());
                const int idx = m_concatQueue.indexOf(m_currentConcatGroup);
                emit progress(90 + (idx * 10 / total) + pct * 10 / total / 100,
                              QStringLiteral("拼接 %1 (%2%)")
                                  .arg(m_currentConcatGroup).arg(pct));
            });
    connect(m_concatEngine, &ConcatEngine::finished,
            this, &PreprocessingCoordinator::onConcatOneFinished);
    connect(m_concatEngine, &ConcatEngine::failed,
            this, &PreprocessingCoordinator::onConcatOneFailed);
}

PreprocessingCoordinator::~PreprocessingCoordinator()
{
    cancel();
}

void PreprocessingCoordinator::setAnalysisEngine(IAnalysisEngine *engine)
{
    m_analysis = engine;
}

void PreprocessingCoordinator::beginWithAutoSort(const QStringList &files)
{
    m_autoSortAfterProbe = true;
    begin(files);
}

// ---------------------------------------------------------------------------
// 状态机
// ---------------------------------------------------------------------------
void PreprocessingCoordinator::setPhase(TaskPhase phase)
{
    if (m_phase == phase)
        return;
    m_phase = phase;
    log(QStringLiteral("[%1] 阶段 → %2").arg(tsLog()).arg(int(phase)));
    emit phaseChanged(phase);
}

void PreprocessingCoordinator::log(const QString &line)
{
    m_log.append(line);
    emit logLine(line);
}

void PreprocessingCoordinator::begin(const QStringList &files)
{
    if (m_phase != TaskPhase::Idle && m_phase != TaskPhase::Done
        && m_phase != TaskPhase::Failed && m_phase != TaskPhase::Cancelled)
        return;
    // 会话复位
    m_autoSortAfterProbe = false;
    m_roiRetryDone = false;   // P-60 ROI 自学习随会话复位（仅本次任务）
    m_reOcrBusy = false;
    m_learnedRoi = QRectF();
    m_trimOverlap = false;    // 修剪开关随会话复位（默认修剪由窗口逐会话应用）
    m_cutPlans.clear();
    m_files = files;
    m_probes.clear();
    m_ocrs.clear();
    m_groups.clear();
    m_prechecks.clear();
    m_channelOverrides.clear();
    m_groupCopyAudio.clear();   // 音频直拷组级策略（2026-08）
    m_transcodeQueue.clear();
    m_transcoded.clear();
    m_transcodeFailed.clear();
    m_concatQueue.clear();
    m_actions.clear();
    m_outputs.clear();
    m_concatOutputs.clear();
    m_log.clear();
    m_report = PreprocessReport{};

    m_evidenceDir = QDir::temp().absolutePath()
        + QStringLiteral("/LumenArc_Evidence_") + nowTag();
    QDir().mkpath(m_evidenceDir + QStringLiteral("/frames"));
    log(QStringLiteral("[%1] 会话开始，%2 个文件；证据目录 %3")
            .arg(tsLog()).arg(files.size()).arg(m_evidenceDir));

    setPhase(TaskPhase::Probing);
    m_probeEngine->probe(files);
}

void PreprocessingCoordinator::onProbeFinished(const QVector<ProbeResult> &results)
{
    if (m_phase != TaskPhase::Probing)
        return;
    for (const auto &r : results)
        m_probes.insert(r.filePath, r);
    emit probeDone(results);

    // 时长存疑文件 → 后台计算可信时长（C4：不阻塞 GUI）；健康文件用容器时长
    QStringList dubious;
    for (const auto &r : results)
        if (r.durationDubious)
            dubious << r.filePath;
    if (!dubious.isEmpty() && m_analysis) {
        log(QStringLiteral("[%1] %2 个文件时长存疑，计算可信时长…")
                .arg(tsLog()).arg(dubious.size()));
        QThreadPool::globalInstance()->start(
            new TrustedDurationTask(this, m_analysis, dubious));
        return;     // onTrustedDurationsReady 继续
    }
    onTrustedDurationsReady({});
}

QMap<QString, qint64> PreprocessingCoordinator::buildDurMap(
    const QMap<QString, qint64> &trusted) const
{
    // 组装时长表：可信时长优先，容器时长兜底
    QMap<QString, qint64> durMap;
    for (const QString &f : m_files) {
        qint64 d = trusted.value(f, 0);
        if (d <= 0)
            d = m_probes.value(f).durationMs;
        if (d > 0)
            durMap.insert(f, d);
    }
    return durMap;
}

void PreprocessingCoordinator::onTrustedDurationsReady(const QMap<QString, qint64> &durations)
{
    if (m_phase != TaskPhase::Probing)
        return;
    const QMap<QString, qint64> durMap = buildDurMap(durations);
    // 就绪：按导入顺序成组（自动排序为可选步骤，不强制——现场反馈）
    setPhase(TaskPhase::UserConfirm);
    buildListOrderGroups();
    logProbeStats();   // 帧率/编码/分辨率统计 + 统一帧率预告（2026-08）
    log(QStringLiteral("[%1] 探测完成，%2 个文件已按导入顺序就绪（可开始拼接或自动排序）")
            .arg(tsLog()).arg(m_files.size()));
    emit evidenceReady(m_groups);
    if (m_autoSortAfterProbe) {
        m_autoSortAfterProbe = false;
        runAutoSort();
    }
}

void PreprocessingCoordinator::logProbeStats()
{
    // 统计源素材帧率/编码/分辨率分布，并预告统一 CFR 帧率
    // （2026-08 人工测试：8fps/12.5fps 混排 concat 时间戳错位 → 尾段卡住）
    QMap<double, int> fpsCnt;
    QMap<QString, int> codecCnt, resCnt;
    float maxFps = 0.0f;
    int okN = 0;
    for (const auto &r : m_probes) {
        if (!r.ok())
            continue;
        ++okN;
        fpsCnt[qRound(r.fps * 10.0) / 10.0] += 1;
        codecCnt[r.videoCodec] += 1;
        resCnt[QStringLiteral("%1×%2").arg(r.width).arg(r.height)] += 1;
        if (r.fps > maxFps)
            maxFps = r.fps;
    }
    if (okN == 0)
        return;
    // 统一帧率 = 全局最大 avg fps（不丢帧；低帧率段重复帧差分≈0），
    // 上限 60 防异常元数据；PreprocessWindow 提示用同一公式
    m_unifiedFps = qBound(1.0f, qRound(maxFps * 10.0f) / 10.0f, 60.0f);
    log(QStringLiteral("[%1] 素材统计：%2 段 | 帧率分布：%3 | 编码：%4 | 分辨率：%5")
            .arg(tsLog()).arg(okN)
            .arg([&]() {
                QStringList parts;
                for (auto it = fpsCnt.constBegin(); it != fpsCnt.constEnd(); ++it)
                    parts << QStringLiteral("%1fps×%2").arg(it.key()).arg(it.value());
                return parts.join(QStringLiteral("、"));
            }())
            .arg([&]() {
                QStringList parts;
                for (auto it = codecCnt.constBegin(); it != codecCnt.constEnd(); ++it)
                    parts << QStringLiteral("%1×%2").arg(it.key()).arg(it.value());
                return parts.join(QStringLiteral("、"));
            }())
            .arg([&]() {
                QStringList parts;
                for (auto it = resCnt.constBegin(); it != resCnt.constEnd(); ++it)
                    parts << QStringLiteral("%1×%2").arg(it.key()).arg(it.value());
                return parts.join(QStringLiteral("、"));
            }()));
    if (fpsCnt.size() > 1)
        log(QStringLiteral("[%1] ⚠ 源素材帧率不统一，将统一按 %2fps（CFR）转码后拼接")
                .arg(tsLog()).arg(m_unifiedFps));
    else
        log(QStringLiteral("[%1] 源素材帧率统一：%2fps")
                .arg(tsLog()).arg(m_unifiedFps));
}

void PreprocessingCoordinator::buildListOrderGroups()
{
    // 文件名时间戳轻量自动排序（2026-08 人工反馈：素材文件名含明确时间
    // 时仍混乱；不依赖 OCR 的最简排序——`20260722-050041` 式解析）
    QStringList ordered = m_files;
    sortFilesByNameTime(ordered);
    m_groups.clear();
    SortGroup g;
    g.channel = QStringLiteral("(默认组)");
    for (const QString &f : ordered) {
        SortEntry e;
        e.filePath = f;
        e.durationMs = durationOf(f);
        e.startMs = 0;                 // 未识别：时间未知（未知段排末尾由用户拖拽定序）
        e.sourceKind = SortEvidenceKind::None;
        g.ordered.append(e);
    }
    g.suspicious = false;
    m_groups.append(g);
}

void PreprocessingCoordinator::sortFilesByNameTime(QStringList &files)
{
    // 解析 `20260722-050041` / `20260722_050041M` 式文件名时间戳；
    // 解析失败的文件按文件名排末尾（稳定序）。
    struct Item { QString file; QDateTime dt; QString name; };
    QVector<Item> items;
    items.reserve(files.size());
    static const QRegularExpression re(
        QStringLiteral(R"((\d{4})(\d{2})(\d{2})[-_]?(\d{2})(\d{2})(\d{2}))"));
    for (const QString &f : files) {
        const QString name = QFileInfo(f).completeBaseName();
        Item it{f, QDateTime(), name};
        const auto m = re.match(name);
        if (m.hasMatch()) {
            it.dt = QDateTime(QDate(m.captured(1).toInt(), m.captured(2).toInt(),
                                    m.captured(3).toInt()),
                              QTime(m.captured(4).toInt(), m.captured(5).toInt(),
                                    m.captured(6).toInt()));
        }
        items.append(it);
    }
    // 都无时间戳：按文件名自然序（v1.12.0 修复：数字感知——
    // 旧字典序会把 0,1,2… 排成 0,1,10,11…19,2…，2026-08-20 现场实证）
    QCollator natural;
    natural.setNumericMode(true);
    std::stable_sort(items.begin(), items.end(),
        [&natural](const Item &a, const Item &b) {
            if (a.dt.isValid() && b.dt.isValid())
                return a.dt < b.dt;
            if (a.dt.isValid() != b.dt.isValid())
                return a.dt.isValid();   // 有时间戳的排前
            return natural.compare(a.name, b.name) < 0;
        });
    files.clear();
    for (const auto &it : items)
        files.append(it.file);
    if (items.size() > 1 && items.first().dt.isValid())
        log(QStringLiteral("[%1] 已按文件名时间戳自动排序：%2 → %3")
                .arg(tsLog(),
                     QFileInfo(items.first().file).fileName(),
                     QFileInfo(items.last().file).fileName()));
}

void PreprocessingCoordinator::runAutoSort()
{
    if (m_phase != TaskPhase::UserConfirm)
        return;
    m_roiRetryDone = false;   // P-60 手动重跑：允许新一轮自学习
    m_reOcrBusy = false;
    // OCR 依赖检测（§10.2：缺失 → 降级流程 + 引导安装，不静默）
    QString availErr;
    setPhase(TaskPhase::Ocr);
    if (!m_ocrEngine->available(&availErr)) {
        log(QStringLiteral("[%1] OCR 引擎不可用：%2；降级为无 OCR 流程（人工兜底）")
                .arg(tsLog(), availErr));
        QVector<OcrResult> degraded;
        for (const QString &f : m_files) {
            OcrResult r;
            r.filePath = f;
            r.durationMs = durationOf(f);
            r.ocrError = QStringLiteral("engine_missing");
            degraded.append(r);
        }
        onOcrFinished(degraded);
        return;
    }
    const QMap<QString, qint64> durMap = buildDurMap({});
    // 流内绝对时间已可信的文件跳过 OCR 推理（现场反馈②：OCR 全失败的
    // DHAV 批次耗时 8.7min；仅截证据帧，排序依据 absStart）
    QStringList framesOnly;
    if (m_skipOcrWhenAbsStart) {
        for (const QString &f : m_files) {
            if (m_probes.value(f).absStartEpochMs > 0)
                framesOnly << f;
        }
        if (!framesOnly.isEmpty())
            log(QStringLiteral("[%1] %2 个文件已有流内录制时间，跳过画面识别"
                               "（仅截证据帧）").arg(tsLog()).arg(framesOnly.size()));
    }
    m_ocrEngine->run(m_files, m_evidenceDir, durMap,
                     m_evidenceDir + QStringLiteral("/frames"),
                     m_opts.withSha256, framesOnly);
}

void PreprocessingCoordinator::onOcrEngineError(PreprocessError error, const QString &detail)
{
    log(QStringLiteral("[%1] OCR 引擎错误（%2）：%3；降级为无 OCR 流程")
            .arg(tsLog()).arg(int(error)).arg(detail));
    if (m_phase != TaskPhase::Ocr)
        return;
    QVector<OcrResult> degraded;
    for (const QString &f : m_files) {
        OcrResult r;
        r.filePath = f;
        r.ocrError = detail;
        degraded.append(r);
    }
    onOcrFinished(degraded);
}

void PreprocessingCoordinator::onOcrFinished(const QVector<OcrResult> &results)
{
    if (m_phase != TaskPhase::Ocr)
        return;
    for (const auto &r : results)
        m_ocrs.insert(r.filePath, r);

    // P-60 同机位 ROI 自学习（方案 §4.2，拍板 Q3 仅本次任务）：首轮 OCR
    // 结束后从成功段学命中位置，给未识别段用窄 ROI 重试一轮（小图推理快
    // 且排除画面干扰文字）；第二轮回来 m_roiRetryDone=true 直通排序
    if (!m_roiRetryDone && !m_reOcrBusy) {
        QString learnedFrom;
        if (!m_learnedRoi.isValid()) {
            for (const auto &r : results) {
                if (r.wallStartMs > 0 && r.hitRoi.isValid()
                    && r.hitRoi.width() > 0.005 && r.hitRoi.height() > 0.003) {
                    m_learnedRoi = r.hitRoi;
                    learnedFrom = r.filePath;
                    break;
                }
            }
        }
        if (m_learnedRoi.isValid()) {
            QStringList retry;
            QMap<QString, QRectF> rois;
            for (const auto &r : results) {
                if (r.wallStartMs > 0)
                    continue;   // 已成功
                if (m_probes.value(r.filePath).absStartEpochMs > 0)
                    continue;   // frames-only 段（流内时间已可信，无需识别）
                retry << r.filePath;
                rois.insert(r.filePath, m_learnedRoi);
            }
            if (!retry.isEmpty()) {
                m_roiRetryDone = true;
                log(QStringLiteral(
                        "[%1] ROI 自学习：以「%2」的命中位置重试 %3 个未识别段")
                        .arg(tsLog(),
                             learnedFrom.isEmpty() ? QStringLiteral("人工框选")
                                                   : learnedFrom)
                        .arg(retry.size()));
                m_ocrEngine->run(retry, m_evidenceDir, buildDurMap({}),
                                 m_evidenceDir + QStringLiteral("/frames"),
                                 m_opts.withSha256, {}, rois);
                return;   // 第二轮 onOcrFinished 合并后继续排序
            }
        }
        m_roiRetryDone = true;   // 没有可学/可重试的 → 标记免回头
    }
    m_reOcrBusy = false;

    emit ocrDone(results);
    setPhase(TaskPhase::Sorting);
    runSorting();
}

void PreprocessingCoordinator::reOcrWithRoi(const QString &file,
                                            const QRectF &roiNorm)
{
    if (m_phase != TaskPhase::UserConfirm || m_reOcrBusy || !roiNorm.isValid())
        return;
    m_reOcrBusy = true;
    m_roiRetryDone = true;       // 人工定版轮即重试轮（onOcrFinished 直通排序）
    m_learnedRoi = roiNorm;
    // 本段 + 其余未识别段一并套用（同一台录像机，一次框选全批受益）
    QStringList retry;
    QMap<QString, QRectF> rois;
    for (auto it = m_ocrs.begin(); it != m_ocrs.end(); ++it) {
        const bool failed = it->wallStartMs <= 0
            && m_probes.value(it.key()).absStartEpochMs <= 0;
        if (failed || it.key() == file) {
            retry << it.key();
            rois.insert(it.key(), roiNorm);
        }
    }
    if (!retry.contains(file)) {
        retry << file;
        rois.insert(file, roiNorm);
    }
    log(QStringLiteral("[%1] 人工框选重识别：%2 个段按框选位置重跑 OCR")
            .arg(tsLog()).arg(retry.size()));
    setPhase(TaskPhase::Ocr);   // 复用 Ocr 阶段进度/完成链路
    m_ocrEngine->run(retry, m_evidenceDir, buildDurMap({}),
                     m_evidenceDir + QStringLiteral("/frames"),
                     m_opts.withSha256, {}, rois);
}

void PreprocessingCoordinator::runSorting()
{
    QVector<ProbeResult> probes = m_probes.values();
    QVector<OcrResult> ocrs = m_ocrs.values();
    m_groups = smartSort(probes, ocrs, m_channelOverrides);
    for (const auto &g : m_groups) {
        const int est = estimatedCount(g);
        log(QStringLiteral("[%1] 排序：组 '%2' 含 %3 个文件%4%5")
                .arg(tsLog(), g.channel).arg(g.ordered.size())
                .arg(est > 0 ? QStringLiteral("（其中 %1 段位置为推算——未识别到"
                                              "时间的夹缝插值/端点外延）").arg(est)
                             : QString())
                .arg(g.suspicious ? QStringLiteral("（存疑，需人工确认）")
                                  : QString()));
    }
    setPhase(TaskPhase::UserConfirm);
    emit evidenceReady(m_groups);
}

void PreprocessingCoordinator::applyManualTimestamp(const QString &file, qint64 wallStartMs)
{
    if (m_phase != TaskPhase::UserConfirm || wallStartMs <= 0)
        return;
    OcrResult &r = m_ocrs[file];
    r.filePath = file;
    r.wallStartMs = wallStartMs;
    r.source = OcrResult::Manual;
    r.conf = 1.0;
    r.ocrError.clear();
    log(QStringLiteral("[%1] 人工手输时间戳：%2 → %3")
            .arg(tsLog(), file,
                 QDateTime::fromMSecsSinceEpoch(wallStartMs, Qt::LocalTime)
                     .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    runSorting();
}

void PreprocessingCoordinator::applyGroupOrder(const QString &channel,
                                               const QStringList &orderedPaths)
{
    if (m_phase != TaskPhase::UserConfirm)
        return;
    for (auto &g : m_groups) {
        if (g.channel != channel)
            continue;
        QMap<QString, SortEntry> byPath;
        for (const auto &e : g.ordered)
            byPath.insert(e.filePath, e);
        QVector<SortEntry> reordered;
        for (const QString &p : orderedPaths)
            if (byPath.contains(p))
                reordered.append(byPath.take(p));
        for (const auto &e : byPath)   // 防漏：未覆盖的文件附后
            reordered.append(e);
        g.ordered = reordered;
        recomputeContinuityWarnings(g);
        log(QStringLiteral("[%1] 人工调整顺序：组 %2（%3 段）")
                .arg(tsLog(), channel).arg(orderedPaths.size()));
        break;
    }
    emit evidenceReady(m_groups);
}

void PreprocessingCoordinator::applyGrouping(const QString &file, const QString &channel)
{
    if (m_phase != TaskPhase::UserConfirm)
        return;
    m_channelOverrides.insert(file, channel);
    log(QStringLiteral("[%1] 人工分组：%2 → %3").arg(tsLog(), file, channel));
    runSorting();
}

void PreprocessingCoordinator::confirmOrder()
{
    if (m_phase != TaskPhase::UserConfirm)
        return;
    setPhase(TaskPhase::Precheck);
    runPrecheck();
}

void PreprocessingCoordinator::runPrecheck()
{
    m_prechecks.clear();
    m_overlapChannels.clear();
    for (const auto &g : m_groups) {
        QVector<ProbeResult> ordered;
        for (const auto &e : g.ordered)
            ordered.append(m_probes.value(e.filePath));
        m_prechecks.insert(g.channel, concatPrecheck(ordered));
        // v1.7.0 M2：组内时间重叠检测（墙钟相邻比较，容差 2s）；
        // v1.12.0（2026-08-20 拍板）：检测到重叠默认修剪、仅留痕标注，
        // 保留原样为 UI 高级选项（Q-17 修剪规则不变）
        for (int i = 1; i < g.ordered.size(); ++i) {
            const SortEntry &prev = g.ordered[i - 1];
            const SortEntry &cur = g.ordered[i];
            const qint64 prevEnd = prev.ocrEndMs > 0 ? prev.ocrEndMs : prev.endMs;
            // 无墙钟信息的段（startMs<=0）不参与重叠判定——全 0 组会
            // 假阳性（0 < prevEnd-2000），已按墙钟推导数据前提修复
            if (cur.startMs > 0 && prevEnd > 0 && cur.startMs < prevEnd - 2000) {
                if (!m_overlapChannels.contains(g.channel))
                    m_overlapChannels.append(g.channel);
                log(QStringLiteral("[%1] ⚠ 组 %2 检测到时间重叠：%3（%4 与 %5 重叠 %6s）")
                        .arg(tsLog(), g.channel, cur.filePath, prev.filePath, cur.filePath)
                        .arg((prevEnd - cur.startMs) / 1000.0, 0, 'f', 1));
            }
        }
    }
    emit precheckReady(m_prechecks);
    if (!m_overlapChannels.isEmpty())
        emit overlapDetected(m_overlapChannels);
}

// ---------------------------------------------------------------------------
// 执行阶段
// ---------------------------------------------------------------------------
void PreprocessingCoordinator::startProcessing(const ProcessingOptions &opts)
{
    if (m_phase == TaskPhase::UserConfirm)
        confirmOrder();               // 直接拼接：自动确认当前列表顺序
    if (m_phase != TaskPhase::Precheck) {
        log(QStringLiteral("[%1] 开始拼接被忽略：当前阶段 %2（需先探测就绪）")
                .arg(tsLog()).arg(int(m_phase)));
        return;
    }
    m_opts = opts;

    // 输出目录（§5.5.2：默认素材目录下 LumenArc_Merged_<时间戳>/）
    m_outputDir = opts.outputDir;
    if (m_outputDir.isEmpty() && !m_files.isEmpty())
        m_outputDir = QFileInfo(m_files.first()).absolutePath()
            + QStringLiteral("/LumenArc_Merged_") + nowTag();
    if (!QDir().mkpath(m_outputDir)) {
        emit failed(PreprocessError::OutputConflict,
                    QStringLiteral("cannot create output dir: %1").arg(m_outputDir));
        return;
    }

    // 磁盘空间前置预估（§10.2）：转码≈输入×1.2，拼接≈输入×1.05
    qint64 inputBytes = 0;
    for (const QString &f : m_files)
        inputBytes += QFileInfo(f).size();
    int needTxCount = 0;
    for (const auto &g : m_groups) {
        QVector<ProbeResult> ordered;
        for (const auto &e : g.ordered)
            ordered.append(m_probes.value(e.filePath));
        needTxCount += filesNeedingTranscode(ordered).size();
    }
    // 组级音频策略（2026-08）：组内全部 AAC 且参数一致 → 直拷保留原始
    // 数据层级；有异参（concat demuxer 会丢后续异参音轨）或非 AAC →
    // 整组重编码（保留组内首个参数档）
    for (const auto &g : m_groups) {
        bool uniform = true;
        const ProbeResult *first = nullptr;
        for (const auto &e : g.ordered) {
            const ProbeResult &p = m_probes.value(e.filePath);
            if (!p.ok())
                continue;
            if (!first) {
                first = &p;
            } else if (p.audioCodec != first->audioCodec
                       || p.audioSampleRate != first->audioSampleRate
                       || p.audioChannels != first->audioChannels) {
                uniform = false;
                break;
            }
        }
        const bool copy = uniform && first && first->audioStreams > 0
            && first->audioCodec == QLatin1String("aac");
        // 注（2026-08 取证）：监控源音频多为 pcm_alaw 8k——带限 3.4kHz
        // 是源固有特性；alaw 无 mp4 sample entry（muxer 拒绝）。
        // P-27（2026-08-16）：非 AAC 源转码时按文件 codec 分流——pcm 家族
        // → pcm_s16le 数学无损（8k mono 128kbps 与 aac 128k 同体积），
        // 其余有损源 → aac 128k 保持现状
        m_groupCopyAudio.insert(g.channel, copy);
        if (first && !copy && first->audioStreams > 0)
            log(QStringLiteral("[%1] 组「%2」音频参数不统一或非 AAC，整组重编码"
                               "（避免拼接丢失异参音轨；pcm 源将无损解压）")
                    .arg(tsLog(), g.channel));
    }
    const bool anyTranscode = needTxCount > 0;
    const qint64 estimate = qint64(inputBytes * (anyTranscode ? 1.2 : 1.05));
    const QStorageInfo storage(m_outputDir);
    if (storage.isValid() && storage.bytesAvailable() > 0
        && storage.bytesAvailable() < estimate) {
        emit failed(PreprocessError::OutputConflict,
                    QStringLiteral("磁盘空间不足：预估需要 %1 MB，可用 %2 MB")
                        .arg(estimate / 1048576).arg(storage.bytesAvailable() / 1048576));
        return;
    }

    // 单文件（GO = 单独转码导出）：合格 MP4 直接完成；否则转码导出不拼接
    if (m_files.size() == 1) {
        const QString f = m_files.first();
        const bool needTx = filesNeedingTranscode({m_probes.value(f)})
                                .contains(f);
        if (!needTx) {
            log(QStringLiteral("[%1] 单文件已是合格 MP4（H.264 且关键帧 ≤2.5s），无需处理")
                    .arg(tsLog()));
            m_report.outputPath = f;
            m_report.evidenceDir.clear();   // 未执行任何处理：不留临时证据目录
            m_report.reportCsvPath.clear();
            setPhase(TaskPhase::Done);
            emit progress(100, QStringLiteral("无需处理"));
            emit finished(m_report);
            return;
        }
        m_transcodeQueue = QStringList{f};
        m_actions.insert(f, QStringLiteral("转码"));
        log(QStringLiteral("[%1] 单文件转码导出：%2").arg(tsLog(), f));
        setPhase(TaskPhase::Transcoding);
        m_currentTranscode.clear();
        m_lastTxPercent = 50;   // 进度钳位起点（多文件路径同）
        startNextTranscode();
        return;
    }

    // 逐文件转码路由（现场反馈：转码非强制步骤，仅确实需要的文件转码）：
    // 白名单外/探测失败/关键帧稀疏/组内参数不一致 → 转码；其余直接无损拼接
    for (const auto &g : m_groups) {
        QVector<ProbeResult> ordered;
        for (const auto &e : g.ordered)
            ordered.append(m_probes.value(e.filePath));
        const QStringList needTx = filesNeedingTranscode(ordered);
        for (const auto &e : g.ordered) {
            // v1.7.0 M2：修剪模式下需要裁剪的文件必须走转码（trim 在转码
            // 阶段生效），即使原本可无损拼接
            const bool needsTrim = m_trimOverlap
                && trimRangeFor(e.filePath).first > 0;
            if (needTx.contains(e.filePath) || needsTrim) {
                if (!m_transcodeQueue.contains(e.filePath))
                    m_transcodeQueue.append(e.filePath);
                m_actions.insert(e.filePath, QStringLiteral("转码"));
            } else {
                m_actions.insert(e.filePath, QStringLiteral("拼接"));
            }
        }
        m_concatQueue.append(g.channel);
    }
    log(QStringLiteral("[%1] 转码 %2 个 / 直接拼接 %3 个")
            .arg(tsLog()).arg(m_transcodeQueue.size())
            .arg(m_files.size() - m_transcodeQueue.size()));

    if (!m_transcodeQueue.isEmpty()) {
        setPhase(TaskPhase::Transcoding);
        m_currentTranscode.clear();
        m_lastTxPercent = 50;
        startNextTranscode();
    } else {
        setPhase(TaskPhase::Concat);
        startNextConcat();
    }
}

TranscodeRequest PreprocessingCoordinator::buildTranscodeRequest(
    const QString &file) const
{
    const QString base = QFileInfo(file).completeBaseName()
        + QStringLiteral("_lumen");
    TranscodeRequest req;
    req.input = file;
    // v1.7.0 M4（Q4）：自动命名 <通道>_<日期>_<起止>.mp4（墙钟可用时）；
    // 与 allocateOutput 避让共存；通道名 sanitize 防非法文件名字符
    {
        const SortEntry *entry = nullptr;
        for (const auto &g : m_groups)
            for (const auto &e : g.ordered)
                if (e.filePath == file) {
                    entry = &e;
                    break;
                }
        QString channel;
        qint64 ws = 0, we = 0;
        for (const auto &g : m_groups)
            for (const auto &e : g.ordered)
                if (e.filePath == file) {
                    channel = g.channel;
                    ws = e.startMs;
                    we = e.ocrEndMs > 0 ? e.ocrEndMs : e.endMs;
                    break;
                }
        Q_UNUSED(entry)
        QString safe = channel;
        safe.replace(QRegularExpression(QStringLiteral(R"([\/:*?"<>|()])")),
                     QStringLiteral("_"));
        // 默认组友好化（与拼接命名 merged 规则一致，现场反馈①）
        if (safe == QStringLiteral("（默认组）")
            || safe == QStringLiteral("_默认组_"))
            safe = QStringLiteral("merged");
        const QString name = autoOutputName(safe, base, ws, we);
        req.output = allocateOutput(m_outputDir,
                                    QFileInfo(name).completeBaseName());
    }
    req.durationMs = durationOf(file);
    req.crf = m_opts.crf;
    req.encoder = m_opts.encoder;   // v1.7.0 M1：硬编可选
    // 统一 CFR（拼接前置要求，2026-08）：全局最大 avg fps，低帧率段插帧
    req.fps = m_unifiedFps;
    if (m_unifiedFps > 0.0f && m_unifiedFps <= 240.0f)
        req.keyframeInterval = qMax(1, qRound(2.0f * m_unifiedFps));
    const ProbeResult p = m_probes.value(file);
    req.deinterlace = m_opts.deinterlace && p.fieldOrder > 1;
    const QString ch = [&]() {
        for (const auto &g : m_groups)
            for (const auto &e : g.ordered)
                if (e.filePath == file)
                    return g.channel;
        return QString();
    }();
    req.copyAudio = m_groupCopyAudio.value(ch, false);
    // P-27（2026-08-16）：pcm 家族源（alaw/mulaw 等）→ 无损解压 pcm_s16le；
    // mp4 容器不收 alaw（无 sample entry），数学无损且体积与 aac 128k 相当
    req.losslessPcm = !req.copyAudio
        && p.audioCodec.startsWith(QLatin1String("pcm_"));
    req.audioSampleRate = p.audioSampleRate.toInt();
    req.audioChannels = p.audioChannels.toInt();
    // v1.7.0 M4（Q6）：跨相机混拼分辨率归一——组内分辨率不一致时
    // 统一到出现最多的档位（保持宽高比缩放，黑边不补）
    {
        QMap<QString, int> resCnt;
        for (const auto &g : m_groups) {
            if (g.channel != ch)
                continue;
            for (const auto &e : g.ordered) {
                const ProbeResult &pp = m_probes.value(e.filePath);
                if (pp.width > 0 && pp.height > 0)
                    ++resCnt[QStringLiteral("%1x%2").arg(pp.width).arg(pp.height)];
            }
        }
        if (resCnt.size() > 1) {
            QString bestKey;
            int bestCnt = 0;
            for (auto it = resCnt.constBegin(); it != resCnt.constEnd(); ++it) {
                if (it.value() > bestCnt) {
                    bestCnt = it.value();
                    bestKey = it.key();
                }
            }
            const int x = bestKey.indexOf('x');
            req.outWidth = bestKey.left(x).toInt();
            req.outHeight = bestKey.mid(x + 1).toInt();
        }
    }
    // v1.7.0 M2：重叠剪切（用户选择修剪时生效）；进度分母同步扣减修剪量
    const auto tr = trimRangeFor(file);
    if (tr.first > 0) {
        req.trimStartMs = tr.first;
        req.trimEndMs = tr.second;
        req.durationMs = qMax<qint64>(0, req.durationMs - tr.first);
    }
    return req;
}

QPair<qint64, qint64> PreprocessingCoordinator::trimRangeFor(
    const QString &file) const
{
    if (!m_trimOverlap)
        return {0, 0};
    for (const CutPlan &p : m_cutPlans) {
        if (p.file == file && p.trimmed && !p.dropped)
            return {p.keepStartMs, p.keepEndMs};
    }
    return {0, 0};
}

void PreprocessingCoordinator::startNextTranscode()
{
    scheduleNextTranscode();
}

void PreprocessingCoordinator::scheduleNextTranscode()
{
    // v1.7.0 M3：组级并行 N=2（同组串行，组间并行）
    const QVector<TranscodeEngine *> engines = {
        m_transcodeEngine, m_transcodeEngine2};
    for (TranscodeEngine *eng : engines) {
        if (m_engineFile.contains(eng))
            continue;
        int pick = -1;
        for (int i = 0; i < m_transcodeQueue.size(); ++i) {
            const QString &f = m_transcodeQueue[i];
            if (m_transcoded.contains(f) || m_transcodeFailed.contains(f))
                continue;
            const QString g = groupOfFile(f);
            if (!m_activeGroups.contains(g)) {
                pick = i;
                break;
            }
        }
        if (pick < 0)
            break;
        const QString file = m_transcodeQueue[pick];
        const QString g = groupOfFile(file);
        const TranscodeRequest req = buildTranscodeRequest(file);
        m_engineFile.insert(eng, file);
        m_activeGroups.insert(g);
        m_actions.insert(file, QStringLiteral("转码(%1)%2")
                             .arg(req.encoder.isEmpty()
                                      ? QStringLiteral("libx264") : req.encoder)
                             .arg(req.trimStartMs > 0
                                      ? QStringLiteral(";重叠修剪%1s")
                                            .arg(req.trimStartMs / 1000.0, 0, 'f', 1)
                                      : QString()));
        log(QStringLiteral("[%1] 转码开始：%2 -> %3%4%5%6")
                .arg(tsLog(), req.input, req.output)
                .arg(m_unifiedFps > 0.0f
                         ? QStringLiteral("（统一 %1fps）").arg(m_unifiedFps)
                         : QString())
                .arg(req.copyAudio
                         ? QStringLiteral("，音频直拷")
                         : (req.losslessPcm
                                ? QStringLiteral("，音频无损解压 pcm_s16le")
                                : QStringLiteral("，音频重编码 aac")))
                .arg(req.trimStartMs > 0
                         ? QStringLiteral("，重叠修剪 %1s 起")
                               .arg(req.trimStartMs / 1000.0, 0, 'f', 1)
                         : QString()));
        eng->run(req);
    }

    if (m_engineFile.isEmpty()) {
        m_currentTranscode.clear();
        m_activeGroups.clear();
        setPhase(TaskPhase::Concat);
        startNextConcat();
    }
}

void PreprocessingCoordinator::setTrimOverlap(bool trim)
{
    m_trimOverlap = trim;
    m_cutPlans.clear();
    if (!trim)
        return;
    // 计算剪切计划：每组按墙钟排序段 → planOverlapCuts
    for (const auto &g : m_groups) {
        QVector<WallSegment> segs;
        for (const auto &e : g.ordered) {
            // 无墙钟段（startMs<=0）跳过——不可靠的 0 起时间会误剪
            if (e.startMs <= 0)
                continue;
            WallSegment s;
            s.file = e.filePath;
            s.wallStartMs = e.startMs;
            s.wallEndMs = e.ocrEndMs > 0 ? e.ocrEndMs : e.endMs;
            s.streamMs = e.durationMs > 0 ? e.durationMs
                        : m_probes.value(e.filePath).durationMs;
            if (s.wallEndMs <= s.wallStartMs)
                s.wallEndMs = s.wallStartMs + s.streamMs;
            segs.append(s);
        }
        m_cutPlans += planOverlapCuts(segs);
    }
    const int trimmed = std::count_if(m_cutPlans.cbegin(), m_cutPlans.cend(),
                                      [](const CutPlan &p) { return p.trimmed; });
    // 修剪后重叠已物理移除：清除组内 Overlap 警告（拼接 hasOverlap/
    // normalizeTimestamps 不再按“有重叠”处理，报告衔接警告也同步消失）
    for (auto &g : m_groups) {
        g.warnings.erase(std::remove_if(g.warnings.begin(), g.warnings.end(),
                                        [](const SortWarning &w) {
                                            return w.type == SortWarningType::Overlap;
                                        }),
                         g.warnings.end());
    }
    if (trimmed > 0)
        log(QStringLiteral("[%1] 重叠修剪已启用：%2 段将被裁剪（Q-17：保前段完整）")
                .arg(tsLog()).arg(trimmed));
}

/// 输出路径归一（报告通道映射键，与 case 登记 normPath 一致）
static QString normOutPath(const QString &p)
{
    return QFileInfo(p).absoluteFilePath();
}

QString PreprocessingCoordinator::groupOfFile(const QString &file) const
{
    for (const auto &g : m_groups)
        for (const auto &e : g.ordered)
            if (e.filePath == file)
                return g.channel;
    return QString();
}

void PreprocessingCoordinator::onTranscodeOneFinished(const QString &outputPath)
{
    if (m_phase != TaskPhase::Transcoding)
        return;
    auto *eng = qobject_cast<TranscodeEngine *>(QObject::sender());
    const QString file = eng ? m_engineFile.value(eng) : m_currentTranscode;
    log(QStringLiteral("[%1] 转码完成：%2").arg(tsLog(), outputPath));
    m_transcoded.insert(file, outputPath);
    m_outputs.insert(file, outputPath);
    if (eng)
        m_engineFile.remove(eng);
    m_activeGroups.clear();
    for (const auto &f : m_engineFile)
        m_activeGroups.insert(groupOfFile(f));
    scheduleNextTranscode();
}

void PreprocessingCoordinator::onTranscodeOneFailed(PreprocessError error,
                                                    const QString &detail)
{
    if (m_phase != TaskPhase::Transcoding)
        return;
    auto *eng = qobject_cast<TranscodeEngine *>(QObject::sender());
    const QString file = eng ? m_engineFile.value(eng) : m_currentTranscode;
    log(QStringLiteral("[%1] 转码失败（%2）：%3 — %4")
            .arg(tsLog()).arg(int(error)).arg(file, detail));
    m_transcodeFailed.append(file);
    m_actions.insert(file, QStringLiteral("转码失败"));
    if (eng)
        m_engineFile.remove(eng);
    m_activeGroups.clear();
    for (const auto &f : m_engineFile)
        m_activeGroups.insert(groupOfFile(f));
    scheduleNextTranscode();   // 已完成文件保留，队列继续（规范 C2 不静默）
}

qint64 PreprocessingCoordinator::durationOf(const QString &file) const
{
    qint64 d = m_ocrs.value(file).durationMs;
    if (d <= 0)
        d = m_probes.value(file).durationMs;
    return d;
}

QString PreprocessingCoordinator::allocateOutput(const QString &dir,
                                                 const QString &base) const
{
    // 输出自动避让（§5.5.2：禁止覆盖既有文件）
    for (int i = 0; ; ++i) {
        const QString name = i == 0 ? base + QStringLiteral(".mp4")
                                    : QStringLiteral("%1_%2.mp4").arg(base).arg(i + 1);
        const QString path = dir + QLatin1Char('/') + name;
        if (!QFile::exists(path))
            return path;
    }
}

void PreprocessingCoordinator::startNextConcat()
{
    const int idx = m_concatQueue.indexOf(m_currentConcatGroup) + 1;
    if (idx >= m_concatQueue.size()) {
        m_currentConcatGroup.clear();
        finalize();
        return;
    }
    m_currentConcatGroup = m_concatQueue[idx];
    const SortGroup *group = nullptr;
    for (const auto &g : m_groups)
        if (g.channel == m_currentConcatGroup)
            group = &g;
    if (!group) {
        startNextConcat();
        return;
    }

    ConcatRequest req;
    req.workDir = m_evidenceDir;
    // §45 定案（2026-08-17）：拼接产物命名 LAMerged_<通道>_<首>_<尾>（默认组
    // 无通道前缀）；替代 fixed `merged_concat` / `<通道>_concat`
    const QString &firstFile = group->ordered.first().filePath;
    const QString &lastFile = group->ordered.last().filePath;
    const QString outBase = concatOutputName(group->channel, firstFile, lastFile);
    req.outputPath = allocateOutput(m_outputDir, outBase);
    qint64 totalMs = 0;
    QVector<qint64> offsets;
    qint64 acc = 0;
    bool hasOverlap = false;
    for (const auto &e : group->ordered) {
        const QString src = m_transcoded.value(e.filePath, e.filePath);
        // 转码失败的文件跳过拼接（报告已记录，C2）
        if (m_transcodeFailed.contains(e.filePath))
            continue;
        req.orderedFiles << src;
        // v1.7.0 M2：重叠修剪后的段时长 = 原时长 - 修剪量（拼接 offsets
        // 必须用实际时长，否则时间戳 gap 错位）
        const auto tr = trimRangeFor(e.filePath);
        const qint64 trimmed = (tr.first > 0) ? tr.first : 0;
        const qint64 d = qMax<qint64>(0, durationOf(e.filePath) - trimmed);
        offsets.append(acc);
        acc += d;
        totalMs += d;
    }
    for (const auto &w : group->warnings)
        hasOverlap = hasOverlap || w.type == SortWarningType::Overlap;
    req.totalDurationMs = totalMs;
    req.normalizeTimestamps = m_opts.normalizeTimestamps && hasOverlap;
    req.segmentOffsetsMs = offsets;
    req.ignoreWarnings = m_opts.ignoreWarnings;
    if (req.orderedFiles.isEmpty()) {
        log(QStringLiteral("[%1] 组 %2 无可拼接文件，跳过").arg(tsLog(), m_currentConcatGroup));
        startNextConcat();
        return;
    }
    log(QStringLiteral("[%1] 拼接开始：组 %2，%3 段 → %4")
            .arg(tsLog(), m_currentConcatGroup).arg(req.orderedFiles.size())
            .arg(req.outputPath));
    m_concatEngine->run(req);
}

void PreprocessingCoordinator::onConcatOneFinished(const QString &outputPath)
{
    if (m_phase != TaskPhase::Concat)
        return;
    log(QStringLiteral("[%1] 拼接完成：%2").arg(tsLog(), outputPath));
    m_concatOutputs.insert(m_currentConcatGroup, outputPath);
    // 报告：组内文件记输出；list.txt 改名留档（§9.1）
    const SortGroup *group = nullptr;
    for (const auto &g : m_groups)
        if (g.channel == m_currentConcatGroup)
            group = &g;
    if (group) {
        for (const auto &e : group->ordered)
            if (!m_transcodeFailed.contains(e.filePath))
                m_outputs.insert(e.filePath, outputPath);
    }
    const QString src = m_evidenceDir + QStringLiteral("/concat_list.txt");
    if (QFile::exists(src)) {
        QString safe = m_currentConcatGroup;
        safe.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|()])")),
                     QStringLiteral("_"));
        QFile::rename(src, m_evidenceDir + QStringLiteral("/concat_list_%1.txt").arg(safe));
    }
    // 拼接成功 → 清理该组中间转码产物（2026-08 需求：只保留最终拼接文件；
    // 每段命令已留痕 operations.log，取证不受影响；失败场景不删，便于排查）
    int removed = 0;
    if (group) {
        QStringList removedNames;
        for (const auto &e : group->ordered) {
            const QString seg = m_transcoded.value(e.filePath);
            if (!seg.isEmpty() && seg != outputPath && QFile::remove(seg)) {
                ++removed;
                removedNames << QFileInfo(seg).fileName();
            }
        }
        // 归一化临时文件（remux 副本，同属中间产物）
        const QDir evDir(m_evidenceDir);
        for (const QString &nf : evDir.entryList({QStringLiteral("norm_*.mp4")},
                                                 QDir::Files)) {
            if (QFile::remove(evDir.absoluteFilePath(nf))) {
                ++removed;
                removedNames << nf;
            }
        }
        if (removed > 0)
            log(QStringLiteral("[%1] 已清理中间转码产物 %2 个：%3")
                    .arg(tsLog()).arg(removed)
                    .arg(removedNames.join(QStringLiteral("、"))));
    }
    startNextConcat();
}

void PreprocessingCoordinator::onConcatOneFailed(PreprocessError error,
                                                 const QString &detail)
{
    if (m_phase != TaskPhase::Concat)
        return;
    log(QStringLiteral("[%1] 拼接失败（%2）：组 %3 — %4")
            .arg(tsLog()).arg(int(error)).arg(m_currentConcatGroup, detail));
    for (auto &g : m_groups)
        if (g.channel == m_currentConcatGroup)
            for (const auto &e : g.ordered)
                m_actions.insert(e.filePath, QStringLiteral("拼接失败"));
    startNextConcat();   // 其余组继续
}

// ---------------------------------------------------------------------------
// 收尾
// ---------------------------------------------------------------------------
void PreprocessingCoordinator::finalize()
{
    // 证据目录迁入输出目录（§9.1；失败则保留临时路径并在报告注明）
    QString finalEvidence = m_outputDir + QStringLiteral("/LumenArc_Evidence_") + nowTag();
    if (!QDir().rename(m_evidenceDir, finalEvidence)) {
        // 跨卷：复制 + 删除（证据帧体量小，秒级）
        auto copyRec = [](const QString &src, const QString &dst) {
            QDir s(src);
            QDir().mkpath(dst);
            for (const QFileInfo &e : s.entryInfoList(QDir::Files))
                QFile::copy(e.absoluteFilePath(), dst + QLatin1Char('/') + e.fileName());
            for (const QFileInfo &e : s.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot))
                ;   // 证据目录仅 frames/ 一层，扁平处理
        };
        QDir().mkpath(finalEvidence);
        copyRec(m_evidenceDir, finalEvidence);
        QDir().mkpath(finalEvidence + QStringLiteral("/frames"));
        copyRec(m_evidenceDir + QStringLiteral("/frames"),
                finalEvidence + QStringLiteral("/frames"));
        QDir(m_evidenceDir).removeRecursively();
        if (!QDir(finalEvidence).exists())
            finalEvidence = m_evidenceDir;   // 迁移失败：保留临时（证据不丢）
    }

    // CSV 报告（RFC4180，F6）
    EvidenceReportInput repIn;
    repIn.probes = m_probes.values();
    repIn.ocrs = m_ocrs.values();
    repIn.groups = m_groups;
    repIn.actions = m_actions;
    repIn.outputs = m_outputs;
    const QString csvPath = finalEvidence + QStringLiteral("/report.csv");
    {
        QFile f(csvPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text))
            f.write(buildEvidenceCsv(repIn).toUtf8());
        else
            log(QStringLiteral("[%1] 报告写入失败：%2").arg(tsLog(), csvPath));
    }
    writeOperationsLog(finalEvidence);

    m_report.evidenceDir = finalEvidence;
    m_report.reportCsvPath = csvPath;
    // 全部输出清单（v1.3.0 案件登记 outputRefs 用；保持与旧 outputPath 同源）
    m_report.outputPaths.clear();
    m_report.outputChannels.clear();
    for (auto it = m_concatOutputs.constBegin(); it != m_concatOutputs.constEnd(); ++it) {
        m_report.outputPaths.append(it.value());
        m_report.outputChannels.insert(normOutPath(it.value()), it.key());
    }
    for (auto it = m_outputs.constBegin(); it != m_outputs.constEnd(); ++it) {
        if (!m_report.outputPaths.contains(it.value()))
            m_report.outputPaths.append(it.value());
        // 转码产物通道：由源文件组归属推导（m_outputs: 源→转码输出）
        m_report.outputChannels.insert(normOutPath(it.value()),
                                       groupOfFile(it.key()));
    }
    if (m_concatOutputs.isEmpty()) {
        if (!m_outputs.isEmpty()) {
            // 单文件转码导出：无拼接但有转码产物
            m_report.outputPath = m_outputs.values().first();
        } else {
            // 全组失败：必须显式失败，禁止绿勾"完成"（现场反馈：显示完成但无输出）
            const QString detail = QStringLiteral(
                "拼接未产出任何输出文件：转码失败 %1 个，拼接失败 %2 组；"
                "详见证据目录 operations.log / report.csv")
                .arg(m_transcodeFailed.size())
                .arg(m_groups.size());
            m_report.error = PreprocessError::ConcatFailed;
            m_report.errorDetail = detail;
            m_report.outputPath.clear();
            writeOperationsLog(finalEvidence);
            setPhase(TaskPhase::Failed);
            log(QStringLiteral("[%1] %2").arg(tsLog(), detail));
            emit failed(PreprocessError::ConcatFailed, detail);
            return;
        }
    } else {
        m_report.outputPath = m_concatOutputs.values().first();
    }
    // v1.2.0：拼接输出随附校时 sidecar（§3.5；主程序打开输出时自动继承。
    // C2：写入失败仅记日志不静默，不阻断完成态）
    // v1.12.0（2026-08-20 拍板：校时反映到产物时间轴）：sidecar 感知重叠修剪/
    // 整段丢弃/转码失败剔除；转码段逐段实测时长（转码件与源时长偏差 ±30~
    // 300ms，累积会污染尾部锚点——实测仅尾段一次 ffprobe，量级可忽略）
    for (auto it = m_concatOutputs.constBegin();
         it != m_concatOutputs.constEnd(); ++it) {
        for (const auto &g : m_groups) {
            if (g.channel != it.key())
                continue;
            QMap<QString, qint64> trimStarts;
            QSet<QString> skips;
            QMap<QString, qint64> actualMs;
            for (const CutPlan &p : m_cutPlans) {
                if (p.dropped)
                    skips.insert(p.file);
                else if (p.trimmed)
                    trimStarts.insert(p.file, p.keepStartMs);
            }
            for (const QString &f : m_transcodeFailed)
                skips.insert(f);
            for (const auto &e : g.ordered) {
                if (skips.contains(e.filePath))
                    continue;
                const QString part = m_transcoded.value(e.filePath);
                if (part.isEmpty())
                    continue;   // 未转码段（无损直拼）：流内时长 = 源时长
                const ProbeResult pr = MediaProbeEngine::probeOne(part);
                if (pr.ok() && pr.durationMs > 0)
                    actualMs.insert(e.filePath, pr.durationMs);
                else
                    log(QStringLiteral("[%1] ⚠ 段产物时长实测失败（锚点回退源时长）：%2")
                            .arg(tsLog(), part));
            }
            QString serr;
            if (!CalibrationService::writeSidecar(it.value(), g.ordered, &serr,
                                                  trimStarts, skips, actualMs))
                log(QStringLiteral("[%1] sidecar 写入失败：%2")
                        .arg(tsLog(), serr));
        }
    }
    // 部分失败：报告留痕（C2 不静默），日志汇总
    if (!m_transcodeFailed.isEmpty())
        log(QStringLiteral("[%1] 注意：%2 个文件转码失败，已从拼接中剔除")
                .arg(tsLog()).arg(m_transcodeFailed.size()));
    setPhase(TaskPhase::Done);
    emit progress(100, QStringLiteral("完成"));
    emit finished(m_report);
}

void PreprocessingCoordinator::writeOperationsLog(const QString &dir) const
{
    QFile f(dir + QStringLiteral("/operations.log"));
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return;
    QTextStream ts(&f);
    for (const QString &line : m_log)
        ts << line << '\n';
}

void PreprocessingCoordinator::cancel()
{
    const TaskPhase ph = m_phase;
    if (ph == TaskPhase::Idle || ph == TaskPhase::Done
        || ph == TaskPhase::Failed || ph == TaskPhase::Cancelled)
        return;
    m_probeEngine->cancel();
    m_ocrEngine->cancel();
    m_concatEngine->cancel();
    m_transcodeEngine->cancel();
    m_transcodeEngine2->cancel();   // v1.7.0 M3：双引擎都要取消
    m_engineFile.clear();
    m_activeGroups.clear();
    log(QStringLiteral("[%1] 用户取消；证据保留于 %2").arg(tsLog(), m_evidenceDir));
    writeOperationsLog(m_evidenceDir);
    setPhase(TaskPhase::Cancelled);
    m_report.error = PreprocessError::Cancelled;
    m_report.errorDetail = m_evidenceDir;
    emit failed(PreprocessError::Cancelled, m_evidenceDir);
}
