/**
 * @file preprocessing_coordinator.h
 * @brief 前处理-流程编排协调器（app 层，状态机 SSOT）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计见 docs/PREPROCESSING_TECH_DESIGN_CN.md §6.3 / §7。
 * 状态机：Idle → Probing → Ocr → Sorting → UserConfirm → Precheck
 *         → Transcoding ⇄ Concat → Done / Failed / Cancelled
 * R4：本层不出现 FFmpeg/Python 字样；引擎经 infrastructure 接口使用，
 * 可信时长经 IAnalysisEngine 引擎中立接口获取。
 */
#pragma once

#include <QObject>
#include <QMap>
#include <QSet>
#include <QPair>
#include "domain/overlap_cut.h"
#include <QVector>
#include <QStringList>
#include "domain/preprocess_task.h"
#include "domain/probe_result.h"
#include "domain/ocr_result.h"
#include "domain/sort_model.h"
#include "domain/concat_precheck.h"

class IAnalysisEngine;
class MediaProbeEngine;
class TimestampOcrEngine;
class ConcatEngine;
class TranscodeEngine;

class PreprocessingCoordinator : public QObject
{
    Q_OBJECT

public:
    explicit PreprocessingCoordinator(QObject *parent = nullptr);
    ~PreprocessingCoordinator() override;

    /// 可信时长来源（引擎中立接口注入，R4；空 = 仅用容器时长）
    void setAnalysisEngine(IAnalysisEngine *engine);
    /// 流内绝对时间可信的文件跳过 OCR（默认 true；仅截证据帧）
    void setSkipOcrWhenAbsStart(bool on) { m_skipOcrWhenAbsStart = on; }

    void begin(const QStringList &files);
    /// 导入即自动排序：探测完成后自动执行画面时间识别与排序（可选，非强制）
    void beginWithAutoSort(const QStringList &files);
    /// 手动触发自动排序（UserConfirm 阶段可选：以画面/流内时间重排）
    void runAutoSort();
    void confirmOrder();                    // UserConfirm → Precheck
    /// 开始执行。UserConfirm 或 Precheck 阶段均可调用（前者自动先确认顺序）。
    void startProcessing(const ProcessingOptions &opts);
    void cancel();

    TaskPhase phase() const { return m_phase; }
    QVector<SortGroup> groups() const { return m_groups; }
    QMap<QString, ProbeResult> probeMap() const { return m_probes; }
    QMap<QString, OcrResult> ocrMap() const { return m_ocrs; }
    QMap<QString, PrecheckResult> precheckMap() const { return m_prechecks; }
    QString evidenceDir() const { return m_evidenceDir; }

    /// 人工兜底：看图手输时间戳（UserConfirm 阶段，标记 source=Manual）
    void applyManualTimestamp(const QString &file, qint64 wallStartMs);
    /// 拖拽微调：整组重排（UserConfirm 阶段，重算连续性提示）
    void applyGroupOrder(const QString &channel, const QStringList &orderedPaths);
    /// 人工分组调整（UserConfirm 阶段）
    void applyGrouping(const QString &file, const QString &channel);

signals:
    void phaseChanged(TaskPhase phase);
    void progress(int percent, const QString &detail);
    void probeDone(const QVector<ProbeResult> &results);
    void ocrDone(const QVector<OcrResult> &results);
    void evidenceReady(const QVector<SortGroup> &groups);
    void precheckReady(const QMap<QString, PrecheckResult> &byGroup);
    /// v1.7.0 M2：Precheck 检测到组内时间重叠（参数=重叠组名列表）
    void overlapDetected(const QStringList &channels);
    void finished(const PreprocessReport &report);
    void failed(PreprocessError error, const QString &detail);
    void logLine(const QString &line);

private slots:
    void onProbeFinished(const QVector<ProbeResult> &results);

public slots:
    /// 后台可信时长计算回投（QThreadPool → UI 线程，functor 投递）
    void onTrustedDurationsReady(const QMap<QString, qint64> &durations);
    /// v1.7.0 M2：用户选择是否修剪重叠段（Q-17：剪后段开头保前段完整）
    void setTrimOverlap(bool trim);

private slots:
    void onOcrFinished(const QVector<OcrResult> &results);
    void onOcrEngineError(PreprocessError error, const QString &detail);
    void onTranscodeOneFinished(const QString &outputPath);
    void onTranscodeOneFailed(PreprocessError error, const QString &detail);
    void onConcatOneFinished(const QString &outputPath);
    void onConcatOneFailed(PreprocessError error, const QString &detail);

private:
    void setPhase(TaskPhase phase);
    void log(const QString &line);
    void logProbeStats();                   // 帧率/编码/分辨率统计 + 统一帧率预告
    void sortFilesByNameTime(QStringList &files);  // 文件名时间戳排序（2026-08）
    QMap<QString, qint64> buildDurMap(const QMap<QString, qint64> &trusted) const;
    void buildListOrderGroups();            // 未自动排序：按导入顺序成组
    void runSorting();
    void runPrecheck();
    void startNextTranscode();
    void scheduleNextTranscode();   // v1.7.0 M3：双引擎并行调度
    QString groupOfFile(const QString &file) const;   // 调度用
    void startNextConcat();
    void finalize();
    QString allocateOutput(const QString &dir, const QString &base) const;
    qint64 durationOf(const QString &file) const;
    void writeOperationsLog(const QString &dir) const;
    TranscodeRequest buildTranscodeRequest(const QString &file) const;  // v1.7.0
    /// v1.7.0 M2：按剪切计划取该文件的流内保留区间（无则 0,0）
    QPair<qint64, qint64> trimRangeFor(const QString &file) const;

    IAnalysisEngine *m_analysis = nullptr;      // 不持有
    bool m_skipOcrWhenAbsStart = true;
    bool m_autoSortAfterProbe = false;          // beginWithAutoSort 链式标志
    MediaProbeEngine *m_probeEngine;
    TimestampOcrEngine *m_ocrEngine;
    ConcatEngine *m_concatEngine;
    TranscodeEngine *m_transcodeEngine;
    TranscodeEngine *m_transcodeEngine2 = nullptr;   // v1.7.0 M3：组级并行 N=2

    TaskPhase m_phase = TaskPhase::Idle;
    QStringList m_files;
    QMap<QString, ProbeResult> m_probes;
    float m_unifiedFps = 0.0f;                  // 统一 CFR 帧率（多段帧率不一时取全局最大 avg，2026-08）
    QMap<QString, bool> m_groupCopyAudio;       // 组→音频直拷标志（组内 AAC 同参才直拷，2026-08）
    QMap<QString, OcrResult> m_ocrs;
    QVector<SortGroup> m_groups;
    QMap<QString, PrecheckResult> m_prechecks;
    QMap<QString, QString> m_channelOverrides;
    ProcessingOptions m_opts;

    QString m_evidenceDir;      // 会话证据目录（临时 → 完成时迁入输出目录）
    QString m_outputDir;
    QStringList m_log;

    // 执行队列状态
    QStringList m_transcodeQueue;               // 待转码文件
    QMap<QString, QString> m_transcoded;        // 原路径 -> 转码输出
    QStringList m_transcodeFailed;
    QStringList m_concatQueue;                  // 待拼接组（channel）
    QString m_currentTranscode;
    QString m_currentConcatGroup;
    QMap<QString, QString> m_actions;           // 报告：处理动作
    QMap<QString, QString> m_outputs;           // 报告：输出文件
    QMap<QString, QString> m_concatOutputs;     // channel -> 拼接输出
    PreprocessReport m_report;

    // v1.7.0 M3：双引擎并行状态
    QMap<TranscodeEngine *, QString> m_engineFile;   // 引擎 -> 当前文件
    QSet<QString> m_activeGroups;                    // 进行中文件的组
    int m_lastTxPercent = 0;                         // 进度单调钳位
    // v1.7.0 M2：重叠剪切
    bool m_trimOverlap = false;                 // 用户选择修剪（默认关）
    QVector<CutPlan> m_cutPlans;                // 剪切计划（trimOverlap 时生效）
    QStringList m_overlapChannels;              // 检测到的重叠组（UI 提示）
};

Q_DECLARE_METATYPE(QVector<OcrResult>)
Q_DECLARE_METATYPE(QVector<SortGroup>)
Q_DECLARE_METATYPE(PreprocessReport)
