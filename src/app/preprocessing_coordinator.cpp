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

#include "infrastructure/ianalysis_engine.h"
#include "infrastructure/media_probe_engine.h"
#include "infrastructure/timestamp_ocr_engine.h"
#include "infrastructure/concat_engine.h"
#include "infrastructure/transcode_engine.h"
#include "domain/smart_sorter.h"
#include "domain/evidence_report.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QRunnable>
#include <QThreadPool>
#include <QStorageInfo>
#include <QTextStream>

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
                const int idx = m_transcodeQueue.indexOf(m_currentTranscode);
                emit progress(50 + (idx * 40 / total) + pct * 40 / total / 100,
                              QStringLiteral("转码 %1 (%2%)")
                                  .arg(m_currentTranscode).arg(pct));
            });
    connect(m_transcodeEngine, &TranscodeEngine::finished,
            this, &PreprocessingCoordinator::onTranscodeOneFinished);
    connect(m_transcodeEngine, &TranscodeEngine::failed,
            this, &PreprocessingCoordinator::onTranscodeOneFailed);

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
    m_files = files;
    m_probes.clear();
    m_ocrs.clear();
    m_groups.clear();
    m_prechecks.clear();
    m_channelOverrides.clear();
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

void PreprocessingCoordinator::onTrustedDurationsReady(const QMap<QString, qint64> &durations)
{
    if (m_phase != TaskPhase::Probing)
        return;
    // 组装时长表：可信时长优先，容器时长兜底
    QMap<QString, qint64> durMap;
    for (const QString &f : m_files) {
        qint64 d = durations.value(f, 0);
        if (d <= 0)
            d = m_probes.value(f).durationMs;
        if (d > 0)
            durMap.insert(f, d);
    }
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
            r.durationMs = durMap.value(f);
            r.ocrError = QStringLiteral("engine_missing");
            degraded.append(r);
        }
        onOcrFinished(degraded);
        return;
    }
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
    emit ocrDone(results);
    setPhase(TaskPhase::Sorting);
    runSorting();
}

void PreprocessingCoordinator::runSorting()
{
    QVector<ProbeResult> probes = m_probes.values();
    QVector<OcrResult> ocrs = m_ocrs.values();
    m_groups = smartSort(probes, ocrs, m_channelOverrides);
    for (const auto &g : m_groups)
        log(QStringLiteral("[%1] 排序：组 '%2' 含 %3 个文件%4")
                .arg(tsLog(), g.channel).arg(g.ordered.size())
                .arg(g.suspicious ? QStringLiteral("（存疑，需人工确认）")
                                  : QString()));
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
    for (const auto &g : m_groups) {
        QVector<ProbeResult> ordered;
        for (const auto &e : g.ordered)
            ordered.append(m_probes.value(e.filePath));
        m_prechecks.insert(g.channel, concatPrecheck(ordered));
    }
    emit precheckReady(m_prechecks);
}

// ---------------------------------------------------------------------------
// 执行阶段
// ---------------------------------------------------------------------------
void PreprocessingCoordinator::startProcessing(const ProcessingOptions &opts)
{
    if (m_phase != TaskPhase::Precheck)
        return;
    m_opts = opts;

    // 输出目录（§5.5.2：默认素材目录下 LumenArc_Transcode_<时间戳>/）
    m_outputDir = opts.outputDir;
    if (m_outputDir.isEmpty() && !m_files.isEmpty())
        m_outputDir = QFileInfo(m_files.first()).absolutePath()
            + QStringLiteral("/LumenArc_Transcode_") + nowTag();
    if (!QDir().mkpath(m_outputDir)) {
        emit failed(PreprocessError::OutputConflict,
                    QStringLiteral("cannot create output dir: %1").arg(m_outputDir));
        return;
    }

    // 磁盘空间前置预估（§10.2）：转码≈输入×1.2，拼接≈输入×1.05
    qint64 inputBytes = 0;
    for (const QString &f : m_files)
        inputBytes += QFileInfo(f).size();
    bool anyTranscode = false;
    for (auto it = m_prechecks.begin(); it != m_prechecks.end(); ++it)
        anyTranscode = anyTranscode || it.value().hasBlock();
    const qint64 estimate = qint64(inputBytes * (anyTranscode ? 1.2 : 1.05));
    const QStorageInfo storage(m_outputDir);
    if (storage.isValid() && storage.bytesAvailable() > 0
        && storage.bytesAvailable() < estimate) {
        emit failed(PreprocessError::OutputConflict,
                    QStringLiteral("磁盘空间不足：预估需要 %1 MB，可用 %2 MB")
                        .arg(estimate / 1048576).arg(storage.bytesAvailable() / 1048576));
        return;
    }

    // 路由：BLOCK 组 → 全组转码后拼接；OK/WARN 组（或用户忽略）→ 直接拼接
    for (const auto &g : m_groups) {
        const PrecheckResult pc = m_prechecks.value(g.channel);
        const bool needTranscode = pc.hasBlock();
        for (const auto &e : g.ordered) {
            if (needTranscode) {
                if (!m_transcodeQueue.contains(e.filePath))
                    m_transcodeQueue.append(e.filePath);
                m_actions.insert(e.filePath, QStringLiteral("转码"));
            } else {
                m_actions.insert(e.filePath, QStringLiteral("拼接"));
            }
        }
        m_concatQueue.append(g.channel);
    }

    if (!m_transcodeQueue.isEmpty()) {
        setPhase(TaskPhase::Transcoding);
        m_currentTranscode.clear();
        startNextTranscode();
    } else {
        setPhase(TaskPhase::Concat);
        startNextConcat();
    }
}

void PreprocessingCoordinator::startNextTranscode()
{
    const int idx = m_transcodeQueue.indexOf(m_currentTranscode) + 1;
    if (idx >= m_transcodeQueue.size()) {
        m_currentTranscode.clear();
        setPhase(TaskPhase::Concat);
        startNextConcat();
        return;
    }
    m_currentTranscode = m_transcodeQueue[idx];
    const QString base = QFileInfo(m_currentTranscode).completeBaseName()
        + QStringLiteral("_lumen");
    TranscodeRequest req;
    req.input = m_currentTranscode;
    req.output = allocateOutput(m_outputDir, base);
    req.durationMs = durationOf(m_currentTranscode);
    req.crf = m_opts.crf;
    // 隔行源 → 默认反交错（探测驱动，可配置关闭）
    const ProbeResult p = m_probes.value(m_currentTranscode);
    req.deinterlace = m_opts.deinterlace && p.fieldOrder > 1;  // 1=progressive
    // 音轨已为 AAC 时直拷（探测驱动，§5.5.1）
    req.copyAudio = p.audioStreams > 0 && p.audioCodec == QLatin1String("aac");
    log(QStringLiteral("[%1] 转码开始：%2 → %3")
            .arg(tsLog(), req.input, req.output));
    m_transcodeEngine->run(req);
}

void PreprocessingCoordinator::onTranscodeOneFinished(const QString &outputPath)
{
    if (m_phase != TaskPhase::Transcoding)
        return;
    log(QStringLiteral("[%1] 转码完成：%2").arg(tsLog(), outputPath));
    m_transcoded.insert(m_currentTranscode, outputPath);
    m_outputs.insert(m_currentTranscode, outputPath);
    startNextTranscode();
}

void PreprocessingCoordinator::onTranscodeOneFailed(PreprocessError error,
                                                    const QString &detail)
{
    if (m_phase != TaskPhase::Transcoding)
        return;
    log(QStringLiteral("[%1] 转码失败（%2）：%3 — %4")
            .arg(tsLog()).arg(int(error)).arg(m_currentTranscode, detail));
    m_transcodeFailed.append(m_currentTranscode);
    m_actions.insert(m_currentTranscode, QStringLiteral("转码失败"));
    startNextTranscode();   // 已完成文件保留，队列继续（规范 C2 不静默）
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
    QString safeChannel = m_currentConcatGroup;
    safeChannel.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|()])")),
                        QStringLiteral("_"));
    // 默认组输出名友好化（现场反馈①：`_默认组__concat.mp4` 难辨认）
    if (safeChannel == QStringLiteral("（默认组）")
        || safeChannel == QStringLiteral("_默认组_"))
        safeChannel = QStringLiteral("merged");
    req.outputPath = allocateOutput(m_outputDir,
                                    safeChannel + QStringLiteral("_concat"));
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
        const qint64 d = durationOf(e.filePath);
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
    m_report.outputPath = m_concatOutputs.isEmpty()
        ? m_outputDir : m_concatOutputs.values().first();
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
    log(QStringLiteral("[%1] 用户取消；证据保留于 %2").arg(tsLog(), m_evidenceDir));
    writeOperationsLog(m_evidenceDir);
    setPhase(TaskPhase::Cancelled);
    m_report.error = PreprocessError::Cancelled;
    m_report.errorDetail = m_evidenceDir;
    emit failed(PreprocessError::Cancelled, m_evidenceDir);
}
