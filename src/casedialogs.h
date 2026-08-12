/**
 * @file casedialogs.h
 * @brief 案件对话框（ui 层）：新建案件 + 案件属性（v1.3.0 M2 任务11）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计来源：docs/DEVELOPMENT_PLAN_V1.3_CN.md §3-M2 任务11 + §8 拍板。
 * 新建：必填校验（名称/调查员/单位/案发时间/城市/区县），编号自动生成
 * 预览（YYYYMMDD-城市区县-x，随输入实时刷新）。属性：可改名/人/单位/
 * 地址/备注；编号/案发时间/地点创建后固定（编号是目录索引源）。
 * 批量重定位/校验报告/导出对话框属 M3（任务12/13）。
 */
#pragma once

#include <QDialog>
#include "domain/case_model.h"

class QLineEdit;
class QDateEdit;
class QPlainTextEdit;
class QLabel;
class QDialogButtonBox;
class CaseManager;

/// 新建案件对话框：收集 CaseMeta 必填字段 + 编号预览
class NewCaseDialog : public QDialog
{
    Q_OBJECT
public:
    /// rootDir：编号预览扫描用（CaseModel::generateCaseNo）
    explicit NewCaseDialog(const QString &rootDir, QWidget *parent = nullptr);

    CaseMeta meta() const;   ///< 仅当 accepted 后有意义（编号已生成入 caseNo）

private slots:
    void refreshPreview();
    void validate();

private:
    QString m_rootDir;
    QLineEdit *m_title = nullptr;
    QLineEdit *m_investigator = nullptr;
    QLineEdit *m_unit = nullptr;
    QDateEdit *m_incidentDate = nullptr;
    QLineEdit *m_city = nullptr;
    QLineEdit *m_district = nullptr;
    QLineEdit *m_locationDetail = nullptr;
    QPlainTextEdit *m_description = nullptr;
    QLabel *m_caseNoPreview = nullptr;
    QLabel *m_hint = nullptr;
    QDialogButtonBox *m_buttons = nullptr;
};

/// 案件属性对话框：查看全部字段，编辑可改字段
class CasePropertiesDialog : public QDialog
{
    Q_OBJECT
public:
    explicit CasePropertiesDialog(CaseManager *cm, QWidget *parent = nullptr);

private:
    CaseManager *m_caseManager = nullptr;
    QLineEdit *m_title = nullptr;
    QLineEdit *m_investigator = nullptr;
    QLineEdit *m_unit = nullptr;
    QLineEdit *m_locationDetail = nullptr;
    QPlainTextEdit *m_description = nullptr;
};
