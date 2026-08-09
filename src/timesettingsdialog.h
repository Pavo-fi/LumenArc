/**
 * @file timesettingsdialog.h
 * @brief 校时窗口：GO 一键自动校时 + 高级折叠区（v1.2.1 重构）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-09
 * @version 2.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计（参考拼接窗口 GO 键）：一个主按钮自动路由——
 * 快速检查（首尾 2 帧）→ 正常文件自动三点识别 / 疑似变速自动时间重建。
 * 非模态：任务后台进行，状态栏可见进度；关闭窗口不取消。
 * Q-3 语义保留：一切候选仅预填，「使用此结果」才生效。
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
class QWidget;

/// GO 一键校时状态机
enum class GoStage { Idle, Quick, Ocr, Recon, Done, Failed };

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

    /// 用户点过「使用」时为真；calibration() 为应生效的新校时值
    bool applied() const { return m_applied; }
    TimeCalibration calibration() const { return m_working; }

signals:
    /// 校时已应用（非模态：主窗口收到后更新图表与状态栏）
    void calibrationApplied(const TimeCalibration &cal);

private slots:
    void onRunGo();          ///< GO 主按钮：快速检查 → 自动路由
    void onCancelGo();
    void onServiceProgress(const QString &stage);
    void onQuickCheckReady(const QString &videoPath, double overallRate,
                           bool suspicious);
    void onThreePointReady(const QString &videoPath,
                           const TimeCalibration &proposed);
    void onReconstructionReady(const QString &videoPath,
                               const TimeCalibration &proposed);
    void onServiceFailed(const QString &videoPath, const QString &error);
    void onAbsStartReady(const QString &videoPath, qint64 absStartEpochMs);
    void onSampleItemChanged(QTableWidgetItem *item);
    void onUseResult();      ///< 结果区主按钮：应用 GO 候选（三点/重建）
    void onToggleDetails();
    void onRunReconForce();  ///< 高级区：强制变速重建（绕过预检）
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
    void setGoBusy(bool busy, const QString &stageText);
    void fillSampleTable(const TimeCalibration &proposed);
    void fillSegmentTable(const TimeCalibration &proposed);
    static QString fmtWall(qint64 epochMs);
    static QString fmtOffset(qint64 offsetMs);

    QString m_videoPath;
    qint64 m_currentPosMs = 0;
    qint64 m_durationMs = 0;
    TimeCalibration m_working;          // 工作副本（采用候选时更新）
    TimeCalibration m_fitResult;        // 最近一次三点拟合候选
    TimeCalibration m_reconResult;      // 最近一次重建候选
    QString m_sidecarWarning;
    CalibrationService *m_service = nullptr;   // 不持有
    bool m_applied = false;
    bool m_updatingTable = false;
    bool m_detailsVisible = false;      // 结果细节折叠
    GoStage m_goStage = GoStage::Idle;
    qint64 m_absStartMs = 0;

    // UI
    QLabel *m_videoLabel = nullptr;
    QLabel *m_workingSummary = nullptr;
    QPushButton *m_goBtn = nullptr;         // GO 主按钮
    QPushButton *m_cancelBtn = nullptr;
    QLabel *m_progressLabel = nullptr;
    QLabel *m_resultLabel = nullptr;        // 一句话结果
    QPushButton *m_detailsBtn = nullptr;    // 查看细节 ▸/▾
    QWidget *m_detailsBox = nullptr;        // 细节折叠容器（取样点表等）
    QTableWidget *m_sampleTable = nullptr;
    QLabel *m_fitWarningLabel = nullptr;
    QPushButton *m_useBtn = nullptr;        // 结果区「使用此结果」
    QCheckBox *m_noDriftCheck = nullptr;
    // 高级区（折叠）
    QWidget *m_advancedBox = nullptr;
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
    QPushButton *m_reconForceBtn = nullptr; // 强制变速重建
    QLabel *m_reconSummaryLabel = nullptr;  // 重建状态（高级区）
    QTableWidget *m_segmentTable = nullptr;
    QLabel *m_sidecarWarnLabel = nullptr;
};
