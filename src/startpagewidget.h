/**
 * @file startpagewidget.h
 * @brief 起始页（ui 层）：三钮 + 最近案件 10 条 + 空态引导（v1.3.0 M2 任务11）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计来源：docs/DEVELOPMENT_PLAN_V1.3_CN.md §3-M2 任务11 + §8-10 拍板。
 * 应用启动时显示（可在页面勾选不再自动显示；案件菜单可随时 reopen）。
 * 三钮：新建案件 / 打开案件 / 独立模式（不使用案件，行为同 v1.2.2）。
 */
#pragma once

#include <QDialog>
#include <QString>

class QListWidget;
class QListWidgetItem;
class QCheckBox;
class QPushButton;

class StartPageDialog : public QDialog
{
    Q_OBJECT
public:
    enum Choice { Independent, NewCase, OpenBrowse, OpenRecent };

    explicit StartPageDialog(QWidget *parent = nullptr);

    Choice choice() const { return m_choice; }
    QString recentDir() const { return m_recentDir; }

    /// 启动时是否自动显示（QSettings case/showStartPage，默认 true）
    static bool showAtStartup();
    static void setShowAtStartup(bool on);

private slots:
    void onRecentDoubleClicked(QListWidgetItem *item);

private:
    QListWidget *m_recentList = nullptr;
    QCheckBox *m_dontShowCheck = nullptr;
    QPushButton *m_btnNew = nullptr;
    QPushButton *m_btnOpen = nullptr;
    QPushButton *m_btnIndependent = nullptr;
    Choice m_choice = Independent;
    QString m_recentDir;
};
