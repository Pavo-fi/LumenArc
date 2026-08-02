/**
 * @file preprocesspanel.cpp
 * @brief 前处理面板实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "preprocesspanel.h"
#include "i18n.h"

#include <QStackedWidget>
#include <QListWidget>
#include <QTableWidget>
#include <QTabWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QFileDialog>
#include <QMessageBox>
#include <QHeaderView>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QUrl>
#include <QFileInfo>

PreprocessPanel::PreprocessPanel(QWidget *parent)
    : QDockWidget(lang("前处理", "Preprocessing"), parent)
    , m_coord(new PreprocessingCoordinator(this))
{
    setObjectName(QStringLiteral("preprocessPanel"));
    setAllowedAreas(Qt::LeftDockWidgetArea | Qt::RightDockWidgetArea);

    m_stack = new QStackedWidget(this);
    m_stack->addWidget(buildStepFiles());
    m_stack->addWidget(buildStepEvidence());
    m_stack->addWidget(buildStepOptions());
    m_stack->addWidget(buildStepRun());
    setWidget(m_stack);

    connect(m_coord, &PreprocessingCoordinator::phaseChanged,
            this, &PreprocessPanel::onPhaseChanged);
    connect(m_coord, &PreprocessingCoordinator::progress,
            this, &PreprocessPanel::onProgress);
    connect(m_coord, &PreprocessingCoordinator::probeDone,
            this, &PreprocessPanel::onProbeDone);
    connect(m_coord, &PreprocessingCoordinator::evidenceReady,
            this, &PreprocessPanel::onEvidenceReady);
    connect(m_coord, &PreprocessingCoordinator::precheckReady,
            this, &PreprocessPanel::onPrecheckReady);
    connect(m_coord, &PreprocessingCoordinator::finished,
            this, &PreprocessPanel::onFinished);
    connect(m_coord, &PreprocessingCoordinator::failed,
            this, &PreprocessPanel::onFailed);
    connect(m_coord, &PreprocessingCoordinator::logLine,
            this, &PreprocessPanel::onLogLine);
}

// ---------------------------------------------------------------------------
// 步骤页构建
// ---------------------------------------------------------------------------
QWidget *PreprocessPanel::buildStepFiles()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);
    lay->addWidget(new QLabel(lang("① 素材：添加待整理的视频片段", 
                                   "Step 1: Add source clips"), w));
    m_fileList = new QListWidget(w);
    m_fileList->setSelectionMode(QAbstractItemView::ExtendedSelection);
    lay->addWidget(m_fileList, 1);
    auto *row = new QHBoxLayout();
    auto *btnAdd = new QPushButton(lang("添加…", "Add…"), w);
    auto *btnRemove = new QPushButton(lang("移除", "Remove"), w);
    m_btnBegin = new QPushButton(lang("开始分析", "Analyze"), w);
    m_btnBegin->setEnabled(false);
    row->addWidget(btnAdd);
    row->addWidget(btnRemove);
    row->addStretch(1);
    row->addWidget(m_btnBegin);
    lay->addLayout(row);
    connect(btnAdd, &QPushButton::clicked, this, &PreprocessPanel::onAddFiles);
    connect(btnRemove, &QPushButton::clicked, this, &PreprocessPanel::onRemoveFiles);
    connect(m_btnBegin, &QPushButton::clicked, this, &PreprocessPanel::onBeginAnalysis);
    return w;
}

QWidget *PreprocessPanel::buildStepEvidence()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);
    lay->addWidget(new QLabel(lang("② 排序与证据：核对智能排序结果（硬证据：首/尾帧画面）",
                                   "Step 2: Review smart ordering (hard evidence: first/last frames)"), w));
    m_groupTabs = new QTabWidget(w);
    lay->addWidget(m_groupTabs, 1);
    m_warnSummary = new QLabel(w);
    m_warnSummary->setWordWrap(true);
    lay->addWidget(m_warnSummary);
    auto *row = new QHBoxLayout();
    auto *btnUp = new QPushButton(QStringLiteral("↑"), w);
    auto *btnDown = new QPushButton(QStringLiteral("↓"), w);
    auto *btnManual = new QPushButton(lang("手输时间戳…", "Manual timestamp…"), w);
    m_btnConfirm = new QPushButton(lang("确认顺序并继续 →", "Confirm & continue →"), w);
    row->addWidget(btnUp);
    row->addWidget(btnDown);
    row->addWidget(btnManual);
    row->addStretch(1);
    row->addWidget(m_btnConfirm);
    lay->addLayout(row);
    connect(btnUp, &QPushButton::clicked, this, [this]() { onMoveRow(-1); });
    connect(btnDown, &QPushButton::clicked, this, [this]() { onMoveRow(1); });
    connect(btnManual, &QPushButton::clicked, this, &PreprocessPanel::onManualTimestamp);
    connect(m_btnConfirm, &QPushButton::clicked, this, &PreprocessPanel::onConfirmOrder);
    return w;
}

QWidget *PreprocessPanel::buildStepOptions()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);
    lay->addWidget(new QLabel(lang("③ 处理选项：一致性校验结果与输出设置",
                                   "Step 3: Consistency check & output options"), w));
    m_precheckView = new QPlainTextEdit(w);
    m_precheckView->setReadOnly(true);
    lay->addWidget(m_precheckView, 1);

    auto *dirRow = new QHBoxLayout();
    dirRow->addWidget(new QLabel(lang("输出目录：", "Output: "), w));
    m_outputDirEdit = new QLineEdit(w);
    m_outputDirEdit->setPlaceholderText(
        lang("留空 = 素材目录下 LumenArc_Transcode_<时间戳>",
             "Empty = <source>/LumenArc_Transcode_<timestamp>"));
    auto *btnBrowse = new QPushButton(lang("浏览…", "Browse…"), w);
    dirRow->addWidget(m_outputDirEdit, 1);
    dirRow->addWidget(btnBrowse);
    lay->addLayout(dirRow);
    connect(btnBrowse, &QPushButton::clicked, this, &PreprocessPanel::onBrowseOutput);

    auto *optRow = new QHBoxLayout();
    optRow->addWidget(new QLabel(lang("CRF：", "CRF: "), w));
    m_crfSpin = new QSpinBox(w);
    m_crfSpin->setRange(0, 51);
    m_crfSpin->setValue(18);
    optRow->addWidget(m_crfSpin);
    m_deinterlaceCheck = new QCheckBox(lang("反交错", "Deinterlace"), w);
    m_deinterlaceCheck->setChecked(true);
    optRow->addWidget(m_deinterlaceCheck);
    m_normalizeCheck = new QCheckBox(lang("时间戳归一化", "Normalize timestamps"), w);
    optRow->addWidget(m_normalizeCheck);
    m_ignoreWarnCheck = new QCheckBox(lang("忽略 WARN", "Ignore WARN"), w);
    optRow->addWidget(m_ignoreWarnCheck);
    m_sha256Check = new QCheckBox(lang("SHA-256", "SHA-256"), w);
    m_sha256Check->setChecked(true);
    optRow->addWidget(m_sha256Check);
    optRow->addStretch(1);
    lay->addLayout(optRow);

    m_btnStart = new QPushButton(lang("开始处理", "Start processing"), w);
    lay->addWidget(m_btnStart);
    connect(m_btnStart, &QPushButton::clicked, this, &PreprocessPanel::onStartProcessing);
    return w;
}

QWidget *PreprocessPanel::buildStepRun()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);
    lay->addWidget(new QLabel(lang("④ 执行与结果", "Step 4: Run & results"), w));
    m_progressBar = new QProgressBar(w);
    lay->addWidget(m_progressBar);
    m_progressLabel = new QLabel(w);
    lay->addWidget(m_progressLabel);
    m_logView = new QPlainTextEdit(w);
    m_logView->setReadOnly(true);
    m_logView->setFont(fontMono(9));
    lay->addWidget(m_logView, 1);
    m_resultLabel = new QLabel(w);
    m_resultLabel->setWordWrap(true);
    lay->addWidget(m_resultLabel);
    auto *row = new QHBoxLayout();
    m_btnCancel = new QPushButton(lang("取消", "Cancel"), w);
    m_btnOpenOutput = new QPushButton(lang("打开输出目录", "Open output folder"), w);
    m_btnOpenOutput->setEnabled(false);
    row->addWidget(m_btnCancel);
    row->addStretch(1);
    row->addWidget(m_btnOpenOutput);
    lay->addLayout(row);
    connect(m_btnCancel, &QPushButton::clicked, this, &PreprocessPanel::onCancel);
    connect(m_btnOpenOutput, &QPushButton::clicked, this, [this]() {
        if (!m_lastOutputDir.isEmpty())
            QDesktopServices::openUrl(QUrl::fromLocalFile(m_lastOutputDir));
    });
    return w;
}

// ---------------------------------------------------------------------------
// ① 素材
// ---------------------------------------------------------------------------
void PreprocessPanel::onAddFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(this,
        lang("添加素材", "Add clips"), QString(),
        lang("视频文件 (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.ts *.webm *.mpg *.mpeg);;所有文件 (*)",
             "Videos (*.mp4 *.avi *.mkv *.mov *.wmv *.flv *.ts *.webm *.mpg *.mpeg);;All (*)"));
    for (const QString &f : files) {
        // 重复添加去重（路径规范化，§8.2）
        bool dup = false;
        for (int i = 0; i < m_fileList->count(); ++i)
            if (QFileInfo(m_fileList->item(i)->text()).canonicalFilePath()
                == QFileInfo(f).canonicalFilePath())
                dup = true;
        if (!dup)
            m_fileList->addItem(f);
    }
    m_btnBegin->setEnabled(m_fileList->count() > 0);
}

void PreprocessPanel::onRemoveFiles()
{
    for (QListWidgetItem *item : m_fileList->selectedItems())
        delete item;
    m_btnBegin->setEnabled(m_fileList->count() > 0);
}

void PreprocessPanel::onBeginAnalysis()
{
    QStringList files;
    for (int i = 0; i < m_fileList->count(); ++i)
        files << m_fileList->item(i)->text();
    if (files.isEmpty())
        return;
    m_thumbCache.clear();
    m_logView->clear();
    m_resultLabel->clear();
    m_progressBar->setValue(0);
    m_coord->begin(files);
}

// ---------------------------------------------------------------------------
// ② 排序与证据
// ---------------------------------------------------------------------------
QPixmap PreprocessPanel::thumbnail(const QString &path)
{
    if (path.isEmpty())
        return {};
    auto it = m_thumbCache.find(path);
    if (it != m_thumbCache.end())
        return it.value();
    if (m_thumbCache.size() >= kThumbCacheCap)
        m_thumbCache.clear();   // 有界（C5）；简单清空重建
    QPixmap pm(path);
    if (!pm.isNull())
        pm = pm.scaled(192, 108, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_thumbCache.insert(path, pm);
    return pm;
}

QString PreprocessPanel::currentGroup() const
{
    auto *tab = qobject_cast<QTableWidget *>(m_groupTabs->currentWidget());
    return tab ? tab->property("channel").toString() : QString();
}

QString PreprocessPanel::selectedFile() const
{
    auto *tab = qobject_cast<QTableWidget *>(m_groupTabs->currentWidget());
    if (!tab || tab->currentRow() < 0)
        return {};
    auto *item = tab->item(tab->currentRow(), 0);
    return item ? item->data(Qt::UserRole).toString() : QString();
}

void PreprocessPanel::refreshEvidenceTables(const QVector<SortGroup> &groups)
{
    m_groupTabs->clear();
    int overlaps = 0, gaps = 0;
    qint64 overlapMs = 0, gapMs = 0;
    for (const auto &g : groups) {
        auto *tab = new QTableWidget(g.ordered.size(), 6, m_groupTabs);
        tab->setProperty("channel", g.channel);
        tab->setHorizontalHeaderLabels({
            QStringLiteral("#"), lang("首帧", "First"), lang("尾帧", "Last"),
            lang("首帧时间", "Start time"), lang("依据", "Source"), lang("状态", "Status")});
        tab->verticalHeader()->setDefaultSectionSize(64);
        tab->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);
        tab->setSelectionBehavior(QAbstractItemView::SelectRows);
        for (int i = 0; i < g.ordered.size(); ++i) {
            const SortEntry &e = g.ordered[i];
            auto *num = new QTableWidgetItem(QString::number(i + 1));
            num->setData(Qt::UserRole, e.filePath);
            num->setFlags(num->flags() & ~Qt::ItemIsEditable);
            tab->setItem(i, 0, num);

            auto *first = new QTableWidgetItem;
            first->setData(Qt::DecorationRole, thumbnail(e.thumbnailFirst));
            first->setFlags(first->flags() & ~Qt::ItemIsEditable);
            tab->setItem(i, 1, first);
            auto *last = new QTableWidgetItem;
            last->setData(Qt::DecorationRole, thumbnail(e.thumbnailLast));
            last->setFlags(last->flags() & ~Qt::ItemIsEditable);
            tab->setItem(i, 2, last);

            const QString timeStr = e.startMs > 0
                ? QDateTime::fromMSecsSinceEpoch(e.startMs, Qt::LocalTime)
                      .toString(QStringLiteral("MM-dd HH:mm:ss"))
                : lang("未知", "unknown");
            auto *time = new QTableWidgetItem(timeStr);
            time->setFlags(time->flags() & ~Qt::ItemIsEditable);
            tab->setItem(i, 3, time);

            QString src;
            switch (e.startSource) {
            case OcrResult::Ocr:    src = QStringLiteral("OCR"); break;
            case OcrResult::Manual: src = lang("人工", "Manual"); break;
            default:                src = lang("其他", "other"); break;
            }
            auto *srcIt = new QTableWidgetItem(src);
            srcIt->setFlags(srcIt->flags() & ~Qt::ItemIsEditable);
            tab->setItem(i, 4, srcIt);

            QString status = QStringLiteral("✓");
            for (const auto &w : g.warnings) {
                if (w.indexA == i || w.indexB == i) {
                    if (w.type == SortWarningType::Overlap)
                        status = QStringLiteral("⚠重叠");
                    else if (w.type == SortWarningType::Gap)
                        status = QStringLiteral("⚠缺口");
                    else
                        status = QStringLiteral("⚠");
                    break;
                }
            }
            auto *stIt = new QTableWidgetItem(status);
            stIt->setFlags(stIt->flags() & ~Qt::ItemIsEditable);
            tab->setItem(i, 5, stIt);
        }
        QString title = g.channel + (g.suspicious ? QStringLiteral(" ⚠存疑") : QString());
        m_groupTabs->addTab(tab, title);
        for (const auto &w : g.warnings) {
            if (w.type == SortWarningType::Overlap) { ++overlaps; overlapMs += -w.deltaMs; }
            if (w.type == SortWarningType::Gap)     { ++gaps;     gapMs += w.deltaMs; }
        }
    }
    m_warnSummary->setText(lang("⚠ 警告汇总：%1 处重叠（共 %2s）、%3 处缺口（共 %4s）",
                                "⚠ Warnings: %1 overlaps (%2s), %3 gaps (%4s)")
                               .arg(overlaps).arg(overlapMs / 1000.0, 0, 'f', 1)
                               .arg(gaps).arg(gapMs / 1000.0, 0, 'f', 1));
}

void PreprocessPanel::onMoveRow(int delta)
{
    auto *tab = qobject_cast<QTableWidget *>(m_groupTabs->currentWidget());
    if (!tab)
        return;
    const int row = tab->currentRow();
    const int target = row + delta;
    if (row < 0 || target < 0 || target >= tab->rowCount())
        return;
    // 组装新顺序 → Coordinator（SSOT）重排并重算连续性
    QStringList order;
    for (int i = 0; i < tab->rowCount(); ++i) {
        int src = i;
        if (i == row)
            src = target;
        else if (i == target)
            src = row;
        order << tab->item(src, 0)->data(Qt::UserRole).toString();
    }
    m_coord->applyGroupOrder(currentGroup(), order);
    tab->setCurrentCell(target, 0);
}

void PreprocessPanel::onManualTimestamp()
{
    const QString file = selectedFile();
    if (file.isEmpty())
        return;
    const OcrResult ocr = m_coord->ocrMap().value(file);

    QDialog dlg(this);
    dlg.setWindowTitle(lang("手输时间戳（看图录入）", "Manual timestamp (from frame)"));
    auto *lay = new QVBoxLayout(&dlg);
    auto *imgLay = new QHBoxLayout();
    for (const QString &imgPath : {ocr.firstFrameImg, ocr.lastFrameImg}) {
        auto *lbl = new QLabel(&dlg);
        QPixmap pm(imgPath);
        lbl->setPixmap(pm.isNull() ? QPixmap() : pm.scaledToWidth(360, Qt::SmoothTransformation));
        imgLay->addWidget(lbl);
    }
    lay->addLayout(imgLay);
    auto *edit = new QDateTimeEdit(QDateTime::currentDateTime(), &dlg);
    edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    edit->setCalendarPopup(true);
    lay->addWidget(edit);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() == QDialog::Accepted) {
        m_coord->applyManualTimestamp(
            file, edit->dateTime().toMSecsSinceEpoch());
    }
}

void PreprocessPanel::onConfirmOrder()
{
    m_coord->confirmOrder();
}

// ---------------------------------------------------------------------------
// ③ 处理选项
// ---------------------------------------------------------------------------
void PreprocessPanel::onBrowseOutput()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, lang("选择输出目录", "Choose output directory"));
    if (!dir.isEmpty())
        m_outputDirEdit->setText(dir);
}

void PreprocessPanel::onStartProcessing()
{
    ProcessingOptions opts;
    opts.outputDir = m_outputDirEdit->text().trimmed();
    opts.crf = m_crfSpin->value();
    opts.deinterlace = m_deinterlaceCheck->isChecked();
    opts.normalizeTimestamps = m_normalizeCheck->isChecked();
    opts.ignoreWarnings = m_ignoreWarnCheck->isChecked();
    opts.withSha256 = m_sha256Check->isChecked();
    m_coord->startProcessing(opts);
}

// ---------------------------------------------------------------------------
// ④ 执行与结果
// ---------------------------------------------------------------------------
void PreprocessPanel::onCancel()
{
    m_coord->cancel();
    m_btnCancel->setEnabled(false);
}

// ---------------------------------------------------------------------------
// Coordinator 接线
// ---------------------------------------------------------------------------
void PreprocessPanel::onPhaseChanged(TaskPhase phase)
{
    switch (phase) {
    case TaskPhase::Probing:
    case TaskPhase::Ocr:
        setStep(3);     // 执行页显示探测/OCR 进度
        m_btnCancel->setEnabled(true);
        break;
    case TaskPhase::UserConfirm:
        setStep(1);
        break;
    case TaskPhase::Precheck:
        setStep(2);
        break;
    case TaskPhase::Transcoding:
    case TaskPhase::Concat:
        setStep(3);
        m_btnCancel->setEnabled(true);
        break;
    default:
        break;
    }
}

void PreprocessPanel::setStep(int index)
{
    m_stack->setCurrentIndex(index);
}

void PreprocessPanel::onProgress(int percent, const QString &detail)
{
    m_progressBar->setValue(percent);
    m_progressLabel->setText(detail);
}

void PreprocessPanel::onProbeDone(const QVector<ProbeResult> &results)
{
    int bad = 0;
    for (const auto &r : results)
        if (!r.ok())
            ++bad;
    if (bad > 0)
        m_resultLabel->setText(lang("探测完成：%1 个文件失败（详见日志）",
                                    "Probe done: %1 file(s) failed (see log)").arg(bad));
}

void PreprocessPanel::onEvidenceReady(const QVector<SortGroup> &groups)
{
    refreshEvidenceTables(groups);
}

void PreprocessPanel::onPrecheckReady(const QMap<QString, PrecheckResult> &byGroup)
{
    QString text;
    for (auto it = byGroup.begin(); it != byGroup.end(); ++it) {
        text += QStringLiteral("══ 组 %1 ══\n").arg(it.key());
        for (const auto &item : it.value().items) {
            const QString tag = item.level == PrecheckLevel::Block
                ? QStringLiteral("[BLOCK]") : item.level == PrecheckLevel::Warn
                ? QStringLiteral("[WARN] ") : QStringLiteral("[OK]   ");
            text += tag + QLatin1Char(' ') + item.checkName
                + QStringLiteral(": ") + item.detail + QLatin1Char('\n');
        }
        if (it.value().hasBlock())
            text += lang("→ 存在 BLOCK 项：该组将先统一转码再拼接\n",
                         "-> BLOCK present: group will be transcoded first\n");
        text += QLatin1Char('\n');
    }
    m_precheckView->setPlainText(text);
}

void PreprocessPanel::onFinished(const PreprocessReport &report)
{
    setStep(3);
    m_progressBar->setValue(100);
    m_lastOutputDir = QFileInfo(report.reportCsvPath).absolutePath();
    m_resultLabel->setText(
        lang("✅ 处理完成\n输出：%1\n证据报告：%2",
             "✅ Done\nOutput: %1\nEvidence report: %2")
            .arg(report.outputPath, report.reportCsvPath));
    m_btnOpenOutput->setEnabled(true);
    m_btnCancel->setEnabled(false);
}

void PreprocessPanel::onFailed(PreprocessError error, const QString &detail)
{
    setStep(3);
    m_btnCancel->setEnabled(false);
    const QString msg = error == PreprocessError::Cancelled
        ? lang("已取消。证据保留于：%1", "Cancelled. Evidence kept at: %1").arg(detail)
        : lang("处理失败（%1）：%2", "Failed (%1): %2").arg(int(error)).arg(detail);
    m_resultLabel->setText(QStringLiteral("❌ ") + msg);
}

void PreprocessPanel::onLogLine(const QString &line)
{
    m_logView->appendPlainText(line);
}
