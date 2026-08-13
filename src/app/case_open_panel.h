/**
 * @file case_open_panel.h
 * @brief 案件打开面板（Blender 式页面内居中，2026-08 人工反馈：打开案件
 *        不做弹窗，主界面内完成）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-14
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 浮于主窗口内容区中央的非模态卡片：最近案件列表 + 浏览/新建入口。
 * 纯展示组件，案件状态一律经 CaseManager 接口读写（R5/R6）。
 */
#pragma once

#include <QFrame>

class QListWidget;
class QListWidgetItem;
class QLabel;
class QPushButton;
class CaseManager;

class CaseOpenPanel : public QFrame
{
    Q_OBJECT

public:
    explicit CaseOpenPanel(CaseManager *cm, QWidget *parent = nullptr);

    /// 重建最近案件列表并显示（父窗口 resize 时由 MainWindow 居中）
    void refresh();

signals:
    void openCaseRequested(const QString &dir);
    void browseRequested();
    void newCaseRequested();
    void closeRequested();

private:
    void onItemActivated(QListWidgetItem *item);

    CaseManager *m_caseManager = nullptr;   // 不持有
    QListWidget *m_recentList = nullptr;
    QLabel *m_emptyLabel = nullptr;
};
