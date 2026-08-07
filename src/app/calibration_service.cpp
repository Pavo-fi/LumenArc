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
#include "infrastructure/timestamp_ocr_engine.h"
#include "domain/probe_result.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
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
                                       qint64 currentPosMs, qint64 durationMs)
{
    if (videoPath.isEmpty() || isRunning())
        return;

    // 可信时长缺失时经引擎中立接口补齐（R4：无 FFmpeg/Python 字样）
    qint64 dur = durationMs;
    if (dur <= 0 && m_analysisEngine)
        dur = m_analysisEngine->trustedDurationMs(videoPath);

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
                                evidenceDirFor(videoPath));
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
}

bool CalibrationService::isRunning() const
{
    return m_ocrEngine->isRunning();
}

void CalibrationService::onAtPositionsFinished(
    const QVector<TimeCalibration::Sample> &samples)
{
    const QString video = m_pendingVideo;
    m_pendingVideo.clear();
    if (video.isEmpty())
        return;

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

void CalibrationService::onAtPositionsFailed(const QString &error)
{
    const QString video = m_pendingVideo;
    m_pendingVideo.clear();
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
