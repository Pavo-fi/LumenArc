/**
 * @file calibration_service.cpp
 * @brief 校时服务实现：三点取样编排 + sidecar 读写
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-05
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "calibration_service.h"
#include "infrastructure/ianalysis_engine.h"
#include "infrastructure/media_probe_engine.h"
#include "infrastructure/python_analysis_engine.h"
#include "infrastructure/timestamp_ocr_engine.h"
#include "domain/probe_result.h"

#include <QDateTime>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <algorithm>

namespace {
/// Q-6 过渡期证据帧目录（案件归档策略 v1.3.0 定稿后迁移）
QString evidenceDirFor(const QString &videoPath)
{
    return QFileInfo(videoPath).absoluteDir().absoluteFilePath(
        QStringLiteral("LumenArc_Calibration"));
}
/// sidecar 缺口容差（Q-4 拍板：2s）
constexpr qint64 kGapToleranceMs = 2000;

// ---- 时间重建参数（v1.2.1；与 time_piecewise.h 阈值配合）----
constexpr int    kCoarseSampleCount  = 60;     ///< 粗采样目标点数
constexpr qint64 kCoarseSampleMinMs  = 30000;  ///< 粗采样最小间隔
constexpr qint64 kCoarseSampleMaxMs  = 120000; ///< 粗采样最大间隔
constexpr qint64 kBoundaryStepMs     = 2000;   ///< 边界加密步长
constexpr qint64 kBoundaryPadMs      = 5000;   ///< 边界区间外扩（加密范围）
constexpr int    kMaxRefinePoints    = 200;    ///< 加密点总量上限（防超时）
constexpr int    kMinRefinePerBoundary = 8;    ///< 每边界保底加密点数（弱边界定位）
constexpr double kAudioConsistencyDev = 0.02;  ///< OSD跨度 vs 音频时长容差
} // namespace

CalibrationService::CalibrationService(IAnalysisEngine *analysisEngine,
                                       QObject *parent)
    : QObject(parent)
    , m_analysisEngine(analysisEngine)
    , m_ocrEngine(new TimestampOcrEngine(this))
    , m_probeEngine(new MediaProbeEngine(this))
{
    connect(m_ocrEngine, &TimestampOcrEngine::atPositionsFinished,
            this, &CalibrationService::onAtPositionsFinished);
    connect(m_ocrEngine, &TimestampOcrEngine::atPositionsFailed,
            this, &CalibrationService::onAtPositionsFailed);
    connect(m_ocrEngine, &TimestampOcrEngine::ocrProgress, this,
            [this](int done, int total, const QString &) {
                emit progress(QStringLiteral("ocr %1/%2").arg(done).arg(total));
            });
    connect(m_probeEngine, &MediaProbeEngine::probeFinished,
            this, &CalibrationService::onProbeFinished);
}

void CalibrationService::setPythonExecutable(const QString &path)
{
    m_ocrEngine->setPythonExecutable(path);
}

void CalibrationService::runThreePoint(const QString &videoPath,
                                       qint64 currentPosMs, qint64 durationMs,
                                       const QRectF &roi)
{
    if (videoPath.isEmpty() || isRunning())
        return;

    // 可信时长缺失时经引擎中立接口补齐（R4：无 FFmpeg/Python 字样）
    qint64 dur = durationMs;
    if (dur <= 0 && m_analysisEngine)
        dur = m_analysisEngine->trustedDurationMs(videoPath);
    // v1.2.1：视频流时长防御（容器总时长取音画最长流，音画不同长会虚标；
    // 尾部取样必须落在画面流内，否则 seek 落空 → 三点退化）
    const qint64 streamDur = probeVideoStreamDurationMs(videoPath);
    if (streamDur > 0 && (dur <= 0 || streamDur < dur))
        dur = streamDur;
    if (dur <= 0)
        return;

    // 三点：首 1s / 当前位置 / 尾-3s（跨度拉满 = 速率测量最准，§3.4）
    QVector<qint64> positions;
    const qint64 head = (dur > 0 && dur < 10000) ? 0 : 1000;
    positions.append(head);
    if (currentPosMs > 0 && (dur <= 0 || currentPosMs < dur))
        positions.append(currentPosMs);
    if (dur > 6000)
        positions.append(dur - 3000);

    // 去重（1s 阈值）+ 排序
    std::sort(positions.begin(), positions.end());
    QVector<qint64> dedup;
    for (qint64 p : positions) {
        if (dedup.isEmpty() || p - dedup.last() > 1000)
            dedup.append(p);
    }

    m_pendingVideo = videoPath;
    m_pendingDurationMs = dur;
    QDir().mkpath(evidenceDirFor(videoPath));
    emit progress(QStringLiteral("sampling"));
    m_ocrEngine->runAtPositions(videoPath, dedup, dur,
                                evidenceDirFor(videoPath), roi);
}

void CalibrationService::runReconstruction(const QString &videoPath,
                                           qint64 durationMs,
                                           const QRectF &roi)
{
    if (videoPath.isEmpty() || isRunning())
        return;
    qint64 dur = durationMs;
    if (dur <= 0 && m_analysisEngine)
        dur = m_analysisEngine->trustedDurationMs(videoPath);
    const qint64 streamDur = probeVideoStreamDurationMs(videoPath);
    if (streamDur > 0 && (dur <= 0 || streamDur < dur))
        dur = streamDur;
    if (dur <= 0)
        return;

    m_pendingVideo = videoPath;
    m_pendingDurationMs = dur;
    m_roi = roi;
    m_reconSamples.clear();

    // 阶段 1 粗采样：间隔 = clamp(dur/60, 30s, 120s)，含首 1s 与尾-3s
    qint64 step = dur / kCoarseSampleCount;
    step = qBound<qint64>(kCoarseSampleMinMs, step, kCoarseSampleMaxMs);
    QVector<qint64> positions;
    for (qint64 p = 1000; p < dur - 3000; p += step)
        positions.append(p);
    if (dur > 6000)
        positions.append(dur - 3000);
    std::sort(positions.begin(), positions.end());
    QVector<qint64> dedup;
    for (qint64 p : positions) {
        if (dedup.isEmpty() || p - dedup.last() > 1000)
            dedup.append(p);
    }

    m_reconStage = ReconStage::Coarse;
    QDir().mkpath(evidenceDirFor(videoPath));
    emit progress(QStringLiteral("coarse %1 pts").arg(dedup.size()));
    m_ocrEngine->runAtPositions(videoPath, dedup, dur,
                                evidenceDirFor(videoPath), roi);
}

void CalibrationService::runQuickCheck(const QString &videoPath,
                                       qint64 durationMs,
                                       const QRectF &roi)
{
    if (videoPath.isEmpty() || isRunning())
        return;
    qint64 dur = durationMs;
    if (dur <= 0 && m_analysisEngine)
        dur = m_analysisEngine->trustedDurationMs(videoPath);
    const qint64 streamDur = probeVideoStreamDurationMs(videoPath);
    if (streamDur > 0 && (dur <= 0 || streamDur < dur))
        dur = streamDur;
    if (dur <= 0)
        return;
    QVector<qint64> positions;
    positions.append(1000);
    if (dur > 6000)
        positions.append(dur - 3000);
    m_pendingVideo = videoPath;
    m_quickPending = true;
    emit progress(QStringLiteral("quick check"));
    m_ocrEngine->runAtPositions(videoPath, positions, dur,
                                evidenceDirFor(videoPath), roi);
}

void CalibrationService::probeAbsStart(const QString &videoPath)
{
    if (videoPath.isEmpty())
        return;
    m_pendingVideo = videoPath;
    m_absPending = true;
    m_probeEngine->probe({videoPath});
}

void CalibrationService::cancel()
{
    m_ocrEngine->cancel();
    m_pendingVideo.clear();
    m_absPending = false;
    m_quickPending = false;
    m_reconStage = ReconStage::None;
    m_reconSamples.clear();
}

bool CalibrationService::isRunning() const
{
    return m_ocrEngine->isRunning();
}

// ---------------------------------------------------------------------------
// 时间重建（v1.2.1）：两级采样状态机
// ---------------------------------------------------------------------------
void CalibrationService::onAtPositionsFinished(
    const QVector<TimeCalibration::Sample> &samples)
{
    const QString video = m_pendingVideo;
    if (video.isEmpty())
        return;
    if (m_quickPending) {
        m_quickPending = false;
        // 首尾两点：整体速率 + 疑似变速判定（>15% 偏差）
        double rate = 1.0;
        bool suspicious = false;
        if (samples.size() >= 2) {
            const qint64 ds = samples.last().streamMs - samples.first().streamMs;
            const qint64 dw = samples.last().wallMs - samples.first().wallMs;
            if (ds > 0 && samples.first().wallMs > 0 && samples.last().wallMs > 0)
                rate = static_cast<double>(dw) / ds;
            suspicious = std::fabs(rate - 1.0) > 0.15;
        }
        emit quickCheckReady(video, rate, suspicious);
        return;
    }
    if (m_reconStage != ReconStage::None) {
        onReconBatchFinished(samples);
        return;
    }

    TimeCalibration cal;
    cal.source = TimeCalibration::Source::Ocr;
    cal.samples = samples;
    cal.dateKnown = true;
    cal.calibratedAtMs = QDateTime::currentMSecsSinceEpoch();
    double minConf = 1.0;
    for (const auto &s : samples)
        minConf = qMin(minConf, s.conf);
    cal.conf = minConf;
    cal.applyFit(TimeCalibration::fit(samples));
    emit threePointReady(video, cal);
}

void CalibrationService::onReconBatchFinished(
    const QVector<TimeCalibration::Sample> &samples)
{
    m_reconSamples += samples;
    if (m_reconStage == ReconStage::Coarse)
        analyzeCoarse();
    else if (m_reconStage == ReconStage::Boundary)
        finalizeReconstruction();
}

void CalibrationService::analyzeCoarse()
{
    // 与 domain detect 同源的粗分析：尖峰野点剔除 + 边界区间
    QVector<PiecewiseSample> ps;
    for (const auto &s : m_reconSamples) {
        PiecewiseSample p;
        p.streamMs = s.streamMs;
        p.wallMs = s.wallMs;
        p.used = s.used;
        p.conf = s.conf;
        ps.append(p);
    }
    const CoarseAnalysis ca = PiecewiseTimeMap::analyzeCoarse(ps);
    if (ps.size() < 2) {
        m_reconStage = ReconStage::None;
        emit failed(m_pendingVideo, QStringLiteral("insufficient samples"));
        return;
    }
    for (int i = 0; i < ca.ranges.size(); ++i)

    // 无边界：整体率正常 → 单段仿射（行为与三点一致）；异常 → 单段变速
    if (!ca.hasBoundary()) {
        const auto &s = m_reconSamples;
        const qint64 ds = s.last().streamMs - s.first().streamMs;
        const qint64 dw = s.last().wallMs - s.first().wallMs;
        const double overallRate = ds > 0 ? static_cast<double>(dw) / ds : 1.0;
        TimeCalibration cal;
        cal.source = TimeCalibration::Source::Ocr;
        cal.samples = m_reconSamples;
        cal.dateKnown = true;
        cal.calibratedAtMs = QDateTime::currentMSecsSinceEpoch();
        double minConf = 1.0;
        for (const auto &sp : m_reconSamples)
            minConf = qMin(minConf, sp.conf);
        cal.conf = minConf;
        if (std::fabs(overallRate - 1.0) <= PiecewiseTimeMap::kNormalRateDev) {
            // 正常录像：仿射拟合（与三点识别同路径，无需分段）
            cal.speedVariant = false;
            cal.applyFit(TimeCalibration::fit(m_reconSamples));
        } else {
            // 整体变速（无内部边界）：单段表 + 警告
            TimeSegment seg;
            seg.streamStartMs = m_reconSamples.first().streamMs;
            seg.wallStartMs = m_reconSamples.first().wallMs;
            seg.rate = overallRate;
            cal.piecewise.segments.append(seg);
            cal.piecewiseApplied = true;
            cal.speedVariant = true;
            cal.boundaryCount = 0;
            cal.totalWallSpanSec = dw / 1000.0;
            cal.rate = overallRate;
        }
        m_reconStage = ReconStage::None;
        m_reconSamples.clear();
        emit reconstructionReady(m_pendingVideo, cal);
        return;
    }

    // 有边界：阶段 2 边界加密——真实边界 ∈ [lo, hi]，
    // 加密范围 = 区间外扩 kBoundaryPadMs，步长 kBoundaryStepMs（跳过已有位置）
    // 总量超 kMaxRefinePoints 时按跳变幅度取 top（防超时）
    struct Job { qint64 lo, hi; double jump; };
    QVector<Job> jobs;
    for (int i = 0; i < ca.ranges.size(); ++i) {
        Job j;
        j.lo = ca.ranges[i].first;
        j.hi = ca.ranges[i].second;
        j.jump = (i < ca.jumps.size()) ? ca.jumps[i] : 0.0;
        jobs.append(j);
    }
    std::sort(jobs.begin(), jobs.end(),
              [](const Job &a, const Job &b) { return a.jump > b.jump; });
    // 加密点分配（v1.2.1 改进）：总量上限内保证每个边界都有保底加密
    // （跳变区间全宽，步长按配额自适应 ≥ 4s），强边界（jump 大）优先
    // 拿 2s 细步长——弱边界不再只有 ±1 粗间隔的定位精度。
    QVector<qint64> extra;
    int remaining = kMaxRefinePoints;
    const int totalJobs = jobs.size();
    for (int idx = 0; idx < totalJobs && remaining > 0; ++idx) {
        const Job &j = jobs[idx];
        const int jobsLeft = totalJobs - idx;
        const qint64 span = j.hi - j.lo + 2 * kBoundaryPadMs;
        // 理想细步长点数 vs 剩余均分配额
        const int ideal = static_cast<int>(span / kBoundaryStepMs) + 1;
        const int quota = qMax(kMinRefinePerBoundary,
                               qMin(ideal, remaining / jobsLeft));
        const qint64 step = qMax<qint64>(
            kBoundaryStepMs, span / qMax(1, quota));
        for (qint64 pos = j.lo - kBoundaryPadMs;
             pos <= j.hi + kBoundaryPadMs; pos += step) {
            if (extra.size() >= kMaxRefinePoints)
                break;
            if (pos < 0 || pos >= m_pendingDurationMs)
                continue;
            bool dup = false;
            for (const auto &s : m_reconSamples)
                if (qAbs(s.streamMs - pos) <= 500) {
                    dup = true;
                    break;
                }
            if (!dup)
                extra.append(pos);
        }
        remaining = kMaxRefinePoints - extra.size();
    }
    std::sort(extra.begin(), extra.end());
    QVector<qint64> dedup;
    for (qint64 p : extra) {
        if (dedup.isEmpty() || p - dedup.last() > 500)
            dedup.append(p);
    }
    m_reconStage = ReconStage::Boundary;
    emit progress(QStringLiteral("boundary %1 pts").arg(dedup.size()));
    m_ocrEngine->runAtPositions(m_pendingVideo, dedup, m_pendingDurationMs,
                                evidenceDirFor(m_pendingVideo), m_roi);
}

void CalibrationService::finalizeReconstruction()
{
    const QString video = m_pendingVideo;
    const qint64 dur = m_pendingDurationMs;
    const QVector<TimeCalibration::Sample> allSamples = m_reconSamples;
    m_reconStage = ReconStage::None;
    m_reconSamples.clear();

    QVector<PiecewiseSample> ps;
    for (const auto &s : allSamples) {
        PiecewiseSample p;
        p.streamMs = s.streamMs;
        p.wallMs = s.wallMs;
        p.used = s.used;
        p.conf = s.conf;
        ps.append(p);
    }
    PiecewiseDetectReport rep;
    PiecewiseTimeMap map = PiecewiseTimeMap::detect(ps, dur, &rep);
    if (!map.isValid()) {
        emit failed(video, QStringLiteral("reconstruction failed"));
        return;
    }

    // 音频校验（尽力而为：失败跳过，audioKnown=false）
    // 用 detect 的首尾 OSD 跨度（排序后首尾，不受错读点 min/max 干扰）
    qint64 audioDur = probeAudioDurationMs(video);
    const double wallSpanSec = rep.totalWallSpanSec;
    bool audioOk = true;
    if (audioDur > 0 && wallSpanSec > 0) {
        const double dev =
            std::fabs(audioDur / 1000.0 - wallSpanSec) / wallSpanSec;
        audioOk = dev <= kAudioConsistencyDev;
    }

    TimeCalibration cal;
    cal.source = TimeCalibration::Source::Ocr;
    cal.samples = allSamples;
    // v1.2.1：OCR 异常测点显式标记（留档/UI 展示，不参与拟合）
    for (int idx : rep.outlierIdx)
        if (idx >= 0 && idx < cal.samples.size())
            cal.samples[idx].ocrSuspicious = true;
    cal.dateKnown = true;
    cal.calibratedAtMs = QDateTime::currentMSecsSinceEpoch();
    cal.piecewise = map;
    cal.piecewiseApplied = true;
    cal.speedVariant = rep.speedVariant;
    cal.boundaryCount = rep.boundaryCount;
    cal.totalWallSpanSec = rep.totalWallSpanSec;
    cal.audioConsistent = audioOk;
    cal.audioKnown = audioDur > 0;
    double minConf = 1.0;
    for (const auto &s : allSamples)
        minConf = qMin(minConf, s.conf);
    cal.conf = minConf;
    emit reconstructionReady(video, cal);
}

void CalibrationService::onAtPositionsFailed(const QString &error)
{
    const QString video = m_pendingVideo;
    m_pendingVideo.clear();
    if (m_quickPending) {
        m_quickPending = false;
    }
    if (m_reconStage != ReconStage::None) {
        m_reconStage = ReconStage::None;
        m_reconSamples.clear();
    }
    emit failed(video, error);
}

void CalibrationService::onProbeFinished(const QVector<ProbeResult> &results)
{
    if (!m_absPending)
        return;
    m_absPending = false;
    for (const ProbeResult &pr : results) {
        if (pr.filePath == m_pendingVideo || results.size() == 1) {
            if (pr.absStartEpochMs > 0)
                emit absStartReady(m_pendingVideo, pr.absStartEpochMs);
            return;
        }
    }
}

// ---------------------------------------------------------------------------
// ffprobe 辅助（视频流/音频流时长；容器总时长取最长流会虚标）
// ---------------------------------------------------------------------------
static QString findFfprobePath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/ffmpeg/ffprobe.exe"),
        appDir + QStringLiteral("/ffprobe.exe"),
        QFileInfo(PythonAnalysisEngine::findFfmpegPath()).dir()
            .absoluteFilePath(QStringLiteral("ffprobe.exe")),
    };
    for (const QString &p : candidates)
        if (QFile::exists(p))
            return p;
    return QStringLiteral("ffprobe");   // 系统 PATH 兜底
}

static qint64 ffprobeStreamDurationMs(const QString &videoPath,
                                      const QString &streamSpec)
{
    const QString ffprobe = findFfprobePath();
    if (ffprobe.isEmpty())
        return 0;
    QProcess proc;
    proc.setProgram(ffprobe);
    proc.setArguments({QStringLiteral("-v"), QStringLiteral("error"),
                       QStringLiteral("-select_streams"), streamSpec,
                       QStringLiteral("-show_entries"),
                       QStringLiteral("stream=duration"),
                       QStringLiteral("-of"),
                       QStringLiteral("default=noprint_wrappers=1:nokey=1"),
                       videoPath});
    proc.start();
    if (!proc.waitForFinished(30000))
        return 0;
    const QString out = QString::fromUtf8(proc.readAllStandardOutput()).trimmed();
    bool ok = false;
    const double sec = out.toDouble(&ok);
    return (ok && sec > 0.0) ? static_cast<qint64>(sec * 1000.0) : 0;
}

qint64 CalibrationService::probeVideoStreamDurationMs(const QString &videoPath)
{
    return ffprobeStreamDurationMs(videoPath, QStringLiteral("v:0"));
}

qint64 CalibrationService::probeAudioDurationMs(const QString &videoPath)
{
    return ffprobeStreamDurationMs(videoPath, QStringLiteral("a:0"));
}

TimeCalibration CalibrationService::fromSinglePoint(qint64 streamMs,
                                                    qint64 wallMs,
                                                    TimeCalibration::Source src)
{
    TimeCalibration cal;
    cal.source = src;
    cal.offsetMs = wallMs - streamMs;
    cal.rate = 1.0;
    cal.dateKnown = true;
    cal.conf = (src == TimeCalibration::Source::Manual) ? 1.0 : 0.8;
    cal.calibratedAtMs = QDateTime::currentMSecsSinceEpoch();
    TimeCalibration::Sample s;
    s.streamMs = streamMs;
    s.wallMs = wallMs;
    cal.samples.append(s);
    return cal;
}

TimeCalibration CalibrationService::fromAbsStart(qint64 absStartEpochMs)
{
    TimeCalibration cal;
    cal.source = TimeCalibration::Source::AbsStart;
    cal.offsetMs = absStartEpochMs;
    cal.rate = 1.0;
    cal.dateKnown = true;
    cal.conf = 0.6;
    cal.calibratedAtMs = QDateTime::currentMSecsSinceEpoch();
    return cal;
}

// ---------------------------------------------------------------------------
// sidecar（V1 方案 §3.5）：<输出>.lumencal.json
// ---------------------------------------------------------------------------
bool CalibrationService::writeSidecar(const QString &outputPath,
                                      const QVector<SortEntry> &orderedEntries,
                                      QString *err)
{
    QJsonArray segs, gaps;
    qint64 streamCursor = 0;
    qint64 prevWallEnd = -1;
    for (const SortEntry &e : orderedEntries) {
        QJsonObject s;
        s[QStringLiteral("streamStartMs")] = static_cast<double>(streamCursor);
        s[QStringLiteral("streamEndMs")] =
            static_cast<double>(streamCursor + e.durationMs);
        s[QStringLiteral("wallStartMs")] = static_cast<double>(e.startMs);
        // 段速率：首尾 OCR 双墙钟可估；否则 1.0（未知）
        double rate = 1.0;
        if (e.startMs > 0 && e.ocrEndMs > e.startMs && e.durationMs > 0)
            rate = double(e.ocrEndMs - e.startMs) / double(e.durationMs);
        s[QStringLiteral("rate")] = rate;
        s[QStringLiteral("source")] = TimeCalibration::sourceToString(
            e.sourceKind == SortEvidenceKind::AbsStart
                ? TimeCalibration::Source::AbsStart
                : (e.sourceKind == SortEvidenceKind::Ocr
                       ? TimeCalibration::Source::Ocr
                       : TimeCalibration::Source::None));
        segs.append(s);

        // 缺口/重叠（墙钟域，推算口径）
        if (prevWallEnd > 0 && e.startMs > 0) {
            const qint64 gap = e.startMs - prevWallEnd;
            if (qAbs(gap) > kGapToleranceMs) {
                QJsonObject g;
                g[QStringLiteral("afterStreamMs")] = static_cast<double>(streamCursor);
                g[QStringLiteral("gapWallMs")] = static_cast<double>(gap);
                gaps.append(g);
            }
        }
        if (e.startMs > 0)
            prevWallEnd = e.startMs + e.durationMs;
        streamCursor += e.durationMs;
    }

    QJsonObject root;
    root[QStringLiteral("version")] = 1;
    root[QStringLiteral("segments")] = segs;
    root[QStringLiteral("gaps")] = gaps;

    const QString sidecarPath = outputPath + QStringLiteral(".lumencal.json");
    QFile f(sidecarPath);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        if (err)
            *err = QStringLiteral("cannot write %1").arg(sidecarPath);
        return false;
    }
    f.write(QJsonDocument(root).toJson(QJsonDocument::Compact));
    if (f.error() != QFile::NoError) {   // C2：写失败不静默
        if (err)
            *err = QStringLiteral("write failed: %1").arg(sidecarPath);
        return false;
    }
    return true;
}

bool CalibrationService::loadSidecar(const QString &videoPath,
                                     TimeCalibration *out, QString *warning)
{
    if (warning)
        warning->clear();
    const QString sidecarPath = videoPath + QStringLiteral(".lumencal.json");
    QFile f(sidecarPath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QJsonParseError perr{};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject())
        return false;
    const QJsonObject root = doc.object();
    if (root[QStringLiteral("version")].toInt() != 1)
        return false;
    const QJsonArray segs = root[QStringLiteral("segments")].toArray();
    if (segs.isEmpty())
        return false;

    // offset = 首段墙钟起点（Q-4：缺口时仍按首段线性，给警告）
    const qint64 wall0 = static_cast<qint64>(
        segs.first().toObject()[QStringLiteral("wallStartMs")].toDouble());
    if (wall0 <= 0)
        return false;

    // rate = 各段实测速率中位数（仅统计有实测的段）
    QVector<double> rates;
    for (const QJsonValue &v : segs) {
        const double r = v.toObject()[QStringLiteral("rate")].toDouble(1.0);
        if (std::fabs(r - 1.0) > 1e-12 && r > 0.5 && r < 2.0)
            rates.append(r);
    }
    double rate = 1.0;
    if (!rates.isEmpty()) {
        std::sort(rates.begin(), rates.end());
        rate = rates[rates.size() / 2];
    }

    TimeCalibration cal;
    cal.source = TimeCalibration::Source::Inherited;
    cal.offsetMs = wall0;
    cal.rate = rate;
    cal.rateApplied =
        std::fabs(rate - 1.0) > TimeCalibration::kMinSignificantRateDev;
    cal.dateKnown = true;
    cal.conf = 0.8;
    cal.calibratedAtMs = QDateTime::currentMSecsSinceEpoch();

    // 缺口警告（Q-4：必进报告，UI 同步提示）
    const QJsonArray gaps = root[QStringLiteral("gaps")].toArray();
    if (warning && !gaps.isEmpty()) {
        qint64 maxGap = 0;
        for (const QJsonValue &v : gaps)
            maxGap = qMax(maxGap, qAbs(static_cast<qint64>(
                v.toObject()[QStringLiteral("gapWallMs")].toDouble())));
        *warning = QStringLiteral("gaps:%1:%2")
            .arg(gaps.size()).arg(maxGap);   // C1：类型化前缀，UI 解析展示
    }

    if (out)
        *out = cal;
    return true;
}
