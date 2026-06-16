/**
 * @file mainwindow.cpp
 * @brief 主窗口实现：菜单/工具栏/快捷键/分析流程/截图叠加/放大镜协调
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "mainwindow.h"
#include "videowidget.h"
#include "chartpanel.h"
#include "domain/region_model.h"
#include "domain/timeline_model.h"
#include "infrastructure/ivideo_engine.h"
#include "infrastructure/ianalysis_engine.h"
#include "infrastructure/vlc_video_engine.h"
#include "infrastructure/python_analysis_engine.h"
#include "magnifierwidget.h"
#include "snapshotoverlay.h"
#include "pinnedwidget.h"
#include "videowidget.h"
#include "i18n.h"
#include "aboutdialog.h"

#include <QSplitter>
#include <QMenuBar>
#include <QToolBar>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QMessageBox>
#include <QApplication>
#include <QStyle>
#include <QDebug>
#include <QProgressDialog>
#include <QDir>
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

/// @brief 构造主窗口：初始化引擎/组件/连接信号槽
MainWindow::MainWindow(QWidget *parent)
    : QMainWindow(parent)
{
    loadLanguage();
    setWindowTitle(lang("追光者 Lumen Arc v0.2 beta 内部测试", "Lumen Arc v0.2 beta Internal Test"));
    resize(1280, 720);

    m_regionModel = new RegionModel(this);
    m_timelineModel = new TimelineModel(this);

    m_videoEngine = new VlcVideoEngine(this);

    m_videoWidget = new VideoWidget(this);
    m_videoWidget->setVideoEngine(m_videoEngine);
    m_videoWidget->setRegionModel(m_regionModel);

    m_chartPanel = new ChartPanel(this);
    m_chartPanel->setRegionModel(m_regionModel);
    m_chartPanel->setTimelineModel(m_timelineModel);

    m_splitter = new QSplitter(Qt::Vertical, this);
    m_splitter->addWidget(m_videoWidget);
    m_splitter->addWidget(m_chartPanel);
    m_splitter->setStretchFactor(0, 2);
    m_splitter->setStretchFactor(1, 1);
    setCentralWidget(m_splitter);

    setAcceptDrops(true);
    m_splitter->setAcceptDrops(true);
    m_splitter->installEventFilter(this);

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
        QString path = QCoreApplication::applicationDirPath() + "/追光者 Lumen Arc v0.2 Beta — 操作手册.pdf";
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
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

    // --- Analyze group ---
    m_analyzeBtn = new QPushButton(lang("分析", "Analyze"), this);
    m_analyzeBtn->setToolTip(lang("批量分析整个视频（无需播放）", "Batch analyze entire video (no playback needed)"));
    m_analyzeBtn->setFixedHeight(30);
    m_analyzeBtn->setStyleSheet(primaryBtnStyle);
    m_analyzeBtn->setEnabled(false);

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

    m_chartPanel->setAutoYRange(true);

    // --- Layout ---
    toolBar->addWidget(m_playBtn);
    toolBar->addWidget(m_pauseBtn);
    toolBar->addWidget(m_stopBtn);
    toolBar->addWidget(m_speedBtn);
    toolBar->addSeparator();
    toolBar->addWidget(m_analyzeBtn);
    toolBar->addWidget(m_setTimeBtn);
    toolBar->addSeparator();
    toolBar->addWidget(m_captureBtn);
    toolBar->addWidget(m_editBtn);
    toolBar->addWidget(m_placeBtn);
    toolBar->addSeparator();

    m_timeLabel = new QLabel("00:00 / 00:00", this);
    m_timeLabel->setStyleSheet(timeLabelStyle);
    toolBar->addWidget(m_timeLabel);
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
    connect(m_analyzeBtn, &QPushButton::clicked, this, &MainWindow::onAnalyze);
    connect(m_setTimeBtn, &QPushButton::clicked, this, &MainWindow::onSetStartTime);

    connect(m_videoEngine, &IVideoEngine::positionChanged,
            this, &MainWindow::onPositionChanged);
    connect(m_videoEngine, &IVideoEngine::durationChanged,
            this, &MainWindow::onDurationChanged);

    connect(m_chartPanel, &ChartPanel::seekRequested,
            this, &MainWindow::onSeekFromChart);

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
    connect(overlay, &OverlayWidget::magnifierCursorMoved,
            this, &MainWindow::onMagnifierCursorMoved);

    // ROI adjustment warning: check for existing analysis data
    connect(overlay, &OverlayWidget::regionAdjustmentFinished,
            this, [this](int regionIndex, const QRect &originalRect, const QRect &newRect) {
                Q_UNUSED(newRect);
                AnalysisSnapshot snapshot = m_timelineModel->snapshot();
                if (!snapshot.isEmpty()) {
                    auto reply = QMessageBox::question(this,
                        lang("数据失效警告", "Data Invalidation Warning"),
                        lang("调整该区域将导致亮度量化数据失效。\n确定要继续吗？",
                             "Adjusting this region will invalidate the luminance analysis data.\nContinue?"),
                        QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
                    if (reply == QMessageBox::Yes) {
                        m_timelineModel->clearData();
                        m_chartPanel->onDataCleared();
                    } else {
                        // Revert the region to its original rect
                        m_regionModel->updateRegion(regionIndex, originalRect);
                    }
                }
            });

    // Right-click context menu on video overlay
    overlay->setContextMenuPolicy(Qt::CustomContextMenu);
    connect(overlay, &QWidget::customContextMenuRequested,
            this, &MainWindow::showVideoContextMenu);

    // Intercept Alt key for magnifier follow mode
    overlay->installEventFilter(this);
    menuBar()->installEventFilter(this);

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
                                       1.0 + m_snapshotOverlay->contrastValue() / 50.0,
                                       m_snapshotOverlay->opacityValue() / 100.0);
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
                                       1.0 + m_snapshotOverlay->contrastValue() / 50.0,
                                       m_snapshotOverlay->opacityValue() / 100.0);
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
                                       1.0 + m_snapshotOverlay->contrastValue() / 50.0,
                                       m_snapshotOverlay->opacityValue() / 100.0);
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

void MainWindow::onOpenFile()
{
    QString filePath = QFileDialog::getOpenFileName(this,
        "Open Video File",
        QString(),
        "Video Files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;"
        "Analysis Results (*.vla);;All Files (*)");
    openVideoFile(filePath);
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
    qint64 timeOffset = 0;
    QRect magnifierRect;
    QVector<ChartLabel> labels;
    QRect pinnedRect;
    SnapshotFusionData snapshotFusion;
    if (m_timelineModel->loadFromFile(filePath, &regions, &timeOffset,
                                       &magnifierRect, &labels, &pinnedRect,
                                       &snapshotFusion)) {
        m_regionModel->clearRegions();
        for (const QRect &rc : regions)
            m_regionModel->addRegion(rc);
        m_chartPanel->setTimeOffset(timeOffset);
        m_chartPanel->setLabels(labels);
        if (!pinnedRect.isEmpty())
            m_pinnedRect = pinnedRect;
        // Restore snapshot fusion
        m_snapshotFusion = snapshotFusion;
        if (snapshotFusion.isValid() && !snapshotFusion.imageData.isNull()) {
            m_snapshotOverlay->setSnapshot(snapshotFusion.imageData);
            m_snapshotOverlay->setParameters(
                snapshotFusion.brightness,
                snapshotFusion.contrast,
                snapshotFusion.opacity);
            m_editBtn->setEnabled(true);
            m_placeBtn->setEnabled(true);
        }

        m_currentVideoPath = filePath;
            setWindowTitle("Lumen Arc v0.2 beta 内部测试 - [Loaded: " +
                           QFileInfo(filePath).fileName() + "]");
            QMessageBox::information(this, lang("已加载", "Loaded"),
                lang("分析结果加载成功。", "Analysis result loaded successfully."));
        } else {
            QMessageBox::critical(this, lang("错误", "Error"),
                lang("加载分析结果文件失败：\n",
                     "Failed to load analysis result file:\n") + filePath);
        }
        return;
    }

    m_currentVideoPath = filePath;

    if (m_videoEngine->load(filePath)) {
        m_regionModel->clearRegions();
        m_timelineModel->clearData();
        m_playBtn->setEnabled(true);
        m_pauseBtn->setEnabled(true);
        m_stopBtn->setEnabled(true);
        m_speedBtn->setEnabled(true);
        m_analyzeBtn->setEnabled(true);
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
                qint64 timeOffset = 0;
                QRect magnifierRect;
                QVector<ChartLabel> labels;
                QRect pinnedRect;
                SnapshotFusionData snapshotFusion;
                if (m_timelineModel->loadFromFile(vlaPath, &regions, &timeOffset,
                                                   &magnifierRect, &labels, &pinnedRect,
                                                   &snapshotFusion)) {
                    m_regionModel->clearRegions();
                    for (const QRect &rc : regions)
                        m_regionModel->addRegion(rc);
                    m_chartPanel->setTimeOffset(timeOffset);
                    m_chartPanel->setLabels(labels);
                    if (!pinnedRect.isEmpty())
                        m_pinnedRect = pinnedRect;
                    QMessageBox::information(this, lang("已加载", "Loaded"),
                        "Cached analysis loaded successfully.");
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
                                     m_snapshotFusion)) {
        QMessageBox::information(this, lang("保存", "Save"),
            lang("分析结果保存成功。", "Analysis result saved successfully."));
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
        qint64 timeOffset = 0;
        QRect magnifierRect;
        QVector<ChartLabel> labels;
        QRect pinnedRect;
        SnapshotFusionData snapshotFusion;
        if (m_timelineModel->loadFromFile(filePath, &regions, &timeOffset,
                                           &magnifierRect, &labels, &pinnedRect,
                                           &snapshotFusion)) {
            m_regionModel->clearRegions();
            for (const QRect &rc : regions)
                m_regionModel->addRegion(rc);
            m_chartPanel->setTimeOffset(timeOffset);
            m_chartPanel->setLabels(labels);
            if (!pinnedRect.isEmpty())
                m_pinnedRect = pinnedRect;
            // Restore snapshot fusion
            m_snapshotFusion = snapshotFusion;
            if (snapshotFusion.isValid() && !snapshotFusion.imageData.isNull()) {
                m_snapshotOverlay->setSnapshot(snapshotFusion.imageData);
                m_snapshotOverlay->setParameters(
                    snapshotFusion.brightness,
                    snapshotFusion.contrast,
                    snapshotFusion.opacity);
                m_editBtn->setEnabled(true);
                m_placeBtn->setEnabled(true);
            }
            m_currentVideoPath = filePath;
        setWindowTitle("Lumen Arc v0.2 beta - [Loaded: " +
                       QFileInfo(filePath).fileName() + "]");
        QMessageBox::information(this, lang("已加载", "Loaded"),
            lang("分析结果加载成功。", "Analysis result loaded successfully."));
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

    QString filePath = urls.first().toLocalFile();
    if (!filePath.isEmpty()) {
        openVideoFile(filePath);
    }
}

/**
 * @brief 键盘快捷键分发：播放/帧进退/音量/倍速/标签/放大镜
 */
void MainWindow::keyPressEvent(QKeyEvent *event)
{
    // Don't handle playback shortcuts when a text input has focus
    QWidget *fw = focusWidget();
    if (qobject_cast<QLineEdit*>(fw) || qobject_cast<QTextEdit*>(fw)) {
        QMainWindow::keyPressEvent(event);
        return;
    }

    if (!m_videoEngine || !m_videoEngine->duration())
        return QMainWindow::keyPressEvent(event);

    switch (event->key()) {
    case Qt::Key_Space:
        if (m_videoEngine->state() == PlaybackState::Playing) {
            m_videoEngine->pause();
            updatePlaybackButtons();
        } else {
            m_videoEngine->play();
            updatePlaybackButtons();
        }
        break;

    case Qt::Key_Left: {
        float f = m_videoEngine->fps();
        qint64 frameStep = static_cast<qint64>(1000.0f / f);
        if (frameStep < 1) frameStep = 33;
        m_videoEngine->seek(m_videoEngine->position() - frameStep);
        break;
    }
    case Qt::Key_Right: {
        float f = m_videoEngine->fps();
        qint64 frameStep = static_cast<qint64>(1000.0f / f);
        if (frameStep < 1) frameStep = 33;
        m_videoEngine->seek(m_videoEngine->position() + frameStep);
        break;
    }

    case Qt::Key_Up:
        m_videoEngine->setVolume(m_videoEngine->volume() + 5);
        break;
    case Qt::Key_Down:
        m_videoEngine->setVolume(m_videoEngine->volume() - 5);
        break;

    case Qt::Key_C:
        adjustSpeed(1.0f);
        break;
    case Qt::Key_X:
        adjustSpeed(-1.0f);
        break;
    case Qt::Key_Z:
        m_currentSpeed = 1.0f;
        m_speedBtn->setText("1x");
        m_videoEngine->setRate(1.0f);
        break;

    case Qt::Key_B: {
        if (m_videoEngine->duration() > 0) {
            qint64 pos = m_videoEngine->position();
            qint64 offset = m_chartPanel->timeOffset();
            m_chartPanel->addLabelAtTime(pos + offset);
        }
        break;
    }

    case Qt::Key_Escape:
        removeMagnifier();
        break;

    case Qt::Key_Alt:
        setMagnifierFollowing(true);
        break;

    default:
        QMainWindow::keyPressEvent(event);
    }
}

void MainWindow::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Alt) {
        setMagnifierFollowing(false);
        return;
    }
    QMainWindow::keyReleaseEvent(event);
}

void MainWindow::onPlay()
{
    m_videoEngine->play();
}

void MainWindow::onPause()
{
    m_videoEngine->pause();
}

void MainWindow::onStop()
{
    m_videoEngine->stop();
    m_currentSpeed = 1.0f;
    m_speedBtn->setText("1x");
    m_videoEngine->setRate(1.0f);
    updatePlaybackButtons();
}

/// @brief 倍速循环调节：0.25x/0.5x/1x/2x/4x/8x
void MainWindow::adjustSpeed(float delta)
{
    static const float speeds[] = {0.25f, 0.5f, 1.0f, 2.0f, 4.0f, 8.0f};
    static const int count = 6;

    // Find current speed index
    int idx = 0;
    for (int i = 0; i < count; ++i) {
        if (qFuzzyCompare(m_currentSpeed, speeds[i])) {
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
}

void MainWindow::updatePlaybackButtons()
{
    bool playing = (m_videoEngine->state() == PlaybackState::Playing);
    bool hasMedia = m_videoEngine->duration() > 0;
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

    addDockWidget(Qt::RightDockWidgetArea, m_magnifier);
    m_magnifier->setWindowTitle("Magnifier [Locked]");
    m_magnifier->show();

    // Sync snapshot overlay if currently active
    if (m_snapshotOverlay && m_snapshotOverlay->hasSnapshot() && m_snapshotOverlay->isOverlayActive()) {
        m_magnifier->setSnapshotOverlay(m_snapshotOverlay->snapshotImage(),
                                         m_snapshotOverlay->brightness(),
                                         m_snapshotOverlay->contrastValue(),
                                         m_snapshotOverlay->opacityValue());
    }
}

void MainWindow::removeMagnifier()
{
    if (!m_magnifier)
        return;

    m_magnifier->clearSnapshotOverlay();
    m_magnifier->close();
    m_magnifier->deleteLater();
    m_magnifier = nullptr;
}

void MainWindow::onMagnifierWheelZoom(int delta)
{
    if (!m_magnifier)
        createMagnifier();

    if (m_magnifier) {
        m_magnifier->adjustZoom(delta);
    }
}

void MainWindow::onMagnifierCursorMoved(QPoint videoPos)
{
    if (m_magnifier && m_magnifierFollowing)
        m_magnifier->updateCursorPosition(videoPos);
}

void MainWindow::setMagnifierFollowing(bool follow)
{
    m_magnifierFollowing = follow;
    if (m_magnifier) {
        m_magnifier->setWindowTitle(follow ? "Magnifier [Following]" : "Magnifier [Locked]");
    }
}

void MainWindow::showVideoContextMenu(const QPoint &pos)
{
    QMenu menu;
    if (m_magnifier) {
        QAction *closeAction = menu.addAction(lang("关闭放大镜", "Close Magnifier"));
        if (menu.exec(m_videoWidget->overlay()->mapToGlobal(pos)) == closeAction) {
            removeMagnifier();
        }
    }
}

void MainWindow::updatePinnedImage(const QImage &frame)
{
    if (m_pinned && !m_pinnedRect.isEmpty() && !frame.isNull())
        m_pinned->setPinnedImage(frame, m_pinnedRect);
}

/// @brief 启动离线分析：前置检查→进度对话框→Python进程
void MainWindow::onAnalyze()
{
    if (m_currentVideoPath.isEmpty()) {
        QMessageBox::information(this, lang("分析", "Analyze"),
            lang("请先打开一个视频文件。", "Please open a video file first."));
        return;
    }

    // If python path not found, silently retry detection
    auto *pyEngine = qobject_cast<PythonAnalysisEngine *>(m_analysisEngine);
    if (pyEngine && pyEngine->pythonExecutable().isEmpty()) {
        pyEngine->setPythonExecutable(detectPythonPath());
        if (pyEngine->pythonExecutable().isEmpty()) {
            return;
        }
    }

    QVector<QRect> regions = m_regionModel->regions();
    if (regions.isEmpty()) {
        QMessageBox::information(this, lang("分析", "Analyze"),
            lang("请先在视频上绘制至少一个 ROI 区域。",
                 "Please draw at least one ROI on the video."));
        return;
    }

    if (m_analysisEngine->isRunning()) {
        QMessageBox::information(this, lang("分析", "Analyze"),
            lang("分析正在运行中。", "Analysis is already running."));
        return;
    }

    m_progressDlg = new QProgressDialog(
        lang("正在准备分析...", "Preparing analysis..."),
        lang("取消", "Cancel"), 0, 100, this);
    m_progressDlg->setWindowModality(Qt::WindowModal);
    m_progressDlg->setMinimumDuration(0);
    m_progressDlg->setValue(0);
    m_progressDlg->setAutoClose(false);
    m_progressDlg->setAutoReset(false);

    connect(m_progressDlg, &QProgressDialog::canceled, this, [this]() {
        m_analysisEngine->cancelAnalysis();
    });

    m_analyzeBtn->setEnabled(false);
    m_analyzeBtn->setText(lang("分析中...", "Analyzing..."));

    m_analysisEngine->startAnalysis(m_currentVideoPath, regions);
}

void MainWindow::onAnalysisProgress(int analyzed, int total, qreal percent)
{
    Q_UNUSED(total)
    if (!m_progressDlg)
        return;
    m_progressDlg->setLabelText(
        QString(lang("已分析 %1 帧（%2%）", "Analyzed %1 frames (%2%)"))
            .arg(analyzed).arg(percent, 0, 'f', 1));
    m_progressDlg->setValue(static_cast<int>(percent));
}

/**
 * @brief 分析完成处理：数据填充→图表更新→自动保存VLA
 */
void MainWindow::onAnalysisFinished(const AnalysisSnapshot &snapshot)
{
    m_analyzeBtn->setText(lang("分析", "Analyze"));
    m_analyzeBtn->setEnabled(true);

    // Detach progress dialog and defer deletion to avoid
    // deleting from within its own signal handler chain.
    if (m_progressDlg) {
        disconnect(m_progressDlg, nullptr, this, nullptr);
        m_progressDlg->close();
        m_progressDlg->deleteLater();
        m_progressDlg = nullptr;
    }

    // --- Synchronous chart update ---
    // setData triggers onDataReplaced() on the main thread directly.
    // All series data, time labels, axis ranges are updated in one pass
    // with no deferred (QueuedConnection) operations.
    m_timelineModel->setData(QVector<qint64>(snapshot.timestamps),
                             QVector<QVector<qreal>>(snapshot.values));

    // Auto-save .vla cache alongside the video file
    if (!m_currentVideoPath.isEmpty() &&
        !m_currentVideoPath.endsWith(".vla", Qt::CaseInsensitive)) {
        QString vlaPath = m_currentVideoPath + ".vla";
        QRect magRect = m_magnifier ? m_magnifier->currentSourceRect() : QRect();
        m_timelineModel->saveToFile(vlaPath, m_regionModel->regions(),
                                     m_chartPanel->timeOffset(),
                                     magRect, m_chartPanel->labels(), m_pinnedRect,
                                     m_snapshotFusion);
    }

    QString msg = QString(lang("分析完成！\n数据点数：%1\n区域数：%2",
                               "Analysis complete!\nTotal points: %1\nRegions: %2"))
                      .arg(snapshot.pointCount())
                      .arg(snapshot.regionCount());

    // Flush all Qt Charts internal paint/layout events that setData queued.
    QCoreApplication::processEvents();

    // Safe to show modal dialog — chart is fully rendered.
    QMessageBox::information(this, lang("分析完成", "Analysis Complete"), msg);
}

void MainWindow::onAnalysisFailed(const QString &error)
{
    m_analyzeBtn->setText(lang("分析", "Analyze"));
    m_analyzeBtn->setEnabled(true);

    // Detach progress dialog and defer deletion — we may be inside
    // its canceled-signal handler chain, so direct delete would
    // destroy the QProgressDialog while it's still on the call stack.
    if (m_progressDlg) {
        disconnect(m_progressDlg, nullptr, this, nullptr);
        m_progressDlg->close();
        m_progressDlg->deleteLater();
        m_progressDlg = nullptr;
    }

    // Flush pending paint events before opening a modal dialog
    // with its own nested event loop.
    QCoreApplication::processEvents();

    QMessageBox::critical(this, lang("分析失败", "Analysis Failed"),
        lang("离线分析失败。\n请确保已安装 Python 和 OpenCV。\n\n",
             "Offline analysis failed.\nMake sure Python and OpenCV are installed.\n\n") + error);
}

void MainWindow::onClearRegions()
{
    m_regionModel->clearRegions();
}

void MainWindow::onClearData()
{
    m_timelineModel->clearData();
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
                QTextStream out(&labelsFile);
                out << "Time(ms),Time,Text,Color\n";
                for (const auto &label : labels) {
                    out << label.timeMs << ","
                        << formatTime(label.timeMs + m_chartPanel->timeOffset()) << ","
                        << label.text << ","
                        << label.color.name(QColor::HexArgb) << "\n";
                }
                labelsFile.close();
                QMessageBox::information(this, lang("导出", "Export"),
                    lang("数据导出成功。\n标签文件：",
                         "Data exported successfully.\nLabels: ") + labelsPath);
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
    m_chartPanel->setDuration(durationMs);
    updateTimeDisplay();
}

void MainWindow::onPositionChanged(qint64 timeMs)
{
    m_chartPanel->setCursorTime(timeMs);
    updateTimeDisplay();
}

void MainWindow::onSeekFromChart(qint64 timeMs)
{
    m_videoEngine->seek(timeMs);
}

void MainWindow::updateTimeDisplay()
{
    qint64 pos = m_videoEngine ? m_videoEngine->position() : 0;
    qint64 dur = m_videoEngine ? m_videoEngine->duration() : 0;
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

/**
 * @brief 全局事件过滤：Alt放大镜跟随/按钮空格拦截/拖放
 */
bool MainWindow::eventFilter(QObject *watched, QEvent *event)
{
    // Space key on any button: redirect to play/pause
    if (event->type() == QEvent::KeyPress) {
        auto *e = static_cast<QKeyEvent *>(event);
        if (e->key() == Qt::Key_Space && qobject_cast<QPushButton*>(focusWidget())) {
            if (m_videoEngine && m_videoEngine->duration() > 0) {
                if (m_videoEngine->state() == PlaybackState::Playing)
                    m_videoEngine->pause();
                else
                    m_videoEngine->play();
                updatePlaybackButtons();
                return true;
            }
        }
    }

    // Alt key toggles magnifier follow mode
    if (watched == m_videoWidget->overlay() || watched == menuBar()) {
        if (event->type() == QEvent::KeyPress) {
            auto *e = static_cast<QKeyEvent *>(event);
            if (e->key() == Qt::Key_Alt) {
                setMagnifierFollowing(true);
                return true;
            }
        } else if (event->type() == QEvent::KeyRelease) {
            auto *e = static_cast<QKeyEvent *>(event);
            if (e->key() == Qt::Key_Alt) {
                setMagnifierFollowing(false);
                return true;
            }
        }
    }

    if (watched == m_splitter) {
        if (event->type() == QEvent::DragEnter) {
            auto *e = static_cast<QDragEnterEvent *>(event);
            if (e->mimeData()->hasUrls()) {
                e->acceptProposedAction();
                return true;
            }
        } else if (event->type() == QEvent::Drop) {
            auto *e = static_cast<QDropEvent *>(event);
            const QMimeData *mimeData = e->mimeData();
            if (mimeData->hasUrls()) {
                QList<QUrl> urls = mimeData->urls();
                if (!urls.isEmpty()) {
                    QString filePath = urls.first().toLocalFile();
                    if (!filePath.isEmpty()) {
                        openVideoFile(filePath);
                    }
                }
                e->acceptProposedAction();
                return true;
            }
        }
    }
    return QMainWindow::eventFilter(watched, event);
}
