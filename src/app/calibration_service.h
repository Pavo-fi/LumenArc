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
#include <QMap>
#include <QSet>
#include <QRectF>
#include <QVector>
#include <functional>
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

    /// 证据帧目录分流器（v1.3.0 M2 任务8）：注入后优先于默认老路径。
    /// 典型接线 = CaseManager::evidenceDirFor（入案→evidence/calibration/V###，
    /// 未入案/无案件→老路径，双模式分流集中于 CaseManager）。
    void setEvidenceDirResolver(
        std::function<QString(const QString &videoPath)> fn);

    /// 一键三点自动校时：首（1s）/当前位置/尾（时长-3s）三处取样 OCR，
    /// 最小二乘拟合仿射模型 → threePointReady（候选，不生效）。
    void runThreePoint(const QString &videoPath, qint64 currentPosMs,
                       qint64 durationMs, const QRectF &roi = QRectF());
    /// 时间重建（v1.2.1）：两级采样（粗采样分段 + 边界加密）→ 分段表。
    /// 正常录像（无边界且 |rate−1|≤1%）退化为单段仿射，行为与三点一致。
    /// 结果经 reconstructionReady 发出（候选，不生效）。
    void runReconstruction(const QString &videoPath, qint64 durationMs,
                           const QRectF &roi = QRectF());
    /// 秒级预检（v1.2.1）：首/尾两点 OCR 判"疑似变速文件"，
    /// 供校时窗口智能推荐（正常→① 自动校时；变速→⑤ 时间重建）。
    /// v1.2.2：时长足够时增采中点做三点共线校验（第三点确认）——
    /// 首尾任一点被 OCR 错读会误判变速 → 白跑数分钟重建。
    void runQuickCheck(const QString &videoPath, qint64 durationMs,
                       const QRectF &roi = QRectF());
    /// 第三点确认（v1.2.2）：预检三点共线校验。true = 任一点疑似错读
    ///（时间不可信，应拒绝自动路由）。samples 任意顺序（内部排序）；
    /// 少于 3 点无法校验返回 false（维持首尾两点旧语义）。
    static bool quickCheckSamplesInconsistent(
        const QVector<TimeCalibration::Sample> &samples);
    /// absStart（流内绝对起始，DHAV 等）快速候选探测。
    void probeAbsStart(const QString &videoPath);
    void cancel();
    bool isRunning() const;    /// 手动校时构造（单点，rate=1.0）
    static TimeCalibration fromSinglePoint(qint64 streamMs, qint64 wallMs,
                                           TimeCalibration::Source src);
    /// absStart 构造（流内 0 点对齐，单点）
    static TimeCalibration fromAbsStart(qint64 absStartEpochMs);

    /// sidecar（<输出>.lumencal.json）继承：读取并构造校时；warning 出参
    /// 携带缺口提示（Q-4：超 2s 容差必须警告，且进报告）。
    static bool loadSidecar(const QString &videoPath, TimeCalibration *out,
                            QString *warning);
    /// sidecar（<输出>.lumencal.json）写出（前处理 finalize 调用）：
    /// 逐段墙钟起点/速率 + 缺口表。
    /// v1.12.0（2026-08-20 拍板：校时反映到前处理产物时间轴）：
    /// - trimStartMs：重叠修剪段（>0 → 墙钟起点后移 trim×rate，流内时长扣减）；
    /// - skipFiles：整段丢弃（完全重叠）/转码失败的文件，不在产物中；
    /// - actualStreamMs：各段在拼接产物中的实测流内时长（转码件逐段实测——
    ///   实测转码段与源时长偏差 ±30~300ms，累积会污染尾部锚点；缺省回退
    ///   durationMs−trim）。
    static bool writeSidecar(const QString &outputPath,
                             const QVector<SortEntry> &orderedEntries,
                             QString *err,
                             const QMap<QString, qint64> &trimStartMs = {},
                             const QSet<QString> &skipFiles = {},
                             const QMap<QString, qint64> &actualStreamMs = {});

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
    /// 秒级预检完成：overallRate 首尾速率；suspicious = 疑似变速；
    /// ocrSuspect = 第三点确认失败（三点不成直线，首尾/中点任一点疑似错读，
    /// 时间不可信，调用方应拒绝自动路由并提示重新框选）
    void quickCheckReady(const QString &videoPath, double overallRate,
                         bool suspicious, bool ocrSuspect);
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
    /// 证据帧目录：分流器优先（案件模式），否则老路径 LumenArc_Calibration
    QString evidenceDirFor(const QString &videoPath) const;

    IAnalysisEngine *m_analysisEngine = nullptr;   // 不持有（引擎中立时长来源）
    std::function<QString(const QString &)> m_evidenceDirResolver;  // v1.3.0 M2
    TimestampOcrEngine *m_ocrEngine = nullptr;     // 自持有（与预处理窗口互不干扰）
    MediaProbeEngine *m_probeEngine = nullptr;     // 自持有
    QString m_pendingVideo;
    qint64 m_pendingDurationMs = 0;
    QRectF m_roi;                  ///< 用户框选时间戳区域（归一化 0~1）
    bool m_absPending = false;

    // 时间重建状态（两级采样）
    ReconStage m_reconStage = ReconStage::None;
    QVector<TimeCalibration::Sample> m_reconSamples;  ///< 粗采样+加密测点累积
    bool m_quickPending = false;   ///< 秒级预检进行中
};
