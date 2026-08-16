/**
 * @file mainwindow.cpp
 * @brief 主窗口实现：菜单/工具栏/快捷键/分析流程/截图叠加/放大镜协调
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "mainwindow.h"
#include "videowidget.h"
#include "chartpanel.h"
#include "domain/roi_model.h"
#include "domain/roi_model.h"
#include "domain/guide_line_model.h"
#include "domain/timeline_model.h"
#include "infrastructure/ivideo_engine.h"
#include "infrastructure/ianalysis_engine.h"
#include "infrastructure/ffmpeg_video_engine.h"
#include "infrastructure/python_analysis_engine.h"
#include "infrastructure/libav_analysis_engine.h"
#include "app/calibration_service.h"
#include "app/case_manager.h"
#include "app/case_open_panel.h"
#include "casedock.h"
#include "casedialogs.h"
#include "multicamview.h"
#include "timesettingsdialog.h"
#include "magnifierwidget.h"
#include "snapshotoverlay.h"
#include "playbackadjustpanel.h"
#include "displayadjust.h"
#include "pinnedwidget.h"
#include "videolistpanel.h"
#include "preprocesswindow.h"
#include "spectrogrampanel_enhanced.h"
#include "i18n.h"
#include "aboutdialog.h"
#include "theme.h"

#include <QListWidget>
#include <QSplitter>
#include <QMenuBar>
#include <QToolBar>
#include <QPushButton>
#include <QtConcurrent>
#include <QLabel>
#include <QProgressBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QSystemTrayIcon>
#include <QStyle>
#include <QDebug>
#include <QDir>
#include <QElapsedTimer>
#include <QCloseEvent>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QInputDialog>
#include <QTime>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>
#include <QSettings>
#include <QCryptographicHash>
#include <QTimer>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QKeyEvent>
#include <QDesktopServices>
#include <QLineEdit>
#include <QTextEdit>
#include <QSlider>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QPainter>
#include <QPropertyAnimation>
#include <QEasingCurve>
#include <QActionGroup>
#include <QDialog>
#include <QTableWidget>
#include <QHeaderView>
#include <QLabel>

/// 构建时间戳（2026-08-13）：标题栏常驻，杜绝“用户在跑旧构建”无法辨识
/// （多次出现修复已提交但用户测的是旧 exe 的扯皮）。__DATE__/__TIME__
/// 为本编译单元的编译时刻，随每次重编 mainwindow.cpp 更新。
static QString buildStamp()
{
    return QStringLiteral(" (build %1)")
        .arg(QStringLiteral(__DATE__) + QStringLiteral(" ")
             + QStringLiteral(__TIME__).left(5));
}

/// @brief 构造主窗口：初始化引擎/组件/连接信号槽
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    loadLanguage();
    setWindowTitle(lang("追光者 Lumen Arc v1.7.0", "Lumen Arc v1.7.0") + buildStamp());
    resize(1280, 720);

    m_roiModel = new RoiModel(this);   // 统一 ROI 模型（矩形+多边形，v1.5.0 Q-18）
    m_guideLineModel = new GuideLineModel(this);
    m_timelineModel = new TimelineModel(this);
    m_stateManager = new VideoStateManager(this);

    // 播放引擎：自研 FFmpeg 内核（硬解设置经 QSettings 持久化）
    {
        QSettings engineSettings("LumenArc", "LumenArc");
        auto *ffEngine = new FfmpegVideoEngine(this);
        ffEngine->setHardwareDecode(
            engineSettings.value("hwDecode", true).toBool());
        ffEngine->setHardwareAdapter(
            engineSettings.value("hwAdapter", -1).toInt());
        m_videoEngine = ffEngine;
    }

    m_videoWidget = new VideoWidget(this);
    m_videoWidget->setVideoEngine(m_videoEngine);
    m_videoWidget->setRegionModel(m_roiModel);
    m_videoWidget->setPolygonModel(m_roiModel);
    m_videoWidget->setGuideLineModel(m_guideLineModel);

    m_chartPanel = new ChartPanel(this);
    m_chartPanel->setRegionModel(m_roiModel);
    m_chartPanel->setPolygonModel(m_roiModel);
    m_chartPanel->setTimelineModel(m_timelineModel);

    // v0.3: Spectrogram panel below chart
    // v0.4: Use enhanced version with GPU rendering and log frequency
    m_spectrogramEnhanced = new SpectrogramPanelEnhanced(this);

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->setHandleWidth(6);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->addWidget(m_videoWidget);
    m_splitter->setStretchFactor(0, 7);  // 44%
    setCentralWidget(m_splitter);

    // --- Shared styles（主题化）---
    const QString titleBarStyle =
        "background: " + Theme::BgPanel + "; border-bottom: 1px solid " + Theme::Border + ";";
    const QString collapseBtnStyle =
        "QPushButton { background: transparent; color: " + Theme::TextSecond + "; border: none; font-size: 10px; border-radius: 4px; }"
        "QPushButton:hover { color: " + Theme::Accent + "; background: " + Theme::BgHover + "; }";
    const QString titleLabelStyle =
        "color: " + Theme::TextPrimary + "; font-size: 12px; font-weight: 600; background: transparent; padding-left: 2px;";

    // --- Chart container with title bar ---
    m_chartContainer = new QWidget(m_splitter);
    auto *chartContainerLayout = new QVBoxLayout(m_chartContainer);
    chartContainerLayout->setContentsMargins(0, 0, 0, 0);
    chartContainerLayout->setSpacing(0);

    auto *chartTitleBar = new QWidget(m_chartContainer);
    chartTitleBar->setFixedHeight(28);
    chartTitleBar->setStyleSheet(titleBarStyle);
    auto *chartTitleLayout = new QHBoxLayout(chartTitleBar);
    chartTitleLayout->setContentsMargins(4, 0, 4, 0);

    m_chartCollapseBtn = new QPushButton(QString::fromUtf8("\xe2\x96\xbc"), chartTitleBar); // ▼
    m_chartCollapseBtn->setFixedSize(22, 22);
    m_chartCollapseBtn->setStyleSheet(collapseBtnStyle);
    m_chartCollapseBtn->setToolTip(lang("收起量化分析", "Collapse quantitative analysis"));
    m_chartCollapseBtn->setFocusPolicy(Qt::NoFocus);

    auto *chartTitleLabel = new QLabel(lang("量化分析", "Quantitative Analysis"), chartTitleBar);
    chartTitleLabel->setStyleSheet(titleLabelStyle);

    chartTitleLayout->addWidget(m_chartCollapseBtn);
    chartTitleLayout->addWidget(chartTitleLabel);
    chartTitleLayout->addStretch();

    m_chartContent = new QWidget(m_chartContainer);
    auto *chartContentLayout = new QVBoxLayout(m_chartContent);
    chartContentLayout->setContentsMargins(0, 2, 0, 0);
    chartContentLayout->addWidget(m_chartPanel);

    chartContainerLayout->addWidget(chartTitleBar);
    chartContainerLayout->addWidget(m_chartContent, 1);

    m_splitter->addWidget(m_chartContainer);
    m_splitter->setStretchFactor(1, 7);  // 25%

    // --- Spectrogram container with title bar ---
    m_spectrogramContainer = new QWidget(m_splitter);
    auto *specContainerLayout = new QVBoxLayout(m_spectrogramContainer);
    specContainerLayout->setContentsMargins(0, 0, 0, 0);
    specContainerLayout->setSpacing(0);

    auto *specTitleBar = new QWidget(m_spectrogramContainer);
    specTitleBar->setFixedHeight(28);
    specTitleBar->setStyleSheet(titleBarStyle);
    auto *specTitleLayout = new QHBoxLayout(specTitleBar);
    specTitleLayout->setContentsMargins(4, 0, 4, 0);

    m_spectrogramCollapseBtn = new QPushButton(QString::fromUtf8("\xe2\x96\xbc"), specTitleBar); // ▼
    m_spectrogramCollapseBtn->setFixedSize(22, 22);
    m_spectrogramCollapseBtn->setStyleSheet(collapseBtnStyle);
    m_spectrogramCollapseBtn->setToolTip(lang("收起语谱图", "Collapse spectrogram"));
    m_spectrogramCollapseBtn->setFocusPolicy(Qt::NoFocus);

    auto *specTitleLabel = new QLabel(lang("语谱图", "Spectrogram"), specTitleBar);
    specTitleLabel->setStyleSheet(titleLabelStyle);

    specTitleLayout->addWidget(m_spectrogramCollapseBtn);
    specTitleLayout->addWidget(specTitleLabel);
    specTitleLayout->addSpacing(12);

    m_noiseFloorLabel = new QLabel(lang("底噪:", "Noise:"), specTitleBar);
    m_noiseFloorLabel->setStyleSheet("QLabel { color: " + Theme::TextSecond + "; font-size: 11px; }");
    specTitleLayout->addWidget(m_noiseFloorLabel);

    m_noiseFloorSlider = new QSlider(Qt::Horizontal, specTitleBar);
    m_noiseFloorSlider->setRange(-100, 0);  // -10.0 to 0.0 dB (x10)
    m_noiseFloorSlider->setValue(-55);       // default -5.5 dB
    m_noiseFloorSlider->setFixedWidth(100);
    m_noiseFloorSlider->setToolTip(lang("底噪阈值: -5.5 dB", "Noise floor threshold: -5.5 dB"));
    m_noiseFloorSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: " + Theme::Border + "; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: " + Theme::Info + "; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }"
        "QSlider::sub-page:horizontal { background: " + Theme::Info + "; border-radius: 2px; }"
    );
    specTitleLayout->addWidget(m_noiseFloorSlider);

    m_noiseFloorValueLabel = new QLabel("-5.5", specTitleBar);
    m_noiseFloorValueLabel->setStyleSheet("QLabel { color: " + Theme::Info + "; font-size: 11px; font-family: Consolas; min-width: 30px; }");
    specTitleLayout->addWidget(m_noiseFloorValueLabel);

    // Noise reduction slider
    m_noiseReductionLabel = new QLabel(lang("降噪:", "NR:"), specTitleBar);
    m_noiseReductionLabel->setStyleSheet("QLabel { color: " + Theme::TextSecond + "; font-size: 11px; }");
    specTitleLayout->addWidget(m_noiseReductionLabel);

    m_noiseReductionSlider = new QSlider(Qt::Horizontal, specTitleBar);
    m_noiseReductionSlider->setRange(0, 50);  // 0.0 to 5.0 (x10)
    m_noiseReductionSlider->setValue(0);       // default off
    m_noiseReductionSlider->setFixedWidth(80);
    m_noiseReductionSlider->setToolTip(lang("降噪强度（需重新分析）", "Noise reduction (re-analysis needed)"));
    m_noiseReductionSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: " + Theme::Border + "; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: " + Theme::Accent + "; width: 14px; height: 14px; margin: -5px 0; border-radius: 7px; }"
        "QSlider::sub-page:horizontal { background: " + Theme::Accent + "; border-radius: 2px; }"
    );
    specTitleLayout->addWidget(m_noiseReductionSlider);

    m_noiseReductionValueLabel = new QLabel("0.0", specTitleBar);
    m_noiseReductionValueLabel->setStyleSheet("QLabel { color: " + Theme::Accent + "; font-size: 11px; font-family: Consolas; min-width: 24px; }");
    specTitleLayout->addWidget(m_noiseReductionValueLabel);

    // 安装事件过滤器，使全局快捷键（方向键、空格等）不被 Slider 拦截
    m_noiseFloorSlider->installEventFilter(this);
    m_noiseReductionSlider->installEventFilter(this);

    m_nrApplyBtn = new QPushButton(lang("应用", "Apply"), specTitleBar);
    m_nrApplyBtn->setFixedSize(40, 22);
    m_nrApplyBtn->setFocusPolicy(Qt::NoFocus);
    m_nrApplyBtn->setStyleSheet(
        "QPushButton { background: transparent; color: " + Theme::Accent + "; border: 1px solid " + Theme::Accent + "; border-radius: 11px; font-size: 10px; }"
        "QPushButton:hover { background: " + Theme::Accent + "; color: " + Theme::AccentOnDark + "; }"
        "QPushButton:disabled { color: " + Theme::TextMuted + "; border-color: " + Theme::Border + "; background: transparent; }"
    );
    specTitleLayout->addWidget(m_nrApplyBtn);

    specTitleLayout->addStretch();

    m_spectrogramContent = new QWidget(m_spectrogramContainer);
    auto *specContentLayout = new QVBoxLayout(m_spectrogramContent);
    specContentLayout->setContentsMargins(0, 0, 0, 0);
    specContentLayout->addWidget(m_spectrogramEnhanced);

    specContainerLayout->addWidget(specTitleBar);
    specContainerLayout->addWidget(m_spectrogramContent, 1);

    m_splitter->addWidget(m_spectrogramContainer);
    m_splitter->setStretchFactor(2, 4);  // 20%

    // Store original splitter sizes for collapse/expand
    m_splitterSizes = {440, 310, 250};
    m_chartSavedSizes = {440, 310, 250};
    m_spectrogramSavedSizes = {440, 310, 250};

    // Chart collapse/expand with splitter size control
    connect(m_chartCollapseBtn, &QPushButton::clicked, this, [this]() {
        QList<int> sizes = m_splitter->sizes();
        if (sizes.size() < 3) return;

        if (sizes[1] > 30) {
            // Collapse: save chart sizes, shrink chart to title bar
            m_chartSavedSizes = sizes;
            int freed = sizes[1] - 24;
            sizes[0] += freed;
            sizes[1] = 24;
            m_chartCollapseBtn->setText(QString::fromUtf8("\xe2\x96\xb2")); // ▲
            m_chartCollapseBtn->setToolTip(lang("展开量化分析", "Expand quantitative analysis"));
            m_chartContent->setVisible(false);
            m_chartContent->setMinimumSize(0, 0);
            m_chartContent->setMaximumSize(0, 0);
            m_chartContainer->setMinimumHeight(28);
        } else {
            // Expand: restore saved sizes
            sizes = m_chartSavedSizes;
            // If spectrogram is collapsed, keep it at 24px
            if (m_spectrogramContainer->minimumHeight() == 24) {
                int diff = sizes[2] - 24;
                sizes[2] = 24;
                sizes[0] += diff;
            }
            m_chartCollapseBtn->setText(QString::fromUtf8("\xe2\x96\xbc")); // ▼
            m_chartCollapseBtn->setToolTip(lang("收起量化分析", "Collapse quantitative analysis"));
            m_chartContent->setVisible(true);
            m_chartContent->setMinimumSize(0, 0);
            m_chartContent->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            m_chartContainer->setMinimumHeight(0);
        }
        m_splitter->setSizes(sizes);
    });

    // Spectrogram collapse/expand with splitter size control
    connect(m_spectrogramCollapseBtn, &QPushButton::clicked, this, [this]() {
        QList<int> sizes = m_splitter->sizes();
        if (sizes.size() < 3) return;

        if (sizes[2] > 30) {
            // Collapse: save spectrogram sizes, shrink to title bar
            m_spectrogramSavedSizes = sizes;
            int freed = sizes[2] - 24;
            if (sizes[1] <= 30) {
                // Chart already collapsed: all freed space goes to video
                sizes[0] += freed;
            } else {
                // Chart expanded: split evenly
                sizes[0] += freed / 2;
                sizes[1] += freed - freed / 2;
            }
            sizes[2] = 24;
            m_spectrogramCollapseBtn->setText(QString::fromUtf8("\xe2\x96\xb2")); // ▲
            m_spectrogramCollapseBtn->setToolTip(lang("展开语谱图", "Expand spectrogram"));
            m_spectrogramContent->setVisible(false);
            m_spectrogramContent->setMinimumSize(0, 0);
            m_spectrogramContent->setMaximumSize(0, 0);
            m_spectrogramContainer->setMinimumHeight(28);
        } else {
            // Expand: restore saved sizes
            sizes = m_spectrogramSavedSizes;
            // If chart is collapsed, keep it at 24px
            if (m_chartContainer->minimumHeight() == 24) {
                int diff = sizes[1] - 24;
                sizes[1] = 24;
                sizes[0] += diff;
            }
            m_spectrogramCollapseBtn->setText(QString::fromUtf8("\xe2\x96\xbc")); // ▼
            m_spectrogramCollapseBtn->setToolTip(lang("收起语谱图", "Collapse spectrogram"));
            m_spectrogramContent->setVisible(true);
            m_spectrogramContent->setMinimumSize(0, 0);
            m_spectrogramContent->setMaximumSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX);
            m_spectrogramContainer->setMinimumHeight(0);
        }
        m_splitter->setSizes(sizes);
    });

    // v0.3: Video list panel (left dock) with sidebar layout
    m_videoListPanel = new VideoListPanel(this);
    addDockWidget(Qt::LeftDockWidgetArea, m_videoListPanel);
    resizeDocks({m_videoListPanel}, {250}, Qt::Horizontal);

    // Build sidebar inside the dock widget's content area
    auto *sidebarWidget = new QWidget(m_videoListPanel);
    auto *sidebarLayout = new QHBoxLayout(sidebarWidget);
    sidebarLayout->setContentsMargins(0, 0, 0, 0);
    sidebarLayout->setSpacing(0);

    // Left vertical bar (always visible)
    m_videoListSidebar = new QWidget(sidebarWidget);
    m_videoListSidebar->setFixedWidth(28);
    m_videoListSidebar->setStyleSheet("background: " + Theme::BgPanel + "; border-right: 1px solid " + Theme::Border + ";");
    auto *sidebarBarLayout = new QVBoxLayout(m_videoListSidebar);
    sidebarBarLayout->setContentsMargins(2, 4, 2, 4);
    sidebarBarLayout->setSpacing(4);

    m_videoListCollapseBtn = new QPushButton(QString::fromUtf8("\xe2\x97\x80"), m_videoListSidebar); // ◀
    m_videoListCollapseBtn->setFixedSize(22, 22);
    m_videoListCollapseBtn->setStyleSheet(collapseBtnStyle);
    m_videoListCollapseBtn->setToolTip(lang("收起视频列表", "Collapse video list"));
    m_videoListCollapseBtn->setFocusPolicy(Qt::NoFocus);
    sidebarBarLayout->addWidget(m_videoListCollapseBtn);
    sidebarBarLayout->addStretch();

    // Vertical "视频列表" label
    {
        QString vertText = lang("视频列表", "Videos");
        for (const QChar &ch : vertText) {
            auto *chLabel = new QLabel(QString(ch), m_videoListSidebar);
            chLabel->setStyleSheet("color: " + Theme::TextSecond + "; font-size: 11px; background: transparent;");
            chLabel->setAlignment(Qt::AlignCenter);
            sidebarBarLayout->addWidget(chLabel);
        }
        sidebarBarLayout->addStretch();
    }

    // Right side: the actual list content from VideoListPanel
    m_videoListContent = m_videoListPanel->widget();

    sidebarLayout->addWidget(m_videoListSidebar);
    sidebarLayout->addWidget(m_videoListContent, 1);

    m_videoListPanel->setWidget(sidebarWidget);
    m_videoListPanel->setTitleBarWidget(new QWidget());  // minimal empty title bar
    m_videoListPanel->titleBarWidget()->setFixedHeight(0);
    m_videoListPanel->setStyleSheet(
        "QDockWidget { border: 0px; padding: 0px; }"
        "QDockWidget::title { padding: 0px; margin: 0px; border: 0px; }");

    // Video list collapse/expand
    connect(m_videoListCollapseBtn, &QPushButton::clicked, this, [this]() {
        bool contentVisible = m_videoListContent->isVisible();
        m_videoListContent->setVisible(!contentVisible);
        m_videoListCollapseBtn->setText(contentVisible
            ? QString::fromUtf8("\xe2\x96\xb6")   // ▶
            : QString::fromUtf8("\xe2\x97\x80"));  // ◀
        m_videoListCollapseBtn->setToolTip(contentVisible
            ? lang("展开视频列表", "Expand video list")
            : lang("收起视频列表", "Collapse video list"));
        resizeDocks({m_videoListPanel}, {contentVisible ? 24 : 250}, Qt::Horizontal);
    });

    m_videoListPanel->show();

    // Placeholder dock: shown when magnifier hides the video list panel
    m_videoListPlaceholder = new QDockWidget(this);
    m_videoListPlaceholder->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_videoListPlaceholder->setTitleBarWidget(new QWidget());
    m_videoListPlaceholder->titleBarWidget()->setFixedHeight(0);
    m_videoListPlaceholder->setStyleSheet(
        "QDockWidget { border: 0px; padding: 0px; }"
        "QDockWidget::title { padding: 0px; margin: 0px; border: 0px; }");
    auto *placeholderContent = new QWidget();
    placeholderContent->setFixedWidth(28);
    placeholderContent->setStyleSheet("background: " + Theme::BgPanel + "; border-right: 1px solid " + Theme::Border + ";");
    auto *phLayout = new QVBoxLayout(placeholderContent);
    phLayout->setContentsMargins(2, 4, 2, 4);
    phLayout->setSpacing(4);
    auto *phExpandBtn = new QPushButton(QString::fromUtf8("\xe2\x96\xb6"), placeholderContent); // ▶
    phExpandBtn->setFixedSize(22, 22);
    phExpandBtn->setStyleSheet(collapseBtnStyle);
    phExpandBtn->setToolTip(lang("展开视频列表", "Expand video list"));
    phExpandBtn->setFocusPolicy(Qt::NoFocus);
    phLayout->addWidget(phExpandBtn);
    phLayout->addStretch();
    {
        QString vertText = lang("视频列表", "Videos");
        for (const QChar &ch : vertText) {
            auto *chLabel = new QLabel(QString(ch), placeholderContent);
            chLabel->setStyleSheet("color: " + Theme::TextSecond + "; font-size: 11px; background: transparent;");
            chLabel->setAlignment(Qt::AlignCenter);
            phLayout->addWidget(chLabel);
        }
        phLayout->addStretch();
    }
    m_videoListPlaceholder->setWidget(placeholderContent);
    addDockWidget(Qt::LeftDockWidgetArea, m_videoListPlaceholder);
    resizeDocks({m_videoListPlaceholder}, {24}, Qt::Horizontal);
    m_videoListPlaceholder->setVisible(false);

    // v1.2: 前处理改为独立任务窗口（docs/PREPROCESSING_UI_REDESIGN_CN.md），
    // 经工具栏「素材转码拼接」按钮/文件菜单打开（见 openPreprocessWindow）

    connect(phExpandBtn, &QPushButton::clicked, this, [this]() {
        m_videoListPlaceholder->setVisible(false);
        m_videoListPanel->setVisible(true);
        m_videoListContent->setVisible(true);
        m_videoListCollapseBtn->setText(QString::fromUtf8("\xe2\x97\x80")); // ◀
        m_videoListCollapseBtn->setToolTip(lang("收起视频列表", "Collapse video list"));
        resizeDocks({m_videoListPanel}, {250}, Qt::Horizontal);
    });

    // v0.3: Status bar with progress
    m_operationLabel = new QLabel(this);
    m_operationLabel->setMinimumWidth(200);
    m_operationLabel->setStyleSheet("color: " + Theme::TextPrimary + ";");
    m_statusLabel = new QLabel(this);
    m_hwAdapterLabel = new QLabel(this);
    m_hwAdapterLabel->setStyleSheet("color: " + Theme::TextMuted + "; font-size: 10px;");
    m_progressBar = new QProgressBar(this);
    m_progressBar->setMaximumWidth(200);
    m_progressBar->setRange(0, 100);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(false);
    m_cancelBtn = new QPushButton(lang("取消分析", "Cancel"), this);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setVisible(false);
    statusBar()->addWidget(m_operationLabel);  // 左侧：操作反馈
    statusBar()->addPermanentWidget(m_statusLabel);  // 右侧：分析状态
    statusBar()->addPermanentWidget(m_hwAdapterLabel);
    statusBar()->addPermanentWidget(m_progressBar);
    statusBar()->addPermanentWidget(m_cancelBtn);
    statusBar()->show();  // 确保状态栏可见

    setAcceptDrops(true);

    auto *pyEngine = new PythonAnalysisEngine(this);
    pyEngine->setPythonExecutable(detectPythonPath());
    m_analysisEngine = pyEngine;

    // v1.5.0 P3：分析引擎切换（libav 默认 / Python 回退，重启生效）。
    // libav 引擎已通过 A/B 对拍（亮度 |Δ|≤1、volume 相关 ≥0.999、
    // 语谱主峰 |Δ|≤0.001）；Python 保留为过渡期回退（设置项）。
    {
        QSettings es("LumenArc", "LumenArc");
        if (es.value("analysisEngine", QStringLiteral("libav")).toString()
            != QStringLiteral("python")) {
            delete m_analysisEngine;
            m_analysisEngine = new LibavAnalysisEngine(this);
        }
    }

    // 校时服务（v1.2.0：三点识别/absStart/sidecar 继承；产出仅预填，
    // 「采用」由 TimeSettingsDialog 决定）
    m_calibrationService = new CalibrationService(m_analysisEngine, this);
    m_calibrationService->setPythonExecutable(detectPythonPath());

    // 案件管理器（v1.3.0 M2）：唯一持有打开的案件；无案件时所有分流
    // 接口（vlaPathFor/timestampRoiFor…）自动回落独立模式老行为
    m_caseManager = new CaseManager(this);
    // 校时证据帧目录分流（M2 任务8）：入案→案件 evidence/calibration/V###；
    // 未入案/无案件→CaseManager 内部回落老路径 LumenArc_Calibration
    m_calibrationService->setEvidenceDirResolver(
        [this](const QString &videoPath) {
            return m_caseManager->evidenceDirFor(videoPath);
        });

    // ---- 案件模式接线（v1.3.0 M2 任务10）----
    m_caseDock = new CaseDock(m_caseManager, this);
    addDockWidget(Qt::LeftDockWidgetArea, m_caseDock);
    resizeDocks({m_caseDock}, {250}, Qt::Horizontal);
    m_caseDock->setVisible(false);   // 仅案件模式可见（替代视频列表）
    connect(m_caseDock, &CaseDock::openVideoRequested,
            this, &MainWindow::openVideoFile);
    connect(m_caseDock, &CaseDock::closeCaseRequested,
            this, &MainWindow::closeCaseWithPrompt);
    // 案件打开面板（Blender 式页面内居中，2026-08 人工反馈：不弹窗）
    m_caseOpenPanel = new CaseOpenPanel(m_caseManager, this);
    m_caseOpenPanel->hide();
    connect(m_caseOpenPanel, &CaseOpenPanel::openCaseRequested,
            this, &MainWindow::openCaseFlow);
    connect(m_caseOpenPanel, &CaseOpenPanel::browseRequested,
            this, &MainWindow::onOpenCaseBrowse);
    connect(m_caseOpenPanel, &CaseOpenPanel::newCaseRequested,
            this, [this]() { onNewCase(); });
    connect(m_caseOpenPanel, &CaseOpenPanel::independentRequested,
            this, [this]() { m_caseOpenPanel->hide(); });
    connect(m_caseOpenPanel, &CaseOpenPanel::closeRequested,
            this, [this]() { m_caseOpenPanel->hide(); });
    connect(m_caseManager, &CaseManager::caseOpened,
            this, &MainWindow::enterCaseMode);
    connect(m_caseManager, &CaseManager::caseClosed,
            this, &MainWindow::exitCaseMode);
    // 证据树刷新触发点：登记/移除/重定位/逐路哈希/队列排空
    auto refreshDock = [this]() {
        if (m_caseManager->isOpen())
            m_caseDock->refreshTree();
    };
    connect(m_caseManager, &CaseManager::videoAdded, this, refreshDock);
    connect(m_caseManager, &CaseManager::videoRemoved, this, refreshDock);
    connect(m_caseManager, &CaseManager::videoInfoChanged, this, refreshDock);
    connect(m_caseManager, &CaseManager::hashProgress, this, refreshDock);
    // v1.3.0 M2 任务8：前处理会话登记/sidecar 归类等落盘即刷新——
    // 缺失此连接时处理完成案件列表不出现会话条目（人工测试反馈）
    connect(m_caseManager, &CaseManager::caseSaved, this, refreshDock);
    connect(m_caseManager, &CaseManager::hashQueueFinished, this,
            [this, refreshDock]() {
                refreshDock();
                // 指纹回写 meta 后静默落盘（机器维护写，与 manifest 同理）
                QString err;
                m_caseManager->saveCase(&err);
            });
    // v1.3.0 M3 任务13：重定位后内存状态键迁移 + 当前路径跟随
    connect(m_caseManager, &CaseManager::videoRelocated, this,
            [this](const QString &, const QString &oldPath,
                   const QString &newPath) {
                if (m_stateManager)
                    m_stateManager->migrateKey(oldPath, newPath);
                if (QDir::cleanPath(m_currentVideoPath)
                    == QDir::cleanPath(oldPath))
                    m_currentVideoPath = newPath;
            });
    // 状态栏📁标识（模式出口三：点击 = 关闭案件）
    m_caseStatusBtn = new QPushButton(this);
    m_caseStatusBtn->setFlat(true);
    m_caseStatusBtn->setStyleSheet(
        "QPushButton { color: " + Theme::Accent + "; border: none;"
        " padding: 0 6px; font-weight: bold; }"
        "QPushButton:hover { color: " + Theme::AccentHover + "; }");
    m_caseStatusBtn->setToolTip(
        lang("案件已打开（点击关闭案件）", "Case open (click to close)"));
    m_caseStatusBtn->setVisible(false);
    statusBar()->addWidget(m_caseStatusBtn);
    connect(m_caseStatusBtn, &QPushButton::clicked,
            this, &MainWindow::closeCaseWithPrompt);
    // v1.2.1 非模态：后台校时任务进度常驻状态栏（对话框关闭后仍可见）
    connect(m_calibrationService, &CalibrationService::progress,
            this, [this](const QString &stage) {
                showOperationStatus(lang("校时任务：%1", "Calibration task: %1")
                                        .arg(stage));
            });

    // Snapshot overlay (floating on video area)
    m_snapshotOverlay = new SnapshotOverlay(m_videoWidget);
    m_snapshotOverlay->hide();

    // 时间戳框选信号（v1.2.1）：永久连接一次——此前挂在 onSetStartTime 里，
    // 每开一次校时窗重复 connect 一份（lambda 永久累积在 VideoWidget 上）。
    // lambda 只读成员（m_roiDialog/m_currentVideoPath/m_videoWidget），
    // 与单次连接语义自洽。
    // v1.2.x UX：拖拽松开/叠加层「确认」→ timestampRoiReady → 校时窗恢复并
    // 提供「确认并开始校时」（ready 与 confirmed 由按钮同时发，接 ready 即可）
    connect(m_videoWidget, &VideoWidget::timestampRoiReady,
            this, [this](const QRectF &norm) {
                if (m_roiDialog) {
                    saveTimestampRoi(m_currentVideoPath, norm);
                    m_roiDialog->stageTimestampRoi(norm);
                }
                m_videoWidget->endTimestampRoiSelection();
                m_roiDialog = nullptr;
            });
    connect(m_videoWidget, &VideoWidget::timestampRoiCancelled,
            this, [this]() {
                if (m_roiDialog)
                    m_roiDialog->stageTimestampRoi(QRectF());
                m_videoWidget->endTimestampRoiSelection();
                m_roiDialog = nullptr;
            });

    createMenus();
    createToolBar();
    setupConnections();

    // v1.3.0 M2 任务11：启动欢迎面板（2026-08 改为页面内居中非模态，
    // 迁自模态起始页；可勾选不再显示；独立模式 = v1.2.2 行为）
    QTimer::singleShot(0, this, [this]() {
        if (CaseOpenPanel::showStartupPanel())
            onShowStartPage();
    });
}

MainWindow::~MainWindow()
{
    disconnect(m_analysisEngine, nullptr, this, nullptr);
    m_analysisEngine->cancelAnalysis();
}

/// @brief 打开素材转码拼接独立任务窗口（每次开启新会话；WA_DeleteOnClose 自销毁）
void MainWindow::openPreprocessWindow()
{
    auto *w = new PreprocessWindow(m_analysisEngine, this);
    // v1.3.0 M2 任务8：案件模式注入（有打开案件时成果默认导入案件）
    w->setCaseManager(m_caseManager);
    connect(w, &PreprocessWindow::openOutputRequested,
            this, &MainWindow::openVideoFile);
    w->show();
    w->raise();
    w->activateWindow();
}

/// @brief 创建菜单栏：文件/编辑/导出/帮助
void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(lang("文件(&F)", "&File"));
    fileMenu->addAction(lang("打开视频(&O)...", "&Open Video..."), this, &MainWindow::onOpenFile, QKeySequence::Open);
    // v1.3.0 M2 任务7：临时打开（不入案）——有打开案件时与 Ctrl+O 的唯一区别
    fileMenu->addAction(lang("临时打开视频（不入案）(&T)...", "Open Video &Temporarily (no case)..."), this, &MainWindow::onOpenFileTemporary);

    // v1.3.0 M2：案件菜单（新建/打开/最近/起始页/属性/根目录设置 +
    // 关闭案件 Ctrl+W = 模式出口二）
    QMenu *caseMenu = menuBar()->addMenu(lang("案件(&C)", "&Case"));
    caseMenu->addAction(lang("新建案件(&N)...", "&New Case..."), this,
                        &MainWindow::onNewCase);
    caseMenu->addAction(lang("打开案件(&O)...", "&Open Case..."), this,
                        &MainWindow::onOpenCase);
    QMenu *recentMenu = caseMenu->addMenu(lang("最近案件(&R)", "&Recent Cases"));
    connect(recentMenu, &QMenu::aboutToShow, this, [this, recentMenu]() {
        recentMenu->clear();
        const QStringList recents = m_caseManager->recentCases();
        if (recents.isEmpty()) {
            recentMenu->addAction(lang("（暂无）", "(none)"))->setEnabled(false);
            return;
        }
        for (const QString &dir : recents) {
            recentMenu->addAction(QFileInfo(dir).fileName(), this,
                [this, dir]() { openCaseFlow(dir); })
                ->setToolTip(dir);
        }
    });
    caseMenu->addAction(lang("起始页(&S)...", "&Start Page..."), this,
                        &MainWindow::onShowStartPage);
    caseMenu->addSeparator();
    m_casePropsAction = caseMenu->addAction(
        lang("案件属性(&P)...", "Case &Properties..."), this,
        &MainWindow::onCaseProperties);
    m_casePropsAction->setEnabled(false);
    // v1.3.0 M3 任务12：导出移交包
    m_exportCaseAction = caseMenu->addAction(
        lang("导出移交包(&E)...", "&Export Handover Package..."), this,
        &MainWindow::onExportCase);
    m_exportCaseAction->setEnabled(false);
    // v1.3.0 M3 任务13：批量重新定位
    m_batchRelocateAction = caseMenu->addAction(
        lang("批量重新定位(&B)...", "&Batch Relocate..."), this,
        &MainWindow::onBatchRelocate);
    m_batchRelocateAction->setEnabled(false);
    // v1.3.0 M3 任务14：多机时间线对齐只读视图（<2 路已校时置灰）
    m_multiCamAction = caseMenu->addAction(
        lang("多机时间线(&M)...", "&Multi-camera Timeline..."), this,
        &MainWindow::onMultiCamView);
    m_multiCamAction->setEnabled(false);
    // 已校时数随案内变化（写 .vla 刷新徽标后重判）→ 菜单弹出时动态置灰
    connect(caseMenu, &QMenu::aboutToShow, this, [this]() {
        if (m_multiCamAction)
            m_multiCamAction->setEnabled(
                m_caseManager->isOpen()
                && m_caseManager->calibratedVideoCount() >= 2);
    });
    caseMenu->addAction(lang("案件根目录设置(&D)...", "Case &Root Folder..."),
                        this, &MainWindow::onCaseRootDir);
    caseMenu->addSeparator();
    m_closeCaseAction = caseMenu->addAction(
        lang("关闭案件(&W)", "&Close Case"), this,
        &MainWindow::closeCaseWithPrompt,
        QKeySequence(QStringLiteral("Ctrl+W")));
    m_closeCaseAction->setEnabled(false);
    fileMenu->addAction(lang("素材转码拼接(&M)...", "&Transcode & Merge..."), this, &MainWindow::openPreprocessWindow, QKeySequence(QStringLiteral("Ctrl+M")));
    fileMenu->addAction(lang("加载图片为叠加(&I)...", "Load Image as &Overlay..."), this, &MainWindow::onLoadOverlayImage);
    fileMenu->addSeparator();
    fileMenu->addAction(lang("保存分析结果(&S)...", "&Save Analysis Result..."), this, &MainWindow::onSaveAnalysis, QKeySequence::Save);
    fileMenu->addAction(lang("加载分析结果(&L)...", "&Load Analysis Result..."), this, &MainWindow::onLoadAnalysis, QKeySequence(QStringLiteral("Ctrl+L")));
    fileMenu->addSeparator();
    fileMenu->addAction(lang("退出(&X)", "E&xit"), this, &QWidget::close, QKeySequence::Quit);

    QMenu *editMenu = menuBar()->addMenu(lang("编辑(&E)", "&Edit"));
    editMenu->addAction(lang("清除选区(&R)", "Clear &Regions"), this, &MainWindow::onClearRegions);
    editMenu->addAction(lang("清除数据(&D)", "Clear &Data"), this, &MainWindow::onClearData);

    QMenu *exportMenu = menuBar()->addMenu(lang("导出(&X)", "&Export"));
    exportMenu->addAction(lang("导出为 CSV(&C)...", "Export to &CSV..."), this, &MainWindow::onExportCsv);

    // Settings menu
    QMenu *settingsMenu = menuBar()->addMenu(lang("设置(&S)", "&Settings"));

    // 分析引擎（v1.5.0 P3：libav 默认 / Python 回退，重启生效）
    QMenu *engineMenu = settingsMenu->addMenu(
        lang("分析引擎（重启生效）", "Analysis Engine (restart required)"));
    QActionGroup *engineGroup = new QActionGroup(this);
    {
        QSettings es("LumenArc", "LumenArc");
        const QString cur = es.value("analysisEngine", QStringLiteral("libav")).toString();
        auto addEngine = [&](const QString &title, const QString &key) {
            QAction *a = engineMenu->addAction(title);
            a->setCheckable(true);
            engineGroup->addAction(a);
            if (cur == key)
                a->setChecked(true);
            connect(a, &QAction::triggered, this, [key]() {
                QSettings s("LumenArc", "LumenArc");
                s.setValue("analysisEngine", key);
            });
        };
        addEngine(lang("libav（原生，快）", "libav (native, fast)"),
                  QStringLiteral("libav"));
        addEngine(lang("Python（回退）", "Python (fallback)"),
                  QStringLiteral("python"));
    }

    QAction *hwAction = settingsMenu->addAction(lang("硬件解码（重启生效）", "Hardware Decoding (restart required)"));
    hwAction->setCheckable(true);
    {
        QSettings s("LumenArc", "LumenArc");
        hwAction->setChecked(s.value("hwDecode", true).toBool());
    }
    connect(hwAction, &QAction::toggled, this, [](bool on) {
        QSettings s("LumenArc", "LumenArc");
        s.setValue("hwDecode", on);
    });

    // 硬解设备选择（自动=偏好独显；重启生效）
    QMenu *adapterMenu = settingsMenu->addMenu(lang("硬解设备（重启生效）", "HW Decode Adapter (restart required)"));
    QActionGroup *adapterGroup = new QActionGroup(this);
    auto addAdapterAction = [this, adapterMenu, adapterGroup](const QString &title, int index, int current) {
        QAction *a = adapterMenu->addAction(title);
        a->setCheckable(true);
        adapterGroup->addAction(a);
        if (index == current)
            a->setChecked(true);
        connect(a, &QAction::triggered, this, [index]() {
            QSettings s("LumenArc", "LumenArc");
            s.setValue("hwAdapter", index);
        });
    };
    {
        QSettings s("LumenArc", "LumenArc");
        int current = s.value("hwAdapter", -1).toInt();
        addAdapterAction(lang("自动（偏好独显）", "Auto (prefer discrete GPU)"), -1, current);
        for (const auto &ad : FfmpegVideoEngine::availableAdapters())
            addAdapterAction(ad.name, ad.index, current);
    }

    // Help menu
    QMenu *helpMenu = menuBar()->addMenu(lang("帮助(&H)", "&Help"));

    // Language submenu
    QMenu *langMenu = helpMenu->addMenu(lang("语言", "Language"));
    QAction *zhAction = langMenu->addAction("中文");
    QAction *enAction = langMenu->addAction("English");
    if (g_language == LangChinese) zhAction->setChecked(true);
    else enAction->setChecked(true);
    connect(zhAction, &QAction::triggered, []() {
        saveLanguage(LangChinese);
        restartApp();
    });
    connect(enAction, &QAction::triggered, []() {
        saveLanguage(LangEnglish);
        restartApp();
    });

    helpMenu->addSeparator();
    helpMenu->addAction(lang("使用手册", "User Manual"), []() {
        QString path = QCoreApplication::applicationDirPath() + "/追光者 Lumen Arc v1.0 — 操作手册.pdf";
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    helpMenu->addSeparator();
    helpMenu->addAction(lang("快捷键速查", "Keyboard Shortcuts"), this, [this]() {
        struct Shortcut { QString key; QString desc; };
        QVector<Shortcut> shortcuts;
        if (g_language == LangChinese) {
            shortcuts = {
                {"Space / K", "播放 / 暂停"}, {"← / →", "后退 / 前进一帧"},
                {"↑ / ↓", "音量增大 / 减小"}, {"C / L", "加速一档"}, {"X / J", "减速一档"},
                {"Z", "恢复 1x 倍速"}, {"N", "在当前位置添加标签"},
                {"A", "设置 A 点"}, {"B", "设置 B 点"},
                {"S", "保存证据快照（帧+曲线合成 PNG）"},
                {"P", "进入多边形模式"}, {"G", "进入辅助线模式"},
                {"Delete", "删除选中的 ROI / 辅助线"},
                {"右键", "删除鼠标下的 ROI / 辅助线（无需先选中）"},
                {"Esc", "关闭放大镜 / 退出当前模式"},
                {"Ctrl+S", "保存分析结果"},
                {"Ctrl+L", "加载分析结果"},
                {"Ctrl+O", "打开视频文件"},
            };
        } else {
            shortcuts = {
                {"Space / K", "Play / Pause"}, {"← / →", "Prev / Next Frame"},
                {"↑ / ↓", "Volume Up / Down"}, {"C / L", "Speed Up"}, {"X / J", "Slow Down"},
                {"Z", "Reset to 1x"}, {"N", "Add Label at Current Position"},
                {"A", "Set A Point"}, {"B", "Set B Point"},
                {"S", "Save evidence snapshot (frame+chart PNG)"},
                {"P", "Enter Polygon Mode"}, {"G", "Enter Guide Line Mode"},
                {"Delete", "Delete Selected ROI / Guide Line"},
                {"Right-click", "Delete ROI / Guide Line under cursor (no selection needed)"},
                {"Esc", "Close Magnifier / Exit Mode"},
                {"Ctrl+S", "Save Analysis"},
                {"Ctrl+L", "Load Analysis"},
                {"Ctrl+O", "Open Video"},
            };
        }

        QDialog *dlg = new QDialog(this);
        dlg->setWindowTitle(lang("快捷键速查", "Keyboard Shortcuts"));
        dlg->setWindowOpacity(0.75);
        dlg->setFixedSize(420, 520);
        dlg->setStyleSheet("QDialog { background: " + Theme::BgPanel + "; }");

        QVBoxLayout *layout = new QVBoxLayout(dlg);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(8);

        QLabel *title = new QLabel(lang("⌨ 快捷键速查", "⌨ Keyboard Shortcuts"));
        title->setStyleSheet("color: " + Theme::TextPrimary + "; font-size: 15px; font-weight: bold; padding: 4px 0;");
        layout->addWidget(title);

        QTableWidget *table = new QTableWidget(shortcuts.size(), 2, dlg);
        table->horizontalHeader()->setVisible(false);
        table->verticalHeader()->setVisible(false);
        table->setShowGrid(false);
        table->setEditTriggers(QAbstractItemView::NoEditTriggers);
        table->setSelectionMode(QAbstractItemView::NoSelection);
        table->setFocusPolicy(Qt::NoFocus);
        table->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
        table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
        table->setColumnWidth(0, 130);
        table->setStyleSheet(
            "QTableWidget { background: " + Theme::BgPanel + "; border: none; color: " + Theme::TextPrimary + "; font-size: 12px; }"
            "QTableWidget::item { padding: 4px 8px; border-bottom: 1px solid " + Theme::Border + "; }"
            "QHeaderView::section { background: " + Theme::BgPanel + "; border: none; }"
        );
        for (int i = 0; i < shortcuts.size(); ++i) {
            QTableWidgetItem *keyItem = new QTableWidgetItem(shortcuts[i].key);
            keyItem->setForeground(QBrush(QColor(Theme::Accent)));
            keyItem->setFont(QFont("Consolas", 11, QFont::Bold));
            table->setItem(i, 0, keyItem);
            QTableWidgetItem *descItem = new QTableWidgetItem(shortcuts[i].desc);
            descItem->setForeground(QBrush(QColor(Theme::TextPrimary)));
            table->setItem(i, 1, descItem);
        }
        table->setRowHeight(shortcuts.size(), 0);
        layout->addWidget(table);

        QLabel *hint = new QLabel(lang("按 Esc 或点击 ✕ 关闭", "Press Esc or click ✕ to close"));
        hint->setStyleSheet("color: " + Theme::TextMuted + "; font-size: 11px; padding: 4px 0;");
        hint->setAlignment(Qt::AlignCenter);
        layout->addWidget(hint);

        dlg->exec();
    });

    helpMenu->addSeparator();
    helpMenu->addAction(lang("关于", "&About"), this, [this]() {
        AboutDialog dlg(this);
        dlg.exec();
    });
}

/// @brief 创建工具栏：播放/分析/截图融合按钮组
void MainWindow::createToolBar()
{
    // --- 三层按钮体系：主操作(实心金) / 次级(无边框卡片) / 幽灵图标 ---
    const QString btnBase =
        "QPushButton {"
        "  height: 32px; border: none; border-radius: 6px;"
        "  background: " + Theme::BgCard + "; color: " + Theme::TextPrimary + ";"
        "  font-family: 'Segoe UI', 'Microsoft YaHei'; font-size: 12px;"
        "  padding: 0 10px;"
        "}"
        "QPushButton:hover { background: " + Theme::BgHover + "; }"
        "QPushButton:pressed { background: " + Theme::BgPressed + "; }"
        "QPushButton:disabled { background: " + Theme::BgPressed + "; color: " + Theme::TextMuted + "; }";

    // 工具组（截图融合）：次级样式，选中态金色描边
    const QString fusionBtnStyle =
        "QPushButton {"
        "  height: 32px; border: none; border-radius: 6px;"
        "  background: " + Theme::BgCard + "; color: " + Theme::TextSecond + ";"
        "  font-family: 'Segoe UI', 'Microsoft YaHei'; font-size: 12px;"
        "  padding: 0 10px;"
        "}"
        "QPushButton:hover { background: " + Theme::BgHover + "; color: " + Theme::TextPrimary + "; }"
        "QPushButton:pressed { background: " + Theme::BgPressed + "; }"
        "QPushButton:disabled { background: " + Theme::BgPressed + "; color: " + Theme::TextMuted + "; }"
        "QPushButton:checked { background: " + Theme::BgCard + "; color: " + Theme::Accent + "; border: 1px solid " + Theme::Accent + "; }";

    // 幽灵图标按钮：透明底 + hover 微光
    const QString iconBtnStyle =
        "QPushButton {"
        "  width: 32px; height: 32px; border: none; border-radius: 6px;"
        "  background: transparent; padding: 0;"
        "}"
        "QPushButton:hover { background: " + Theme::BgHover + "; }"
        "QPushButton:pressed { background: " + Theme::BgPressed + "; }"
        "QPushButton:disabled { background: transparent; }";

    // 倍速芯片：胶囊形状态徽章
    const QString speedBtnStyle =
        "QPushButton {"
        "  width: 46px; height: 32px; border: none; border-radius: 16px;"
        "  background: " + Theme::BgCard + "; color: " + Theme::Accent + "; font-weight: bold;"
        "  font-family: 'Consolas', monospace; font-size: 12px; padding: 0;"
        "}"
        "QPushButton:hover { background: " + Theme::BgHover + "; color: " + Theme::AccentHover + "; }"
        "QPushButton:pressed { background: " + Theme::BgPressed + "; }"
        "QPushButton:disabled { background: " + Theme::BgPressed + "; color: " + Theme::TextMuted + "; }";

    // 主操作：实心金底深字（视觉焦点，每屏唯一）
    const QString primaryBtnStyle =
        "QPushButton {"
        "  height: 32px; border: none; border-radius: 6px;"
        "  background: " + Theme::Accent + "; color: " + Theme::AccentOnDark + ";"
        "  font-family: 'Segoe UI', 'Microsoft YaHei'; font-size: 12px; font-weight: bold;"
        "  padding: 0 14px; min-width: 80px;"
        "}"
        "QPushButton:hover { background: " + Theme::AccentHover + "; }"
        "QPushButton:pressed { background: " + Theme::AccentPress + "; }"
        "QPushButton:disabled { background: " + Theme::BgPressed + "; color: " + Theme::TextMuted + "; }";

    const QString timeLabelStyle =
        "QLabel { font-family: 'Consolas', monospace; font-size: 13px; color: " + Theme::TextPrimary + "; padding: 0 8px; }";

    QToolBar *toolBar = addToolBar("Main");
    toolBar->setIconSize(QSize(18, 18));

    // --- Playback group ---
    m_playBtn = new QPushButton(this);
    m_playBtn->setIcon(QIcon(QStringLiteral(":/icons/play.svg")));
    m_playBtn->setToolTip(lang("播放", "Play"));
    m_playBtn->setFixedSize(32, 32);
    m_playBtn->setIconSize(QSize(18, 18));
    m_playBtn->setStyleSheet(iconBtnStyle);
    m_playBtn->setEnabled(false);

    m_pauseBtn = new QPushButton(this);
    m_pauseBtn->setIcon(QIcon(QStringLiteral(":/icons/pause.svg")));
    m_pauseBtn->setToolTip(lang("暂停", "Pause"));
    m_pauseBtn->setFixedSize(32, 32);
    m_pauseBtn->setIconSize(QSize(18, 18));
    m_pauseBtn->setStyleSheet(iconBtnStyle);
    m_pauseBtn->setEnabled(false);

    m_stopBtn = new QPushButton(this);
    m_stopBtn->setIcon(QIcon(QStringLiteral(":/icons/stop.svg")));
    m_stopBtn->setToolTip(lang("停止", "Stop"));
    m_stopBtn->setFixedSize(32, 32);
    m_stopBtn->setIconSize(QSize(18, 18));
    m_stopBtn->setStyleSheet(iconBtnStyle);
    m_stopBtn->setEnabled(false);

    m_speedBtn = new QPushButton("1x", this);
    m_speedBtn->setToolTip(lang("倍速播放 (0.25x/0.5x/1x/2x/4x/8x)", "Playback speed (0.25x/0.5x/1x/2x/4x/8x)"));
    m_speedBtn->setFixedSize(46, 32);
    m_speedBtn->setStyleSheet(speedBtnStyle);
    m_speedBtn->setEnabled(false);

    // --- Analyze group ---
    m_analyzeBtn = new QPushButton(lang("亮度分析", "Luminance"), this);
    m_analyzeBtn->setToolTip(lang("分析当前视频的亮度（无需播放）", "Analyze current video luminance (no playback needed)"));
    m_analyzeBtn->setFixedHeight(32);
    m_analyzeBtn->setStyleSheet(primaryBtnStyle);
    m_analyzeBtn->setEnabled(false);

    // 次主操作：金色描边轮廓样式（与实心主按钮形成层级）
    const QString audioBtnStyle =
        "QPushButton {"
        "  height: 32px; border: 1px solid " + Theme::Accent + "; border-radius: 6px;"
        "  background: transparent; color: " + Theme::Accent + ";"
        "  font-family: 'Segoe UI', 'Microsoft YaHei'; font-size: 12px; font-weight: bold;"
        "  padding: 0 14px; min-width: 80px;"
        "}"
        "QPushButton:hover { background: rgba(240, 180, 41, 25); }"
        "QPushButton:pressed { background: rgba(240, 180, 41, 45); }"
        "QPushButton:disabled { background: transparent; color: " + Theme::TextMuted + "; border-color: " + Theme::Border + "; }";
    m_audioAnalysisBtn = new QPushButton(lang("音频分析", "Audio"), this);
    m_audioAnalysisBtn->setToolTip(lang("独立分析音频（频谱图+音量）", "Analyze audio only (spectrogram + volume)"));
    m_audioAnalysisBtn->setFixedHeight(32);
    m_audioAnalysisBtn->setStyleSheet(audioBtnStyle);
    m_audioAnalysisBtn->setEnabled(false);

    m_setTimeBtn = new QPushButton(lang("校时…", "Calibrate…"), this);
    m_setTimeBtn->setToolTip(lang("视频校时：自动识别/手动/北京时间校验",
                                  "Time calibration: auto OCR / manual / Beijing-time check"));
    m_setTimeBtn->setFixedHeight(32);
    m_setTimeBtn->setStyleSheet(btnBase);
    m_setTimeBtn->setEnabled(false);

    // --- Fusion group ---
    m_captureBtn = new QPushButton(lang("截取", "Capture"), this);
    m_captureBtn->setToolTip(lang("截取当前帧", "Capture current frame"));
    m_captureBtn->setFixedHeight(32);
    m_captureBtn->setStyleSheet(fusionBtnStyle);
    m_captureBtn->setEnabled(false);

    m_editBtn = new QPushButton(lang("编辑", "Edit"), this);
    m_editBtn->setToolTip(lang("编辑截图叠加", "Edit snapshot overlay"));
    m_editBtn->setFixedHeight(32);
    m_editBtn->setStyleSheet(fusionBtnStyle);
    m_editBtn->setEnabled(false);

    m_placeBtn = new QPushButton(lang("放置", "Place"), this);
    m_placeBtn->setToolTip(lang("切换叠加显示", "Toggle overlay on/off"));
    m_placeBtn->setFixedHeight(32);
    m_placeBtn->setStyleSheet(fusionBtnStyle);
    m_placeBtn->setCheckable(true);
    m_placeBtn->setEnabled(false);

    // 播放选项包（2026-08-14）：证据快照 + 画面调节面板开关
    m_snapshotBtn = new QPushButton(lang("快照", "Snapshot"), this);
    m_snapshotBtn->setToolTip(lang(
        "保存证据快照：当前帧+曲线分析合成 PNG（快捷键 S）",
        "Save evidence snapshot: frame + chart composite PNG (S)"));
    m_snapshotBtn->setFixedHeight(32);
    m_snapshotBtn->setStyleSheet(fusionBtnStyle);
    m_snapshotBtn->setEnabled(false);

    m_adjustBtn = new QPushButton(lang("画面调节", "Adjust"), this);
    m_adjustBtn->setToolTip(lang("播放画面亮度/对比度调节（仅显示，不动证据）",
                                 "Playback brightness/contrast (display only)"));
    m_adjustBtn->setFixedHeight(32);
    m_adjustBtn->setStyleSheet(fusionBtnStyle);
    m_adjustBtn->setCheckable(true);

    // v0.5: ROI 模式按钮组（分段控件：共享圆角外框）
    const QString modeBtnStyle =
        "QPushButton {"
        "  height: 28px; border: none; border-radius: 4px;"
        "  background: transparent; color: " + Theme::TextSecond + ";"
        "  font-family: 'Segoe UI', 'Microsoft YaHei'; font-size: 12px;"
        "  padding: 0 10px;"
        "}"
        "QPushButton:hover { color: " + Theme::TextPrimary + "; background: " + Theme::BgHover + "; }"
        "QPushButton:checked { background: " + Theme::Accent + "; color: " + Theme::AccentOnDark + "; font-weight: bold; }";

    m_rectModeBtn = new QPushButton(lang("矩形", "Rect"), this);
    m_rectModeBtn->setToolTip(lang("矩形ROI模式 (P)", "Rect ROI Mode (P)"));
    m_rectModeBtn->setFixedHeight(28);
    m_rectModeBtn->setStyleSheet(modeBtnStyle);
    m_rectModeBtn->setCheckable(true);
    m_rectModeBtn->setChecked(true);

    m_polygonModeBtn = new QPushButton(lang("多边形", "Polygon"), this);
    m_polygonModeBtn->setToolTip(lang(
        "多边形 ROI 模式 (P)\n"
        "· 单击添加顶点\n"
        "· 双击闭合多边形\n"
        "· 右键取消绘制\n"
        "· Esc 退出模式",
        "Polygon ROI Mode (P)\n"
        "· Click to add vertex\n"
        "· Double-click to close\n"
        "· Right-click to cancel\n"
        "· Esc to exit mode"));
    m_polygonModeBtn->setFixedHeight(28);
    m_polygonModeBtn->setStyleSheet(modeBtnStyle);
    m_polygonModeBtn->setCheckable(true);

    m_guideLineBtn = new QPushButton(lang("辅助线", "Guide"), this);
    m_guideLineBtn->setToolTip(lang(
        "辅助线模式 (G)\n"
        "· 左键拖拽绘制辅助线\n"
        "· Shift 约束水平/垂直\n"
        "· 左键点击线体：移动整条线\n"
        "· 左键拖拽端点：调整位置\n"
        "· 右键点击：删除\n"
        "· Esc 退出模式",
        "Guide Line Mode (G)\n"
        "· Left-drag to draw\n"
        "· Shift constrains H/V\n"
        "· Click line: move\n"
        "· Drag endpoint: resize\n"
        "· Right-click: delete\n"
        "· Esc to exit mode"));
    m_guideLineBtn->setFixedHeight(28);
    m_guideLineBtn->setStyleSheet(modeBtnStyle);
    m_guideLineBtn->setCheckable(true);

    // 分段控件容器：三个模式按钮共享一个圆角外框
    auto *modeSegment = new QWidget(this);
    modeSegment->setStyleSheet("background: " + Theme::BgCard + "; border-radius: 6px;");
    auto *modeLayout = new QHBoxLayout(modeSegment);
    modeLayout->setContentsMargins(2, 2, 2, 2);
    modeLayout->setSpacing(2);
    modeLayout->addWidget(m_rectModeBtn);
    modeLayout->addWidget(m_polygonModeBtn);
    modeLayout->addWidget(m_guideLineBtn);

    m_copyRoiBtn = new QPushButton(lang("复制ROI", "Copy ROI"), this);
    m_copyRoiBtn->setToolTip(lang("复制ROI区域 (Ctrl+Shift+C)", "Copy ROI (Ctrl+Shift+C)"));
    m_copyRoiBtn->setFixedHeight(32);
    m_copyRoiBtn->setStyleSheet(btnBase);

    m_pasteRoiBtn = new QPushButton(lang("粘贴ROI", "Paste ROI"), this);
    m_pasteRoiBtn->setToolTip(lang("粘贴ROI区域 (Ctrl+Shift+V)", "Paste ROI (Ctrl+Shift+V)"));
    m_pasteRoiBtn->setFixedHeight(32);
    m_pasteRoiBtn->setStyleSheet(btnBase);
    m_pasteRoiBtn->setEnabled(false);

    m_chartPanel->setAutoYRange(true);

    // --- Layout ---
    toolBar->addWidget(m_playBtn);
    toolBar->addWidget(m_pauseBtn);
    toolBar->addWidget(m_stopBtn);
    toolBar->addWidget(m_speedBtn);
    toolBar->addSeparator();
    toolBar->addWidget(m_analyzeBtn);
    toolBar->addWidget(m_audioAnalysisBtn);  // v0.3: Audio analysis
    toolBar->addWidget(m_setTimeBtn);
    toolBar->addSeparator();
    toolBar->addWidget(modeSegment);
    toolBar->addSeparator();
    toolBar->addWidget(m_copyRoiBtn);
    toolBar->addWidget(m_pasteRoiBtn);
    toolBar->addSeparator();
    toolBar->addWidget(m_captureBtn);
    toolBar->addWidget(m_editBtn);
    toolBar->addWidget(m_placeBtn);
    toolBar->addSeparator();
    toolBar->addWidget(m_snapshotBtn);
    toolBar->addWidget(m_adjustBtn);
    toolBar->addSeparator();

    m_timeLabel = new QLabel("00:00 / 00:00", this);
    m_timeLabel->setStyleSheet(timeLabelStyle);
    toolBar->addWidget(m_timeLabel);

    // v1.2: 素材转码拼接入口（独立任务窗口，显眼入口，UI 重设计 D1）
    auto *tbSpacer = new QWidget(this);
    tbSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar->addWidget(tbSpacer);
    auto *preprocessBtn = new QPushButton(lang("素材转码拼接", "Transcode & Merge"), this);
    preprocessBtn->setToolTip(lang("多段监控录像智能排序、无损拼接与统一格式",
                                   "Sort, losslessly merge and normalize surveillance clips"));
    preprocessBtn->setMinimumHeight(30);
    preprocessBtn->setStyleSheet(QStringLiteral(
        "QPushButton { background: %1; color: %2; font-weight: bold; "
        "border-radius: 6px; padding: 4px 14px; }"
        "QPushButton:hover { background: %3; }")
        .arg(Theme::Accent, Theme::AccentOnDark, Theme::AccentHover));
    toolBar->addWidget(preprocessBtn);
    connect(preprocessBtn, &QPushButton::clicked,
            this, &MainWindow::openPreprocessWindow);

    // Prevent toolbar buttons from stealing keyboard focus
    for (auto *btn : toolBar->findChildren<QPushButton*>()) {
        btn->setFocusPolicy(Qt::NoFocus);
    }

    // 播放画面调节面板（2026-08-14）：dock 常驻模式，默认隐藏，
    // 工具栏「画面调节」按钮调出后一直开着直到手动关闭。
    m_adjustPanel = new PlaybackAdjustPanel(this);
    addDockWidget(Qt::RightDockWidgetArea, m_adjustPanel);
    m_adjustPanel->hide();
}

/// @brief 连接所有信号槽：引擎→UI更新/按钮→槽/截图同步
void MainWindow::setupConnections()
{
    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::onPlay);
    connect(m_pauseBtn, &QPushButton::clicked, this, &MainWindow::onPause);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(m_speedBtn, &QPushButton::clicked, this, &MainWindow::cycleSpeed);
    connect(m_analyzeBtn, &QPushButton::clicked, this, &MainWindow::onAnalyze);
    connect(m_audioAnalysisBtn, &QPushButton::clicked, this, &MainWindow::onAudioAnalysis);
    connect(m_setTimeBtn, &QPushButton::clicked, this, &MainWindow::onSetStartTime);

    connect(m_videoEngine, &IVideoEngine::positionChanged,
            this, &MainWindow::onPositionChanged);
    connect(m_videoEngine, &IVideoEngine::durationChanged,
            this, &MainWindow::onDurationChanged);

    // Noise floor slider: update spectrogram min value
    connect(m_noiseFloorSlider, &QSlider::valueChanged, this, [this](int value) {
        qreal db = value / 10.0;
        m_noiseFloorValueLabel->setText(QString::number(db, 'f', 1));
        m_noiseFloorSlider->setToolTip(QString("底噪阈值: %1 dB").arg(db, 0, 'f', 1));
        if (m_spectrogramEnhanced) {
            m_spectrogramEnhanced->setNoiseFloor(db);
        }
    });

    // Noise reduction slider
    connect(m_noiseReductionSlider, &QSlider::valueChanged, this, [this](int value) {
        m_noiseReductionStrength = value / 10.0;
        m_noiseReductionValueLabel->setText(QString::number(m_noiseReductionStrength, 'f', 1));
    });
    // Apply button: directly connected via member variable
    connect(m_nrApplyBtn, &QPushButton::clicked, this, [this]() {
        if (m_noiseReductionStrength > 0 && !m_currentVideoPath.isEmpty()) {
            auto *pyEngine = qobject_cast<PythonAnalysisEngine *>(m_analysisEngine);
            if (pyEngine) {
                pyEngine->setNoiseReduction(m_noiseReductionStrength);
            }
            onAudioAnalysis();
        }
    });

    connect(m_chartPanel, &ChartPanel::seekRequested,
            this, &MainWindow::onSeekFromChart);
    // 拖拽松手：退出 scrub 模式 + 最终精确 seek
    connect(m_chartPanel, &ChartPanel::scrubEnded, this, [this]() {
        m_videoEngine->setScrubMode(false);
        qint64 pos = m_chartPanel->cursorTime();
        m_videoEngine->seek(pos);
    });

    connect(m_analysisEngine, &IAnalysisEngine::progressUpdated,
            this, &MainWindow::onAnalysisProgress);
    connect(m_analysisEngine, &IAnalysisEngine::analysisFinished,
            this, &MainWindow::onAnalysisFinished);
    connect(m_analysisEngine, &IAnalysisEngine::analysisFailed,
            this, &MainWindow::onAnalysisFailed);

    // Magnifier signals from video overlay
    auto *overlay = m_videoWidget->overlay();
    connect(overlay, &OverlayWidget::magnifierWheelZoom,
            this, &MainWindow::onMagnifierWheelZoom);
    connect(overlay, &OverlayWidget::magnifierPanRequested, this, [this](QPoint delta) {
        if (m_magnifier) {
            if (m_magnifier->isinvertPan())
                delta = -delta;
            QPoint newPos = m_magnifier->cursorPosition() + delta;
            m_magnifier->updateCursorPosition(newPos);
        }
    });

    // ROI adjustment warning: check for existing analysis data
    connect(overlay, &OverlayWidget::regionAdjustmentFinished,
            this, [this](int regionIndex, const QRect &originalRect, const QRect &newRect) {
                Q_UNUSED(newRect);
                AnalysisSnapshot snapshot = m_timelineModel->snapshot();
                if (!snapshot.isEmpty()) {
                    int roiId = m_roiModel->roiIdAt(regionIndex);
                    if (roiId > 0 && snapshot.dataIndexOfRoiId(roiId, DataEntry::Rect) >= 0) {
                        auto reply = QMessageBox::question(this,
                            lang("数据失效警告", "Data Invalidation Warning"),
                            lang("调整该区域将导致亮度量化数据失效。\n确定要继续吗？",
                                 "Adjusting this region will invalidate the luminance analysis data.\nContinue?"),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                        if (reply == QMessageBox::Yes) {
                            m_timelineModel->removeRegionDataByRoiId(roiId, DataEntry::Rect);
                        } else {
                            m_roiModel->updateRegion(regionIndex, originalRect);
                        }
                    }
                }
            });

    // Bug fix: When an ROI region is deleted, remove its corresponding analysis data
    // to prevent stale curves from appearing when a new ROI is drawn at the same index.
    connect(m_roiModel, &RoiModel::regionRemoved,
            this, [this](int index, int roiId) {
                Q_UNUSED(index);
                AnalysisSnapshot snapshot = m_timelineModel->snapshot();
                if (!snapshot.isEmpty()) {
                    m_timelineModel->removeRegionDataByRoiId(roiId, DataEntry::Rect);
                }
            });

    // When a polygon ROI is deleted, remove its data by ROI ID
    connect(m_roiModel, &RoiModel::polygonRemoved,
            this, [this](int index, int roiId) {
                Q_UNUSED(index);
                AnalysisSnapshot snapshot = m_timelineModel->snapshot();
                if (!snapshot.isEmpty()) {
                    m_timelineModel->removeRegionDataByRoiId(roiId, DataEntry::Polygon);
                }
            });

    // Polygon adjustment: show data invalidation warning
    connect(overlay, &OverlayWidget::polygonAdjustmentFinished,
            this, [this](int polygonIndex, const QPolygon &originalPolygon, const QPolygon &newPolygon) {
                Q_UNUSED(newPolygon);
                AnalysisSnapshot snapshot = m_timelineModel->snapshot();
                if (!snapshot.isEmpty()) {
                    int roiId = m_roiModel->polygonRoiIdAt(polygonIndex);
                    int dataIdx = snapshot.dataIndexOfRoiId(roiId, DataEntry::Polygon);
                    if (dataIdx >= 0) {
                        auto reply = QMessageBox::question(this,
                            lang("数据失效警告", "Data Invalidation Warning"),
                            lang("调整该多边形将导致亮度量化数据失效。\n确定要继续吗？",
                                 "Adjusting this polygon will invalidate the luminance analysis data.\nContinue?"),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                        if (reply == QMessageBox::Yes) {
                            m_timelineModel->removeRegionDataByRoiId(roiId, DataEntry::Polygon);
                        } else {
                            m_roiModel->updatePolygon(polygonIndex, originalPolygon);
                        }
                    }
                }
            });

    // Right-click context menu on video overlay
    overlay->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(overlay, &QWidget::customContextMenuRequested,
            this, &MainWindow::showVideoContextMenu);

    // Install event filter for global shortcut handling
    overlay->installEventFilter(this);
    menuBar()->installEventFilter(this);
    this->installEventFilter(this);  // Global shortcut handling
    m_videoListPanel->listWidget()->installEventFilter(this);  // 视频列表快捷键

    // Forward video frames to magnifier and pinned
    connect(m_videoEngine, &IVideoEngine::frameReady,
            this, [this](const QImage &img) {
                if (m_magnifier)
                    m_magnifier->onFrameReady(img);
                updatePinnedImage(img);
            });

    // Snapshot overlay connections
    connect(m_snapshotOverlay, &SnapshotOverlay::captureRequested,
            this, [this]() {
                m_videoWidget->grabFrameSnapshot();
            });
    connect(m_snapshotOverlay, &SnapshotOverlay::clearRequested, this, [this]() {
        m_videoWidget->clearSnapshot();
        m_snapshotFusion = SnapshotFusionData();
        if (m_magnifier) {
            m_magnifier->clearSnapshotOverlay();
        }
    });

    // Fusion buttons
    connect(m_captureBtn, &QPushButton::clicked, this, [this]() {
        m_videoWidget->grabFrameSnapshot();
    });
    // 证据快照 + 画面调节面板（2026-08-14）
    connect(m_snapshotBtn, &QPushButton::clicked,
            this, &MainWindow::onSnapshotQuick);
    connect(m_adjustBtn, &QPushButton::toggled, this, [this](bool on) {
        if (m_adjustPanel)
            m_adjustPanel->setVisible(on);
    });
    if (m_adjustPanel) {
        connect(m_adjustPanel, &PlaybackAdjustPanel::adjustChanged, this,
                [this](const DisplayAdjust &adj) {
                    // VideoWidget 内部会从保留的原始帧重建显示帧，
                    // 暂停态拖滑杆同样实时预览；放大镜/钉图同一张 LUT。
                    const QByteArray lut = adj.buildLut();
                    m_videoWidget->setDisplayAdjust(adj);
                    if (m_magnifier)
                        m_magnifier->setDisplayAdjust(adj);
                    if (m_pinned)
                        m_pinned->setDisplayLut(lut);
                });
        // 旋转档位（Q1 方案 A）：主画面 + 放大镜 + 钉图同步随转
        connect(m_adjustPanel, &PlaybackAdjustPanel::rotationChanged, this,
                [this](int degrees) {
                    m_videoWidget->setDisplayRotation(degrees);
                    if (m_magnifier)
                        m_magnifier->setDisplayRotation(degrees);
                    if (m_pinned)
                        m_pinned->setDisplayRotation(degrees);
                    showOperationStatus(degrees == 0
                        ? lang("显示旋转已复位", "Display rotation reset")
                        : lang("显示旋转 %1°（覆盖物随转，分析坐标不变）",
                               "Display rotation %1° (overlays follow; analysis coords unchanged)")
                                   .arg(degrees));
                });
        connect(m_adjustPanel, &QDockWidget::visibilityChanged, this,
                [this](bool vis) {
                    if (m_adjustBtn) {
                        QSignalBlocker blk(m_adjustBtn);
                        m_adjustBtn->setChecked(vis);
                    }
                });
    }
    connect(m_videoWidget, &VideoWidget::frameSnapshotReady, this, [this](const QImage &img) {
        m_snapshotOverlay->setSnapshot(img);
        m_editBtn->setEnabled(true);
        m_placeBtn->setEnabled(true);
        m_snapshotFusion.imageData = img;
    });
    connect(m_editBtn, &QPushButton::clicked, this, [this]() {
        if (m_snapshotOverlay->hasSnapshot()) {
            m_snapshotOverlay->setVisible(!m_snapshotOverlay->isVisible());
        }
    });
    connect(m_placeBtn, &QPushButton::clicked, this, [this](bool checked) {
        if (!m_snapshotOverlay->hasSnapshot()) return;
        // Sync overlay's place state
        if (checked) {
            m_videoWidget->setSnapshot(m_snapshotOverlay->snapshotImage(),
                                       m_snapshotOverlay->brightness(),
                                       m_snapshotOverlay->contrastValue(),
                                       m_snapshotOverlay->opacityValue());
            if (m_magnifier) {
                m_magnifier->setSnapshotOverlay(m_snapshotOverlay->snapshotImage(),
                                                m_snapshotOverlay->brightness(),
                                                m_snapshotOverlay->contrastValue(),
                                                m_snapshotOverlay->opacityValue());
            }
        } else {
            m_videoWidget->clearSnapshot();
            if (m_magnifier) {
                m_magnifier->clearSnapshotOverlay();
                m_magnifier->update();
            }
        }
    });
    connect(m_snapshotOverlay, &SnapshotOverlay::placeToggled, this, [this](bool active) {
        m_placeBtn->setChecked(active);
        // Sync snapshot fusion data
        m_snapshotFusion.brightness = m_snapshotOverlay->brightness();
        m_snapshotFusion.contrast = m_snapshotOverlay->contrastValue();
        m_snapshotFusion.opacity = m_snapshotOverlay->opacityValue();
        if (m_snapshotOverlay->hasSnapshot()) {
            m_snapshotFusion.imageData = m_snapshotOverlay->snapshotImage();
        }
        if (active) {
            m_videoWidget->setSnapshot(m_snapshotOverlay->snapshotImage(),
                                       m_snapshotOverlay->brightness(),
                                       m_snapshotOverlay->contrastValue(),
                                       m_snapshotOverlay->opacityValue());
            if (m_magnifier) {
                m_magnifier->setSnapshotOverlay(m_snapshotOverlay->snapshotImage(),
                                                m_snapshotOverlay->brightness(),
                                                m_snapshotOverlay->contrastValue(),
                                                m_snapshotOverlay->opacityValue());
            }
        } else {
            m_videoWidget->clearSnapshot();
            if (m_magnifier) {
                m_magnifier->clearSnapshotOverlay();
            }
        }
    });
    connect(m_snapshotOverlay, &SnapshotOverlay::snapshotChanged, this, [this]() {
        // Sync snapshot fusion data (always, for VLA persistence)
        m_snapshotFusion.brightness = m_snapshotOverlay->brightness();
        m_snapshotFusion.contrast = m_snapshotOverlay->contrastValue();
        m_snapshotFusion.opacity = m_snapshotOverlay->opacityValue();
        if (m_snapshotOverlay->hasSnapshot()) {
            m_snapshotFusion.imageData = m_snapshotOverlay->snapshotImage();
        }

        // Only apply overlay when Place is active
        if (m_snapshotOverlay->isOverlayActive()) {
            m_videoWidget->setSnapshot(m_snapshotOverlay->snapshotImage(),
                                       m_snapshotOverlay->brightness(),
                                       m_snapshotOverlay->contrastValue(),
                                       m_snapshotOverlay->opacityValue());
            if (m_magnifier) {
                m_magnifier->setSnapshotOverlay(m_snapshotOverlay->snapshotImage(),
                                                m_snapshotOverlay->brightness(),
                                                m_snapshotOverlay->contrastValue(),
                                                m_snapshotOverlay->opacityValue());
            }
        } else {
            m_videoWidget->clearSnapshot();
            if (m_magnifier) {
                m_magnifier->clearSnapshotOverlay();
            }
        }
    });

    // Pinned timestamp
    connect(overlay, &OverlayWidget::pinnedRequested,
            this, [this](const QRect &videoRect) {
                m_pinnedRect = videoRect;
                if (!m_pinned) {
                    m_pinned = new PinnedWidget(this);
                    m_pinned->setAttribute(Qt::WA_DeleteOnClose);
                    connect(m_pinned, &QObject::destroyed, this, [this]() {
                        m_pinned = nullptr;
                        m_pinnedRect = QRect();
                    });
                    m_pinned->show();
                }
                // 钉图内容随主画面旋转（Q1 方案 A）+ 同一调节 LUT
                m_pinned->setDisplayRotation(m_videoWidget->displayRotation());
                if (m_adjustPanel)
                    m_pinned->setDisplayLut(m_adjustPanel->adjust().buildLut());
            });

    // v0.3: Video list panel connections
    connect(m_videoListPanel, &VideoListPanel::videoSelected,
            this, &MainWindow::onVideoSelected);
    // When the video list is cleared, unload everything: stop the engine,
    // clear the displayed frame and all analysis state, disable controls.
    connect(m_videoListPanel, &VideoListPanel::videoCountChanged,
            this, [this](int count) {
                if (count > 0)
                    return;
                m_videoEngine->unload();   // 彻底卸载：停线程+释放文件+duration 归零
                                          // （仅 stop() 时空格快捷键仍可继续播放——现场反馈）
                removeMagnifier();
                m_currentVideoPath.clear();
                m_trustedDurationMs = 0;
                m_currentDurationMs = 0;

                m_roiModel->clearRegions();
                m_roiModel->clearPolygons();
                m_guideLineModel->clearLines();
                m_timelineModel->clearData();
                if (m_spectrogramEnhanced)
                    m_spectrogramEnhanced->clear();

                m_chartPanel->setLabels({});
                m_calibration = TimeCalibration();
                m_chartPanel->setCalibration(m_calibration);
                m_chartPanel->clearAB();
                m_pinnedRect = QRect();
                m_snapshotFusion = SnapshotFusionData();
                if (m_snapshotOverlay)
                    m_snapshotOverlay->clearSnapshot();
                if (m_videoWidget) {
                    m_videoWidget->clearSnapshot();
                    m_videoWidget->clearFrame();   // 清空 m_frameImage → 回到初始空状态
                }
                // 竞态：worker 已 emit、UI 尚未处理的在途 frameReady 会在
                // clearFrame 之后到达又把帧画回去（现场反馈：第二次清空画面留存）。
                // unload 已停线程不会再产生新帧，事件循环尾部再清一次即可兜住。
                QTimer::singleShot(0, this, [this]() {
                    if (m_videoWidget && m_currentVideoPath.isEmpty())
                        m_videoWidget->clearFrame();
                });

                m_playBtn->setEnabled(false);
                m_pauseBtn->setEnabled(false);
                m_stopBtn->setEnabled(false);
                m_speedBtn->setEnabled(false);
                m_analyzeBtn->setEnabled(false);
                m_audioAnalysisBtn->setEnabled(false);
                m_setTimeBtn->setEnabled(false);
                m_captureBtn->setEnabled(false);
                if (m_snapshotBtn)
                    m_snapshotBtn->setEnabled(false);
                if (m_adjustPanel) {   // 清空列表：调节回默认，面板状态保留
                    m_adjustPanel->setValues(DisplayAdjust(), 0);
                    m_videoWidget->setDisplayAdjust(DisplayAdjust());
                    m_videoWidget->setDisplayRotation(0);
                    if (m_magnifier)
                        m_magnifier->setDisplayAdjust(DisplayAdjust());
                    if (m_pinned) {
                        m_pinned->setDisplayLut(QByteArray());
                        m_pinned->setDisplayRotation(0);
                    }
                }

                setWindowTitle(windowTitleWithCase(
                    lang("追光者 Lumen Arc v1.7.0", "Lumen Arc v1.7.0")));
                updateTimeDisplay();
                showOperationStatus(lang("已清空视频列表", "Video list cleared"));
            });
    // B5: Respond to drag-reorder changes (recalculate playback offsets, etc.)
    connect(m_videoListPanel, &VideoListPanel::videoReordered,
            this, [this]() {
                // Invalidate cached timeline offsets; next playback/analysis
                // will pick up the new order from allVideos().
            });

    // v0.3: Cancel button
    connect(m_cancelBtn, &QPushButton::clicked, this, [this]() {
        m_analysisEngine->cancelAnalysis();
    });

    // v0.35: Spectrogram X-axis bidirectional sync with ChartPanel
    // Chart zoom/pan → spectrogram follows
    connect(m_chartPanel, &ChartPanel::xAxisRangeChanged,
            m_spectrogramEnhanced, &SpectrogramPanelEnhanced::onXAxisRangeChanged);
    // Spectrogram zoom → chart follows
    connect(m_spectrogramEnhanced, &SpectrogramPanelEnhanced::xAxisRangeChanged,
            this, [this](qreal xMin, qreal xMax) {
        if (m_chartPanel && m_chartPanel->axisX()) {
            m_chartPanel->axisX()->setRange(xMin, xMax);
        }
    });

    // Spectrogram axis alignment with chart plot area
    connect(m_chartPanel, &ChartPanel::plotAreaUpdated,
            m_spectrogramEnhanced, &SpectrogramPanelEnhanced::onChartPlotAreaChanged);

    // Spectrogram cursor drag -> seek
    connect(m_spectrogramEnhanced, &SpectrogramPanelEnhanced::seekRequested,
            this, &MainWindow::onSeekFromChart);
    // 语谱拖拽松手：退出 scrub 模式 + 最终精确 seek（光标在拖拽中已两面板同步）
    connect(m_spectrogramEnhanced, &SpectrogramPanelEnhanced::scrubEnded, this, [this]() {
        m_videoEngine->setScrubMode(false);
        m_videoEngine->seek(m_chartPanel->cursorTime());
    });

    // v0.5: 多边形ROI和辅助线模式切换
    connect(m_rectModeBtn, &QPushButton::clicked, this, &MainWindow::onRectMode);
    connect(m_polygonModeBtn, &QPushButton::clicked, this, &MainWindow::onPolygonMode);
    connect(m_guideLineBtn, &QPushButton::clicked, this, &MainWindow::onGuideLineMode);

    // v0.5: 复制粘贴ROI
    connect(m_copyRoiBtn, &QPushButton::clicked, this, &MainWindow::onCopyRoi);
    connect(m_pasteRoiBtn, &QPushButton::clicked, this, &MainWindow::onPasteRoi);

    // v0.5: 模式变化信号
    connect(m_videoWidget->overlay(), &OverlayWidget::modeChanged, this, [this](const QString &mode) {
        showOperationStatus(mode);
    });

    // v0.5: 更新粘贴按钮状态
    connect(m_roiModel, &RoiModel::regionsChanged, this, [this]() {
        m_pasteRoiBtn->setEnabled(!m_roiClipboard.isEmpty() || !m_polygonClipboard.isEmpty());
    });
    connect(m_roiModel, &RoiModel::polygonsChanged, this, [this]() {
        m_pasteRoiBtn->setEnabled(!m_roiClipboard.isEmpty() || !m_polygonClipboard.isEmpty());
    });
}

/**
 * @brief 5级Python检测：内嵌→环境变量→注册表→常见路径→py.exe
 */
QString MainWindow::detectPythonPath() const
{
    // Sunk into the engine (R2/R4, preprocess design §3.4); keep this wrapper
    // to avoid touching legacy call sites.
    return PythonAnalysisEngine::detectPythonPath();
}

qint64 MainWindow::trustedDurationFor(const QString &path) const
{
    // R2: engine-neutral interface, no downcast (preprocess design §3.4)
    return m_analysisEngine ? m_analysisEngine->trustedDurationMs(path) : 0;
}

void MainWindow::onOpenFile()
{
    // Ctrl+O：案件打开时自动入案（拍板§8-5）
    openVideosInteractive(true);
}

void MainWindow::onOpenFileTemporary()
{
    // 「临时打开(不入案)」：跳过入案登记，其余流程一致
    openVideosInteractive(false);
}

void MainWindow::openVideosInteractive(bool admitToCase)
{
    QStringList filePaths = QFileDialog::getOpenFileNames(this,
        lang("打开视频", "Open Video Files"),
        QString(),
        lang("视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;所有文件 (*)",
             "Video Files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;All Files (*)"));

    if (filePaths.isEmpty())
        return;

    // v0.3: Add all files to video list, open first one for playback
    for (int i = 0; i < filePaths.size(); ++i) {
        const QString &path = filePaths[i];
        // Get video info (fps + total frames -> duration) via interface (R2)
        IAnalysisEngine::VideoTiming timing;
        if (m_analysisEngine)
            timing = m_analysisEngine->videoTiming(path);
        float fps = timing.fps;
        qint64 durationMs = timing.durationMs;
        if (fps <= 0) fps = 30.0f;
        m_videoListPanel->addVideo(path, durationMs, fps);

        // v1.3.0 M2 任务7：先于 openVideoFile 入案，使源旁 .vla 导入后
        // 缓存探测直接命中案件 .vla（入案视频不弹询问）
        if (admitToCase)
            admitVideoToCase(path, true);

        if (i == 0) {
            openVideoFile(path);
            // 引擎回退：getVideoInfo 失败时改用引擎时长
            if (durationMs <= 0) {
                qint64 engineDur = m_videoEngine->duration();
                if (engineDur > 0) {
                    m_videoListPanel->updateDuration(path, engineDur);
                }
            }
        }
    }
}

/// @brief 打开视频文件：加载/缓存检测/按钮启用
/// @brief 视频入案登记（v1.3.0 M2 任务7）
void MainWindow::admitVideoToCase(const QString &path, bool interactive)
{
    if (!m_caseManager || !m_caseManager->isOpen() || path.isEmpty())
        return;
    if (path.endsWith(".vla", Qt::CaseInsensitive))
        return;   // .vla 分析文件不是视频，不入案
    if (m_caseManager->isCaseVideo(path))
        return;   // 已在案：照常打开即可，不重复登记

    QString err;
    const QString id = m_caseManager->addVideo(path, &err);
    if (id.isEmpty()) {
        // 重复路径拒绝（拍板§8-5）：非模态提示，不打断打开流程
        if (!err.isEmpty())
            showOperationStatus(err);
        return;
    }
    showOperationStatus(lang("已入案登记：%1", "Registered into case: %1").arg(id));

    // 同内容仅提示（拍板§8-5）：与案内他路大小完全一致 → 可能同一来源
    if (const auto *self = m_caseManager->videoById(id)) {
        for (const auto &v : m_caseManager->meta().videos) {
            if (v.id != id && v.sizeBytes > 0 && v.sizeBytes == self->sizeBytes) {
                showOperationStatus(
                    lang("提示：%1 与 %2 文件大小相同，请确认是否同一来源",
                         "Note: %1 and %2 have identical size; same source?")
                        .arg(id, v.id));
                break;
            }
        }
    }

    // 源旁已有 .vla 询问导入（默认是，复制；拍板§8-5）
    const QString sideVla = path + QStringLiteral(".vla");
    const QString caseVla = m_caseManager->vlaPathFor(path);
    if (QFile::exists(sideVla) && !QFile::exists(caseVla)) {
        bool importIt = true;
        if (interactive) {
            const auto reply = QMessageBox::question(this,
                lang("导入已有分析结果", "Import Existing Analysis"),
                lang("源视频旁存在已保存的分析结果：\n%1\n\n是否复制导入案件（%2）？",
                     "A saved analysis exists beside the source:\n%1\n\n"
                     "Copy it into the case (%2)?").arg(sideVla, id),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            importIt = (reply == QMessageBox::Yes);
        }
        if (importIt) {
            QDir().mkpath(QFileInfo(caseVla).absolutePath());
            if (QFile::copy(sideVla, caseVla))
                showOperationStatus(
                    lang("已导入分析结果到 %1", "Imported analysis into %1").arg(id));
            else
                showOperationStatus(
                    lang("分析结果复制失败", "Failed to copy the analysis file"));
        }
    }

    // 登记即落盘（取证：崩溃不丢登记；case.json 体量小，保存开销可忽略）
    QString saveErr;
    if (!m_caseManager->saveCase(&saveErr))
        showOperationStatus(
            lang("案件保存失败：%1", "Failed to save case: %1").arg(saveErr));
}

// ---------------------------------------------------------------------------
// 案件模式（v1.3.0 M2 任务10）
// ---------------------------------------------------------------------------
QString MainWindow::windowTitleWithCase(const QString &base) const
{
    QString t = base;
    if (m_caseManager && m_caseManager->isOpen())
        t += QStringLiteral(" - 《")
            + m_caseManager->meta().caseNo + QStringLiteral("-")
            + m_caseManager->meta().title + QStringLiteral("》");
    return t + buildStamp();   // 构建时间戳常驻标题栏
}

void MainWindow::enterCaseMode()
{
    // CaseDock 替代视频列表（仅案件模式，拍板§8-6）
    m_videoListPanel->setVisible(false);
    m_videoListPlaceholder->setVisible(false);
    m_caseDock->setVisible(true);
    m_caseDock->refreshTree();
    resizeDocks({m_caseDock}, {250}, Qt::Horizontal);
    m_caseStatusBtn->setText(
        QStringLiteral("📁 ") + m_caseManager->meta().caseNo);
    m_caseStatusBtn->setVisible(true);
    if (m_closeCaseAction)
        m_closeCaseAction->setEnabled(true);
    if (m_casePropsAction)
        m_casePropsAction->setEnabled(true);
    if (m_exportCaseAction)
        m_exportCaseAction->setEnabled(true);
    if (m_batchRelocateAction)
        m_batchRelocateAction->setEnabled(true);
    setWindowTitle(windowTitleWithCase(
        lang("追光者 Lumen Arc v1.7.0", "Lumen Arc v1.7.0")));
    showOperationStatus(lang("案件已打开：%1", "Case opened: %1")
                            .arg(m_caseManager->meta().caseNo));
    // 开案批量校时徽标校验（用户实测：旧 vla time_offset=0 误亮 ⏰ 且只在
    // 打开视频时才刷新）——轻量读每个标了校准的视频的 vla 校时字段，
    // 修正 case.json 里的历史误值。同步但仅限 hasCalibration 的视频
    // （通常很少），单个 peek 只读 META/顶层字段，毫秒级。
    {
        const auto videos = m_caseManager->meta().videos;
        int fixed = 0;
        for (const auto &v : videos) {
            if (!v.hasCalibration)
                continue;
            const QString vlaPath = m_caseManager->vlaPathFor(v.originalPath);
            const TimeCalibration peek = TimelineModel::peekCalibrationFromVla(vlaPath);
            if (!peek.isEffective()) {
                m_caseManager->updateCalibrationBadge(v.originalPath, false, QString());
                ++fixed;
            }
        }
        if (fixed > 0) {
            m_caseDock->refreshTree();
            showOperationStatus(lang("已修正 %1 个视频的校时徽标（旧数据误标）",
                                     "Fixed %1 calibration badges (stale)")
                                    .arg(fixed));
        }
    }
    // 开案恢复现场（uiState.lastVideoId）：.vla 缓存探测自动加载分析数据
    if (const auto *v = m_caseManager->videoById(m_caseManager->meta().lastVideoId)) {
        if (QFile::exists(v->originalPath))
            openVideoFile(v->originalPath);
    }
}

void MainWindow::exitCaseMode()
{
    m_caseDock->setVisible(false);
    m_videoListPanel->setVisible(true);
    m_videoListContent->setVisible(true);
    resizeDocks({m_videoListPanel}, {250}, Qt::Horizontal);
    m_caseStatusBtn->setVisible(false);
    if (m_closeCaseAction)
        m_closeCaseAction->setEnabled(false);
    if (m_casePropsAction)
        m_casePropsAction->setEnabled(false);
    if (m_exportCaseAction)
        m_exportCaseAction->setEnabled(false);
    if (m_batchRelocateAction)
        m_batchRelocateAction->setEnabled(false);
    setWindowTitle(lang("追光者 Lumen Arc v1.7.0", "Lumen Arc v1.7.0"));
    showOperationStatus(lang("案件已关闭", "Case closed"));
}

void MainWindow::closeCaseWithPrompt()
{
    if (!m_caseManager || !m_caseManager->isOpen())
        return;
    // 记录现场后关案（不中断播放，拍板§8-6）
    if (const auto *v = m_caseManager->videoByPath(m_currentVideoPath))
        m_caseManager->setLastVideoId(v->id);
    if (m_caseManager->isDirty()) {
        const auto reply = QMessageBox::question(this,
            lang("关闭案件", "Close Case"),
            lang("案件有未保存的变更。关闭前是否保存？",
                 "The case has unsaved changes. Save before closing?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (reply == QMessageBox::Cancel)
            return;
        if (reply == QMessageBox::Save) {
            QString err;
            if (!m_caseManager->saveCase(&err)) {
                QMessageBox::critical(this, lang("错误", "Error"),
                    lang("案件保存失败：%1", "Failed to save case: %1").arg(err));
                return;
            }
        }
    }
    m_caseManager->closeCase();   // caseClosed → exitCaseMode
}

void MainWindow::closeEvent(QCloseEvent *event)
{
    // 退出前案件 dirty 检查（取证：未保存变更须明示；取消则中止退出）
    if (m_caseManager && m_caseManager->isOpen() && m_caseManager->isDirty()) {
        if (const auto *v = m_caseManager->videoByPath(m_currentVideoPath))
            m_caseManager->setLastVideoId(v->id);
        const auto reply = QMessageBox::question(this,
            lang("退出", "Exit"),
            lang("案件有未保存的变更。退出前是否保存？",
                 "The case has unsaved changes. Save before exiting?"),
            QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel,
            QMessageBox::Save);
        if (reply == QMessageBox::Cancel) {
            event->ignore();
            return;
        }
        if (reply == QMessageBox::Save) {
            QString err;
            if (!m_caseManager->saveCase(&err)) {
                QMessageBox::critical(this, lang("错误", "Error"),
                    lang("案件保存失败：%1", "Failed to save case: %1").arg(err));
                event->ignore();
                return;
            }
        }
    }
    QMainWindow::closeEvent(event);
}

// ---------------------------------------------------------------------------
// 案件对话框与起始页（v1.3.0 M2 任务11）
// ---------------------------------------------------------------------------
void MainWindow::onNewCase()
{
    if (m_caseManager->isOpen()) {
        closeCaseWithPrompt();
        if (m_caseManager->isOpen())
            return;   // 用户取消，维持现案
    }
    QDir().mkpath(CaseManager::caseRootDir());
    NewCaseDialog dlg(CaseManager::caseRootDir(), this);
    if (dlg.exec() != QDialog::Accepted)
        return;
    QString err;
    if (!m_caseManager->createCase(CaseManager::caseRootDir(), dlg.meta(), &err))
        QMessageBox::critical(this, lang("新建案件失败", "New Case Failed"), err);
    // 成功：caseOpened 信号 → enterCaseMode 自动
}

void MainWindow::onOpenCase()
{
    // 页面内居中面板（Blender 式，2026-08 人工反馈：不弹窗打开案件）
    if (!m_caseOpenPanel)
        return;
    centerCaseOpenPanel();
    m_caseOpenPanel->refresh();
    m_caseOpenPanel->show();
    m_caseOpenPanel->raise();
    m_caseOpenPanel->activateWindow();
}

void MainWindow::onOpenCaseBrowse()
{
    const QString dir = QFileDialog::getExistingDirectory(this,
        lang("打开案件（选择案件目录）", "Open Case (choose case folder)"),
        CaseManager::caseRootDir());
    if (!dir.isEmpty()) {
        m_caseOpenPanel->hide();
        openCaseFlow(dir);
    }
}

void MainWindow::centerCaseOpenPanel()
{
    if (!m_caseOpenPanel || !centralWidget())
        return;
    // 以内容区为基准居中（dock/工具栏之外）
    const QRect r = centralWidget()->rect();
    m_caseOpenPanel->move(r.center() - m_caseOpenPanel->rect().center());
}

void MainWindow::resizeEvent(QResizeEvent *event)
{
    QMainWindow::resizeEvent(event);
    if (m_caseOpenPanel && m_caseOpenPanel->isVisible())
        centerCaseOpenPanel();
}

void MainWindow::openCaseFlow(const QString &dir)
{
    if (dir.isEmpty())
        return;
    if (m_caseManager->isOpen()) {
        closeCaseWithPrompt();
        if (m_caseManager->isOpen())
            return;   // 用户取消
    }
    QString err;
    QStringList warnings;
    bool lockConflict = false;
    bool opened = m_caseManager->openCase(dir, &err, &warnings,
                                          &lockConflict, false);
    if (!opened && lockConflict) {
        // 残留锁已自动清理；到此 = 真双开冲突（持有者进程仍在）
        const auto reply = QMessageBox::warning(this,
            lang("案件被锁定", "Case Locked"),
            lang("案件正在另一个实例中打开。\n\n%1\n\n"
                 "仍要强制打开吗？（建议先关闭另一实例）",
                 "The case is open in another instance.\n\n%1\n\n"
                 "Force open anyway? (close the other instance first)")
                .arg(err),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return;
        opened = m_caseManager->openCase(dir, &err, &warnings,
                                         nullptr, true);
    }
    if (!opened) {
        QMessageBox::critical(this, lang("打开案件失败", "Open Case Failed"),
                              err);
        return;
    }
    if (m_caseOpenPanel)
        m_caseOpenPanel->hide();
    if (!warnings.isEmpty())
        showOperationStatus(warnings.join(QStringLiteral("；")));
    // caseOpened 信号 → enterCaseMode 自动
}

void MainWindow::onCaseProperties()
{
    if (!m_caseManager->isOpen())
        return;
    CasePropertiesDialog dlg(m_caseManager, this);
    dlg.exec();
    // 名称可能已改：刷新标题/面板/状态栏
    if (m_caseManager->isOpen()) {
        setWindowTitle(windowTitleWithCase(
            lang("追光者 Lumen Arc v1.7.0", "Lumen Arc v1.7.0")));
        m_caseDock->refreshTree();
        m_caseStatusBtn->setText(
            QStringLiteral("📁 ") + m_caseManager->meta().caseNo);
    }
}

void MainWindow::onCaseRootDir()
{
    const QString dir = QFileDialog::getExistingDirectory(this,
        lang("选择案件根目录（新建案件的默认存放位置）",
             "Choose case root folder (default location for new cases)"),
        CaseManager::caseRootDir());
    if (dir.isEmpty())
        return;
    CaseManager::setCaseRootDir(dir);
    showOperationStatus(lang("案件根目录：%1", "Case root: %1").arg(dir));
}

void MainWindow::onShowStartPage()
{
    // 页面内居中欢迎面板（Blender 式，非模态）：主界面已打开后才出现
    if (!m_caseOpenPanel)
        return;
    centerCaseOpenPanel();
    m_caseOpenPanel->refresh();
    m_caseOpenPanel->show();
    m_caseOpenPanel->raise();
    m_caseOpenPanel->activateWindow();
}

void MainWindow::onExportCase()
{
    if (!m_caseManager->isOpen())
        return;
    ExportCaseDialog dlg(m_caseManager, this);
    dlg.exec();
}

void MainWindow::onBatchRelocate()
{
    if (!m_caseManager->isOpen())
        return;
    BatchRelocateDialog dlg(m_caseManager, this);
    dlg.exec();
}

void MainWindow::onMultiCamView()
{
    if (!m_caseManager->isOpen())
        return;
    MultiCamDialog dlg(m_caseManager, this);
    // 双击块打开该路（只读视图，打开不改变任何数据）
    connect(&dlg, &MultiCamDialog::openVideoRequested, this,
            [this](const QString &id) {
                if (const auto *v = m_caseManager->videoById(id))
                    openVideoFile(m_caseManager->effectivePathFor(*v));
            });
    dlg.exec();
}

void MainWindow::openVideoFile(const QString &filePath)
{
    if (filePath.isEmpty())
        return;

    if (filePath.endsWith(".dav", Qt::CaseInsensitive)) {
        QMessageBox::warning(this, lang("不支持的格式", "Unsupported Format"),
            lang("DAV 格式不受本工具支持。\n"
                 "请将视频转换为 MP4、AVI 或 MKV 格式。",
                 "DAV format is not supported by this tool.\n"
                 "Please convert the video to MP4, AVI, or MKV format."));
        return;
    }

    // If it's a .vla analysis result file, load it directly
    if (filePath.endsWith(".vla", Qt::CaseInsensitive)) {
    QVector<QRect> regions;
    QVector<QPolygon> loadedPolygons;
    QVector<GuideLine> loadedGuideLines;
    QVector<int> loadedRegionRoiIds;
    QVector<int> loadedPolygonRoiIds;
    TimeCalibration calibration;
    QRect magnifierRect;
    QVector<ChartLabel> labels;
    QRect pinnedRect;
    SnapshotFusionData snapshotFusion;
    if (m_timelineModel->loadFromFile(filePath, &regions, &calibration,
                                       &magnifierRect, &labels, &pinnedRect,
                                       &snapshotFusion, &loadedPolygons, &loadedGuideLines,
                                       &loadedRegionRoiIds, &loadedPolygonRoiIds)) {
        restoreAnalysisState(regions, calibration, labels, pinnedRect, snapshotFusion, loadedRegionRoiIds);
        if (loadedPolygonRoiIds.size() == loadedPolygons.size())
            m_roiModel->restorePolygons(loadedPolygons, loadedPolygonRoiIds);
        else {
            m_roiModel->clearPolygons();
            for (const QPolygon &poly : loadedPolygons)
                m_roiModel->addPolygon(poly);
        }
        m_guideLineModel->clearLines();
        for (const GuideLine &line : loadedGuideLines)
            m_guideLineModel->addLine(line);

        // Do NOT overwrite m_currentVideoPath with the .vla path: it is an
        // analysis file, not a playable video, and it keys VideoStateManager.
        setWindowTitle(windowTitleWithCase("Lumen Arc v1.7.0 - [Loaded: " +
                           QFileInfo(filePath).fileName() + "]"));
        } else {
            QMessageBox::critical(this, lang("错误", "Error"),
                lang("加载分析结果文件失败：\n",
                     "Failed to load analysis result file:\n") + filePath);
        }
        return;
    }

    // Save current video state before switching
    if (!m_currentVideoPath.isEmpty() && m_stateManager) {
        QRect magRect = m_magnifier ? m_magnifier->currentSourceRect() : QRect();
        m_stateManager->saveState(
            m_currentVideoPath,
            m_timelineModel->snapshot(),
            m_roiModel->regions(),
            m_calibration,
            magRect,
            m_chartPanel->labels(),
            m_pinnedRect,
            m_snapshotFusion,
            m_chartPanel->abPointA(),
            m_chartPanel->abPointB(),
            m_chartPanel->isABLoop(),
            m_roiModel->polygons(),
            m_guideLineModel->lines(),
            m_chartPanel->chartGuideLinesData(),
            m_roiModel->roiIds(),
            m_roiModel->polygonRoiIds(),
            m_adjustPanel ? m_adjustPanel->adjust() : DisplayAdjust(),
            m_adjustPanel ? m_adjustPanel->rotation() : 0
        );
    }

    removeMagnifier();
    m_currentVideoPath = filePath;
    m_trustedDurationMs = trustedDurationFor(filePath);
    m_currentDurationMs = 0;  // 等待 durationChanged 校准
    // 案件现场跟踪（v1.3.0 M2：开案恢复 lastVideoId 的数据源）
    if (const auto *cv = m_caseManager->videoByPath(filePath))
        m_caseManager->setLastVideoId(cv->id);

    if (m_videoEngine->load(filePath)) {
        // 同步源视频原生分辨率（时间戳框选归一化基准，v1.2.1）
        m_videoWidget->overlay()->setVideoSize(
            m_videoEngine->videoWidth(), m_videoEngine->videoHeight());
        // 大视频加载需要数秒：主窗口显示“导入中…”（首帧到达自动清除），
        // 避免用户误以为程序无响应
        m_videoWidget->setLoading(true);
        QObject::connect(m_videoEngine, &IVideoEngine::frameReady, this,
                         [this](const QImage &) { m_videoWidget->setLoading(false); },
                         Qt::SingleShotConnection);
        // 加入视频列表（“在主窗口播放输出”路径此前漏加；hasVideo 去重避免
        // 每次切换视频重复跑 videoTiming）
        if (m_videoListPanel && !m_videoListPanel->hasVideo(filePath)) {
            IAnalysisEngine::VideoTiming timing;
            if (m_analysisEngine)
                timing = m_analysisEngine->videoTiming(filePath);
            float fps = timing.fps > 0 ? timing.fps : m_videoEngine->fps();
            qint64 dur = timing.durationMs;
            if (dur <= 0)
                dur = m_videoEngine->duration();
            m_videoListPanel->addVideo(filePath, dur, fps);
        }
        // Check if we have a saved state for this video (memory state takes priority)
        VideoState savedState;
        if (m_stateManager->restoreState(filePath, savedState)) {
            // 带 roiId 恢复：保持与分析数据 dataEntries 的 roi_id 对齐
            if (savedState.regionRoiIds.size() == savedState.regions.size())
                m_roiModel->restoreRegions(savedState.regions, savedState.regionRoiIds);
            else {
                m_roiModel->clearRegions();
                for (const QRect &rc : savedState.regions)
                    m_roiModel->addRegion(rc);
            }

            if (savedState.polygonRoiIds.size() == savedState.polygons.size())
                m_roiModel->restorePolygons(savedState.polygons, savedState.polygonRoiIds);
            else {
                m_roiModel->clearPolygons();
                for (const QPolygon &poly : savedState.polygons)
                    m_roiModel->addPolygon(poly);
            }

            m_guideLineModel->clearLines();
            for (const GuideLine &line : savedState.guideLines)
                m_guideLineModel->addLine(line);

            m_timelineModel->setData(
                QVector<qint64>(savedState.snapshot.timestamps),
                QVector<QVector<qreal>>(savedState.snapshot.values),
                QVector<DataEntry>(savedState.snapshot.dataEntries),
                savedState.snapshot.audio
            );

            m_calibration = savedState.calibration;
            m_chartPanel->setCalibration(m_calibration);
            // 校时徽标以 .vla 为 SSOT：空校时模型（旧 v7 迁移 offset=0）同步
            // 熄灭案件里误亮的 ⏰（用户实测反馈）
            m_caseManager->updateCalibrationBadge(
                m_currentVideoPath, m_calibration.isEffective(),
                calibrationBadgeSummary());
            m_chartPanel->setLabels(savedState.labels);
            m_chartPanel->setChartGuideLinesData(savedState.chartGuideLines);

            // Restore A/B region
            if (savedState.abPointA >= 0) m_chartPanel->setPointA(savedState.abPointA);
            if (savedState.abPointB >= 0) m_chartPanel->setPointB(savedState.abPointB);
            if (savedState.abLoop) m_chartPanel->setABLoop(true);

            if (!savedState.pinnedRect.isEmpty())
                m_pinnedRect = savedState.pinnedRect;

            m_snapshotFusion = savedState.snapshotFusion;
            if (savedState.snapshotFusion.isValid() && !savedState.snapshotFusion.imageData.isNull()) {
                m_snapshotOverlay->setSnapshot(savedState.snapshotFusion.imageData);
                m_snapshotOverlay->setParameters(
                    savedState.snapshotFusion.brightness,
                    savedState.snapshotFusion.contrast,
                    savedState.snapshotFusion.opacity);
                m_editBtn->setEnabled(true);
                m_placeBtn->setEnabled(true);
            }

            // 恢复播放画面调节（逐视频记忆：亮度/对比度/伽马/色阶/反色/旋转）
            if (m_adjustPanel) {
                m_adjustPanel->setValues(savedState.display,
                                         savedState.displayRotation);
                const QByteArray lut = savedState.display.buildLut();
                m_videoWidget->setDisplayAdjust(savedState.display);
                m_videoWidget->setDisplayRotation(savedState.displayRotation);
                if (m_magnifier) {
                    m_magnifier->setDisplayAdjust(savedState.display);
                    m_magnifier->setDisplayRotation(savedState.displayRotation);
                }
                if (m_pinned) {
                    m_pinned->setDisplayLut(lut);
                    m_pinned->setDisplayRotation(savedState.displayRotation);
                }
            }

            if (m_spectrogramEnhanced && savedState.snapshot.hasAudio())
                m_spectrogramEnhanced->setSpectrogramData(savedState.snapshot.audio);

            m_playBtn->setEnabled(true);
            m_pauseBtn->setEnabled(true);
            m_stopBtn->setEnabled(true);
            m_speedBtn->setEnabled(true);
            m_analyzeBtn->setEnabled(true);
            m_audioAnalysisBtn->setEnabled(true);
            m_setTimeBtn->setEnabled(true);
            m_captureBtn->setEnabled(true);
            if (m_snapshotBtn)
                m_snapshotBtn->setEnabled(true);

            onPlay();
            return;
        }

        // No saved state, clear data and check for .vla cache
        m_roiModel->clearRegions();
        m_roiModel->clearPolygons();
        m_guideLineModel->clearLines();
        m_timelineModel->clearData();
        if (m_spectrogramEnhanced)
            m_spectrogramEnhanced->clear();

        // Also reset chart-level state so labels/time axis/A-B/pinned/fusion
        // from the previous video do not leak into this one.
        m_chartPanel->setLabels({});
        m_chartPanel->clearChartGuideLines();
        m_calibration = TimeCalibration();
        m_chartPanel->setCalibration(m_calibration);
        m_chartPanel->clearAB();
        m_pinnedRect = QRect();
        m_snapshotFusion = SnapshotFusionData();
        // 无状态视频：画面调节回默认（防跨视频泄漏）
        if (m_adjustPanel) {
            m_adjustPanel->setValues(DisplayAdjust(), 0);
            m_videoWidget->setDisplayAdjust(DisplayAdjust());
            m_videoWidget->setDisplayRotation(0);
            if (m_magnifier)
                m_magnifier->setDisplayAdjust(DisplayAdjust());
            if (m_pinned)
                m_pinned->setDisplayLut(QByteArray());
        }
        if (m_snapshotOverlay)
            m_snapshotOverlay->clearSnapshot();
        if (m_videoWidget)
            m_videoWidget->clearSnapshot();

        m_playBtn->setEnabled(true);
        m_pauseBtn->setEnabled(true);
        m_stopBtn->setEnabled(true);
        m_speedBtn->setEnabled(true);
        m_analyzeBtn->setEnabled(true);
        m_audioAnalysisBtn->setEnabled(true);  // v0.3
        m_setTimeBtn->setEnabled(true);
        m_captureBtn->setEnabled(true);
        if (m_snapshotBtn)
            m_snapshotBtn->setEnabled(true);

        // Check for cached .vla file alongside the video
        // v1.3.0 路径分流：入案视频缓存 = 案件 videos/V###.vla；未入案照旧源旁
        const QString vlaPath = m_caseManager->vlaPathFor(filePath);
        if (QFile::exists(vlaPath)) {
            // 入案视频：案件内 .vla 即权威缓存，直接加载不弹询问（拍板§3-6）
            bool loadCache = m_caseManager->isCaseVideo(filePath);
            if (!loadCache) {
            auto reply = QMessageBox::question(this,
                lang("找到缓存的分析结果", "Cached Analysis Found"),
                lang("找到此视频已保存的分析结果。\n"
                     "是否直接加载而无需重新分析？\n\n",
                     "A saved analysis result was found for this video.\n"
                     "Would you like to load it instead of re-analyzing?\n\n") + vlaPath,
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            loadCache = (reply == QMessageBox::Yes);
            }
            if (loadCache) {
                QVector<QRect> regions;
                QVector<QPolygon> loadedPolygons;
                QVector<GuideLine> loadedGuideLines;
                QVector<int> loadedRegionRoiIds;
                QVector<int> loadedPolygonRoiIds;
                TimeCalibration calibration;
                QRect magnifierRect;
                QVector<ChartLabel> labels;
                QRect pinnedRect;
                SnapshotFusionData snapshotFusion;
                if (m_timelineModel->loadFromFile(vlaPath, &regions, &calibration,
                                                    &magnifierRect, &labels, &pinnedRect,
                                                    &snapshotFusion, &loadedPolygons, &loadedGuideLines,
                                                    &loadedRegionRoiIds, &loadedPolygonRoiIds)) {
                    restoreAnalysisState(regions, calibration, labels, pinnedRect, snapshotFusion, loadedRegionRoiIds);
                    if (loadedPolygonRoiIds.size() == loadedPolygons.size())
                        m_roiModel->restorePolygons(loadedPolygons, loadedPolygonRoiIds);
                    else {
                        m_roiModel->clearPolygons();
                        for (const QPolygon &poly : loadedPolygons)
                            m_roiModel->addPolygon(poly);
                    }
                    m_guideLineModel->clearLines();
                    for (const GuideLine &line : loadedGuideLines)
                        m_guideLineModel->addLine(line);
                }
            }
        }

        // v1.2.0 sidecar 继承：拼接输出的校时自动带入（仅无现有校时时，Q-4）
        if (!m_calibration.isValid()) {
            TimeCalibration inherited;
            QString sidecarWarning;
            if (CalibrationService::loadSidecar(filePath, &inherited,
                                                &sidecarWarning)
                && inherited.isValid()) {
                m_calibration = inherited;
                m_chartPanel->setCalibration(m_calibration);
                // v1.7.1 修复：继承的校时必须落盘 .vla 并刷新案件 ⏰ 徽标
                // （用户实测：前处理产物继承校时后切换/重开丢失、无徽标——
                // 旧流程只在内存，产物无分析数据时从未写 vla）
                saveCurrentVlaAsync();
                if (sidecarWarning.isEmpty()) {
                    showOperationStatus(
                        lang("已继承前处理校时",
                             "Inherited calibration from preprocessing"));
                } else {
                    QMessageBox::warning(this,
                        lang("拼接时间缺口提示", "Time Gap Notice"),
                        lang("已继承前处理校时。注意：此拼接文件段间存在时间缺口/重叠，\n"
                             "首段之后的墙钟可能不准（将写入报告）。",
                             "Calibration inherited. Note: this concatenated file has "
                             "time gaps/overlaps;\nwall clock after the first segment "
                             "may drift (will be noted in reports)."));
                }
            }
        }

        onPlay();
    } else {
        QMessageBox::critical(this, lang("错误", "Error"),
            lang("打开视频文件失败：\n", "Failed to open video file:\n") + filePath);
    }
}

void MainWindow::onSaveAnalysis()
{
    AnalysisSnapshot snapshot = m_timelineModel->snapshot();
    if (snapshot.isEmpty()) {
        QMessageBox::information(this, lang("保存", "Save"),
            lang("没有分析数据可保存。", "No analysis data to save."));
        return;
    }

    // v1.3.0 路径分流：入案视频默认存案件 videos/V###.vla（仍可另选路径）；
    // 直接加载 .vla 的场景默认覆写原文件（v1.2.2 行为保持）
    QString defaultPath = m_currentVideoPath;
    if (defaultPath.isEmpty())
        defaultPath = "analysis_result.vla";
    else if (!defaultPath.endsWith(".vla", Qt::CaseInsensitive))
        defaultPath = m_caseManager->vlaPathFor(defaultPath);

    QString filePath = QFileDialog::getSaveFileName(this,
        lang("保存分析结果", "Save Analysis Result"), defaultPath,
        lang("VLA 文件 (*.vla)", "VLA Files (*.vla)"));
    if (filePath.isEmpty())
        return;

    QRect magnifierRect = m_magnifier ? m_magnifier->currentSourceRect() : QRect();
    if (m_timelineModel->saveToFile(filePath, m_roiModel->regions(),
                                     m_calibration,
                                     magnifierRect,
                                     m_chartPanel->labels(),
                                     m_pinnedRect,
                                     m_snapshotFusion,
                                     m_roiModel->polygons(),
                                     m_guideLineModel->lines(),
                                     m_roiModel->roiIds(),
                                     m_roiModel->polygonRoiIds())) {
        // VLA2：频谱已内嵌于文件中，无需 .spec 伴随文件
        // 存入案件管理路径时同步刷新校时徽标缓存（.vla 为 SSOT）
        if (!m_currentVideoPath.isEmpty()
            && QFileInfo(filePath).absoluteFilePath()
                   == QFileInfo(m_caseManager->vlaPathFor(m_currentVideoPath)).absoluteFilePath()) {
            m_caseManager->updateCalibrationBadge(
                m_currentVideoPath, m_calibration.isEffective(),
                calibrationBadgeSummary());
        }
        QMessageBox::information(this, lang("保存", "Save"),
            lang("分析结果保存成功。", "Analysis result saved successfully."));
        showOperationStatus(lang("保存成功", "Saved"));
    } else {
        QMessageBox::critical(this, lang("错误", "Error"),
            lang("保存分析结果失败。", "Failed to save analysis result."));
    }
}

void MainWindow::onLoadAnalysis()
{
    // v1.3.0 路径分流：入案视频从案件 videos/ 目录起始浏览
    const QString startDir = m_currentVideoPath.isEmpty()
        ? QString()
        : QFileInfo(m_caseManager->vlaPathFor(m_currentVideoPath)).absolutePath();
    QString filePath = QFileDialog::getOpenFileName(this,
        "Load Analysis Result", startDir,
        "VLA Files (*.vla);;All Files (*)");
    if (filePath.isEmpty())
        return;

        QVector<QRect> regions;
        QVector<QPolygon> loadedPolygons;
        QVector<GuideLine> loadedGuideLines;
        QVector<int> loadedRegionRoiIds;
        QVector<int> loadedPolygonRoiIds;
        TimeCalibration calibration;
        QRect magnifierRect;
        QVector<ChartLabel> labels;
        QRect pinnedRect;
        SnapshotFusionData snapshotFusion;
        if (m_timelineModel->loadFromFile(filePath, &regions, &calibration,
                                            &magnifierRect, &labels, &pinnedRect,
                                            &snapshotFusion, &loadedPolygons, &loadedGuideLines,
                                            &loadedRegionRoiIds, &loadedPolygonRoiIds)) {
            restoreAnalysisState(regions, calibration, labels, pinnedRect, snapshotFusion, loadedRegionRoiIds);
            if (loadedPolygonRoiIds.size() == loadedPolygons.size())
                m_roiModel->restorePolygons(loadedPolygons, loadedPolygonRoiIds);
            else {
                m_roiModel->clearPolygons();
                for (const QPolygon &poly : loadedPolygons)
                    m_roiModel->addPolygon(poly);
            }
            m_guideLineModel->clearLines();
            for (const GuideLine &line : loadedGuideLines)
                m_guideLineModel->addLine(line);

            // Do NOT overwrite m_currentVideoPath with the .vla path (see openVideoFile).
        setWindowTitle(windowTitleWithCase("Lumen Arc v1.7.0 - [Loaded: " +
                       QFileInfo(filePath).fileName() + "]"));
        QMessageBox::information(this, lang("已加载", "Loaded"),
            lang("分析结果加载成功。", "Analysis result loaded successfully."));
        showOperationStatus(lang("加载成功", "Loaded"));
    } else {
        QMessageBox::critical(this, lang("错误", "Error"),
            lang("加载分析结果文件失败：\n",
                 "Failed to load analysis result file:\n") + filePath);
    }
}

/// @brief 从外部文件加载图片作为叠加层
void MainWindow::onLoadOverlayImage()
{
    QString filePath = QFileDialog::getOpenFileName(this,
        lang("加载图片为叠加", "Load Image as Overlay"), QString(),
        "Images (*.png *.jpg *.jpeg *.bmp *.tiff *.tif);;All Files (*)");
    if (filePath.isEmpty())
        return;

    QImage img(filePath);
    if (img.isNull()) {
        QMessageBox::warning(this,
            lang("加载失败", "Load Failed"),
            lang("无法加载该图片文件。\n请确认文件格式是否支持。",
                 "Failed to load the image file.\nPlease check if the format is supported."));
        return;
    }

    m_snapshotOverlay->setSnapshot(img);
    m_editBtn->setEnabled(true);
    m_placeBtn->setEnabled(true);
    m_snapshotFusion.imageData = img;
}

// ---------------------------------------------------------------------------
// 时间戳区域持久化（v1.2.1：按视频路径 hash，同一摄像头复用）
// ---------------------------------------------------------------------------
QRectF MainWindow::readTimestampRoiRegistry(const QString &videoPath) const
{
    QSettings s("LumenArc", "LumenArc");
    const QByteArray key = "calibration/roi_"
        + QCryptographicHash::hash(videoPath.toUtf8(), QCryptographicHash::Md5).toHex();
    return s.value(QString::fromLatin1(key)).toRectF();
}

QRectF MainWindow::savedTimestampRoi(const QString &videoPath) const
{
    if (videoPath.isEmpty())
        return QRectF();
    // v1.3.0 框选记忆随案（M2 任务9）：入案视频读写 case.json
    if (m_caseManager && m_caseManager->isCaseVideo(videoPath)) {
        QRectF roi = m_caseManager->timestampRoiFor(videoPath);
        if (roi.isValid())
            return roi;
        // 迁移：注册表旧值只读复制一次入案（注册表原值保留一版，拍板§8-12）
        roi = readTimestampRoiRegistry(videoPath);
        if (roi.isValid())
            m_caseManager->setTimestampRoi(videoPath, roi);
        return roi;
    }
    // 独立模式照旧 QSettings
    return readTimestampRoiRegistry(videoPath);
}

/// v1.7.1：后台保存当前视频 .vla + 同步案件校时徽标。
/// 分析完成自动保存与校时采用后共用（用户实测：校时后徽标不出现——
/// 旧流程只在保存 .vla 时刷新徽标，校时采用未触发保存）。
void MainWindow::saveCurrentVlaAsync()
{
    if (m_currentVideoPath.isEmpty()
        || m_currentVideoPath.endsWith(".vla", Qt::CaseInsensitive))
        return;
    // v1.3.0 路径分流：入案视频 .vla 落案件 videos/V###.vla
    const QString vlaPath = m_caseManager->vlaPathFor(m_currentVideoPath);
    QDir().mkpath(QFileInfo(vlaPath).absolutePath());
    const QRect magRect = m_magnifier ? m_magnifier->currentSourceRect() : QRect();
    // 全部参数为值拷贝（各 model 的 getter 返回副本），后台线程安全
    const QVector<QRect> regions = m_roiModel->regions();
    const TimeCalibration calibration = m_calibration;
    const QVector<ChartLabel> labels = m_chartPanel->labels();
    const QRect pinned = m_pinnedRect;
    const SnapshotFusionData fusion = m_snapshotFusion;
    const QVector<QPolygon> polygons = m_roiModel->polygons();
    const QVector<GuideLine> lines = m_guideLineModel->lines();
    const QVector<int> regionRoiIds = m_roiModel->roiIds();
    const QVector<int> polygonRoiIds = m_roiModel->polygonRoiIds();
    TimelineModel *model = m_timelineModel;
    QtConcurrent::run([model, vlaPath, regions, calibration, magRect, labels,
                       pinned, fusion, polygons, lines,
                       regionRoiIds, polygonRoiIds]() {
        model->saveToFile(vlaPath, regions, calibration, magRect, labels,
                          pinned, fusion, polygons, lines,
                          regionRoiIds, polygonRoiIds);
    });
    // 同步刷新案件校时徽标缓存（.vla 为 SSOT；案件模式空指针安全）
    m_caseManager->updateCalibrationBadge(
        m_currentVideoPath, m_calibration.isEffective(),
        calibrationBadgeSummary());
}

void MainWindow::saveTimestampRoi(const QString &videoPath, const QRectF &norm)
{
    if (videoPath.isEmpty() || !norm.isValid())
        return;
    // v1.3.0 入案视频框选记忆写 case.json（独立模式照旧 QSettings）
    if (m_caseManager && m_caseManager->isCaseVideo(videoPath)) {
        m_caseManager->setTimestampRoi(videoPath, norm);
        return;
    }
    QSettings s("LumenArc", "LumenArc");
    const QByteArray key = "calibration/roi_"
        + QCryptographicHash::hash(videoPath.toUtf8(), QCryptographicHash::Md5).toHex();
    s.setValue(QString::fromLatin1(key), norm);
}

QString MainWindow::calibrationBadgeSummary() const
{
    if (!m_calibration.isEffective())
        return QString();
    QString src;
    switch (m_calibration.source) {
    case TimeCalibration::Source::Manual:    src = lang("手动", "manual"); break;
    case TimeCalibration::Source::Ocr:       src = QStringLiteral("OCR"); break;
    case TimeCalibration::Source::AbsStart:  src = QStringLiteral("absStart"); break;
    case TimeCalibration::Source::Inherited: src = lang("继承", "inherited"); break;
    default: break;
    }
    // 例："OCR 3点, rate=1.000"；分段重建模式标注 piecewise
    QString s = lang("%1 %2点, rate=%3", "%1 %2pts, rate=%3")
        .arg(src).arg(m_calibration.samples.size())
        .arg(m_calibration.effectiveRate(), 0, 'f', 3);
    if (m_calibration.piecewiseMode())
        s += lang("（分段重建）", " (piecewise)");
    return s;
}

void MainWindow::showTrayNotification(const QString &title,
                                      const QString &message)
{
    // v1.2.2：校时长任务（重建可达数分钟）用户最小化等待场景 ——
    // Windows toast 通知。用户正在操作任一窗口时不打扰。
    if (QApplication::activeWindow())
        return;
    if (!QSystemTrayIcon::isSystemTrayAvailable()
        || !QSystemTrayIcon::supportsMessages())
        return;
    if (!m_trayIcon) {
        m_trayIcon = new QSystemTrayIcon(windowIcon(), this);
        connect(m_trayIcon, &QSystemTrayIcon::messageClicked, this, [this]() {
            if (m_calibrationDialog) {
                m_calibrationDialog->showNormal();
                m_calibrationDialog->raise();
                m_calibrationDialog->activateWindow();
            }
            showNormal();
            raise();
            activateWindow();
        });
    }
    m_trayIcon->show();
    m_trayIcon->showMessage(title, message,
                            QSystemTrayIcon::Information, 10000);
    // 托盘图标不常驻：消息弹出后自动隐藏（toast 已入系统通知中心）
    QTimer::singleShot(15000, m_trayIcon, &QSystemTrayIcon::hide);
}

void MainWindow::onSetStartTime()
{
    // v1.2.1 非模态校时窗口：重建/识别在后台进行，主窗口可继续操作；
    // 关闭窗口不取消任务，重开可见进度与结果（Q-3 候选语义不变）。
    if (m_currentVideoPath.isEmpty())
        return;
    // 已开窗口：置顶返回（此前无 return 照样新建 → 双窗共存，框选确认只
    // 推进最后打开的窗口，先开的永远卡在「框选完了不抵达下一步」）
    if (m_calibrationDialog) {
        m_calibrationDialog->raise();
        m_calibrationDialog->activateWindow();
        return;
    }
    const qint64 curPos = m_videoEngine ? m_videoEngine->position() : 0;
    QString sidecarWarning;
    if (m_calibration.source == TimeCalibration::Source::Inherited)
        CalibrationService::loadSidecar(m_currentVideoPath, nullptr,
                                        &sidecarWarning);
    auto *dlg = new TimeSettingsDialog(
        m_currentVideoPath, curPos,
        m_currentDurationMs > 0 ? m_currentDurationMs : m_trustedDurationMs,
        m_calibration, sidecarWarning, m_calibrationService, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    m_calibrationDialog = dlg;
    // 恢复已保存的时间戳区域（同一摄像头自动复用）
    const QRectF savedRoi = savedTimestampRoi(m_currentVideoPath);
    if (savedRoi.isValid())
        dlg->setTimestampRoi(savedRoi);
    // 应用校时（与旧模态路径等价：应用后更新图表与状态栏）
    // v1.2.1 时间戳框选：dialog 请求 → 主窗口视频框选 → 回传 + 持久化
    connect(dlg, &TimeSettingsDialog::requestTimestampRoi,
            this, [this, dlg]() {
                // 优先用已保存的区域（同一摄像头复用）；无则给右上角默认框
                QRectF saved = savedTimestampRoi(m_currentVideoPath);
                m_videoWidget->beginTimestampRoiSelection(saved);
                m_roiDialog = dlg;
            });
    connect(dlg, &TimeSettingsDialog::cancelTimestampRoiRequest,
            this, [this]() {
                m_videoWidget->endTimestampRoiSelection();
                m_roiDialog = nullptr;
    });
    // 窗口关闭时退出框选模式
    connect(dlg, &QDialog::destroyed, this, [this]() {
        if (m_roiDialog)
            m_roiDialog = nullptr;
        m_videoWidget->endTimestampRoiSelection();
        m_calibrationDialog = nullptr;
    });
    connect(dlg, &TimeSettingsDialog::goTaskFinished,
            this, &MainWindow::showTrayNotification);
    connect(dlg, &TimeSettingsDialog::calibrationApplied,
            this, [this](const TimeCalibration &cal) {
                m_calibration = cal;
                m_chartPanel->setCalibration(m_calibration);
                QString msg;
                if (!cal.isValid()) {
                    msg = lang("校时已清除", "Calibration cleared");
                } else if (cal.dateKnown) {
                    msg = lang("校时已应用：流内 0 点 = %1",
                               "Calibration applied: stream 0 = %1")
                              .arg(QDateTime::fromMSecsSinceEpoch(cal.offsetMs)
                                       .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")));
                    if (cal.rateApplied)
                        msg += lang("；漂移 %1 秒/天", "; drift %1 s/day")
                                   .arg(cal.driftSecondsPerDay(), 0, 'f', 1);
                    if (cal.truthSet)
                        msg += lang("；北京时间偏移 %1s", "; Beijing offset %1s")
                                   .arg(cal.truthOffsetMs / 1000.0, 0, 'f', 1);
                    if (cal.piecewiseMode())
                        msg += lang("；分段重建 %1 段（变速）", "; piecewise %1 segs")
                                   .arg(cal.piecewise.size());
                } else {
                    msg = lang("时间偏移已应用：%1s", "Time offset applied: %1s")
                              .arg(cal.offsetMs / 1000.0, 0, 'f', 1);
                }
                showOperationStatus(msg);
                // v1.7.1：校时采用后即落盘 .vla 并刷新案件徽标（用户实测：
                // 校时完成后 ⏰ 不出现——旧流程只更新内存，徽标要等保存）
                saveCurrentVlaAsync();
                if (m_caseDock)
                    m_caseDock->refreshTree();
            });
    dlg->show();
    dlg->raise();
    dlg->activateWindow();
}

void MainWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        event->acceptProposedAction();
    }
}

void MainWindow::dropEvent(QDropEvent *event)
{
    const QMimeData *mimeData = event->mimeData();
    if (!mimeData->hasUrls())
        return;

    QList<QUrl> urls = mimeData->urls();
    if (urls.isEmpty())
        return;

    // v0.3: Support multi-file drop - add all to video list
    bool first = true;
    for (const QUrl &url : urls) {
        QString filePath = url.toLocalFile();
        if (filePath.isEmpty()) continue;

        // Get video metadata (fps + total frames -> duration)
        float fps = 30.0f;
        qint64 durationMs = 0;
        const auto timing = m_analysisEngine->videoTiming(filePath);   // R2：接口调用
        fps = (timing.fps > 0) ? timing.fps : 30.0f;
        durationMs = timing.durationMs;
        if (fps <= 0) fps = 30.0f;

        m_videoListPanel->addVideo(filePath, durationMs, fps);

        // v1.3.0 M2 任务7：拖入即入案（有打开案件时；拍板§8-5）
        admitVideoToCase(filePath, true);

        if (first) {
            openVideoFile(filePath);
            // 引擎回退：getVideoInfo 失败时改用引擎时长
            if (durationMs <= 0) {
                qint64 engineDur = m_videoEngine->duration();
                if (engineDur > 0) {
                    m_videoListPanel->updateDuration(filePath, engineDur);
                }
            }
            first = false;
        }
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    QMainWindow::keyReleaseEvent(event);
}

void MainWindow::onPlay()
{
    m_videoEngine->play();
    showOperationStatus(lang("播放", "Playing"));
}

void MainWindow::onPause()
{
    m_videoEngine->pause();
    showOperationStatus(lang("暂停", "Paused"));
}

void MainWindow::onStop()
{
    m_videoEngine->stop();
    m_currentSpeed = 1.0f;
    m_speedBtn->setText("1x");
    m_videoEngine->setRate(1.0f);
    updatePlaybackButtons();
    showOperationStatus(lang("停止", "Stopped"));
}

/// @brief v0.3: 启动多视频连续播放
/// @brief 按步进调整播放速度：0.25x/0.5x/1x/2x/4x/8x
void MainWindow::adjustSpeed(float delta)
{
    static const float speeds[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
    static const int count = 6;

    // Find current speed index
    int idx = 0;
    for (int i = 0; i < count; ++i) {
        if (qAbs(m_currentSpeed - speeds[i]) < 0.01f) {
            idx = i;
            break;
        }
    }

    // Adjust index
    if (delta > 0)
        idx = qMin(idx + 1, count - 1);
    else
        idx = qMax(idx - 1, 0);

    applySpeed(speeds[idx]);
}

/// @brief 循环切换播放倍速：0.25x/0.5x/1x/2x/4x/8x，到达最高速后回到最低速
void MainWindow::cycleSpeed()
{
    static const float speeds[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
    static const int count = 6;

    // Find current speed index
    int idx = 0;
    for (int i = 0; i < count; ++i) {
        if (qAbs(m_currentSpeed - speeds[i]) < 0.01f) {
            idx = i;
            break;
        }
    }

    // Cycle to next speed, wrapping around to the start
    idx = (idx + 1) % count;
    applySpeed(speeds[idx]);
}

/// @brief 统一应用播放速度并更新 UI 和状态提示
void MainWindow::applySpeed(float speed)
{
    m_currentSpeed = speed;

    // Format display text: integers show "2x", decimals show "0.5x"
    QString speedText;
    if (m_currentSpeed == static_cast<int>(m_currentSpeed))
        speedText = QString("%1x").arg(static_cast<int>(m_currentSpeed));
    else
        speedText = QString("%1x").arg(m_currentSpeed, 0, 'f', 2).replace(".00", "");

    m_speedBtn->setText(speedText);
    m_videoEngine->setRate(m_currentSpeed);
    QString speedStatus = QString(lang("倍速 %1", "Speed %1")).arg(speedText);
    if (qAbs(m_currentSpeed - 1.0f) > 0.01f && !m_videoEngine->supportsRateAudio())
        speedStatus += lang("（音频已静音）", " (audio muted)");
    showOperationStatus(speedStatus);
}

void MainWindow::updatePlaybackButtons()
{
    bool playing = (m_videoEngine->state() == PlaybackState::Playing);
    bool hasMedia = (m_currentDurationMs > 0) || m_videoEngine->duration() > 0;
    m_playBtn->setEnabled(!playing && hasMedia);
    m_pauseBtn->setEnabled(playing);
    m_stopBtn->setEnabled(playing || m_videoEngine->state() == PlaybackState::Paused);
}

/// @brief 创建放大镜：设置视频尺寸/区域模型/同步截图叠加
void MainWindow::createMagnifier()
{
    if (m_magnifier)
        return;

    m_magnifier = new MagnifierWidget(this);

    int vw = m_videoEngine ? m_videoEngine->videoWidth() : 1920;
    int vh = m_videoEngine ? m_videoEngine->videoHeight() : 1080;
    m_magnifier->setVideoSize(vw, vh);
    m_magnifier->setRegionModel(m_roiModel);
    m_magnifier->setPolygonModel(m_roiModel);
    m_magnifier->setGuideLineModel(m_guideLineModel);

    // 旋转档位 + 画面调节同步（Q1 方案 A：放大视图随主画面一起转/调）
    m_magnifier->setDisplayRotation(m_videoWidget->displayRotation());
    if (m_adjustPanel)
        m_magnifier->setDisplayAdjust(m_adjustPanel->adjust());

    // 放大镜来源标识框（§14 Q1）：源区域变化实时同步到主画面 overlay；
    // 初始值立即下发（setVideoSize 的信号早于 connect）
    connect(m_magnifier, &MagnifierWidget::sourceRectChanged,
            this, [this](const QRect &storedRect, qreal zoom) {
                m_videoWidget->overlay()->setMagnifierRect(storedRect, zoom);
            });
    m_videoWidget->overlay()->setMagnifierRect(m_magnifier->currentSourceRect(),
                                               m_magnifier->zoomLevel());

    // Sync current ROI mode to magnifier overlay
    if (m_magnifier->overlay()) {
        m_magnifier->overlay()->setPolygonMode(m_videoWidget->overlay()->isPolygonMode());
        m_magnifier->overlay()->setGuideLineMode(m_videoWidget->overlay()->isGuideLineMode());
    }

    addDockWidget(Qt::RightDockWidgetArea, m_magnifier);
    m_magnifier->setWindowTitle(lang("放大镜", "Magnifier"));
    m_magnifier->show();

    // Auto-layout: hide video list panel, show placeholder, resize magnifier to ~40%
    m_videoListWasExpanded = m_videoListContent->isVisible();
    if (m_videoListWasExpanded || m_videoListPanel->isVisible()) {
        m_videoListPanel->setVisible(false);
        m_videoListPlaceholder->setVisible(true);
        resizeDocks({m_videoListPlaceholder}, {24}, Qt::Horizontal);
    }
    int windowWidth = width();
    resizeDocks({m_magnifier}, {static_cast<int>(windowWidth * 0.4)}, Qt::Horizontal);

    // Sync snapshot overlay if currently active
    if (m_snapshotOverlay && m_snapshotOverlay->hasSnapshot() && m_snapshotOverlay->isOverlayActive()) {
        m_magnifier->setSnapshotOverlay(m_snapshotOverlay->snapshotImage(),
                                         m_snapshotOverlay->brightness(),
                                         m_snapshotOverlay->contrastValue(),
                                         m_snapshotOverlay->opacityValue());
    }

    // Connect magnifier overlay's regionAdjustmentFinished for data invalidation warning
    connect(m_magnifier->overlay(), &OverlayWidget::regionAdjustmentFinished,
            this, [this](int regionIndex, const QRect &originalRect, const QRect &newRect) {
                Q_UNUSED(newRect);
                AnalysisSnapshot snapshot = m_timelineModel->snapshot();
                if (!snapshot.isEmpty()) {
                    int roiId = m_roiModel->roiIdAt(regionIndex);
                    if (roiId > 0 && snapshot.dataIndexOfRoiId(roiId, DataEntry::Rect) >= 0) {
                        auto reply = QMessageBox::question(this,
                            lang("数据失效警告", "Data Invalidation Warning"),
                            lang("调整该区域将导致亮度量化数据失效。\n确定要继续吗？",
                                 "Adjusting this region will invalidate the luminance analysis data.\nContinue?"),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                        if (reply == QMessageBox::Yes) {
                            m_timelineModel->removeRegionDataByRoiId(roiId, DataEntry::Rect);
                        } else {
                            m_roiModel->updateRegion(regionIndex, originalRect);
                        }
                    }
                }
            });

    // Connect magnifier overlay's polygonAdjustmentFinished
    connect(m_magnifier->overlay(), &OverlayWidget::polygonAdjustmentFinished,
            this, [this](int polygonIndex, const QPolygon &originalPolygon, const QPolygon &newPolygon) {
                Q_UNUSED(newPolygon);
                AnalysisSnapshot snapshot = m_timelineModel->snapshot();
                if (!snapshot.isEmpty()) {
                    int roiId = m_roiModel->polygonRoiIdAt(polygonIndex);
                    int dataIdx = snapshot.dataIndexOfRoiId(roiId, DataEntry::Polygon);
                    if (dataIdx >= 0) {
                        auto reply = QMessageBox::question(this,
                            lang("数据失效警告", "Data Invalidation Warning"),
                            lang("调整该多边形将导致亮度量化数据失效。\n确定要继续吗？",
                                 "Adjusting this polygon will invalidate the luminance analysis data.\nContinue?"),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                        if (reply == QMessageBox::Yes) {
                            m_timelineModel->removeRegionDataByRoiId(roiId, DataEntry::Polygon);
                        } else {
                            m_roiModel->updatePolygon(polygonIndex, originalPolygon);
                        }
                    }
                }
            });

    // Forward the current frame so the magnifier shows content immediately
    // even if the video is paused or has ended.
    // 注意必须给【原始帧】：放大镜内部按原视频系坐标裁剪（旋转由 ContentWidget
    // 在显示前应用）；currentFrame() 是已旋转+LUT 的显示帧，裁剪几何会错。
    if (!m_videoWidget->rawFrame().isNull()) {
        m_magnifier->onFrameReady(m_videoWidget->rawFrame());
    }
}

void MainWindow::removeMagnifier()
{
    if (!m_magnifier)
        return;

    // Restore video list panel state
    if (m_videoListPlaceholder->isVisible()) {
        // User didn't expand video list while magnifier was open
        m_videoListPlaceholder->setVisible(false);
        if (m_videoListWasExpanded) {
            m_videoListPanel->setVisible(true);
            m_videoListContent->setVisible(true);
            m_videoListCollapseBtn->setText(QString::fromUtf8("\xe2\x97\x80")); // ◀
            m_videoListCollapseBtn->setToolTip(lang("收起视频列表", "Collapse video list"));
            resizeDocks({m_videoListPanel}, {250}, Qt::Horizontal);
        }
    }
    // else: user expanded video list while magnifier was open, keep it as is

    m_videoWidget->overlay()->setMagnifierRect(QRect(), 0.0);   // 标识框随 dock 关闭消失
    m_magnifier->clearSnapshotOverlay();
    m_magnifier->close();
    m_magnifier->deleteLater();
    m_magnifier = nullptr;
}

void MainWindow::onMagnifierWheelZoom(int delta, QPoint videoPos)
{
    if (!m_magnifier)
        createMagnifier();

    if (m_magnifier) {
        m_magnifier->zoomAtPoint(delta, videoPos);
    }
}

void MainWindow::showVideoContextMenu(const QPoint &pos)
{
    QMenu menu;

    if (m_chartPanel->isABRegionSet()) {
        QAction *loopAction = menu.addAction(m_chartPanel->isABLoop()
            ? lang("关闭循环", "Disable Loop")
            : lang("开启循环", "Enable Loop"));
        QAction *clearABAction = menu.addAction(lang("清除 A/B 区域", "Clear A/B Region"));
        QAction *chosen = menu.exec(m_videoWidget->overlay()->mapToGlobal(pos));
        if (chosen == loopAction) {
            m_chartPanel->setABLoop(!m_chartPanel->isABLoop());
        } else if (chosen == clearABAction) {
            m_chartPanel->clearAB();
        }
    } else {
        if (m_magnifier) {
            menu.addAction(lang("关闭放大镜", "Close Magnifier"));
            QAction *invertAction = menu.addAction(lang("反向平移", "Invert Pan"));
            invertAction->setCheckable(true);
            invertAction->setChecked(m_magnifier->isinvertPan());
            QAction *chosen = menu.exec(m_videoWidget->overlay()->mapToGlobal(pos));
            if (m_magnifier && chosen && chosen->text() == lang("关闭放大镜", "Close Magnifier")) {
                removeMagnifier();
            } else if (chosen == invertAction) {
                m_magnifier->setInvertPan(invertAction->isChecked());
            }
        } else {
            menu.exec(m_videoWidget->overlay()->mapToGlobal(pos));
        }
    }
}

void MainWindow::updatePinnedImage(const QImage &frame)
{
    if (!m_pinned || m_pinnedRect.isEmpty() || frame.isNull())
        return;
    // 同步源视频原生分辨率（帧坐标换算基准；videoSizeChanged 仅在 load 时发出一次，
    // 而 PinnedWidget 跨视频存活，故每帧同步，赋值零开销）
    if (m_videoEngine)
        m_pinned->setVideoSize(m_videoEngine->videoWidth(), m_videoEngine->videoHeight());
    m_pinned->setPinnedImage(frame, m_pinnedRect);
}

/// @brief 启动离线分析：前置检查→状态栏进度→Python进程
void MainWindow::onAnalyze()
{
    if (m_currentVideoPath.isEmpty()) {
        QMessageBox::information(this, lang("亮度分析", "Luminance Analysis"),
            lang("请先打开一个视频文件。", "Please open a video file first."));
        return;
    }

    // If python path not found, silently retry detection
    auto *pyEngine = qobject_cast<PythonAnalysisEngine *>(m_analysisEngine);
    if (pyEngine && pyEngine->pythonExecutable().isEmpty()) {
        pyEngine->setPythonExecutable(detectPythonPath());
        if (pyEngine->pythonExecutable().isEmpty()) {
            QMessageBox::warning(this, lang("亮度分析", "Luminance Analysis"),
                lang("未找到 Python 解释器。\n请安装 Python 3.8+ 并确保在 PATH 中。",
                     "Python interpreter not found.\nPlease install Python 3.8+ and ensure it is in PATH."));
            return;
        }
    }

    QVector<QRect> regions = m_roiModel->regions();
    QVector<QPolygon> polygons = m_roiModel->polygons();
    if (regions.isEmpty() && polygons.isEmpty()) {
        QMessageBox::information(this, lang("亮度分析", "Luminance Analysis"),
            lang("请先在视频上绘制至少一个 ROI 区域。",
                 "Please draw at least one ROI on the video."));
        return;
    }

    // Collect ROI IDs for data tracking
    QVector<int> rectRoiIds, polygonRoiIds;
    for (int i = 0; i < m_roiModel->regionCount(); ++i)
        rectRoiIds.append(m_roiModel->roiIdAt(i));
    for (int i = 0; i < m_roiModel->polygonCount(); ++i)
        polygonRoiIds.append(m_roiModel->polygonRoiIdAt(i));

    if (m_analysisEngine->isRunning()) {
        QMessageBox::information(this, lang("亮度分析", "Luminance Analysis"),
            lang("分析正在运行中。", "Analysis is already running."));
        return;
    }

    // v0.3: Use status bar instead of modal dialog
    m_analyzeBtn->setEnabled(false);
    m_analyzeBtn->setText(lang("亮度分析中...", "Analyzing luminance..."));
    m_cancelBtn->setEnabled(true);
    m_cancelBtn->setVisible(true);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_statusLabel->setText(lang("正在准备分析...", "Preparing analysis..."));

    m_analysisPhase = Luminance;
    m_analysisEngine->startAnalysis(m_currentVideoPath, regions, polygons, {},
                                     rectRoiIds, polygonRoiIds);
}

/// @brief 启动音频分析（独立于亮度分析，无需 ROI）
void MainWindow::onAudioAnalysis()
{
    if (m_currentVideoPath.isEmpty()) {
        QMessageBox::information(this, lang("音频分析", "Audio Analysis"),
            lang("请先打开一个视频文件。", "Please open a video file first."));
        return;
    }

    // v1.5.0-3：startAudioAnalysis 已上移接口（libav/Python 均支持），
    // 移除 Python 专属 guard（旧版 qobject_cast 检查已废弃）
    if (auto *pyEngine = qobject_cast<PythonAnalysisEngine *>(m_analysisEngine)) {
        // Python 引擎需要解释器存在（libav 引擎无需 Python）
        if (pyEngine->pythonExecutable().isEmpty()) {
            pyEngine->setPythonExecutable(detectPythonPath());
            if (pyEngine->pythonExecutable().isEmpty()) {
                QMessageBox::warning(this, lang("音频分析", "Audio Analysis"),
                    lang("未找到 Python 解释器。\n请安装 Python 3.8+ 并确保在 PATH 中。",
                         "Python interpreter not found.\nPlease install Python 3.8+ and ensure it is in PATH."));
                return;
            }
        }
    }

    if (m_analysisEngine->isRunning()) {
        QMessageBox::information(this, lang("音频分析", "Audio Analysis"),
            lang("分析正在运行中。", "Analysis is already running."));
        return;
    }

    m_analyzeBtn->setEnabled(false);
    m_audioAnalysisBtn->setEnabled(false);
    m_audioAnalysisBtn->setText(lang("分析中...", "Analyzing..."));
    m_cancelBtn->setEnabled(true);
    m_cancelBtn->setVisible(true);
    m_progressBar->setVisible(true);
    m_progressBar->setValue(0);
    m_statusLabel->setText(lang("正在分析音频...", "Analyzing audio..."));

    m_analysisPhase = Audio;
    m_analysisEngine->startAudioAnalysis(m_currentVideoPath);   // R2：接口调用
}

void MainWindow::onAnalysisProgress(int analyzed, int total, qreal percent)
{
    // Use m_analysisPhase to determine progress handling (not total <= 10 heuristic)
    if (m_analysisPhase == Audio) {
        // Audio analysis: map to 70%-100%
        qreal mappedPercent = 70.0 + (percent / 100.0) * 30.0;
        m_statusLabel->setText(
            QString(lang("音频分析阶段 %1/%2（%3%）", "Audio phase %1/%2 (%3%)"))
                .arg(analyzed).arg(total).arg(percent, 0, 'f', 1));
        int newPercent = qBound(0, static_cast<int>(mappedPercent), 100);
        if (newPercent > m_progressBar->value()) {
            m_progressBar->setValue(newPercent);
        }
    } else if (m_analysisPhase == Luminance) {
        // Luminance analysis: 0%-100%（v1.1 起 analyzed/total 语义统一为采样点数）
        m_statusLabel->setText(
            QString(lang("分析中 %1%（%2 个采样点）", "Analyzing %1% (%2 samples)"))
                .arg(percent, 0, 'f', 1).arg(analyzed));
        int newPercent = qBound(0, static_cast<int>(percent), 100);
        if (newPercent > m_progressBar->value()) {
            m_progressBar->setValue(newPercent);
        }
    }
}

/**
 * @brief 分析完成处理：数据填充→图表更新→频谱图更新→自动保存VLA
 */
void MainWindow::onAnalysisFinished(const AnalysisSnapshot &snapshot)
{
    m_analysisPhase = None;
    m_analyzeBtn->setText(lang("亮度分析", "Luminance"));
    m_analyzeBtn->setEnabled(true);
    m_audioAnalysisBtn->setText(lang("音频分析", "Audio"));
    m_audioAnalysisBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setVisible(false);
    m_progressBar->setVisible(false);
    m_statusLabel->setText(lang("分析完成", "Analysis complete"));

    // --- Chart update: luminance analysis preserves existing audio ---
    if (!snapshot.timestamps.isEmpty()) {
        // Luminance analysis: keep existing audio if new result has none
        AnalysisSnapshot existing = m_timelineModel->snapshot();
        AudioData audioToUse = snapshot.hasAudio() ? snapshot.audio : existing.audio;
        m_timelineModel->setData(QVector<qint64>(snapshot.timestamps),
                                 QVector<QVector<qreal>>(snapshot.values),
                                 QVector<DataEntry>(snapshot.dataEntries),
                                 audioToUse);
        // Update spectrogram only if new audio arrived; otherwise keep existing
        if (snapshot.hasAudio() && m_spectrogramEnhanced) {
            m_spectrogramEnhanced->setSpectrogramData(snapshot.audio);
        }
    } else if (snapshot.hasAudio()) {
        // Audio-only: merge audio into existing snapshot
        AnalysisSnapshot existing = m_timelineModel->snapshot();
        existing.audio = snapshot.audio;
        m_timelineModel->setData(existing.timestamps, existing.values, existing.audio);
        // Update spectrogram with new audio data
        if (m_spectrogramEnhanced) {
            m_spectrogramEnhanced->setSpectrogramData(snapshot.audio);
        }
    }

    // Auto-save .vla cache alongside the video file（后台线程；含校时徽标刷新）
    saveCurrentVlaAsync();

    QString msg;
    if (!snapshot.timestamps.isEmpty()) {
        msg = lang("亮度分析完成！\n数据点数：%1\n区域数：%2",
                   "Luminance analysis complete!\nTotal points: %1\nRegions: %2")
                  .arg(snapshot.pointCount()).arg(snapshot.regionCount());
    } else {
        msg = lang("音频分析完成，音量图、语谱图已生成。",
                   "Audio analysis complete. Volume and spectrogram generated.");
    }

    QTimer::singleShot(0, this, [this, msg]() {
        auto *box = new QMessageBox(QMessageBox::Information,
                                    lang("分析完成", "Analysis Complete"), msg,
                                    QMessageBox::Ok, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        QTimer::singleShot(5000, box, &QWidget::close);
        box->show();
    });
}

void MainWindow::onAnalysisFailed(const QString &error)
{
    m_analysisPhase = None;
    m_analyzeBtn->setText(lang("亮度分析", "Luminance"));
    m_analyzeBtn->setEnabled(true);
    m_audioAnalysisBtn->setText(lang("音频分析", "Audio"));
    m_audioAnalysisBtn->setEnabled(true);
    m_cancelBtn->setEnabled(false);
    m_cancelBtn->setVisible(false);
    m_progressBar->setVisible(false);
    m_statusLabel->setText(lang("分析失败", "Analysis failed"));

    if (error != "Analysis cancelled by user.") {
        // Defer dialog to avoid processEvents() race condition
        QTimer::singleShot(0, this, [this, error]() {
            QMessageBox::critical(this, lang("分析失败", "Analysis Failed"),
                lang("离线分析失败。\n请确保已安装 Python 和 OpenCV。\n\n",
                     "Offline analysis failed.\nMake sure Python and OpenCV are installed.\n\n") + error);
        });
    }
}

void MainWindow::onClearRegions()
{
    m_roiModel->clearRegions();
    m_roiModel->clearPolygons();
    // Bug fix: Clear luminance data when all regions are removed.
    // The data is ROI-dependent and meaningless without regions.
    // Audio data is preserved.
    AnalysisSnapshot snapshot = m_timelineModel->snapshot();
    if (!snapshot.isEmpty()) {
        m_timelineModel->clearLuminanceData();
    }
    showOperationStatus(lang("选区已清除", "Regions cleared"));
}

void MainWindow::onClearData()
{
    m_timelineModel->clearData();
    showOperationStatus(lang("数据已清除", "Data cleared"));
}

void MainWindow::onExportCsv()
{
    AnalysisSnapshot snapshot = m_timelineModel->snapshot();
    if (snapshot.isEmpty()) {
        QMessageBox::information(this, lang("导出", "Export"),
            lang("没有数据可导出。", "No data to export."));
        return;
    }

    QString filePath = QFileDialog::getSaveFileName(this,
        lang("导出 CSV", "Export CSV"),
        "luminance_data.csv",
        lang("CSV 文件 (*.csv)", "CSV Files (*.csv)"));

    if (filePath.isEmpty())
        return;

    QVector<QRect> regions = m_roiModel->regions();
    if (snapshot.exportToCsv(filePath, regions, m_calibration)) {
        // Export labels to separate file
        QVector<ChartLabel> labels = m_chartPanel->labels();
        if (!labels.isEmpty()) {
            QString labelsPath = filePath;
            if (labelsPath.endsWith(".csv", Qt::CaseInsensitive))
                labelsPath.chop(4);
            labelsPath += "_labels.csv";

            QFile labelsFile(labelsPath);
            if (labelsFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                // RFC 4180: quote fields containing comma, quote or newline
                auto csvField = [](const QString &s) -> QString {
                    if (s.contains(QLatin1Char(',')) || s.contains(QLatin1Char('"'))
                        || s.contains(QLatin1Char('\n')) || s.contains(QLatin1Char('\r'))) {
                        QString t = s;
                        t.replace(QStringLiteral("\""), QStringLiteral("\"\""));
                        return QStringLiteral("\"") + t + QStringLiteral("\"");
                    }
                    return s;
                };
                QTextStream out(&labelsFile);
                out << "Time(ms),Time,Text,Color\n";
                for (const auto &label : labels) {
                    QString timeStr;
                    if (m_calibration.dateKnown) {
                        timeStr = QDateTime::fromMSecsSinceEpoch(
                            m_calibration.beijingMsOf(label.timeMs))
                            .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
                    } else {
                        timeStr = formatTime(label.timeMs + m_calibration.offsetMs);
                    }
                    out << label.timeMs << ","
                        << timeStr << ","
                        << csvField(label.text) << ","
                        << label.color.name(QColor::HexArgb) << "\n";
                }
                labelsFile.close();
                QMessageBox::information(this, lang("导出", "Export"),
                    lang("数据导出成功。\n标签文件：",
                         "Data exported successfully.\nLabels: ") + labelsPath);
                showOperationStatus(lang("导出成功", "Exported"));
            } else {
                QMessageBox::information(this, lang("导出", "Export"),
                    lang("数据导出成功。\n标签导出失败。",
                         "Data exported successfully.\nFailed to export labels."));
            }
        } else {
            QMessageBox::information(this, lang("导出", "Export"),
                lang("数据导出成功。", "Data exported successfully."));
        }
    } else {
        QMessageBox::critical(this, lang("错误", "Error"),
            lang("数据导出失败。", "Failed to export data."));
    }
}

void MainWindow::onDurationChanged(qint64 durationMs)
{
    // VLC 可能读取到容器里异常大的时长（如监控文件拼接导致）。
    // 用 Python 分析引擎按真实帧数算出的时长作为上限校准。
    qint64 effectiveDur = durationMs;
    if (m_trustedDurationMs > 0 && durationMs > m_trustedDurationMs)
        effectiveDur = m_trustedDurationMs;
    m_currentDurationMs = effectiveDur;

    m_chartPanel->setDuration(effectiveDur);

    // 拖拽匀速化（第一层）：向图表传入帧时长，启用速度自适应帧网格量化
    const float fpsNow = m_videoEngine->fps();
    m_chartPanel->setFrameDuration(fpsNow > 0.0f ? qint64(1000.0 / fpsNow + 0.5) : 0);

    // v0.3: Sync duration to VideoListPanel
    if (!m_currentVideoPath.isEmpty() && m_videoListPanel) {
        m_videoListPanel->updateDuration(m_currentVideoPath, effectiveDur);
    }

    updateTimeDisplay();

    // 更新硬解适配器显示（引擎 openFile 后 adapterName 已确定）
    QString adapter = m_videoEngine->hardwareAdapterName();
    if (adapter.isEmpty())
        m_hwAdapterLabel->setText(lang("软解", "SW decode"));
    else
        m_hwAdapterLabel->setText(adapter);
}

void MainWindow::onPositionChanged(qint64 timeMs)
{
    // Bug fix: During chart cursor drag, VLC may fire positionChanged with
    // stale intermediate positions (play→pause cycle during seek). Skip
    // overwriting the cursor position so the drag stays smooth.
    if (!m_chartPanel->isDraggingCursor() && !m_spectrogramEnhanced->isDraggingCursor()) {
        m_chartPanel->setCursorTime(timeMs);
        m_spectrogramEnhanced->setCursorTime(timeMs);
    }

    // A/B region loop playback
    if (m_chartPanel->isABRegionSet() && m_videoEngine->state() == PlaybackState::Playing) {
        qint64 bPoint = m_chartPanel->abPointB();
        if (timeMs >= bPoint) {
            if (m_chartPanel->isABLoop()) {
                m_videoEngine->seek(m_chartPanel->abPointA());
            } else {
                onPause();
            }
        }
    }

    updateTimeDisplay();
}

void MainWindow::onSeekFromChart(qint64 timeMs)
{
    // 光标立即跟随（UI 响应）
    m_chartPanel->setCursorTime(timeMs);
    m_spectrogramEnhanced->setCursorTime(timeMs);

    // 拖拽中：scrub 追逐模式——只写原子目标，引擎 worker 连续解码追赶，
    // 免节流免命令队列（mouseMove 不再产生 seek 命令，拖拽期间解码管线不断流）。
    // 图表与语谱两个面板的光标拖拽都走此路径（此前语谱拖拽漏判，
    // 退化成 50ms 节流一次性 seek：每拍全量 flush 重定，无追赶无缓存——卡顿主因）
    const bool dragging = m_chartPanel->isDraggingCursor()
                          || (m_spectrogramEnhanced && m_spectrogramEnhanced->isDraggingCursor());
    if (dragging) {
        m_videoEngine->setScrubMode(true);
        m_videoEngine->setScrubTarget(timeMs);
    } else {
        // 点击/标签跳转：一次性 seek
        m_videoEngine->setScrubMode(false);
        m_pendingSeekMs = timeMs;
        if (!m_seekThrottleTimer) {
            m_seekThrottleTimer = new QTimer(this);
            m_seekThrottleTimer->setSingleShot(true);
            m_seekThrottleTimer->setInterval(50);
            connect(m_seekThrottleTimer, &QTimer::timeout, this, [this]() {
                if (m_pendingSeekMs != m_lastIssuedSeekMs && m_pendingSeekMs >= 0) {
                    m_lastIssuedSeekMs = m_pendingSeekMs;
                    m_videoEngine->seek(m_pendingSeekMs);
                }
            });
        }
        if (!m_seekThrottleTimer->isActive()) {
            m_lastIssuedSeekMs = timeMs;
            m_videoEngine->seek(timeMs);
            m_seekThrottleTimer->start();
        }
    }
}

void MainWindow::updateTimeDisplay()
{
    qint64 pos = m_videoEngine ? m_videoEngine->position() : 0;
    qint64 dur = (m_currentDurationMs > 0) ? m_currentDurationMs
                                            : (m_videoEngine ? m_videoEngine->duration() : 0);
    m_timeLabel->setText(QString("%1 / %2").arg(formatTime(pos)).arg(formatTime(dur)));
}

QString MainWindow::formatTime(qint64 ms) const
{
    if (ms < 0)
        ms = 0;
    int totalSeconds = static_cast<int>(ms / 1000);
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;
    if (hours > 0)
        return QString("%1:%2:%3").arg(hours).arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
    return QString("%1:%2").arg(minutes, 2, 10, QChar('0')).arg(seconds, 2, 10, QChar('0'));
}

/// @brief 在状态栏左侧显示操作反馈（2秒后自动清除）
void MainWindow::showOperationStatus(const QString &text)
{
    m_operationLabel->setText(text);
    QTimer::singleShot(5000, this, [this]() {
        m_operationLabel->clear();
    });
}

/// @brief 恢复分析状态：区域/校时/标签/截图融合/音频
void MainWindow::restoreAnalysisState(const QVector<QRect> &regions,
                                       const TimeCalibration &calibration,
                                       const QVector<ChartLabel> &labels,
                                       const QRect &pinnedRect,
                                       const SnapshotFusionData &fusion,
                                       const QVector<int> &regionRoiIds)
{
    // 带 roiId 恢复：保持与分析数据 dataEntries 的 roi_id 对齐
    if (regionRoiIds.size() == regions.size())
        m_roiModel->restoreRegions(regions, regionRoiIds);
    else {
        m_roiModel->clearRegions();
        for (const QRect &rc : regions)
            m_roiModel->addRegion(rc);
    }
    m_calibration = calibration;
    m_chartPanel->setCalibration(m_calibration);
    m_chartPanel->setLabels(labels);
    if (!pinnedRect.isEmpty())
        m_pinnedRect = pinnedRect;
    m_snapshotFusion = fusion;
    if (fusion.isValid() && !fusion.imageData.isNull()) {
        m_snapshotOverlay->setSnapshot(fusion.imageData);
        m_snapshotOverlay->setParameters(fusion.brightness, fusion.contrast, fusion.opacity);
        m_editBtn->setEnabled(true);
        m_placeBtn->setEnabled(true);
    }
    AnalysisSnapshot loaded = m_timelineModel->snapshot();
    qDebug() << "[restoreAnalysisState] hasAudio:" << loaded.hasAudio()
             << "spectrogram.size:" << loaded.audio.spectrogram.size()
             << "volume.size:" << loaded.audio.volume.size();
    if (m_spectrogramEnhanced && loaded.hasAudio())
        m_spectrogramEnhanced->setSpectrogramData(loaded.audio);
}

/**
 * @brief 全局事件过滤：所有快捷键全局生效，不受焦点区域影响
 */
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    if (event->type() == QEvent::KeyPress) {
        auto *e = static_cast<QKeyEvent *>(event);
        int key = e->key();
        QWidget *fw = focusWidget();

        // Protection: Don't intercept text input widgets
        if (qobject_cast<QLineEdit*>(fw) || qobject_cast<QTextEdit*>(fw)) {
            return QMainWindow::eventFilter(watched, event);
        }

        // Check if video engine is available
        if (!m_videoEngine || !m_videoEngine->duration())
            return QMainWindow::eventFilter(watched, event);

        // Handle all shortcuts globally
        switch (key) {
        case Qt::Key_Space:
            if (m_videoEngine->state() == PlaybackState::Playing) {
                m_videoEngine->pause();
                showOperationStatus(lang("暂停", "Paused"));
            } else {
                m_videoEngine->play();
                showOperationStatus(lang("播放", "Playing"));
            }
            updatePlaybackButtons();
            return true;

        case Qt::Key_Left: {
            float f = m_videoEngine->fps();
            qint64 frameStep = static_cast<qint64>(1000.0f / f);
            if (frameStep < 1) frameStep = 33;
            m_videoEngine->seek(m_videoEngine->position() - frameStep);
            showOperationStatus(lang("帧 -1", "Frame -1"));
            return true;
        }
        case Qt::Key_Right: {
            float f = m_videoEngine->fps();
            qint64 frameStep = static_cast<qint64>(1000.0f / f);
            if (frameStep < 1) frameStep = 33;
            m_videoEngine->seek(m_videoEngine->position() + frameStep);
            showOperationStatus(lang("帧 +1", "Frame +1"));
            return true;
        }

        case Qt::Key_Up: {
            const int cur = m_videoEngine->volume();
            // v1.7.1：突破 200% 时给一次提示（每次会话仅一次，防重复打扰）
            if (cur <= 200 && cur + 5 > 200 && !m_volumeWarnShown) {
                m_volumeWarnShown = true;
                QMessageBox::information(this, lang("音量", "Volume"),
                    lang("音量即将超过 200%。继续增大可能造成声音失真（削波），\n"
                         "建议仅在原始素材音量过低时使用。",
                         "Volume will exceed 200%. Further increase may cause "
                         "clipping distortion.\nRecommended only for very quiet "
                         "source material."));
            }
            m_videoEngine->setVolume(cur + 5);   // 上限 500%
            showOperationStatus(QString(lang("音量 +5，现音量：%1%", "Volume +5, Current: %1%"))
                                    .arg(m_videoEngine->volume()));
            return true;
        }
        case Qt::Key_Down:
            m_videoEngine->setVolume(m_videoEngine->volume() - 5);
            showOperationStatus(QString(lang("音量 -5，现音量：%1%", "Volume -5, Current: %1%"))
                                    .arg(m_videoEngine->volume()));
            return true;

        case Qt::Key_C:
            adjustSpeed(1.0f);
            return true;
        case Qt::Key_X:
            adjustSpeed(-1.0f);
            return true;
        case Qt::Key_Z:
            m_currentSpeed = 1.0f;
            m_speedBtn->setText("1x");
            m_videoEngine->setRate(1.0f);
            showOperationStatus(lang("倍速 1x", "Speed 1x"));
            return true;

        case Qt::Key_N: {
            if (m_videoEngine->duration() > 0) {
                // Pause while the modal label dialog is open; otherwise the
                // video keeps playing (and short clips reach the end).
                bool wasPlaying = (m_videoEngine->state() == PlaybackState::Playing);
                if (wasPlaying)
                    m_videoEngine->pause();
                qint64 pos = m_videoEngine->position();
                // 标签一律流内时间存储（显示时走校时换算）。修复：旧代码把
                // 显示偏移加进存储值，设置过时间后标签错位一个 offset 且
                // 悬停/导出时间加了两次 offset。
                m_chartPanel->addLabelAtTime(pos);
                if (wasPlaying)
                    m_videoEngine->play();
                showOperationStatus(lang("标签已添加", "Label added"));
            }
            return true;
        }

        case Qt::Key_A: {
            if (m_videoEngine->duration() > 0) {
                qint64 pos = m_videoEngine->position();
                m_chartPanel->setPointA(pos);
                showOperationStatus(lang("A 点已设置", "Point A set"));
            }
            return true;
        }
        case Qt::Key_B: {
            if (m_videoEngine->duration() > 0) {
                qint64 pos = m_videoEngine->position();
                m_chartPanel->setPointB(pos);
                showOperationStatus(lang("B 点已设置", "Point B set"));
            }
            return true;
        }
        case Qt::Key_S:
            onSnapshotQuick();   // 证据快照（2026-08-14）
            return true;
        case Qt::Key_J:
            adjustSpeed(-1.0f);
            return true;
        case Qt::Key_K:
            if (m_videoEngine->state() == PlaybackState::Playing) {
                m_videoEngine->pause();
                showOperationStatus(lang("暂停", "Paused"));
            } else {
                m_videoEngine->play();
                showOperationStatus(lang("播放", "Playing"));
            }
            updatePlaybackButtons();
            return true;
        case Qt::Key_L:
            adjustSpeed(1.0f);
            return true;

        case Qt::Key_Escape:
            // 如果OverlayWidget在辅助线模式或多边形模式，不处理，让OverlayWidget处理
            if (m_videoWidget->overlay()->isGuideLineMode() ||
                m_videoWidget->overlay()->isPolygonMode()) {
                return false;  // 让事件继续传递给OverlayWidget
            }
            // 否则关闭放大镜
            removeMagnifier();
            showOperationStatus(lang("放大镜已关闭", "Magnifier closed"));
            return true;

        case Qt::Key_P:
            // 切换矩形/多边形模式
            if (m_videoWidget->overlay()->isPolygonMode()) {
                onRectMode();
            } else {
                onPolygonMode();
            }
            return true;

        case Qt::Key_G:
            // 切换辅助线模式
            if (m_videoWidget->overlay()->isGuideLineMode()) {
                onRectMode();
            } else {
                onGuideLineMode();
            }
            return true;
        }
    }

    return QMainWindow::eventFilter(watched, event);
}

void MainWindow::onSnapshotQuick()
{
    const QImage frame = m_videoWidget->currentFrame();   // 已含画面调节+旋转（所见即所得）
    if (frame.isNull()) {
        showOperationStatus(lang("当前无画面，无法快照", "No frame to snapshot"));
        return;
    }
    const int rotation = m_videoWidget->displayRotation();
    const qint64 posMs = m_videoEngine ? m_videoEngine->position() : 0;

    // ---- 时间码：校时后用北京时间；否则相对时间 ----
    QString timeText, fileTimeTag;
    if (m_calibration.dateKnown) {
        const QDateTime dt =
            QDateTime::fromMSecsSinceEpoch(m_calibration.beijingMsOf(posMs));
        timeText = dt.toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        fileTimeTag = dt.toString(QStringLiteral("yyyyMMdd_HHmmss"));
    } else {
        qint64 ms = posMs + m_calibration.offsetMs;
        if (ms < 0) ms = 0;
        const int s = int(ms / 1000);
        timeText = QStringLiteral("%1:%2:%3")
            .arg(s / 3600, 2, 10, QChar('0')).arg((s % 3600) / 60, 2, 10, QChar('0'))
            .arg(s % 60, 2, 10, QChar('0'));
        fileTimeTag = QStringLiteral("t%1-%2-%3")
            .arg(s / 3600, 2, 10, QChar('0')).arg((s % 3600) / 60, 2, 10, QChar('0'))
            .arg(s % 60, 2, 10, QChar('0'));
    }

    // ---- 当前帧标签（±1s 内最近者；ChartLabel 存流内时间）----
    QString labelText;
    {
        qint64 best = LLONG_MAX;
        for (const auto &l : m_chartPanel->labels()) {
            const qint64 d = qAbs(l.timeMs - posMs);
            if (d < best) { best = d; labelText = l.text; }
        }
        if (best > 1000)
            labelText.clear();
    }

    // ---- 视频部分：覆盖层烧录（§14 Q3）→ 放大镜标识框（Q4）→ OSD（Q5）----
    QImage videoPart = frame.convertToFormat(QImage::Format_ARGB32);
    const QSize videoSize = m_videoWidget->overlay()->videoSize();   // 原视频原生尺寸
    // 线宽/字号随分辨率缩放（屏上 1~2px 观感 → 原生全分辨率等比）
    const int annoPen = qBound(1, qRound(videoPart.width() / 1280.0), 4);
    const int magPen  = qBound(2, qRound(videoPart.width() / 640.0), 8);
    const int magFont = qBound(10, qRound(videoPart.width() * 12.0 / 1280.0), 28);
    {
        QPainter p(&videoPart);
        p.setRenderHint(QPainter::Antialiasing);
        // Q3：ROI 矩形/多边形/辅助线按模型颜色全分辨率烧录
        OverlayWidget::burnAnnotations(p, videoPart.size(), videoSize, rotation,
                                       m_roiModel, m_roiModel,
                                       m_guideLineModel, annoPen);
        // Q4：放大镜来源标识框（与屏上同款金色四角括号 + 倍率徽章）
        if (m_magnifier && m_magnifier->currentSourceRect().isValid()) {
            const QRect magRect = OverlayWidget::mapStoredRectToFrame(
                m_magnifier->currentSourceRect(), videoPart.size(), videoSize, rotation);
            if (magRect.isValid() && !magRect.isEmpty())
                OverlayWidget::drawMagnifierIndicator(
                    p, magRect, m_magnifier->zoomLevel(), magPen, magFont);
        }
        p.end();
    }
    {
        // Q5：OSD = 文件名（白色小字）+ 标签（金色）+ 时间码，黑底阴影保证可读
        QPainter p(&videoPart);
        p.setRenderHint(QPainter::Antialiasing);
        const int fs = qBound(14, videoPart.height() / 40, 40);
        const int pad = fs;
        int y = videoPart.height() - pad;
        const QString line2 = (m_calibration.dateKnown
            ? lang("北京时间 ", "Beijing ") : lang("相对时刻 ", "Stream ")) + timeText;
        QStringList lines;
        lines << line2;
        if (!labelText.isEmpty())
            lines << labelText;
        const QString fileName = QFileInfo(m_currentVideoPath).fileName();
        if (!fileName.isEmpty())
            lines << fileName;   // 最上方一行
        for (int i = 0; i < lines.size(); ++i) {
            const QString &txt = lines[i];
            const bool isLabel = (i == 1 && !labelText.isEmpty());
            const bool isFile = (i == lines.size() - 1 && !fileName.isEmpty());
            const int thisFs = isFile ? qMax(10, fs * 3 / 4) : fs;
            p.setFont(fontSans(thisFs, QFont::Bold));
            const QColor fg = isLabel ? QColor(Theme::Accent) : Qt::white;
            p.setPen(QColor(0, 0, 0, 200));
            p.drawText(QRect(pad + 2, y - thisFs * 13 / 10 + 2,
                             videoPart.width() - pad * 2, thisFs * 13 / 10),
                       Qt::AlignLeft | Qt::AlignVCenter, txt);
            p.setPen(fg);
            p.drawText(QRect(pad, y - thisFs * 13 / 10,
                             videoPart.width() - pad * 2, thisFs * 13 / 10),
                       Qt::AlignLeft | Qt::AlignVCenter, txt);
            y -= thisFs * 3 / 2;
        }
        p.end();
    }

    // ---- 分析数据区（§14 v2：离屏重渲染——图表走 CPU 矢量重绘，语谱图走
    // CPU 光栅化，均不动屏幕 widget；dock 内 resize+grab 实测曲线丢失/GL 错位）----
    const AnalysisSnapshot snap = m_timelineModel->snapshot();
    QImage chartImg, specImg, magImg;
    QString magCaption, magSub;
    if (!snap.isEmpty() || snap.hasAudio())
        chartImg = m_chartPanel->renderToImage(QSize(videoPart.width(), 420));
    if (snap.hasAudio() && m_spectrogramEnhanced)
        specImg = m_spectrogramEnhanced->renderHeatmapImage(
            QSize(videoPart.width(), 300));
    if (m_magnifier) {
        const QImage raw = m_magnifier->currentMagnifiedImage();
        if (!raw.isNull()) {
            magImg = raw.scaledToHeight(320, Qt::SmoothTransformation);
            const QRect src = m_magnifier->currentSourceRect();
            magCaption = lang("源区域 (%1, %2) · %3 × %4", "Source (%1, %2) · %3 × %4")
                .arg(src.x()).arg(src.y()).arg(src.width()).arg(src.height());
            magSub = lang("倍率 %1× · 时刻 %2", "Zoom %1× · At %2")
                .arg(m_magnifier->zoomLevel(), 0, 'f', 1).arg(timeText);
        }
    }

    // ---- 竖向全宽堆叠 + 分段标题条（视频 / 曲线 / 语谱图 / 放大镜）----
    struct Section {
        QString title;
        QImage img;
        QString caption;   // 非空 = 放大镜段（图左文右）
        QString sub;
    };
    QVector<Section> sections;
    if (!chartImg.isNull())
        sections << Section{lang("亮度 / 音量曲线", "Luminance / Volume"),
                            chartImg, {}, {}};
    if (!specImg.isNull())
        sections << Section{lang("语谱图", "Spectrogram"), specImg, {}, {}};
    if (!magImg.isNull())
        sections << Section{lang("放大镜视图", "Magnifier"), magImg,
                            magCaption, magSub};

    const int titleH = 34, divH = 2;
    int outH = videoPart.height();
    for (const auto &s : sections)
        outH += divH + titleH + s.img.height();
    QImage out(videoPart.width(), outH, QImage::Format_ARGB32);
    out.fill(Qt::black);
    {
        QPainter p(&out);
        p.setRenderHint(QPainter::Antialiasing);
        const int W = out.width();
        int yy = 0;
        p.drawImage(0, yy, videoPart);
        yy += videoPart.height();
        for (const auto &s : sections) {
            // 分隔线
            p.fillRect(0, yy, W, divH, QColor(0x2A, 0x2A, 0x2A));
            yy += divH;
            // 标题条：Accent 竖条 + 次级色标题
            p.fillRect(14, yy + (titleH - 16) / 2, 4, 16, QColor(Theme::Accent));
            p.setPen(QColor(Theme::TextSecond));
            p.setFont(fontSans(15, QFont::Bold));
            p.drawText(QRect(30, yy, W - 30, titleH),
                       Qt::AlignLeft | Qt::AlignVCenter, s.title);
            yy += titleH;
            if (s.caption.isEmpty()) {
                p.drawImage(0, yy, s.img);
            } else {
                // 放大镜段：裁剪图 + 金色描边 + 右侧信息块（垂直居中两行）
                p.drawImage(0, yy, s.img);
                p.setPen(QPen(QColor(Theme::Accent), 2));
                p.drawRect(1, yy + 1, s.img.width() - 2, s.img.height() - 2);
                const int tx = s.img.width() + 32;
                if (tx + 120 < W) {
                    const int lineH = 34;
                    const int blockH = lineH * 2;
                    int ty = yy + (s.img.height() - blockH) / 2;
                    p.setPen(QColor(Theme::TextPrimary));
                    p.setFont(fontSans(20, QFont::Bold));
                    p.drawText(QRect(tx, ty, W - tx - 24, lineH),
                               Qt::AlignLeft | Qt::AlignVCenter, s.caption);
                    ty += lineH;
                    p.setPen(QColor(Theme::Accent));
                    p.setFont(fontSans(18, QFont::Normal));
                    p.drawText(QRect(tx, ty, W - tx - 24, lineH),
                               Qt::AlignLeft | Qt::AlignVCenter, s.sub);
                }
            }
            yy += s.img.height();
        }
        p.end();
    }
    // 取证留痕：旋转档位写入 PNG 文本元数据（0 不记，保持旧产物字节级一致）
    if (rotation != 0)
        out.setText(QStringLiteral("LumenArc:displayRotation"),
                    QString::number(rotation));

    // ---- 保存：案件 snapshots/ 优先；无案件则视频同目录 snapshots/ ----
    const QString base = QFileInfo(m_currentVideoPath).completeBaseName();
    QString dir;
    const bool inCase = m_caseManager && m_caseManager->isOpen();
    if (inCase)
        dir = m_caseManager->caseDir() + QStringLiteral("/snapshots");
    else if (!m_currentVideoPath.isEmpty())
        dir = QFileInfo(m_currentVideoPath).absolutePath()
              + QStringLiteral("/snapshots");
    else
        dir = QDir::homePath() + QStringLiteral("/LumenArc_Snapshots");
    if (!QDir().mkpath(dir)) {
        showOperationStatus(lang("快照目录创建失败：%1", "Cannot create snapshot dir: %1")
                                .arg(dir));
        return;
    }
    QString path;
    for (int i = 0; ; ++i) {
        const QString name = i == 0
            ? base + QStringLiteral("_") + fileTimeTag + QStringLiteral(".png")
            : base + QStringLiteral("_") + fileTimeTag
              + QStringLiteral("_%1.png").arg(i + 1);
        path = dir + QLatin1Char('/') + name;
        if (!QFile::exists(path))
            break;
    }
    if (!out.save(path, "PNG")) {
        showOperationStatus(lang("快照保存失败：%1", "Snapshot save failed: %1")
                                .arg(path));
        return;
    }
    if (inCase && m_caseDock)
        m_caseDock->refreshTree();   // 案件快照组即时可见（dock 扫描目录驱动）
    showOperationStatus(lang("快照已保存：%1", "Snapshot saved: %1").arg(path)
        + (rotation != 0
            ? lang("（显示旋转 %1°）", " (rotated %1°)").arg(rotation)
            : QString()));
}

void MainWindow::onVideoSelected(int index)
{
    if (index >= 0 && index < m_videoListPanel->videoCount()) {
        // B6: Cancel any running analysis before switching video to prevent
        // stale results being saved under the wrong filename.
        if (m_analysisEngine->isRunning()) {
            m_analysisEngine->cancelAnalysis();
        }
        VideoEntry entry = m_videoListPanel->videoAt(index);
        if (!entry.filePath.isEmpty()) {
            openVideoFile(entry.filePath);
        }
    }
}

// v0.5: 矩形模式
void MainWindow::onRectMode()
{
    m_videoWidget->overlay()->setPolygonMode(false);
    m_videoWidget->overlay()->setGuideLineMode(false);
    if (m_magnifier && m_magnifier->overlay()) {
        m_magnifier->overlay()->setPolygonMode(false);
        m_magnifier->overlay()->setGuideLineMode(false);
    }
    m_rectModeBtn->setChecked(true);
    m_polygonModeBtn->setChecked(false);
    m_guideLineBtn->setChecked(false);
}

// v0.5: 多边形模式
void MainWindow::onPolygonMode()
{
    m_videoWidget->overlay()->setPolygonMode(true);
    if (m_magnifier && m_magnifier->overlay()) {
        m_magnifier->overlay()->setPolygonMode(true);
    }
    m_rectModeBtn->setChecked(false);
    m_polygonModeBtn->setChecked(true);
    m_guideLineBtn->setChecked(false);
}

// v0.5: 辅助线模式
void MainWindow::onGuideLineMode()
{
    m_videoWidget->overlay()->setGuideLineMode(true);
    if (m_magnifier && m_magnifier->overlay()) {
        m_magnifier->overlay()->setGuideLineMode(true);
    }
    m_rectModeBtn->setChecked(false);
    m_polygonModeBtn->setChecked(false);
    m_guideLineBtn->setChecked(true);
}

// v0.5: 复制ROI
void MainWindow::onCopyRoi()
{
    m_roiClipboard = m_roiModel->regions();
    m_polygonClipboard = m_roiModel->polygons();
    m_guideLineClipboard = m_guideLineModel->lines();
    int total = m_roiClipboard.size() + m_polygonClipboard.size();
    if (total > 0) {
        showOperationStatus(lang(QString("已复制 %1 个ROI区域").arg(total),
                                 QString("Copied %1 ROI regions").arg(total)));
        m_pasteRoiBtn->setEnabled(true);
    } else {
        showOperationStatus(lang("没有ROI可复制", "No ROI to copy"));
    }
}

// v0.5: 粘贴ROI
void MainWindow::onPasteRoi()
{
    if (m_roiClipboard.isEmpty() && m_polygonClipboard.isEmpty()) {
        showOperationStatus(lang("剪贴板为空", "Clipboard is empty"));
        return;
    }

    // Check if there's existing analysis data
    AnalysisSnapshot snap = m_timelineModel->snapshot();
    if (!snap.isEmpty()) {
        QMessageBox msgWarn(this);
        msgWarn.setIcon(QMessageBox::Warning);
        msgWarn.setWindowTitle(lang("粘贴ROI", "Paste ROI"));
        msgWarn.setText(lang("当前视频已有亮度分析数据。\n粘贴新的ROI将导致已有数据失效。",
                             "The current video has luminance analysis data.\nPasting new ROI will invalidate existing data."));
        auto continueBtn = msgWarn.addButton(lang("继续", "Continue"), QMessageBox::AcceptRole);
        auto cancelBtn = msgWarn.addButton(QMessageBox::Cancel);
        msgWarn.exec();
        if (msgWarn.clickedButton() == cancelBtn)
            return;
        m_timelineModel->clearLuminanceData();
    }

    QMessageBox msgBox(this);
    msgBox.setIcon(QMessageBox::Question);
    msgBox.setWindowTitle(lang("粘贴ROI", "Paste ROI"));
    msgBox.setText(lang("替换当前ROI还是追加？", "Replace or append current ROI?"));
    auto replaceBtn = msgBox.addButton(lang("替换", "Replace"), QMessageBox::AcceptRole);
    auto appendBtn = msgBox.addButton(lang("追加", "Append"), QMessageBox::AcceptRole);
    auto cancelBtn = msgBox.addButton(QMessageBox::Cancel);
    msgBox.exec();

    if (msgBox.clickedButton() == cancelBtn)
        return;

    if (msgBox.clickedButton() == replaceBtn) {
        m_roiModel->clearRegions();
        m_roiModel->clearPolygons();
        m_guideLineModel->clearLines();
    }

    for (const QRect &rc : m_roiClipboard) {
        m_roiModel->addRegion(rc);
    }
    for (const QPolygon &poly : m_polygonClipboard) {
        m_roiModel->addPolygon(poly);
    }
    for (const GuideLine &line : m_guideLineClipboard) {
        m_guideLineModel->addLine(line);
    }

    m_videoWidget->overlay()->update();

    int total = m_roiClipboard.size() + m_polygonClipboard.size();
    showOperationStatus(lang(QString("已粘贴 %1 个ROI区域").arg(total),
                             QString("Pasted %1 ROI regions").arg(total)));
}

// v0.5: 粘贴ROI到所有视频
void MainWindow::onPasteRoiToAll()
{
    if (m_roiClipboard.isEmpty() && m_polygonClipboard.isEmpty()) {
        showOperationStatus(lang("剪贴板为空", "Clipboard is empty"));
        return;
    }

    // 这里可以扩展为将ROI保存到VideoStateManager
    // 目前先提示用户
    QMessageBox::information(this,
        lang("粘贴到所有视频", "Paste to All Videos"),
        lang("此功能将在后续版本中实现。",
             "This feature will be implemented in a future version."));
}
