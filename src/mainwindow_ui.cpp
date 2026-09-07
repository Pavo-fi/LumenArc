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
#include "app/report_service.h"
#include "app/report_docx_builder.h"
#include "reportpreflightdialog.h"
#include "sitemapeditordialog.h"
#include <QProgressDialog>
#include <QFutureWatcher>
#include <QtConcurrent/QtConcurrent>
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
    setWindowTitle(lang("追光者 Lumen Arc v1.16.1", "Lumen Arc v1.16.1") + buildStamp());
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
    // v1.3.0 M2 任务7：临时打开（不入案）——有打开案件时与 Ctrl+O 的唯一区别
    fileMenu->addAction(lang("临时打开视频（不入案）(&T)...", "Open Video &Temporarily (no case)..."), this, &MainWindow::onOpenFileTemporary);
    // P-57 U-1：独立模式 2 路对比播放（无案件也可用，双临时进槽位）
    fileMenu->addAction(lang("多机对比播放（2 路）(&M)...", "&Multi-cam Compare Playback (2 lanes)..."),
                        this, &MainWindow::onMultiCamStandalone);

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
    // P-28 报告模块：生成分析报告（草稿→案内 reports/）
    m_genReportAction = caseMenu->addAction(
        lang("生成分析报告(&G)...", "&Generate Analysis Report..."), this, [this]() {
        if (m_caseManager->meta().videos.isEmpty()) {
            QMessageBox::warning(this, lang("生成分析报告", "Generate Report"),
                lang("请先打开案件并入案视频。", "Open a case with videos first."));
            return;
        }
        // P-28 批次③：自检 + 补录闸门（❌阻断/⚠️放行；补录落 extraFields）
        ReportPreflightDialog preflight(m_caseManager, m_sessionMgr->stateManager(), this);
        if (preflight.exec() != QDialog::Accepted)
            return;
        // 终生成：哈希补算走工作线程 + 进度对话框（可取消）
        QProgressDialog prog(lang("正在生成报告…", "Generating report..."),
                             lang("取消", "Cancel"), 0, 1000, this);
        prog.setWindowTitle(lang("生成分析报告", "Generate Report"));
        prog.setWindowModality(Qt::WindowModal);
        prog.setMinimumDuration(0);
        prog.setValue(0);
        std::atomic<bool> cancel{false};
        connect(&prog, &QProgressDialog::canceled, this, [&] { cancel = true; });
        auto cb = [&](const QString &stage, double f) -> bool {
            QMetaObject::invokeMethod(&prog, [&prog, stage, f] {
                prog.setLabelText(stage);
                if (f >= 0.0)
                    prog.setValue(int(f * 1000));
            }, Qt::QueuedConnection);
            return !cancel.load();
        };
        CaseManager *cm = m_caseManager;
        VideoStateManager *vsm = m_sessionMgr->stateManager();
        QFutureWatcher<ReportData> watcher;
        QEventLoop loop;
        connect(&watcher, &QFutureWatcher<ReportData>::finished,
                &loop, &QEventLoop::quit);
        watcher.setFuture(QtConcurrent::run([cm, vsm, &cb]() {
            return ReportService::collect(cm, vsm, /*computeHashes=*/true, cb,
                                          nullptr);
        }));
        loop.exec();
        prog.reset();
        if (cancel.load())
            return;
        ReportData rd = watcher.result();
        // 图表光栅（GUI 线程离屏渲染）
        prog.setLabelText(lang("渲染曲线图…", "Rendering charts..."));
        prog.setRange(0, 0);
        prog.show();
        ReportService::renderChartImages(cm, vsm, rd);
        prog.reset();
        const QString dirPath = m_caseManager->caseDir() + QStringLiteral("/reports");
        QDir().mkpath(dirPath);
        const QString out = dirPath + QStringLiteral("/火灾视频分析报告_%1.docx")
            .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmmss")));
        const QString err = ReportDocxBuilder::build(rd, out);
        if (!err.isEmpty()) {
            QMessageBox::critical(this, lang("生成分析报告", "Generate Report"), err);
            return;
        }
        qInfo() << "report: docx written" << out;
        QMessageBox box(this);
        box.setWindowTitle(lang("生成分析报告", "Generate Report"));
        box.setText(lang("报告已生成：%1", "Report written: %1").arg(out));
        box.addButton(QMessageBox::Ok);
        QPushButton *openBtn = box.addButton(lang("打开文件夹", "Open Folder"),
                                             QMessageBox::ActionRole);
        box.exec();
        if (box.clickedButton() == openBtn)
            QDesktopServices::openUrl(QUrl::fromLocalFile(dirPath));
    });
    m_genReportAction->setEnabled(false);
    // P-74 点位图编辑器（报告二(三)节成品图来源）
    m_sitemapAction = caseMenu->addAction(
        lang("编辑监控点位图(&M)...", "Edit Site &Map..."), this, [this]() {
        SiteMapEditorDialog dlg(m_caseManager, this);
        dlg.exec();
    });
    m_sitemapAction->setEnabled(false);
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
    // P-57/P-59：多机同步播放大窗（机位勾选面板引导；案件开着即可进，
    // 校时状态在面板内逐路标识——不再按校时数置灰，新手也能摸到入口）
    m_multiCamAction = caseMenu->addAction(
        lang("多机同步播放(&M)...", "&Multi-camera Synced Playback..."), this,
        &MainWindow::onMultiCamView);
    m_multiCamAction->setEnabled(false);
    // 案件开着即可用（面板内引导空案/未校时场景）→ 菜单弹出时动态置灰
    connect(caseMenu, &QMenu::aboutToShow, this, [this]() {
        if (m_multiCamAction)
            m_multiCamAction->setEnabled(m_caseManager->isOpen());
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

    QMenu *exportMenu = menuBar()->addMenu(lang("导出(&X)", "&Export"));
    exportMenu->addAction(lang("导出为 CSV(&C)...", "Export to &CSV..."), this, &MainWindow::onExportCsv);

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
    helpMenu->addAction(lang("账号管理", "Account"), this, [this]() {
        AccountDialog dlg(this);
        dlg.exec();
        if (dlg.signedOut()) {
            LoginDialog login(this);
            if (login.exec() != QDialog::Accepted) close();  // 放弃登录 = 退出
        }
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
    // P-68 导出选段视频（A/B 选段存在时可用；分段变速+图表语谱同框）
    m_exportClipBtn = new QPushButton(lang("合成导出", "Compose Export"), this);
    m_exportClipBtn->setToolTip(lang("合成导出：多段拼接 + 证据直拷/演示烧录双模式",
                                     "Compose export: multi-segment + evidence/demo modes"));
    m_exportClipBtn->setFixedHeight(32);
    m_exportClipBtn->setEnabled(false);
    connect(m_exportClipBtn, &QPushButton::clicked,
            this, &MainWindow::onExportSegmentClip);
    toolBar->addWidget(m_exportClipBtn);
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
    // v1.7.1：画面调节面板控件不劫持全局快捷键
    for (QWidget *w : m_adjustPanel->findChildren<QWidget *>()) {
        if (qobject_cast<QSlider *>(w) || qobject_cast<QSpinBox *>(w)
            || qobject_cast<QDoubleSpinBox *>(w) || qobject_cast<QPushButton *>(w))
            w->installEventFilter(m_keyGuard);
    }
    addDockWidget(Qt::RightDockWidgetArea, m_adjustPanel);
    m_adjustPanel->hide();
}



/// @brief 中央区：[视频|放大镜] 顶行 + 量化分析容器 + 语谱图容器（含折叠钮/底噪滑杆/降噪滑杆/应用钮）
/// v1.17.0 P-79：自构造函数逐字抽取（共享样式串经参数传入）
void MainWindow::buildCentralLayout(const QString &titleBarStyle,
                                    const QString &collapseBtnStyle,
                                    const QString &titleLabelStyle)
{
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
    m_splitter->setStretchFactor(1, 21);  // ~21%

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
    m_noiseFloorSlider->installEventFilter(m_keyGuard);
    m_noiseReductionSlider->installEventFilter(m_keyGuard);

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
    m_splitter->setStretchFactor(2, 15);  // ~15%

    // Store original splitter sizes for collapse/expand
    m_splitterSizes = {550, 230, 160};
    m_chartSavedSizes = {550, 230, 160};
    m_spectrogramSavedSizes = {550, 230, 160};
    // v1.13.1：初始纵向比例落地改在 resizeEvent 首次有效尺寸时执行
    // （见 m_initialSplitApplied）——ctor 里 singleShot(0) 会被 main.cpp
    // splash 的 processEvents 提前触发（窗口未 show 高度无效），真机被跳过。

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
    casePhContent->setStyleSheet("background: " + Theme::BgPanel
        + "; border-right: 1px solid " + Theme::Border + ";");
    auto *casePhLay = new QVBoxLayout(casePhContent);
    casePhLay->setContentsMargins(2, 4, 2, 4);
    casePhLay->setSpacing(4);
    auto *casePhExpandBtn = new QPushButton(QString::fromUtf8("â¶"),
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
        m_casePlaceholder->setVisible(false);
        m_caseDock->setVisible(true);
        m_caseDockWasExpanded = true;
        if (m_caseListContent)
            m_caseListContent->setVisible(true);
        if (m_caseCollapseBtn)
            m_caseCollapseBtn->setText(QString::fromUtf8("◀"));
        resizeDocks({m_caseDock}, {250}, Qt::Horizontal);
    });


    // ---- 案件模式接线（v1.3.0 M2 任务10）----
    m_caseDock = new CaseDock(m_caseManager, this);
    // v1.7.1：全局快捷键不被案件树劫持（与视频列表/语谱滑块同机制——
    // 过滤器须装在具体控件上；文本输入控件由 eventFilter 保护放行）
    for (QWidget *w : m_caseDock->findChildren<QWidget *>()) {
        if (qobject_cast<QTreeWidget *>(w) || qobject_cast<QPushButton *>(w))
            w->installEventFilter(m_keyGuard);
    }
    addDockWidget(Qt::LeftDockWidgetArea, m_caseDock);
    resizeDocks({m_caseDock}, {250}, Qt::Horizontal);
    m_caseDock->setVisible(false);   // 仅案件模式可见（替代视频列表）

    // v1.13.1（用户实测反馈）：案件列表常驻手动折叠条（与视频列表同款
    // ◀/▶ 细条）——此前只能随放大镜开关联动收放，平时无控件，案件模式
    // 演示无法收起。放大镜自动折叠（占位细条）逻辑不受影响。
    {
        QWidget *caseInner = m_caseDock->widget();
        auto *caseWrap = new QWidget;
        auto *caseWrapLay = new QHBoxLayout(caseWrap);
        caseWrapLay->setContentsMargins(0, 0, 0, 0);
        caseWrapLay->setSpacing(0);
        auto *caseStrip = new QWidget(caseWrap);
        caseStrip->setFixedWidth(28);
        caseStrip->setStyleSheet("background: " + Theme::BgPanel
            + "; border-right: 1px solid " + Theme::Border + ";");
        auto *stripLay = new QVBoxLayout(caseStrip);
        stripLay->setContentsMargins(2, 4, 2, 4);
        stripLay->setSpacing(4);
        m_caseCollapseBtn = new QPushButton(
            QString::fromUtf8("◀"), caseStrip);
        m_caseCollapseBtn->setFixedSize(22, 22);
        m_caseCollapseBtn->setStyleSheet(collapseBtnStyle);
        m_caseCollapseBtn->setToolTip(lang("收起案件列表", "Collapse case list"));
        m_caseCollapseBtn->setFocusPolicy(Qt::NoFocus);
        stripLay->addWidget(m_caseCollapseBtn);
        stripLay->addStretch();
        {
            const QString vertText = lang("案件列表", "Case");
            for (const QChar &ch : vertText) {
                auto *chLabel = new QLabel(QString(ch), caseStrip);
                chLabel->setStyleSheet("color: " + Theme::TextSecond
                    + "; font-size: 11px; background: transparent;");
                chLabel->setAlignment(Qt::AlignCenter);
                stripLay->addWidget(chLabel);
            }
        }
        stripLay->addStretch();
        caseWrapLay->addWidget(caseStrip);
        caseWrapLay->addWidget(caseInner, 1);
        m_caseDock->setWidget(caseWrap);
        m_caseListContent = caseInner;
        connect(m_caseCollapseBtn, &QPushButton::clicked, this, [this]() {
            const bool vis = m_caseListContent->isVisible();
            m_caseListContent->setVisible(!vis);
            m_caseCollapseBtn->setText(vis
                ? QString::fromUtf8("▶")
                : QString::fromUtf8("◀"));
            m_caseCollapseBtn->setToolTip(vis
                ? lang("展开案件列表", "Expand case list")
                : lang("收起案件列表", "Collapse case list"));
            resizeDocks({m_caseDock}, {vis ? 24 : 250}, Qt::Horizontal);
        });
    }
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
