/**
 * @file case_open_panel.cpp
 * @brief 案件工作入口实现（2026-11：首访引导 / 回访续作双态布局）
 */
#include "case_open_panel.h"

#include <QDir>
#include <QFontMetrics>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
#include <QShortcut>
#include <QSizePolicy>
#include <QToolButton>
#include <QVBoxLayout>

#include "app/case_manager.h"
#include "i18n.h"
#include "theme.h"

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
    resize(780, 500);
    setStyleSheet(QStringLiteral(
        "QFrame#caseOpenPanel { background:%1; border:1px solid %2;"
        " border-radius:12px; }"
        "QLabel { border:none; background:transparent; }"
        "QLineEdit { background:%3; color:%4; border:1px solid %2;"
        " border-radius:6px; padding:5px 9px; selection-background-color:%5; }"
        "QLineEdit:focus { border-color:%5; }"
        "QListWidget { background:transparent; border:none; outline:none; }"
        "QListWidget::item { padding:7px 10px; border-radius:5px; }"
        "QListWidget::item:selected { background:%6; color:%4; }"
        "QListWidget::item:hover { background:%6; }")
        .arg(Theme::BgCard, Theme::Border, Theme::BgApp, Theme::TextPrimary,
             Theme::Accent, Theme::BgHover));

    auto *root = new QVBoxLayout(this);
    root->setContentsMargins(28, 22, 28, 24);
    root->setSpacing(22);

    // 品牌退后：页面任务是「开始 / 继续工作」，不是展示宣传标题。
    auto *header = new QHBoxLayout();
    header->setSpacing(12);
    auto *brandBox = new QVBoxLayout();
    brandBox->setSpacing(3);
    auto *title = new QLabel(QStringLiteral("Lumen Arc"), this);
    title->setStyleSheet(QStringLiteral(
        "font-size:20px; font-weight:600; color:%1;").arg(Theme::TextPrimary));
    auto *subtitle = new QLabel(lang("火灾调查视频分析", "Fire-investigation video analysis"), this);
    subtitle->setStyleSheet(QStringLiteral("color:%1; font-size:12px;")
                                .arg(Theme::TextSecond));
    brandBox->addWidget(title);
    brandBox->addWidget(subtitle);
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
        "QPushButton { background:%1; color:%2; font-weight:600;"
        " border:1px solid %1; border-radius:7px; font-size:14px; }"
        "QPushButton:hover { background:%3; border-color:%3; }"
        "QPushButton:pressed { background:%1; }"
        "QPushButton:focus { border-color:%2; }")
        .arg(Theme::Accent, Theme::AccentOnDark, Theme::AccentHover);
    const QString secondaryStyle = QStringLiteral(
        "QPushButton { background:transparent; color:%1; border:1px solid %2;"
        " border-radius:7px; font-size:13px; }"
        "QPushButton:hover { border-color:%3; color:%3; background:%4; }"
        "QPushButton:focus { border-color:%3; }")
        .arg(Theme::TextPrimary, Theme::Border, Theme::Accent, Theme::BgHover);

    m_primaryButton = new QPushButton(this);
    m_primaryButton->setObjectName(QStringLiteral("caseOpenPrimary"));
    m_primaryButton->setMinimumHeight(46);
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
    m_newCaseButton->setMinimumHeight(38);
    m_newCaseButton->setCursor(Qt::PointingHandCursor);
    m_newCaseButton->setStyleSheet(secondaryStyle);
    actions->addWidget(m_newCaseButton);

    m_openCaseButton = new QPushButton(this);
    m_openCaseButton->setObjectName(QStringLiteral("caseOpenBrowse"));
    m_openCaseButton->setMinimumHeight(38);
    m_openCaseButton->setCursor(Qt::PointingHandCursor);
    m_openCaseButton->setStyleSheet(secondaryStyle);
    actions->addWidget(m_openCaseButton);

    m_directVideoButton = new QPushButton(
        lang("直接打开视频", "Open a video directly"), this);
    m_directVideoButton->setObjectName(QStringLiteral("caseOpenDirectVideo"));
    m_directVideoButton->setMinimumHeight(34);
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
    onboarding->setSpacing(16);
    auto *firstUse = new QLabel(lang("第一次使用？", "First time here?"), m_onboardingPage);
    firstUse->setStyleSheet(QStringLiteral("font-size:16px; font-weight:600; color:%1;")
                                .arg(Theme::TextPrimary));
    onboarding->addWidget(firstUse);
    onboarding->addSpacing(5);

    const QStringList steps = {
        lang("新建案件", "Create a case"),
        lang("添加视频", "Add videos"),
        lang("开始分析", "Start analysis")
    };
    for (int i = 0; i < steps.size(); ++i) {
        auto *row = new QHBoxLayout();
        row->setSpacing(12);
        auto *number = new QLabel(QString::number(i + 1), m_onboardingPage);
        number->setFixedSize(24, 24);
        number->setAlignment(Qt::AlignCenter);
        number->setStyleSheet(QStringLiteral(
            "color:%1; background:%2; border:1px solid %3; border-radius:5px;"
            " font-weight:600;").arg(Theme::Accent, Theme::BgApp, Theme::Border));
        auto *text = new QLabel(steps[i], m_onboardingPage);
        text->setStyleSheet(QStringLiteral("color:%1; font-size:13px;")
                                .arg(Theme::TextPrimary));
        row->addWidget(number);
        row->addWidget(text);
        row->addStretch(1);
        onboarding->addLayout(row);
    }
    onboarding->addSpacing(7);
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
    recentTitle->setStyleSheet(QStringLiteral("font-size:14px; font-weight:600; color:%1;")
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

    if (hasRecents) {
        const QString name = QDir(m_recents.constFirst()).dirName();
        m_primaryCaption->setText(QFontMetrics(m_primaryCaption->font()).elidedText(
            name, Qt::ElideMiddle, m_primaryButton->width() - 8));
        m_primaryCaption->setToolTip(m_recents.constFirst());
        m_primaryButton->setToolTip(m_recents.constFirst());
    } else {
        m_primaryCaption->clear();
        m_primaryButton->setToolTip(lang("创建案件并添加视频", "Create a case and add videos"));
    }

    m_searchEdit->setVisible(m_recents.size() > 6);
    m_searchEdit->clear();
    m_recentList->clear();
    for (const QString &dir : m_recents) {
        const bool exists = QDir(dir).exists();
        auto *item = new QListWidgetItem(m_recentList);
        item->setData(Qt::UserRole, dir);
        item->setData(Qt::UserRole + 1, exists);
        item->setText(exists ? QDir(dir).dirName()
                             : QDir(dir).dirName() + QStringLiteral("\n")
                               + lang("目录已移动 · 双击重新定位",
                                      "Folder moved · double-click to relocate"));
        item->setToolTip(dir);
        item->setSizeHint(QSize(0, exists ? 38 : 54));

        auto *row = new QWidget(m_recentList);
        row->setObjectName(QStringLiteral("caseOpenRecentRow"));
        row->setStyleSheet(QStringLiteral("QWidget#caseOpenRecentRow{background:transparent;}"));
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(10, 2, 4, 2);
        rowLayout->setSpacing(6);
        auto *name = new QLabel(item->text(), row);
        name->setAttribute(Qt::WA_TransparentForMouseEvents, true);
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        name->setToolTip(dir);
        name->setStyleSheet(QStringLiteral("color:%1; font-size:12px;")
                                .arg(exists ? Theme::TextPrimary : Theme::TextSecond));
        rowLayout->addWidget(name, 1);

        auto *remove = new QToolButton(row);
        remove->setObjectName(QStringLiteral("caseOpenRemoveRecent"));
        remove->setProperty("recentDir", dir);
        remove->setText(lang("删除", "Remove"));
        remove->setToolTip(lang("仅从最近案件移除，不删除文件",
                                "Remove from recent cases only; no files are deleted"));
        remove->setCursor(Qt::PointingHandCursor);
        remove->setFixedSize(44, 26);
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
    applyFilter();
}

void CaseOpenPanel::applyFilter()
{
    const QString needle = m_searchEdit->text().trimmed();
    int visible = 0;
    for (int i = 0; i < m_recentList->count(); ++i) {
        QListWidgetItem *item = m_recentList->item(i);
        const QString dir = item->data(Qt::UserRole).toString();
        const bool match = needle.isEmpty()
            || item->text().contains(needle, Qt::CaseInsensitive)
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
