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
#include "domain/roi_model.h"
#include "i18n.h"
#include "keyguardfilter.h"
#include "infrastructure/ffmpeg_video_engine.h"
#include "infrastructure/ivideo_engine.h"
#include "multicamplaybackwindow.h"
#include "playbackadjustpanel.h"
#include "theme.h"
#include "ui/app_state.h"
#include "ui/command_registry.h"
#include "multicamview.h"
#include "ui/chevron_button.h"
#include "ui/inspector_panel.h"
#include "ui/task_bar.h"
#include "ui/workflow_bar.h"

#include <QAbstractAnimation>
#include <QCheckBox>
#include <QComboBox>
#include <QDebug>
#include <QDir>
#include <QDockWidget>
#include <QEasingCurve>
#include <QEventLoop>
#include <QFile>
#include <QTextStream>
#include <QTimer>
#include <QGraphicsOpacityEffect>
#include <QGridLayout>
#include <QHeaderView>
#include <QLabel>
#include <QListWidget>
#include <QLocale>
#include <QMenuBar>
#include <QOpenGLWidget>
#include <QPropertyAnimation>
#include <QPushButton>
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

    constexpr int kCaseMinH = 180;         // 至少还能看见几行机位
    constexpr int kInspMinH = 200;         // 至少够「工具」区常驻
    const int want = qRound(total * 0.4);  // 案件 40%，检视器 60%
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

    QWidget *page = m_stagePages.value(int(s), nullptr);

    // 页尚未创建 → 触发该阶段的既有入口（入口内部会 embedWorkspace 回来）
    if (!page) {
        switch (s) {
        case ui::Stage::Prepare: openPreprocessWindow(); break;
        case ui::Stage::Produce: onExportSegmentClip();  break;
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
        setCaseListExpanded(true);        // 保证案件坞在屏（前面的用例可能收起了）
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
        check(qAbs(r - 0.4) <= 0.06,
              QStringLiteral("左侧栏比例：案件 %1%（%2px）/ 检视器 %3%（%4px）")
                  .arg(int(r * 100 + 0.5)).arg(caseH)
                  .arg(100 - int(r * 100 + 0.5)).arg(inspH));
        check(inspH > caseH,
              QStringLiteral("左侧栏比例：检视器高于案件区（不再被压缩）"));
        snap(QStringLiteral("ratio"));
        check(inspH >= 300,
              QStringLiteral("左侧栏比例：检视器至少 300px（实际 %1px）").arg(inspH));
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
        QCoreApplication::processEvents();
        check(m_magnifier == nullptr, QStringLiteral("放大镜：关闭后已移除"));
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

    // 6. 产出：创建 + 嵌入
    showStage(ui::Stage::Produce);
    pump();
    wait(300);
    QWidget *prod = m_stagePages.value(int(ui::Stage::Produce), nullptr);
    check(prod != nullptr, QStringLiteral("产出页已创建"));
    if (prod) {
        check(m_workspaceStack->currentWidget() == prod, QStringLiteral("产出页成为当前页"));
        check((prod->windowFlags() & Qt::WindowType_Mask) == Qt::Widget,
              QStringLiteral("产出页是嵌入式（非独立窗口）"));
        check(prod->isVisible(), QStringLiteral("产出页可见"));
        check(prod->width() >= 900 && prod->height() >= 600,
              QStringLiteral("产出页尺寸 %1x%2").arg(prod->width()).arg(prod->height()));
        snap(QStringLiteral("produce"));
    }

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

    // 9. 任务带
    const int id = m_taskBar->addTask(QStringLiteral("自检任务"));
    check(m_taskBar->activeCount() == 1, QStringLiteral("TaskBar addTask"));
    m_taskBar->setTaskProgress(id, 42, QStringLiteral("0:42 / 1:40"));
    m_taskBar->finishTask(id, true);
    check(m_taskBar->activeCount() == 0, QStringLiteral("TaskBar finishTask 移除"));

    // 10. 工作流条强制绘制（paintEvent 不崩）
    const QPixmap shot = m_workflowBar->grab();
    check(!shot.isNull() && shot.width() > 50, QStringLiteral("工作流条 paintEvent 正常出图"));

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
