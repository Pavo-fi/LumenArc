/**
 * @file aboutdialog.h
 * @brief 关于对话框，显示版本信息、版权和项目链接
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include <QDialog>

class QLabel;
class QPushButton;

class AboutDialog : public QDialog
{
    Q_OBJECT

public:
    explicit AboutDialog(QWidget *parent = nullptr);
};
