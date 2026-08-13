/**
 * @file case_open_panel.cpp
 * @brief 案件打开面板实现：最近案件 + 浏览 + 新建（页面内居中，非弹窗）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-14
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "case_open_panel.h"

#include <QCheckBox>
#include <QDir>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSettings>
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
    setStyleSheet(QStringLiteral(
        "QFrame#caseOpenPanel { background:%1; border:1px solid %2;"
        " border-radius:10px; }"
        "QLabel { border:none; background:transparent; }"
        "QListWidget { background:%3; border:1px solid %4; border-radius:6px;"
        " font-size:13px; }"
        "QListWidget::item { padding:6px 8px; }"
        "QListWidget::item:selected { background:%5; color:%6; }")
        .arg(Theme::BgCard, Theme::Border, Theme::BgCard, Theme::Border,
             Theme::Accent, Theme::AccentOnDark));
    setFixedSize(560, 440);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(20, 18, 20, 18);
    lay->setSpacing(10);

    auto *title = new QLabel(lang("追光者 Lumen Arc", "Lumen Arc"), this);
    title->setStyleSheet(QStringLiteral(
        "font-size:20px; font-weight:bold; color:%1;").arg(Theme::TextPrimary));
    lay->addWidget(title);

    auto *sub = new QLabel(lang("案件 = 证据容器｜最近打开的案件（双击打开）",
                                "A case is the evidence container — recent cases "
                                "(double-click to open)"), this);
    sub->setStyleSheet(QStringLiteral(
        "color:%1;").arg(Theme::TextSecond));
    sub->setWordWrap(true);
    lay->addWidget(sub);

    m_recentList = new QListWidget(this);
    m_recentList->setWordWrap(true);
    lay->addWidget(m_recentList, 1);

    m_emptyLabel = new QLabel(lang("暂无最近案件", "No recent cases"), this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setStyleSheet(QStringLiteral(
        "color:%1; font-size:14px;").arg(Theme::TextMuted));
    m_emptyLabel->setVisible(false);
    lay->addWidget(m_emptyLabel);

    auto *row = new QHBoxLayout();
    row->addStretch(1);
    auto *btnNew = new QPushButton(lang("新建案件…", "New case…"), this);
    auto *btnBrowse = new QPushButton(lang("浏览案件目录…", "Browse…"), this);
    auto *btnIndependent = new QPushButton(
        lang("独立模式", "Independent"), this);
    btnIndependent->setToolTip(lang("不使用案件，直接进入（与旧版一致）",
                                    "No case; enter directly (as before)"));
    auto *btnClose = new QPushButton(lang("✕ 关闭", "✕ Close"), this);
    btnClose->setStyleSheet(QStringLiteral(
        "QPushButton { background:transparent; color:%1; border:none; }"
        "QPushButton:hover { color:%2; }")
        .arg(Theme::TextSecond, Theme::TextPrimary));
    row->addWidget(btnNew);
    row->addWidget(btnBrowse);
    row->addWidget(btnIndependent);
    row->addWidget(btnClose);
    lay->addLayout(row);

    // 启动时不再显示（2026-08：迁自模态起始页；面板随时可从案件菜单打开）
    m_dontShowCheck = new QCheckBox(
        lang("启动时不再显示（案件菜单可随时打开）",
             "Don't show at startup (reopen from Case menu)"), this);
    m_dontShowCheck->setStyleSheet(QStringLiteral(
        "color:%1;").arg(Theme::TextMuted));
    m_dontShowCheck->setChecked(!showStartupPanel());
    connect(m_dontShowCheck, &QCheckBox::toggled, this,
            [](bool off) { setShowStartupPanel(!off); });
    lay->addWidget(m_dontShowCheck);

    connect(btnNew, &QPushButton::clicked, this, [this]() {
        emit newCaseRequested();
    });
    connect(btnBrowse, &QPushButton::clicked, this, [this]() {
        emit browseRequested();
    });
    connect(btnIndependent, &QPushButton::clicked, this, [this]() {
        emit independentRequested();
    });
    connect(btnClose, &QPushButton::clicked, this, [this]() {
        emit closeRequested();
    });
    connect(m_recentList, &QListWidget::itemActivated,
            this, &CaseOpenPanel::onItemActivated);
    connect(m_recentList, &QListWidget::itemDoubleClicked,
            this, &CaseOpenPanel::onItemActivated);
}

void CaseOpenPanel::refresh()
{
    m_recentList->clear();
    const QStringList recents = m_caseManager
        ? m_caseManager->recentCases() : QStringList();
    for (const QString &dir : recents) {
        if (dir.isEmpty())
            continue;
        auto *it = new QListWidgetItem(
            QDir(dir).dirName() + QStringLiteral("\n") + dir,
            m_recentList);
        it->setData(Qt::UserRole, dir);
        it->setToolTip(dir);
    }
    m_emptyLabel->setVisible(recents.isEmpty());
    m_recentList->setVisible(!recents.isEmpty());
}

void CaseOpenPanel::onItemActivated(QListWidgetItem *item)
{
    if (!item)
        return;
    const QString dir = item->data(Qt::UserRole).toString();
    if (!dir.isEmpty())
        emit openCaseRequested(dir);
}
