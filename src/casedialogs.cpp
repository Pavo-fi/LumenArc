/**
 * @file casedialogs.cpp
 * @brief 案件对话框实现：新建（编号预览/必填校验）+ 属性（可改字段编辑）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "casedialogs.h"

#include <QDateEdit>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

#include "app/case_manager.h"
#include "i18n.h"
#include "theme.h"

// ---------------------------------------------------------------------------
// NewCaseDialog
// ---------------------------------------------------------------------------
NewCaseDialog::NewCaseDialog(const QString &rootDir, QWidget *parent)
    : QDialog(parent)
    , m_rootDir(rootDir)
{
    setWindowTitle(lang("新建案件", "New Case"));
    setMinimumWidth(480);
    auto *lay = new QVBoxLayout(this);

    auto *form = new QFormLayout();
    m_title = new QLineEdit(this);
    m_title->setPlaceholderText(lang("如：xx厂房火灾", "e.g. Warehouse fire"));
    m_investigator = new QLineEdit(this);
    m_unit = new QLineEdit(this);
    m_incidentDate = new QDateEdit(QDate::currentDate(), this);
    m_incidentDate->setCalendarPopup(true);
    m_incidentDate->setDisplayFormat(QStringLiteral("yyyy-MM-dd"));
    m_city = new QLineEdit(this);
    m_city->setPlaceholderText(lang("如：广州", "e.g. Guangzhou"));
    m_district = new QLineEdit(this);
    m_district->setPlaceholderText(lang("如：天河", "e.g. Tianhe"));
    m_locationDetail = new QLineEdit(this);
    m_description = new QPlainTextEdit(this);
    m_description->setMaximumHeight(72);

    const QString req = QStringLiteral(" *");
    form->addRow(lang("案件名称", "Case title") + req, m_title);
    form->addRow(lang("调查员", "Investigator") + req, m_investigator);
    form->addRow(lang("单位", "Unit") + req, m_unit);
    form->addRow(lang("案发日期", "Incident date") + req, m_incidentDate);
    form->addRow(lang("案发城市", "City") + req, m_city);
    form->addRow(lang("案发区县", "District") + req, m_district);
    form->addRow(lang("详细地址（选填）", "Address (optional)"), m_locationDetail);
    form->addRow(lang("备注（选填）", "Notes (optional)"), m_description);
    lay->addLayout(form);

    m_caseNoPreview = new QLabel(this);
    m_caseNoPreview->setStyleSheet(QStringLiteral(
        "color:%1; font-weight:bold; padding:4px 0;").arg(Theme::Accent));
    lay->addWidget(m_caseNoPreview);

    m_hint = new QLabel(lang("* 为必填项；编号创建后固定，案件目录 = 编号-名称",
                             "* required; the case number is fixed once created"),
                        this);
    m_hint->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextMuted));
    lay->addWidget(m_hint);

    m_buttons = new QDialogButtonBox(
        QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    m_buttons->button(QDialogButtonBox::Ok)->setText(lang("创建", "Create"));
    m_buttons->button(QDialogButtonBox::Cancel)->setText(lang("取消", "Cancel"));
    lay->addWidget(m_buttons);
    connect(m_buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(m_buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    // 编号预览随输入实时刷新 + 必填校验
    auto refresh = [this]() { refreshPreview(); validate(); };
    connect(m_title, &QLineEdit::textChanged, this, refresh);
    connect(m_investigator, &QLineEdit::textChanged, this, refresh);
    connect(m_unit, &QLineEdit::textChanged, this, refresh);
    connect(m_city, &QLineEdit::textChanged, this, refresh);
    connect(m_district, &QLineEdit::textChanged, this, refresh);
    connect(m_incidentDate, &QDateEdit::dateChanged, this, refresh);
    refresh();
}

void NewCaseDialog::refreshPreview()
{
    // 编号自动生成预览：YYYYMMDD-城市区县-x（城市/区县内部去空白标准化）
    const QString city = m_city->text().trimmed();
    const QString district = m_district->text().trimmed();
    if (city.isEmpty() || district.isEmpty()) {
        m_caseNoPreview->setText(
            lang("编号：（填城市与区县后自动生成）",
                 "Number: (auto-generated once city & district are set)"));
        return;
    }
    const qint64 ms = m_incidentDate->dateTime().toMSecsSinceEpoch();
    m_caseNoPreview->setText(lang("编号：%1", "Number: %1")
        .arg(CaseModel::generateCaseNo(ms, city, district, m_rootDir)));
}

void NewCaseDialog::validate()
{
    const bool ok = !m_title->text().trimmed().isEmpty()
        && !m_investigator->text().trimmed().isEmpty()
        && !m_unit->text().trimmed().isEmpty()
        && !m_city->text().trimmed().isEmpty()
        && !m_district->text().trimmed().isEmpty()
        && m_incidentDate->date().isValid();
    m_buttons->button(QDialogButtonBox::Ok)->setEnabled(ok);
}

CaseMeta NewCaseDialog::meta() const
{
    CaseMeta m;
    m.title = m_title->text().trimmed();
    m.investigator = m_investigator->text().trimmed();
    m.unit = m_unit->text().trimmed();
    m.incidentTimeMs = m_incidentDate->dateTime().toMSecsSinceEpoch();
    m.city = m_city->text().trimmed();
    m.district = m_district->text().trimmed();
    m.locationDetail = m_locationDetail->text().trimmed();
    m.description = m_description->toPlainText();
    m.caseNo = CaseModel::generateCaseNo(m.incidentTimeMs, m.city,
                                         m.district, m_rootDir);
    return m;
}

// ---------------------------------------------------------------------------
// CasePropertiesDialog
// ---------------------------------------------------------------------------
CasePropertiesDialog::CasePropertiesDialog(CaseManager *cm, QWidget *parent)
    : QDialog(parent)
    , m_caseManager(cm)
{
    setWindowTitle(lang("案件属性", "Case Properties"));
    setMinimumWidth(480);
    auto *lay = new QVBoxLayout(this);

    const CaseMeta &meta = cm->meta();
    auto *form = new QFormLayout();
    auto *caseNo = new QLabel(meta.caseNo, this);
    caseNo->setStyleSheet(QStringLiteral(
        "color:%1; font-weight:bold;").arg(Theme::Accent));
    form->addRow(lang("编号（固定）", "Number (fixed)"), caseNo);
    form->addRow(lang("案发日期（固定）", "Incident date (fixed)"),
                 new QLabel(QDateTime::fromMSecsSinceEpoch(meta.incidentTimeMs)
                                .toString(QStringLiteral("yyyy-MM-dd")), this));
    form->addRow(lang("案发地点（固定）", "Location (fixed)"),
                 new QLabel(meta.city + QStringLiteral(" ") + meta.district,
                            this));
    form->addRow(lang("创建时间", "Created"),
                 new QLabel(QDateTime::fromMSecsSinceEpoch(meta.createdMs)
                                .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")),
                            this));

    m_title = new QLineEdit(meta.title, this);
    m_investigator = new QLineEdit(meta.investigator, this);
    m_unit = new QLineEdit(meta.unit, this);
    m_locationDetail = new QLineEdit(meta.locationDetail, this);
    m_description = new QPlainTextEdit(meta.description, this);
    m_description->setMaximumHeight(72);
    form->addRow(lang("案件名称 *", "Case title *"), m_title);
    form->addRow(lang("调查员", "Investigator"), m_investigator);
    form->addRow(lang("单位", "Unit"), m_unit);
    form->addRow(lang("详细地址", "Address"), m_locationDetail);
    form->addRow(lang("备注", "Notes"), m_description);
    lay->addLayout(form);

    auto *stat = new QLabel(
        lang("视频 %1 路 · 前处理会话 %2 个 · 报告 %3 份",
             "%1 video(s) · %2 preprocess session(s) · %3 report(s)")
            .arg(meta.videos.size()).arg(meta.preprocessSessions.size())
            .arg(meta.reports.size()), this);
    stat->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    lay->addWidget(stat);

    auto *buttons = new QDialogButtonBox(
        QDialogButtonBox::Save | QDialogButtonBox::Close, this);
    buttons->button(QDialogButtonBox::Save)->setText(lang("保存", "Save"));
    buttons->button(QDialogButtonBox::Close)->setText(lang("关闭", "Close"));
    lay->addWidget(buttons);
    connect(buttons->button(QDialogButtonBox::Close), &QPushButton::clicked,
            this, &QDialog::accept);
    connect(buttons->button(QDialogButtonBox::Save), &QPushButton::clicked,
            this, [this, buttons]() {
        QString err;
        if (!m_caseManager->updateCaseInfo(
                m_title->text(), m_investigator->text(), m_unit->text(),
                m_locationDetail->text(), m_description->toPlainText(), &err)) {
            QMessageBox::warning(this, lang("保存失败", "Save failed"), err);
            return;
        }
        if (m_caseManager->isDirty()) {
            QString serr;
            if (!m_caseManager->saveCase(&serr)) {
                QMessageBox::warning(this, lang("保存失败", "Save failed"), serr);
                return;
            }
        }
        buttons->button(QDialogButtonBox::Save)->setEnabled(false);
    });
}
