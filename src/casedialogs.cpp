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

#include "infrastructure/signature_store.h"

#include <QDateEdit>
#include <QDateTime>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProgressBar>
#include <QPushButton>
#include <QRadioButton>
#include <QTableWidget>
#include <QVBoxLayout>
#include <QFutureWatcher>
#include <QtConcurrent>

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
    // 账号系统 v1.3：调查员/单位默认取署名（SignatureStore 单一真源，用户可改）
    {
        if (!SignatureStore::name().isEmpty()) m_investigator->setText(SignatureStore::name());
        if (!SignatureStore::org().isEmpty()) m_unit->setText(SignatureStore::org());
    }
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
    // 账号系统 v1.3：空值时用署名预填（已有值不动）
    {
        if (m_investigator->text().trimmed().isEmpty() && !SignatureStore::name().isEmpty())
            m_investigator->setText(SignatureStore::name());
        if (m_unit->text().trimmed().isEmpty() && !SignatureStore::org().isEmpty())
            m_unit->setText(SignatureStore::org());
    }
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

// ---------------------------------------------------------------------------
// 重定位交互（CaseDock / 导出前自检共用）
// ---------------------------------------------------------------------------
bool relocateVideoInteractive(CaseManager *cm, const QString &id,
                              QWidget *parent)
{
    const auto *v = cm->videoById(id);
    if (!v)
        return false;
    const QString newPath = QFileDialog::getOpenFileName(parent,
        lang("重新定位源视频（只改引用，不动案件数据）— %1",
             "Relocate source video (reference only) — %1").arg(id),
        QFileInfo(v->originalPath).absolutePath(),
        lang("视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;所有文件 (*)",
             "Video Files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;All Files (*)"));
    if (newPath.isEmpty())
        return false;
    QString err;
    bool mismatch = false;
    if (!cm->relocateVideo(id, newPath, &err, &mismatch, false)) {
        if (!mismatch) {
            QMessageBox::warning(parent, lang("重定位失败", "Relocate failed"), err);
            return false;
        }
        // 大小不一致：默认拒绝，显式【仍要采用】后强制（留档，拍板§8-8）
        const auto reply = QMessageBox::warning(parent,
            lang("重定位校验", "Relocate check"),
            err + QStringLiteral("\n\n") +
            lang("确定仍要采用该文件作为 %1 的来源吗？\n（采用后指纹将重算，覆写轨迹将留档）",
                 "Use this file as the source of %1 anyway?\n(Fingerprint will be "
                 "recomputed; the override is archived)").arg(id),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return false;
        if (!cm->relocateVideo(id, newPath, &err, nullptr, true)) {
            QMessageBox::warning(parent, lang("重定位失败", "Relocate failed"), err);
            return false;
        }
    }
    return true;
}

// ---------------------------------------------------------------------------
// ExportCaseDialog（M3 任务12）
// ---------------------------------------------------------------------------
ExportCaseDialog::ExportCaseDialog(CaseManager *cm, QWidget *parent)
    : QDialog(parent)
    , m_cm(cm)
{
    setWindowTitle(lang("导出移交包", "Export Handover Package"));
    setMinimumWidth(560);
    auto *lay = new QVBoxLayout(this);

    // 包类型：完整包默认 / 轻量可选（拍板§8-1 修订 Q-8）
    auto *typeRow = new QHBoxLayout();
    m_fullRadio = new QRadioButton(
        lang("完整包（默认）：含 sources/ 视频副本，换机零操作可用",
             "Full (default): video copies in sources/, works on another machine"),
        this);
    m_fullRadio->setChecked(true);
    m_lightRadio = new QRadioButton(
        lang("轻量包：不含视频副本，接收方需重新定位源视频",
             "Light: no video copies; receiver relocates sources"), this);
    typeRow->addWidget(m_fullRadio);
    typeRow->addWidget(m_lightRadio);
    typeRow->addStretch(1);
    lay->addLayout(typeRow);

    // 目标目录
    auto *dirRow = new QHBoxLayout();
    dirRow->addWidget(new QLabel(lang("导出到：", "Export to:"), this));
    m_targetEdit = new QLineEdit(
        QFileInfo(cm->caseDir()).absoluteDir().absolutePath(), this);
    auto *btnBrowse = new QPushButton(lang("浏览…", "Browse…"), this);
    dirRow->addWidget(m_targetEdit, 1);
    dirRow->addWidget(btnBrowse);
    lay->addLayout(dirRow);
    connect(btnBrowse, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this,
            lang("选择导出位置", "Choose export destination"),
            m_targetEdit->text());
        if (!dir.isEmpty())
            m_targetEdit->setText(dir);
    });

    // 自检结果区
    m_checkLabel = new QLabel(this);
    m_checkLabel->setWordWrap(true);
    m_checkLabel->setStyleSheet(QStringLiteral(
        "color:%1; padding:6px; background:%2; border-radius:6px;")
        .arg(Theme::TextSecond, Theme::BgCard));
    lay->addWidget(m_checkLabel);

    // 自检动作行
    auto *actRow = new QHBoxLayout();
    m_btnHash = new QPushButton(lang("立即计算缺失指纹", "Compute missing hashes"),
                                this);
    m_btnRelocate = new QPushButton(lang("重新定位缺失文件", "Relocate missing files"),
                                    this);
    actRow->addWidget(m_btnHash);
    actRow->addWidget(m_btnRelocate);
    actRow->addStretch(1);
    lay->addLayout(actRow);
    connect(m_btnHash, &QPushButton::clicked,
            this, &ExportCaseDialog::onComputeHashes);
    connect(m_btnRelocate, &QPushButton::clicked,
            this, &ExportCaseDialog::onRelocateMissing);

    // 进度
    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    lay->addWidget(m_progress);

    // 主按钮行
    auto *btnRow = new QHBoxLayout();
    m_btnExport = new QPushButton(lang("开始导出", "Export"), this);
    m_btnExport->setDefault(true);
    m_btnCancelExport = new QPushButton(lang("取消导出", "Cancel export"), this);
    m_btnCancelExport->setVisible(false);
    auto *btnClose = new QPushButton(lang("关闭", "Close"), this);
    btnRow->addStretch(1);
    btnRow->addWidget(m_btnExport);
    btnRow->addWidget(m_btnCancelExport);
    btnRow->addWidget(btnClose);
    lay->addLayout(btnRow);
    connect(m_btnExport, &QPushButton::clicked,
            this, &ExportCaseDialog::onExport);
    connect(m_btnCancelExport, &QPushButton::clicked, this, [this]() {
        m_cm->cancelExport();
    });
    connect(btnClose, &QPushButton::clicked, this, [this]() {
        if (m_exporting)
            m_cm->cancelExport();
        reject();
    });
    connect(m_targetEdit, &QLineEdit::textChanged,
            this, &ExportCaseDialog::refreshPrecheck);
    connect(m_fullRadio, &QRadioButton::toggled,
            this, &ExportCaseDialog::refreshPrecheck);
    connect(m_lightRadio, &QRadioButton::toggled,
            this, &ExportCaseDialog::refreshPrecheck);
    connect(m_cm, &CaseManager::exportProgress, this,
            [this](int pct, const QString &stage) {
                m_progress->setValue(pct);
                m_progress->setFormat(
                    lang("%p%（%1）", "%p% (%1)").arg(stage));
            });
    connect(m_cm, &CaseManager::exportFinished, this,
            [this](bool ok, const QString &message) {
                m_exporting = false;
                m_progress->setVisible(false);
                m_btnCancelExport->setVisible(false);
                m_btnExport->setEnabled(true);
                if (ok) {
                    QMessageBox::information(this,
                        lang("导出完成", "Export finished"),
                        lang("移交包已导出：\n%1", "Package exported to:\n%1")
                            .arg(message));
                    accept();
                } else {
                    QMessageBox::warning(this,
                        lang("导出失败", "Export failed"), message);
                }
                refreshPrecheck();
            });
    connect(m_cm, &CaseManager::hashQueueFinished, this,
            [this]() { refreshPrecheck(); });
    refreshPrecheck();
}

void ExportCaseDialog::refreshPrecheck()
{
    if (m_exporting)
        return;
    const bool full = m_fullRadio->isChecked();
    const auto pc = m_cm->exportPrecheck(m_targetEdit->text().trimmed(), full);
    QStringList lines;
    auto fmtBytesLocal = [](qint64 b) {
        if (b >= (1LL << 30)) return QStringLiteral("%1 GB").arg(b / 1073741824.0, 0, 'f', 1);
        if (b >= (1LL << 20)) return QStringLiteral("%1 MB").arg(b / 1048576.0, 0, 'f', 1);
        return QStringLiteral("%1 KB").arg(b / 1024.0, 0, 'f', 1);
    };
    if (pc.missingHashCount > 0)
        lines << lang("⚠ %1 路视频未算指纹（可【立即计算】补算；不补则包内登记为空，"
                      "接收方校验能力受限）",
                      "⚠ %1 video(s) missing fingerprints (compute now, or export "
                      "with empty hashes)").arg(pc.missingHashCount);
    if (!pc.missingVideoIds.isEmpty())
        lines << lang("⚠ 源文件缺失：%1（可【重新定位】；仍要导出则不携带副本，"
                      "README 注明）",
                      "⚠ Missing sources: %1 (relocate, or export without copies — "
                      "noted in README)").arg(pc.missingVideoIds.join("、"));
    const qint64 need = pc.caseBytes + (full ? pc.sourcesBytes : 0);
    lines << lang("包体量约 %1（案内 %2%3）",
                  "Package size ≈ %1 (case %2%3)")
                 .arg(fmtBytesLocal(need), fmtBytesLocal(pc.caseBytes),
                      full ? QStringLiteral(" + sources ")
                                 + fmtBytesLocal(pc.sourcesBytes) : QString());
    if (pc.availableBytes >= 0) {
        lines << lang("目标卷可用 %1", "Available on target: %1")
                     .arg(fmtBytesLocal(pc.availableBytes));
        if (pc.insufficientSpace)
            lines << lang("✗ 空间不足（需预留 10% 余量）",
                          "✗ Insufficient space (10% headroom required)");
    }
    if (pc.missingHashCount == 0 && pc.missingVideoIds.isEmpty()
        && !pc.insufficientSpace)
        lines << lang("✓ 自检通过，可导出", "✓ Pre-check passed, ready to export");
    m_checkLabel->setText(lines.join(QStringLiteral("\n")));
    // 知情可仍要导出（拍板§8-9）：缺失哈希/缺失源不阻断；空间不足阻断
    m_btnExport->setEnabled(!pc.insufficientSpace);
    m_btnHash->setEnabled(pc.missingHashCount > 0);
    m_btnRelocate->setEnabled(!pc.missingVideoIds.isEmpty());
}

void ExportCaseDialog::onComputeHashes()
{
    m_cm->queueMissingHashes();
    m_btnHash->setEnabled(false);
    m_checkLabel->setText(
        lang("指纹后台计算中（单线程低优先级，可继续使用）…",
             "Computing fingerprints in background…"));
}

void ExportCaseDialog::onRelocateMissing()
{
    const auto pc = m_cm->exportPrecheck(m_targetEdit->text().trimmed(),
                                         m_fullRadio->isChecked());
    for (const QString &id : pc.missingVideoIds)
        relocateVideoInteractive(m_cm, id, this);
    refreshPrecheck();
}

void ExportCaseDialog::onExport()
{
    const QString target = m_targetEdit->text().trimmed();
    if (target.isEmpty())
        return;
    m_exporting = true;
    m_btnExport->setEnabled(false);
    m_progress->setVisible(true);
    m_progress->setValue(0);
    m_btnCancelExport->setVisible(true);
    m_cm->exportCase(target, m_fullRadio->isChecked());
}

// ---------------------------------------------------------------------------
// BatchRelocateDialog（M3 任务13）
// ---------------------------------------------------------------------------
BatchRelocateDialog::BatchRelocateDialog(CaseManager *cm, QWidget *parent)
    : QDialog(parent)
    , m_cm(cm)
{
    setWindowTitle(lang("批量重新定位", "Batch Relocate"));
    setMinimumSize(860, 480);
    auto *lay = new QVBoxLayout(this);

    // 候选目录行
    auto *dirRow = new QHBoxLayout();
    dirRow->addWidget(new QLabel(lang("候选目录：", "Search folder:"), this));
    m_dirEdit = new QLineEdit(this);
    m_dirEdit->setPlaceholderText(
        lang("选择源视频当前所在目录（递归扫描，名+大小模糊匹配）",
             "Folder where source videos now live (recursive, name+size match)"));
    auto *btnDir = new QPushButton(lang("浏览…", "Browse…"), this);
    m_btnScan = new QPushButton(lang("扫描候选", "Scan"), this);
    dirRow->addWidget(m_dirEdit, 1);
    dirRow->addWidget(btnDir);
    dirRow->addWidget(m_btnScan);
    lay->addLayout(dirRow);
    connect(btnDir, &QPushButton::clicked, this, [this]() {
        const QString dir = QFileDialog::getExistingDirectory(this,
            lang("选择候选目录", "Choose search folder"), m_dirEdit->text());
        if (!dir.isEmpty())
            m_dirEdit->setText(dir);
    });
    connect(m_btnScan, &QPushButton::clicked, this, &BatchRelocateDialog::onScan);

    // 候选表：V### | 原路径（缺失） | 候选新路径 | 匹配 | 状态/备注
    m_table = new QTableWidget(this);
    m_table->setColumnCount(5);
    m_table->setHorizontalHeaderLabels(
        {lang("编号", "ID"), lang("原路径（缺失）", "Original (missing)"),
         lang("候选新路径", "Candidate"), lang("匹配", "Match"),
         lang("状态/备注", "Status")});
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_table, 1);

    // 行操作 + 摘要
    auto *rowOps = new QHBoxLayout();
    m_btnBrowseRow = new QPushButton(
        lang("浏览覆盖选中行候选…", "Browse candidate for row…"), this);
    m_btnBrowseRow->setEnabled(false);
    rowOps->addWidget(m_btnBrowseRow);
    m_summary = new QLabel(this);
    m_summary->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    rowOps->addWidget(m_summary, 1);
    lay->addLayout(rowOps);
    connect(m_btnBrowseRow, &QPushButton::clicked,
            this, &BatchRelocateDialog::onBrowseRow);
    connect(m_table, &QTableWidget::itemSelectionChanged, this, [this]() {
        m_btnBrowseRow->setEnabled(!m_hashing
                                   && !m_table->selectedItems().isEmpty());
    });

    // 比对进度
    m_progress = new QProgressBar(this);
    m_progress->setVisible(false);
    lay->addWidget(m_progress);

    // 主按钮行
    auto *btnRow = new QHBoxLayout();
    m_btnVerify = new QPushButton(lang("比对并采用", "Verify && Apply"), this);
    m_btnVerify->setDefault(true);
    m_btnVerify->setEnabled(false);
    m_btnOverride = new QPushButton(
        lang("仍要采用（留档）", "Apply anyway (archived)"), this);
    m_btnOverride->setEnabled(false);
    m_btnCancelHash = new QPushButton(lang("取消比对", "Cancel verify"), this);
    m_btnCancelHash->setVisible(false);
    auto *btnClose = new QPushButton(lang("关闭", "Close"), this);
    btnRow->addStretch(1);
    btnRow->addWidget(m_btnVerify);
    btnRow->addWidget(m_btnOverride);
    btnRow->addWidget(m_btnCancelHash);
    btnRow->addWidget(btnClose);
    lay->addLayout(btnRow);
    connect(m_btnVerify, &QPushButton::clicked,
            this, &BatchRelocateDialog::onVerifyAndApply);
    connect(m_btnOverride, &QPushButton::clicked,
            this, &BatchRelocateDialog::onApplyOverrides);
    connect(m_btnCancelHash, &QPushButton::clicked, this, [this]() {
        m_hashCancel = true;
        if (m_workerAbort)
            m_workerAbort->store(true);
    });
    connect(btnClose, &QPushButton::clicked, this, &QDialog::reject);

    refreshSummary();
}

void BatchRelocateDialog::reject()
{
    // 关闭前先取消后台比对；watcher 是本对话框子对象，随销毁断链，
    // 工作线程持有独立 abort 副本（shared_ptr）不会悬垂
    m_hashCancel = true;
    if (m_workerAbort)
        m_workerAbort->store(true);
    QDialog::reject();
}

void BatchRelocateDialog::onScan()
{
    const QString dir = m_dirEdit->text().trimmed();
    if (dir.isEmpty())
        return;
    const auto cands = m_cm->proposeRelocations(dir);
    m_rows.clear();
    m_rows.reserve(cands.size());
    for (const auto &c : cands) {
        Row r;
        r.videoId = c.videoId;
        r.originalPath = c.originalPath;
        r.candidatePath = c.candidatePath;
        r.matchLevel = c.matchLevel;
        r.status = c.candidatePath.isEmpty() ? 5 : 0;   // 无候选 / 待比对
        m_rows.append(r);
    }
    rebuildTable();
    refreshSummary();
}

void BatchRelocateDialog::onBrowseRow()
{
    const int row = m_table->currentRow();
    if (row < 0 || row >= m_rows.size())
        return;
    const QString path = QFileDialog::getOpenFileName(this,
        lang("为 %1 指定候选文件", "Pick candidate for %1")
            .arg(m_rows[row].videoId),
        m_dirEdit->text().trimmed(),
        lang("视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;所有文件 (*)",
             "Video Files (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.webm);;All Files (*)"));
    if (path.isEmpty())
        return;
    m_rows[row].candidatePath = path;
    m_rows[row].manual = true;
    m_rows[row].status = 0;      // 回到待比对
    m_rows[row].note.clear();
    m_rows[row].computedSha.clear();
    rebuildTable();
    refreshSummary();
}

void BatchRelocateDialog::rebuildTable()
{
    m_table->setRowCount(m_rows.size());
    for (int i = 0; i < m_rows.size(); ++i) {
        const Row &r = m_rows[i];
        auto mk = [](const QString &t) { return new QTableWidgetItem(t); };
        m_table->setItem(i, 0, mk(r.videoId));
        auto *orig = mk(QFileInfo(r.originalPath).fileName());
        orig->setToolTip(r.originalPath);
        m_table->setItem(i, 1, orig);
        auto *cand = mk(r.candidatePath.isEmpty()
                            ? lang("（无候选）", "(none)")
                            : QFileInfo(r.candidatePath).fileName());
        cand->setToolTip(r.candidatePath);
        m_table->setItem(i, 2, cand);
        QString matchTxt;
        if (r.manual)
            matchTxt = lang("人工指定", "Manual");
        else if (r.matchLevel == 2)
            matchTxt = lang("名+大小一致", "Name+size");
        else if (r.matchLevel == 1)
            matchTxt = lang("仅文件名", "Name only");
        else
            matchTxt = QStringLiteral("—");
        m_table->setItem(i, 3, mk(matchTxt));
        setRowStatus(i);
    }
    m_table->resizeColumnsToContents();
    m_table->horizontalHeader()->setSectionResizeMode(1, QHeaderView::Stretch);
    m_table->horizontalHeader()->setSectionResizeMode(2, QHeaderView::Stretch);
}

void BatchRelocateDialog::setRowStatus(int row)
{
    if (row < 0 || row >= m_rows.size() || row >= m_table->rowCount())
        return;
    const Row &r = m_rows[row];
    QString txt;
    QColor color(Theme::TextMuted);
    switch (r.status) {
    case 1: txt = lang("✓ 已採用", "✓ Applied"); color = QColor(Theme::Success); break;
    case 2: txt = lang("⚠ 指纹不一致（默认拒绝）", "⚠ Hash mismatch (rejected)");
        color = QColor(Theme::Danger); break;
    case 3: txt = lang("✓ 已採用（留档）", "✓ Applied (archived)");
        color = QColor(Theme::Accent); break;
    case 4: txt = lang("✗ 失败", "✗ Failed"); color = QColor(Theme::Danger); break;
    case 5: txt = lang("✗ 无候选", "✗ No candidate");
        color = QColor(Theme::Danger); break;
    default: txt = lang("待比对", "Pending"); break;
    }
    if (!r.note.isEmpty())
        txt += QStringLiteral(" — ") + r.note;
    auto *it = new QTableWidgetItem(txt);
    it->setForeground(color);
    m_table->setItem(row, 4, it);
}

void BatchRelocateDialog::refreshSummary()
{
    int adopted = 0, pending = 0, mismatch = 0, missing = 0, failed = 0;
    for (const Row &r : m_rows) {
        switch (r.status) {
        case 1: case 3: ++adopted; break;
        case 2: ++mismatch; break;
        case 4: ++failed; break;
        case 5: ++missing; break;
        default: ++pending; break;
        }
    }
    m_summary->setText(
        lang("缺失 %1 路 · 已採用 %2 · 待比对 %3 · 不一致 %4 · 无候选 %5",
             "%1 missing · %2 applied · %3 pending · %4 mismatch · %5 no candidate")
            .arg(m_rows.size()).arg(adopted).arg(pending).arg(mismatch)
            .arg(missing)
        + (failed ? lang(" · 失败 %1", " · %1 failed").arg(failed) : QString()));
    const bool hasWork = pending > 0 || failed > 0;
    m_btnVerify->setEnabled(!m_hashing && hasWork);
    m_btnOverride->setEnabled(!m_hashing && mismatch > 0);
    m_btnScan->setEnabled(!m_hashing);
}

QString BatchRelocateDialog::registeredSha(const QString &id) const
{
    const auto *v = m_cm->videoById(id);
    return v ? v->sha256 : QString();
}

void BatchRelocateDialog::onVerifyAndApply()
{
    m_pending.clear();
    for (int i = 0; i < m_rows.size(); ++i)
        if (!m_rows[i].candidatePath.isEmpty()
            && (m_rows[i].status == 0 || m_rows[i].status == 4))
            m_pending.append(i);
    if (m_pending.isEmpty())
        return;
    m_hashCancel = false;
    m_workerAbort = std::make_shared<std::atomic<bool>>(false);
    m_hashing = true;
    m_progress->setVisible(true);
    m_progress->setRange(0, m_pending.size());
    m_progress->setValue(0);
    m_btnCancelHash->setVisible(true);
    refreshSummary();   // 禁用扫描/比对按钮
    startNextHash();
}

void BatchRelocateDialog::startNextHash()
{
    if (m_hashCancel || m_pending.isEmpty()) {
        onAllHashed();
        return;
    }
    const int row = m_pending.takeFirst();
    const QString path = m_rows[row].candidatePath;
    auto abort = m_workerAbort;
    // 逐路串行（同哈希队列 IO 纪律）；结果经 watcher 回 GUI 线程
    auto *watcher = new QFutureWatcher<QPair<bool, QString>>(this);
    connect(watcher, &QFutureWatcher<QPair<bool, QString>>::finished,
            this, [this, watcher, row]() {
                const auto res = watcher->result();
                watcher->deleteLater();
                onRowHashed(row, res.first, res.second);
                startNextHash();
            });
    watcher->setFuture(QtConcurrent::run([path, abort]() {
        QString sha;
        const bool ok = CaseManager::computeSha256(path, &sha, abort.get());
        return qMakePair(ok, sha);
    }));
}

void BatchRelocateDialog::onRowHashed(int row, bool ok, const QString &sha)
{
    if (row < 0 || row >= m_rows.size())
        return;
    Row &r = m_rows[row];
    if (!ok) {
        // 取消导致的失败回到待比对（可再次比对）；读取失败保持失败态
        r.status = m_hashCancel ? 0 : 4;
        r.note = m_hashCancel ? QString() : lang("读取失败", "Read failed");
    } else {
        r.computedSha = sha;
        const QString reg = registeredSha(r.videoId);
        if (!reg.isEmpty() && reg != sha) {
            // 指纹强制比对：不一致默认拒绝（拍板§8-8，显式【仍要采用】留档）
            r.status = 2;
            r.note = lang("登记 %1… ≠ 候选 %2…", "reg %1… ≠ cand %2…")
                         .arg(reg.left(8), sha.left(8));
        } else {
            // 一致（或无登记基线）→ 採用；knownSha 直接登记免二次哈希
            QString err;
            bool mismatch = false;
            if (m_cm->relocateVideo(r.videoId, r.candidatePath, &err,
                                    &mismatch, false, sha)) {
                r.status = 1;
                r.note.clear();
            } else if (mismatch) {
                r.status = 2;
                r.note = err;
            } else {
                r.status = 4;
                r.note = err;
            }
        }
    }
    setRowStatus(row);
    m_progress->setValue(m_progress->value() + 1);
    refreshSummary();
}

void BatchRelocateDialog::onAllHashed()
{
    m_hashing = false;
    m_progress->setVisible(false);
    m_btnCancelHash->setVisible(false);
    // 採用已落 meta：静默落盘（机器维护写，与哈希队列排空同理）
    if (m_cm->isDirty()) {
        QString err;
        m_cm->saveCase(&err);
    }
    refreshSummary();
}

void BatchRelocateDialog::onApplyOverrides()
{
    QVector<int> targets;
    for (int i = 0; i < m_rows.size(); ++i)
        if (m_rows[i].status == 2 && !m_rows[i].computedSha.isEmpty())
            targets.append(i);
    if (targets.isEmpty())
        return;
    // 默认拒绝，显式【仍要采用】后强制（留档，拍板§8-8）
    QMessageBox box(this);
    box.setWindowTitle(lang("仍要采用", "Apply anyway"));
    box.setIcon(QMessageBox::Warning);
    box.setText(lang("%1 路候选指纹与登记不一致。\n仍要采用将以候选为准重登记指纹，"
                     "覆写轨迹留档于 case.json。",
                     "%1 candidate(s) mismatch the registered fingerprint.\n"
                     "Applying anyway re-registers the candidate fingerprint; "
                     "the override is archived in case.json.").arg(targets.size()));
    QAbstractButton *btnYes = box.addButton(
        lang("仍要采用（留档）", "Apply anyway (archived)"),
        QMessageBox::YesRole);
    box.addButton(lang("取消", "Cancel"), QMessageBox::NoRole);
    box.exec();
    if (box.clickedButton() != btnYes)
        return;
    for (int row : targets) {
        Row &r = m_rows[row];
        QString err;
        if (m_cm->relocateVideo(r.videoId, r.candidatePath, &err,
                                nullptr, true, r.computedSha)) {
            r.status = 3;
            r.note = lang("覆写轨迹已留档", "Override archived");
        } else {
            r.status = 4;
            r.note = err;
        }
        setRowStatus(row);
    }
    if (m_cm->isDirty()) {
        QString err;
        m_cm->saveCase(&err);
    }
    refreshSummary();
}
