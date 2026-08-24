/**
 * @file preprocesswindow.h
 * @brief 前处理-素材转码拼接独立任务窗口（四幕流程，ui 层）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计见 docs/PREPROCESSING_UI_REDESIGN_CN.md（v1.0 评审通过版）。
 * 四幕：① 导入素材 → ② 校对顺序（时间线+证据卡片）→ ③ 拼接设置 → ④ 执行与报告。
 * 本层仅经 PreprocessingCoordinator 接口编排，不接触任何引擎实现（R4）。
 */
#pragma once

#include <QMainWindow>
#include <QMap>
#include <QSet>
#include <QVector>
#include <QStringList>
#include <QElapsedTimer>
#include <QComboBox>
#include "app/preprocessing_coordinator.h"
#include "domain/dedupe_plan.h"
#include "domain/sort_model.h"

class QStackedWidget;
class QVBoxLayout;
class QPushButton;
class QLabel;
class QTableWidget;
class QProgressBar;
class QPlainTextEdit;
class QLineEdit;
class QSpinBox;
class QCheckBox;
class QRadioButton;
class QScrollArea;
class QFrame;
class ClipTimelineWidget;
class IAnalysisEngine;
class CaseManager;

#include "infrastructure/integrity_checker.h"

class PreprocessWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit PreprocessWindow(IAnalysisEngine *analysis, QWidget *parent = nullptr);

    /// 案件模式注入（v1.3.0 M2 任务8）：有打开案件时显示案件横幅，
    /// 成果默认导入案件 preprocess/<ts>/ 并自动登记 case.json；
    /// 横幅可切「独立输出」回到 v1.2.2 老行为。空指针 = 独立模式。
    void setCaseManager(CaseManager *cm);

signals:
    /// 请求主窗口播放输出文件（MainWindow 接线 openVideoFile，R2 不回转）
    void openOutputRequested(const QString &path);

private slots:
    // ① 导入
    void onAddFiles();
    void onClearFiles();
    void onBeginSort();
    void onQuickMerge();
    void syncPendingFromTable();
    ProcessingOptions collectProcessingOptions() const;
    // ② 校对
    void onMoveSelected(int delta);
    void onManualTimestamp();
    void onReIdentify();
    void onConfirmOrder();
    void onCardClicked(const QString &filePath);
    // ③ 设置
    void onBrowseOutput();
    void onToggleDetails();
    void onToggleAdvanced();
    void onStartProcessing();
    // ④ 执行
    void onCancelRun();
    void onOpenOutputFolder();
    void onOpenReport();
    // Coordinator 接线
    void onPhaseChanged(TaskPhase phase);
    void onProgress(int percent, const QString &detail);
    void onProbeDone(const QVector<ProbeResult> &results);
    QString buildProbeStatsText() const;   // 结果页源素材统计（2026-08）
    void onEvidenceReady(const QVector<SortGroup> &groups);
    void onPrecheckReady(const QMap<QString, PrecheckResult> &byGroup);
    void onOverlapDetected(const QStringList &channels);   // v1.7.0 M2
    void onFinished(const PreprocessReport &report);
    void onFailed(PreprocessError error, const QString &detail);
    void onLogLine(const QString &line);
    // §45：NAL 完整性快检（转码/拼接失败后诊断）
    void runIntegrityCheck();
    void onIntegrityFinished(const QVector<NalIntegrityChecker::Result> &results);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    /// 前处理产物归组对话框（2026-08-24 拍板：登记完毕逐产物
    /// 「归入已有摄像头/创建新摄像头」）
    void promptGroupAssignment(const QStringList &outputs,
                               const QMap<QString, QString> &outputChannels);
    QWidget *buildFormatBanner();
    QWidget *buildCaseBanner();
    QWidget *buildPageImport();
    QWidget *buildPageReview();
    QWidget *buildPageSettings();
    QWidget *buildPageRun();

    void setStep(int idx);
    void addFiles(const QStringList &files);
    void rebuildReviewViews();          // 时间线+卡片（m_groups → UI）
    void refreshReviewSummary();
    QWidget *makeCard(const SortEntry &e, int groupIdx, const QString &channel);
    QPixmap thumb(const QString &path); // 有界缓存（C5）
    void selectCard(const QString &filePath, bool scrollTo);
    QString selectedFile() const;
    void showManualDialog(const QString &filePath);
    void updateSettingsPage();
    void updateDiskEstimate();
    /// 案件会话目录（惰性生成：<案件>/preprocess/<yyyyMMdd_HHmmss>）
    QString caseSessionDir() const;
    /// 案件横幅模式切换（导入案件=true / 独立输出=false）
    void setCaseImportMode(bool on);
    /// 案件模式刷新（横幅可见性 + 输出目录行可用性）
    void refreshCaseBanner();
    static QString fmtBytes(qint64 bytes);
    static QString fmtDuration(qint64 ms);

    // 案件模式（v1.3.0 M2 任务8；不持有，SSOT 在 MainWindow）
    CaseManager *m_caseManager = nullptr;
    QFrame *m_caseBanner = nullptr;
    QWidget *m_caseModeRow = nullptr;   ///< §45：案件模式行（无案件时隐藏）
    QLabel *m_caseBannerLabel = nullptr;
    QPushButton *m_btnCaseImport = nullptr;
    QPushButton *m_btnCaseIndep = nullptr;
    QPushButton *m_btnBrowseOutput = nullptr;
    bool m_caseImportMode = true;      ///< 案件打开时默认导入案件（拍板§8-11）
    mutable QString m_caseSessionDir;  ///< 本次处理会话目录（惰性生成）

    PreprocessingCoordinator *m_coord;

    int m_currentStep = 0;

    QStackedWidget *m_stack = nullptr;

    // ① 导入
    QTableWidget *m_fileTable = nullptr;
    QLabel *m_importSummary = nullptr;
    QProgressBar *m_importProgress = nullptr;
    QLabel *m_importStatus = nullptr;
    QPushButton *m_btnBeginSort = nullptr;
    QPushButton *m_btnQuickMerge = nullptr;
    QStringList m_pendingFiles;
    QVector<DuplicatePair> m_dupExcluded;   // 导入去重排除清单（完成页证据摘要留痕）
    int m_tableDragRow = -1;               // 导入表拖拽起点行（自建拖拽，不依赖 Qt 内置）
    bool m_pendingQuickMerge = false;      // 直接拼接链式标志（列表顺序）
    ProcessingOptions m_pendingOpts;

    // ② 校对
    QLabel *m_reviewSummary = nullptr;
    ClipTimelineWidget *m_timeline = nullptr;
    QScrollArea *m_cardScroll = nullptr;
    QWidget *m_cardHost = nullptr;
    // P-60 B 路径问题卡区（方案 §4.5，拍板 Q4A）：拿不准才出现；
    // 完整视图默认折叠（查看全部片段展开）；确认主按钮由问题清零门控
    QWidget *m_problemPanel = nullptr;
    QVBoxLayout *m_problemLay = nullptr;
    QWidget *m_fullReview = nullptr;
    QPushButton *m_showAllBtn = nullptr;
    QPushButton *m_confirmBtn = nullptr;
    QSet<QString> m_problemAcks;           // 已知悉问题键（u|path / o|a|b / s|channel）
    QVector<SortGroup> m_groups;
    QVector<ProbeResult> m_probeResults;   // 探测结果存档（结果页统计用，2026-08）
    QString m_selectedPath;
    QMap<QString, QPixmap> m_thumbCache;
    static constexpr int kThumbCacheCap = 96;

    void rebuildProblemPanel();            // 问题卡重建（collectSortProblems → UI）
    void updateConfirmGate();              // 确认主按钮门控（问题清零才可继续）
    /// v1.12.0：重叠默认修剪策略是否生效（高级选项「保留原样」勾选时退出）；
    /// 生效时重叠不再阻断 GO 直通/不出问题卡（自动修剪+留痕）
    bool overlapTrimOn() const;
    void roiPickForFile(const QString &filePath);   // 框选时间戳对话框
    QString evidenceImageFor(const QString &filePath) const;  // 首帧证据图

    // 卡片拖拽换序状态
    QString m_dragPath;
    QString m_dragChan;
    QPoint m_dragStartPos;
    QFrame *m_dragIndicator = nullptr;

    // ③ 设置
    QRadioButton *m_radioLossless = nullptr;
    QRadioButton *m_radioTranscode = nullptr;
    QLabel *m_modeReason = nullptr;
    QPushButton *m_btnDetails = nullptr;
    QPlainTextEdit *m_precheckDetail = nullptr;
    QLineEdit *m_outputDirEdit = nullptr;
    NalIntegrityChecker *m_checker = nullptr;   ///< §45 完整性快检（失败后按需创建）
    QLabel *m_outputNames = nullptr;
    QLabel *m_diskEstimate = nullptr;
    QPushButton *m_btnAdvanced = nullptr;
    QWidget *m_advancedHost = nullptr;
    QCheckBox *m_normalizeCheck = nullptr;
    QCheckBox *m_keepOverlapCheck = nullptr;  // 勾选=保留重叠原样；默认自动修剪
    QStringList m_overlapPendingChannels;     // 预检发现的重叠组（开始拼接时应用修剪）
    QCheckBox *m_deinterlaceCheck = nullptr;
    QCheckBox *m_sha256Check = nullptr;
    QCheckBox *m_ignoreWarnCheck = nullptr;
    QSpinBox *m_crfSpin = nullptr;
    QComboBox *m_encoderCombo = nullptr;   // v1.7.0 M1：编码器选择
    bool m_anyBlock = false;

    // ④ 执行
    QProgressBar *m_runProgress = nullptr;
    QLabel *m_runStatus = nullptr;
    QLabel *m_runEta = nullptr;
    QPushButton *m_btnRunLog = nullptr;
    QPlainTextEdit *m_runLog = nullptr;
    QPushButton *m_btnCancel = nullptr;
    QWidget *m_resultCard = nullptr;
    QLabel *m_resultTitle = nullptr;
    QLabel *m_resultOutput = nullptr;
    QLabel *m_resultStats = nullptr;      // 源素材统计 + 统一帧率醒目提示（2026-08）
    QPlainTextEdit *m_resultSummary = nullptr;  // v1.12.1 证据摘要卡（默认展开）
    QLabel *m_resultEvidence = nullptr;
    QPushButton *m_btnOpenFolder = nullptr;
    QPushButton *m_btnOpenReport = nullptr;
    QPushButton *m_btnPlayOutput = nullptr;
    QElapsedTimer m_runTimer;
    QString m_reportCsv;
    QString m_reportOutputPath;
    QString m_outputDir;
};
