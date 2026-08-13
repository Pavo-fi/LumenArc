/**
 * @file case_open_panel.cpp
 * @brief 案件欢迎面板实现（2026-08 重新设计：品牌头部 + 三枚大按钮 +
 *        最近案件 + 空态引导；页面内居中非模态，启动画面结束后出现）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-14
 * @version 1.1
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
        " border-radius:12px; }"
        "QLabel { border:none; background:transparent; }"
        "QListWidget { background:%3; border:1px solid %4; border-radius:8px;"
        " font-size:13px; }"
        "QListWidget::item { padding:7px 10px; border-radius:4px; }"
        "QListWidget::item:selected { background:%5; color:%6; }"
        "QListWidget::item:hover { background:%7; }")
        .arg(Theme::BgCard, Theme::Border, Theme::BgCard, Theme::Border,
             Theme::Accent, Theme::AccentOnDark, Theme::BgHover));
    setFixedSize(600, 480);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(28, 24, 28, 20);
    lay->setSpacing(10);

    // ---- 品牌头部 ----
    auto *title = new QLabel(lang("追光者 Lumen Arc", "Lumen Arc"), this);
    title->setStyleSheet(QStringLiteral(
        "font-size:26px; font-weight:bold; color:%1;")
        .arg(Theme::TextPrimary));
    lay->addWidget(title);
    auto *subtitle = new QLabel(
        lang("火灾调查视频分析工具 — 案件 = 证据容器",
             "Fire-investigation video analysis — a case is the evidence container"),
        this);
    subtitle->setStyleSheet(QStringLiteral(
        "color:%1; font-size:13px;").arg(Theme::TextSecond));
    subtitle->setWordWrap(true);
    lay->addWidget(subtitle);
    lay->addSpacing(8);

    // ---- 三枚大按钮：新建 / 打开 / 独立（拍板§8-10）----
    auto *btnRow = new QHBoxLayout();
    btnRow->setSpacing(14);
    auto *btnNew = new QPushButton(lang("新建案件…", "New Case…"), this);
    auto *btnOpen = new QPushButton(lang("打开案件…", "Open Case…"), this);
    auto *btnIndependent = new QPushButton(
        lang("独立模式\n（不使用案件）", "Independent\n(no case)"), this);
    for (auto *b : {btnNew, btnOpen, btnIndependent}) {
        b->setMinimumHeight(56);
        b->setMinimumWidth(150);
        b->setCursor(Qt::PointingHandCursor);
    }
    // 主入口（新建）用品牌金强调；其余用卡片灰
    btnNew->setStyleSheet(QStringLiteral(
        "QPushButton { background:%1; color:%2; font-weight:bold;"
        " border:none; border-radius:8px; font-size:14px; }"
        "QPushButton:hover { background:%3; }")
        .arg(Theme::Accent, Theme::AccentOnDark, Theme::AccentHover));
    for (auto *b : {btnOpen, btnIndependent}) {
        b->setStyleSheet(QStringLiteral(
            "QPushButton { background:%4; color:%1; border:1px solid %2;"
            " border-radius:8px; font-size:14px; }"
            "QPushButton:hover { border-color:%3; color:%3; }")
            .arg(Theme::TextPrimary, Theme::Border, Theme::Accent,
                 Theme::BgHover));
    }
    btnRow->addWidget(btnNew);
    btnRow->addWidget(btnOpen);
    btnRow->addWidget(btnIndependent);
    btnRow->addStretch(1);
    lay->addLayout(btnRow);
    lay->addSpacing(6);

    // ---- 最近案件 ----
    auto *recentTitle = new QLabel(lang("最近案件（双击打开）", "Recent cases (double-click)"),
                                   this);
    recentTitle->setStyleSheet(QStringLiteral(
        "font-weight:bold; color:%1;").arg(Theme::TextPrimary));
    lay->addWidget(recentTitle);

    m_recentList = new QListWidget(this);
    lay->addWidget(m_recentList, 1);

    m_emptyLabel = new QLabel(
        lang("暂无最近案件。\n点击「新建案件」开始：视频、ROI 分析、校时证据、前处理成果将统一入案管理，"
             "可校验完整性、打包移交。\n或选「独立模式」直接进入（与旧版一致）。",
             "No recent cases.\nClick “New Case” to start: videos, ROI analysis, calibration "
             "evidence and preprocessing results are managed in one place, with integrity "
             "verification and handover packaging.\nOr choose “Independent” to enter directly."),
        this);
    m_emptyLabel->setAlignment(Qt::AlignCenter);
    m_emptyLabel->setWordWrap(true);
    m_emptyLabel->setStyleSheet(QStringLiteral(
        "color:%1; font-size:13px;").arg(Theme::TextMuted));
    m_emptyLabel->setVisible(false);
    lay->addWidget(m_emptyLabel);

    // ---- 底部：不再显示 + 关闭 ----
    auto *foot = new QHBoxLayout();
    m_dontShowCheck = new QCheckBox(
        lang("启动时不再显示（案件菜单可随时打开）",
             "Don't show at startup (reopen from Case menu)"), this);
    m_dontShowCheck->setStyleSheet(QStringLiteral(
        "color:%1;").arg(Theme::TextMuted));
    m_dontShowCheck->setChecked(!showStartupPanel());
    connect(m_dontShowCheck, &QCheckBox::toggled, this,
            [](bool off) { setShowStartupPanel(!off); });
    auto *btnClose = new QPushButton(lang("✕ 关闭", "✕ Close"), this);
    btnClose->setStyleSheet(QStringLiteral(
        "QPushButton { background:transparent; color:%1; border:none; }"
        "QPushButton:hover { color:%2; }")
        .arg(Theme::TextSecond, Theme::TextPrimary));
    foot->addWidget(m_dontShowCheck);
    foot->addStretch(1);
    foot->addWidget(btnClose);
    lay->addLayout(foot);

    connect(btnNew, &QPushButton::clicked, this, [this]() {
        emit newCaseRequested();
    });
    connect(btnOpen, &QPushButton::clicked, this, [this]() {
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
        if (!QDir(dir).exists())
            it->setForeground(QColor(Theme::TextMuted));   // 目录已搬走
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
