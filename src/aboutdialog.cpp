/**
 * @file aboutdialog.cpp
 * @brief 关于对话框实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "aboutdialog.h"
#include "i18n.h"
#include "theme.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QDesktopServices>
#include <QUrl>

AboutDialog::AboutDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(lang("关于", "About"));
    setFixedSize(420, 300);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(0);

    // Title
    auto *titleLabel = new QLabel("追光者 Lumen Arc", this);
    titleLabel->setStyleSheet("font-size: 18px; font-weight: bold; color: " + Theme::TextPrimary + ";");
    titleLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(titleLabel);
    layout->addSpacing(4);

    // Version
    auto *versionLabel = new QLabel(lang("版本 v1.12.4", "Version v1.12.4"), this);
    versionLabel->setStyleSheet("font-size: 12px; color: " + Theme::TextSecond + ";");
    versionLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(versionLabel);
    layout->addSpacing(16);

    // System name
    auto *sysLabel = new QLabel(lang("火灾调查视频分析工具", "Fire Investigation Video Analysis Tool"), this);
    sysLabel->setStyleSheet("font-size: 13px; color: " + Theme::TextPrimary + ";");
    sysLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(sysLabel);
    layout->addSpacing(20);

    // Copyright
    auto *copyrightLabel = new QLabel(lang("版权所有 2026 Huang Jingyun/Liu xinghua/Huang Wenhua", "Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua"), this);
    copyrightLabel->setStyleSheet("font-size: 11px; color: " + Theme::TextMuted + ";");
    copyrightLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(copyrightLabel);
    layout->addSpacing(4);

    // License
    auto *licenseLabel = new QLabel(lang(
        "本项目由广东省火调技术中心发布及维护，采用Apache许可证2.0版进行授权。",
        "Published and maintained by Guangdong Fire Investigation Technology Center. Licensed under Apache 2.0."), this);
    licenseLabel->setStyleSheet("font-size: 11px; color: " + Theme::TextMuted + ";");
    licenseLabel->setAlignment(Qt::AlignCenter);
    layout->addWidget(licenseLabel);
    layout->addSpacing(20);

    // GitHub link
    auto *githubBtn = new QPushButton(lang("GitHub 项目地址", "GitHub Repository"), this);
    githubBtn->setStyleSheet(
        "QPushButton { color: " + Theme::Accent + "; border: none; font-size: 12px; "
        "text-decoration: underline; background: transparent; }"
        "QPushButton:hover { color: " + Theme::AccentHover + "; }"
    );
    connect(githubBtn, &QPushButton::clicked, []() {
        QDesktopServices::openUrl(QUrl("https://github.com/Pavo-fi/LumenArc"));
    });
    layout->addWidget(githubBtn, 0, Qt::AlignCenter);

    layout->addStretch();

    // Close button
    auto *closeBtn = new QPushButton(lang("关闭", "Close"), this);
    closeBtn->setFixedWidth(80);
    closeBtn->setFixedHeight(28);
    closeBtn->setStyleSheet(
        "QPushButton { border: none; border-radius: 6px; background: " + Theme::BgCard + "; color: " + Theme::TextPrimary + "; }"
        "QPushButton:hover { background: " + Theme::BgHover + "; }"
    );
    connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
    auto *btnLayout = new QHBoxLayout();
    btnLayout->addStretch();
    btnLayout->addWidget(closeBtn);
    btnLayout->addStretch();
    layout->addLayout(btnLayout);
}
