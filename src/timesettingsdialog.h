/**
 * @file timesettingsdialog.h
 * @brief 校时窗口：三点自动识别/absStart/手动 + 北京时间校验（v1.2.0）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-05
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计见 docs/V1_ERA_TECH_PLAN_CN.md §3.7（Q-3：一切候选仅预填，「采用」才生效）。
 */
#pragma once

#include <QDialog>
#include <QString>
#include "domain/time_calibration.h"

class CalibrationService;
class QLabel;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QDateTimeEdit;
class QLineEdit;
class QCheckBox;

class TimeSettingsDialog : public QDialog
{
    Q_OBJECT

public:
    /// sidecarWarning：拼接缺口警告（Q-4，空=无）
    TimeSettingsDialog(const QString &videoPath,
                       qint64 currentPosMs,
                       qint64 durationMs,
                       const TimeCalibration &current,
                       const QString &sidecarWarning,
                       CalibrationService *service,
                       QWidget *parent = nullptr);

    /// 用户点过「采用」时为真；calibration() 为应生效的新校时值
    bool applied() const { return m_applied; }
    TimeCalibration calibration() const { return m_working; }

signals:
    /// 校时已应用（非模态：主窗口收到后更新图表与状态栏）
    void calibrationApplied(const TimeCalibration &cal);

private slots:
    void onRunThreePoint();
    void onRunRecon();
    void onServiceProgress(const QString &stage);
    void onQuickCheckReady(const QString &videoPath, double overallRate,
                           bool suspicious);
    void onToggleDetails();
    void onThreePointReady(const QString &videoPath,
                           const TimeCalibration &proposed);
    void onReconstructionReady(const QString &videoPath,
                               const TimeCalibration &proposed);
    void onServiceFailed(const QString &videoPath, const QString &error);
    void onAbsStartReady(const QString &videoPath, qint64 absStartEpochMs);
    void onSampleItemChanged(QTableWidgetItem *item);
    void onAdoptFit();
    void onAdoptRecon();
    void onAdoptAbsStart();
    void onAdoptManual();
    void onTruthInputChanged();
    void onAdoptTruth();
    void onClearTruth();
    void onNoDriftCorrectionToggled(bool on);

private:
    void buildUi();
    void refreshWorkingSummary();
    void refitFromTable();
    void refitSummaryRefresh();
    void setBusy(bool busy, const QString &text = QString());
    static QString fmtWall(qint64 epochMs);
    static QString fmtOffset(qint64 offsetMs);

    QString m_videoPath;
    qint64 m_currentPosMs = 0;
    qint64 m_durationMs = 0;
    TimeCalibration m_working;          // 工作副本（采用候选时更新）
    TimeCalibration m_fitResult;        // 最近一次三点拟合候选
    QString m_sidecarWarning;
    CalibrationService *m_service = nullptr;   // 不持有
    bool m_applied = false;
    bool m_updatingTable = false;
    bool m_detailsVisible = false;      // 自动识别详情折叠
    bool m_taskStarted = false;         // 已启动识别/重建（区分预检失败）
    qint64 m_absStartMs = 0;

    // UI
    QLabel *m_videoLabel = nullptr;
    QLabel *m_currentLabel = nullptr;
    QLabel *m_workingSummary = nullptr;
    QLabel *m_quickLabel = nullptr;     // 秒级预检推荐条（v1.2.1）
    QPushButton *m_runBtn = nullptr;
    QLabel *m_progressLabel = nullptr;
    QPushButton *m_detailsBtn = nullptr;    // 查看细节 ▸/▾
    QWidget *m_detailsBox = nullptr;        // 详情折叠容器（测点表等）
    QTableWidget *m_sampleTable = nullptr;
    QLabel *m_fitLabel = nullptr;
    QLabel *m_fitWarningLabel = nullptr;
    QPushButton *m_adoptFitBtn = nullptr;
    // 时间重建（v1.2.1：变速/抽帧文件）
    QPushButton *m_reconBtn = nullptr;
    QLabel *m_reconSummaryLabel = nullptr;
    QTableWidget *m_segmentTable = nullptr;
    QPushButton *m_adoptReconBtn = nullptr;
    TimeCalibration m_reconResult;   // 最近一次重建候选
    QLabel *m_absLabel = nullptr;
    QPushButton *m_adoptAbsBtn = nullptr;
    QDateTimeEdit *m_manualEdit = nullptr;
    QPushButton *m_adoptManualBtn = nullptr;
    QLabel *m_monitorTimeLabel = nullptr;
    QDateTimeEdit *m_beijingEdit = nullptr;
    QLabel *m_truthPreviewLabel = nullptr;
    QLineEdit *m_truthNoteEdit = nullptr;
    QPushButton *m_adoptTruthBtn = nullptr;
    QPushButton *m_clearTruthBtn = nullptr;
    QCheckBox *m_noDriftCheck = nullptr;
    QLabel *m_sidecarWarnLabel = nullptr;
};
