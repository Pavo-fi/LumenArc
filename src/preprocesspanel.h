/**
 * @file preprocesspanel.h
 * @brief 前处理面板：素材→排序与证据→处理选项→执行与结果（四步向导）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计见 docs/PREPROCESSING_TECH_DESIGN_CN.md §8。
 * 纯展示组件：不持有业务状态，全部状态真身在 PreprocessingCoordinator（R5）。
 */
#pragma once

#include <QDockWidget>
#include <QMap>
#include <QPixmap>
#include "app/preprocessing_coordinator.h"

class QStackedWidget;
class QListWidget;
class QTableWidget;
class QTabWidget;
class QLabel;
class QProgressBar;
class QPushButton;
class QPlainTextEdit;
class QLineEdit;
class QSpinBox;
class QCheckBox;

class PreprocessPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit PreprocessPanel(QWidget *parent = nullptr);
    ~PreprocessPanel() override = default;

    PreprocessingCoordinator *coordinator() const { return m_coord; }

private slots:
    void onAddFiles();
    void onRemoveFiles();
    void onBeginAnalysis();
    void onConfirmOrder();
    void onStartProcessing();
    void onCancel();
    void onManualTimestamp();
    void onMoveRow(int delta);
    void onBrowseOutput();

    // Coordinator 接线
    void onPhaseChanged(TaskPhase phase);
    void onProgress(int percent, const QString &detail);
    void onProbeDone(const QVector<ProbeResult> &results);
    void onEvidenceReady(const QVector<SortGroup> &groups);
    void onPrecheckReady(const QMap<QString, PrecheckResult> &byGroup);
    void onFinished(const PreprocessReport &report);
    void onFailed(PreprocessError error, const QString &detail);
    void onLogLine(const QString &line);

private:
    QWidget *buildStepFiles();
    QWidget *buildStepEvidence();
    QWidget *buildStepOptions();
    QWidget *buildStepRun();
    void refreshEvidenceTables(const QVector<SortGroup> &groups);
    void setStep(int index);
    QPixmap thumbnail(const QString &path);   // 有界缓存（C5）
    QString currentGroup() const;
    QString selectedFile() const;

    PreprocessingCoordinator *m_coord;
    QStackedWidget *m_stack;
    QListWidget *m_fileList;
    QTabWidget *m_groupTabs;
    QLabel *m_warnSummary;
    QPushButton *m_btnBegin;
    QPushButton *m_btnConfirm;
    QPushButton *m_btnStart;
    QPushButton *m_btnCancel;
    QPlainTextEdit *m_precheckView;
    QLineEdit *m_outputDirEdit;
    QSpinBox *m_crfSpin;
    QCheckBox *m_deinterlaceCheck;
    QCheckBox *m_normalizeCheck;
    QCheckBox *m_ignoreWarnCheck;
    QCheckBox *m_sha256Check;
    QProgressBar *m_progressBar;
    QLabel *m_progressLabel;
    QPlainTextEdit *m_logView;
    QLabel *m_resultLabel;
    QPushButton *m_btnOpenOutput;

    QString m_lastOutputDir;
    QMap<QString, QPixmap> m_thumbCache;      // LRU 近似：超界清空重建（有界，C5）
    static constexpr int kThumbCacheCap = 40;
};
