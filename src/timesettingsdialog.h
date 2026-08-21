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
#include <QRectF>
#include "domain/time_calibration.h"

class CalibrationService;
class QLabel;
class QPushButton;
class QTableWidget;
class QTableWidgetItem;
class QDateTimeEdit;
class QLineEdit;
class QCheckBox;
class QComboBox;
class QSpinBox;
class QWidget;

/// GO 一键校时状态机
enum class GoStage { Idle, Staged, Quick, Ocr, Recon, Done, Failed };
// Staged（v1.2.x UX）：框选就绪、待「确认并开始校时」——拖拽结束后校时窗
// 自动恢复并给出醒目确认按钮，用户显式确认才进入 Quick。


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
    /// 请求在主窗口视频上框选时间戳区域（GO 首次使用且无已存区域时）
    void requestTimestampRoi();
    /// 取消框选（主窗口退出框选模式）
    void cancelTimestampRoiRequest();
    /// GO 长任务（识别/重建，可达数分钟）完成或失败（v1.2.2）：
    /// 供主窗口在用户最小化等待时发 Windows toast 通知
    void goTaskFinished(const QString &title, const QString &message);

private slots:
    void onRunGo();          ///< GO 主按钮：快速检查 → 自动路由
    void onRoiButton();      ///< 手动重新框选时间戳区域
    void onCancelGo();
    void onServiceProgress(const QString &stage);
    void onQuickCheckReady(const QString &videoPath, double overallRate,
                           bool suspicious, bool ocrSuspect);
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
    /// v1.12.5 北京时间对时（拍板：校时图片框选 OCR / 手动两时间 / 直输偏移）
    void onTruthPhotoPick();          // 选图 → 框选对话框 → 引擎识别
    void onCalibPhotoFinished(bool ok,
                              const QVector<QPair<QString, double>> &monitorLines,
                              const QVector<QPair<QString, double>> &beijingLines,
                              const QString &error);
    void onAdoptTruthManualOffset();  // 方式三：直输偏移量「快/慢 X日X时X分X秒」
    void onNoDriftCorrectionToggled(bool on);

public slots:
    /// 主窗口框选完成回调：rect 归一化 0~1（无效 = 用户跳过）
    void setTimestampRoi(const QRectF &rect);
    /// 框选就绪（拖拽松开/叠加层确认）：恢复窗口并给出「确认并开始校时」；
    /// rect 无效 = 用户跳过框选 → GO 流程直接自动扫描开始
    void stageTimestampRoi(const QRectF &rect);

private:
    void buildUi();
    QWidget *buildUsageBanner();   ///< 顶部用法说明（参考拼接窗口格式横幅）
    void startGo();                ///< 实际启动 GO（ROI 就绪后）
    void refreshWorkingSummary();
    void refitFromTable();
    void refitSummaryRefresh();
    void applyWorking(const TimeCalibration &cal);   ///< 应用候选并通知主窗口
    void maybeAutoApply();                           ///< GO 完成且无异常 → 自动应用
    void setGoBusy(bool busy, const QString &stageText);
    void fillSampleTable(const TimeCalibration &proposed);
    void fillSegmentTable(const TimeCalibration &proposed);
    static QString fmtWall(qint64 epochMs);
    static QString fmtOffset(qint64 offsetMs);
    /// v1.12.5 拍板表述：「（比北京时间）快/慢 X日X时X分X秒」
    QString fmtOffsetVerbose(qint64 offsetMs) const;

    QString m_videoPath;
    qint64 m_currentPosMs = 0;
    qint64 m_durationMs = 0;
    QRectF m_roi;                    ///< 时间戳区域（归一化 0~1，无效=未框选）
    bool m_waitingRoi = false;       ///< 等待主窗口框选结果
    bool m_roiRetried = false;       ///< v1.7.1：框选失败后已自动全画面重试
    TimeCalibration m_working;          // 工作副本（采用候选时更新）
    TimeCalibration m_fitResult;        // 最近一次三点拟合候选
    TimeCalibration m_reconResult;      // 最近一次重建候选
    QString m_sidecarWarning;
    CalibrationService *m_service = nullptr;   // 不持有
    bool m_applied = false;
    bool m_updatingTable = false;
    bool m_detailsVisible = false;      // 结果细节折叠
    bool m_autoApplied = false;         // GO 完成已自动应用（防重启用按钮）
    GoStage m_goStage = GoStage::Idle;
    qint64 m_absStartMs = 0;

    // UI
    QLabel *m_videoLabel = nullptr;
    QLabel *m_workingSummary = nullptr;
    QPushButton *m_goBtn = nullptr;         // GO 主按钮
    QPushButton *m_roiBtn = nullptr;        // 框选时间戳区域
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
    QDateTimeEdit *m_monitorEdit = nullptr;   // v1.12.5 手输：监控主机时间
    QComboBox *m_offsetDirCombo = nullptr;    // v1.12.5 直输偏移：快/慢
    QSpinBox *m_offsetDays = nullptr;         // v1.12.5 直输偏移：日/时/分/秒
    QSpinBox *m_offsetHours = nullptr;
    QSpinBox *m_offsetMins = nullptr;
    QSpinBox *m_offsetSecs = nullptr;
    QPushButton *m_truthPhotoBtn = nullptr;   // v1.12.5 校时图片识别入口
    QLabel *m_truthPreviewLabel = nullptr;
    QLineEdit *m_truthNoteEdit = nullptr;
    QPushButton *m_adoptTruthBtn = nullptr;
    QPushButton *m_clearTruthBtn = nullptr;
    // v1.12.5 校时图片流程待应用（识别完成回传后写入 m_working）
    QString m_pendingTruthImage;
    QRect   m_pendingTruthBox1;
    QRect   m_pendingTruthBox2;
    QPushButton *m_reconForceBtn = nullptr; // 强制变速重建
    QLabel *m_reconSummaryLabel = nullptr;  // 重建状态（高级区）
    QTableWidget *m_segmentTable = nullptr;
    QLabel *m_sidecarWarnLabel = nullptr;
};
