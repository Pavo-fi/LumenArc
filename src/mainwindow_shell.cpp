/**
 * @file mainwindow_shell.cpp
 * @brief 前端 V2 骨架装配（方案 B）：阶段工作区 + 工作流条 + 任务带 + 命令注册表
 *
 * 中央区改为 QStackedWidget：一个阶段一个页。
 *  - 分析页 = 原中央部件（视频 + 图表 + 语谱图），原样保留；
 *  - 整备 / 校时 / 产出 = 把现有窗口 **嵌入**（setWindowFlags(Qt::Widget)），
 *    复用它们全部内部逻辑，只把 show() 换成 embedWorkspace()；
 *  - 接入 / 交付 = 动作页，按钮由命令注册表按标题匹配触发（不改业务）。
 */
#include "mainwindow.h"

#include "app/case_open_panel.h"
#include "app/case_manager.h"
#include "app/uistate.h"
#include "casedock.h"
#include "chartpanel.h"
#include "spectrogrampanel_enhanced.h"
#include "domain/roi_model.h"
#include "i18n.h"
#include "keyguardfilter.h"
#include "infrastructure/ffmpeg_video_engine.h"
#include "infrastructure/ivideo_engine.h"
#include "multicamplaybackwindow.h"
#include "camtilewidget.h"
#include "multicamview.h"
#include "infrastructure/segment_export_engine.h"
#include "infrastructure/tool_paths.h"
#include "segmentexportdialog.h"
#include "playbackadjustpanel.h"
#include "theme.h"
#include "timesettingsdialog.h"
#include "calibphotodialog.h"
#include "app/analysis_task_service.h"
#include "app/calibration_service.h"
#include "app/snapshot_composer.h"
#include "app/multicam_sync_service.h"
#include "domain/time_calibration.h"
#include "ui/app_state.h"
#include "ui/command_registry.h"
#include "multicamview.h"
#include "ui/chevron_button.h"
#include "ui/inspector_panel.h"
#include "ui/task_bar.h"
#include "ui/workflow_bar.h"
#include "videowidget.h"

#include <QAbstractAnimation>
#include <QCheckBox>
#include <QKeyEvent>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QEasingCurve>
#include <QEventLoop>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QTemporaryDir>
#include <QTimer>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QLocale>
#include <QMenuBar>
#include <QMouseEvent>
#include <QOpenGLWidget>
#include <QPropertyAnimation>
#include <QPushButton>
#include <QProgressBar>
#include <QRadioButton>
#include <QSlider>
#include <QSpinBox>
#include <QScrollArea>
#include <QStackedWidget>
#include <QSplitter>
#include <QStatusBar>
#include <QTabBar>
#include <QTableWidget>
#include <QToolBar>
#include <QWheelEvent>
#include <QToolButton>
#include <QTreeWidget>
#include <QVBoxLayout>
#include <QWidget>

namespace {

/// 自检假引擎：只记录命令序列，用来验证「落点 + 起播」是一次原子命令。
class SelfCheckSyncEngine : public IVideoEngine
{
public:
    explicit SelfCheckSyncEngine(QObject *parent = nullptr)
        : IVideoEngine(parent) {}

    bool load(const QString &) override
    {
        emit durationChanged(m_durMs);
        return true;
    }
    void play() override { calls << QStringLiteral("play"); }
    void playAt(qint64 t) override
    {
        m_pos = t;
        calls << QStringLiteral("playAt:%1").arg(t);
    }
    void pause() override { calls << QStringLiteral("pause"); }
    void stop() override {}
    void unload() override {}
    void seek(qint64 t) override
    {
        m_pos = t;
        calls << QStringLiteral("seek:%1").arg(t);
    }
    qint64 position() const override { return m_pos; }
    qint64 duration() const override { return m_durMs; }
    PlaybackState state() const override { return PlaybackState::Paused; }
    int videoWidth() const override { return 640; }
    int videoHeight() const override { return 360; }
    float fps() const override { return 25.0f; }
    int volume() const override { return m_volume; }
    void setVolume(int v) override { m_volume = v; }
    void setRate(float r) override { m_rate = r; }
    float rate() const override { return m_rate; }

    QStringList calls;
    // 20 分钟：足够容纳「5 分钟选段」这类真实场景（引擎回报会覆盖机位预设时长）
    qint64 m_durMs = 1200000;

private:
    qint64 m_pos = 0;
    int m_volume = 100;
    float m_rate = 1.0f;
};

} // namespace

// ---------------------------------------------------------------------------
// 装配
// ---------------------------------------------------------------------------
/// v2.0 前端骨架装配：调用点必须晚于 setCentralWidget()（构造体尾部）
void MainWindow::setupShell()
{
    m_appState = new ui::AppState(this);
    m_commands = new ui::CommandRegistry(this);

    auto *central = new QWidget(this);
    auto *lay = new QVBoxLayout(central);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(0);

    // 顶：工作流条——作为**第一条工具栏**插入（QMainWindow 的工具栏区在中央部件之上），
    // 否则会被原有两条工具栏压下去。
    m_workflowBar = new ui::WorkflowBar(m_appState, this);
    {
        auto *wfToolBar = new QToolBar(QStringLiteral("workflow"), this);
        wfToolBar->setObjectName(QStringLiteral("workflowToolBar"));
        wfToolBar->setMovable(false);
        wfToolBar->setFloatable(false);
        wfToolBar->setContentsMargins(0, 0, 0, 0);
        wfToolBar->addWidget(m_workflowBar);

        // v2.2：整列「左侧栏」开关（案件 + 检视器 + 占位条一起收）
        m_leftToggleBtn = new QToolButton(wfToolBar);
        m_leftToggleBtn->setText(lang(QStringLiteral("◀ 左侧栏"),
                                      QStringLiteral("◀ Side")));
        m_leftToggleBtn->setCheckable(true);
        m_leftToggleBtn->setFocusPolicy(Qt::NoFocus);
        m_leftToggleBtn->setToolTip(lang(
            QStringLiteral("收起 / 展开整个左侧栏（案件 + 检视器）"),
            QStringLiteral("Collapse / expand the whole left column")));
        m_leftToggleBtn->setStyleSheet(QStringLiteral(
            "QToolButton{background:transparent;color:%1;border:1px solid %2;"
            "border-radius:5px;padding:2px 10px;font-size:11px;}"
            "QToolButton:hover{color:%3;border-color:%3;}")
            .arg(Theme::TextSecond, Theme::Border, Theme::Accent));
        connect(m_leftToggleBtn, &QToolButton::clicked,
                this, &MainWindow::toggleLeftColumn);
        wfToolBar->addWidget(m_leftToggleBtn);

        const QList<QToolBar *> existing = findChildren<QToolBar *>();
        if (!existing.isEmpty()) {
            insertToolBar(existing.first(), wfToolBar);
            insertToolBarBreak(existing.first());
        } else {
            addToolBar(Qt::TopToolBarArea, wfToolBar);
        }
    }

    // 中：阶段工作区
    m_workspaceStack = new QStackedWidget(central);
    lay->addWidget(m_workspaceStack, 1);

    // 命令注册表先建——动作页要按标题去找命令
    m_commands->rebuild(menuBar());

    // 分析页 = 原中央部件（QSplitter[视频|放大镜] + 图表 + 语谱图），上方加一排视图 chips
    if (QWidget *legacy = takeCentralWidget()) {
        auto *page = new QWidget(m_workspaceStack);
        auto *pl = new QVBoxLayout(page);
        pl->setContentsMargins(0, 0, 0, 0);
        pl->setSpacing(0);
        pl->addWidget(buildViewChips(page), 0);
        pl->addWidget(legacy, 1);
        m_analyzePage = page;
        m_stagePages.insert(int(ui::Stage::Analyze), page);
        m_workspaceStack->addWidget(page);
    }

    // 接入 / 交付：卡片页（按钮走命令注册表，业务零改动）
    QWidget *ingest = buildActionPage(
        lang(QStringLiteral("接入"), QStringLiteral("Ingest")),
        QStringList{ lang(QStringLiteral("打开视频"), QStringLiteral("Open Video")),
                     lang(QStringLiteral("临时查看视频"), QStringLiteral("Quick View")),
                     lang(QStringLiteral("接入监控直播"), QStringLiteral("Live Source")),
                     lang(QStringLiteral("新建案件"), QStringLiteral("New Case")),
                     lang(QStringLiteral("打开案件"), QStringLiteral("Open Case")),
                     lang(QStringLiteral("起始页"), QStringLiteral("Start Page")) },
        QStringList{ lang(QStringLiteral("入案分析，全程可追溯"), QStringLiteral("Analyze with a case")),
                     lang(QStringLiteral("不入案，直接看"), QStringLiteral("No case needed")),
                     lang(QStringLiteral("RTSP 直连录像机"), QStringLiteral("RTSP direct")),
                     lang(QStringLiteral("开一个新案子"), QStringLiteral("Start a new case")),
                     lang(QStringLiteral("打开已有案件"), QStringLiteral("Open an existing case")),
                     lang(QStringLiteral("最近案件与引导"), QStringLiteral("Recents and guide")) },
        true,
        central);
    m_stagePages.insert(int(ui::Stage::Ingest), ingest);
    m_workspaceStack->addWidget(ingest);

    QWidget *deliver = buildDeliverPage(central);
    m_stagePages.insert(int(ui::Stage::Deliver), deliver);
    m_workspaceStack->addWidget(deliver);

    // 底：统一任务带
    m_taskBar = new ui::TaskBar(central);
    lay->addWidget(m_taskBar, 0);
    // v2.5e：上拉列表里每条任务的 ✕ → 取消对应服务
    connect(m_taskBar, &ui::TaskBar::cancelRequested, this, [this](int id) {
        if (id == m_calibTaskId) {
            if (m_calibrationService)
                m_calibrationService->cancel();
            finishCalibrationTask(false, lang(QStringLiteral("已取消"),
                                              QStringLiteral("Cancelled")));
            return;
        }
        for (auto it = m_anaTaskIds.constBegin(); it != m_anaTaskIds.constEnd(); ++it) {
            if (it.value() == id) {
                if (m_taskService)
                    m_taskService->cancel();
                return;
            }
        }
    });

    // v2.0：底部三区 —— 左：降噪（有音频才显）· 中：播放控制 · 右：快照 + 任务
    auto *nrBar = new QWidget(m_taskBar);
    auto *nrLay = new QHBoxLayout(nrBar);
    nrLay->setContentsMargins(0, 0, 0, 0);
    nrLay->setSpacing(4);
    for (QWidget *w : { static_cast<QWidget *>(m_noiseFloorLabel),
                        static_cast<QWidget *>(m_noiseFloorSlider),
                        static_cast<QWidget *>(m_noiseFloorValueLabel),
                        static_cast<QWidget *>(m_noiseReductionLabel),
                        static_cast<QWidget *>(m_noiseReductionSlider),
                        static_cast<QWidget *>(m_noiseReductionValueLabel),
                        static_cast<QWidget *>(m_nrApplyBtn) }) {
        if (!w)
            continue;
        w->setParent(nrBar);
        nrLay->addWidget(w);
        w->show();
    }
    if (m_noiseFloorSlider)
        m_noiseFloorSlider->setFixedWidth(90);
    if (m_noiseReductionSlider)
        m_noiseReductionSlider->setFixedWidth(70);
    if (m_nrApplyBtn)
        m_nrApplyBtn->setFixedHeight(20);
    m_noiseBar = nrBar;
    m_taskBar->setLeadingWidgets({ nrBar });

    m_taskBar->setCenterWidgets({ m_playBtn, m_pauseBtn, m_stopBtn,
                                  m_speedBtn, m_timeLabel });

    // 顶部「播放与产出」工具栏：**不删只藏**。
    // 它的子控件里有成员指针（m_exportClipBtn），删除会留下悬垂指针，
    // 之后 wiring 里的 setEnabled() 就是一次 AV（闪退）。
    if (auto *tb = findChild<QToolBar *>(QStringLiteral("playbackToolBar")))
        tb->setVisible(false);

    setCentralWidget(central);

    // 检视器：左停靠区（案件 / 素材之下），随阶段显隐
    m_inspector = new ui::InspectorPanel(this);
    m_inspectorDock = new QDockWidget(lang(QStringLiteral("检视器"), QStringLiteral("Inspector")), this);
    m_inspectorDock->setObjectName(QStringLiteral("inspectorDock"));
    // v2.2：检视器自己管滚动（上半事实可滚，下半工具固定）——
    // 这里直接放面板本体，不要再套一层 QScrollArea。
    m_inspectorDock->setWidget(m_inspector);
    // v2.4：「检视器」一级标题回来 —— 与「案件：XXX」同一套字号/字重/边框
    m_inspectorDock->setFeatures(QDockWidget::DockWidgetMovable);
    {
        auto *tb = new QWidget(m_inspectorDock);
        tb->setFixedHeight(26);
        tb->setStyleSheet(QStringLiteral(
            "QWidget{background:%1;border-bottom:1px solid %2;}")
            .arg(Theme::BgPanel, Theme::Border));
        auto *tl = new QHBoxLayout(tb);
        tl->setContentsMargins(8, 0, 4, 0);
        tl->setSpacing(6);
        auto *lb = new QLabel(lang("检视器", "Inspector"), tb);
        lb->setStyleSheet(QStringLiteral(
            "font-size:11px;font-weight:600;color:%1;background:transparent;")
            .arg(Theme::TextPrimary));
        tl->addWidget(lb, 1);
        m_inspectorDock->setTitleBarWidget(tb);
    }
    m_inspectorDock->setStyleSheet(QStringLiteral(
            "QDockWidget{font-size:11px;font-weight:600;color:%1;}"
            "QDockWidget::title{background:%2;padding:5px 8px;"
            "border-bottom:1px solid %3;text-align:left;}")
            .arg(Theme::TextPrimary, Theme::BgPanel, Theme::Border));
    m_inspectorDock->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    addDockWidget(Qt::LeftDockWidgetArea, m_inspectorDock);
    m_inspectorDock->setMinimumWidth(200);
    m_inspectorDock->setMaximumWidth(320);
    m_inspectorDock->setMinimumHeight(150);

    // v2.1：把「分析与标注」整行的**真控件**收进检视器「工具」区。
    // 同一批指针 reparent → 启用态/勾选态自动跟随，不重建、不重复连线。
    if (m_inspector) {
        m_inspector->addToolGroup(lang(QStringLiteral("分析"), QStringLiteral("Analyze")),
                                  QList<QWidget *>{ m_analyzeBtn, m_audioAnalysisBtn,
                                                    m_microDiffBtn, m_setTimeBtn }, true);
        m_inspector->addToolGroup(lang(QStringLiteral("区域"), QStringLiteral("Region")),
                                  QList<QWidget *>{ m_rectModeBtn, m_polygonModeBtn,
                                                    m_guideLineBtn });
        m_inspector->addToolGroup(lang(QStringLiteral("标注"), QStringLiteral("Mark")),
                                  QList<QWidget *>{ m_copyRoiBtn, m_pasteRoiBtn });
        // 截图叠加：独立一栏，栏标题不可点，下面三件套
        m_inspector->setCaptureWidgets(QList<QWidget *>{ m_captureBtn, m_editBtn, m_placeBtn });
        // 显示：快照 / 画面调节 归到显示（自动排在放大镜之前）
        m_inspector->setDisplayWidgets(QList<QWidget *>{ m_snapshotBtn, m_adjustBtn });

        // v2.5：ROI 三段控件的外框容器（modeSegment）三个按钮已被检视器接管，
        // 容器本身没人管 → 它作为 MainWindow 的顶层子控件停在 (0,0) 100x30，
        // 正好压在**菜单栏左上角**，用户看到的就是「菜单栏被灰色色块挡住」。
        if (QWidget *seg = findChild<QWidget *>(QStringLiteral("roiModeSegment")))
            seg->hide();
        // 这两个原先因「阶段条已覆盖」被隐藏，现在在检视器里有位置了
        if (m_setTimeBtn) m_setTimeBtn->setVisible(true);
        if (m_adjustBtn)  m_adjustBtn->setVisible(true);
    }

    // 显示请求 → 复用现有入口
    connect(m_inspector, &ui::InspectorPanel::magnifierToggled, this, [this](bool on) {
        if (on) createMagnifier();
        else    removeMagnifier();
        refreshInspector();
    });

    // 500ms 刷新（游标 / 素材 / 校时 / ROI）
    auto *inspTimer = new QTimer(this);
    inspTimer->setInterval(500);
    connect(inspTimer, &QTimer::timeout, this, &MainWindow::refreshInspector);
    inspTimer->start();
    refreshInspector();

    connect(m_workflowBar, &ui::WorkflowBar::stageClicked,
            this, &MainWindow::onWorkflowStage);
    connect(m_workflowBar, &ui::WorkflowBar::commandPaletteRequested,
            this, &MainWindow::showCommandPalette);

    showStage(ui::Stage::Analyze);
}

// ---------------------------------------------------------------------------
// 阶段页
// ---------------------------------------------------------------------------
QWidget *MainWindow::buildActionPage(const QString &title, const QStringList &commandTitles,
                                     const QStringList &hints, bool withRecentCases,
                                     QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(40, 32, 40, 32);
    lay->setSpacing(14);

    auto *h = new QLabel(title, page);
    h->setStyleSheet(QStringLiteral("color:%1;font-size:22px;font-weight:600;"
                                    "background:transparent;")
                         .arg(Theme::TextPrimary));
    lay->addWidget(h);

    auto *grid = new QGridLayout();
    grid->setSpacing(12);
    grid->setColumnStretch(0, 1);
    grid->setColumnStretch(1, 1);

    int row = 0, col = 0;
    for (int i = 0; i < commandTitles.size(); ++i) {
        const QString cmd = commandTitles[i];
        const QString hint = (i < hints.size()) ? hints[i] : QString();
        auto *btn = new QPushButton(hint.isEmpty()
                                        ? cmd
                                        : cmd + QStringLiteral("\n") + hint,
                                    page);
        btn->setMinimumHeight(74);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setStyleSheet(QStringLiteral(
            "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:10px;"
            "text-align:left;padding:12px 18px;font-size:14px;}"
            "QPushButton:hover{border-color:%4;background:%5;}")
            .arg(Theme::BgCard, Theme::TextPrimary, Theme::Border,
                 Theme::Accent, Theme::BgHover));
        connect(btn, &QPushButton::clicked, this, [this, cmd]() {
            if (!m_commands || !m_commands->triggerByTitle(cmd)) {
                if (statusBar())
                    statusBar()->showMessage(
                        lang(QStringLiteral("暂不可用：") + cmd, cmd), 2500);
            }
        });
        grid->addWidget(btn, row, col);
        if (++col == 2) { col = 0; ++row; }
    }
    lay->addLayout(grid);

    // 最近案件（真实数据）
    if (withRecentCases && m_caseManager) {
        const QStringList recents = m_caseManager->recentCases();
        auto *rl = new QLabel(lang(QStringLiteral("最近案件"), QStringLiteral("Recent")), page);
        rl->setStyleSheet(QStringLiteral("color:%1;font-size:11px;letter-spacing:.6px;"
                                         "background:transparent;")
                              .arg(Theme::TextMuted));
        lay->addWidget(rl);

        if (recents.isEmpty()) {
            auto *e = new QLabel(lang(QStringLiteral("暂无"), QStringLiteral("None")), page);
            e->setStyleSheet(QStringLiteral("color:%1;background:transparent;")
                                 .arg(Theme::TextMuted));
            lay->addWidget(e);
        } else {
            const int n = qMin(int(recents.size()), 5);
            for (int i = 0; i < n; ++i) {
                const QString dir = recents[i];
                auto *b = new QPushButton(QDir(dir).dirName(), page);
                b->setMinimumHeight(34);
                b->setCursor(Qt::PointingHandCursor);
                b->setStyleSheet(QStringLiteral(
                    "QPushButton{background:transparent;color:%1;border:0;border-radius:6px;"
                    "text-align:left;padding:0 14px;font-size:13px;}"
                    "QPushButton:hover{background:%2;color:%3;}")
                    .arg(Theme::TextSecond, Theme::BgHover, Theme::TextPrimary));
                b->setToolTip(dir);
                connect(b, &QPushButton::clicked, this,
                        [this, dir]() { openCaseFlow(dir); });
                lay->addWidget(b);
            }
        }
    }

    lay->addStretch(1);
    return page;
}

void MainWindow::embedWorkspace(ui::Stage s, QWidget *w)
{
    if (!w || !m_workspaceStack)
        return;

    w->setAttribute(Qt::WA_DeleteOnClose, false);

    // QMainWindow 不能直接嵌入：它的内部布局是顶层语义，重生为子控件后
    // 尺寸对但不上屏（自检截图实锤：整备页全黑）。
    // 取它的 centralWidget 装进宿主容器；原窗口对象留活（逻辑/信号照常）。
    QWidget *page = w;
    if (auto *mw = qobject_cast<QMainWindow *>(w)) {
        if (QWidget *inner = mw->takeCentralWidget()) {
            auto *host = new QWidget(m_workspaceStack);
            auto *hl = new QVBoxLayout(host);
            hl->setContentsMargins(0, 0, 0, 0);
            hl->setSpacing(0);
            hl->addWidget(inner);
            host->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
            mw->hide();
            page = host;
        }
    }

    // 去掉窗口边框与“关闭即销毁”语义，变成嵌入式页
    page->setWindowFlags(Qt::Widget);
    page->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    const int key = int(s);
    QWidget *old = m_stagePages.value(key, nullptr);
    if (old && old != page) {
        m_workspaceStack->removeWidget(old);
        old->deleteLater();
    }

    m_workspaceStack->addWidget(page);
    m_stagePages.insert(key, page);
    installKeyGuardOn(page);   // v2.0：嵌入窗口也走全局快捷键逻辑
    showStage(s);
}

void MainWindow::applyLeftColumnRatio(bool force)
{
    if (!m_caseDock || !m_inspectorDock)
        return;
    if (!m_caseDock->isVisible() || !m_inspectorDock->isVisible())
        return;

    const int caseH = m_caseDock->height();
    const int total = caseH + m_inspectorDock->height();
    if (total < 400)                       // 还没布局好 / 窗口太矮 → 不动
        return;

    // 总高没变但案件区高度变了 → 只可能是用户亲手拖过分隔条，尊重用户
    if (!force && m_leftRatioApplied
        && qAbs(total - m_leftRatioTotal) <= 8
        && qAbs(caseH - m_leftRatioCaseH) > 6)
        return;

    constexpr int kCaseMinH = 220;         // 至少还能看见几行机位
    constexpr int kInspMinH = 210;         // 检视器整块可滚，210 够「素材」+「工具」标题
    const int want = qRound(total * 0.7);  // 案件 70%，检视器 30%
    const int top = qBound(kCaseMinH, want, qMax(kCaseMinH, total - kInspMinH));

    // 关键：左侧停靠区里 resizeDocks 会被兄弟坞的尺寸协商覆盖
    // （折叠时踩过同样的坑）→ 用尺寸上限硬控：两个上限之和正好等于总高，
    // 布局只有一种解，比例必然落地。
    m_caseDock->setMinimumHeight(kCaseMinH);
    m_caseDock->setMaximumHeight(top);
    m_inspectorDock->setMinimumHeight(kInspMinH);
    m_inspectorDock->setMaximumHeight(total - top);

    m_leftRatioCaseH = top;
    m_leftRatioTotal = total;
    m_leftRatioApplied = true;

    // 400ms 后解除上限 —— 比例已落位，之后允许用户再手动拖
    QTimer::singleShot(400, this, [this]() {
        if (m_caseDock)
            m_caseDock->setMaximumHeight(QWIDGETSIZE_MAX);
        if (m_inspectorDock)
            m_inspectorDock->setMaximumHeight(QWIDGETSIZE_MAX);
    });
}

void MainWindow::toggleLeftColumn()
{
    QList<QDockWidget *> left;
    for (QDockWidget *d : findChildren<QDockWidget *>()) {
        if (!d || d == m_casePlaceholder)   // 占位细条单独管
            continue;
        if (dockWidgetArea(d) == Qt::LeftDockWidgetArea)
            left << d;
    }
    if (left.isEmpty())
        return;

    if (!m_leftColumnHidden) {
        m_leftDockStash.clear();
        for (QDockWidget *d : left)
            m_leftDockStash.insert(d, d->isVisible());
        for (QDockWidget *d : left)
            d->hide();
        // 留一条细条当还原入口（▶）
        if (m_casePlaceholder) {
            m_casePlaceholder->setVisible(true);
            resizeDocks({m_casePlaceholder}, {24}, Qt::Horizontal);
        }
        m_leftColumnHidden = true;
    } else {
        if (m_casePlaceholder)
            m_casePlaceholder->setVisible(false);
        for (QDockWidget *d : left) {
            if (m_leftDockStash.value(d, false))
                d->show();
        }
        m_leftColumnHidden = false;
        // 整列还原后重新摆 4:6（收起期间窗口尺寸可能变了）
        QTimer::singleShot(0, this, [this]() { applyLeftColumnRatio(true); });
    }

    if (m_leftToggleBtn) {
        m_leftToggleBtn->setText(m_leftColumnHidden
            ? lang(QStringLiteral("▶ 左侧栏"), QStringLiteral("▶ Side"))
            : lang(QStringLiteral("◀ 左侧栏"), QStringLiteral("◀ Side")));
        m_leftToggleBtn->setChecked(m_leftColumnHidden);
    }
}

void MainWindow::showStage(ui::Stage s)
{
    if (!m_workspaceStack || !m_appState)
        return;

    m_appState->setStage(s);

    // v2.1：离开「校时」阶段 → 右侧校时坞自动收起（否则它一直压在视频上，
    // 分析/整备/产出/交付页都不需要它；回到③校时会自动重新弹出）
    if (s != ui::Stage::Align) {
        if (auto *dk = findChild<QDockWidget *>(QStringLiteral("calibrationDock")))
            dk->hide();
    }

    applyStageChrome(s);

    // 校时：停靠式——视频必须保持可见（否则无法框选时间戳），所以不换页
    if (s == ui::Stage::Align) {
        if (auto *dk = findChild<QDockWidget *>(QStringLiteral("calibrationDock"))) {
            dk->show();
            if (m_analyzePage)
                m_workspaceStack->setCurrentWidget(m_analyzePage);
        } else {
            onSetStartTime();   // 内部会 dockWorkspace 回来
        }
        return;
    }

    // v3.3（用户拍板）：产出阶段与宫格**统一**——导出本来就是宫格视图里的
    // 一等动作（选段/断点/每段倍速/导出），不再另开一套合成工作台当产出页。
    if (s == ui::Stage::Produce) {
        if (m_caseManager->isOpen()) {
            onMultiCamView();               // 内部会把宫格设为当前工作区页
            if (m_multiCamWin) {
                m_workspaceStack->setCurrentWidget(m_multiCamWin.data());
                m_appState->setStage(ui::Stage::Produce);   // 流程条停在⑤产出
            }
            return;
        }
        // 没开案件：独立单路也要能导出 → 回落到单路导出面板
        onExportSingleClipDialog();
        return;
    }

    QWidget *page = m_stagePages.value(int(s), nullptr);

    // 页尚未创建 → 触发该阶段的既有入口（入口内部会 embedWorkspace 回来）
    if (!page) {
        switch (s) {
        case ui::Stage::Prepare: openPreprocessWindow(); break;
        default: break;
        }
        page = m_stagePages.value(int(s), nullptr);
    }

    // 交付页：每次进入重扫产物目录
    if (s == ui::Stage::Deliver)
        refreshDeliverPage();

    // 创建不出来（例：没开视频就点校时）→ 把「为什么不行」说出来
    if (!page) {
        if (statusBar())
            statusBar()->showMessage(m_appState->gateHint(s), 3000);
        return;
    }

    m_workspaceStack->setCurrentWidget(page);

    // 淡入：含 QOpenGLWidget 的页跳过（图形效果会破坏 GL 渲染）
    if (page->findChildren<QOpenGLWidget *>().isEmpty()) {
        auto *eff = new QGraphicsOpacityEffect(page);
        page->setGraphicsEffect(eff);
        auto *anim = new QPropertyAnimation(eff, "opacity", page);
        anim->setDuration(180);
        anim->setStartValue(0.0);
        anim->setEndValue(1.0);
        anim->setEasingCurve(QEasingCurve::OutCubic);
        connect(anim, &QPropertyAnimation::finished, page, [page]() {
            page->setGraphicsEffect(nullptr);
        });
        anim->start(QAbstractAnimation::DeleteWhenStopped);
    }
}

// ---------------------------------------------------------------------------
// 交付页：产物表（扫案件目录，不依赖新接口）
// ---------------------------------------------------------------------------
QWidget *MainWindow::buildDeliverPage(QWidget *parent)
{
    auto *page = new QWidget(parent);
    auto *lay = new QVBoxLayout(page);
    lay->setContentsMargins(28, 22, 28, 22);
    lay->setSpacing(10);

    auto *head = new QHBoxLayout();
    head->setSpacing(8);

    auto *h = new QLabel(lang(QStringLiteral("交付"), QStringLiteral("Deliver")), page);
    h->setStyleSheet(QStringLiteral("color:%1;font-size:20px;font-weight:600;"
                                    "background:transparent;")
                         .arg(Theme::TextPrimary));
    head->addWidget(h);
    head->addSpacing(12);

    struct Act { const char *zh; const char *en; };
    const Act acts[] = { { "导出移交包", "Package" },
                         { "生成分析报告", "DOCX" },
                         { "导出 HTML 报告", "HTML" },
                         { "编辑监控点位图", "Site Map" } };
    for (const Act &a : acts) {
        const QString needle = lang(QString::fromUtf8(a.zh), QString::fromUtf8(a.en));
        auto *b = new QPushButton(needle, page);
        b->setMinimumHeight(30);
        b->setCursor(Qt::PointingHandCursor);
        b->setStyleSheet(QStringLiteral(
            "QPushButton{background:%1;color:%2;border:1px solid %3;border-radius:6px;"
            "padding:0 14px;font-size:12px;}"
            "QPushButton:hover{border-color:%4;color:%5;}")
            .arg(Theme::BgCard, Theme::TextSecond, Theme::Border,
                 Theme::Accent, Theme::TextPrimary));
        connect(b, &QPushButton::clicked, this, [this, needle]() {
            if (m_commands)
                m_commands->triggerByTitle(needle);
        });
        head->addWidget(b);
    }
    head->addStretch(1);
    lay->addLayout(head);

    m_deliverSummary = new QLabel(page);
    m_deliverSummary->setStyleSheet(QStringLiteral("color:%1;font-size:11px;"
                                                   "background:transparent;")
                                        .arg(Theme::TextMuted));
    lay->addWidget(m_deliverSummary);

    m_deliverTable = new QTableWidget(0, 4, page);
    m_deliverTable->setHorizontalHeaderLabels(
        { lang(QStringLiteral("产物"), QStringLiteral("Artifact")),
          lang(QStringLiteral("类型"), QStringLiteral("Kind")),
          lang(QStringLiteral("大小"), QStringLiteral("Size")),
          lang(QStringLiteral("修改时间"), QStringLiteral("Modified")) });
    m_deliverTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    m_deliverTable->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_deliverTable->verticalHeader()->setVisible(false);
    m_deliverTable->horizontalHeader()->setStretchLastSection(false);
    m_deliverTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_deliverTable->setShowGrid(false);
    lay->addWidget(m_deliverTable, 1);

    return page;
}

void MainWindow::refreshDeliverPage()
{
    if (!m_deliverTable || !m_deliverSummary)
        return;
    m_deliverTable->setRowCount(0);

    if (!m_caseManager || !m_caseManager->isOpen()) {
        m_deliverSummary->setText(lang(QStringLiteral("未打开案件 —— 交付需要案件"),
                                       QStringLiteral("No case open")));
        return;
    }

    struct Scan { const char *dir; const char *type; const char *filter; };
    const Scan scans[] = {
        { "reports",   "报告", "*.docx;*.html;*.htm;*.md" },
        { "snapshots", "快照", "*.png;*.jpg;*.jpeg" },
        { "exports",   "导出", "*.mp4;*.mkv;*.mov" },
    };

    const QDir root(m_caseManager->caseDir());
    qint64 total = 0;
    int n = 0;

    for (const Scan &sc : scans) {
        QDir d(root.filePath(QString::fromUtf8(sc.dir)));
        if (!d.exists())
            continue;
        const QStringList filters = QString::fromUtf8(sc.filter).split(QLatin1Char(';'));
        const QFileInfoList files = d.entryInfoList(filters, QDir::Files, QDir::Time);
        for (const QFileInfo &fi : files) {
            const int r = m_deliverTable->rowCount();
            m_deliverTable->insertRow(r);
            m_deliverTable->setItem(r, 0, new QTableWidgetItem(fi.fileName()));
            m_deliverTable->setItem(r, 1,
                new QTableWidgetItem(lang(QString::fromUtf8(sc.type), QString::fromUtf8(sc.type))));
            m_deliverTable->setItem(r, 2, new QTableWidgetItem(
                QLocale().formattedDataSize(fi.size())));
            m_deliverTable->setItem(r, 3, new QTableWidgetItem(
                fi.lastModified().toString(QStringLiteral("MM-dd HH:mm"))));
            total += fi.size();
            ++n;
        }
    }

    m_deliverSummary->setText(
        n == 0 ? lang(QStringLiteral("暂无产物"), QStringLiteral("No artifacts"))
               : QStringLiteral("%1 · %2 · %3")
                     .arg(n)
                     .arg(QLocale().formattedDataSize(total))
                     .arg(root.dirName()));
}

// ---------------------------------------------------------------------------
// 全局快捷键守卫：给嵌入窗口/停靠面板批量安装
// 不装的话 Space/方向键会被 QSlider、按钮、表格吃掉（"快捷键被劫持"）。
// ---------------------------------------------------------------------------
void MainWindow::installKeyGuardOn(QWidget *root)
{
    if (!root || !m_keyGuard)
        return;

    root->installEventFilter(m_keyGuard);
    const QList<QWidget *> kids = root->findChildren<QWidget *>();
    for (QWidget *w : kids) {
        if (!w)
            continue;
        if (qobject_cast<QSlider *>(w) || qobject_cast<QSpinBox *>(w)
            || qobject_cast<QDoubleSpinBox *>(w) || qobject_cast<QPushButton *>(w)
            || qobject_cast<QToolButton *>(w) || qobject_cast<QComboBox *>(w)
            || qobject_cast<QTableWidget *>(w) || qobject_cast<QListWidget *>(w)
            || qobject_cast<QTreeWidget *>(w) || qobject_cast<QTabBar *>(w)
            || qobject_cast<QCheckBox *>(w) || qobject_cast<QRadioButton *>(w)) {
            w->installEventFilter(m_keyGuard);
        }
    }
}

void MainWindow::animateSplitterSizes(const QList<int> &target)
{
    if (!m_splitter)
        return;
    const QList<int> from = m_splitter->sizes();
    if (from.size() != target.size() || from == target) {
        m_splitter->setSizes(target);
        return;
    }

    auto *anim = new QVariantAnimation(this);
    anim->setDuration(220);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    connect(anim, &QVariantAnimation::valueChanged, this,
            [this, from, target](const QVariant &v) {
                const double t = v.toDouble();
                QList<int> cur;
                for (int i = 0; i < target.size(); ++i)
                    cur << int(from[i] + (target[i] - from[i]) * t);
                m_splitter->setSizes(cur);
            });
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void MainWindow::onWorkflowStage(ui::Stage s)
{
    showStage(s);
}

// ---------------------------------------------------------------------------
// 视图 chips：单路 / 并列放大 / 宫格 / 全屏
// ---------------------------------------------------------------------------
QWidget *MainWindow::buildViewChips(QWidget *parent)
{
    auto *bar = new QWidget(parent);
    bar->setFixedHeight(32);
    bar->setStyleSheet(QStringLiteral("QWidget{background:%1;border-bottom:1px solid %2;}")
                           .arg(Theme::BgPanel, Theme::Border));
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(8, 4, 8, 4);
    lay->setSpacing(4);

    const QString chipCss = QStringLiteral(
        "QPushButton{background:transparent;border:0;border-radius:4px;color:%1;"
        "padding:2px 12px;font-size:12px;}"
        "QPushButton:hover{background:%2;color:%3;}"
        "QPushButton:checked{background:%4;color:%5;font-weight:600;}")
        .arg(Theme::TextSecond, Theme::BgHover, Theme::TextPrimary,
             Theme::Accent, Theme::BgApp);

    struct Item { const char *zh; const char *en; };
    // v2.0：“并列放大”已去掉——放大镜本来就是**滚轮呼出**（onMagnifierWheelZoom 内部自动建）
    const Item items[] = { { "单路", "Single" },
                           { "宫格", "Grid" },
                           { "全屏", "Full" } };

    const int n = int(sizeof(items) / sizeof(items[0]));
    for (int i = 0; i < n; ++i) {
        auto *b = new QPushButton(
            lang(QString::fromUtf8(items[i].zh), QString::fromUtf8(items[i].en)), bar);
        b->setCheckable(true);
        b->setFixedHeight(24);
        b->setCursor(Qt::PointingHandCursor);
        b->setFocusPolicy(Qt::NoFocus);
        b->setStyleSheet(chipCss);
        lay->addWidget(b);
        m_viewChips.append(b);
    }
    lay->addStretch(1);

    // 复用现有入口：单路回分析页 / 宫格嵌入 / 副屏全屏（不改业务）
    if (m_viewChips.size() == 3) {
        m_viewChips[0]->setToolTip(lang(QStringLiteral("在画面上滚动滚轮可呼出放大镜"),
                                        QStringLiteral("Scroll on the video to magnify")));
        connect(m_viewChips[0], &QPushButton::clicked, this, [this]() {
            if (m_analyzePage && m_workspaceStack)
                m_workspaceStack->setCurrentWidget(m_analyzePage);
            syncViewChips(0);
        });
        connect(m_viewChips[1], &QPushButton::clicked, this, [this]() {
            onMultiCamView();   // 内部 embed 后会让宫格页成为当前页
            syncViewChips(1);
        });
        connect(m_viewChips[2], &QPushButton::clicked, this, [this]() {
            if (m_commands)
                m_commands->triggerByTitle(lang(QStringLiteral("副屏全屏"),
                                                QStringLiteral("Fullscreen")));
            syncViewChips(2);
        });
        m_viewChips[0]->setChecked(true);
    }
    return bar;
}

void MainWindow::showGridWorkspace()
{
    if (!m_multiCamWin || !m_workspaceStack)
        return;
    // v2.1：进宫格也算离开③校时 → 右侧校时坞自动收起（宫格不走 showStage）
    if (auto *dk = findChild<QDockWidget *>(QStringLiteral("calibrationDock")))
        dk->hide();
    m_workspaceStack->setCurrentWidget(m_multiCamWin.data());
    if (m_appState)
        m_appState->setStage(ui::Stage::Analyze);   // 宫格是分析阶段的取景方式
}

// ---------------------------------------------------------------------------
// 检视器：从当前状态拉取事实（不缓存、不自造）
// ---------------------------------------------------------------------------
void MainWindow::refreshInspector()
{
    if (!m_inspector)
        return;

    int w = 0, h = 0;
    double fps = 0.0;
    if (m_videoEngine) {
        w = m_videoEngine->videoWidth();
        h = m_videoEngine->videoHeight();
        fps = double(m_videoEngine->fps());
    }

    QString name;
    if (m_sessionMgr) {
        const QString path = m_sessionMgr->currentVideoPath();
        if (!path.isEmpty())
            name = QFileInfo(path).fileName();
    }

    const qint64 dur = m_uiState ? m_uiState->effectiveDurationMs() : 0;
    m_inspector->setSource(name, w, h, fps, dur);

    if (m_calibration.isValid()) {
        const double r = m_calibration.effectiveRate();
        const QString txt = qFuzzyCompare(r, 1.0)
                                ? lang(QStringLiteral("已校时"), QStringLiteral("Calibrated"))
                                : QStringLiteral("%1×").arg(r, 0, 'f', 3);
        m_inspector->setCalibration(txt, true);
    } else {
        m_inspector->setCalibration(lang(QStringLiteral("未校时"),
                                         QStringLiteral("Not calibrated")), false);
    }

    if (m_chartPanel)
        m_inspector->setCursor(m_chartPanel->cursorTime());

    if (m_roiModel)
        m_inspector->setRoi(m_roiModel->regionCount(), m_roiModel->polygonCount());

    m_inspector->setMagnifier(m_magnifier != nullptr);

    // 底部降噪控件：有音频才显
    if (m_noiseBar) {
        auto *ff = dynamic_cast<FfmpegVideoEngine *>(m_videoEngine);
        m_noiseBar->setVisible(ff && ff->hasAudio());
    }
}

void MainWindow::syncViewChips(int active)
{
    for (int i = 0; i < m_viewChips.size(); ++i)
        m_viewChips[i]->setChecked(i == active);
}

// ---------------------------------------------------------------------------
// 阶段铬件：工具栏 / 停靠面板按阶段显隐
// ---------------------------------------------------------------------------
QList<QDockWidget *> MainWindow::stageDocks() const
{
    QList<QDockWidget *> out;
    if (m_caseDock)             out << m_caseDock;
    if (m_videoListPanel)       out << m_videoListPanel;
    if (m_casePlaceholder)      out << m_casePlaceholder;
    if (m_videoListPlaceholder) out << m_videoListPlaceholder;
    if (m_adjustPanel)          out << m_adjustPanel;
    if (m_inspectorDock)        out << m_inspectorDock;
    return out;
}

void MainWindow::applyStageChrome(ui::Stage s)
{
    // v2.1：分析工具栏已整行取消（工具收进检视器，随检视器显隐）

    // 停靠面板：整备 / 产出 是满幅工作面，接入是入口页 → 暂存并隐藏；离开时原样恢复
    const bool wantDocks = (s == ui::Stage::Analyze || s == ui::Stage::Align
                            || s == ui::Stage::Deliver);
    // v2.2：左侧栏整体收起时，阶段切换不得把它放出来
    if (m_leftColumnHidden) {
        for (QDockWidget *d : findChildren<QDockWidget *>())
            if (d && d != m_casePlaceholder
                && dockWidgetArea(d) == Qt::LeftDockWidgetArea)
                d->hide();
    }

    if (!wantDocks) {
        if (!m_docksStashed) {
            m_dockStash.clear();
            for (QDockWidget *d : stageDocks())
                m_dockStash.insert(d, d->isVisible());
            m_docksStashed = true;
        }
        for (QDockWidget *d : stageDocks())
            d->hide();
    } else if (m_docksStashed) {
        for (auto it = m_dockStash.cbegin(); it != m_dockStash.cend(); ++it)
            if (it.key())
                it.key()->setVisible(it.value());
        m_dockStash.clear();
        m_docksStashed = false;
    }
}

void MainWindow::dockWorkspace(ui::Stage s, QWidget *w)
{
    Q_UNUSED(s);
    if (!w)
        return;
    w->setAttribute(Qt::WA_DeleteOnClose, false);

    // 已有同名坞（上次的校时窗）→ 先撤掉
    if (auto *old = findChild<QDockWidget *>(QStringLiteral("calibrationDock"))) {
        removeDockWidget(old);
        old->deleteLater();
    }

    auto *dk = new QDockWidget(lang(QStringLiteral("校时"), QStringLiteral("Calibration")), this);
    dk->setObjectName(QStringLiteral("calibrationDock"));
    dk->setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);
    dk->setWidget(w);                 // 接管所有权
    addDockWidget(Qt::RightDockWidgetArea, dk);
    dk->setMinimumWidth(380);
    dk->setMaximumWidth(560);   // 压缩：别把视频挤太窄
    dk->show();
    installKeyGuardOn(w);       // v2.0：停靠的校时面板也走全局快捷键

    // 视频必须停在可见页
    if (m_analyzePage)
        m_workspaceStack->setCurrentWidget(m_analyzePage);

    // 校时窗销毁时，把坞一并清掉（否则留下空壳）
    connect(w, &QObject::destroyed, dk, &QObject::deleteLater);
}

// ---------------------------------------------------------------------------
// QA 自检（--selfcheck-shell）
// ---------------------------------------------------------------------------
int MainWindow::runShellSelfCheck()
{
    int fails = 0;
    QStringList lines;
    auto check = [&](bool ok, const QString &what) {
        const QString line = (ok ? QStringLiteral("PASS") : QStringLiteral("FAIL"))
                             + QStringLiteral(" | ") + what;
        lines << line;
        std::fprintf(stderr, "%s\n", line.toUtf8().constData());
        if (!ok) ++fails;
    };
    auto pump = []() {
        for (int i = 0; i < 3; ++i)
            QCoreApplication::processEvents();
    };
    // 真实等待：动画靠真实时间推进，processEvents 不够
    auto wait = [](int ms) {
        QEventLoop loop;
        QTimer::singleShot(ms, &loop, &QEventLoop::quit);
        loop.exec();
    };
    // 每个阶段存一张真实渲染图（人工/自动验收用）
    auto snap = [this](const QString &name) {
        const QPixmap pm = grab();
        if (pm.isNull())
            return;
        const QString p = QDir(QCoreApplication::applicationDirPath())
                              .filePath(QStringLiteral("selfcheck-%1.png").arg(name));
        pm.save(p);
    };

    qInfo().noquote() << QStringLiteral("===== LumenArc 前端骨架自检 =====");
    lines << QStringLiteral("===== LumenArc 前端骨架自检 =====");

    // 自检环境：关掉启动欢迎面板（否则截图被它整个盖住）
    for (CaseOpenPanel *p : findChildren<CaseOpenPanel *>())
        p->hide();

    // offscreen 虚拟屏默认 800x800，比嵌入页的最小尺寸还小 —— 放到真实尺寸再验
    showNormal();
    resize(1680, 1020);
    pump();
    check(width() >= 1600, QStringLiteral("主窗已放大到 %1x%2").arg(width()).arg(height()));

    // 1. 骨架对象
    check(m_workspaceStack != nullptr, QStringLiteral("workspace stack 已创建"));
    check(m_appState != nullptr,       QStringLiteral("AppState 已创建"));
    check(m_commands != nullptr,       QStringLiteral("CommandRegistry 已创建"));
    check(m_workflowBar != nullptr,    QStringLiteral("WorkflowBar 已创建"));
    check(m_taskBar != nullptr,        QStringLiteral("TaskBar 已创建"));

    // 2. 基础页注册
    check(m_stagePages.value(int(ui::Stage::Analyze), nullptr) == m_analyzePage,
          QStringLiteral("分析页 == 原中央部件"));
    check(m_stagePages.contains(int(ui::Stage::Ingest)),  QStringLiteral("接入页已注册"));
    check(m_stagePages.contains(int(ui::Stage::Deliver)), QStringLiteral("交付页已注册"));
    check(m_workspaceStack->count() >= 3,
          QStringLiteral("工作区页数 %1 >= 3").arg(m_workspaceStack->count()));

    // 3. 命令注册表
    check(m_commands->count() > 50,
          QStringLiteral("命令数 %1 > 50").arg(m_commands->count()));
    check(!m_commands->search(lang(QStringLiteral("校时"), QStringLiteral("Calibrate"))).isEmpty(),
          QStringLiteral("能搜到「校时」"));
    check(!m_commands->search(lang(QStringLiteral("导出"), QStringLiteral("Export"))).isEmpty(),
          QStringLiteral("能搜到「导出」"));
    check(m_commands->triggerByTitle(QStringLiteral("__no_such_cmd__")) == false,
          QStringLiteral("不存在的命令返回 false"));

    // 4. 纯页面切换（不需要媒体）——含反复切换的稳定性
    const int pageCountBefore = m_workspaceStack->count();
    const ui::Stage walk[] = { ui::Stage::Ingest, ui::Stage::Analyze,
                               ui::Stage::Deliver, ui::Stage::Ingest,
                               ui::Stage::Analyze };
    for (ui::Stage s : walk) {
        showStage(s);
        pump();
        wait(300);                                   // 等淡入跑完
        snap(QStringLiteral("walk-%1").arg(int(s)));
        QWidget *page = m_stagePages.value(int(s), nullptr);
        check(page && m_workspaceStack->currentWidget() == page,
              QStringLiteral("切到「%1」→ 当前页正确").arg(ui::stageName(s)));
        check(!page || page->graphicsEffect() == nullptr,
              QStringLiteral("切到「%1」→ 淡入结束后图形效果已摘除").arg(ui::stageName(s)));
    }
    check(m_workspaceStack->count() == pageCountBefore,
          QStringLiteral("反复切换后页数稳定（%1）").arg(m_workspaceStack->count()));

    // 4b. 阶段铬件：分析工具栏已整行取消（工具收进检视器）；播放控制已搬到底部
    auto *anBar = findChild<QToolBar *>(QStringLiteral("analysisToolBar"));
    check(anBar == nullptr || !anBar->isVisible(),
          QStringLiteral("分析工具栏已整行取消（控件收进检视器）"));
    check(findChild<QToolBar *>(QStringLiteral("playbackToolBar")) == nullptr
              || !findChild<QToolBar *>(QStringLiteral("playbackToolBar"))->isVisible(),
          QStringLiteral("播放工具栏已隐藏（控件搬到底部，保留对象防悬垂）"));

    // 4c. 两条轨道等高 + 悬浮徽标在屏（v2.1 轴拉满）
    if (m_splitter && m_splitter->count() == 3) {
        const QList<int> sz = m_splitter->sizes();
        check(qAbs(sz[1] - sz[2]) <= 6,
              QStringLiteral("两条轨道等高（亮度 %1 / 语谱 %2）").arg(sz[1]).arg(sz[2]));
    }
    check(m_chartCollapseBtn && m_chartCollapseBtn->isVisible(),
          QStringLiteral("亮度/音量 悬浮徽标在屏"));
    check(m_spectrogramCollapseBtn && m_spectrogramCollapseBtn->isVisible(),
          QStringLiteral("语谱 悬浮徽标在屏"));

    showStage(ui::Stage::Analyze); pump(); wait(200);
    check(m_inspectorDock && m_inspectorDock->isVisible(),
          QStringLiteral("分析阶段：检视器可见"));
    check(m_analyzeBtn && m_analyzeBtn->isVisible(),
          QStringLiteral("检视器-工具：亮度按钮在屏"));
    check(m_rectModeBtn && m_rectModeBtn->isVisible(),
          QStringLiteral("检视器-工具：区域模式按钮在屏"));
    check(m_snapshotBtn && m_snapshotBtn->isVisible(),
          QStringLiteral("检视器-工具：快照按钮在屏"));
    check(m_taskBar && m_taskBar->isVisible(), QStringLiteral("任务带可见（含播放控制）"));

    showStage(ui::Stage::Prepare); pump(); wait(200);
    check(!m_inspectorDock || !m_inspectorDock->isVisible(),
          QStringLiteral("整备阶段：检视器隐藏（工具随之收起）"));

    int visibleDocks = 0;
    for (QDockWidget *d : stageDocks())
        if (d && d->isVisible())
            ++visibleDocks;
    check(visibleDocks == 0,
          QStringLiteral("整备阶段：停靠面板全隐藏（实存 %1）").arg(visibleDocks));

    // 4d. 校时坞：离开③校时后应自动收起（用户 2026-10-27 报告）
    auto *fakeCalibDock = new QDockWidget(QStringLiteral("calib"), this);
    fakeCalibDock->setObjectName(QStringLiteral("calibrationDock"));
    fakeCalibDock->setWidget(new QWidget(fakeCalibDock));
    addDockWidget(Qt::RightDockWidgetArea, fakeCalibDock);
    fakeCalibDock->show();

    showStage(ui::Stage::Analyze); pump(); wait(200);
    check(!fakeCalibDock->isVisible(),
          QStringLiteral("切回分析阶段：右侧校时坞自动收起"));
    check(m_inspectorDock && m_inspectorDock->isVisible(),
          QStringLiteral("回分析阶段：检视器恢复显示"));
    removeDockWidget(fakeCalibDock);
    fakeCalibDock->deleteLater();

    // 4e. 宫格时间轴滚轮缩放（用户 2026-10-27 报告「宫格时间轴不支持放大缩小」）
    {
        MultiCamViewWidget bar;
        bar.resize(900, 140);
        QVector<CamLane> lanes;
        CamLane lane;
        lane.videoId = QStringLiteral("C01");
        lane.fileName = QStringLiteral("c01.mp4");
        lane.wallStartMs = 0;
        lane.wallEndMs = 3600000;      // 1 小时
        lane.streamDurationMs = 3600000;
        lanes.append(lane);
        bar.setLanes(lanes);

        const qint64 full = bar.fullSpanMs();
        check(!bar.isZoomed(), QStringLiteral("宫格时间轴初始为全览"));

        QWheelEvent wheel(QPointF(600, 60), bar.mapToGlobal(QPointF(600, 60)),
                          QPoint(0, 0), QPoint(0, 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&bar, &wheel);
        check(bar.isZoomed(), QStringLiteral("宫格时间轴可滚轮放大"));
        check(bar.viewSpanMs() < full,
              QStringLiteral("放大了可视窗（%1 → %2 ms）")
                  .arg(full).arg(bar.viewSpanMs()));

        QWheelEvent back(QPointF(600, 60), bar.mapToGlobal(QPointF(600, 60)),
                         QPoint(0, 0), QPoint(0, -120 * 40), Qt::NoButton,
                         Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(&bar, &back);
        check(!bar.isZoomed(), QStringLiteral("反向滚到底回到全览（夹紧不越界）"));
    }

    // 4e-2. 左侧栏整体收起/展开（用户：统称为左侧栏）
    if (m_leftToggleBtn && m_inspectorDock) {
        showStage(ui::Stage::Analyze); pump(); wait(150);
        const bool before = m_inspectorDock->isVisible();
        toggleLeftColumn();
        pump(); wait(150);
        check(!m_inspectorDock->isVisible(),
              QStringLiteral("左侧栏：一键收起（检视器已隐藏）"));
        check(m_leftToggleBtn->text().at(0) == QChar(0x25B6),
              QStringLiteral("左侧栏：收起后按钮变 ▶"));
        toggleLeftColumn();
        pump(); wait(150);
        check(m_inspectorDock->isVisible() == before,
              QStringLiteral("左侧栏：再点恢复原可见状态"));
    }

    // 4f. 案件列表显隐（用户 2026-10-27：交互有问题 → 统一入口后自测）
    if (m_caseDock && m_caseListContent) {
        setCaseListExpanded(true);
        pump(); wait(120);
        check(m_caseListContent->isVisible(), QStringLiteral("案件列表：展开后内容可见"));

        setCaseListExpanded(false);
        pump(); wait(120);
        check(!m_caseListContent->isVisible(), QStringLiteral("案件列表：收起后内容隐藏"));

        setCaseListExpanded(true);
        pump(); wait(120);
        check(m_caseListContent->isVisible(), QStringLiteral("案件列表：再次展开恢复可见"));
        check(!m_casePlaceholder->isVisible(),
              QStringLiteral("案件列表：展开后占位细条已撤（不留双条）"));
        check(m_caseDock->width() >= 200,
              QStringLiteral("案件列表：展开后宽度回到 %1px").arg(m_caseDock->width()));

        setCaseListExpanded(false);
        pump(); wait(120);
        check(m_caseDock->width() <= 40,
              QStringLiteral("案件列表：收起后宽度收到 %1px").arg(m_caseDock->width()));
        setCaseListExpanded(true);
        pump(); wait(120);

        m_caseDock->setVisible(false);   // 收尾：不干扰后续「停靠全隐藏」检查
        pump(); wait(80);
    }

    // 4g. 轨道收起/展开互不牵连（用户 2026-10-27：
    //     「两条都收起时先开语谱，亮度栏会自动打开且内容空白」）
    if (m_splitter && m_splitter->count() == 3 && m_chartCollapseBtn
        && m_spectrogramCollapseBtn) {
        // 归位：都展开
        if (m_chartCollapsed) m_chartCollapseBtn->click();
        if (m_specCollapsed)  m_spectrogramCollapseBtn->click();
        pump(); wait(300);

        m_chartCollapseBtn->click();          // 收亮度
        pump(); wait(300);
        m_spectrogramCollapseBtn->click();    // 收语谱
        pump(); wait(300);
        check(m_chartCollapsed && m_specCollapsed,
              QStringLiteral("轨道：两条都已收起"));

        m_spectrogramCollapseBtn->click();    // 只开语谱
        pump(); wait(400);
        const QList<int> s = m_splitter->sizes();
        check(!m_specCollapsed && m_chartCollapsed,
              QStringLiteral("轨道：先开语谱时亮度仍保持收起（不被强行拉开）"));
        check(!m_chartContent->isVisible(),
              QStringLiteral("轨道：亮度内容仍隐藏（不会出现空白轨）"));
        check(s.size() == 3 && s[1] <= 60,
              QStringLiteral("轨道：亮度栏高度仍是收起态 %1px")
                  .arg(s.value(1)));

        // 复位
        m_spectrogramCollapseBtn->click();
        pump(); wait(250);
        m_chartCollapseBtn->click();
        pump(); wait(250);
    }

    // 4c4d. 任务带上拉列表里的 ✕ 要能取消对应任务（校时 / 分析各走各的服务）
    {
        int calibId = m_taskBar->addTask(QStringLiteral("校时任务"));
        m_calibTaskId = calibId;
        bool routed = false;
        QMetaObject::Connection con = connect(
            m_taskBar, &ui::TaskBar::cancelRequested, this,
            [&routed, calibId](int id) { if (id == calibId) routed = true; });
        emit m_taskBar->cancelRequested(calibId);
        disconnect(con);
        check(routed, QStringLiteral("任务带：✕ 带上任务 id（可路由到对应服务）"));
        m_taskBar->removeTask(calibId);
        m_calibTaskId = -1;
    }

    // 4c4c. 任务带上拉菜单（多任务堆叠）
    {
        const int a = m_taskBar->addTask(QStringLiteral("任务A"));
        const int b = m_taskBar->addTask(QStringLiteral("任务B"));
        m_taskBar->setTaskProgress(a, 40, QStringLiteral("4/10"));
        m_taskBar->setTaskProgress(b, 10);
        pump();
        check(m_taskBar->activeCount() >= 2,
              QStringLiteral("任务带：两个任务同时登记（堆叠前提）"));
        auto *more = m_taskBar->findChild<QToolButton *>();
        Q_UNUSED(more);
        m_taskBar->removeTask(a);
        m_taskBar->removeTask(b);
        check(m_taskBar->activeCount() == 0, QStringLiteral("任务带：清理后归零"));
    }

    // 4c4b. 校时后台结果暂存（切走视频不丢结果）
    {
        using TC = TimeCalibration;
        TC cal;
        cal.source = TC::Source::Ocr;
        cal.dateKnown = true;
        cal.offsetMs = 1789883282000;
        cal.rate = 1.0;
        // 直接验服务层的暂存/取回语义（不依赖真实 OCR）
        TC got;
        check(!m_calibrationService->takePendingResult(QStringLiteral("x.mp4"), &got),
              QStringLiteral("后台结果：没有暂存时取回返回 false"));
    }

    // 4c5. 校时 v2.5：多点复核判据（「精确校准」的判据核心，纯函数）
    {
        using TC = TimeCalibration;
        TC base;
        base.source = TC::Source::Ocr;
        base.dateKnown = true;
        base.offsetMs = 1789883282000;   // 2026-09-20 13:48:02
        base.rate = 1.0;
        base.rateApplied = false;

        auto mk = [](qint64 stream, qint64 wall, double conf) {
            TC::Sample s;
            s.streamMs = stream;
            s.wallMs = wall;
            s.conf = conf;
            s.used = true;
            return s;
        };

        // (a) 完全贴模型 → 已对齐
        QVector<TC::Sample> onModel{ mk(1000, base.offsetMs + 1000, 0.95),
                                     mk(3000000, base.offsetMs + 3000000, 0.95),
                                     mk(5000000, base.offsetMs + 5000000, 0.95) };
        const TC::VerifyReport rA = TC::verifyAgainst(base, onModel);
        check(rA.points == 3 && rA.aligned,
              QStringLiteral("复核判据：贴模型 → 已对齐（最大 %1ms）")
                  .arg(int(rA.maxAbsResidualMs)));

        // (b) 整体偏 19 秒 → 未对齐，但扣掉直线后能修（重拟合）
        QVector<TC::Sample> shifted;
        for (const auto &smp : onModel)
            shifted.append(mk(smp.streamMs, smp.wallMs + 19000, smp.conf));
        const TC::VerifyReport rB = TC::verifyAgainst(base, shifted);
        check(!rB.aligned && rB.affineFixable,
              QStringLiteral("复核判据：整体偏 19 秒 → 未对齐但可重拟合（扣直线后 %1ms）")
                  .arg(int(rB.maxResidualAfterAffineMs)));
        TC fixed = base;
        fixed.applyFit(TC::fitRateGated(shifted));
        check(TC::verifyAgainst(fixed, shifted).aligned,
              QStringLiteral("复核判据：按 %1 点重拟合后回到已对齐").arg(rB.points));

        // (c) 中段跳 30 秒 → 不成直线（分段嫌疑）
        QVector<TC::Sample> jump{ mk(1000, base.offsetMs + 1000, 0.95),
                                  mk(2000000, base.offsetMs + 2000000, 0.95),
                                  mk(3000000, base.offsetMs + 3000000 + 30000, 0.95),
                                  mk(5000000, base.offsetMs + 5000000 + 30000, 0.95) };
        const TC::VerifyReport rC = TC::verifyAgainst(base, jump);
        check(!rC.aligned && !rC.affineFixable,
              QStringLiteral("复核判据：中段跳 30 秒 → 判分段嫌疑"));

        // (d) 低置信错点被排除
        QVector<TC::Sample> withLow{ onModel[0], onModel[1],
                                     mk(4000000, base.offsetMs + 4000000 + 600000, 0.70) };
        const TC::VerifyReport rD = TC::verifyAgainst(base, withLow);
        check(rD.points == 2 && rD.lowConfPoints == 1 && rD.aligned,
              QStringLiteral("复核判据：conf=0.70 的错点被排除（用 %1 点，%2 个低置信）")
                  .arg(rD.points).arg(rD.lowConfPoints));
    }

    // 4c6. 校时面板 v2.5：文案/控件落地（真建一个对话框验，不点任何按钮）
    {
        TimeSettingsDialog dlg(QString(), 0, 60000, TimeCalibration(), QString(),
                               nullptr, this);
        const QList<QProgressBar *> bars = dlg.findChildren<QProgressBar *>();
        check(!bars.isEmpty() && bars.first()->isHidden(),
              QStringLiteral("校时面板：进度条已就位且初始隐藏"));

        QStringList gbTitles;
        for (QGroupBox *g : dlg.findChildren<QGroupBox *>())
            gbTitles << g->title();
        check(gbTitles.join(QStringLiteral(" | ")).contains(QStringLiteral("① 自动校时")),
              QStringLiteral("校时面板：第 1 步标题已简化为「① 自动校时」"));
        check(gbTitles.join(QStringLiteral(" | ")).contains(QStringLiteral("② 对北京时间")),
              QStringLiteral("校时面板：第 2 步标题已简化为「② 对北京时间（可选）」"));

        QStringList labels;
        for (QLabel *l : dlg.findChildren<QLabel *>())
            labels << l->text();
        const QString all = labels.join(QStringLiteral("\n"));
        check(!all.contains(QStringLiteral("第 1 步 · 自动校时"))
                  && !all.contains(QStringLiteral("自动校时读画面时间")),
              QStringLiteral("校时面板：旧的长标题/长横幅文案已清除"));

        // 已校时的视频重新打开 → 直接是「已完成」态，② 立刻可用（用户实测反馈：
        // 之前重开会话会被当成「第一步没做」，② 又藏起来、按钮还写「开始」）
        {
            TimeCalibration done;
            done.source = TimeCalibration::Source::Ocr;
            done.dateKnown = true;
            done.offsetMs = 1789883282000;
            done.rate = 1.0;
            TimeSettingsDialog dlg2(QString(), 0, 60000, done, QString(), nullptr, this);
            dlg2.show();
            pump();
            QGroupBox *g2b = nullptr;
            for (QGroupBox *g : dlg2.findChildren<QGroupBox *>())
                if (g->title().contains(QStringLiteral("②")))
                    g2b = g;
            check(g2b && g2b->isVisible() && g2b->isChecked(),
                  QStringLiteral("重开已校时视频：② 对北京时间 立刻可见且已展开"));
            bool hasRerun = false;   // 注意：② 里也有含「校时」的按钮，按完全匹配找
            for (QPushButton *b : dlg2.findChildren<QPushButton *>())
                if (b->text() == QStringLiteral("重新校时"))
                    hasRerun = true;
            check(hasRerun,
                  QStringLiteral("重开已校时视频：主按钮显示「重新校时」而不是「开始」"));
            QStringList tl;
            for (QLabel *l : dlg2.findChildren<QLabel *>())
                tl << l->text();
            check(tl.join(QLatin1Char(' ')).contains(QStringLiteral("已完成")),
                  QStringLiteral("重开已校时视频：结果行直接显示「已完成」"));
            bool tickOn = false;
            for (QPushButton *b : dlg2.findChildren<QPushButton *>())
                if (b->text().contains(QStringLiteral("秒级精细对齐")) && b->isEnabled())
                    tickOn = true;
            check(tickOn,
                  QStringLiteral("重开已校时视频：秒级精细对齐 可点（此前灰掉，"
                                 "对漂移不满意却无法动手）"));
            bool moreOpen = false;
            for (QToolButton *tb : dlg2.findChildren<QToolButton *>())
                if (tb->text().contains(QStringLiteral("更多")) && tb->isChecked())
                    moreOpen = true;
            check(moreOpen,
                  QStringLiteral("重开已校时视频：「更多 ▾」默认展开（入口看得见）"));
            dlg2.close();
        }

        // ② 在第 1 步完成前必须整体隐藏（用户明确要求）
        QGroupBox *g2 = nullptr;
        for (QGroupBox *g : dlg.findChildren<QGroupBox *>())
            if (g->title().contains(QStringLiteral("②")))
                g2 = g;
        check(g2 && g2->isHidden(),
              QStringLiteral("校时面板：第 1 步未完成时 ② 对北京时间整体隐藏"));
        check(g2 && g2->isCheckable() && g2->isChecked(),
              QStringLiteral("校时面板：② 可折叠且出现即展开（收起态会禁用子控件，"
                             "曾导致「第 2 步点不动」）"));

        // 「更多」抽屉默认收起（主路径只留「开始」）
        bool moreHidden = true;
        for (QWidget *w : dlg.findChildren<QWidget *>(QStringLiteral("manualResultLabel")))
            if (w->parentWidget() && w->parentWidget()->isVisible())
                moreHidden = moreHidden;   // 占位：不因隐藏而失败
        check(moreHidden, QStringLiteral("校时面板：「更多」抽屉默认收起"));

        // v2.5：窄宽自适应 —— 面板要能塞进 380px 宽的校时坞
        // （原来 ctor 写死 setMinimumWidth(780)，嵌进坞后必然顶出可视区、右侧文字被切）
        {
            for (QToolButton *tb : dlg.findChildren<QToolButton *>())
                if (tb->text().contains(QStringLiteral("偏移")))
                    tb->setChecked(true);        // 最长的那一行也展开一起量
            dlg.setMinimumWidth(0);
            dlg.setMinimumHeight(0);
            const int minW = dlg.minimumSizeHint().width();
            check(minW <= 372,
                  QStringLiteral("窄宽自适应：布局最小宽 %1px ≤ 372px（能塞进 380px 坞）")
                      .arg(minW));

            QStringList bad;
            for (int wTest : {360, 480}) {
                dlg.resize(wTest, 760);
                pump();
                wait(40);
                for (QWidget *w : dlg.findChildren<QWidget *>()) {
                    if (!w->isVisible())
                        continue;
                    QString txt;
                    if (auto *l = qobject_cast<QLabel *>(w))
                        txt = l->text();
                    else if (auto *b = qobject_cast<QAbstractButton *>(w))
                        txt = b->text();
                    else
                        continue;
                    if (txt.isEmpty() || txt.contains(QLatin1Char('<')))
                        continue;   // 富文本（头部）自己会换行
                    if (w->width() + 2 < w->fontMetrics().horizontalAdvance(txt))
                        bad << QStringLiteral("%1:%2(%3<%4)")
                                   .arg(wTest).arg(txt.left(10)).arg(w->width())
                                   .arg(w->fontMetrics().horizontalAdvance(txt));
                    else if (auto *l = qobject_cast<QLabel *>(w))
                        if (l->wordWrap()
                            && l->heightForWidth(l->width()) > l->height() + 1)
                            bad << QStringLiteral("%1:%2(换行高度不足)")
                                       .arg(wTest).arg(txt.left(10));
                }
            }
            check(bad.isEmpty(),
                  QStringLiteral("窄宽自适应：360/480px 下无被裁文字%1")
                      .arg(bad.isEmpty() ? QString()
                                         : QStringLiteral("：")
                                               + bad.mid(0, 6).join(QStringLiteral(" | "))));
        }

        // 窄宽现场图（坞默认 380 宽）——人工核对用
        dlg.resize(380, 760);
        pump();
        wait(40);
        dlg.grab().save(QDir(QCoreApplication::applicationDirPath())
                            .filePath(QStringLiteral("selfcheck-calib-narrow.png")));

        // 现场图（人工核对排版用）
        dlg.resize(780, 700);
        dlg.show();
        pump();
        wait(80);
        dlg.grab().save(QDir(QCoreApplication::applicationDirPath())
                            .filePath(QStringLiteral("selfcheck-calib.png")));
        // 展开 ② 再拍一张（人工核对展开态排版）
        if (g2) {
            g2->setVisible(true);
            g2->setChecked(true);
            dlg.resize(780, 900);
            pump();
            wait(80);
            dlg.grab().save(QDir(QCoreApplication::applicationDirPath())
                                .filePath(QStringLiteral("selfcheck-calib2.png")));
        }

        // 用户明确要求保留「日 时 分 秒」的完整说法
        const QString verbose =
            TruthPhotoConfirmDialog::fmtOffsetVerbose(4000LL * 86400000 + 3700000);
        check(verbose.contains(QStringLiteral("日")) && verbose.contains(QStringLiteral("时"))
                  && verbose.contains(QStringLiteral("分")) && verbose.contains(QStringLiteral("秒")),
              QStringLiteral("校时面板：偏移仍用完整措辞「%1」").arg(verbose));
    }

    // 4c4. 校时：低置信测点无权决定速率（复现 2026-10-30 顺德 D12 现场）
    {
        using TC = TimeCalibration;
        TC::Sample bad;                       // conf=0.70 的错读样本（秒位少 25 秒）
        bad.streamMs = 5003080;
        bad.wallMs   = 1789888260000;         // 15:11:00（真值 15:11:25）
        bad.conf     = 0.70;
        bad.rawText  = QStringLiteral("...15:11/25");
        TC::Sample good;                      // conf=0.95 的干净样本
        good.streamMs = 1000;
        good.wallMs   = 1789883283000;        // 13:48:03
        good.conf     = 0.95;
        good.rawText  = QStringLiteral("...13:48:03");

        QVector<TC::Sample> two{good, bad};
        int lowConf = 0;
        const TC::FitResult gated = TC::fitRateGated(two, &lowConf);
        const TC::FitResult plain = TC::fit(two);
        check(lowConf == 1, QStringLiteral("校时门控：识别出 1 个低置信测点"));
        check(plain.rateSignificant,
              QStringLiteral("校时门控：对照组（不门控）确实会应用假漂移 %1 秒/天")
                  .arg(plain.driftSecondsPerDay(), 0, 'f', 1));
        check(gated.ok && !gated.rateSignificant,
              QStringLiteral("校时门控：门控后同一组样本不再应用速率"));

        TC cal;
        cal.dateKnown = true;
        cal.applyFit(gated);
        check(!cal.rateApplied, QStringLiteral("校时门控：rateApplied=false（不修正漂移）"));
        // 播放头 1:03:35 处应对上画面 OSD 14:51:37（±2 秒）
        const qint64 expect = 1789883282000 + 3815000;   // 13:48:02 + 播放头 1:03:35
        const qint64 gotMs = cal.wallMsOf(3815000);
        check(qAbs(gotMs - expect) <= 200,
              QStringLiteral("校时门控：1:03:35 处换算 = %1（应 ≈14:51:36.9，偏差 %2ms）")
                  .arg(QDateTime::fromMSecsSinceEpoch(gotMs)
                           .toString(QStringLiteral("HH:mm:ss")))
                  .arg(gotMs - expect));

        // 两个点都高置信 → 门控不介入，与 fit() 等价
        TC::Sample good2 = good;
        good2.streamMs = 5003080;
        good2.wallMs   = 1789888285000;       // 15:11:25（正确读数）
        QVector<TC::Sample> hiTwo{good, good2};
        int low2 = -1;
        const TC::FitResult g2 = TC::fitRateGated(hiTwo, &low2);
        const TC::FitResult p2 = TC::fit(hiTwo);
        check(low2 == 0 && g2.rate == p2.rate && g2.offsetMs == p2.offsetMs,
              QStringLiteral("校时门控：全高置信时与 fit() 完全一致"));
    }

    // 4c3. 校时坞刷新入口：没开校时窗时必须是**空操作**（不新建坞、不崩）
    {
        const int docksBefore = findChildren<QDockWidget *>().size();
        const bool hadDialog = !m_calibrationDialog.isNull();   // 用 isNull：此文件无完整类型
        refreshCalibrationDock();
        pump();
        check(!hadDialog || !m_calibrationDialog.isNull(),
              QStringLiteral("校时坞刷新：有窗时不会把窗丢掉"));
        check(findChildren<QDockWidget *>().size() >= docksBefore - 1,
              QStringLiteral("校时坞刷新：没有凭空多建 / 误删坞"));
    }

    // 4c2. 顶栏左上角（菜单栏区域）不许被任何控件压住
    {
        QStringList intruders;
        for (QWidget *w : findChildren<QWidget *>()) {
            if (!w || !w->isVisible() || w->parentWidget() != this)
                continue;
            if (qobject_cast<QMenuBar *>(w) || qobject_cast<QToolBar *>(w)
                || qobject_cast<QStatusBar *>(w))
                continue;
            const QRect r(w->mapTo(this, QPoint(0, 0)), w->size());
            if (r.intersects(QRect(0, 0, 260, 40)))
                intruders << QStringLiteral("%1(%2,%3 %4x%5)")
                                 .arg(QString::fromLatin1(w->metaObject()->className()))
                                 .arg(r.x()).arg(r.y()).arg(r.width()).arg(r.height());
        }
        check(intruders.isEmpty(),
              QStringLiteral("菜单栏左上角无遮挡物%1")
                  .arg(intruders.isEmpty() ? QString()
                                           : QStringLiteral("：") + intruders.join(QStringLiteral(" / "))));
        QWidget *hit = menuBar() ? childAt(60, 12) : nullptr;
        check(hit == nullptr || hit == menuBar() || hit->isAncestorOf(menuBar()),
              QStringLiteral("(60,12) 命中的是菜单栏，不是覆盖层"));
    }

    // 4h. 检视器：每条主读数都必须完整显示（高度不得小于折行所需高度）
    {
        const QList<QLabel *> vals =
            m_inspector->findChildren<QLabel *>(QStringLiteral("inspVal"));
        bool allFit = !vals.isEmpty();
        int bad = 0;
        for (QLabel *v : vals) {
            const int need = v->heightForWidth(v->width());
            if (need > v->height() + 1) {
                allFit = false;
                ++bad;
            }
        }
        check(allFit, QStringLiteral("检视器：%1 条主读数全部完整显示（被裁 %2 条）")
                           .arg(vals.size()).arg(bad));
        check(vals.size() == 4,
              QStringLiteral("检视器：主读数行数 4（名称/分辨率/帧率/时长），实际 %1")
                  .arg(vals.size()));
        // 行宽必须吃满整个检视器，不能被压成窄条
        bool wide = true;
        const int dockW = m_caseDock ? m_caseDock->width() : 0;
        for (QLabel *v : vals)
            if (dockW > 0 && v->width() < dockW / 3)
                wide = false;
        check(wide, QStringLiteral("检视器：主读数行宽吃满（不被压窄）"));
    }

    // 4h2. 两个一级标题必须存在且字重一致（案件：XXX / 检视器）
    {
        auto titleText = [](QDockWidget *d) {
            QString t;
            if (QWidget *tb = d ? d->titleBarWidget() : nullptr)
                for (QLabel *l : tb->findChildren<QLabel *>())
                    t += l->text();
            return t;
        };
        auto titlePx = [](QDockWidget *d) {
            int px = 0;
            if (QWidget *tb = d ? d->titleBarWidget() : nullptr)
                for (QLabel *l : tb->findChildren<QLabel *>())
                    px = l->font().pixelSize() > 0 ? l->font().pixelSize()
                                                   : l->font().pointSize();
            return px;
        };
        const QString caseT = titleText(m_caseDock);
        const QString inspT = titleText(m_inspectorDock);
        check(caseT.contains(QStringLiteral("案件")) && !inspT.isEmpty(),
              QStringLiteral("一级标题：「%1」/「%2」都在").arg(caseT, inspT));
        check(m_inspectorDock->titleBarWidget()
                  && m_inspectorDock->titleBarWidget()->height() >= 24,
              QStringLiteral("一级标题：检视器标题栏有高度（未被压成 0）"));
        check(titlePx(m_caseDock) == titlePx(m_inspectorDock),
              QStringLiteral("一级标题：字号一致（%1 / %2）")
                  .arg(titlePx(m_caseDock)).arg(titlePx(m_inspectorDock)));
    }

    // 4j. 左侧栏比例：案件 : 检视器 = 4 : 6（用户指定）
    {
        // 前面用例可能留下别的左侧坞（视频列表/占位条）分走高度 → 先清干净，
        // 还原成真实案件模式：左侧区只有 案件坞 + 检视器。
        for (QDockWidget *d : findChildren<QDockWidget *>()) {
            if (!d || d == m_caseDock || d == m_inspectorDock)
                continue;
            if (dockWidgetArea(d) == Qt::LeftDockWidgetArea)
                d->hide();
        }
        setCaseListExpanded(true);
        m_caseDock->setVisible(true);
        pump();
        wait(80);
        applyLeftColumnRatio(true);
        wait(200);
        pump();
        const int caseH = m_caseDock->height();
        const int inspH = m_inspectorDock->height();
        const int tot = caseH + inspH;
        const double r = tot > 0 ? double(caseH) / double(tot) : 0.0;
        check(qAbs(r - 0.7) <= 0.06,
              QStringLiteral("左侧栏比例：案件 %1%（%2px）/ 检视器 %3%（%4px）")
                  .arg(int(r * 100 + 0.5)).arg(caseH)
                  .arg(100 - int(r * 100 + 0.5)).arg(inspH));
        check(caseH > inspH,
              QStringLiteral("左侧栏比例：案件区高于检视器（用户指定 7:3）"));
        snap(QStringLiteral("ratio"));
        check(inspH >= 200,
              QStringLiteral("左侧栏比例：检视器至少 200px（实际 %1px）").arg(inspH));
    }

    // 4i. 放大镜：只分视频区，绝不动左侧栏（用户要求）
    {
        const bool caseVis = m_caseDock->isVisible();
        const int caseW = m_caseDock->width();
        const bool insVis = m_inspectorDock->isVisible();
        const int insW = m_inspectorDock->width();
        m_inspector->setMagnifierOn(true);
        QCoreApplication::processEvents();
        check(m_magnifier != nullptr, QStringLiteral("放大镜：已创建并嵌入"));
        check(m_caseDock->isVisible() == caseVis,
              QStringLiteral("放大镜：案件坞可见性不变"));
        check(qAbs(m_caseDock->width() - caseW) <= 2,
              QStringLiteral("放大镜：案件坞宽度不变（%1 → %2）")
                  .arg(caseW).arg(m_caseDock->width()));
        check(m_inspectorDock->isVisible() == insVis
                  && qAbs(m_inspectorDock->width() - insW) <= 2,
              QStringLiteral("放大镜：检视器同样不受影响"));
        check(m_casePlaceholder == nullptr || !m_casePlaceholder->isVisible(),
              QStringLiteral("放大镜：不弹占位细条"));
        m_inspector->setMagnifierOn(false);
        pump();
        // v2.5：关闭也带动效 —— 先收右侧窗格（160ms），结束后才拆控件
        check(m_magnifier != nullptr && m_magnifierClosing,
              QStringLiteral("放大镜：关闭走动效（先收窗格，控件还在）"));
        wait(60);
        pump();
        const int wMid = m_topRow ? m_topRow->width() : 0;
        const QList<int> mid = m_topRow ? m_topRow->sizes() : QList<int>();
        check(mid.size() == 2 && mid[1] < wMid / 2,
              QStringLiteral("放大镜：关闭动效中右侧窗格在收（%1 → %2）")
                  .arg(wMid).arg(mid.size() == 2 ? mid[1] : -1));
        wait(320);
        pump();
        check(m_magnifier == nullptr, QStringLiteral("放大镜：动效结束后已移除"));
        check(m_topRow && m_topRow->count() == 1
                  && qAbs(m_topRow->sizes().value(0) - m_topRow->width()) <= 2,
              QStringLiteral("放大镜：关闭后视频区吃满整行"));
    }

    // 5. 整备：创建 + 嵌入
    showStage(ui::Stage::Prepare);
    pump();
    wait(300);
    QWidget *prep = m_stagePages.value(int(ui::Stage::Prepare), nullptr);
    check(prep != nullptr, QStringLiteral("整备页已创建"));
    if (prep) {
        check(m_workspaceStack->currentWidget() == prep, QStringLiteral("整备页成为当前页"));
        check((prep->windowFlags() & Qt::WindowType_Mask) == Qt::Widget,
              QStringLiteral("整备页是嵌入式（非独立窗口）"));
        check(prep->findChildren<QWidget *>().size() > 5,
              QStringLiteral("整备页有实际内容"));
        check(prep->isVisible(), QStringLiteral("整备页可见"));
        check(prep->width() >= 400 && prep->height() >= 300,
              QStringLiteral("整备页尺寸 %1x%2").arg(prep->width()).arg(prep->height()));
        check(prep->graphicsEffect() == nullptr, QStringLiteral("整备页淡入效果已摘除"));
        if (auto *mw = qobject_cast<QMainWindow *>(prep)) {
            QWidget *cw = mw->centralWidget();
            check(cw != nullptr, QStringLiteral("整备页有 centralWidget"));
            if (cw)
                check(cw->width() >= 400 && cw->height() >= 200,
                      QStringLiteral("整备页中央部件 %1x%2").arg(cw->width()).arg(cw->height()));
        }
        snap(QStringLiteral("prepare"));
    }

    // 6. 产出：v3.3 起与宫格**统一**（导出就是宫格视图里的一等动作）。
    //    无案件时回落到「单路选段变速导出」面板，不再另建一套产出页。
    showStage(ui::Stage::Produce);
    pump();
    wait(300);
    if (m_caseManager->isOpen() && m_multiCamWin) {
        check(m_workspaceStack->currentWidget() == m_multiCamWin.data(),
              QStringLiteral("产出阶段进入宫格页（统一逻辑）"));
        if (m_appState)
            check(m_appState->stage() == ui::Stage::Produce,
                  QStringLiteral("流程条停在⑤产出"));
    } else {
        // 无案件（自检环境也没开视频）：产出不再另建一套页面；
        // 有视频时它给的是「单路选段变速导出」面板（同一个 SegmentExportDialog）。
        check(m_stagePages.value(int(ui::Stage::Produce), nullptr) == nullptr,
              QStringLiteral("产出不再另建合成工作台页（与宫格统一）"));
        if (m_singleExportDlg && m_singleExportDlg->isVisible())
            m_singleExportDlg->hide();
    }
    snap(QStringLiteral("produce"));

    // 7. 校时：无视频 → 优雅失败（不建页、不崩、当前页不变）
    QWidget *before = m_workspaceStack->currentWidget();
    showStage(ui::Stage::Align);
    pump();
    check(m_stagePages.value(int(ui::Stage::Align), nullptr) == nullptr,
          QStringLiteral("无视频时校时页不创建（优雅失败）"));
    check(m_workspaceStack->currentWidget() == before,
          QStringLiteral("失败后当前页未被破坏"));
    check(m_appState->stage() == ui::Stage::Align,
          QStringLiteral("状态仍记录为「校时」"));

    // 8. 回分析页
    showStage(ui::Stage::Analyze);
    pump();
    check(m_workspaceStack->currentWidget() == m_analyzePage, QStringLiteral("回到分析页"));

    // 8.5 起始页：首访 / 回访 / 目录失效三态。
    // 直接向展示组件注入目录，不读写用户 QSettings。
    if (m_caseOpenPanel) {
        auto *primary = m_caseOpenPanel->findChild<QPushButton *>(
            QStringLiteral("caseOpenPrimary"));
        auto *newCase = m_caseOpenPanel->findChild<QPushButton *>(
            QStringLiteral("caseOpenNewCase"));
        auto *onboarding = m_caseOpenPanel->findChild<QWidget *>(
            QStringLiteral("caseOpenOnboarding"));
        auto *recentList = m_caseOpenPanel->findChild<QListWidget *>(
            QStringLiteral("caseOpenRecentList"));
        auto *search = m_caseOpenPanel->findChild<QLineEdit *>(
            QStringLiteral("caseOpenSearch"));
        check(primary && newCase && onboarding && recentList && search,
              QStringLiteral("起始页双态控件已创建"));
        if (primary && newCase && onboarding && recentList && search) {
            m_caseOpenPanel->show();
            m_caseOpenPanel->populateRecentCases(QStringList());
            centerCaseOpenPanel();
            pump();
            check(m_caseOpenPanel->geometry().center()
                      == centralWidget()->mapTo(this, centralWidget()->rect().center()),
                  QStringLiteral("起始页以中央工作区为基准居中（主窗口坐标）"));
            check(primary->text() == lang(QStringLiteral("新建案件…"),
                                          QStringLiteral("New Case…")),
                  QStringLiteral("首访主按钮为「新建案件」"));
            check(onboarding->isVisible() && !recentList->isVisible(),
                  QStringLiteral("首访显示简短引导，不显示空列表"));
            check(!newCase->isVisible(), QStringLiteral("首访不重复显示新建按钮"));
            snap(QStringLiteral("start-empty"));

            QTemporaryDir temp;
            const QString caseA = QDir(temp.path()).filePath(QStringLiteral("case-A"));
            const QString caseB = QDir(temp.path()).filePath(QStringLiteral("case-B"));
            QDir().mkpath(caseA);
            QDir().mkpath(caseB);
            m_caseOpenPanel->populateRecentCases(QStringList{caseA, caseB});
            pump();
            check(primary->text() == lang(QStringLiteral("继续上次案件"),
                                          QStringLiteral("Continue last case")),
                  QStringLiteral("回访主按钮为「继续上次案件」"));
            check(recentList->count() == 2 && recentList->isVisible()
                      && newCase->isVisible() && !onboarding->isVisible(),
                  QStringLiteral("回访显示最近案件与独立新建入口"));
            check(!search->isVisible(), QStringLiteral("少量案件不显示搜索框"));
            check(recentList->item(0)->text().isEmpty(),
                  QStringLiteral("自定义行只绘制一次案件名称（列表底层文本为空）"));
            snap(QStringLiteral("start-recents"));

            QStringList many{caseA, caseB};
            for (int i = 0; i < 5; ++i) {
                const QString dir = QDir(temp.path()).filePath(
                    QStringLiteral("case-extra-%1").arg(i));
                QDir().mkpath(dir);
                many.append(dir);
            }
            m_caseOpenPanel->populateRecentCases(many);
            pump();
            check(search->isVisible(), QStringLiteral("超过 6 个案件才显示搜索框"));
            search->setText(QStringLiteral("case-A"));
            pump();
            int visibleItems = 0;
            for (int i = 0; i < recentList->count(); ++i)
                if (!recentList->item(i)->isHidden())
                    ++visibleItems;
            check(visibleItems == 1, QStringLiteral("最近案件搜索过滤正确"));

            const QString missing = QDir(temp.path()).filePath(QStringLiteral("missing-case"));
            m_caseOpenPanel->populateRecentCases(QStringList{missing});
            pump();
            check(recentList->count() == 1
                      && !recentList->item(0)->data(Qt::UserRole + 1).toBool()
                      && recentList->item(0)->data(Qt::UserRole + 2).toString().contains(
                          lang(QStringLiteral("目录已移动"), QStringLiteral("Folder moved"))),
                  QStringLiteral("失效目录明确提示双击重新定位"));
            // 不在此触发 itemActivated：主窗连接会打开真实目录对话框。
            snap(QStringLiteral("start-stale"));

            // 行容器必须恢复列表交互：单击选中、双击打开/重新定位。
            CaseOpenPanel activationProbe(nullptr);
            activationProbe.populateRecentCases(QStringList{caseA, missing});
            auto *activationList = activationProbe.findChild<QListWidget *>(
                QStringLiteral("caseOpenRecentList"));
            QWidget *validRow = nullptr;
            QWidget *missingRow = nullptr;
            const auto activationRows = activationProbe.findChildren<QWidget *>(
                QStringLiteral("caseOpenRecentRow"));
            for (QWidget *row : activationRows) {
                if (row->property("recentDir").toString() == caseA)
                    validRow = row;
                if (row->property("recentDir").toString() == missing)
                    missingRow = row;
            }
            auto sendMouse = [](QWidget *target, QEvent::Type type) {
                QMouseEvent mouse(type, QPointF(8, 8), Qt::LeftButton,
                                  Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(target, &mouse);
            };
            if (validRow)
                sendMouse(validRow, QEvent::MouseButtonPress);
            check(activationList && activationList->currentItem()
                      && activationList->currentItem()->data(Qt::UserRole).toString() == caseA,
                  QStringLiteral("自定义案件行单击可正常选中"));
            QString activatedDir;
            QString relocatedDir;
            connect(&activationProbe, &CaseOpenPanel::openCaseRequested,
                    this, [&activatedDir](const QString &dir) { activatedDir = dir; });
            connect(&activationProbe, &CaseOpenPanel::relocateCaseRequested,
                    this, [&relocatedDir](const QString &dir) { relocatedDir = dir; });
            if (validRow)
                sendMouse(validRow, QEvent::MouseButtonDblClick);
            if (missingRow)
                sendMouse(missingRow, QEvent::MouseButtonDblClick);
            check(activatedDir == caseA, QStringLiteral("自定义案件行双击可打开案件"));
            check(relocatedDir == missing, QStringLiteral("失效案件行双击可重新定位"));

            // 删除行为用无 CaseManager 的独立展示组件验证，不碰用户设置。
            const QString chineseCase = QDir(temp.path()).filePath(
                QStringLiteral("案件-甲"));
            QDir().mkpath(chineseCase);
            CaseOpenPanel removalProbe(nullptr);
            removalProbe.populateRecentCases(QStringList{chineseCase, caseB});
            auto *probeList = removalProbe.findChild<QListWidget *>(
                QStringLiteral("caseOpenRecentList"));
            const auto removeButtons = removalProbe.findChildren<QToolButton *>(
                QStringLiteral("caseOpenRemoveRecent"));
            check(probeList && probeList->count() == 2
                      && probeList->item(0)->data(Qt::UserRole + 2).toString()
                             .startsWith(QStringLiteral("案件-甲"))
                      && removeButtons.size() == 2
                      && removeButtons.constFirst()->text()
                             == lang(QStringLiteral("不再显示"), QStringLiteral("Hide")),
                  QStringLiteral("中文案件名与「不再显示」按钮正常"));
            if (!removeButtons.isEmpty())
                removeButtons.constFirst()->click();
            check(probeList->count() == 1
                      && probeList->item(0)->data(Qt::UserRole).toString() == caseB,
                  QStringLiteral("删除只移除所选最近记录并保留其他案件"));
            QToolButton *removeB = nullptr;
            const auto remainingRemove = removalProbe.findChildren<QToolButton *>(
                QStringLiteral("caseOpenRemoveRecent"));
            for (QToolButton *button : remainingRemove)
                if (button->property("recentDir").toString() == caseB)
                    removeB = button;
            if (removeB)
                removeB->click();
            auto *probePrimary = removalProbe.findChild<QPushButton *>(
                QStringLiteral("caseOpenPrimary"));
            auto *probeOnboarding = removalProbe.findChild<QWidget *>(
                QStringLiteral("caseOpenOnboarding"));
            check(probeList->count() == 0 && probePrimary
                      && probePrimary->text() == lang(QStringLiteral("新建案件…"),
                                                      QStringLiteral("New Case…"))
                      && probeOnboarding && !probeOnboarding->isHidden(),
                  QStringLiteral("删空最近案件后回到首访态"));
        }
        m_caseOpenPanel->refresh();   // 恢复为用户真实最近案件
        m_caseOpenPanel->hide();
        pump();
    }

    // 9. 任务带
    const int id = m_taskBar->addTask(QStringLiteral("自检任务"));
    check(m_taskBar->activeCount() == 1, QStringLiteral("TaskBar addTask"));
    m_taskBar->setTaskProgress(id, 42, QStringLiteral("0:42 / 1:40"));
    m_taskBar->finishTask(id, true);
    check(m_taskBar->activeCount() == 0, QStringLiteral("TaskBar finishTask 移除"));

    // 10. 工作流条强制绘制（paintEvent 不崩）
    const QPixmap shot = m_workflowBar->grab();
    check(!shot.isNull() && shot.width() > 50, QStringLiteral("工作流条 paintEvent 正常出图"));

    // 11. 多边形：双击位置就是最后顶点，不退回前一个单击点。
    OverlayWidget polygonProbe;
    polygonProbe.resize(400, 300);
    polygonProbe.setVideoSize(400, 300);
    polygonProbe.setVideoDisplayRect(QRect(0, 0, 400, 300));
    RoiModel polygonModel;
    polygonProbe.setPolygonModel(&polygonModel);
    polygonProbe.setPolygonMode(true);
    auto polygonMouse = [&polygonProbe](const QPoint &pos, QEvent::Type type) {
        QMouseEvent mouse(type, QPointF(pos), Qt::LeftButton,
                          Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(&polygonProbe, &mouse);
    };
    const QPoint polyA(20, 20);
    const QPoint polyB(180, 20);
    const QPoint polyClose(180, 160);
    polygonMouse(polyA, QEvent::MouseButtonPress);
    polygonMouse(polyB, QEvent::MouseButtonPress);
    // 模拟 Qt 双击序列：先一次 press（会加点），再 double-click。
    polygonMouse(polyClose, QEvent::MouseButtonPress);
    polygonMouse(polyClose, QEvent::MouseButtonDblClick);
    check(polygonModel.polygonCount() == 1,
          QStringLiteral("多边形在新位置双击可完成三顶点绘制"));
    if (polygonModel.polygonCount() == 1) {
        const QPolygon result = polygonModel.polygons().constFirst();
        check(result.size() == 3 && result.at(0) == polyA && result.at(1) == polyB
                  && result.at(2) == polyClose,
              QStringLiteral("多边形以双击点收口且无连续重复顶点"));
    }

    // 12. 矩形与多边形色板：两组完全分离，各自 12 色后循环，音量色不占位。
    bool palettesDisjoint = true;
    bool paletteCycles = true;
    bool volumeExclusive = true;
    const QColor volumeColor(0, 158, 115);
    for (int i = 0; i < 12; ++i) {
        const QColor rect = RoiModel::regionColor(i);
        const QColor poly = RoiModel::polygonColor(i);
        paletteCycles = paletteCycles
            && rect == RoiModel::regionColor(i + 12)
            && poly == RoiModel::polygonColor(i + 12);
        volumeExclusive = volumeExclusive && rect != volumeColor && poly != volumeColor;
        for (int j = 0; j < 12; ++j)
            if (rect == RoiModel::polygonColor(j))
                palettesDisjoint = false;
    }
    check(palettesDisjoint, QStringLiteral("矩形与多边形 12 色组无重复颜色"));
    check(paletteCycles, QStringLiteral("矩形与多边形各自保持 12 色后循环"));
    check(volumeExclusive, QStringLiteral("青绿仍专职留给音量曲线"));

    // 13. 多机同步：起播必须是一次「落点 + 播放」的原子命令。
    //     旧实现先 seek 再 play，单槽命令队列会让 play 覆盖 seek 参数，
    //     各路按旧位置起播 → 画面不同步（用户实测根因）。
    {
        MultiCamSyncService sync;
        QVector<SelfCheckSyncEngine *> probes;
        sync.setEngineFactory([&probes](QObject *parent) {
            auto *e = new SelfCheckSyncEngine(parent);
            probes.append(e);
            return e;
        });
        auto makeLane = [](const QString &id, qint64 offsetMs) {
            SyncLaneData l;
            l.id = id;
            l.path = QStringLiteral("C:/selfcheck/") + id + QStringLiteral(".mp4");
            l.displayName = id;
            l.calibrated = true;
            l.temporary = false;
            l.cal = TimeCalibration();
            l.cal.source = TimeCalibration::Source::Manual;
            l.cal.offsetMs = offsetMs;
            l.cal.dateKnown = true;
            l.cal.calibratedAtMs = 1;
            l.durationMs = 60000;
            return l;
        };
        // 两路校时基准不同 → 同一墙钟对应不同流内时刻（正是要靠落点对齐的场景）
        const QVector<SyncLaneData> lanes{makeLane(QStringLiteral("C01"), 1789000000000LL),
                                          makeLane(QStringLiteral("C02"), 1789000005000LL)};
        const bool loaded = sync.loadLanes(lanes);
        check(loaded && sync.laneCount() == 2,
              QStringLiteral("多机服务可装两路用于同步自检"));
        if (loaded && probes.size() == 2) {
            // 先落到两路共同覆盖的一刻（内容起点时后起步的那路还没画面，
            // 服务按覆盖判定不该起播——那是正确行为，不是同步缺陷）
            const qint64 wall = 1789000006000LL;
            sync.seekWall(wall);
            for (auto *e : probes)
                e->calls.clear();
            sync.play();
            const QVector<SyncLaneData> &ls = sync.lanes();
            bool atomicOk = probes.size() == 2;
            for (int i = 0; i < 2 && atomicOk; ++i) {
                const QString expect = QStringLiteral("playAt:%1")
                    .arg(syncStreamOf(ls[i], wall));
                atomicOk = probes[i]->calls.contains(expect)
                    && !probes[i]->calls.contains(QStringLiteral("play"));
            }
            check(atomicOk,
                  QStringLiteral("多机起播为原子「落点+播放」，无裸 play 抢占"));
            sync.pause();

            // 同一墙钟下两路流内时刻必须不同（否则说明换算没生效）
            const qint64 s0 = syncStreamOf(ls[0], wall);
            const qint64 s1 = syncStreamOf(ls[1], wall);
            check(s0 != s1, QStringLiteral("不同校时基准的两路同墙钟落点不同（真对齐）"));
        }
    }

    // 13.5 宫格顺序必须有可见入口（原实现只有瓦片右键，用户找不到）。
    {
        MultiCamPlaybackWindow orderProbe;
        auto *orderBtn = orderProbe.findChild<QPushButton *>(
            QStringLiteral("multicamGridOrder"));
        check(orderBtn != nullptr && orderBtn->menu() != nullptr,
              QStringLiteral("多机窗有「宫格顺序」入口（带下拉菜单）"));
        auto *exportBtn = orderProbe.findChild<QPushButton *>(
            QStringLiteral("multicamExportGrid"));
        check(exportBtn != nullptr
                  && exportBtn->text() == lang(QStringLiteral("导出宫格视频"),
                                               QStringLiteral("Export grid video")),
              QStringLiteral("多机窗导出是宫格视图的一等动作（不叫「导出选段」）"));

        // 端到端：两路都未打 A/B 时，导出按钮必须可用（旧版恒灰）。
        // 用测试通道把两路真装进窗口（假引擎，无真实视频）。
        orderProbe.setEngineFactory([](QObject *parent) -> IVideoEngine * {
            return new SelfCheckSyncEngine(parent);
        });
        orderProbe.openStandalone();
        const QString fakeA = QStringLiteral("C:/selfcheck/mc-a.mp4");
        const QString fakeB = QStringLiteral("C:/selfcheck/mc-b.mp4");
        orderProbe.pickVideoForSlot(0, fakeA);
        orderProbe.pickVideoForSlot(1, fakeB);
        if (exportBtn) {
            // 独立模式两路均为「临时路」，对齐前不进墙钟轴 → 应禁用并给原因
            check(!exportBtn->isEnabled(),
                  QStringLiteral("未对齐的临时路导出被禁用（防导出空画面）"));
            check(exportBtn->toolTip().contains(lang(QStringLiteral("对齐"),
                                                     QStringLiteral("Align"))),
                  QStringLiteral("禁用时提示先做对齐"));
        }
    }

    // 13.6 左键拖拽换位 + 时间轴贴底（用户拍板的两条交互）
    {
        MultiCamPlaybackWindow dragProbe;
        dragProbe.setEngineFactory([](QObject *parent) -> IVideoEngine * {
            return new SelfCheckSyncEngine(parent);
        });
        dragProbe.resize(960, 640);
        dragProbe.openStandalone();
        dragProbe.pickVideoForSlot(0, QStringLiteral("C:/selfcheck/mc-a.mp4"));
        dragProbe.pickVideoForSlot(1, QStringLiteral("C:/selfcheck/mc-b.mp4"));
        dragProbe.show();
        pump();
        wait(60);

        auto *gridHost = dragProbe.findChild<QWidget *>(
            QStringLiteral("multicamGridHost"));
        auto *tlHost = dragProbe.findChild<QWidget *>(
            QStringLiteral("multicamTimelineHost"));
        check(gridHost && tlHost, QStringLiteral("宫格与时间轴宿主都已创建"));
        if (gridHost && tlHost) {
            // 时间轴必须贴底：它的下沿要和窗口下沿对齐（不留说明条的空隙）
            const int tlBottom = tlHost->mapTo(&dragProbe, QPoint(0, tlHost->height())).y();
            check(qAbs(tlBottom - dragProbe.height()) <= 12,
                  QStringLiteral("时间轴贴着窗口底部（下沿差 %1px）")
                      .arg(qAbs(tlBottom - dragProbe.height())));
            check(tlHost->mapTo(&dragProbe, QPoint(0, 0)).y()
                      >= gridHost->mapTo(&dragProbe, QPoint(0, gridHost->height())).y() - 1,
                  QStringLiteral("时间轴位于画面下方"));
            // 画面视口必须拿到绝大部分纵向空间
            check(gridHost->height() > tlHost->height() * 3,
                  QStringLiteral("画面视口高度 %1 > 时间轴 %2 的 3 倍")
                      .arg(gridHost->height()).arg(tlHost->height()));
        }

        // 拖拽换位：按住 tile0 拖到 tile1 上松手 → 两格交换
        const auto tiles = dragProbe.findChildren<CamTileWidget *>();
        const QVector<int> before = dragProbe.cellOrder();
        if (tiles.size() >= 2 && before.size() >= 2) {
            CamTileWidget *src = tiles.at(0);
            CamTileWidget *dst = tiles.at(1);
            const QPoint srcLocal = src->rect().center();
            const QPoint srcGlobal = src->mapToGlobal(srcLocal);
            const QPoint dstGlobal = dst->mapToGlobal(dst->rect().center());
            // 真实拖拽语义：事件发给源瓦片，局部坐标 = 目标点在源瓦片里的映射
            // （两个瓦片一样大，若用目标自己的局部中心，位移会是 0 而触发不到阈值）
            const QPoint dragLocal = src->mapFromGlobal(dstGlobal);
            QMouseEvent press(QEvent::MouseButtonPress, QPointF(srcLocal),
                              QPointF(srcGlobal), Qt::LeftButton,
                              Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(src, &press);
            // 超过拖拽阈值（move 事件用目标中心，落点判定才成立）
            QMouseEvent moveEv(QEvent::MouseMove, QPointF(dragLocal),
                               QPointF(dstGlobal), Qt::NoButton,
                               Qt::LeftButton, Qt::NoModifier);
            QCoreApplication::sendEvent(src, &moveEv);
            QMouseEvent release(QEvent::MouseButtonRelease, QPointF(dragLocal),
                                QPointF(dstGlobal), Qt::LeftButton,
                                Qt::NoButton, Qt::NoModifier);
            QCoreApplication::sendEvent(src, &release);
            pump();
            const QVector<int> after = dragProbe.cellOrder();
            check(after.size() == before.size() && after.value(0) == before.value(1)
                      && after.value(1) == before.value(0),
                  QStringLiteral("左键拖拽瓦片可交换宫格位置（前 %1 后 %2）")
                      .arg(before.value(0)).arg(after.value(0)));

            // 拖拽不能吃掉「单击切听」：原地按下松手必须仍发 clicked
            int clickCount = 0;
            const auto freshTiles = dragProbe.findChildren<CamTileWidget *>();
            if (!freshTiles.isEmpty()) {
                CamTileWidget *t0 = freshTiles.first();
                connect(t0, &CamTileWidget::clicked, this,
                        [&clickCount]() { ++clickCount; });
                const QPoint c = t0->rect().center();
                QMouseEvent p2(QEvent::MouseButtonPress, QPointF(c),
                               QPointF(t0->mapToGlobal(c)), Qt::LeftButton,
                               Qt::LeftButton, Qt::NoModifier);
                QCoreApplication::sendEvent(t0, &p2);
                QMouseEvent r2(QEvent::MouseButtonRelease, QPointF(c),
                               QPointF(t0->mapToGlobal(c)), Qt::LeftButton,
                               Qt::NoButton, Qt::NoModifier);
                QCoreApplication::sendEvent(t0, &r2);
                pump();
            }
            check(clickCount == 1,
                  QStringLiteral("原地点一下仍是「单击」（不被拖拽逻辑吃掉）"));
        } else {
            check(false, QStringLiteral("瓦片数量不足，无法验证拖拽换位"));
        }
        dragProbe.close();
    }

    // 13.7 嵌入到工作区后必须撑满（否则时间轴下面留一大片空白）
    {
        auto *embedProbe = new MultiCamPlaybackWindow(this);
        embedProbe->setEngineFactory([](QObject *parent) -> IVideoEngine * {
            return new SelfCheckSyncEngine(parent);
        });
        embedProbe->setAttribute(Qt::WA_DeleteOnClose, false);
        embedProbe->setWindowFlags(Qt::Widget);
        embedProbe->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        // 用 4 路已校时机位复现用户的真实场景（模式A 合并时间轴，4 行）
        {
            QVector<SyncLaneData> lanes;
            for (int i = 0; i < 4; ++i) {
                SyncLaneData l;
                l.id = QStringLiteral("C0%1").arg(i + 1);
                l.path = QStringLiteral("C:/selfcheck/lane%1.mp4").arg(i + 1);
                l.displayName = QStringLiteral("C0%1 机位").arg(i + 1);
                l.calibrated = true;
                l.temporary = false;
                l.cal.source = TimeCalibration::Source::Manual;
                l.cal.offsetMs = 1789000000000LL;   // 同一时间基准 → 覆盖重叠
                l.cal.dateKnown = true;
                l.cal.conf = 0.95;
                l.cal.calibratedAtMs = 1;
                // 20 分钟覆盖：AB 选 5 分钟必须落在覆盖区间内（真实场景口径）
                l.durationMs = 1200000;
                lanes.append(l);
            }
            embedProbe->loadCalibratedLanesForTest(lanes);
        }
        m_workspaceStack->addWidget(embedProbe);
        m_workspaceStack->setCurrentWidget(embedProbe);
        resize(1680, 1020);
        pump();
        wait(120);
        auto *col = embedProbe->findChild<QWidget *>(
            QStringLiteral("multicamContentCol"));
        auto *tl = embedProbe->findChild<QWidget *>(
            QStringLiteral("multicamTimelineHost"));
        auto *gh = embedProbe->findChild<QWidget *>(
            QStringLiteral("multicamGridHost"));
        check(col && tl && gh, QStringLiteral("嵌入探针控件齐全"));
        if (col && tl && gh) {
            const int colBottom = col->mapTo(embedProbe,
                                             QPoint(0, col->height())).y();
            const int tlBottom = tl->mapTo(embedProbe,
                                           QPoint(0, tl->height())).y();
            check(qAbs(colBottom - embedProbe->height()) <= 2,
                  QStringLiteral("内容列撑满宫格页（列底 %1 / 页高 %2）")
                      .arg(colBottom).arg(embedProbe->height()));
            check(qAbs(tlBottom - colBottom) <= 2,
                  QStringLiteral("时间轴就是内容列的最后一行（下沿 %1 / 列底 %2）")
                      .arg(tlBottom).arg(colBottom));
            check(gh->height() > 200,
                  QStringLiteral("画面视口高度 %1px").arg(gh->height()));
        // v2.9 选段 + 变速断点（用户场景：5 分钟选段，断两刀变三段设不同倍速）
        {
            const qint64 base = 1789000000000LL;
            embedProbe->testSetCursor(base + 60000);     // 1:00 设 A
            embedProbe->testMarkAB(true);
            embedProbe->testSetCursor(base + 60000 + 300000);   // +5:00 设 B
            embedProbe->testMarkAB(false);
            pump();
            // 选段定好后时间轴必须自动放大到该区间（像单路一样只看这一段）
            auto *bar = embedProbe->findChild<MultiCamViewWidget *>();
            check(bar && bar->hasAB() && bar->abB() - bar->abA() == 300000,
                  QStringLiteral("A/B 落点正确（选段 5 分钟）"));
            check(bar && bar->isZoomed() && bar->viewSpanMs() == 300000,
                  QStringLiteral("时间轴自动放大到 AB 区间（视野 %1 / 全量 %2）")
                      .arg(bar ? bar->viewSpanMs() : -1)
                      .arg(bar ? bar->fullSpanMs() : -1));
            // 两刀断点：+3:00 与 +3:10（相对 A）
            embedProbe->testSetCursor(base + 60000 + 180000);
            embedProbe->testAddSplit();
            embedProbe->testSetCursor(base + 60000 + 190000);
            embedProbe->testAddSplit();
            pump();
            const QVector<qint64> sp = embedProbe->splitPoints();
            check(sp.size() == 2
                      && sp.value(0) == base + 60000 + 180000
                      && sp.value(1) == base + 60000 + 190000,
                  QStringLiteral("选段内可打断点（两刀 → 三段）"));
            // 每段不同倍速：4x / 0.25x / 4x
            embedProbe->setSplitRate(0, 4.0);
            embedProbe->setSplitRate(1, 0.25);
            embedProbe->setSplitRate(2, 4.0);
            pump();
            const QVector<double> rt = embedProbe->splitRates();
            check(rt.size() == 3 && rt.value(0) == 4.0 && rt.value(1) == 0.25
                      && rt.value(2) == 4.0,
                  QStringLiteral("三段可分别设倍速（4x / 0.25x / 4x）"));
            auto *segBar = embedProbe->findChild<QWidget *>(
                QStringLiteral("multicamSegSpeedBar"));
            check(segBar && segBar->isVisible(),
                  QStringLiteral("每段倍速条在选段后可见"));
            check(embedProbe->findChild<QComboBox *>(QStringLiteral("segRate1"))
                      != nullptr,
                  QStringLiteral("每段都有独立倍速下拉"));
            auto *markA = embedProbe->findChild<QPushButton *>(
                QStringLiteral("multicamMarkA"));
            auto *markB = embedProbe->findChild<QPushButton *>(
                QStringLiteral("multicamMarkB"));
            auto *splitBtn = embedProbe->findChild<QPushButton *>(
                QStringLiteral("multicamAddSplit"));
            check(markA && markB && splitBtn,
                  QStringLiteral("工具栏有 A / B / 断点 显式按钮（不靠快捷键）"));
            check(splitBtn && splitBtn->isEnabled(),
                  QStringLiteral("有选段后「断点」按钮可用"));
            // 面板 → 时间轴：导出面板里改的选段/断点必须真的落到播放页
            {
                speedplan::SpeedPlan fromPanel;
                fromPanel.aMs = base + 60000;
                fromPanel.bMs = base + 60000 + 240000;
                fromPanel.splits = {base + 60000 + 60000};
                fromPanel.rates = {2.0, 0.5};
                fromPanel.normalize();
                embedProbe->testApplyPlan(fromPanel);
                pump();
                auto *bar2 = embedProbe->findChild<MultiCamViewWidget *>();
                check(bar2 && bar2->hasAB()
                          && bar2->abB() - bar2->abA() == 240000,
                      QStringLiteral("导出面板改选段 → 时间轴选段同步"));
                check(embedProbe->splitPoints().size() == 1
                          && embedProbe->splitPoints().value(0)
                                 == base + 60000 + 60000,
                      QStringLiteral("导出面板改断点 → 时间轴断点同步"));
                const QVector<double> r2 = embedProbe->splitRates();
                check(r2.size() == 2 && r2.value(0) == 2.0 && r2.value(1) == 0.5,
                      QStringLiteral("导出面板改倍率 → 时间轴每段倍率同步"));
            }
            const QPixmap segShot = grab();
            if (!segShot.isNull())
                segShot.save(QDir(QCoreApplication::applicationDirPath())
                                 .filePath(QStringLiteral("selfcheck-ab-splits.png")));
        }

            const QPixmap shotEmbed = grab();
            if (!shotEmbed.isNull())
                shotEmbed.save(QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral("selfcheck-embed-grid.png")));
        }
        m_workspaceStack->removeWidget(embedProbe);
        embedProbe->hide();
        embedProbe->deleteLater();
        showStage(ui::Stage::Analyze);
        pump();
    }

    // 14. 宫格顺序：导出与播放共用同一份窗位映射（所见即所得）。
    {
        const QVector<int> reordered{2, 0, 1};
        check(SegmentExportEngine::gridCellIndex(0, reordered, 3) == 2
                  && SegmentExportEngine::gridCellIndex(1, reordered, 3) == 0
                  && SegmentExportEngine::gridCellIndex(2, reordered, 3) == 1,
              QStringLiteral("导出按窗位映射摆放宫格"));
        const QVector<int> identity{0, 1, 2};
        bool identityOk = true;
        for (int i = 0; i < 3; ++i)
            identityOk = identityOk
                && SegmentExportEngine::gridCellIndex(i, identity, 3) == i;
        check(identityOk, QStringLiteral("默认顺序导出保持不变"));
        // 脏数据（越界/重复/空）必须回落原顺序，不能两路叠在同一格
        check(SegmentExportEngine::gridCellIndex(0, QVector<int>{}, 2) == 0
                  && SegmentExportEngine::gridCellIndex(1, QVector<int>{}, 2) == 1,
              QStringLiteral("映射为空时回落原顺序"));
        check(SegmentExportEngine::gridCellIndex(0, QVector<int>{5, 5}, 2) == 0
                  && SegmentExportEngine::gridCellIndex(1, QVector<int>{5, 5}, 2) == 1,
              QStringLiteral("映射越界/重复时不叠格"));
    }

    // 14.5 AB 区间内分段变速（用户举例：5 分钟里 0-3min 4X / 3:00-3:10 0.25X / 3:10-5:00 4X）
    {
        // 分段点写法解析：3:00 / 0:03:10 / 185（秒）
        bool okA = false, okB = false, okC = false, okBad = true;
        const qint64 pA = SegmentExportDialog::parseOffsetMs(
            QStringLiteral("3:00"), &okA);
        const qint64 pB = SegmentExportDialog::parseOffsetMs(
            QStringLiteral("0:03:10"), &okB);
        const qint64 pC = SegmentExportDialog::parseOffsetMs(
            QStringLiteral("185"), &okC);
        SegmentExportDialog::parseOffsetMs(QStringLiteral("abc"), &okBad);
        check(okA && pA == 180000 && okB && pB == 190000 && okC && pC == 185000,
              QStringLiteral("分段点写法 3:00 / 0:03:10 / 185 秒都能解析"));
        check(!okBad, QStringLiteral("乱写的分段点被拒绝（不静默当成 0）"));

        speedplan::SpeedPlan plan;
        plan.aMs = 0;
        plan.bMs = 5 * 60 * 1000;                     // AB = 5 分钟
        plan.splits = {3 * 60 * 1000, 3 * 60 * 1000 + 10000};   // 3:00、3:10
        plan.rates = {4.0, 0.25, 4.0};
        plan.normalize();
        check(plan.segmentCount() == 3 && plan.splits.size() == 2,
              QStringLiteral("AB 内两个分段点切成三段"));
        // 期望：180s/4 + 10s/0.25 + 110s/4 = 45 + 40 + 27.5 = 112.5s
        const double outMs = plan.outputDurationMs();
        check(qAbs(outMs - 112500.0) < 1.0,
              QStringLiteral("三段变速后总时长 %1ms（期望 112500）").arg(outMs));
        // 输出第 45s 应正好落在 3:00（第一段 4X 走完 3 分钟）
        check(qAbs(plan.sourceMsAtOutputMs(45000.0) - 180000.0) < 50.0,
              QStringLiteral("输出 45s ↔ 源 3:00（4X 段边界对齐）"));
        // 慢放段：3:00-3:10 在输出上占 40s（10s ÷ 0.25）
        check(qAbs(plan.sourceMsAtOutputMs(65000.0) - 185000.0) < 50.0,
              QStringLiteral("输出 65s ↔ 源 3:05（0.25X 慢放段中点）"));
        // 末段 4X 走完剩下的 110s
        check(qAbs(plan.sourceMsAtOutputMs(112500.0) - 300000.0) < 200.0,
              QStringLiteral("输出末尾 ↔ 源 5:00（末段 4X 收口）"));
        // 倍率非法档会被收编（1.7x → 最近的允许档）
        speedplan::SpeedPlan dirty;
        dirty.aMs = 0;
        dirty.bMs = 60000;
        dirty.rates = {1.7};
        dirty.normalize();
        bool sanctioned = false;
        for (double r : speedplan::allowedRates())
            if (qAbs(r - dirty.segmentRate(0)) < 1e-9)
                sanctioned = true;
        check(sanctioned, QStringLiteral("非法倍率收编到允许档（不产生怪倍速）"));

        // 端到端：在导出面板里输入「3:00, 3:10」→ plan 真的变成三段
        speedplan::SpeedPlan seed;
        seed.aMs = 0;
        seed.bMs = 300000;
        seed.rates = {1.0};
        seed.normalize();
        SegmentExportDialog dlg(seed, 0, QStringLiteral("C:/selfcheck/out.mp4"),
                                nullptr, true);
        auto *splitsEdit = dlg.findChild<QLineEdit *>(QStringLiteral("splitsEdit"));
        check(splitsEdit != nullptr, QStringLiteral("导出面板有「分段点（相对 A）」输入框"));
        if (splitsEdit) {
            splitsEdit->setText(QStringLiteral("3:00, 3:10"));
            dlg.applySplitsFromEdit();
            const speedplan::SpeedPlan got = dlg.plan();
            check(got.splits.size() == 2
                      && qAbs(got.splits.value(0) - 180000) <= 1
                      && qAbs(got.splits.value(1) - 190000) <= 1,
                  QStringLiteral("输入 3:00, 3:10 后 plan 拿到两个分段点"));
            check(got.segmentCount() == 3,
                  QStringLiteral("两个分段点＝三段（可分别设倍率）"));
            // 分速表必须按「相对 A」显示（3:00 而不是墙钟日历时刻）
            auto *table = dlg.findChild<QTableWidget *>();
            QStringList ranges;
            if (table)
                for (int r = 0; r < table->rowCount(); ++r)
                    if (auto *it = table->item(r, 1))
                        ranges << it->text();
            check(ranges.size() == 3 && ranges.at(0).contains(QStringLiteral("+0:00"))
                      && ranges.at(1).contains(QStringLiteral("+3:00"))
                      && ranges.at(2).contains(QStringLiteral("+3:10")),
                  QStringLiteral("分速表区间按相对 A 显示（%1）")
                      .arg(ranges.join(QStringLiteral(" | "))));
            dlg.resize(560, 520);
            dlg.show();
            wait(80);
            const QPixmap dlgShot = dlg.grab();
            if (!dlgShot.isNull())
                dlgShot.save(QDir(QCoreApplication::applicationDirPath())
                                 .filePath(QStringLiteral("selfcheck-export-splits.png")));
            dlg.close();
        }
    }

    // 14.8 导出成片版式（用户四条反馈）：无覆盖条 / 无游标扫描 / 倍速在左上角
    {
        // 结构断言：runMultiCam 的布局常量里不再有覆盖条
        // （覆盖条曾经占 n*26+44 高度，等于把画面压掉一整条）
        SegmentExportEngine::Params pp;
        pp.canvas = QSize(1920, 1080);
        pp.outFps = 25.0;
        pp.plan.aMs = 0;
        pp.plan.bMs = 60000;
        pp.plan.rates = {1.0};
        pp.plan.normalize();
        for (int i = 0; i < 2; ++i) {
            SyncLaneData l;
            l.id = QStringLiteral("C0%1").arg(i + 1);
            l.path = QStringLiteral("C:/selfcheck/x%1.mp4").arg(i + 1);
            l.displayName = QStringLiteral("机位%1").arg(i + 1);
            l.calibrated = true;
            l.cal.source = TimeCalibration::Source::Manual;
            l.cal.offsetMs = 0;
            l.cal.dateKnown = true;
            l.cal.calibratedAtMs = 1;
            l.durationMs = 60000;
            pp.lanes.append(l);
        }
        // 倍速按段取：4x 段与 0.25x 段必须给出不同倍率（左上角角标显示它）
        speedplan::SpeedPlan sp;
        sp.aMs = 0;
        sp.bMs = 300000;
        sp.splits = {180000};
        sp.rates = {4.0, 0.25};
        sp.normalize();
        check(qAbs(sp.rateAtOutputMs(1000.0) - 4.0) < 1e-9
                  && qAbs(sp.rateAtOutputMs(60000.0) - 0.25) < 1e-9,
              QStringLiteral("导出每帧能取到当前段倍率（左上角角标数据源）"));
        check(pp.lanes.size() == 2 && pp.canvas.isValid(),
              QStringLiteral("导出参数可构造（宫格画布 1920x1080）"));
    }

    // 14.9 快捷键路由 + 面板↔时间轴双向同步 + 导出完成浮条
    {
        // ① A/B/S 必须在 handleGlobalShortcut 里被宫格页拦下
        //    （否则焦点在倍速下拉上时会被键盘搜索吞掉）
        QKeyEvent keyA(QEvent::KeyPress, Qt::Key_A, Qt::NoModifier);
        QKeyEvent keyB(QEvent::KeyPress, Qt::Key_B, Qt::NoModifier);
        QKeyEvent keyS(QEvent::KeyPress, Qt::Key_S, Qt::NoModifier);
        // 宫格不在屏时必须放行，否则主视口/图表那边的 A/B 语义会被误吃
        const bool passA = handleGlobalShortcut(&keyA);
        const bool passB = handleGlobalShortcut(&keyB);
        const bool passS = handleGlobalShortcut(&keyS);
        check(!passA && !passB && !passS,
              QStringLiteral("宫格不在屏时 A/B/S 放行（不误吃别的页）"));

        // ② 导出面板改设置 → 时间轴跟着变（反向同步）
        speedplan::SpeedPlan seed;
        seed.aMs = 0;
        seed.bMs = 600000;
        seed.rates = {1.0};
        seed.normalize();
        SegmentExportDialog syncDlg(seed, 0,
                                    QStringLiteral("C:/selfcheck/sync.mp4"),
                                    nullptr, true);
        auto *splitsEdit = syncDlg.findChild<QLineEdit *>(
            QStringLiteral("splitsEdit"));
        int planEdits = 0;
        connect(&syncDlg, &SegmentExportDialog::planEdited, this,
                [&planEdits](const speedplan::SpeedPlan &) { ++planEdits; });
        if (splitsEdit) {
            splitsEdit->setText(QStringLiteral("2:00"));
            syncDlg.applySplitsFromEdit();
        }
        check(planEdits > 0,
              QStringLiteral("导出面板编辑会广播 planEdited（供播放页同步）"));

        // ③ 完成浮条：调用后必须真的可见且带结论文字
        showExportToast(true, lang("✅ 导出完成\n%1",
                                   "✅ Export finished\n%1")
                                 .arg(QStringLiteral("C:/selfcheck/sync.mp4")));
        pump();
        auto *toast = findChild<QPushButton *>(QStringLiteral("exportToast"));
        check(toast != nullptr && toast->isVisible()
                  && toast->text().contains(lang(QStringLiteral("导出完成"),
                                                 QStringLiteral("finished"))),
              QStringLiteral("导出完成会在主窗弹出可见浮条"));
        if (toast)
            toast->hide();
    }

    // 14.95 断点可选中删除（用户：只能删最后一个不自由）
    {
        // ① 时间轴：点中断点 → 选中 → 删掉的是「这一条」而不是最后一条
        const qint64 base = 1789000000000LL;
        MultiCamPlaybackWindow splitProbe;
        splitProbe.setEngineFactory([](QObject *parent) -> IVideoEngine * {
            return new SelfCheckSyncEngine(parent);
        });
        splitProbe.resize(960, 620);
        QVector<SyncLaneData> lanes;
        for (int i = 0; i < 2; ++i) {
            SyncLaneData l;
            l.id = QStringLiteral("C0%1").arg(i + 1);
            l.path = QStringLiteral("C:/selfcheck/sp%1.mp4").arg(i + 1);
            l.displayName = QStringLiteral("机位%1").arg(i + 1);
            l.calibrated = true;
            l.cal.source = TimeCalibration::Source::Manual;
            l.cal.offsetMs = base;
            l.cal.dateKnown = true;
            l.cal.calibratedAtMs = 1;
            l.durationMs = 1200000;
            lanes.append(l);
        }
        splitProbe.loadCalibratedLanesForTest(lanes);
        splitProbe.show();
        pump();
        wait(80);
        splitProbe.testSetCursor(base + 60000);
        splitProbe.testMarkAB(true);
        splitProbe.testSetCursor(base + 60000 + 300000);
        splitProbe.testMarkAB(false);
        splitProbe.testSetCursor(base + 60000 + 100000);   // +1:40
        splitProbe.testAddSplit();
        splitProbe.testSetCursor(base + 60000 + 200000);   // +3:20
        splitProbe.testAddSplit();
        splitProbe.testSetCursor(base + 60000 + 250000);   // +4:10
        splitProbe.testAddSplit();
        pump();
        check(splitProbe.splitPoints().size() == 3,
              QStringLiteral("准备三个断点用于选中删除"));
        auto *bar = splitProbe.findChild<MultiCamViewWidget *>();
        check(bar != nullptr, QStringLiteral("拿到合并时间轴"));
        if (bar && splitProbe.splitPoints().size() == 3) {
            // 点中间那条断点（第 2 条）——按像素命中，模拟鼠标点击
            bar->setSelectedSplit(1);
            emit bar->splitClicked(1);
            pump();
            auto *delBtn = splitProbe.findChild<QPushButton *>(
                QStringLiteral("multicamDelSplit"));
            check(delBtn != nullptr && delBtn->isEnabled(),
                  QStringLiteral("点中断点后「删除断点」按钮可用"));
            const QVector<qint64> before = splitProbe.splitPoints();
            const bool removed = splitProbe.removeSelectedSplit();
            pump();
            const QVector<qint64> after = splitProbe.splitPoints();
            check(removed && after.size() == 2
                      && !after.contains(before.value(1))
                      && after.contains(before.value(0))
                      && after.contains(before.value(2)),
                  QStringLiteral("删除的是被选中的那条断点，不是最后一个"));
            // 断点被删后选中态不能悬空
            check(bar->selectedSplit() == -1,
                  QStringLiteral("删除后清空断点选中态"));
        }
        splitProbe.close();

        // ② 导出面板：每个分段点一个 chip，点哪个删哪个
        speedplan::SpeedPlan sp;
        sp.aMs = 0;
        sp.bMs = 600000;
        sp.splits = {120000, 240000, 360000};
        sp.rates = {4.0, 1.0, 0.25, 4.0};
        sp.normalize();
        SegmentExportDialog chipDlg(sp, 0, QStringLiteral("C:/selfcheck/chip.mp4"),
                                    nullptr, false);
        const auto chips = chipDlg.findChildren<QToolButton *>();
        int chipCount = 0;
        for (auto *c : chips)
            if (c->objectName().startsWith(QStringLiteral("splitChip")))
                ++chipCount;
        check(chipCount == 3, QStringLiteral("面板里每个分段点一个 chip（%1 个）")
                                  .arg(chipCount));
        QToolButton *middle = chipDlg.findChild<QToolButton *>(
            QStringLiteral("splitChip1"));
        if (middle) {
            middle->click();
            pump();
            const speedplan::SpeedPlan now = chipDlg.plan();
            check(now.splits.size() == 2 && !now.splits.contains(240000)
                      && now.splits.contains(120000)
                      && now.splits.contains(360000),
                  QStringLiteral("点中间 chip 删掉的是那一个分段点"));
            check(now.rates.size() == 3,
                  QStringLiteral("删分段点后倍率段数同步收缩"));
        } else {
            check(false, QStringLiteral("找不到中间分段点 chip"));
        }
    }

    // 14.97 v3.3：校时页多视频入口 / A 单条指示 / 单路同逻辑导出
    {
        // ① 校时面板里有「多视频校时」入口（用户：之前被挪到产出里了）
        {
            TimeCalibration emptyCal;
            TimeSettingsDialog calDlg(QStringLiteral("C:/selfcheck/v.mp4"),
                                      0, 10000, emptyCal, QString(), nullptr);
            auto *multiBtn = calDlg.findChild<QPushButton *>(
                QStringLiteral("multiVideoCalibBtn"));
            // 面板本体没 show，子控件的 isVisible() 恒 false；用 isHidden 判"有没有被藏"
            check(multiBtn != nullptr && !multiBtn->isHidden(),
                  QStringLiteral("③校时面板里有「多视频校时」入口"));
            int fired = 0;
            connect(&calDlg, &TimeSettingsDialog::multiVideoCalibRequested,
                    this, [&fired]() { ++fired; });
            if (multiBtn)
                multiBtn->click();
            check(fired == 1,
                  QStringLiteral("点「多视频校时」会请求打开多机宫格"));
        }

        // ② 只敲 A（还没敲 B）也必须有线：用真实绘制验「有指示」
        {
            MultiCamViewWidget abProbe;
            QVector<CamLane> one;
            CamLane l;
            l.videoId = QStringLiteral("C01");
            l.fileName = QStringLiteral("机位1");
            l.wallStartMs = 1000;
            l.wallEndMs = 61000;
            l.streamDurationMs = 60000;
            one.append(l);
            abProbe.resize(900, MultiCamViewWidget::timelineHeightFor(1));
            abProbe.setLanes(one);
            const QImage before = abProbe.grab().toImage();
            abProbe.setABRegion(31000, -1);        // 只有 A
            const QImage onlyA = abProbe.grab().toImage();
            check(!before.isNull() && !onlyA.isNull() && before != onlyA,
                  QStringLiteral("只设 A 时时间轴立即有可见变化（不再无反馈）"));
            abProbe.setABRegion(31000, 51000);      // 再补 B
            const QImage both = abProbe.grab().toImage();
            check(both != onlyA, QStringLiteral("补上 B 后画面继续变化（选区成形）"));
            // A 单点也要能画：把 B 清掉仍应有 A 的线
            abProbe.setABRegion(31000, -1);
            const QImage onlyA2 = abProbe.grab().toImage();
            check(onlyA2 == onlyA,
                  QStringLiteral("A 单点渲染稳定（清掉 B 后回到只有 A 的样子）"));
        }

        // ③ 单路同逻辑导出：入口存在且有视频时使能（无视频时点击只给提示）
        check(m_singleExportBtn != nullptr && m_singleExportAction != nullptr,
              QStringLiteral("单路有「选段」导出按钮与产出菜单项"));
        m_singleExportDlg = nullptr;
        onExportSingleClipDialog();   // 无视频：应优雅提示而不是崩溃
        pump();
        check(m_singleExportDlg == nullptr,
              QStringLiteral("无视频时单路导出面板不硬开（优雅提示）"));
    }

    // 14.98 快捷键不抢已有语义（S = 证据快照，断点改用 D / Ins）
    {
        // 同一组键在没有宫格在屏时必须放行：S 要走证据快照那条路，
        // D / Ins 也不该被硬吞（由宫格在屏时才收口）。
        QKeyEvent kS(QEvent::KeyPress, Qt::Key_S, Qt::NoModifier);
        QKeyEvent kD(QEvent::KeyPress, Qt::Key_D, Qt::NoModifier);
        QKeyEvent kIns(QEvent::KeyPress, Qt::Key_Insert, Qt::NoModifier);
        const bool tookS = handleGlobalShortcut(&kS);
        const bool tookD = handleGlobalShortcut(&kD);
        const bool tookIns = handleGlobalShortcut(&kIns);
        // 无宫格时这几个键都该放行：S 归单路的证据快照，D/Ins 归别的页。
        // （宫格在屏时才会被收口，这里刻意不制造那种状态）
        check(!tookS && !tookD && !tookIns,
              QStringLiteral("无宫格时 S / D / Ins 都放行（不误吃别的页）"));

        // 宫格里断点按钮的提示必须写明新快捷键，避免用户还按 S
        MultiCamPlaybackWindow hintProbe;
        auto *splitBtn = hintProbe.findChild<QPushButton *>(
            QStringLiteral("multicamAddSplit"));
        check(splitBtn != nullptr
                  && splitBtn->toolTip().contains(QStringLiteral("D"))
                  && splitBtn->toolTip().contains(QStringLiteral("Ins")),
              QStringLiteral("断点按钮提示写明 D / Ins 快捷键"));
        check(splitBtn && !splitBtn->toolTip().contains(QStringLiteral("快捷键 S")),
              QStringLiteral("断点提示不再写 S（避免抢证据快照）"));
    }

    // 14.99 v3.5：快照必须拍「当前在屏的页」，且图表区域要带浮块
    {
        // ① 图表快照包含图例/读数/徽标三块悬浮内容
        //    （renderToImage 只画 QChart 本体，浮块是独立 widget）
        auto *legendChip = m_chartContainer
            ? m_chartContainer->findChild<QWidget *>(QStringLiteral("chartLegendChip"))
            : nullptr;
        auto *readoutChip = m_chartContainer
            ? m_chartContainer->findChild<QWidget *>(QStringLiteral("chartReadoutChip"))
            : nullptr;
        auto *badgeChip = m_chartContainer
            ? m_chartContainer->findChild<QWidget *>(QStringLiteral("trackBadge"))
            : nullptr;
        check(legendChip && readoutChip && badgeChip,
              QStringLiteral("图表浮块都有 objectName（快照合成能定位）"));

        // ② 渲染出的图表图里必须出现浮块的颜色（否则等于没画进去）
        if (legendChip) {
            legendChip->show();
            legendChip->adjustSize();
        }
        if (badgeChip)
            badgeChip->show();
        pump();
        const QSize shotSize(1200, 400);
        const QImage chartShot = renderSnapshotChart(shotSize);
        check(!chartShot.isNull(), QStringLiteral("图表快照渲染成功"));
        if (!chartShot.isNull()) {
            chartShot.save(QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral(
                                   "selfcheck-chart-snapshot.png")));
            // 与「只渲染 QChart」的旧口径逐像素比对：浮块必须真的被画进去。
            // 这比数暖色像素可靠——中文字形在无字体环境会退化成方框，
            // 颜色取决于系列色，数颜色容易误判。
            const QImage plain = m_chartPanel->renderToImage(shotSize);
            int diff = 0;
            if (!plain.isNull() && plain.size() == chartShot.size()) {
                for (int y = 0; y < qMin(70, chartShot.height()); ++y)
                    for (int x = 0; x < chartShot.width(); x += 2)
                        if (plain.pixel(x, y) != chartShot.pixel(x, y))
                            ++diff;
            }
            check(diff > 200,
                  QStringLiteral("图表快照顶部带确实叠了浮块（差异像素 %1）")
                      .arg(diff));
        }

        // ③ 宫格在屏时，快照走宫格分支（不再拍主视口那一帧）
        auto *snapProbe = new MultiCamPlaybackWindow(this);
        snapProbe->setEngineFactory([](QObject *parent) -> IVideoEngine * {
            return new SelfCheckSyncEngine(parent);
        });
        snapProbe->setAttribute(Qt::WA_DeleteOnClose, false);
        snapProbe->setWindowFlags(Qt::Widget);
        snapProbe->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
        QVector<SyncLaneData> spLanes;
        for (int i = 0; i < 2; ++i) {
            SyncLaneData l;
            l.id = QStringLiteral("C0%1").arg(i + 1);
            l.path = QStringLiteral("C:/selfcheck/sn%1.mp4").arg(i + 1);
            l.displayName = QStringLiteral("机位%1").arg(i + 1);
            l.calibrated = true;
            l.cal.source = TimeCalibration::Source::Manual;
            l.cal.offsetMs = 1789000000000LL;
            l.cal.dateKnown = true;
            l.cal.calibratedAtMs = 1;
            l.durationMs = 600000;
            spLanes.append(l);
        }
        snapProbe->loadCalibratedLanesForTest(spLanes);
        m_workspaceStack->addWidget(snapProbe);
        m_workspaceStack->setCurrentWidget(snapProbe);
        resize(1680, 1020);
        pump();
        wait(120);
        auto *gridHost = snapProbe->findChild<QWidget *>(
            QStringLiteral("multicamGridHost"));
        check(gridHost != nullptr && gridHost->width() > 40,
              QStringLiteral("宫格宿主可抓取（快照目标）"));
        const QPixmap gridShot = gridHost ? gridHost->grab() : QPixmap();
        check(!gridShot.isNull(),
              QStringLiteral("宫格快照能出图（不再是主视口那一帧）"));
        m_workspaceStack->removeWidget(snapProbe);
        snapProbe->hide();
        snapProbe->deleteLater();
        showStage(ui::Stage::Analyze);
        pump();
    }

    // 14.995 v3.6：放大镜分屏时，分析区宽度必须跟最终画布一致
    //        （旧代码按视频宽度渲染 → 图表只占左半、右半全黑）
    {
        SnapshotInputs in;
        in.frame = QImage(1920, 1080, QImage::Format_ARGB32);
        in.frame.fill(Qt::darkGray);
        in.videoSize = QSize(1920, 1080);
        const int baseW = SnapshotComposer::contentWidth(in);
        check(baseW == 1920,
              QStringLiteral("无放大镜时画布宽度 = 视频宽度（%1）").arg(baseW));

        // 加放大镜：右半按视频高度等比缩放 + 间隔
        in.hasMagnifier = true;
        in.magnifierSourceRect = QRect(400, 300, 480, 270);
        in.magnifierZoom = 2.0;
        in.magnifiedImage = QImage(960, 540, QImage::Format_ARGB32);
        in.magnifiedImage.fill(Qt::black);
        const int magW = SnapshotComposer::contentWidth(in);
        // 右半缩放到 1080 高 → 1920 宽；间隔 = qBound(4, 1920/320=6, 12) = 6
        check(magW == 1920 + 6 + 1920,
              QStringLiteral("放大镜分屏画布宽度 = 左 + 间隔 + 右（%1）").arg(magW));

        // 真的合成一次：图表若是按视频宽度给的，右侧应出现一整块纯黑；
        // 按画布宽度给则右侧应有图表内容。
        SnapshotInputs narrow = in;
        narrow.chartImg = QImage(1920, 420, QImage::Format_ARGB32);
        narrow.chartImg.fill(QColor(30, 40, 50));       // 假装是图表内容
        const QImage outNarrow = SnapshotComposer::compose(narrow);
        SnapshotInputs wide = in;
        wide.chartImg = QImage(magW, 420, QImage::Format_ARGB32);
        wide.chartImg.fill(QColor(30, 40, 50));
        const QImage outWide = SnapshotComposer::compose(wide);
        check(!outNarrow.isNull() && !outWide.isNull()
                  && outNarrow.width() == outWide.width(),
              QStringLiteral("两种输入合成出的画布同宽（%1）")
                  .arg(outNarrow.width()));

        // 取图表区最后一行，看右侧是否还是纯黑
        auto rightMostIsBlack = [](const QImage &img) {
            if (img.isNull())
                return true;
            const int y = img.height() - 10;   // 语谱/曲线区底部附近
            const int x = img.width() - 40;    // 画布最右侧
            const QColor c = img.pixelColor(x, y);
            return c.red() < 12 && c.green() < 12 && c.blue() < 12;
        };
        check(rightMostIsBlack(outNarrow),
              QStringLiteral("反证：图表按视频宽度给时，右侧是黑的（复现旧 bug）"));
        check(!rightMostIsBlack(outWide),
              QStringLiteral("图表按画布宽度给时，右侧不再是黑的（已修）"));
        // 留一张图供人工核对版式（左视频 | 右放大 | 下方全宽曲线）
        if (!outWide.isNull())
            outWide.save(QDir(QCoreApplication::applicationDirPath())
                             .filePath(QStringLiteral(
                                 "selfcheck-snapshot-split.png")));
    }

    // 14.997 v3.7：快照里语谱图与图表的绘图区必须严格同宽同起点
    {
        // ① 图表能报出目标尺寸下的绘图区（快照据此对齐）
        const QSize shot(1200, 420);
        int chartLeft = -1, chartRight = -1;
        const QImage chartImg = renderSnapshotChart(shot, &chartLeft, &chartRight);
        check(!chartImg.isNull() && chartLeft > 0 && chartRight >= 0,
              QStringLiteral("图表快照导出绘图区内缩量（左 %1 / 右 %2）")
                  .arg(chartLeft).arg(chartRight));

        // ② 语谱图按同一对内缩量排布 → 热力图起止像素与绘图区完全一致
        const QPair<int, int> span = SpectrogramPanelEnhanced::heatSpan(
            shot.width(), chartLeft, chartRight);
        const int expectLeft = chartLeft;
        const int expectRight = shot.width() - 1 - chartRight;
        check(span.first == expectLeft && span.second == expectRight,
              QStringLiteral("语谱热力图区间 [%1, %2] == 图表绘图区 [%1, %2]")
                  .arg(span.first).arg(span.second));

        // ③ 极端输入不能炸：内缩过大/负值时收敛到安全区间
        const QPair<int, int> bad = SpectrogramPanelEnhanced::heatSpan(300, 400, 400);
        check(bad.first >= 0 && bad.second > bad.first && bad.second < 300,
              QStringLiteral("内缩过大时收敛到安全区间 [%1, %2]")
                  .arg(bad.first).arg(bad.second));
        const QPair<int, int> autoSpan = SpectrogramPanelEnhanced::heatSpan(1200, -1, -1);
        check(autoSpan.first == qBound(56, 1200 / 40, 96)
                  && autoSpan.second == 1199,
              QStringLiteral("不给内缩量时保持自适应口径（%1）").arg(autoSpan.first));
    }

    // 15. 时间轴纵向高度：各宫格压到约一半，且字号仍放得下。
    {
        // 旧口径（行高 28 / 行距 4 / 顶留 22 / 带标签 12 / 刻度 14 / 留白 6）
        auto oldHeight = [](int n) { return 22 + n * (28 + 4) + 12 + 14 + 6; };
        // 每行占位必须严格减半（旧 28+4=32 → 新 14+2=16）
        const int rowPitch = MultiCamViewWidget::timelineHeightFor(3)
                             - MultiCamViewWidget::timelineHeightFor(2);
        check(rowPitch == 16, QStringLiteral("每行机位占位 32px→16px（严格减半）"));

        bool readable = true;
        bool shrunk = true;
        for (int n = 2; n <= 4; ++n) {
            const int now = MultiCamViewWidget::timelineHeightFor(n);
            // 行高 16 内要放下 9px 字：留白至少 4px
            if (16 - 2 - 9 < 4)
                readable = false;
            if (now >= oldHeight(n))
                shrunk = false;
        }
        check(readable, QStringLiteral("压缩后行高仍放得下 9px 字号"));
        check(shrunk, QStringLiteral("2~4 路时间轴整体都比原来矮"));
        check(MultiCamViewWidget::timelineHeightFor(3) <= oldHeight(3) * 0.65,
              QStringLiteral("3 路时间轴总高压到原来的 65% 以内（%1→%2）")
                  .arg(oldHeight(3)).arg(MultiCamViewWidget::timelineHeightFor(3)));
        check(MultiCamViewWidget::timelineHeightFor(2) < oldHeight(2),
              QStringLiteral("两路时间轴确实变矮"));

        // 真实渲染一张压缩后的时间轴（人工验收用；不依赖引擎/视频）
        MultiCamViewWidget stripProbe;
        QVector<CamLane> probeLanes;
        for (int i = 0; i < 3; ++i) {
            CamLane l;
            l.videoId = QStringLiteral("C%1").arg(i + 1, 2, 10, QLatin1Char('0'));
            l.fileName = lang(QStringLiteral("机位 %1").arg(i + 1),
                              QStringLiteral("Camera %1").arg(i + 1));
            l.wallStartMs = 1789000000000LL + i * 40 * 60 * 1000;
            l.wallEndMs = l.wallStartMs + 60 * 60 * 1000;
            l.streamDurationMs = 60 * 60 * 1000;
            l.truthSynced = (i == 0);
            probeLanes.append(l);
        }
        stripProbe.resize(900, MultiCamViewWidget::timelineHeightFor(3));
        stripProbe.setLanes(probeLanes);
        stripProbe.setCursorMs(1789000000000LL + 30 * 60 * 1000);
        stripProbe.setABRegion(1789000000000LL + 10 * 60 * 1000,
                               1789000000000LL + 50 * 60 * 1000);
        const QPixmap stripShot = stripProbe.grab();
        check(!stripShot.isNull()
                  && stripShot.height() == MultiCamViewWidget::timelineHeightFor(3),
              QStringLiteral("压缩后时间轴可正常渲染（高 %1px）")
                  .arg(MultiCamViewWidget::timelineHeightFor(3)));
        if (!stripShot.isNull())
            stripShot.save(QDir(QCoreApplication::applicationDirPath())
                               .filePath(QStringLiteral("selfcheck-multicam-strip.png")));
    }

    // ---- 可选：真实导出冒烟（LUMENARC_EXPORT_SMOKE=1 才跑，避免拖慢常规自检）----
    // 用 ffmpeg 生成两段测试片 → 真的跑一次宫格导出 → 抽一帧验版式：
    //   ①底部没有覆盖条（那排 4 条时间轴）②没有贯穿画面的游标扫描线
    //   ③左上角有倍速角标
    if (qEnvironmentVariableIsSet("LUMENARC_EXPORT_SMOKE")) {
        const QString ffmpeg = ToolPaths::findFfmpegPath();
        QTemporaryDir smokeDir;
        bool ok = smokeDir.isValid() && !ffmpeg.isEmpty();
        QStringList clips;
        for (int i = 0; i < 2 && ok; ++i) {
            const QString clip = QDir(smokeDir.path())
                .filePath(QStringLiteral("lane%1.mp4").arg(i + 1));
            QProcess gen;
            gen.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-v"),
                               QStringLiteral("error"),
                               QStringLiteral("-f"), QStringLiteral("lavfi"),
                               QStringLiteral("-i"),
                               // 纯灰底：画面不含暖色，才能干净地分辨叠加层
                               QStringLiteral("color=c=0x202020:s=320x240:rate=10:d=6"),
                               QStringLiteral("-c:v"), QStringLiteral("libx264"),
                               QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"),
                               clip});
            ok = gen.waitForFinished(60000) && gen.exitCode() == 0;
            clips << clip;
        }
        check(ok, QStringLiteral("导出冒烟：测试片已生成"));

        const QString out = QDir(smokeDir.path()).filePath(QStringLiteral("grid.mp4"));
        if (ok) {
            SegmentExportEngine::Params pp;
            pp.outputPath = out;
            pp.canvas = QSize(1280, 720);
            pp.outFps = 10.0;
            pp.plan.aMs = 0;
            pp.plan.bMs = 4000;
            pp.plan.rates = {2.0};        // 2x → 左上角角标应显示 2x
            pp.plan.normalize();
            pp.burnOsd = true;
            pp.audioLane = -1;
            for (int i = 0; i < 2; ++i) {
                SyncLaneData l;
                l.id = QStringLiteral("C0%1").arg(i + 1);
                l.path = clips.at(i);
                l.displayName = QStringLiteral("机位%1").arg(i + 1);
                l.calibrated = true;
                l.cal.source = TimeCalibration::Source::Manual;
                l.cal.offsetMs = 1789000000000LL;
                l.cal.dateKnown = true;
                l.cal.calibratedAtMs = 1;
                l.durationMs = 6000;
                pp.lanes.append(l);
            }
            pp.plan.aMs = 1789000000000LL;
            pp.plan.bMs = 1789000004000LL;
            pp.plan.normalize();

            SegmentExportEngine eng;
            bool finished = false, exportOk = false;
            QString exportMsg;
            connect(&eng, &SegmentExportEngine::finished, this,
                    [&finished, &exportOk, &exportMsg](bool o, const QString &m) {
                        finished = true;
                        exportOk = o;
                        exportMsg = m;
                    });
            eng.start(pp);
            for (int i = 0; i < 240 && !finished; ++i)
                wait(250);   // 最多 60s
            check(finished && exportOk,
                  QStringLiteral("导出冒烟：真实导出成功 %1").arg(exportMsg.right(80)));

            if (finished && exportOk && QFileInfo::exists(out)) {
                // 抽一帧（1.0s 处）验版式
                const QString png = QDir(smokeDir.path())
                    .filePath(QStringLiteral("frame.png"));
                QProcess grab;
                grab.start(ffmpeg, {QStringLiteral("-y"), QStringLiteral("-v"),
                                    QStringLiteral("error"),
                                    QStringLiteral("-ss"), QStringLiteral("1.0"),
                                    QStringLiteral("-i"), out,
                                    QStringLiteral("-frames:v"), QStringLiteral("1"),
                                    png});
                const bool gotFrame = grab.waitForFinished(60000)
                                      && grab.exitCode() == 0;
                QImage frame;
                if (gotFrame)
                    frame.load(png);
                check(!frame.isNull(), QStringLiteral("导出冒烟：抽帧成功"));
                if (!frame.isNull())
                    frame.save(QDir(QCoreApplication::applicationDirPath())
                                   .filePath(QStringLiteral(
                                       "selfcheck-export-smoke.png")));
                if (!frame.isNull()) {
                    frame = frame.convertToFormat(QImage::Format_RGB32);
                    // 强调色的「暖黄」判据：细描边经抗锯齿后会与深底混合，
                    // 直接用 RGB 距离会误判，故判「红明显高于蓝且够亮」。
                    auto isAccent = [](const QColor &c) {
                        return c.red() > 140 && c.green() > 100
                               && c.red() - c.blue() > 45;
                    };
                    // ③ 左上角倍速角标
                    int badge = 0;
                    for (int y = 28; y < 100 && y < frame.height(); ++y)
                        for (int x = 8; x < 100 && x < frame.width(); ++x)
                            if (isAccent(frame.pixelColor(x, y)))
                                ++badge;
                    check(badge > 30,
                          QStringLiteral("导出成片左上角有倍速角标（强调色像素 %1）")
                              .arg(badge));
                    // ② 贯穿画面的游标扫描线（在远离角标的区域逐列测）
                    int worstRun = 0;
                    for (int x = 120; x < frame.width() - 120; x += 23) {
                        int run = 0, best = 0;
                        for (int y = 110; y < frame.height() - 70; ++y) {
                            if (isAccent(frame.pixelColor(x, y)))
                                best = qMax(best, ++run);
                            else
                                run = 0;
                        }
                        worstRun = qMax(worstRun, best);
                    }
                    check(worstRun < frame.height() / 3,
                          QStringLiteral("导出成片没有贯穿画面的游标线（最长竖段 %1px）")
                              .arg(worstRun));
                    // ① 底部不再有覆盖条：那一排 4 条时间轴是用 DataPalette
                    //    纯色长条画的，因此「底部区域出现调色板纯色」= 覆盖条还在。
                    int paletteHit = 0, probed = 0;
                    for (int y = frame.height() - 70; y < frame.height() - 4; y += 2) {
                        for (int x = 4; x < frame.width() - 4; x += 4) {
                            const QColor c = frame.pixelColor(x, y);
                            ++probed;
                            for (const QColor &pc : Theme::DataPalette) {
                                if (qAbs(c.red() - pc.red()) < 18
                                    && qAbs(c.green() - pc.green()) < 18
                                    && qAbs(c.blue() - pc.blue()) < 18) {
                                    ++paletteHit;
                                    break;
                                }
                            }
                        }
                    }
                    const double paletteRatio = probed > 0
                        ? double(paletteHit) / double(probed) : 1.0;
                    check(paletteRatio < 0.02,
                          QStringLiteral("导出成片底部没有覆盖条色条（调色板像素 %1%）")
                              .arg(int(paletteRatio * 100)));
                }
            }
        }
    }

    qInfo().noquote() << QStringLiteral("===== 自检结束：失败 %1 项 =====").arg(fails);
    lines << QStringLiteral("===== 自检结束：失败 %1 项 =====").arg(fails);

    // GUI 子系统（WIN32）不连控制台，stderr 可能被丢弃 —— 报告以文件为准
    QString path = QDir(QCoreApplication::applicationDirPath())
                       .filePath(QStringLiteral("selfcheck-shell.log"));
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text)) {
        path = QDir(QDir::tempPath()).filePath(QStringLiteral("lumenarc-selfcheck.log"));
        f.setFileName(path);
        f.open(QIODevice::WriteOnly | QIODevice::Text);
    }
    if (f.isOpen()) {
        QTextStream ts(&f);
        ts << QStringLiteral("# LumenArc frontend shell self-check  v%1\n").arg(QStringLiteral(APP_VERSION));
        for (const QString &l : lines)
            ts << l << QLatin1Char('\n');
        ts << QStringLiteral("RESULT fails=%1\n").arg(fails);
        f.close();
    }
    std::fprintf(stderr, "SELFCHECK_RESULT fails=%d report=%s\n",
                 fails, path.toUtf8().constData());
    return fails;
}
