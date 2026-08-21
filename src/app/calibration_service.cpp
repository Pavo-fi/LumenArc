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
#include "infrastructure/tool_paths.h"
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
#include <utility>

namespace {
/// Q-6 过渡期证据帧默认目录（独立模式老路径；案件模式经分流器 override）
QString defaultEvidenceDirFor(const QString &videoPath)
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

void CalibrationService::setEvidenceDirResolver(
    std::function<QString(const QString &videoPath)> fn)
{
    m_evidenceDirResolver = std::move(fn);
}

QString CalibrationService::evidenceDirFor(const QString &videoPath) const
{
    // v1.3.0 M2 任务8：分流器（CaseManager）优先；未注入走老路径
    if (m_evidenceDirResolver) {
        const QString r = m_evidenceDirResolver(videoPath);
        if (!r.isEmpty())
            return r;
    }
    return defaultEvidenceDirFor(videoPath);
}

CalibrationService::CalibrationService(IAnalysisEngine *analysisEngine,
                                       QObject *parent)
    : QObject(parent)
    , m_analysisEngine(analysisEngine)
    , m_ocrEngine(new TimestampOcrEngine(this))
{
    connect(m_ocrEngine, &TimestampOcrEngine::atPositionsFinished,
            this, &CalibrationService::onAtPositionsFinished);
    connect(m_ocrEngine, &TimestampOcrEngine::atPositionsFailed,
            this, &CalibrationService::onAtPositionsFailed);
    connect(m_ocrEngine, &TimestampOcrEngine::ocrProgress, this,
            [this](int done, int total, const QString &) {
                emit progress(QStringLiteral("ocr %1/%2").arg(done).arg(total));
            });
    // v1.12.5 北京时间对时：校时照片两框识别结果转发
    connect(m_ocrEngine, &TimestampOcrEngine::calibPhotoFinished,
            this, &CalibrationService::calibPhotoFinished);
}

void CalibrationService::runCalibPhoto(const QString &imagePath,
                                       const QRect &monitorBox,
                                       const QRect &beijingBox)
{
    m_ocrEngine->runCalibPhoto(imagePath, monitorBox, beijingBox);
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
    if (dur > 12000)
        positions.append(dur / 2);   // v1.2.2 第三点（中点）共线校验
    if (dur > 6000)
        positions.append(dur - 3000);
    m_pendingVideo = videoPath;
    m_quickPending = true;
    emit progress(QStringLiteral("quick check"));
    m_ocrEngine->runAtPositions(videoPath, positions, dur,
                                evidenceDirFor(videoPath), roi);
}

bool CalibrationService::quickCheckSamplesInconsistent(
    const QVector<TimeCalibration::Sample> &samples)
{
    if (samples.size() < 3)
        return false;   // 两点无法校验（维持原首尾语义）
    auto sorted = samples;
    std::sort(sorted.begin(), sorted.end(),
              [](const TimeCalibration::Sample &a,
                 const TimeCalibration::Sample &b) {
                  return a.streamMs < b.streamMs;
              });
    const qint64 ds = sorted.last().streamMs - sorted.first().streamMs;
    const qint64 dw = sorted.last().wallMs - sorted.first().wallMs;
    if (ds <= 0 || sorted.first().wallMs <= 0 || sorted.last().wallMs <= 0)
        return false;
    // OSD 分辨率 1s → 容差 max(5s, 2%跨度)；错一位分钟 = 60s+ 必超阈。
    const double threshMs = qMax(5000.0, 0.02 * ds);
    for (int i = 1; i + 1 < sorted.size(); ++i) {
        const auto &m = sorted[i];
        if (m.wallMs <= 0)
            return true;
        const double pred = sorted.first().wallMs
            + static_cast<double>(dw)
                  * (m.streamMs - sorted.first().streamMs) / ds;
        if (std::fabs(m.wallMs - pred) > threshMs)
            return true;
    }
    return false;
}

void CalibrationService::cancel()
{
    m_ocrEngine->cancel();
    m_pendingVideo.clear();
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
        // at 模式按位置分片并行，聚合顺序 = 完成顺序（随机）：先按 streamMs
        // 排序，首尾语义才成立（与 onReconBatchFinished 同一防御）。
        auto sorted = samples;
        std::sort(sorted.begin(), sorted.end(),
                  [](const TimeCalibration::Sample &a,
                     const TimeCalibration::Sample &b) {
                      return a.streamMs < b.streamMs;
                  });
        // 首尾两点：整体速率 + 疑似变速判定（>15% 偏差）
        double rate = 1.0;
        bool suspicious = false;
        bool ocrSuspect = false;
        if (sorted.size() >= 2) {
            const qint64 ds = sorted.last().streamMs - sorted.first().streamMs;
            const qint64 dw = sorted.last().wallMs - sorted.first().wallMs;
            if (ds > 0 && sorted.first().wallMs > 0 && sorted.last().wallMs > 0)
                rate = static_cast<double>(dw) / ds;
            suspicious = std::fabs(rate - 1.0) > 0.15;
            // 第三点确认（v1.2.2）：中点墙钟必须落在首尾直线上，
            // 否则首尾/中点任一点疑似错读 → 拒绝路由，防误判变速白跑重建。
            ocrSuspect = quickCheckSamplesInconsistent(sorted);
        }
        emit quickCheckReady(video, rate, suspicious, ocrSuspect);
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
    // at 模式按位置分片并行：聚合顺序 = 完成顺序（随机），必须按 streamMs
    // 排序——下游 no-boundary 分支与 overallRate 依赖 first/last 语义。
    std::sort(m_reconSamples.begin(), m_reconSamples.end(),
              [](const TimeCalibration::Sample &a,
                 const TimeCalibration::Sample &b) {
                  return a.streamMs < b.streamMs;
              });
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
    if (ps.size() < 2) {
        m_reconStage = ReconStage::None;
        m_reconSamples.clear();
        emit failed(m_pendingVideo, QStringLiteral("insufficient samples"));
        return;
    }
    const CoarseAnalysis ca = PiecewiseTimeMap::analyzeCoarse(ps);

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
        if (!rep.rejectedNonMonotonic) {
            emit failed(video, QStringLiteral("reconstruction failed"));
            return;
        }
        // v1.12.8（天河案实测）：OCR 成片误读（月份位）造伪边界致分段
        // 墙钟倒流被物理闸拒绝 → 回落稳健仿射：偏移 = 干净测点的
        // r=wall−stream 中位数，rate 固定 1.0（该类文件实为正常录像）。
        QVector<qint64> offs;
        for (int i = 0; i < allSamples.size(); ++i) {
            const auto &s = allSamples[i];
            if (!s.used || s.wallMs <= 0)
                continue;
            if (rep.outlierIdx.contains(i))
                continue;
            offs.append(s.wallMs - s.streamMs);
        }
        if (offs.isEmpty()) {
            emit failed(video, QStringLiteral("reconstruction failed"));
            return;
        }
        std::sort(offs.begin(), offs.end());
        TimeCalibration cal;
        cal.source = TimeCalibration::Source::Ocr;
        cal.samples = allSamples;
        for (int idx : rep.outlierIdx)
            if (idx >= 0 && idx < cal.samples.size())
                cal.samples[idx].ocrSuspicious = true;
        cal.offsetMs = offs[offs.size() / 2];
        cal.rate = 1.0;
        cal.rateApplied = false;
        cal.dateKnown = true;
        cal.conf = 0.5;   // 回落结果降置信，UI 提示复核
        cal.calibratedAtMs = QDateTime::currentMSecsSinceEpoch();
        cal.piecewiseApplied = false;
        cal.speedVariant = false;
        cal.boundaryCount = 0;
        cal.audioConsistent = true;
        cal.audioKnown = false;
        emit reconstructionReady(video, cal);
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

// ---------------------------------------------------------------------------
// ffprobe 辅助（视频流/音频流时长；容器总时长取最长流会虚标）
// ---------------------------------------------------------------------------
static QString findFfprobePath()
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QStringList candidates = {
        appDir + QStringLiteral("/ffmpeg/ffprobe.exe"),
        appDir + QStringLiteral("/ffprobe.exe"),
        QFileInfo(ToolPaths::findFfmpegPath()).dir()
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
    // ffprobe.exe（动态版）的 av*.dll 部署在应用根目录而非 ffmpeg/ 子目录；
    // DLL 搜索含工作目录 → 显式钉到应用目录，否则 cwd 不同时静默启动失败
    // （视频流时长防御失效，容器虚标时尾部取样落空 → 三点退化）
    proc.setWorkingDirectory(QCoreApplication::applicationDirPath());
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

// ---------------------------------------------------------------------------
// sidecar（V1 方案 §3.5）：<输出>.lumencal.json
// ---------------------------------------------------------------------------
bool CalibrationService::writeSidecar(const QString &outputPath,
                                      const QVector<SortEntry> &orderedEntries,
                                      QString *err,
                                      const QMap<QString, qint64> &trimStartMs,
                                      const QSet<QString> &skipFiles,
                                      const QMap<QString, qint64> &actualStreamMs)
{
    QJsonArray segs, gaps;
    qint64 streamCursor = 0;
    qint64 prevWallEnd = -1;
    for (const SortEntry &e : orderedEntries) {
        // v1.12.0：不在产物中的段（整段丢弃/转码失败）跳过
        if (skipFiles.contains(e.filePath))
            continue;
        // 段速率：首尾 OCR 双墙钟可估；否则 1.0（未知）
        // v1.12.0 修复分母：尾帧墙钟对应的是尾帧流内实测位置（通常比总时长
        // 提前 1~3s）——旧按 durationMs 计得系统性 ~0.94 偏慢速率，越秀案实测
        // 实锤；缺尾帧位置时回退总时长。首尾矛盾段尾帧已被排序器弃用（ocrEnd=0）
        double rate = 1.0;
        const qint64 rateSpan = e.ocrEndFrameRelMs > 0 ? e.ocrEndFrameRelMs
                                                       : e.durationMs;
        if (e.startMs > 0 && e.ocrEndMs > e.startMs && rateSpan > 0)
            rate = double(e.ocrEndMs - e.startMs) / double(rateSpan);
        // v1.12.0：重叠修剪——保留部分墙钟起点后移 trim×rate，流内时长扣减
        const qint64 trim = qMax<qint64>(0, trimStartMs.value(e.filePath, 0));
        const qint64 keptStreamMs = qMax<qint64>(0, e.durationMs - trim);
        if (keptStreamMs <= 0)
            continue;
        const qint64 wallStart = e.startMs > 0
            ? e.startMs + static_cast<qint64>(
                  std::llround(rate * static_cast<double>(trim)))
            : 0;
        // 产物中的实际流内时长：实测优先（转码段与源时长有 ±30~300ms 偏差）
        const qint64 streamDur = actualStreamMs.value(e.filePath, 0) > 0
            ? actualStreamMs.value(e.filePath) : keptStreamMs;
        QJsonObject s;
        s[QStringLiteral("streamStartMs")] = static_cast<double>(streamCursor);
        s[QStringLiteral("streamEndMs")] =
            static_cast<double>(streamCursor + streamDur);
        s[QStringLiteral("wallStartMs")] = static_cast<double>(wallStart);
        s[QStringLiteral("rate")] = rate;
        s[QStringLiteral("source")] = TimeCalibration::sourceToString(
            e.sourceKind == SortEvidenceKind::AbsStart
                ? TimeCalibration::Source::AbsStart
                : (e.sourceKind == SortEvidenceKind::Ocr
                       ? TimeCalibration::Source::Ocr
                       : TimeCalibration::Source::None));
        segs.append(s);

        // 缺口/重叠（墙钟域，段内按 rate 推算末端）
        if (prevWallEnd > 0 && wallStart > 0) {
            const qint64 gap = wallStart - prevWallEnd;
            if (qAbs(gap) > kGapToleranceMs) {
                QJsonObject g;
                g[QStringLiteral("afterStreamMs")] = static_cast<double>(streamCursor);
                g[QStringLiteral("gapWallMs")] = static_cast<double>(gap);
                gaps.append(g);
            }
        }
        if (wallStart > 0)
            prevWallEnd = wallStart + static_cast<qint64>(
                std::llround(rate * static_cast<double>(keptStreamMs)));
        streamCursor += streamDur;
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
    // v1.12.3：实现下沉 domain（loadSidecarCalibration，SSOT）——
    // cam_timeline 等轻量调用方直用域函数，本壳保持既有 API 不变
    return loadSidecarCalibration(videoPath, out, warning);
}
