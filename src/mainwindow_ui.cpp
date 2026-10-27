/**
 * @file mainwindow_ui.cpp
 * @brief v1.17.0 P-79：自 mainwindow.cpp 拆出——构造体（分域构建方法）/菜单/工具栏
 */
#include "mainwindow.h"
#include "keyguardfilter.h"
#include "build_stamp.h"
#include "videowidget.h"
#include "chartpanel.h"
#include "domain/roi_model.h"
#include "domain/roi_model.h"
#include "domain/guide_line_model.h"
#include "domain/timeline_model.h"
#include "infrastructure/ivideo_engine.h"
#include "infrastructure/ianalysis_engine.h"
#include "infrastructure/ffmpeg_video_engine.h"
#include "app/calibration_service.h"
#include "sitemapeditordialog.h"
#include "app/case_manager.h"
#include "app/case_open_panel.h"
#include "app/analysis_task_service.h"
#include "app/analysis_controller.h"
#include "app/uistate.h"
#include "app/project_io.h"
#include "app/video_session_manager.h"
#include "domain/task_registry.h"
#include "casedock.h"
#include "casedialogs.h"
#include "multicamplaybackwindow.h"
#include "timesettingsdialog.h"
#include "magnifierwidget.h"
#include "fullscreenvideowindow.h"
#include "snapshotoverlay.h"
#include "playbackadjustpanel.h"
#include "displayadjust.h"
#include "pinnedwidget.h"
#include "videolistpanel.h"
#include "preprocesswindow.h"
#include "composeworkbench.h"
#include "infrastructure/segment_export_engine.h"
#include <QProgressDialog>
#include "spectrogrampanel_enhanced.h"
#include "i18n.h"
#include "aboutdialog.h"
#include "accountdialog.h"
#include "feedbackdialog.h"
#include "logindialog.h"
#include "infrastructure/credential_store.h"
#include "theme.h"
#include "ui/chevron_button.h"

#include <QListWidget>
#include <QSplitter>
#include <QMenuBar>
#include <QToolBar>
#include <QToolButton>
#include <QShortcut>
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
#include <QGuiApplication>
#include <QScreen>
#include <QWindow>
#include <QFile>
#include <QDateTime>
#include <QDir>
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
#include <QTreeWidget>
#include <QDoubleSpinBox>
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
    setWindowTitle(lang(QStringLiteral("追光者 Lumen Arc v") + QString(APP_VERSION), QStringLiteral("Lumen Arc v") + QString(APP_VERSION)) + buildStamp());
    resize(1280, 720);

    m_roiModel = new RoiModel(this);   // 统一 ROI 模型（矩形+多边形，v1.5.0 Q-18）
    m_guideLineModel = new GuideLineModel(this);
    m_timelineModel = new TimelineModel(this);
    m_sessionMgr = new VideoSessionManager(this);
    // v1.17.0 P-77：播放参数 SSOT（倍速+降噪强度）——必须早于 applyPlaybackDenoiseSetting()（引擎块内调用）
    m_playbackSettings = new PlaybackSettings(this);
    // v1.17.0 P-78（Q5）：快捷键守卫（18 键 switch 经 handleGlobalShortcut 转发）
    m_keyGuard = new KeyGuardFilter(this);
    m_keyGuard->setHandler([this](QKeyEvent *e) { return handleGlobalShortcut(e); });

    // 播放引擎：自研 FFmpeg 内核（硬解设置经 QSettings 持久化）
    {
        QSettings engineSettings("LumenArc", "LumenArc");
        auto *ffEngine = new FfmpegVideoEngine(this);
        ffEngine->setHardwareDecode(
            engineSettings.value("hwDecode", true).toBool());
        ffEngine->setHardwareAdapter(
            engineSettings.value("hwAdapter", -1).toInt());
        m_videoEngine = ffEngine;
        applyPlaybackDenoiseSetting();   // P-54b：启动即同步开关/强度
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
    // v1.13.1（用户拍板布局）：顶行改为水平 QSplitter [视频 | 放大镜]——
    // 放大镜内嵌右上象限（与视频等大同高），图表/语谱图保持全宽在下；
    // 左侧视频/案件列表 dock 天然全高。此前放大镜为右侧 dock 贯通整窗
    // 高度，挤压图表区且上下大黑边。
    m_topRow = new QSplitter(Qt::Horizontal, m_splitter);
    m_topRow->setHandleWidth(4);
    m_topRow->setChildrenCollapsible(false);
    m_topRow->addWidget(m_videoWidget);
    m_topRow->setStretchFactor(0, 1);
    m_splitter->addWidget(m_topRow);
    m_splitter->setStretchFactor(0, 55);  // 视频行 ~55%（v1.13.1 按 v1.7.0 观感拍板）
    setCentralWidget(m_splitter);

    // --- Shared styles（主题化）---
    const QString titleBarStyle =
        "background: " + Theme::BgPanel + "; border-bottom: 1px solid " + Theme::Border + ";";
    const QString collapseBtnStyle =
        "QPushButton { background: transparent; color: " + Theme::TextSecond + "; border: none; font-size: 10px; border-radius: 4px; }"
        "QPushButton:hover { color: " + Theme::Accent + "; background: " + Theme::BgHover + "; }";
    const QString titleLabelStyle =
        "color: " + Theme::TextPrimary + "; font-size: 12px; font-weight: 600; background: transparent; padding-left: 2px;";

    // v1.17.0 P-79：构造体逐字抽取为分域构建方法（行为冻结：调用序与原构造一致；
    // 共享样式串经参数传递，不重复定义）
    buildCentralLayout(titleBarStyle, collapseBtnStyle, titleLabelStyle);


    buildVideoListDock(collapseBtnStyle);


    buildStatusBar();
    setAcceptDrops(true);
    buildServices();


    buildCaseUi(collapseBtnStyle);


    createMenus();


    createToolBar();


    setupConnections();




    // v1.3.0 M2 任务11：启动欢迎面板（2026-08 改为页面内居中非模态，
    // 迁自模态起始页；可勾选不再显示；独立模式 = v1.2.2 行为）
    QTimer::singleShot(0, this, [this]() {
        if (CaseOpenPanel::showStartupPanel())
            onShowStartPage();
    });

    // v2.0 前端骨架（P0）：工作流条 + 任务带 + 命令注册表。
    // 必须放构造体最后——此刻中央部件与菜单栏已全部建好。
    setupShell();
}

MainWindow::~MainWindow()
{


    disconnect(m_analysisEngine, nullptr, this, nullptr);
    m_analysisEngine->cancelAnalysis();
}


/// @brief 创建菜单栏：文件/编辑/导出/帮助
void MainWindow::createMenus()
{
    QMenu *fileMenu = menuBar()->addMenu(lang("文件(&F)", "&File"));
    fileMenu->addAction(lang("打开视频(&O)...", "&Open Video..."), this, &MainWindow::onOpenFile, QKeySequence::Open);
    // v1.18 UI 重组：Ctrl+Shift+O 恒为「临时查看」，与 Ctrl+O（案件模式=入案）语义固定
    fileMenu->addAction(lang("临时查看视频（不入案）(&T)...", "Open Video &Temporarily (no case)..."),
                        this, &MainWindow::onOpenFileTemporary,
                        QKeySequence(QStringLiteral("Ctrl+Shift+O")));
    fileMenu->addSeparator();
    // 素材准备与接入
    fileMenu->addAction(lang("素材转码拼接(&M)...", "&Transcode & Merge..."), this, &MainWindow::openPreprocessWindow, QKeySequence(QStringLiteral("Ctrl+M")));
    fileMenu->addAction(lang("接入监控直播(&L)...", "Live &Monitor (RTSP)..."),
                        this, &MainWindow::onOpenLiveMonitor);
    fileMenu->addAction(lang("加载图片为叠加(&I)...", "Load Image as &Overlay..."), this, &MainWindow::onLoadOverlayImage);
    fileMenu->addSeparator();
    fileMenu->addAction(lang("保存分析结果(&S)...", "&Save Analysis Result..."), this, &MainWindow::onSaveAnalysis, QKeySequence::Save);
    fileMenu->addAction(lang("加载分析结果(&L)...", "&Load Analysis Result..."), this, &MainWindow::onLoadAnalysis, QKeySequence(QStringLiteral("Ctrl+L")));
    fileMenu->addSeparator();
    fileMenu->addAction(lang("退出(&X)", "E&xit"), this, &QWidget::close, QKeySequence::Quit);

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
    // v1.18 UI 重组：案件属性与案件根目录合并为一个对话框（无需开案即可改根目录）
    m_casePropsAction = caseMenu->addAction(
        lang("案件属性与设置(&P)...", "Case &Properties && Settings..."), this,
        &MainWindow::onCaseProperties);
    // v1.3.0 M3 任务13：批量重新定位（检材维持）
    m_batchRelocateAction = caseMenu->addAction(
        lang("批量重新定位(&B)...", "&Batch Relocate..."), this,
        &MainWindow::onBatchRelocate);
    m_batchRelocateAction->setEnabled(false);
    caseMenu->addSeparator();
    m_closeCaseAction = caseMenu->addAction(
        lang("关闭案件(&W)", "&Close Case"), this,
        &MainWindow::closeCaseWithPrompt,
        QKeySequence(QStringLiteral("Ctrl+W")));
    m_closeCaseAction->setEnabled(false);

    // v1.18 UI 重组：新增「分析」菜单，收拢分析/校时/多机与区域工具（替代原「编辑」空壳）
    QMenu *analysisMenu = menuBar()->addMenu(lang("分析(&A)", "&Analyze"));
    // P-57/P-59：多机同步播放大窗（案件开着即可进；面板内逐路标识校时状态）
    m_multiCamAction = analysisMenu->addAction(
        lang("多机同步播放(&M)...", "&Multi-camera Synced Playback..."), this,
        &MainWindow::onMultiCamView);
    m_multiCamAction->setEnabled(false);
    // 案件开着即可用（面板内引导空案/未校时场景）→ 菜单弹出时动态置灰
    connect(analysisMenu, &QMenu::aboutToShow, this, [this]() {
        if (m_multiCamAction)
            m_multiCamAction->setEnabled(m_caseManager->isOpen());
    });
    // P-57 U-1：独立 2 路对比（无案件也可用）——与上面合并到同一菜单
    analysisMenu->addAction(lang("多机对比播放（2 路·独立）(&N)...", "Multi-cam &Compare (2 lanes, standalone)..."),
                            this, &MainWindow::onMultiCamStandalone);
    analysisMenu->addSeparator();
    analysisMenu->addAction(lang("亮度分析(&L)", "&Luminance Analysis"), this, &MainWindow::onAnalyze);
    analysisMenu->addAction(lang("音频分析(&A)", "&Audio Analysis"), this, &MainWindow::onAudioAnalysis);
    analysisMenu->addAction(lang("微变分析(&D)...", "&Micro-change Analysis..."), this, &MainWindow::onMicroDiff);
    analysisMenu->addAction(lang("校时(&T)...", "&Time Calibration..."), this, &MainWindow::onSetStartTime);
    analysisMenu->addSeparator();
    QMenu *regionMenu = analysisMenu->addMenu(lang("区域模式(&R)", "&Region Mode"));
    regionMenu->addAction(lang("矩形", "Rectangle"), this, &MainWindow::onRectMode);
    regionMenu->addAction(lang("多边形", "Polygon"), this, &MainWindow::onPolygonMode);
    regionMenu->addAction(lang("辅助线", "Guide Line"), this, &MainWindow::onGuideLineMode);
    analysisMenu->addAction(lang("复制区域(&C)", "&Copy Regions"), this, &MainWindow::onCopyRoi);
    analysisMenu->addAction(lang("粘贴区域(&V)", "&Paste Regions"), this, &MainWindow::onPasteRoi);
    analysisMenu->addSeparator();
    analysisMenu->addAction(lang("清除选区(&E)", "Clear &Regions"), this, &MainWindow::onClearRegions);
    analysisMenu->addAction(lang("清除数据(&X)", "Clear &Data"), this, &MainWindow::onClearData);

    // v1.18 UI 重组：新增「产出」菜单，集中所有交付物（替代原「导出」单项菜单）
    QMenu *deliverMenu = menuBar()->addMenu(lang("产出(&P)", "&Deliver"));
    deliverMenu->addAction(lang("证据快照(&S)", "Evidence &Snapshot"), this, &MainWindow::onSnapshotQuick);
    m_composeExportAction = deliverMenu->addAction(
        lang("合成导出工作台(&E)...", "&Compose Export Workbench..."), this,
        &MainWindow::onExportSegmentClip);
    m_composeExportAction->setEnabled(false);
    deliverMenu->addSeparator();
    deliverMenu->addAction(lang("导出为 CSV(&C)...", "Export to &CSV..."), this, &MainWindow::onExportCsv);
    deliverMenu->addSeparator();
    // P-28 报告模块：生成分析报告（草稿→案内 reports/）
    m_genReportAction = deliverMenu->addAction(
        lang("生成分析报告（DOCX）(&G)...", "&Generate Analysis Report (DOCX)..."), this, [this]() {
        onGenerateReport(false);
    });
    m_genReportAction->setEnabled(false);
    // P-26 复活：HTML 报告（离线单文件，含时间轴可视化章节）——采集/光栅与 DOCX 共用
    m_genHtmlReportAction = deliverMenu->addAction(
        lang("导出 HTML 报告(&H)...", "Export &HTML Report..."), this, [this]() {
        onGenerateReport(true);
    });
    m_genHtmlReportAction->setEnabled(false);
    // P-74 点位图编辑器（报告二(三)节成品图来源）
    m_sitemapAction = deliverMenu->addAction(
        lang("编辑监控点位图(&M)...", "Edit Site &Map..."), this, [this]() {
        SiteMapEditorDialog dlg(m_caseManager, this);
        dlg.exec();
    });
    m_sitemapAction->setEnabled(false);
    deliverMenu->addSeparator();
    // v1.3.0 M3 任务12：导出移交包
    m_exportCaseAction = deliverMenu->addAction(
        lang("导出移交包(&T)...", "&Export Handover Package..."), this,
        &MainWindow::onExportCase);
    m_exportCaseAction->setEnabled(false);

    // 视图菜单：副屏全屏展示（多屏主机；拍板：手选屏/纯画面/调节共享/单路/F11）
    QMenu *viewMenu = menuBar()->addMenu(lang("视图(&V)", "&View"));
    QMenu *fsMenu = viewMenu->addMenu(lang("副屏全屏显示(&F)", "Fullscreen on &Screen"));
    auto rebuildFsMenu = [this, fsMenu]() {
        fsMenu->clear();
        const auto screens = QGuiApplication::screens();
        const QScreen *primary = QGuiApplication::primaryScreen();
        const QString last = QSettings().value(QStringLiteral("fullscreen/screen")).toString();
        for (int i = 0; i < screens.size(); ++i) {
            QScreen *s = screens[i];
            QString text = QStringLiteral("%1. %2 (%3×%4)")
                               .arg(i + 1).arg(s->name())
                               .arg(s->size().width()).arg(s->size().height());
            if (s == primary)
                text += lang("〔主屏〕", " [primary]");
            if (s->name() == last)
                text += lang("〔上次〕", " [last]");
            if (m_fsWindow && m_fsWindow->screen() == s)
                text += lang("〔全屏中〕", " [fullscreen]");
            fsMenu->addAction(text, this, [this, s]() { toggleFullscreenOnScreen(s); });
        }
        if (screens.size() < 2)
            fsMenu->addAction(lang("（仅检测到一块屏幕）", "(only one screen detected)"))
                ->setEnabled(false);
    };
    QObject::connect(fsMenu, &QMenu::aboutToShow, this, rebuildFsMenu);
    QAction *fsLast = viewMenu->addAction(lang("副屏全屏（上次屏幕）(&L)", "Fullscreen (last screen)(&L)"),
                                          this, [this]() { toggleFullscreenLastScreen(); });
    fsLast->setShortcut(QKeySequence(QStringLiteral("F11")));
    // v1.18 UI 重组：查看工具并入「视图」（原仅副屏全屏两项）
    viewMenu->addSeparator();
    viewMenu->addAction(lang("画面调节面板(&A)", "&Adjustment Panel"), this, [this]() {
        if (m_adjustBtn) m_adjustBtn->setChecked(!m_adjustBtn->isChecked());
    });
    QMenu *overlayMenu = viewMenu->addMenu(lang("截图叠加(&O)", "Snapshot &Overlay"));
    overlayMenu->addAction(lang("截取当前帧", "Capture Current Frame"), this, [this]() {
        if (m_captureBtn) m_captureBtn->click();
    });
    overlayMenu->addAction(lang("编辑叠加", "Edit Overlay"), this, [this]() {
        if (m_editBtn) m_editBtn->click();
    });
    overlayMenu->addAction(lang("显示/隐藏叠加", "Show/Hide Overlay"), this, [this]() {
        if (m_placeBtn) m_placeBtn->setChecked(!m_placeBtn->isChecked());
    });

    // Settings menu
    QMenu *settingsMenu = menuBar()->addMenu(lang("设置(&S)", "&Settings"));

    // v1.8.0 P-25：分析引擎切换菜单随 Python 引擎退役移除（libav 为唯一实现）；
    // 历史设置键 analysisEngine 不再读取，QSettings 遗留值无副作用

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

    // v1.16.1 P-54b：播放音频降噪（谱门控流式 DSP，强度与语谱图降噪滑杆共享；
    // 默认关——取证保守；scrub 拖拽片段旁路；seek/开关自动重建状态）
    QAction *pbNrAction = settingsMenu->addAction(
        lang("播放音频降噪（强度随语谱图降噪滑杆）",
             "Playback noise reduction (strength follows spectrogram NR slider)"));
    pbNrAction->setCheckable(true);
    pbNrAction->setToolTip(lang("对播放声音实时降噪（语谱图「降噪」滑杆控制强度）。\n"
                                "只影响监听，原始数据与分析结果不变。",
                                "Real-time playback denoise; strength follows the spectrogram NR slider.\n"
                                "Monitoring only — source data and analysis are untouched."));
    {
        QSettings s("LumenArc", "LumenArc");
        pbNrAction->setChecked(s.value("playbackDenoise", false).toBool());
    }
    connect(pbNrAction, &QAction::toggled, this, [this](bool on) {
        QSettings s("LumenArc", "LumenArc");
        s.setValue("playbackDenoise", on);
        applyPlaybackDenoiseSetting();
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

    // v1.18 UI 重组：语言与账号管理归入「设置」（配置类），帮助只留求助类
    settingsMenu->addSeparator();
    QMenu *langMenu = settingsMenu->addMenu(lang("语言", "Language"));
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
    settingsMenu->addAction(lang("账号管理(&A)...", "&Account..."), this, [this]() {
        AccountDialog dlg(this);
        dlg.exec();
        if (dlg.signedOut()) {
            LoginDialog login(this);
            if (login.exec() != QDialog::Accepted) close();  // 放弃登录 = 退出
        }
    });

    // Help menu
    QMenu *helpMenu = menuBar()->addMenu(lang("帮助(&H)", "&Help"));
    helpMenu->addSeparator();
    helpMenu->addAction(lang("使用手册", "User Manual"), []() {
        // v1.16.0：文件名去版本号（随包手册永远最新，名不再陈旧）
        QString path = QCoreApplication::applicationDirPath()
                       + "/追光者 Lumen Arc — 操作手册.pdf";
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    });

    helpMenu->addSeparator();
    helpMenu->addAction(lang("意见反馈", "Feedback"), this, [this]() {
        FeedbackDialog dlg(this);
        dlg.exec();
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

    // v1.18 UI 重组：工具栏拆两条——一级「播放与产出」+ 二级「分析与标注」
    QToolBar *toolBar = addToolBar(lang("播放与产出", "Playback & Deliver"));
    toolBar->setObjectName(QStringLiteral("playbackToolBar"));   // v2.0：按阶段显隐
    toolBar->setIconSize(QSize(18, 18));
    addToolBarBreak(Qt::TopToolBarArea);
    QToolBar *analysisBar = addToolBar(lang("分析与标注", "Analyze & Annotate"));
    analysisBar->setObjectName(QStringLiteral("analysisToolBar"));   // v2.0：按阶段显隐
    analysisBar->setIconSize(QSize(18, 18));

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
    m_analyzeBtn = new QPushButton(lang("亮度", "Luminance"), this);
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
    m_audioAnalysisBtn = new QPushButton(lang("音频", "Audio"), this);
    m_audioAnalysisBtn->setToolTip(lang("独立分析音频（频谱图+音量）", "Analyze audio only (spectrogram + volume)"));
    m_audioAnalysisBtn->setFixedHeight(32);
    m_audioAnalysisBtn->setStyleSheet(audioBtnStyle);
    m_audioAnalysisBtn->setEnabled(false);

    // 微变分析（2026-09-10）：显示链彩色增强，非模态面板
    m_microDiffBtn = new QPushButton(lang("微变", "Micro"), this);
    m_microDiffBtn->setToolTip(lang("微变分析：把肉眼不可见的早期烟气/辉光变成彩色云团"
                                    "（需先圈定 ROI 并采集“起火前干净段”作为基准）",
                                    "Micro-change analysis: reveal invisible early smoke as a "
                                    "colour cloud (draw an ROI and collect a clean baseline first)"));
    m_microDiffBtn->setFixedHeight(32);
    m_microDiffBtn->setStyleSheet(audioBtnStyle);
    m_microDiffBtn->setEnabled(false);

    m_setTimeBtn = new QPushButton(lang("校时", "Calibrate"), this);
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

    m_copyRoiBtn = new QPushButton(lang("复制", "Copy"), this);
    m_copyRoiBtn->setToolTip(lang("复制ROI区域 (Ctrl+Shift+C)", "Copy ROI (Ctrl+Shift+C)"));
    m_copyRoiBtn->setFixedHeight(32);
    m_copyRoiBtn->setStyleSheet(btnBase);

    m_pasteRoiBtn = new QPushButton(lang("粘贴", "Paste"), this);
    m_pasteRoiBtn->setToolTip(lang("粘贴ROI区域 (Ctrl+Shift+V)", "Paste ROI (Ctrl+Shift+V)"));
    m_pasteRoiBtn->setFixedHeight(32);
    m_pasteRoiBtn->setStyleSheet(btnBase);
    m_pasteRoiBtn->setEnabled(false);

    m_chartPanel->setAutoYRange(true);

    // --- Layout ---
    // --- Layout ---
    // 一级条（播放与产出）：秒级高频 + 产出主入口 + 素材前处理（用户拍板：保留在一级条）
    toolBar->addWidget(m_playBtn);
    toolBar->addWidget(m_pauseBtn);
    toolBar->addWidget(m_stopBtn);
    toolBar->addWidget(m_speedBtn);
    m_timeLabel = new QLabel("00:00 / 00:00", this);
    m_timeLabel->setStyleSheet(timeLabelStyle);
    toolBar->addWidget(m_timeLabel);
    toolBar->addSeparator();
    toolBar->addWidget(m_snapshotBtn);
    // P-68 导出选段视频（A/B 选段存在时可用；分段变速+图表语谱同框）
    m_exportClipBtn = new QPushButton(lang("合成", "Compose"), this);
    m_exportClipBtn->setToolTip(lang("合成导出：多段拼接 + 证据直拷/演示烧录双模式",
                                     "Compose export: multi-segment + evidence/demo modes"));
    m_exportClipBtn->setFixedHeight(32);
    m_exportClipBtn->setEnabled(false);
    connect(m_exportClipBtn, &QPushButton::clicked,
            this, &MainWindow::onExportSegmentClip);
    toolBar->addWidget(m_exportClipBtn);
    toolBar->addSeparator();

    // 素材转码拼接：高频独立任务窗口，固定在右端（2026-10 用户拍板保留一级位置）
    auto *tbSpacer = new QWidget(this);
    tbSpacer->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    toolBar->addWidget(tbSpacer);
    auto *preprocessBtn = new QPushButton(lang("转码拼接", "Transcode"), this);
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

    // v1.18 UI 重组：工作台 ▾ —— 独立窗口汇总入口（素材转码拼接仍单列在左，此处为快捷方式）
    auto *workbenchBtn = new QToolButton(this);
    workbenchBtn->setText(lang("工作台 ▾", "Workspaces ▾"));
    workbenchBtn->setPopupMode(QToolButton::InstantPopup);
    workbenchBtn->setToolTip(lang("独立工作窗口（前处理 / 合成导出 / 多机 / 直播 / 点位图）",
                                  "Independent workspaces"));
    workbenchBtn->setMinimumHeight(30);
    workbenchBtn->setFocusPolicy(Qt::NoFocus);
    workbenchBtn->setStyleSheet(QStringLiteral(
        "QToolButton { background: %1; color: %2; border: none; border-radius: 6px; "
        "padding: 4px 10px; font-weight: bold; }"
        "QToolButton:hover { background: %3; }"
        "QToolButton::menu-indicator { image: none; }")
        .arg(Theme::BgCard, Theme::TextPrimary, Theme::BgHover));
    auto *wbMenu = new QMenu(workbenchBtn);
    wbMenu->addAction(lang("素材转码拼接…", "&Transcode & Merge…"), this,
                      &MainWindow::openPreprocessWindow);
    if (m_composeExportAction)
        wbMenu->addAction(m_composeExportAction);
    wbMenu->addSeparator();
    if (m_multiCamAction)
        wbMenu->addAction(m_multiCamAction);
    wbMenu->addAction(lang("接入监控直播…", "Live Monitor (&RTSP)…"), this,
                      &MainWindow::onOpenLiveMonitor);
    wbMenu->addSeparator();
    if (m_sitemapAction)
        wbMenu->addAction(m_sitemapAction);
    workbenchBtn->setMenu(wbMenu);
    toolBar->addWidget(workbenchBtn);

    // v2.1：分析工具栏**整行取消** —— 亮度/音频/微变/校时、区域模式、复制粘贴、
    // 快照/截取/编辑/放置/画面调节，全部由 setupShell() 收进检视器「工具」区。
    // 同一批指针 reparent，启用态/勾选态自动跟随，零重复逻辑。

    // ── v2.0 压缩：以下入口已由阶段条或左下常驻面板覆盖，工具栏不再重复─
    //    转码拼接→②整备 · 合成→⑤产出 · 校时→③校时 · 工作台▾→全部被阶段条覆盖
    //    画面调节→左下常驻面板 · 模式标签→对应按钮高亮已表达
    for (QWidget *w : { static_cast<QWidget *>(preprocessBtn),
                        static_cast<QWidget *>(workbenchBtn),
                        static_cast<QWidget *>(m_exportClipBtn) }) {
        if (w)
            w->setVisible(false);
    }

    // Prevent toolbar buttons from stealing keyboard focus
    for (auto *btn : toolBar->findChildren<QPushButton*>())
        btn->setFocusPolicy(Qt::NoFocus);

    // v2.1：分析工具栏空壳撤掉（按钮是 MainWindow 成员，已被 reparent 到检视器）
    removeToolBar(analysisBar);
    analysisBar->hide();
    analysisBar->deleteLater();

    // v1.18 UI 重组：Ctrl+K 命令面板（菜单项可搜索直达，新增功能不必再抢菜单槽位）
    auto *paletteSc = new QShortcut(QKeySequence(QStringLiteral("Ctrl+K")), this);
    connect(paletteSc, &QShortcut::activated, this, &MainWindow::showCommandPalette);

    // 播放画面调节面板（2026-08-14）：dock 常驻模式，默认隐藏，
    // 工具栏「画面调节」按钮调出后一直开着直到手动关闭。
    m_adjustPanel = new PlaybackAdjustPanel(this);
    // v1.7.1：画面调节面板控件不劫持全局快捷键
    for (QWidget *w : m_adjustPanel->findChildren<QWidget *>()) {
        if (qobject_cast<QSlider *>(w) || qobject_cast<QSpinBox *>(w)
            || qobject_cast<QDoubleSpinBox *>(w) || qobject_cast<QPushButton *>(w))
            w->installEventFilter(m_keyGuard);
    }
    // v2.0 方案 B：画面调节面板放**左下**（原来在右侧，挤压视频宽度）
    addDockWidget(Qt::LeftDockWidgetArea, m_adjustPanel);
    m_adjustPanel->hide();
}



/// @brief 中央区：[视频|放大镜] 顶行 + 量化分析容器 + 语谱图容器（含折叠钮/底噪滑杆/降噪滑杆/应用钮）
/// v1.17.0 P-79：自构造函数逐字抽取（共享样式串经参数传入）
namespace {

/// 双击轨道分隔条 → 两条轨道恢复等高（v2.1 交互：高度均分可随时复位）
class TrackBalanceFilter : public QObject
{
public:
    explicit TrackBalanceFilter(QSplitter *sp)
        : QObject(sp), m_sp(sp) {}

protected:
    bool eventFilter(QObject *o, QEvent *e) override
    {
        if (e->type() == QEvent::MouseButtonDblClick && m_sp && m_sp->count() >= 3) {
            QList<int> s = m_sp->sizes();
            const int total = s[1] + s[2];
            s[1] = total / 2;
            s[2] = total - total / 2;
            m_sp->setSizes(s);
            return true;
        }
        return QObject::eventFilter(o, e);
    }

private:
    QSplitter *m_sp = nullptr;
};

} // namespace

void MainWindow::buildCentralLayout(const QString &titleBarStyle,
                                    const QString &collapseBtnStyle,
                                    const QString &titleLabelStyle)
{
    // --- 轨道 1：亮度 / 音量 ---
    // v2.1：取消左侧 56px 名称列 → 悬浮徽标叠在图形左上角，时间轴从 x=0 拉满。
    m_chartContainer = new QWidget(m_splitter);
    auto *chartStack = new QGridLayout(m_chartContainer);
    chartStack->setContentsMargins(0, 0, 0, 0);
    chartStack->setSpacing(0);

    m_chartContent = new QWidget(m_chartContainer);
    auto *chartContentLayout = new QVBoxLayout(m_chartContent);
    chartContentLayout->setContentsMargins(0, 0, 0, 0);
    chartContentLayout->addWidget(m_chartPanel);

    // 悬浮徽标：▾ 收起 + 色点 + 名称（半透明，不抢图形）
    auto *chartBadge = new QWidget(m_chartContainer);
    chartBadge->setObjectName(QStringLiteral("trackBadge"));
    chartBadge->setAttribute(Qt::WA_StyledBackground, true);
    chartBadge->setStyleSheet(QStringLiteral(
        "QWidget#trackBadge{background:rgba(22,24,29,96);border:1px solid %1;"
        "border-radius:7px;}"
        "QWidget#trackBadge:hover{background:rgba(22,24,29,232);border-color:%2;}")
        .arg(Theme::Border, Theme::Accent));
    auto *chartGutter = new QHBoxLayout(chartBadge);
    chartGutter->setContentsMargins(3, 1, 8, 1);
    chartGutter->setSpacing(4);

    m_chartCollapseBtn = new ui::ChevronButton(chartBadge);
    m_chartCollapseBtn->setToolTip(lang("收起 / 展开（双击分隔条可复位均分）",
                                        "Collapse / Expand (double-click the handle to balance)"));

    auto *chartDot = new QLabel(QStringLiteral("\u25CF"), chartBadge);
    chartDot->setStyleSheet(
        QStringLiteral("QLabel{color:%1;font-size:10px;background:transparent;}")
            .arg(Theme::DataPalette.isEmpty() ? Theme::Accent
                                              : Theme::DataPalette[0].name()));
    auto *chartTitleLabel = new QLabel(lang("亮度 · 音量", "Luma · Audio"), chartBadge);
    chartTitleLabel->setStyleSheet(titleLabelStyle);
    chartTitleLabel->setToolTip(lang("亮度 · 音量", "Luma · Audio"));

    chartGutter->addWidget(m_chartCollapseBtn);
    chartGutter->addWidget(chartDot);
    chartGutter->addWidget(chartTitleLabel);

    // 游标读数左界让出徽标宽度（否则两者在左上角叠成一坨灰块）
    if (m_chartPanel)
        m_chartPanel->setCursorReadoutMinX(chartBadge->sizeHint().width() + 3 + 10);

    // 徽标外框：左上偏移 40px，避开 y 轴刻度文字
    auto *chartBadgeHolder = new QWidget(m_chartContainer);
    auto *chartHolderLay = new QVBoxLayout(chartBadgeHolder);
    chartHolderLay->setContentsMargins(3, 2, 0, 0);
    chartHolderLay->setSpacing(0);
    chartHolderLay->addWidget(chartBadge);
    chartHolderLay->addStretch();

    // 同一格里叠放：图形铺满，徽标浮在左上
    chartStack->addWidget(m_chartContent, 0, 0);
    chartStack->addWidget(chartBadgeHolder, 0, 0, Qt::AlignLeft | Qt::AlignTop);
    chartStack->setRowStretch(0, 1);
    chartStack->setColumnStretch(0, 1);

    m_splitter->addWidget(m_chartContainer);
    m_splitter->setStretchFactor(1, 1);   // v2.1：与语谱等分

    // --- 轨道 2：语谱（同样取消左列，图标悬浮）---
    m_spectrogramContainer = new QWidget(m_splitter);
    auto *specStack = new QGridLayout(m_spectrogramContainer);
    specStack->setContentsMargins(0, 0, 0, 0);
    specStack->setSpacing(0);

    // 隐藏壳：仅暂存「底噪/降噪/应用」控件，setupShell() 会把它们搬到底部任务带
    auto *specTitleBar = new QWidget(m_spectrogramContainer);
    specTitleBar->hide();
    auto *specGutter = new QVBoxLayout(specTitleBar);
    specGutter->setContentsMargins(6, 4, 6, 4);
    specGutter->setSpacing(3);

    // 悬浮徽标：▾ + 色点 + 语谱
    auto *specBadge = new QWidget(m_spectrogramContainer);
    specBadge->setObjectName(QStringLiteral("trackBadge"));
    specBadge->setAttribute(Qt::WA_StyledBackground, true);
    specBadge->setStyleSheet(QStringLiteral(
        "QWidget#trackBadge{background:rgba(22,24,29,96);border:1px solid %1;"
        "border-radius:7px;}"
        "QWidget#trackBadge:hover{background:rgba(22,24,29,232);border-color:%2;}")
        .arg(Theme::Border, Theme::Accent));
    auto *specHeadRow = new QHBoxLayout(specBadge);
    specHeadRow->setContentsMargins(3, 1, 8, 1);
    specHeadRow->setSpacing(4);

    m_spectrogramCollapseBtn = new ui::ChevronButton(specBadge);
    m_spectrogramCollapseBtn->setToolTip(lang("收起 / 展开（双击分隔条可复位均分）",
                                              "Collapse / Expand (double-click the handle to balance)"));

    auto *specDot = new QLabel(QStringLiteral("\u25CF"), specBadge);
    specDot->setStyleSheet(
        QStringLiteral("QLabel{color:%1;font-size:10px;background:transparent;}")
            .arg(Theme::DataPalette.size() > 4 ? Theme::DataPalette[4].name()
                                               : Theme::Accent));
    auto *specTitleLabel = new QLabel(lang("语谱", "Spectrum"), specBadge);
    specTitleLabel->setStyleSheet(titleLabelStyle);
    specTitleLabel->setToolTip(lang("语谱图", "Spectrogram"));

    specHeadRow->addWidget(m_spectrogramCollapseBtn);
    specHeadRow->addWidget(specDot);
    specHeadRow->addWidget(specTitleLabel);

    // 底噪行
    auto *nfRow = new QHBoxLayout();
    nfRow->setSpacing(4);
    m_noiseFloorLabel = new QLabel(lang("底噪", "Noise"), specTitleBar);
    m_noiseFloorLabel->setStyleSheet("QLabel { color: " + Theme::TextSecond + "; font-size: 10px; }");

    m_noiseFloorSlider = new QSlider(Qt::Horizontal, specTitleBar);
    m_noiseFloorSlider->setRange(-100, 0);  // -10.0 to 0.0 dB (x10)
    m_noiseFloorSlider->setValue(-55);       // default -5.5 dB
    m_noiseFloorSlider->setToolTip(lang("底噪阈值", "Noise floor"));
    m_noiseFloorSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: " + Theme::Border + "; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: " + Theme::Info + "; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; }"
        "QSlider::sub-page:horizontal { background: " + Theme::Info + "; border-radius: 2px; }"
    );
    m_noiseFloorValueLabel = new QLabel("-5.5", specTitleBar);
    m_noiseFloorValueLabel->setStyleSheet("QLabel { color: " + Theme::Info + "; font-size: 10px; font-family: Consolas; min-width: 26px; }");

    nfRow->addWidget(m_noiseFloorLabel);
    nfRow->addWidget(m_noiseFloorSlider, 1);
    nfRow->addWidget(m_noiseFloorValueLabel);
    specGutter->addLayout(nfRow);

    // 降噪行
    auto *nrRow = new QHBoxLayout();
    nrRow->setSpacing(4);
    m_noiseReductionLabel = new QLabel(lang("降噪", "NR"), specTitleBar);
    m_noiseReductionLabel->setStyleSheet("QLabel { color: " + Theme::TextSecond + "; font-size: 10px; }");

    m_noiseReductionSlider = new QSlider(Qt::Horizontal, specTitleBar);
    m_noiseReductionSlider->setRange(0, 50);  // 0.0 to 5.0 (x10)
    m_noiseReductionSlider->setValue(0);       // default off
    m_noiseReductionSlider->setToolTip(lang("降噪强度（需重新分析）", "Noise reduction (re-analysis needed)"));
    m_noiseReductionSlider->setStyleSheet(
        "QSlider::groove:horizontal { background: " + Theme::Border + "; height: 4px; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: " + Theme::Accent + "; width: 12px; height: 12px; margin: -4px 0; border-radius: 6px; }"
        "QSlider::sub-page:horizontal { background: " + Theme::Accent + "; border-radius: 2px; }"
    );
    m_noiseReductionValueLabel = new QLabel("0.0", specTitleBar);
    m_noiseReductionValueLabel->setStyleSheet("QLabel { color: " + Theme::Accent + "; font-size: 10px; font-family: Consolas; min-width: 26px; }");

    nrRow->addWidget(m_noiseReductionLabel);
    nrRow->addWidget(m_noiseReductionSlider, 1);
    nrRow->addWidget(m_noiseReductionValueLabel);
    specGutter->addLayout(nrRow);

    // 安装事件过滤器，使全局快捷键（方向键、空格等）不被 Slider 拦截
    m_noiseFloorSlider->installEventFilter(m_keyGuard);
    m_noiseReductionSlider->installEventFilter(m_keyGuard);

    m_nrApplyBtn = new QPushButton(lang("应用降噪", "Apply"), specTitleBar);
    m_nrApplyBtn->setFixedHeight(20);
    m_nrApplyBtn->setFocusPolicy(Qt::NoFocus);
    m_nrApplyBtn->setStyleSheet(
        "QPushButton { background: transparent; color: " + Theme::Accent + "; border: 1px solid " + Theme::Accent + "; border-radius: 10px; font-size: 10px; }"
        "QPushButton:hover { background: " + Theme::Accent + "; color: " + Theme::AccentOnDark + "; }"
        "QPushButton:disabled { color: " + Theme::TextMuted + "; border-color: " + Theme::Border + "; background: transparent; }"
    );
    specGutter->addWidget(m_nrApplyBtn);
    specGutter->addStretch();

    m_spectrogramContent = new QWidget(m_spectrogramContainer);
    auto *specContentLayout = new QVBoxLayout(m_spectrogramContent);
    specContentLayout->setContentsMargins(0, 0, 0, 0);
    specContentLayout->addWidget(m_spectrogramEnhanced);

    auto *specBadgeHolder = new QWidget(m_spectrogramContainer);
    auto *specHolderLay = new QVBoxLayout(specBadgeHolder);
    specHolderLay->setContentsMargins(3, 2, 0, 0);
    specHolderLay->setSpacing(0);
    specHolderLay->addWidget(specBadge);
    specHolderLay->addStretch();

    specStack->addWidget(m_spectrogramContent, 0, 0);
    specStack->addWidget(specBadgeHolder, 0, 0, Qt::AlignLeft | Qt::AlignTop);
    specStack->setRowStretch(0, 1);
    specStack->setColumnStretch(0, 1);

    m_splitter->addWidget(m_spectrogramContainer);
    m_splitter->setStretchFactor(2, 1);   // v2.1：与亮度/音量等分

    // Store original splitter sizes for collapse/expand（v2.1：两条轨道等高）
    m_splitterSizes = {550, 230, 230};
    m_chartSavedSizes = {550, 230, 230};
    m_spectrogramSavedSizes = {550, 230, 230};
    // v1.13.1：初始纵向比例落地改在 resizeEvent 首次有效尺寸时执行
    // （见 m_initialSplitApplied）——ctor 里 singleShot(0) 会被 main.cpp
    // splash 的 processEvents 提前触发（窗口未 show 高度无效），真机被跳过。

    // v2.3：两条轨道的收起/展开统一算尺寸
    // 原来靠 m_chartContainer->minimumHeight() == 24 判断对轨是否收起，
    // 但收起时设的是 28 —— 条件永远不成立，于是「先开语谱」会把已经收起的
    // 亮度栏按历史高度一起拉出来，而它的内容还是隐藏的 → 空白轨。
    constexpr int kTrackCollapsedH = 28;

    auto applyTrackState = [this]() {
        if (!m_splitter || m_splitter->count() != 3)
            return;
        const QList<int> cur = m_splitter->sizes();
        const int total = cur[0] + cur[1] + cur[2];

        int chartH = m_chartCollapsed ? kTrackCollapsedH : qMax(120, m_chartPrefH);
        int specH  = m_specCollapsed  ? kTrackCollapsedH : qMax(120, m_specPrefH);

        // 画面至少留 160；不够就按比例压两条展开中的轨道（不低于 90）
        int videoH = total - chartH - specH;
        if (videoH < 160) {
            const int need = 160 - videoH;
            const int chartRoom = m_chartCollapsed ? 0 : qMax(0, chartH - 90);
            const int specRoom  = m_specCollapsed  ? 0 : qMax(0, specH - 90);
            const int room = chartRoom + specRoom;
            if (room > 0) {
                const int cut = qMin(need, room);
                if (chartRoom > 0)
                    chartH -= cut * chartRoom / room;
                if (specRoom > 0)
                    specH -= cut * specRoom / room;
            }
            videoH = qMax(60, total - chartH - specH);
        }
        animateSplitterSizes({videoH, chartH, specH});
    };

    auto setTrackCollapsed = [this, applyTrackState](bool chart) {
        bool &flag = chart ? m_chartCollapsed : m_specCollapsed;
        QWidget *content = chart ? m_chartContent : m_spectrogramContent;
        QWidget *container = chart ? m_chartContainer : m_spectrogramContainer;
        ui::ChevronButton *btn = chart ? m_chartCollapseBtn : m_spectrogramCollapseBtn;

        // 收起前记下当前真实高度，展开时回到这个高度（而不是写死的同一档）
        if (!flag) {
            const QList<int> s = m_splitter->sizes();
            const int idx = chart ? 1 : 2;
            if (s.size() == 3 && s[idx] > 60)
                (chart ? m_chartPrefH : m_specPrefH) = s[idx];
        }
        flag = !flag;
        if (btn)
            btn->setExpanded(!flag);
        if (content) {
            content->setVisible(!flag);
            content->setMinimumSize(0, 0);
            content->setMaximumSize(flag ? QSize(0, 0)
                                         : QSize(QWIDGETSIZE_MAX, QWIDGETSIZE_MAX));
        }
        if (container)
            container->setMinimumHeight(flag ? kTrackCollapsedH : 0);
        applyTrackState();
    };

    connect(m_chartCollapseBtn, &ui::ChevronButton::clicked, this,
            [setTrackCollapsed]() { setTrackCollapsed(true); });
    connect(m_spectrogramCollapseBtn, &ui::ChevronButton::clicked, this,
            [setTrackCollapsed]() { setTrackCollapsed(false); });


    // v2.1 交互：双击任一条轨道分隔条 → 亮度/音量 与 语谱 高度均分
    {
        auto *bal = new TrackBalanceFilter(m_splitter);
        for (int i = 1; i < m_splitter->count(); ++i) {
            if (QSplitterHandle *h = m_splitter->handle(i))
                h->installEventFilter(bal);
        }
    }

}

/// @brief 视频列表 dock + 侧栏 + 折叠占位条（含其全部 connect）
/// v1.17.0 P-79：自构造函数逐字抽取
void MainWindow::buildVideoListDock(const QString &collapseBtnStyle)
{
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

}

/// @brief 状态栏：操作反馈/状态/硬解适配器/进度条/取消钮
/// v1.17.0 P-79：自构造函数逐字抽取（setAcceptDrops 留在构造函数原位）
void MainWindow::buildStatusBar()
{
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

}

/// @brief 核心服务装配：AnalysisController/UiState/CalibrationService/CaseManager/ProjectIO + 其 connect
/// v1.17.0 P-79：自构造函数逐字抽取
void MainWindow::buildServices()
{
    // v1.9.0 P-31 T3：引擎 + 任务注册 + 服务装配归 AnalysisController
    // （P-25 退役后 libav 为唯一实现；A/B 对拍基线见 DEVELOPMENT_PLAN_V1.8 §5）
    m_analysisController = new AnalysisController(m_roiModel, m_timelineModel, this);
    m_analysisEngine = m_analysisController->engine();          // 非持有别名
    m_taskService = m_analysisController->taskService();        // 非持有别名
    // v1.9.0 P-31 T4：时长 SSOT（图表/列表/时间标签派生刷新入口）
    m_uiState = new UiState(this);
    connect(m_uiState, &UiState::effectiveDurationChanged, this, [this](qint64) {
        updateTimeDisplay();
    });

    // 校时服务（v1.2.0：三点识别/absStart/sidecar 继承；产出仅预填，
    // 「采用」由 TimeSettingsDialog 决定）
    m_calibrationService = new CalibrationService(m_analysisEngine, this);

    // 案件管理器（v1.3.0 M2）：唯一持有打开的案件；无案件时所有分流
    // 接口（vlaPathFor/timestampRoiFor…）自动回落独立模式老行为
    m_caseManager = new CaseManager(this);
    // v1.9.0 P-31 T1：工程读写服务（vla/CSV/时间戳ROI/徽标）
    m_projectIo = new ProjectIO(m_caseManager, m_timelineModel, this);
    // C2 不静默：后台保存失败上表面（此前 vlaSaved 无消费者，写盘失败用户无感）
    connect(m_projectIo, &ProjectIO::vlaSaved, this,
            [this](const QString &path, bool ok) {
                if (!ok)
                    showOperationStatus(
                        lang("⚠ 分析结果保存失败：%1", "⚠ Failed to save: %1")
                            .arg(QFileInfo(path).fileName()));
            });
    // 校时证据帧目录分流（M2 任务8）：入案→案件 evidence/calibration/V###；
    // 未入案/无案件→CaseManager 内部回落老路径 LumenArc_Calibration
    m_calibrationService->setEvidenceDirResolver(
        [this](const QString &videoPath) {
            return m_caseManager->evidenceDirFor(videoPath);
        });
    // P-98：秒级对齐（2~4 分钟长任务）结果落库接线——关窗/切视频也不丢结果
    wireTickAlignPersistence();
}

/// @brief 案件模式 UI：案件折叠占位条 + CaseDock + 折叠条 + 打开面板 + CaseManager 接线 + 状态栏📁
/// 及截图叠加层/时间戳框选 connect。v1.17.0 P-79：自构造函数逐字抽取（两段拼接，原相对顺序保持）
void MainWindow::buildCaseUi(const QString &collapseBtnStyle)
{
    // v1.13.1（用户反馈①）：案件面板同款折叠占位条——打开放大镜时
    // 案件列表与旧版视频列表一样收成细条，点击 ▶ 展开。
    m_casePlaceholder = new QDockWidget(this);
    m_casePlaceholder->setFeatures(QDockWidget::NoDockWidgetFeatures);
    m_casePlaceholder->setTitleBarWidget(new QWidget());
    m_casePlaceholder->titleBarWidget()->setFixedHeight(0);
    m_casePlaceholder->setStyleSheet(
        "QDockWidget { border: 0px; padding: 0px; }"
        "QDockWidget::title { padding: 0px; margin: 0px; border: 0px; }");
    auto *casePhContent = new QWidget();
    casePhContent->setFixedWidth(28);
    casePhContent->setStyleSheet("background: transparent;");   // v2.1：去掉左侧色块
    auto *casePhLay = new QVBoxLayout(casePhContent);
    casePhLay->setContentsMargins(2, 4, 2, 4);
    casePhLay->setSpacing(4);
    auto *casePhExpandBtn = new QPushButton(QString::fromUtf8("\xe2\x96\xb6"),
                                            casePhContent);   // ▶
    casePhExpandBtn->setFixedSize(22, 22);
    casePhExpandBtn->setStyleSheet(collapseBtnStyle);
    casePhExpandBtn->setToolTip(lang("展开案件列表", "Expand case list"));
    casePhExpandBtn->setFocusPolicy(Qt::NoFocus);
    casePhLay->addWidget(casePhExpandBtn);
    casePhLay->addStretch();
    {
        const QString vertText = lang("案件列表", "Case");
        for (const QChar &ch : vertText) {
            auto *chLabel = new QLabel(QString(ch), casePhContent);
            chLabel->setStyleSheet("color: " + Theme::TextSecond
                + "; font-size: 11px; background: transparent;");
            chLabel->setAlignment(Qt::AlignCenter);
            casePhLay->addWidget(chLabel);
        }
    }
    casePhLay->addStretch();
    m_casePlaceholder->setWidget(casePhContent);
    addDockWidget(Qt::LeftDockWidgetArea, m_casePlaceholder);
    resizeDocks({m_casePlaceholder}, {24}, Qt::Horizontal);
    m_casePlaceholder->setVisible(false);
    connect(casePhExpandBtn, &QPushButton::clicked, this, [this]() {
        setCaseListExpanded(true);   // v2.1：唯一入口
    });


    // ---- 案件模式接线（v1.3.0 M2 任务10）----
    m_caseDock = new CaseDock(m_caseManager, this);
    // v1.7.1：全局快捷键不被案件树劫持（与视频列表/语谱滑块同机制——
    // 过滤器须装在具体控件上；文本输入控件由 eventFilter 保护放行）
    for (QWidget *w : m_caseDock->findChildren<QWidget *>()) {
        if (qobject_cast<QTreeWidget *>(w) || qobject_cast<QPushButton *>(w))
            w->installEventFilter(m_keyGuard);
    }
    // v2.3：一级标题（案件 / 检视器）字体与边框完全一致
    m_caseDock->setStyleSheet(QStringLiteral(
            "QDockWidget{font-size:11px;font-weight:600;color:%1;}"
            "QDockWidget::title{background:%2;padding:5px 8px;"
            "border-bottom:1px solid %3;text-align:left;}")
            .arg(Theme::TextPrimary, Theme::BgPanel, Theme::Border));
    addDockWidget(Qt::LeftDockWidgetArea, m_caseDock);
    resizeDocks({m_caseDock}, {250}, Qt::Horizontal);
    // v2.2：案件区高度下限——检视器内容变多后会把案件树挤到只剩几行
    m_caseDock->setMinimumHeight(380);
    m_caseDock->setVisible(false);   // 仅案件模式可见（替代视频列表）

    // v2.2：去掉案件坞内的左右竖条（用户：不要左右折叠了，
    // 只要纵向滚动的案件内容即可）。占位细条仍保留给放大镜自动折叠用。
    m_caseListContent = m_caseDock->widget();
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
    // v1.18.x §98：移除视频 = 丢弃该路径的会话内存状态（校时/帧选回忆/分析快照）。
    // 此前 VideoStateManager::removeState 全库零调用 → 同路径重新导入会把旧校时
    // 「复活」（实测：顺德件移除 V13 后导入为 V14，仍带着旧的 2 点 rate=1.0 校准）。
    connect(m_caseManager, &CaseManager::videoRemoved, this,
            [this, refreshDock](const QString &, const QString &originalPath,
                                const QString &effectivePath) {
                const QString cur = m_sessionMgr->currentVideoPath();
                bool clearedCurrent = false;
                for (const QString &p : {originalPath, effectivePath}) {
                    if (p.isEmpty())
                        continue;
                    if (!cur.isEmpty()
                        && QString::compare(QDir::cleanPath(p),
                                            QDir::cleanPath(cur),
                                            Qt::CaseInsensitive) == 0)
                        clearedCurrent = true;
                    m_sessionMgr->removeState(p);
                }
                if (clearedCurrent) {
                    m_calibration = TimeCalibration();
                    m_chartPanel->setCalibration(m_calibration);
                    showOperationStatus(lang(
                        "该视频已移出案件，内存中的校时/分析已清除（文件仍在播放）",
                        "Video removed from case: in-memory calibration/analysis "
                        "cleared (playback continues)"));
                }
                refreshDock();
            });
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
                if (m_sessionMgr)
                    m_sessionMgr->migrateKey(oldPath, newPath);
                if (QDir::cleanPath(m_sessionMgr->currentVideoPath())
                    == QDir::cleanPath(oldPath))
                    m_sessionMgr->setCurrentVideoPath(newPath);
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
    // lambda 只读成员（m_roiDialog/m_sessionMgr->currentVideoPath()/m_videoWidget），
    // 与单次连接语义自洽。
    // v1.2.x UX：拖拽松开/叠加层「确认」→ timestampRoiReady → 校时窗恢复并
    // 提供「确认并开始校时」（ready 与 confirmed 由按钮同时发，接 ready 即可）
    connect(m_videoWidget, &VideoWidget::timestampRoiReady,
            this, [this](const QRectF &norm) {
                if (m_roiDialog) {
                    m_projectIo->saveTimestampRoi(m_sessionMgr->currentVideoPath(), norm);
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

}

/// v1.18 UI 重组：Ctrl+K 命令面板。
/// 命令表不单独维护 —— 直接从菜单栏遍历收集，菜单加一项面板自动多一项，
/// 避免"新功能要同时在两处登记"的漂移。禁用的菜单项在面板里也显示为禁用。
void MainWindow::showCommandPalette()
{
    struct Cmd { QString text; QString group; QAction *action; };
    QVector<Cmd> cmds;
    const auto menus = menuBar()->findChildren<QMenu *>(QString(),
                                                        Qt::FindDirectChildrenOnly);
    for (QMenu *m : menus) {
        if (!m || !m->menuAction() || m->menuAction()->isSeparator())
            continue;
        const QString group = QString(m->title()).remove(QLatin1Char('&'));
        for (QAction *a : m->actions()) {
            if (!a || a->isSeparator() || a->menu())
                continue;   // 子菜单本身不是命令
            cmds.append({QString(a->text()).remove(QLatin1Char('&')), group, a});
        }
    }

    QDialog dlg(this);
    dlg.setWindowTitle(lang("命令面板", "Command Palette"));
    dlg.setMinimumSize(580, 430);
    auto *v = new QVBoxLayout(&dlg);
    auto *edit = new QLineEdit(&dlg);
    edit->setPlaceholderText(lang("输入命令名称…（↑↓ 选择，Enter 执行，Esc 关闭）",
                                  "Type a command… (↑↓ select, Enter run, Esc close)"));
    auto *list = new QListWidget(&dlg);
    list->setAlternatingRowColors(true);
    v->addWidget(edit);
    v->addWidget(list, 1);

    auto refresh = [&]() {
        const QString needle = edit->text().trimmed();
        list->clear();
        for (int i = 0; i < cmds.size(); ++i) {
            const Cmd &c = cmds[i];
            if (!needle.isEmpty()
                && !c.text.contains(needle, Qt::CaseInsensitive)
                && !c.group.contains(needle, Qt::CaseInsensitive))
                continue;
            auto *it = new QListWidgetItem(
                QStringLiteral("%1        · %2").arg(c.text, c.group), list);
            it->setData(Qt::UserRole, i);
            if (!c.action->isEnabled()) {
                it->setForeground(QBrush(QColor(Theme::TextMuted)));
                it->setFlags(it->flags() & ~Qt::ItemIsEnabled);
            }
        }
        if (list->count() > 0)
            list->setCurrentRow(0);
    };
    connect(edit, &QLineEdit::textChanged, &dlg, [&refresh](const QString &) { refresh(); });

    auto run = [&]() {
        auto *it = list->currentItem();
        if (!it || !(it->flags() & Qt::ItemIsEnabled))
            return;
        const int idx = it->data(Qt::UserRole).toInt();
        if (idx < 0 || idx >= cmds.size())
            return;
        QAction *a = cmds[idx].action;
        if (!a || !a->isEnabled())
            return;
        dlg.accept();
        a->trigger();
    };
    connect(list, &QListWidget::itemActivated, &dlg, [&run](QListWidgetItem *) { run(); });
    connect(edit, &QLineEdit::returnPressed, &dlg, [&run]() { run(); });

    refresh();
    edit->setFocus();
    dlg.exec();
}
