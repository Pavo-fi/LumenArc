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
#include <QVector>
#include <QStringList>
#include <QElapsedTimer>
#include "app/preprocessing_coordinator.h"
#include "domain/sort_model.h"

class QStackedWidget;
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

class PreprocessWindow : public QMainWindow
{
    Q_OBJECT
public:
    explicit PreprocessWindow(IAnalysisEngine *analysis, QWidget *parent = nullptr);

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
    void onEvidenceReady(const QVector<SortGroup> &groups);
    void onPrecheckReady(const QMap<QString, PrecheckResult> &byGroup);
    void onFinished(const PreprocessReport &report);
    void onFailed(PreprocessError error, const QString &detail);
    void onLogLine(const QString &line);

protected:
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    QWidget *buildFormatBanner();
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
    static QString fmtBytes(qint64 bytes);
    static QString fmtDuration(qint64 ms);

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
    int m_tableDragRow = -1;               // 导入表拖拽起点行（自建拖拽，不依赖 Qt 内置）
    bool m_pendingQuickMerge = false;      // 直接拼接链式标志（列表顺序）
    ProcessingOptions m_pendingOpts;

    // ② 校对
    QLabel *m_reviewSummary = nullptr;
    ClipTimelineWidget *m_timeline = nullptr;
    QScrollArea *m_cardScroll = nullptr;
    QWidget *m_cardHost = nullptr;
    QVector<SortGroup> m_groups;
    QString m_selectedPath;
    QMap<QString, QPixmap> m_thumbCache;
    static constexpr int kThumbCacheCap = 96;

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
    QLabel *m_outputNames = nullptr;
    QLabel *m_diskEstimate = nullptr;
    QPushButton *m_btnAdvanced = nullptr;
    QWidget *m_advancedHost = nullptr;
    QCheckBox *m_normalizeCheck = nullptr;
    QCheckBox *m_deinterlaceCheck = nullptr;
    QCheckBox *m_sha256Check = nullptr;
    QCheckBox *m_ignoreWarnCheck = nullptr;
    QSpinBox *m_crfSpin = nullptr;
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
    QLabel *m_resultEvidence = nullptr;
    QPushButton *m_btnOpenFolder = nullptr;
    QPushButton *m_btnOpenReport = nullptr;
    QPushButton *m_btnPlayOutput = nullptr;
    QElapsedTimer m_runTimer;
    QString m_reportCsv;
    QString m_reportOutputPath;
    QString m_outputDir;
};
