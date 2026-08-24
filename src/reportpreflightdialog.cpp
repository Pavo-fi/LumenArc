#include "reportpreflightdialog.h"

#include "app/case_manager.h"
#include "app/report_service.h"
#include "domain/report_preflight.h"

#include <QDialogButtonBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QTableWidget>
#include <QTreeWidget>
#include <QVBoxLayout>

ReportPreflightDialog::ReportPreflightDialog(CaseManager *cm,
                                             VideoStateManager *vsm,
                                             QWidget *parent)
    : QDialog(parent), m_cm(cm), m_vsm(vsm)
{
    setWindowTitle(tr("生成分析报告 — 自检与信息补录"));
    resize(760, 640);
    auto *lay = new QVBoxLayout(this);

    // ---- 自检 ----
    auto *checkGrp = new QGroupBox(tr("① 生成前自检"), this);
    auto *cg = new QVBoxLayout(checkGrp);
    m_checks = new QTreeWidget(checkGrp);
    m_checks->setHeaderLabels({tr("级别"), tr("检查项")});
    m_checks->setRootIsDecorated(false);
    m_checks->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    m_checks->header()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_checks->setMinimumHeight(200);
    cg->addWidget(m_checks);
    lay->addWidget(checkGrp);

    // ---- 补录：人员 ----
    auto *metaGrp = new QGroupBox(tr("② 信息补录（保存入案件，下次生成记忆）"), this);
    auto *mg = new QVBoxLayout(metaGrp);
    auto *form = new QFormLayout;
    m_reviewer = new QLineEdit(metaGrp);
    m_approver = new QLineEdit(metaGrp);
    m_sender = new QLineEdit(metaGrp);
    form->addRow(tr("审核人："), m_reviewer);
    form->addRow(tr("批准人："), m_approver);
    form->addRow(tr("送检人："), m_sender);
    mg->addLayout(form);

    // ---- 补录：逐路 ----
    mg->addWidget(new QLabel(tr("逐路检材补录："), metaGrp));
    m_videoTable = new QTableWidget(metaGrp);
    m_videoTable->setColumnCount(4);
    m_videoTable->setHorizontalHeaderLabels(
        {tr("检材"), tr("拍摄方向"), tr("提取方式"), tr("存储介质")});
    m_videoTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    for (int c = 1; c < 4; ++c)
        m_videoTable->horizontalHeader()->setSectionResizeMode(c, QHeaderView::Stretch);
    m_videoTable->setMinimumHeight(120);
    m_videoTable->setMaximumHeight(180);
    mg->addWidget(m_videoTable);
    lay->addWidget(metaGrp);

    // ---- 按钮行 ----
    auto *btnRow = new QHBoxLayout;
    auto *recheckBtn = new QPushButton(tr("重新自检"), this);
    connect(recheckBtn, &QPushButton::clicked, this, [this]() {
        persistExtras();   // 补录先落盘再重检（级别联动）
        recheck();
    });
    btnRow->addWidget(recheckBtn);
    btnRow->addStretch();
    auto *cancelBtn = new QPushButton(tr("取消"), this);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
    btnRow->addWidget(cancelBtn);
    m_genBtn = new QPushButton(tr("生成报告"), this);
    m_genBtn->setDefault(true);
    connect(m_genBtn, &QPushButton::clicked, this, [this]() {
        persistExtras();
        accept();
    });
    btnRow->addWidget(m_genBtn);
    lay->addLayout(btnRow);

    // 初始填充补录值
    const CaseMeta &meta = m_cm->meta();
    m_reviewer->setText(meta.extraFields.value(QStringLiteral("report/reviewer")));
    m_approver->setText(meta.extraFields.value(QStringLiteral("report/approver")));
    m_sender->setText(meta.extraFields.value(QStringLiteral("report/sender")));

    recheck();
}

void ReportPreflightDialog::recheck()
{
    const ReportData rd = ReportService::collect(m_cm, m_vsm, /*computeHashes=*/false);
    const auto items = reportPreflight(rd);

    m_checks->clear();
    for (const auto &it : items) {
        const QString icon = it.level == ReportPreflightItem::Block
            ? QStringLiteral("❌")
            : it.level == ReportPreflightItem::Warn
                ? QStringLiteral("⚠️") : QStringLiteral("✅");
        auto *row = new QTreeWidgetItem(m_checks, {icon, it.text});
        if (it.level == ReportPreflightItem::Block)
            row->setForeground(1, QBrush(QColor(200, 40, 40)));
        else if (it.level == ReportPreflightItem::Warn)
            row->setForeground(1, QBrush(QColor(180, 130, 20)));
    }

    // 逐路补录表
    m_videoIds.clear();
    m_videoTable->setRowCount(0);
    for (const ReportVideoRow &v : rd.videos) {
        const int r = m_videoTable->rowCount();
        m_videoTable->insertRow(r);
        m_videoIds << v.id;
        auto *name = new QTableWidgetItem(
            QStringLiteral("%1（%2）").arg(v.cameraLabel, v.id));
        name->setFlags(name->flags() & ~Qt::ItemIsEditable);
        m_videoTable->setItem(r, 0, name);
        m_videoTable->setItem(r, 1, new QTableWidgetItem(v.shootDir));
        m_videoTable->setItem(r, 2, new QTableWidgetItem(v.extractMethod));
        m_videoTable->setItem(r, 3, new QTableWidgetItem(v.storageMedium));
    }

    const bool blocked = reportPreflightBlocked(items);
    m_genBtn->setEnabled(!blocked);
    m_genBtn->setToolTip(blocked
        ? tr("存在 ❌ 阻断项，请先处理（缺失文件/全部未校时）")
        : tr("补录已保存；点击生成 DOCX 报告"));
}

void ReportPreflightDialog::persistExtras()
{
    auto put = [this](const QString &key, const QString &val) {
        QString err;
        if (!m_cm->setReportExtra(key, val.trimmed(), &err))
            qWarning() << "report extra save failed:" << key << err;
    };
    put(QStringLiteral("reviewer"), m_reviewer->text());
    put(QStringLiteral("approver"), m_approver->text());
    put(QStringLiteral("sender"), m_sender->text());
    for (int r = 0; r < m_videoTable->rowCount() && r < m_videoIds.size(); ++r) {
        const QString id = m_videoIds[r];
        auto cell = [this](int r, int c) {
            QTableWidgetItem *it = m_videoTable->item(r, c);
            return it ? it->text().trimmed() : QString();
        };
        put(QStringLiteral("video/") + id + QStringLiteral("/direction"), cell(r, 1));
        put(QStringLiteral("video/") + id + QStringLiteral("/method"), cell(r, 2));
        put(QStringLiteral("video/") + id + QStringLiteral("/medium"), cell(r, 3));
    }
}
