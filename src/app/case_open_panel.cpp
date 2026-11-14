/**
 * @file case_open_panel.cpp
 * @brief 案件工作入口实现（2026-11：首访引导 / 回访续作双态布局 + 卡片化视觉）
 */
#include "case_open_panel.h"

#include <QDir>
#include <QEvent>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMouseEvent>
#include <QPixmap>
#include <QPushButton>
#include <QResizeEvent>
#include <QSettings>
#include <QShortcut>
#include <QSizePolicy>
#include <QStyle>
#include <QToolButton>
#include <QVariant>
#include <QVBoxLayout>

#include "app/case_manager.h"
#include "i18n.h"
#include "theme.h"

namespace {

/// 左动作列固定宽度（actionHost->setFixedWidth）：主按钮下那行案件名按它省略，
/// 不依赖子控件布局时机（构造期 width() 还不可信）。
constexpr int kActionColumnWidth = 218;

/// 面板宽 → 最近案件行文字列可用宽的固定扣减：
/// 卡片内边距 48 + 动作列 218 + body 间距 30 + 行内边距 16
/// + 图标 24+10 + 「不再显示」64+10 + 竖向滚动条余量 14 + 字形余量 8 = 442
/// （字形余量是给样式表字体尚未落到 widget font 时的估算误差留的，宁窄不裁字）
constexpr int kRecentRowChromeWidth = 442;

} // namespace

bool CaseOpenPanel::showStartupPanel()
{
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    return s.value(QStringLiteral("case/showStartPage"), true).toBool();
}

void CaseOpenPanel::setShowStartupPanel(bool on)
{
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    s.setValue(QStringLiteral("case/showStartPage"), on);
}

CaseOpenPanel::CaseOpenPanel(CaseManager *cm, QWidget *parent)
    : QFrame(parent)
    , m_caseManager(cm)
{
    setObjectName(QStringLiteral("caseOpenPanel"));
    setMinimumSize(680, 440);
    resize(780, 520);
    setStyleSheet(QStringLiteral(
        "QFrame#caseOpenPanel { background:%1; border:1px solid %2;"
        " border-radius:16px; }"
        "QLabel { border:none; background:transparent; }"
        "QLineEdit { background:%3; color:%4; border:1px solid %7;"
        " border-radius:6px; padding:5px 9px; selection-background-color:%5; }"
        "QLineEdit:focus { border-color:%5; }"
        "QListWidget { background:transparent; border:none; outline:none; }"
        "QListWidget::item { padding:0px; border-radius:8px; }"
        "QListWidget::item:selected { background:%6; color:%4; }"
        // 行容器（setItemWidget）铺满整个 item，item:hover 画不到；悬停反馈
        // 改由行容器自己的 :hover 绘制（见 populateRecentCases）。
        "QListWidget::item:hover { background:transparent; }")
        .arg(Theme::BgCard, Theme::BgHover, Theme::BgApp, Theme::TextPrimary,
             Theme::Accent, Theme::BgHover, Theme::Border));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(24, 24, 24, 24);
    root->setSpacing(20);

    // 品牌区（视觉稿②）：32px logo + 产品名 + 一行说明。
    // 产品名与主窗口标题／关于框同源，说明沿用原副标题文案，不新造宣传语。
    auto *header = new QHBoxLayout();
    header->setSpacing(10);
    auto *logo = new QLabel(this);
    logo->setObjectName(QStringLiteral("caseOpenLogo"));
    logo->setFixedSize(32, 32);
    logo->setScaledContents(true);
    const QPixmap logoPixmap(QStringLiteral(":/logo.png"));   // src/resources.qrc alias
    if (!logoPixmap.isNull()) {
        logo->setPixmap(logoPixmap.scaled(32, 32, Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation));
    } else {
        logo->hide();   // 资源缺失时不留占位空洞（不新增 png 资源）
    }
    auto *brandBox = new QVBoxLayout();
    brandBox->setSpacing(2);
    auto *title = new QLabel(lang("追光者 Lumen Arc", "Lumen Arc"), this);
    title->setStyleSheet(QStringLiteral(
        "font-size:14pt; font-weight:700; color:%1;").arg(Theme::TextPrimary));
    auto *subtitle = new QLabel(lang("火灾调查视频分析", "Fire-investigation video analysis"), this);
    subtitle->setStyleSheet(QStringLiteral("color:%1; font-size:11px;")
                                .arg(Theme::TextSecond));
    brandBox->addWidget(title);
    brandBox->addWidget(subtitle);
    header->addWidget(logo, 0, Qt::AlignVCenter);
    header->addLayout(brandBox, 1);

    auto *closeButton = new QPushButton(lang("关闭", "Close"), this);
    closeButton->setCursor(Qt::PointingHandCursor);
    closeButton->setFixedHeight(28);
    closeButton->setStyleSheet(QStringLiteral(
        "QPushButton { background:transparent; color:%1; border:1px solid transparent;"
        " border-radius:5px; padding:0 8px; }"
        "QPushButton:hover { color:%2; background:%3; }"
        "QPushButton:focus { border-color:%4; }")
        .arg(Theme::TextSecond, Theme::TextPrimary, Theme::BgHover, Theme::Accent));
    connect(closeButton, &QPushButton::clicked, this, &CaseOpenPanel::closeRequested);
    header->addWidget(closeButton, 0, Qt::AlignTop);
    root->addLayout(header);

    auto *body = new QHBoxLayout();
    body->setSpacing(30);
    root->addLayout(body, 1);

    // ---- 左：动作列 ----
    auto *actions = new QVBoxLayout();
    actions->setSpacing(10);
    auto *actionHost = new QWidget(this);
    actionHost->setObjectName(QStringLiteral("caseOpenActions"));
    actionHost->setStyleSheet(QStringLiteral("QWidget#caseOpenActions{background:transparent;}"));
    actionHost->setFixedWidth(218);
    actionHost->setLayout(actions);
    body->addWidget(actionHost);

    const QString primaryStyle = QStringLiteral(
        "QPushButton { background:%1; color:%2; font-weight:700;"
        " border:1px solid %1; border-radius:10px; font-size:11pt; }"
        "QPushButton:hover { background:%3; border-color:%3; }"
        "QPushButton:pressed { background:%4; border-color:%4; }"
        "QPushButton:focus { border-color:%2; }")
        .arg(Theme::Accent, Theme::AccentOnDark, Theme::AccentHover,
             Theme::AccentPress);
    const QString secondaryStyle = QStringLiteral(
        "QPushButton { background:transparent; color:%1; border:1px solid %2;"
        " border-radius:10px; font-size:10pt; }"
        "QPushButton:hover { border-color:%3; color:%3; background:%4; }"
        "QPushButton:focus { border-color:%3; }")
        .arg(Theme::TextPrimary, Theme::BgHover, Theme::Accent, Theme::BgHover);

    m_primaryButton = new QPushButton(this);
    m_primaryButton->setObjectName(QStringLiteral("caseOpenPrimary"));
    m_primaryButton->setFixedHeight(48);
    m_primaryButton->setCursor(Qt::PointingHandCursor);
    m_primaryButton->setStyleSheet(primaryStyle);
    m_primaryButton->setDefault(true);
    actions->addWidget(m_primaryButton);

    m_primaryCaption = new QLabel(this);
    m_primaryCaption->setStyleSheet(QStringLiteral("color:%1; font-size:12px;")
                                        .arg(Theme::TextSecond));
    m_primaryCaption->setAlignment(Qt::AlignCenter);
    actions->addWidget(m_primaryCaption);
    actions->addSpacing(7);

    m_newCaseButton = new QPushButton(lang("新建案件…", "New Case…"), this);
    m_newCaseButton->setObjectName(QStringLiteral("caseOpenNewCase"));
    m_newCaseButton->setFixedHeight(48);
    m_newCaseButton->setCursor(Qt::PointingHandCursor);
    m_newCaseButton->setStyleSheet(secondaryStyle);
    actions->addWidget(m_newCaseButton);

    m_openCaseButton = new QPushButton(this);
    m_openCaseButton->setObjectName(QStringLiteral("caseOpenBrowse"));
    m_openCaseButton->setFixedHeight(48);
    m_openCaseButton->setCursor(Qt::PointingHandCursor);
    m_openCaseButton->setStyleSheet(secondaryStyle);
    actions->addWidget(m_openCaseButton);

    m_directVideoButton = new QPushButton(
        lang("直接打开视频", "Open a video directly"), this);
    m_directVideoButton->setObjectName(QStringLiteral("caseOpenDirectVideo"));
    m_directVideoButton->setFixedHeight(32);
    m_directVideoButton->setCursor(Qt::PointingHandCursor);
    m_directVideoButton->setToolTip(lang("不进入案件，直接查看视频", "View a video without opening a case"));
    m_directVideoButton->setStyleSheet(QStringLiteral(
        "QPushButton { background:transparent; color:%1; border:1px solid transparent;"
        " border-radius:6px; text-align:left; padding:0 8px; }"
        "QPushButton:hover { color:%2; background:%3; }"
        "QPushButton:focus { border-color:%2; }")
        .arg(Theme::TextSecond, Theme::Accent, Theme::BgHover));
    actions->addWidget(m_directVideoButton);
    actions->addStretch(1);

    // ---- 右：首访引导 / 最近案件 ----
    m_onboardingPage = new QWidget(this);
    m_onboardingPage->setObjectName(QStringLiteral("caseOpenOnboarding"));
    m_onboardingPage->setStyleSheet(QStringLiteral("QWidget#caseOpenOnboarding{background:transparent;}"));
    auto *onboarding = new QVBoxLayout(m_onboardingPage);
    onboarding->setContentsMargins(0, 2, 0, 0);
    onboarding->setSpacing(8);
    auto *firstUse = new QLabel(lang("第一次使用？", "First time here?"), m_onboardingPage);
    firstUse->setStyleSheet(QStringLiteral("font-size:13pt; font-weight:600; color:%1;")
                                .arg(Theme::TextPrimary));
    onboarding->addWidget(firstUse);
    onboarding->addSpacing(6);

    const QStringList steps = {
        lang("新建案件", "Create a case"),
        lang("添加视频", "Add videos"),
        lang("开始分析", "Start analysis")
    };
    // 说明行只陈述程序真实行为（取证工具第一原则：界面不与实际不符），不做宣传语。
    const QStringList stepHints = {
        lang("在案件根目录下建立案件文件夹", "Creates a case folder under the case root"),
        lang("可拖入，支持多路视频与音频", "Drag files in; multiple video and audio routes"),
        lang("框选区域量化亮度，或分析音频", "Quantify brightness in a region, or analyze audio")
    };
    for (int i = 0; i < steps.size(); ++i) {
        auto *row = new QHBoxLayout();
        row->setSpacing(10);
        auto *number = new QLabel(QString::number(i + 1), m_onboardingPage);
        number->setFixedSize(22, 22);
        number->setAlignment(Qt::AlignCenter);
        number->setStyleSheet(QStringLiteral(
            "color:%1; background:%2; border:none; border-radius:11px;"
            " font-size:11px; font-weight:700;")
            .arg(Theme::Accent, Theme::withAlpha(Theme::Accent, 20)));
        auto *stepTexts = new QVBoxLayout();
        stepTexts->setSpacing(1);
        auto *text = new QLabel(steps[i], m_onboardingPage);
        text->setStyleSheet(QStringLiteral("color:%1; font-size:13px; font-weight:600;")
                                .arg(Theme::TextPrimary));
        auto *hint = new QLabel(i < stepHints.size() ? stepHints.at(i) : QString(),
                                m_onboardingPage);
        hint->setStyleSheet(QStringLiteral("color:%1; font-size:11px;")
                                .arg(Theme::TextMuted));
        stepTexts->addWidget(text);
        stepTexts->addWidget(hint);
        row->addWidget(number, 0, Qt::AlignTop);
        row->addLayout(stepTexts, 1);
        onboarding->addLayout(row);
    }
    onboarding->addSpacing(8);
    auto *caseValue = new QLabel(
        lang("案件会保存视频、分析结果与报告。",
             "A case keeps videos, analysis results and reports together."),
        m_onboardingPage);
    caseValue->setWordWrap(true);
    caseValue->setStyleSheet(QStringLiteral("color:%1; font-size:12px;")
                                 .arg(Theme::TextSecond));
    onboarding->addWidget(caseValue);
    onboarding->addStretch(1);
    body->addWidget(m_onboardingPage, 1);

    m_recentPage = new QWidget(this);
    m_recentPage->setObjectName(QStringLiteral("caseOpenRecentPage"));
    m_recentPage->setStyleSheet(QStringLiteral("QWidget#caseOpenRecentPage{background:transparent;}"));
    auto *recentLayout = new QVBoxLayout(m_recentPage);
    recentLayout->setContentsMargins(0, 0, 0, 0);
    recentLayout->setSpacing(10);
    auto *recentHeader = new QHBoxLayout();
    auto *recentTitle = new QLabel(lang("最近案件", "Recent cases"), m_recentPage);
    recentTitle->setStyleSheet(QStringLiteral("font-size:13pt; font-weight:600; color:%1;")
                                   .arg(Theme::TextPrimary));
    recentHeader->addWidget(recentTitle);
    recentHeader->addStretch(1);
    m_searchEdit = new QLineEdit(m_recentPage);
    m_searchEdit->setObjectName(QStringLiteral("caseOpenSearch"));
    m_searchEdit->setPlaceholderText(lang("搜索案件", "Search cases"));
    m_searchEdit->setClearButtonEnabled(true);
    m_searchEdit->setFixedWidth(170);
    recentHeader->addWidget(m_searchEdit);
    recentLayout->addLayout(recentHeader);

    m_recentList = new QListWidget(m_recentPage);
    m_recentList->setObjectName(QStringLiteral("caseOpenRecentList"));
    m_recentList->setSpacing(2);
    m_recentList->setUniformItemSizes(false);
    m_recentList->setToolTip(lang("双击打开案件；目录失效时双击重新定位",
                                  "Double-click to open; double-click a moved case to relocate it"));
    recentLayout->addWidget(m_recentList, 1);
    m_noResultsLabel = new QLabel(lang("没有匹配的案件", "No matching cases"), m_recentPage);
    m_noResultsLabel->setAlignment(Qt::AlignCenter);
    m_noResultsLabel->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    recentLayout->addWidget(m_noResultsLabel);
    m_noResultsLabel->hide();
    body->addWidget(m_recentPage, 1);

    connect(m_primaryButton, &QPushButton::clicked, this, [this]() {
        if (m_recents.isEmpty())
            emit newCaseRequested();
        else
            activateRecent(m_recents.constFirst());
    });
    connect(m_newCaseButton, &QPushButton::clicked, this, &CaseOpenPanel::newCaseRequested);
    connect(m_openCaseButton, &QPushButton::clicked, this, &CaseOpenPanel::browseRequested);
    connect(m_directVideoButton, &QPushButton::clicked, this, &CaseOpenPanel::independentRequested);
    connect(m_recentList, &QListWidget::itemActivated,
            this, &CaseOpenPanel::onItemActivated);
    connect(m_searchEdit, &QLineEdit::textChanged, this, &CaseOpenPanel::applyFilter);
    auto *removeSelected = new QShortcut(QKeySequence(Qt::Key_Delete), m_recentList);
    removeSelected->setContext(Qt::WidgetWithChildrenShortcut);
    connect(removeSelected, &QShortcut::activated, this, [this]() {
        if (QListWidgetItem *item = m_recentList->currentItem())
            removeRecentEntry(item->data(Qt::UserRole).toString());
    });
    auto *escape = new QShortcut(QKeySequence(Qt::Key_Escape), this);
    escape->setContext(Qt::WidgetWithChildrenShortcut);
    connect(escape, &QShortcut::activated, this, &CaseOpenPanel::closeRequested);
}

void CaseOpenPanel::refresh()
{
    populateRecentCases(m_caseManager ? m_caseManager->recentCases() : QStringList());
}

void CaseOpenPanel::populateRecentCases(const QStringList &dirs)
{
    m_recents = dirs;
    m_recents.removeAll(QString());

    const bool hasRecents = !m_recents.isEmpty();
    m_onboardingPage->setVisible(!hasRecents);
    m_recentPage->setVisible(hasRecents);
    m_primaryCaption->setVisible(hasRecents);
    m_newCaseButton->setVisible(hasRecents);
    m_primaryButton->setText(hasRecents
        ? lang("继续上次案件", "Continue last case")
        : lang("新建案件…", "New Case…"));
    m_openCaseButton->setText(hasRecents
        ? lang("打开其他案件…", "Open another case…")
        : lang("打开案件…", "Open Case…"));

    m_primaryCaseDir = hasRecents ? m_recents.constFirst() : QString();
    updatePrimaryCaption();
    if (hasRecents) {
        m_primaryButton->setToolTip(m_primaryCaseDir);
    } else {
        m_primaryButton->setToolTip(lang("创建案件并添加视频", "Create a case and add videos"));
    }

    m_searchEdit->setVisible(m_recents.size() > 6);
    m_searchEdit->clear();
    m_recentList->clear();
    const QIcon dirIcon = m_recentList->style()->standardIcon(QStyle::SP_DirIcon);
    for (const QString &dir : m_recents) {
        const bool exists = QDir(dir).exists();
        auto *item = new QListWidgetItem(m_recentList);
        item->setData(Qt::UserRole, dir);
        item->setData(Qt::UserRole + 1, exists);
        // setItemWidget 后 QListWidget 仍会绘制 item 文本；若这里 setText，
        // 会与行内 QLabel 重绘同一字符串（截图表现为中文错位重叠）。
        // 文本仅存数据角色供搜索/自检，绘制完全交给行内标签。
        const QString displayText = exists ? QDir(dir).dirName()
            : QDir(dir).dirName() + QStringLiteral("\n")
              + lang("目录已移动 · 双击重新定位",
                     "Folder moved · double-click to relocate");
        item->setData(Qt::UserRole + 2, displayText);
        item->setToolTip(dir);
        item->setSizeHint(QSize(0, 56));   // 视觉稿⑤：行高固定 56（含失效行的两行文字）

        auto *row = new QWidget(m_recentList);
        row->setObjectName(QStringLiteral("caseOpenRecentRow"));
        row->setProperty("recentDir", dir);
        // 行容器铺满 item，列表层的 hover 画不到：普通 QWidget 必须显式开启
        // 样式表背景绘制 + 悬停事件，:hover 才会整行变色。
        row->setAttribute(Qt::WA_StyledBackground, true);
        row->setAttribute(Qt::WA_Hover, true);
        row->installEventFilter(this);
        row->setStyleSheet(QStringLiteral(
            "QWidget#caseOpenRecentRow { background:transparent; border-radius:8px; }"
            "QWidget#caseOpenRecentRow:hover { background:%1; }").arg(Theme::BgHover));
        // 行内标签对鼠标事件透明（点了要落到行上），它们的 tooltip 不会触发；
        // 路径被省略后靠行 tooltip 看全路径。
        row->setToolTip(dir);
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 4, 6, 4);
        rowLayout->setSpacing(10);

        auto *icon = new QLabel(row);
        icon->setFixedSize(24, 24);
        icon->setScaledContents(true);
        icon->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        if (!dirIcon.isNull())
            icon->setPixmap(dirIcon.pixmap(24, 24));   // 标准图标，不新增 png 资源
        rowLayout->addWidget(icon, 0, Qt::AlignVCenter);

        auto *rowTexts = new QVBoxLayout();
        rowTexts->setSpacing(1);
        auto *name = new QLabel(QDir(dir).dirName(), row);
        name->setStyleSheet(QStringLiteral("color:%1; font-size:13px; font-weight:600;")
                                .arg(Theme::TextPrimary));
        auto *sub = new QLabel(row);
        if (exists) {
            sub->setStyleSheet(QStringLiteral("color:%1; font-size:11px;")
                                   .arg(Theme::TextMuted));
            sub->setText(dir);   // 次要行 = 目录全路径
        } else {
            sub->setStyleSheet(QStringLiteral("color:%1; font-size:11px;")
                                   .arg(Theme::Danger));
            sub->setText(lang("目录已移动 · 双击重新定位",
                              "Folder moved · double-click to relocate"));
        }
        for (QLabel *label : { name, sub }) {
            label->setAttribute(Qt::WA_TransparentForMouseEvents, true);
            label->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
            label->setToolTip(dir);
            label->setProperty("elideFull", label->text());   // 宽度变化后按此重算省略
            rowTexts->addWidget(label);
        }
        rowLayout->addLayout(rowTexts, 1);

        auto *remove = new QToolButton(row);
        remove->setObjectName(QStringLiteral("caseOpenRemoveRecent"));
        remove->setProperty("recentDir", dir);
        remove->setText(lang("不再显示", "Hide"));
        remove->setToolTip(lang("只从此列表移除；案件目录、视频和分析结果全部保留",
                                "Hide from this list only; the case, videos and analysis stay on disk"));
        remove->setCursor(Qt::PointingHandCursor);
        remove->setFixedSize(64, 26);
        remove->setStyleSheet(QStringLiteral(
            "QToolButton { background:transparent; color:%1; border:1px solid transparent;"
            " border-radius:4px; font-size:12px; padding:0 4px; }"
            "QToolButton:hover { color:%2; background:%3; border-color:%4; }"
            "QToolButton:focus { border-color:%5; }")
            .arg(Theme::TextMuted, Theme::TextPrimary, Theme::BgHover,
                 Theme::Border, Theme::Accent));
        connect(remove, &QToolButton::clicked, this, [this, dir]() {
            removeRecentEntry(dir);
        });
        rowLayout->addWidget(remove, 0, Qt::AlignVCenter);
        m_recentList->setItemWidget(item, row);
    }
    relayoutElidedTexts();
    applyFilter();
}

int CaseOpenPanel::recentTextWidth() const
{
    return qMax(160, width() - kRecentRowChromeWidth);
}

void CaseOpenPanel::updatePrimaryCaption()
{
    if (!m_primaryCaption)
        return;
    if (m_primaryCaseDir.isEmpty()) {
        m_primaryCaption->clear();
        m_primaryCaption->setToolTip(QString());
        return;
    }
    const QString name = QDir(m_primaryCaseDir).dirName();
    m_primaryCaption->setText(QFontMetrics(m_primaryCaption->font())
        .elidedText(name, Qt::ElideMiddle, kActionColumnWidth - 8));
    m_primaryCaption->setToolTip(m_primaryCaseDir);
}

void CaseOpenPanel::relayoutElidedTexts()
{
    // 行内标签是在 populate 时一次性建的，面板尺寸变化（小窗口缩放、居中 resize）
    // 后按 elideFull 里留的原文重算省略，避免出现整段被裁断的路径。
    const int avail = recentTextWidth();
    const auto labels = findChildren<QLabel *>();
    for (QLabel *label : labels) {
        const QVariant full = label->property("elideFull");
        if (!full.isValid())
            continue;
        label->setText(QFontMetrics(label->font())
            .elidedText(full.toString(), Qt::ElideMiddle, avail));
    }
}

void CaseOpenPanel::resizeEvent(QResizeEvent *event)
{
    // 居中仍由主窗 centerCaseOpenPanel() 以主窗口坐标完成（HANDOVER §107.10 坑①），
    // 这里只做「宽度变了 → 省略号重算」，不参与任何几何决策。
    QFrame::resizeEvent(event);
    updatePrimaryCaption();
    relayoutElidedTexts();
}

void CaseOpenPanel::applyFilter()
{
    const QString needle = m_searchEdit->text().trimmed();
    int visible = 0;
    for (int i = 0; i < m_recentList->count(); ++i) {
        QListWidgetItem *item = m_recentList->item(i);
        const QString dir = item->data(Qt::UserRole).toString();
        const bool match = needle.isEmpty()
            || item->data(Qt::UserRole + 2).toString().contains(needle, Qt::CaseInsensitive)
            || dir.contains(needle, Qt::CaseInsensitive);
        item->setHidden(!match);
        if (match)
            ++visible;
    }
    m_recentList->setVisible(visible > 0);
    m_noResultsLabel->setVisible(visible == 0);
}

void CaseOpenPanel::activateRecent(const QString &dir)
{
    if (dir.isEmpty())
        return;
    if (QDir(dir).exists())
        emit openCaseRequested(dir);
    else
        emit relocateCaseRequested(dir);
}

bool CaseOpenPanel::eventFilter(QObject *watched, QEvent *event)
{
    // setItemWidget 的行容器会拦截列表鼠标事件；不能给整行设
    // WA_TransparentForMouseEvents（会连同删除按钮一起屏蔽），
    // 因此只对待命的行容器显式恢复「单击选中 / 双击打开或重新定位」。
    if (watched->objectName() == QStringLiteral("caseOpenRecentRow")) {
        const QEvent::Type type = event->type();
        if (type == QEvent::MouseButtonPress || type == QEvent::MouseButtonDblClick) {
            auto *mouse = static_cast<QMouseEvent *>(event);
            if (mouse->button() == Qt::LeftButton) {
                const QString dir = watched->property("recentDir").toString();
                for (int i = 0; i < m_recentList->count(); ++i) {
                    QListWidgetItem *item = m_recentList->item(i);
                    if (item->data(Qt::UserRole).toString() == dir) {
                        m_recentList->setCurrentItem(item);
                        if (type == QEvent::MouseButtonDblClick)
                            activateRecent(dir);
                        break;
                    }
                }
                return true;
            }
        } else if (type == QEvent::MouseButtonRelease) {
            return true;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void CaseOpenPanel::removeRecentEntry(const QString &dir)
{
    if (dir.isEmpty())
        return;
    if (m_caseManager) {
        m_caseManager->removeRecent(dir);
        refresh();
    } else {
        m_recents.removeAll(dir);
        const QStringList remaining = m_recents;
        populateRecentCases(remaining);
    }
}

void CaseOpenPanel::onItemActivated(QListWidgetItem *item)
{
    if (item)
        activateRecent(item->data(Qt::UserRole).toString());
}
