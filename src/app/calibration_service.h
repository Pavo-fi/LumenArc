/**
 * @file calibration_service.h
 * @brief 校时服务（app 层）：三点自动校时/absStart 候选/sidecar 继承
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-05
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计见 docs/V1_ERA_TECH_PLAN_CN.md §3（v1.2.0，Q-1~Q-6 拍板）。
 * 产出均为"候选校时"——生效与否由 UI（TimeSettingsDialog「采用」）决定（Q-3）。
 */
#pragma once

#include <QObject>
#include <QString>
#include <QVector>
#include "domain/time_calibration.h"
#include "domain/sort_model.h"
#include "domain/probe_result.h"

class IAnalysisEngine;
class MediaProbeEngine;
class TimestampOcrEngine;

/// 时间重建阶段（两级采样状态机）
enum class ReconStage { None, Coarse, Boundary };

class CalibrationService : public QObject
{
    Q_OBJECT

public:
    explicit CalibrationService(IAnalysisEngine *analysisEngine,
                                QObject *parent = nullptr);

    /// python 路径注入（转发 OCR 引擎；空 = 自动探测）
    void setPythonExecutable(const QString &path);

    /// 一键三点自动校时：首（1s）/当前位置/尾（时长-3s）三处取样 OCR，
    /// 最小二乘拟合仿射模型 → threePointReady（候选，不生效）。
    void runThreePoint(const QString &videoPath, qint64 currentPosMs,
                       qint64 durationMs);
    /// 时间重建（v1.2.1）：两级采样（粗采样分段 + 边界加密）→ 分段表。
    /// 正常录像（无边界且 |rate−1|≤1%）退化为单段仿射，行为与三点一致。
    /// 结果经 reconstructionReady 发出（候选，不生效）。
    void runReconstruction(const QString &videoPath, qint64 durationMs);
    /// absStart（流内绝对起始，DHAV 等）快速候选探测。
    void probeAbsStart(const QString &videoPath);
    void cancel();
    bool isRunning() const;

    /// 手动校时构造（单点，rate=1.0）
    static TimeCalibration fromSinglePoint(qint64 streamMs, qint64 wallMs,
                                           TimeCalibration::Source src);
    /// absStart 构造（流内 0 点对齐，单点）
    static TimeCalibration fromAbsStart(qint64 absStartEpochMs);

    /// sidecar（<输出>.lumencal.json）继承：读取并构造校时；warning 出参
    /// 携带缺口提示（Q-4：超 2s 容差必须警告，且进报告）。
    static bool loadSidecar(const QString &videoPath, TimeCalibration *out,
                            QString *warning);
    /// sidecar 写出（前处理 finalize 调用）：逐段墙钟起点/速率 + 缺口表。
    static bool writeSidecar(const QString &outputPath,
                             const QVector<SortEntry> &orderedEntries,
                             QString *err);

    /// 视频流时长（ffprobe -select_streams v:0）。容器总时长取音画最长流，
    /// 音画不同长时会虚标（B3一单元客梯：音轨 102min/画面 74min）→
    /// 校时尾部取样必须用视频流时长（v1.2.1 防御）。0=探测失败。
    static qint64 probeVideoStreamDurationMs(const QString &videoPath);
    /// 音频流时长（ffprobe -select_streams a:0），用于 OSD 跨度校验。0=无/失败。
    static qint64 probeAudioDurationMs(const QString &videoPath);

signals:
    void progress(const QString &stage);
    void threePointReady(const QString &videoPath,
                         const TimeCalibration &proposed);
    /// 时间重建完成（候选，不生效；proposed 含 piecewise 分段表）
    void reconstructionReady(const QString &videoPath,
                             const TimeCalibration &proposed);
    void absStartReady(const QString &videoPath, qint64 absStartEpochMs);
    void failed(const QString &videoPath, const QString &error);

private slots:
    void onAtPositionsFinished(const QVector<TimeCalibration::Sample> &samples);
    void onAtPositionsFailed(const QString &error);
    void onProbeFinished(const QVector<ProbeResult> &results);

private:
    void onReconBatchFinished(const QVector<TimeCalibration::Sample> &samples);
    void analyzeCoarse();        ///< 粗采样完成后：判边界 → 转加密或出结果
    void finalizeReconstruction(); ///< 加密完成后：detect → 构造候选

    IAnalysisEngine *m_analysisEngine = nullptr;   // 不持有（引擎中立时长来源）
    TimestampOcrEngine *m_ocrEngine = nullptr;     // 自持有（与预处理窗口互不干扰）
    MediaProbeEngine *m_probeEngine = nullptr;     // 自持有
    QString m_pendingVideo;
    qint64 m_pendingDurationMs = 0;
    bool m_absPending = false;

    // 时间重建状态（两级采样）
    ReconStage m_reconStage = ReconStage::None;
    QVector<TimeCalibration::Sample> m_reconSamples;  ///< 粗采样+加密测点累积
};
