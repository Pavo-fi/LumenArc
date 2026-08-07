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

signals:
    void progress(const QString &stage);
    void threePointReady(const QString &videoPath,
                         const TimeCalibration &proposed);
    void absStartReady(const QString &videoPath, qint64 absStartEpochMs);
    void failed(const QString &videoPath, const QString &error);

private slots:
    void onAtPositionsFinished(const QVector<TimeCalibration::Sample> &samples);
    void onAtPositionsFailed(const QString &error);
    void onProbeFinished(const QVector<ProbeResult> &results);

private:
    IAnalysisEngine *m_analysisEngine = nullptr;   // 不持有（引擎中立时长来源）
    TimestampOcrEngine *m_ocrEngine = nullptr;     // 自持有（与预处理窗口互不干扰）
    MediaProbeEngine *m_probeEngine = nullptr;     // 自持有
    QString m_pendingVideo;
    qint64 m_pendingDurationMs = 0;
    bool m_absPending = false;
};
