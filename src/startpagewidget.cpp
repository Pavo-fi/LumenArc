/**
 * @file startpagewidget.cpp
 * @brief 起始页实现：三钮 + 最近案件 + 空态引导文案
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "startpagewidget.h"

#include <QCheckBox>
#include <QDir>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QListWidgetItem>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include "app/case_manager.h"
#include "i18n.h"
#include "theme.h"

bool StartPageDialog::showAtStartup()
{
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    return s.value(QStringLiteral("case/showStartPage"), true).toBool();
}

void StartPageDialog::setShowAtStartup(bool on)
{
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    s.setValue(QStringLiteral("case/showStartPage"), on);
}

StartPageDialog::StartPageDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(lang("追光者 Lumen Arc — 起始页", "Lumen Arc — Start"));
    setMinimumSize(520, 420);

    auto *lay = new QVBoxLayout(this);
    lay->setContentsMargins(24, 20, 24, 16);
    lay->setSpacing(10);

    auto *title = new QLabel(lang("追光者 Lumen Arc", "Lumen Arc"), this);
    title->setStyleSheet(QStringLiteral(
        "font-size:22px; font-weight:bold; color:%1;").arg(Theme::TextPrimary));
    lay->addWidget(title);
    auto *subtitle = new QLabel(
        lang("火灾调查视频分析工具 — 案件 = 证据容器",
             "Fire-investigation video analysis — a case is the evidence container"),
        this);
    subtitle->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    lay->addWidget(subtitle);
    lay->addSpacing(6);

    // 三钮：新建 / 打开 / 独立模式（拍板§8-10）
    auto *btnRow = new QHBoxLayout();
    m_btnNew = new QPushButton(lang("新建案件…", "New Case…"), this);
    m_btnOpen = new QPushButton(lang("打开案件…", "Open Case…"), this);
    m_btnIndependent = new QPushButton(
        lang("独立模式\n（不使用案件）", "Independent\n(no case)"), this);
    for (auto *b : {m_btnNew, m_btnOpen, m_btnIndependent}) {
        b->setMinimumHeight(56);
        b->setMinimumWidth(130);
        btnRow->addWidget(b);
    }
    m_btnNew->setDefault(true);
    lay->addLayout(btnRow);

    // 最近案件（新→旧最多 10 条，双击打开）
    auto *recentTitle = new QLabel(lang("最近案件", "Recent cases"), this);
    recentTitle->setStyleSheet(QStringLiteral(
        "font-weight:bold; color:%1;").arg(Theme::TextPrimary));
    lay->addWidget(recentTitle);

    m_recentList = new QListWidget(this);
    m_recentList->setStyleSheet(QStringLiteral(
        "QListWidget { background:%1; border:1px solid %2; border-radius:6px; }")
        .arg(Theme::BgCard, Theme::Border));
    lay->addWidget(m_recentList, 1);

    CaseManager probe;   // recentCases 为无状态 QSettings 读取
    const QStringList recents = probe.recentCases();
    for (const QString &dir : recents) {
        auto *it = new QListWidgetItem(QFileInfo(dir).fileName(), m_recentList);
        it->setData(Qt::UserRole, dir);
        it->setToolTip(dir);
        if (!QDir(dir).exists())
            it->setForeground(QColor(Theme::TextMuted));   // 目录已搬走
    }
    if (recents.isEmpty()) {
        // 空态引导文案（拍板§8-10）
        auto *it = new QListWidgetItem(
            lang("暂无最近案件。\n点击「新建案件」开始：视频、ROI 分析、校时证据、\n"
                 "前处理成果将统一入案管理，可校验完整性、打包移交。\n"
                 "或选「独立模式」直接进入（与旧版一致）。",
                 "No recent cases.\nClick “New Case” to start: videos, ROI analysis,\n"
                 "calibration evidence and preprocessing results are managed in one\n"
                 "place, with integrity verification and handover packaging.\n"
                 "Or choose “Independent” to enter directly (same as before)."),
            m_recentList);
        it->setFlags(Qt::NoItemFlags);
        it->setForeground(QColor(Theme::TextMuted));
    }
    connect(m_recentList, &QListWidget::itemDoubleClicked,
            this, &StartPageDialog::onRecentDoubleClicked);

    m_dontShowCheck = new QCheckBox(
        lang("启动时不再显示（案件菜单可随时打开起始页）",
             "Don't show at startup (reopen anytime from the Case menu)"), this);
    m_dontShowCheck->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextMuted));
    lay->addWidget(m_dontShowCheck);

    connect(m_btnNew, &QPushButton::clicked, this, [this]() {
        m_choice = NewCase;
        setShowAtStartup(!m_dontShowCheck->isChecked());
        accept();
    });
    connect(m_btnOpen, &QPushButton::clicked, this, [this]() {
        m_choice = OpenBrowse;
        setShowAtStartup(!m_dontShowCheck->isChecked());
        accept();
    });
    connect(m_btnIndependent, &QPushButton::clicked, this, [this]() {
        m_choice = Independent;
        setShowAtStartup(!m_dontShowCheck->isChecked());
        accept();
    });
}

void StartPageDialog::onRecentDoubleClicked(QListWidgetItem *item)
{
    if (!item)
        return;
    const QString dir = item->data(Qt::UserRole).toString();
    if (dir.isEmpty() || !QDir(dir).exists())
        return;
    m_recentDir = dir;
    m_choice = OpenRecent;
    setShowAtStartup(!m_dontShowCheck->isChecked());
    accept();
}
