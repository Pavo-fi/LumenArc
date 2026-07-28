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
#include "domain/region_model.h"
#include "domain/polygon_model.h"
#include "domain/guide_line_model.h"
#include "domain/timeline_model.h"
#include "infrastructure/ivideo_engine.h"
#include "infrastructure/ianalysis_engine.h"
#include "infrastructure/vlc_video_engine.h"
#include "infrastructure/ffmpeg_video_engine.h"
#include "infrastructure/python_analysis_engine.h"
#include "magnifierwidget.h"
#include "snapshotoverlay.h"
#include "pinnedwidget.h"
#include "videolistpanel.h"
#include "spectrogrampanel.h"
#include "spectrogrampanel_enhanced.h"
#include "i18n.h"
#include "aboutdialog.h"

#include <QListWidget>
#include <QSplitter>
#include <QMenuBar>
#include <QToolBar>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QStatusBar>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
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

/// @brief 构造主窗口：初始化引擎/组件/连接信号槽
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    loadLanguage();
    setWindowTitle(lang("追光者 Lumen Arc v1.0", "Lumen Arc v1.0"));
    resize(1280, 720);

    m_regionModel = new RegionModel(this);
    m_polygonModel = new PolygonModel(this);
    m_guideLineModel = new GuideLineModel(this);
    m_timelineModel = new TimelineModel(this);
    m_stateManager = new VideoStateManager(this);

    // 播放引擎选择（QSettings 持久化，默认 FFmpeg；VLC 保留为后备）
    {
        QSettings engineSettings("LumenArc", "LumenArc");
        QString engineName = engineSettings.value("videoEngine", "ffmpeg").toString();
        if (engineName == "vlc") {
            m_videoEngine = new VlcVideoEngine(this);
        } else {
            auto *ffEngine = new FfmpegVideoEngine(this);
            ffEngine->setHardwareDecode(
                engineSettings.value("hwDecode", true).toBool());
            ffEngine->setHardwareAdapter(
                engineSettings.value("hwAdapter", -1).toInt());
            m_videoEngine = ffEngine;
        }
    }

    m_videoWidget = new VideoWidget(this);
    m_videoWidget->setVideoEngine(m_videoEngine);
    m_videoWidget->setRegionModel(m_regionModel);
    m_videoWidget->setPolygonModel(m_polygonModel);
    m_videoWidget->setGuideLineModel(m_guideLineModel);

    m_chartPanel = new ChartPanel(this);
    m_chartPanel->setRegionModel(m_regionModel);
    m_chartPanel->setPolygonModel(m_polygonModel);
    m_chartPanel->setTimelineModel(m_timelineModel);

    // v0.3: Spectrogram panel below chart
    // v0.4: Use enhanced version with GPU rendering and log frequency
    m_spectrogramEnhanced = new SpectrogramPanelEnhanced(this);
    m_spectrogramPanel = nullptr;  // Not used in enhanced version

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->setHandleWidth(0);
    m_splitter->setChildrenCollapsible(false);
    m_splitter->addWidget(m_videoWidget);
    m_splitter->setStretchFactor(0, 7);  // 44%
    setCentralWidget(m_splitter);

    // --- Shared styles ---
    const QString titleBarStyle = "background: #363636;";
    const QString collapseBtnStyle =
        "QPushButton { background: transparent; color: #aaa; border: none; font-size: 10px; }"
        "QPushButton:hover { color: #ddd; }";
    const QString titleLabelStyle = "color: #F5F0E8; font-size: 11px; background: transparent;";

    // --- Chart container with title bar ---
    m_chartContainer = new QWidget(m_splitter);
    auto *chartContainerLayout = new QVBoxLayout(m_chartContainer);
    chartContainerLayout->setContentsMargins(0, 0, 0, 0);
    chartContainerLayout->setSpacing(0);

    auto *chartTitleBar = new QWidget(m_chartContainer);
    chartTitleBar->setFixedHeight(24);
    chartTitleBar->setStyleSheet(titleBarStyle);
    auto *chartTitleLayout = new QHBoxLayout(chartTitleBar);
    chartTitleLayout->setContentsMargins(4, 0, 4, 0);

    m_chartCollapseBtn = new QPushButton(QString::fromUtf8("\xe2\x96\xbc"), chartTitleBar); // ▼
    m_chartCollapseBtn->setFixedSize(20, 20);
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
    specTitleBar->setFixedHeight(24);
    specTitleBar->setStyleSheet(titleBarStyle);
    auto *specTitleLayout = new QHBoxLayout(specTitleBar);
    specTitleLayout->setContentsMargins(4, 0, 4, 0);

    m_spectrogramCollapseBtn = new QPushButton(QString::fromUtf8("\xe2\x96\xbc"), specTitleBar); // ▼
    m_spectrogramCollapseBtn->setFixedSize(20, 20);
    m_spectrogramCollapseBtn->setStyleSheet(collapseBtnStyle);
    m_spectrogramCollapseBtn->setToolTip(lang("收起语谱图", "Collapse spectrogram"));
    m_spectrogramCollapseBtn->setFocusPolicy(Qt::NoFocus);

    auto *specTitleLabel = new QLabel(lang("语谱图", "Spectrogram"), specTitleBar);
    specTitleLabel->setStyleSheet(titleLabelStyle);

    specTitleLayout->addWidget(m_spectrogramCollapseBtn);
    specTitleLayout->addWidget(specTitleLabel);
    specTitleLayout->addSpacing(12);

    m_noiseFloorLabel = new QLabel(lang("底噪:", "Noise:"), specTitleBar);
    m_noiseFloorLabel->setStyleSheet("QLabel { color: #aaa; font-size: 11px; }");
    specTitleLayout->addWidget(m_noiseFloorLabel);

    m_noiseFloorSlider = new QSlider(Qt::Horizontal, specTitleBar);
    m_noiseFloorSlider->setRange(-100, 0);  // -10.0 to 0.0 dB (x10)
    m_noiseFloorSlider->setValue(-55);       // default -5.5 dB
    m_noiseFloorSlider->setFixedWidth(100);
    m_noiseFloorSlider->setToolTip(lang("底噪阈值: -5.5 dB", "Noise floor threshold: -5.5 dB"));
    m_noiseFloorSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #444; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #00bcd4; width: 12px; margin: -4px 0; border-radius: 6px; }"
        "QSlider::sub-page:horizontal { background: #00bcd4; border-radius: 2px; }"
    );
    specTitleLayout->addWidget(m_noiseFloorSlider);

    m_noiseFloorValueLabel = new QLabel("-5.5", specTitleBar);
    m_noiseFloorValueLabel->setStyleSheet("QLabel { color: #00bcd4; font-size: 11px; font-family: Consolas; min-width: 30px; }");
    specTitleLayout->addWidget(m_noiseFloorValueLabel);

    // Noise reduction slider
    m_noiseReductionLabel = new QLabel(lang("降噪:", "NR:"), specTitleBar);
    m_noiseReductionLabel->setStyleSheet("QLabel { color: #aaa; font-size: 11px; }");
    specTitleLayout->addWidget(m_noiseReductionLabel);

    m_noiseReductionSlider = new QSlider(Qt::Horizontal, specTitleBar);
    m_noiseReductionSlider->setRange(0, 50);  // 0.0 to 5.0 (x10)
    m_noiseReductionSlider->setValue(0);       // default off
    m_noiseReductionSlider->setFixedWidth(80);
    m_noiseReductionSlider->setToolTip(lang("降噪强度（需重新分析）", "Noise reduction (re-analysis needed)"));
    m_noiseReductionSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: #444; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: #ff9800; width: 12px; margin: -4px 0; border-radius: 6px; }"
        "QSlider::sub-page:horizontal { background: #ff9800; border-radius: 2px; }"
    );
    specTitleLayout->addWidget(m_noiseReductionSlider);

    m_noiseReductionValueLabel = new QLabel("0.0", specTitleBar);
    m_noiseReductionValueLabel->setStyleSheet("QLabel { color: #ff9800; font-size: 11px; font-family: Consolas; min-width: 24px; }");
    specTitleLayout->addWidget(m_noiseReductionValueLabel);

    // 安装事件过滤器，使全局快捷键（方向键、空格等）不被 Slider 拦截
    m_noiseFloorSlider->installEventFilter(this);
    m_noiseReductionSlider->installEventFilter(this);

    m_nrApplyBtn = new QPushButton(lang("应用", "Apply"), specTitleBar);
    m_nrApplyBtn->setFixedSize(36, 20);
    m_nrApplyBtn->setFocusPolicy(Qt::NoFocus);
    m_nrApplyBtn->setStyleSheet(
        "QPushButton { background: #555; color: #ff9800; border: 1px solid #ff9800; border-radius: 3px; font-size: 10px; }"
        "QPushButton:hover { background: #666; }"
        "QPushButton:disabled { color: #666; border-color: #666; }"
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
            m_chartContainer->setMinimumHeight(24);
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
            m_spectrogramContainer->setMinimumHeight(24);
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
    m_videoListSidebar->setFixedWidth(24);
    m_videoListSidebar->setStyleSheet("background: #363636;");
    auto *sidebarBarLayout = new QVBoxLayout(m_videoListSidebar);
    sidebarBarLayout->setContentsMargins(2, 4, 2, 4);
    sidebarBarLayout->setSpacing(4);

    m_videoListCollapseBtn = new QPushButton(QString::fromUtf8("\xe2\x97\x80"), m_videoListSidebar); // ◀
    m_videoListCollapseBtn->setFixedSize(20, 20);
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
            chLabel->setStyleSheet("color: #F5F0E8; font-size: 11px; background: transparent;");
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
    placeholderContent->setFixedWidth(24);
    placeholderContent->setStyleSheet("background: #363636;");
    auto *phLayout = new QVBoxLayout(placeholderContent);
    phLayout->setContentsMargins(2, 4, 2, 4);
    phLayout->setSpacing(4);
    auto *phExpandBtn = new QPushButton(QString::fromUtf8("\xe2\x96\xb6"), placeholderContent); // ▶
    phExpandBtn->setFixedSize(20, 20);
    phExpandBtn->setStyleSheet(collapseBtnStyle);
    phExpandBtn->setToolTip(lang("展开视频列表", "Expand video list"));
    phExpandBtn->setFocusPolicy(Qt::NoFocus);
    phLayout->addWidget(phExpandBtn);
    phLayout->addStretch();
    {
        QString vertText = lang("视频列表", "Videos");
        for (const QChar &ch : vertText) {
            auto *chLabel = new QLabel(QString(ch), placeholderContent);
            chLabel->setStyleSheet("color: #F5F0E8; font-size: 11px; background: transparent;");
            chLabel->setAlignment(Qt::AlignCenter);
            phLayout->addWidget(chLabel);
        }
        phLayout->addStretch();
    }
    m_videoListPlaceholder->setWidget(placeholderContent);
    addDockWidget(Qt::LeftDockWidgetArea, m_videoListPlaceholder);
    resizeDocks({m_videoListPlaceholder}, {24}, Qt::Horizontal);
    m_videoListPlaceholder->setVisible(false);

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
    m_operationLabel->setStyleSheet("color: #F5F0E8;");
    m_statusLabel = new QLabel(this);
    m_hwAdapterLabel = new QLabel(this);
    m_hwAdapterLabel->setStyleSheet("color: #888; font-size: 10px;");
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

    // Snapshot overlay (floating on video area)
    m_snapshotOverlay = new SnapshotOverlay(m_videoWidget);
    m_snapshotOverlay->hide();

    createMenus();
    createToolBar();
    setupConnections();
}

MainWindow::~MainWindow()
{
    disconnect(m_analysisEngine, nullptr, this, nullptr);
    m_analysisEngine->cancelAnalysis();
}

/// @brief 创建菜单栏：文件/编辑/导出/帮助
void MainWindow::createMenus()
{
    // Fix menu item left padding (remove unused icon space)
    QString menuStyle =
        "QMenu { padding: 4px 0; }"
        "QMenu::item { padding: 5px 24px 5px 12px; }"
        "QMenu::item:disabled { color: #888; }";
    menuBar()->setStyleSheet(menuBar()->styleSheet() + menuStyle);

    QMenu *fileMenu = menuBar()->addMenu(lang("文件(&F)", "&File"));
    fileMenu->addAction(lang("打开视频(&O)...", "&Open Video..."), this, &MainWindow::onOpenFile, QKeySequence::Open);
    fileMenu->addAction(lang("加载图片为叠加(&I)...", "Load Image as &Overlay..."), this, &MainWindow::onLoadOverlayImage);
    fileMenu->addSeparator();
    fileMenu->addAction(lang("保存分析结果(&S)...", "&Save Analysis Result..."), this, &MainWindow::onSaveAnalysis, QKeySequence::Save);
    fileMenu->addAction(lang("加载分析结果(&L)...", "&Load Analysis Result..."), this, &MainWindow::onLoadAnalysis, QKeySequence::Open);
    fileMenu->addSeparator();
    fileMenu->addAction(lang("退出(&X)", "E&xit"), this, &QWidget::close, QKeySequence::Quit);

    QMenu *editMenu = menuBar()->addMenu(lang("编辑(&E)", "&Edit"));
    editMenu->addAction(lang("清除选区(&R)", "Clear &Regions"), this, &MainWindow::onClearRegions);
    editMenu->addAction(lang("清除数据(&D)", "Clear &Data"), this, &MainWindow::onClearData);

    QMenu *exportMenu = menuBar()->addMenu(lang("导出(&X)", "&Export"));
    exportMenu->addAction(lang("导出为 CSV(&C)...", "Export to &CSV..."), this, &MainWindow::onExportCsv);

    // Settings menu
    QMenu *settingsMenu = menuBar()->addMenu(lang("设置(&S)", "&Settings"));
    QMenu *engineMenu = settingsMenu->addMenu(lang("播放内核（重启生效）", "Playback Engine (restart required)"));
    QActionGroup *engineGroup = new QActionGroup(this);
    QAction *ffmpegAction = engineMenu->addAction("FFmpeg");
    QAction *vlcAction = engineMenu->addAction("VLC");
    for (QAction *a : {ffmpegAction, vlcAction}) {
        a->setCheckable(true);
        engineGroup->addAction(a);
    }
    {
        QSettings s("LumenArc", "LumenArc");
        QString cur = s.value("videoEngine", "ffmpeg").toString();
        (cur == "vlc" ? vlcAction : ffmpegAction)->setChecked(true);
    }
    connect(ffmpegAction, &QAction::triggered, this, [this]() {
        QSettings s("LumenArc", "LumenArc");
        s.setValue("videoEngine", "ffmpeg");
        showOperationStatus(lang("播放内核将在重启后切换为 FFmpeg", "Engine will switch to FFmpeg after restart"));
    });
    connect(vlcAction, &QAction::triggered, this, [this]() {
        QSettings s("LumenArc", "LumenArc");
        s.setValue("videoEngine", "vlc");
        showOperationStatus(lang("播放内核将在重启后切换为 VLC", "Engine will switch to VLC after restart"));
    });

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
        QString path = QCoreApplication::applicationDirPath() + "/追光者 Lumen Arc v0.5 Beta — 操作手册.pdf";
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
                {"P", "进入多边形模式"}, {"G", "进入辅助线模式"},
                {"Delete", "删除选中的 ROI / 辅助线"},
                {"右键", "删除鼠标下的 ROI / 辅助线（无需先选中）"},
                {"Esc", "关闭放大镜 / 退出当前模式"},
                {"Ctrl+S", "保存分析结果"},
            };
        } else {
            shortcuts = {
                {"Space / K", "Play / Pause"}, {"← / →", "Prev / Next Frame"},
                {"↑ / ↓", "Volume Up / Down"}, {"C / L", "Speed Up"}, {"X / J", "Slow Down"},
                {"Z", "Reset to 1x"}, {"N", "Add Label at Current Position"},
                {"A", "Set A Point"}, {"B", "Set B Point"},
                {"P", "Enter Polygon Mode"}, {"G", "Enter Guide Line Mode"},
                {"Delete", "Delete Selected ROI / Guide Line"},
                {"Right-click", "Delete ROI / Guide Line under cursor (no selection needed)"},
                {"Esc", "Close Magnifier / Exit Mode"},
                {"Ctrl+S", "Save Analysis"},
            };
        }

        QDialog *dlg = new QDialog(this);
        dlg->setWindowTitle(lang("快捷键速查", "Keyboard Shortcuts"));
        dlg->setWindowOpacity(0.75);
        dlg->setFixedSize(420, 520);
        dlg->setStyleSheet("QDialog { background: #1e1e2e; }");

        QVBoxLayout *layout = new QVBoxLayout(dlg);
        layout->setContentsMargins(16, 16, 16, 16);
        layout->setSpacing(8);

        QLabel *title = new QLabel(lang("⌨ 快捷键速查", "⌨ Keyboard Shortcuts"));
        title->setStyleSheet("color: #cdd6f4; font-size: 15px; font-weight: bold; padding: 4px 0;");
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
            "QTableWidget { background: #1e1e2e; border: none; color: #cdd6f4; font-size: 12px; }"
            "QTableWidget::item { padding: 4px 8px; border-bottom: 1px solid #313244; }"
            "QHeaderView::section { background: #1e1e2e; border: none; }"
        );
        for (int i = 0; i < shortcuts.size(); ++i) {
            QTableWidgetItem *keyItem = new QTableWidgetItem(shortcuts[i].key);
            keyItem->setForeground(QBrush(QColor("#89b4fa")));
            keyItem->setFont(QFont("Consolas", 11, QFont::Bold));
            table->setItem(i, 0, keyItem);
            QTableWidgetItem *descItem = new QTableWidgetItem(shortcuts[i].desc);
            descItem->setForeground(QBrush(QColor("#bac2de")));
            table->setItem(i, 1, descItem);
        }
        table->setRowHeight(shortcuts.size(), 0);
        layout->addWidget(table);

        QLabel *hint = new QLabel(lang("按 Esc 或点击 ✕ 关闭", "Press Esc or click ✕ to close"));
        hint->setStyleSheet("color: #585b70; font-size: 11px; padding: 4px 0;");
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
    // --- Unified button style ---
    const QString btnBase =
        "QPushButton {"
        "  height: 30px; border: 1px solid #888; border-radius: 4px;"
        "  background: #555; color: #eee;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei'; font-size: 12px;"
        "  padding: 0 8px;"
        "}"
        "QPushButton:hover { background: #666; }"
        "QPushButton:pressed { background: #444; }"
        "QPushButton:disabled { background: #444; color: #888; border: 1px solid #555; }";

    // Fusion buttons: orange accent when enabled, orange bg when checked
    const QString fusionBtnStyle =
        "QPushButton {"
        "  height: 30px; border: 1px solid #FF9800; border-radius: 4px;"
        "  background: #555; color: #FF9800;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei'; font-size: 12px;"
        "  font-weight: bold;"
        "  padding: 0 8px;"
        "}"
        "QPushButton:hover { background: #666; color: #FFB74D; }"
        "QPushButton:pressed { background: #444; color: #F57C00; }"
        "QPushButton:disabled { background: #444; color: #888; border: 1px solid #555; }"
        "QPushButton:checked { background: #FF9800; color: #fff; border: 1px solid #F57C00; }"
        "QPushButton:checked:hover { background: #FFB74D; color: #fff; }"
        "QPushButton:checked:pressed { background: #F57C00; color: #fff; }";

    const QString iconBtnStyle =
        "QPushButton {"
        "  width: 30px; height: 30px; border: 1px solid #888; border-radius: 4px;"
        "  background: #555; padding: 0;"
        "}"
        "QPushButton:hover { background: #666; }"
        "QPushButton:pressed { background: #444; }"
        "QPushButton:disabled { background: #444; border: 1px solid #555; }";

    const QString speedBtnStyle =
        "QPushButton {"
        "  width: 40px; height: 30px; border: 1px solid #888; border-radius: 4px;"
        "  background: #555; color: #00bcd4; font-weight: bold;"
        "  font-family: 'Consolas', monospace; font-size: 12px; padding: 0;"
        "}"
        "QPushButton:hover { background: #666; }"
        "QPushButton:pressed { background: #444; }"
        "QPushButton:disabled { background: #444; color: #666; border: 1px solid #555; }";

    const QString primaryBtnStyle =
        "QPushButton {"
        "  height: 30px; border: none; border-radius: 4px;"
        "  background: #2196F3; color: #fff;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei'; font-size: 12px; font-weight: bold;"
        "  padding: 0 14px; min-width: 80px;"
        "}"
        "QPushButton:hover { background: #1976D2; }"
        "QPushButton:pressed { background: #0D47A1; }"
        "QPushButton:disabled { background: #555; color: #888; }";

    const QString timeLabelStyle =
        "QLabel { font-family: 'Consolas', monospace; font-size: 12px; color: #ccc; padding: 0 6px; }";

    QToolBar *toolBar = addToolBar("Main");
    toolBar->setIconSize(QSize(16, 16));
    toolBar->setStyleSheet(
        "QToolBar { spacing: 4px; padding: 2px; background: #333; border: none; }"
        "QToolBar::separator { width: 1px; background: #666; margin: 4px 4px; }"
    );

    // --- Playback group ---
    m_playBtn = new QPushButton(this);
    m_playBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPlay));
    m_playBtn->setToolTip(lang("播放", "Play"));
    m_playBtn->setFixedSize(30, 30);
    m_playBtn->setIconSize(QSize(14, 14));
    m_playBtn->setStyleSheet(iconBtnStyle);
    m_playBtn->setEnabled(false);

    m_pauseBtn = new QPushButton(this);
    m_pauseBtn->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
    m_pauseBtn->setToolTip(lang("暂停", "Pause"));
    m_pauseBtn->setFixedSize(30, 30);
    m_pauseBtn->setIconSize(QSize(14, 14));
    m_pauseBtn->setStyleSheet(iconBtnStyle);
    m_pauseBtn->setEnabled(false);

    m_stopBtn = new QPushButton(this);
    m_stopBtn->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
    m_stopBtn->setToolTip(lang("停止", "Stop"));
    m_stopBtn->setFixedSize(30, 30);
    m_stopBtn->setIconSize(QSize(14, 14));
    m_stopBtn->setStyleSheet(iconBtnStyle);
    m_stopBtn->setEnabled(false);

    m_speedBtn = new QPushButton("1x", this);
    m_speedBtn->setToolTip(lang("倍速播放 (1x/2x/4x/8x)", "Playback speed (1x/2x/4x/8x)"));
    m_speedBtn->setFixedSize(40, 30);
    m_speedBtn->setStyleSheet(speedBtnStyle);
    m_speedBtn->setEnabled(false);

    // A/B loop button
    m_loopBtn = new QPushButton(QString::fromUtf8("\xf0\x9f\x94\x81"), this); // 🔁
    m_loopBtn->setToolTip(lang("循环播放 A-B 区域", "Loop A-B region"));
    m_loopBtn->setFixedSize(30, 30);
    m_loopBtn->setStyleSheet(iconBtnStyle);
    m_loopBtn->setCheckable(true);
    m_loopBtn->setEnabled(false);

    // --- Analyze group ---
    m_analyzeBtn = new QPushButton(lang("亮度分析", "Luminance"), this);
    m_analyzeBtn->setToolTip(lang("分析当前视频的亮度（无需播放）", "Analyze current video luminance (no playback needed)"));
    m_analyzeBtn->setFixedHeight(30);
    m_analyzeBtn->setStyleSheet(primaryBtnStyle);
    m_analyzeBtn->setEnabled(false);

    // v0.3: Audio analysis button (teal)
    const QString audioBtnStyle =
        "QPushButton {"
        "  height: 30px; border: none; border-radius: 4px;"
        "  background: #00897B; color: #fff;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei'; font-size: 12px; font-weight: bold;"
        "  padding: 0 14px; min-width: 80px;"
        "}"
        "QPushButton:hover { background: #00796B; }"
        "QPushButton:pressed { background: #00695C; }"
        "QPushButton:disabled { background: #555; color: #888; }";
    m_audioAnalysisBtn = new QPushButton(lang("音频分析", "Audio"), this);
    m_audioAnalysisBtn->setToolTip(lang("独立分析音频（频谱图+音量）", "Analyze audio only (spectrogram + volume)"));
    m_audioAnalysisBtn->setFixedHeight(30);
    m_audioAnalysisBtn->setStyleSheet(audioBtnStyle);
    m_audioAnalysisBtn->setEnabled(false);

    m_setTimeBtn = new QPushButton(lang("设置时间", "Set Time"), this);
    m_setTimeBtn->setToolTip(lang("设置图表起始时间 (HH:MM:SS)", "Set start time for the chart axis (HH:MM:SS)"));
    m_setTimeBtn->setFixedHeight(30);
    m_setTimeBtn->setStyleSheet(btnBase);
    m_setTimeBtn->setEnabled(false);

    // --- Fusion group ---
    m_captureBtn = new QPushButton(lang("截取", "Capture"), this);
    m_captureBtn->setToolTip(lang("截取当前帧", "Capture current frame"));
    m_captureBtn->setFixedHeight(30);
    m_captureBtn->setStyleSheet(fusionBtnStyle);
    m_captureBtn->setEnabled(false);

    m_editBtn = new QPushButton(lang("编辑", "Edit"), this);
    m_editBtn->setToolTip(lang("编辑截图叠加", "Edit snapshot overlay"));
    m_editBtn->setFixedHeight(30);
    m_editBtn->setStyleSheet(fusionBtnStyle);
    m_editBtn->setEnabled(false);

    m_placeBtn = new QPushButton(lang("放置", "Place"), this);
    m_placeBtn->setToolTip(lang("切换叠加显示", "Toggle overlay on/off"));
    m_placeBtn->setFixedHeight(30);
    m_placeBtn->setStyleSheet(fusionBtnStyle);
    m_placeBtn->setCheckable(true);
    m_placeBtn->setEnabled(false);

    // v0.5: ROI模式按钮
    const QString modeBtnStyle =
        "QPushButton {"
        "  height: 30px; border: 1px solid #888; border-radius: 4px;"
        "  background: #555; color: #eee;"
        "  font-family: 'Segoe UI', 'Microsoft YaHei'; font-size: 12px;"
        "  padding: 0 8px;"
        "}"
        "QPushButton:hover { background: #666; }"
        "QPushButton:pressed { background: #444; }"
        "QPushButton:disabled { background: #444; color: #888; border: 1px solid #555; }"
        "QPushButton:checked { background: #2196F3; color: #fff; border: 1px solid #1976D2; }";

    m_rectModeBtn = new QPushButton(lang("矩形", "Rect"), this);
    m_rectModeBtn->setToolTip(lang("矩形ROI模式 (P)", "Rect ROI Mode (P)"));
    m_rectModeBtn->setFixedHeight(30);
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
    m_polygonModeBtn->setFixedHeight(30);
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
    m_guideLineBtn->setFixedHeight(30);
    m_guideLineBtn->setStyleSheet(modeBtnStyle);
    m_guideLineBtn->setCheckable(true);

    m_copyRoiBtn = new QPushButton(lang("复制ROI", "Copy ROI"), this);
    m_copyRoiBtn->setToolTip(lang("复制ROI区域 (Ctrl+Shift+C)", "Copy ROI (Ctrl+Shift+C)"));
    m_copyRoiBtn->setFixedHeight(30);
    m_copyRoiBtn->setStyleSheet(btnBase);

    m_pasteRoiBtn = new QPushButton(lang("粘贴ROI", "Paste ROI"), this);
    m_pasteRoiBtn->setToolTip(lang("粘贴ROI区域 (Ctrl+Shift+V)", "Paste ROI (Ctrl+Shift+V)"));
    m_pasteRoiBtn->setFixedHeight(30);
    m_pasteRoiBtn->setStyleSheet(btnBase);
    m_pasteRoiBtn->setEnabled(false);

    m_chartPanel->setAutoYRange(true);

    // --- Layout ---
    toolBar->addWidget(m_playBtn);
    toolBar->addWidget(m_pauseBtn);
    toolBar->addWidget(m_stopBtn);
    toolBar->addWidget(m_speedBtn);
    toolBar->addWidget(m_loopBtn);
    toolBar->addSeparator();
    toolBar->addWidget(m_analyzeBtn);
    toolBar->addWidget(m_audioAnalysisBtn);  // v0.3: Audio analysis
    toolBar->addWidget(m_setTimeBtn);
    toolBar->addSeparator();
    toolBar->addWidget(m_rectModeBtn);
    toolBar->addWidget(m_polygonModeBtn);
    toolBar->addWidget(m_guideLineBtn);
    toolBar->addSeparator();
    toolBar->addWidget(m_copyRoiBtn);
    toolBar->addWidget(m_pasteRoiBtn);
    toolBar->addSeparator();
    toolBar->addWidget(m_captureBtn);
    toolBar->addWidget(m_editBtn);
    toolBar->addWidget(m_placeBtn);
    toolBar->addSeparator();

    m_timeLabel = new QLabel("00:00 / 00:00", this);
    m_timeLabel->setStyleSheet(timeLabelStyle);
    toolBar->addWidget(m_timeLabel);

    // Prevent toolbar buttons from stealing keyboard focus
    for (auto *btn : toolBar->findChildren<QPushButton*>()) {
        btn->setFocusPolicy(Qt::NoFocus);
    }
}

/// @brief 连接所有信号槽：引擎→UI更新/按钮→槽/截图同步
void MainWindow::setupConnections()
{
    connect(m_playBtn, &QPushButton::clicked, this, &MainWindow::onPlay);
    connect(m_pauseBtn, &QPushButton::clicked, this, &MainWindow::onPause);
    connect(m_stopBtn, &QPushButton::clicked, this, &MainWindow::onStop);
    connect(m_speedBtn, &QPushButton::clicked, this, [this]() {
        adjustSpeed(1.0f);
    });
    connect(m_loopBtn, &QPushButton::clicked, this, [this](bool checked) {
        m_chartPanel->setABLoop(checked);
        showOperationStatus(checked
            ? lang("循环播放已开启", "Loop enabled")
            : lang("循环播放已关闭", "Loop disabled"));
    });
    connect(m_chartPanel, &ChartPanel::abRegionChanged, this, [this]() {
        bool hasRegion = m_chartPanel->isABRegionSet();
        m_loopBtn->setEnabled(hasRegion);
        m_loopBtn->setChecked(hasRegion);
    });
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
                    int roiId = m_regionModel->roiIdAt(regionIndex);
                    if (roiId > 0 && snapshot.dataIndexOfRoiId(roiId, DataEntry::Rect) >= 0) {
                        auto reply = QMessageBox::question(this,
                            lang("数据失效警告", "Data Invalidation Warning"),
                            lang("调整该区域将导致亮度量化数据失效。\n确定要继续吗？",
                                 "Adjusting this region will invalidate the luminance analysis data.\nContinue?"),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                        if (reply == QMessageBox::Yes) {
                            m_timelineModel->removeRegionDataByRoiId(roiId, DataEntry::Rect);
                        } else {
                            m_regionModel->updateRegion(regionIndex, originalRect);
                        }
                    }
                }
            });

    // Bug fix: When an ROI region is deleted, remove its corresponding analysis data
    // to prevent stale curves from appearing when a new ROI is drawn at the same index.
    connect(m_regionModel, &RegionModel::regionRemoved,
            this, [this](int index, int roiId) {
                Q_UNUSED(index);
                AnalysisSnapshot snapshot = m_timelineModel->snapshot();
                if (!snapshot.isEmpty()) {
                    m_timelineModel->removeRegionDataByRoiId(roiId, DataEntry::Rect);
                }
            });

    // When a polygon ROI is deleted, remove its data by ROI ID
    connect(m_polygonModel, &PolygonModel::polygonRemoved,
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
                    int roiId = m_polygonModel->roiIdAt(polygonIndex);
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
                            m_polygonModel->updatePolygon(polygonIndex, originalPolygon);
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
                m_videoEngine->stop();
                removeMagnifier();
                m_currentVideoPath.clear();
                m_trustedDurationMs = 0;
                m_currentDurationMs = 0;

                m_regionModel->clearRegions();
                m_polygonModel->clearPolygons();
                m_guideLineModel->clearLines();
                m_timelineModel->clearData();
                if (m_spectrogramEnhanced)
                    m_spectrogramEnhanced->clear();

                m_chartPanel->setLabels({});
                m_chartPanel->setTimeOffset(0);
                m_chartPanel->clearAB();
                m_pinnedRect = QRect();
                m_snapshotFusion = SnapshotFusionData();
                if (m_snapshotOverlay)
                    m_snapshotOverlay->clearSnapshot();
                if (m_videoWidget) {
                    m_videoWidget->clearSnapshot();
                    m_videoWidget->clearFrame();
                }

                m_playBtn->setEnabled(false);
                m_pauseBtn->setEnabled(false);
                m_stopBtn->setEnabled(false);
                m_speedBtn->setEnabled(false);
                m_analyzeBtn->setEnabled(false);
                m_audioAnalysisBtn->setEnabled(false);
                m_setTimeBtn->setEnabled(false);
                m_captureBtn->setEnabled(false);
                m_loopBtn->setEnabled(false);

                setWindowTitle(lang("追光者 Lumen Arc v1.0", "Lumen Arc v1.0"));
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
    connect(m_regionModel, &RegionModel::regionsChanged, this, [this]() {
        m_pasteRoiBtn->setEnabled(!m_roiClipboard.isEmpty() || !m_polygonClipboard.isEmpty());
    });
    connect(m_polygonModel, &PolygonModel::polygonsChanged, this, [this]() {
        m_pasteRoiBtn->setEnabled(!m_roiClipboard.isEmpty() || !m_polygonClipboard.isEmpty());
    });
}

/**
 * @brief 5级Python检测：内嵌→环境变量→注册表→常见路径→py.exe
 */
QString MainWindow::detectPythonPath() const
{
    QString appDir = QCoreApplication::applicationDirPath();

#ifdef Q_OS_WIN
    // 0. Bundled Python (Windows)
    QString bundledPy = appDir + "/python/python.exe";
    if (QFile::exists(bundledPy))
        return QDir::toNativeSeparators(bundledPy);
#endif

#ifdef Q_OS_MACOS
    // 0. Bundled Python (inside .app bundle)
    QString bundledPy = appDir + "/python/bin/python3";
    if (QFile::exists(bundledPy))
        return bundledPy;
#endif

    // 1. Environment variable (cross-platform)
    QString env = qEnvironmentVariable("PYTHON_PATH");
    if (!env.isEmpty() && QFile::exists(env))
        return env;

#ifdef Q_OS_WIN
    // 2. Registry-based detection via QSettings
    QStringList registryKeys = {
        "HKEY_CURRENT_USER\\Software\\Python\\PythonCore",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore"
    };
    for (const QString &regKey : registryKeys) {
        QSettings settings(regKey, QSettings::NativeFormat);
        QStringList versions = settings.childGroups();
        std::sort(versions.begin(), versions.end(), std::greater<QString>());
        for (const QString &ver : versions) {
            settings.beginGroup(ver);
            QString installPath = settings.value("InstallPath").toString();
            settings.endGroup();
            if (!installPath.isEmpty()) {
                QString pyPath = installPath + "/python.exe";
                if (QFile::exists(pyPath))
                    return QDir::toNativeSeparators(pyPath);
                pyPath = installPath + "/python3.exe";
                if (QFile::exists(pyPath))
                    return QDir::toNativeSeparators(pyPath);
            }
        }
    }

    // 3. Common Windows install paths
    QStringList winCandidates = {
        "C:/Python313/python.exe",
        "C:/Python312/python.exe",
        "C:/Python311/python.exe",
        "C:/Python310/python.exe",
        "C:/Program Files/Python313/python.exe",
        "C:/Program Files/Python312/python.exe",
        "C:/Program Files/Python311/python.exe",
        "C:/Program Files/Python310/python.exe",
        "python.exe"
    };
    for (const QString &c : winCandidates) {
        if (QFile::exists(c))
            return c;
    }

    // 4. Windows py.exe launcher
    QProcess probe;
    probe.start("py", {"-3", "-c", "import sys; print(sys.executable)"});
    if (probe.waitForFinished(3000) && probe.exitCode() == 0) {
        QString pyPath = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
        if (!pyPath.isEmpty() && QFile::exists(pyPath))
            return pyPath;
    }
#endif

#ifdef Q_OS_MACOS
    // 2. which python3
    QProcess probe;
    probe.start("which", {"python3"});
    if (probe.waitForFinished(3000) && probe.exitCode() == 0) {
        QString pyPath = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
        if (!pyPath.isEmpty() && QFile::exists(pyPath))
            return pyPath;
    }

    // 3. Common macOS install paths
    QStringList macCandidates = {
        "/opt/homebrew/bin/python3",
        "/usr/local/bin/python3",
        "/usr/bin/python3"
    };
    for (const QString &c : macCandidates) {
        if (QFile::exists(c))
            return c;
    }
#endif

    return QString();
}

qint64 MainWindow::trustedDurationFor(const QString &path) const
{
    auto *pyEngine = qobject_cast<PythonAnalysisEngine *>(m_analysisEngine);
    if (!pyEngine)
        return 0;

    auto info = pyEngine->getVideoInfo(path);
    if (info.fps <= 0.0f || info.totalFrames <= 0)
        return 0;

    return static_cast<qint64>((static_cast<qreal>(info.totalFrames) / info.fps) * 1000.0);
}

void MainWindow::onOpenFile()
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
        // Get video info (fps + total frames -> duration)
        auto *pyEngine = qobject_cast<PythonAnalysisEngine *>(m_analysisEngine);
        float fps = 30.0f;
        qint64 durationMs = 0;
        if (pyEngine) {
            auto info = pyEngine->getVideoInfo(path);
            fps = info.fps;
            if (fps > 0 && info.totalFrames > 0)
                durationMs = static_cast<qint64>((info.totalFrames / fps) * 1000.0);
        }
        if (fps <= 0) fps = 30.0f;
        m_videoListPanel->addVideo(path, durationMs, fps);

        if (i == 0) {
            openVideoFile(path);
            // VLC fallback: if getVideoInfo failed, use VLC's duration
            if (durationMs <= 0) {
                qint64 vlcDur = m_videoEngine->duration();
                if (vlcDur > 0) {
                    m_videoListPanel->updateDuration(path, vlcDur);
                }
            }
        }
    }
}

/// @brief 打开视频文件：加载/缓存检测/按钮启用
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
    qint64 timeOffset = 0;
    QRect magnifierRect;
    QVector<ChartLabel> labels;
    QRect pinnedRect;
    SnapshotFusionData snapshotFusion;
    if (m_timelineModel->loadFromFile(filePath, &regions, &timeOffset,
                                       &magnifierRect, &labels, &pinnedRect,
                                       &snapshotFusion, &loadedPolygons, &loadedGuideLines)) {
        restoreAnalysisState(regions, timeOffset, labels, pinnedRect, snapshotFusion);
        m_polygonModel->clearPolygons();
        for (const QPolygon &poly : loadedPolygons)
            m_polygonModel->addPolygon(poly);
        m_guideLineModel->clearLines();
        for (const GuideLine &line : loadedGuideLines)
            m_guideLineModel->addLine(line);

        // Do NOT overwrite m_currentVideoPath with the .vla path: it is an
        // analysis file, not a playable video, and it keys VideoStateManager.
        setWindowTitle("Lumen Arc v1.0 - [Loaded: " +
                           QFileInfo(filePath).fileName() + "]");
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
            m_regionModel->regions(),
            m_chartPanel->timeOffset(),
            magRect,
            m_chartPanel->labels(),
            m_pinnedRect,
            m_snapshotFusion,
            m_chartPanel->abPointA(),
            m_chartPanel->abPointB(),
            m_chartPanel->isABLoop(),
            m_polygonModel->polygons(),
            m_guideLineModel->lines()
        );
    }

    removeMagnifier();
    m_currentVideoPath = filePath;
    m_trustedDurationMs = trustedDurationFor(filePath);
    m_currentDurationMs = 0;  // 等待 durationChanged 校准

    if (m_videoEngine->load(filePath)) {
        // Check if we have a saved state for this video (memory state takes priority)
        VideoState savedState;
        if (m_stateManager->restoreState(filePath, savedState)) {
            m_regionModel->clearRegions();
            for (const QRect &rc : savedState.regions)
                m_regionModel->addRegion(rc);

            m_polygonModel->clearPolygons();
            for (const QPolygon &poly : savedState.polygons)
                m_polygonModel->addPolygon(poly);

            m_guideLineModel->clearLines();
            for (const GuideLine &line : savedState.guideLines)
                m_guideLineModel->addLine(line);

            m_timelineModel->setData(
                QVector<qint64>(savedState.snapshot.timestamps),
                QVector<QVector<qreal>>(savedState.snapshot.values),
                QVector<DataEntry>(savedState.snapshot.dataEntries),
                savedState.snapshot.audio
            );

            m_chartPanel->setTimeOffset(savedState.timeOffsetMs);
            m_chartPanel->setLabels(savedState.labels);

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

            onPlay();
            return;
        }

        // No saved state, clear data and check for .vla cache
        m_regionModel->clearRegions();
        m_polygonModel->clearPolygons();
        m_guideLineModel->clearLines();
        m_timelineModel->clearData();
        if (m_spectrogramEnhanced)
            m_spectrogramEnhanced->clear();

        // Also reset chart-level state so labels/time axis/A-B/pinned/fusion
        // from the previous video do not leak into this one.
        m_chartPanel->setLabels({});
        m_chartPanel->setTimeOffset(0);
        m_chartPanel->clearAB();
        m_pinnedRect = QRect();
        m_snapshotFusion = SnapshotFusionData();
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

        // Check for cached .vla file alongside the video
        QString vlaPath = filePath + ".vla";
        if (QFile::exists(vlaPath)) {
            auto reply = QMessageBox::question(this,
                lang("找到缓存的分析结果", "Cached Analysis Found"),
                lang("找到此视频已保存的分析结果。\n"
                     "是否直接加载而无需重新分析？\n\n",
                     "A saved analysis result was found for this video.\n"
                     "Would you like to load it instead of re-analyzing?\n\n") + vlaPath,
                QMessageBox::Yes | QMessageBox::No, QMessageBox::Yes);
            if (reply == QMessageBox::Yes) {
                QVector<QRect> regions;
                QVector<QPolygon> loadedPolygons;
                QVector<GuideLine> loadedGuideLines;
                qint64 timeOffset = 0;
                QRect magnifierRect;
                QVector<ChartLabel> labels;
                QRect pinnedRect;
                SnapshotFusionData snapshotFusion;
                if (m_timelineModel->loadFromFile(vlaPath, &regions, &timeOffset,
                                                    &magnifierRect, &labels, &pinnedRect,
                                                    &snapshotFusion, &loadedPolygons, &loadedGuideLines)) {
                    restoreAnalysisState(regions, timeOffset, labels, pinnedRect, snapshotFusion);
                    m_polygonModel->clearPolygons();
                    for (const QPolygon &poly : loadedPolygons)
                        m_polygonModel->addPolygon(poly);
                    m_guideLineModel->clearLines();
                    for (const GuideLine &line : loadedGuideLines)
                        m_guideLineModel->addLine(line);
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

    QString defaultPath = m_currentVideoPath;
    if (defaultPath.isEmpty())
        defaultPath = "analysis_result.vla";
    else if (!defaultPath.endsWith(".vla"))
        defaultPath += ".vla";

    QString filePath = QFileDialog::getSaveFileName(this,
        lang("保存分析结果", "Save Analysis Result"), defaultPath,
        lang("VLA 文件 (*.vla)", "VLA Files (*.vla)"));
    if (filePath.isEmpty())
        return;

    QRect magnifierRect = m_magnifier ? m_magnifier->currentSourceRect() : QRect();
    if (m_timelineModel->saveToFile(filePath, m_regionModel->regions(),
                                     m_chartPanel->timeOffset(),
                                     magnifierRect,
                                     m_chartPanel->labels(),
                                     m_pinnedRect,
                                     m_snapshotFusion,
                                     m_polygonModel->polygons(),
                                     m_guideLineModel->lines())) {
        // Save spectrogram to separate binary file
        AnalysisSnapshot snap = m_timelineModel->snapshot();
        if (snap.hasAudio()) {
            TimelineModel::saveSpecToFile(filePath + ".spec", snap.audio);
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
    QString filePath = QFileDialog::getOpenFileName(this,
        "Load Analysis Result", QString(),
        "VLA Files (*.vla);;All Files (*)");
    if (filePath.isEmpty())
        return;

        QVector<QRect> regions;
        QVector<QPolygon> loadedPolygons;
        QVector<GuideLine> loadedGuideLines;
        qint64 timeOffset = 0;
        QRect magnifierRect;
        QVector<ChartLabel> labels;
        QRect pinnedRect;
        SnapshotFusionData snapshotFusion;
        if (m_timelineModel->loadFromFile(filePath, &regions, &timeOffset,
                                            &magnifierRect, &labels, &pinnedRect,
                                            &snapshotFusion, &loadedPolygons, &loadedGuideLines)) {
            restoreAnalysisState(regions, timeOffset, labels, pinnedRect, snapshotFusion);
            m_polygonModel->clearPolygons();
            for (const QPolygon &poly : loadedPolygons)
                m_polygonModel->addPolygon(poly);
            m_guideLineModel->clearLines();
            for (const GuideLine &line : loadedGuideLines)
                m_guideLineModel->addLine(line);

            // Do NOT overwrite m_currentVideoPath with the .vla path (see openVideoFile).
        setWindowTitle("Lumen Arc v1.0 - [Loaded: " +
                       QFileInfo(filePath).fileName() + "]");
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

void MainWindow::onSetStartTime()
{
    bool ok = false;
    QString text = QInputDialog::getText(this,
        lang("设置时间", "Set Time"),
        lang("请输入当前真实时间（HH:MM:SS）：",
             "Enter current real-world time (HH:MM:SS):"),
        QLineEdit::Normal, "00:00:00", &ok);
    if (!ok || text.isEmpty())
        return;

    QTime time = QTime::fromString(text, "hh:mm:ss");
    if (!time.isValid()) {
        QMessageBox::warning(this,
            lang("时间格式无效", "Invalid Time"),
            lang("请输入 HH:MM:SS 格式的时间。",
                 "Please enter time in HH:MM:SS format."));
        return;
    }

    qint64 inputMs = time.hour() * 3600000LL +
                     time.minute() * 60000LL +
                     time.second() * 1000LL;
    qint64 currentPos = m_videoEngine ? m_videoEngine->position() : 0;
    qint64 offset = inputMs - currentPos;
    m_chartPanel->setTimeOffset(offset);
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
        auto *pyEngine = qobject_cast<PythonAnalysisEngine *>(m_analysisEngine);
        if (pyEngine) {
            auto info = pyEngine->getVideoInfo(filePath);
            fps = info.fps;
            if (fps > 0 && info.totalFrames > 0)
                durationMs = static_cast<qint64>((info.totalFrames / fps) * 1000.0);
        }
        if (fps <= 0) fps = 30.0f;

        m_videoListPanel->addVideo(filePath, durationMs, fps);

        if (first) {
            openVideoFile(filePath);
            // VLC fallback: if getVideoInfo failed, use VLC's duration
            if (durationMs <= 0) {
                qint64 vlcDur = m_videoEngine->duration();
                if (vlcDur > 0) {
                    m_videoListPanel->updateDuration(filePath, vlcDur);
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
/// @brief 倍速循环调节：0.25x/0.5x/1x/2x/4x/8x
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

    m_currentSpeed = speeds[idx];

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
    m_magnifier->setRegionModel(m_regionModel);
    m_magnifier->setPolygonModel(m_polygonModel);
    m_magnifier->setGuideLineModel(m_guideLineModel);

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
                    int roiId = m_regionModel->roiIdAt(regionIndex);
                    if (roiId > 0 && snapshot.dataIndexOfRoiId(roiId, DataEntry::Rect) >= 0) {
                        auto reply = QMessageBox::question(this,
                            lang("数据失效警告", "Data Invalidation Warning"),
                            lang("调整该区域将导致亮度量化数据失效。\n确定要继续吗？",
                                 "Adjusting this region will invalidate the luminance analysis data.\nContinue?"),
                            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                        if (reply == QMessageBox::Yes) {
                            m_timelineModel->removeRegionDataByRoiId(roiId, DataEntry::Rect);
                        } else {
                            m_regionModel->updateRegion(regionIndex, originalRect);
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
                    int roiId = m_polygonModel->roiIdAt(polygonIndex);
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
                            m_polygonModel->updatePolygon(polygonIndex, originalPolygon);
                        }
                    }
                }
            });

    // Forward the current frame so the magnifier shows content immediately
    // even if the video is paused or has ended.
    if (!m_videoWidget->currentFrame().isNull()) {
        m_magnifier->onFrameReady(m_videoWidget->currentFrame());
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
    if (m_pinned && !m_pinnedRect.isEmpty() && !frame.isNull())
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

    QVector<QRect> regions = m_regionModel->regions();
    QVector<QPolygon> polygons = m_polygonModel->polygons();
    if (regions.isEmpty() && polygons.isEmpty()) {
        QMessageBox::information(this, lang("亮度分析", "Luminance Analysis"),
            lang("请先在视频上绘制至少一个 ROI 区域。",
                 "Please draw at least one ROI on the video."));
        return;
    }

    // Collect ROI IDs for data tracking
    QVector<int> rectRoiIds, polygonRoiIds;
    for (int i = 0; i < m_regionModel->regionCount(); ++i)
        rectRoiIds.append(m_regionModel->roiIdAt(i));
    for (int i = 0; i < m_polygonModel->polygonCount(); ++i)
        polygonRoiIds.append(m_polygonModel->roiIdAt(i));

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

    auto *pyEngine = qobject_cast<PythonAnalysisEngine *>(m_analysisEngine);
    if (!pyEngine) {
        QMessageBox::warning(this, lang("音频分析", "Audio Analysis"),
            lang("当前分析引擎不支持音频分析。",
                 "The current analysis engine does not support audio analysis."));
        return;
    }
    if (pyEngine->pythonExecutable().isEmpty()) {
        pyEngine->setPythonExecutable(detectPythonPath());
        if (pyEngine->pythonExecutable().isEmpty()) {
            QMessageBox::warning(this, lang("音频分析", "Audio Analysis"),
                lang("未找到 Python 解释器。\n请安装 Python 3.8+ 并确保在 PATH 中。",
                     "Python interpreter not found.\nPlease install Python 3.8+ and ensure it is in PATH."));
            return;
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
    pyEngine->startAudioAnalysis(m_currentVideoPath);
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
        // Luminance analysis: 0%-100%
        m_statusLabel->setText(
            QString(lang("已分析 %1 帧（%2%）", "Analyzed %1 frames (%2%)"))
                .arg(analyzed).arg(percent, 0, 'f', 1));
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

    // Auto-save .vla cache alongside the video file
    if (!m_currentVideoPath.isEmpty() &&
        !m_currentVideoPath.endsWith(".vla", Qt::CaseInsensitive)) {
        QString vlaPath = m_currentVideoPath + ".vla";
        QRect magRect = m_magnifier ? m_magnifier->currentSourceRect() : QRect();
        m_timelineModel->saveToFile(vlaPath, m_regionModel->regions(),
                                     m_chartPanel->timeOffset(),
                                     magRect, m_chartPanel->labels(), m_pinnedRect,
                                     m_snapshotFusion,
                                     m_polygonModel->polygons(),
                                     m_guideLineModel->lines());
        // Save spectrogram to separate binary file
        AnalysisSnapshot snap = m_timelineModel->snapshot();
        if (snap.hasAudio()) {
            TimelineModel::saveSpecToFile(vlaPath + ".spec", snap.audio);
        }
    }

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
    m_regionModel->clearRegions();
    m_polygonModel->clearPolygons();
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

    QVector<QRect> regions = m_regionModel->regions();
    if (snapshot.exportToCsv(filePath, regions, m_chartPanel->timeOffset())) {
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
                    out << label.timeMs << ","
                        << formatTime(label.timeMs + m_chartPanel->timeOffset()) << ","
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
    // 免节流免命令队列（mouseMove 不再产生 seek 命令，拖拽期间解码管线不断流）
    if (m_chartPanel->isDraggingCursor()) {
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

/// @brief 恢复分析状态：区域/时间偏移/标签/截图融合/音频
void MainWindow::restoreAnalysisState(const QVector<QRect> &regions,
                                       qint64 timeOffset,
                                       const QVector<ChartLabel> &labels,
                                       const QRect &pinnedRect,
                                       const SnapshotFusionData &fusion)
{
    m_regionModel->clearRegions();
    for (const QRect &rc : regions)
        m_regionModel->addRegion(rc);
    m_chartPanel->setTimeOffset(timeOffset);
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

        case Qt::Key_Up:
            m_videoEngine->setVolume(m_videoEngine->volume() + 5);
            showOperationStatus(QString(lang("音量 +5，现音量：%1", "Volume +5, Current: %1")).arg(m_videoEngine->volume()));
            return true;
        case Qt::Key_Down:
            m_videoEngine->setVolume(m_videoEngine->volume() - 5);
            showOperationStatus(QString(lang("音量 -5，现音量：%1", "Volume -5, Current: %1")).arg(m_videoEngine->volume()));
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
                qint64 offset = m_chartPanel->timeOffset();
                m_chartPanel->addLabelAtTime(pos + offset);
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
    m_roiClipboard = m_regionModel->regions();
    m_polygonClipboard = m_polygonModel->polygons();
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
        m_regionModel->clearRegions();
        m_polygonModel->clearPolygons();
        m_guideLineModel->clearLines();
    }

    for (const QRect &rc : m_roiClipboard) {
        m_regionModel->addRegion(rc);
    }
    for (const QPolygon &poly : m_polygonClipboard) {
        m_polygonModel->addPolygon(poly);
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
