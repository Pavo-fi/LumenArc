/**
 * @file preprocesswindow.cpp
 * @brief 前处理-素材整理拼接独立任务窗口实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-02
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计见 docs/PREPROCESSING_UI_REDESIGN_CN.md。
 * 原则：结论先行、术语平实、技术细节默认折叠、证据/OCR 原文可见（F1-F6）。
 */
#include "preprocesswindow.h"
#include "cliptimelinewidget.h"
#include "i18n.h"
#include "theme.h"

#include <QStackedWidget>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QLineEdit>
#include <QSpinBox>
#include <QCheckBox>
#include <QRadioButton>
#include <QScrollArea>
#include <QFileDialog>
#include <QMessageBox>
#include <QFileInfo>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QUrl>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDrag>
#include <QMouseEvent>
#include <QStorageInfo>
#include <QCloseEvent>
#include <QRegularExpression>

namespace {
const char *kVideoExts[] = {"mp4", "avi", "wmv", "flv", "ts", "mts",
                            "m2ts", "dav", "mov", "mkv", "mpg", "mpeg", "3gp"};

bool isVideoFile(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    for (const char *e : kVideoExts)
        if (suffix == QLatin1String(e))
            return true;
    return false;
}

QString cardStyle(const QString &border)
{
    return QStringLiteral(
        "QFrame { background: %1; border: 2px solid %2; border-radius: 8px; }"
        "QLabel { border: none; background: transparent; }")
        .arg(Theme::BgCard, border);
}
} // namespace

// ---------------------------------------------------------------------------
PreprocessWindow::PreprocessWindow(IAnalysisEngine *analysis, QWidget *parent)
    : QMainWindow(parent)
    , m_coord(new PreprocessingCoordinator(this))
{
    setWindowTitle(lang("素材整理拼接", "Clip Organizer & Merger"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1120, 780);
    m_coord->setAnalysisEngine(analysis);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(8);
    root->addWidget(buildStepBar());
    m_stack = new QStackedWidget(central);
    m_stack->addWidget(buildPageImport());
    m_stack->addWidget(buildPageReview());
    m_stack->addWidget(buildPageSettings());
    m_stack->addWidget(buildPageRun());
    root->addWidget(m_stack, 1);
    setCentralWidget(central);

    connect(m_coord, &PreprocessingCoordinator::phaseChanged,
            this, &PreprocessWindow::onPhaseChanged);
    connect(m_coord, &PreprocessingCoordinator::progress,
            this, &PreprocessWindow::onProgress);
    connect(m_coord, &PreprocessingCoordinator::probeDone,
            this, &PreprocessWindow::onProbeDone);
    connect(m_coord, &PreprocessingCoordinator::evidenceReady,
            this, &PreprocessWindow::onEvidenceReady);
    connect(m_coord, &PreprocessingCoordinator::precheckReady,
            this, &PreprocessWindow::onPrecheckReady);
    connect(m_coord, &PreprocessingCoordinator::finished,
            this, &PreprocessWindow::onFinished);
    connect(m_coord, &PreprocessingCoordinator::failed,
            this, &PreprocessWindow::onFailed);
    connect(m_coord, &PreprocessingCoordinator::logLine,
            this, &PreprocessWindow::onLogLine);
    updateStepBar();
}

// ---------------------------------------------------------------------------
// 步骤条
// ---------------------------------------------------------------------------
QWidget *PreprocessWindow::buildStepBar()
{
    auto *bar = new QWidget(this);
    auto *lay = new QHBoxLayout(bar);
    lay->setContentsMargins(0, 0, 0, 0);
    lay->setSpacing(4);
    const QStringList labels = {
        lang("① 导入素材", "① Import"),
        lang("② 校对顺序", "② Review order"),
        lang("③ 拼接设置", "③ Merge settings"),
        lang("④ 执行与报告", "④ Run & report"),
    };
    for (int i = 0; i < labels.size(); ++i) {
        auto *btn = new QPushButton(labels[i], bar);
        btn->setFlat(true);
        btn->setMinimumHeight(34);
        btn->setCursor(Qt::PointingHandCursor);
        connect(btn, &QPushButton::clicked, this, [this, i]() {
            if (i <= m_maxReachedStep && m_coord->phase() != TaskPhase::Transcoding
                && m_coord->phase() != TaskPhase::Concat)
                setStep(i);
        });
        m_stepBtns.append(btn);
        lay->addWidget(btn);
        if (i < labels.size() - 1) {
            auto *sep = new QLabel(QStringLiteral("─"), bar);
            sep->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextMuted));
            lay->addWidget(sep);
        }
    }
    lay->addStretch(1);
    return bar;
}

void PreprocessWindow::updateStepBar()
{
    const bool locked = m_coord->phase() == TaskPhase::Transcoding
        || m_coord->phase() == TaskPhase::Concat
        || m_coord->phase() == TaskPhase::Probing
        || m_coord->phase() == TaskPhase::Ocr;
    for (int i = 0; i < m_stepBtns.size(); ++i) {
        const bool current = i == m_currentStep;
        const bool reachable = i <= m_maxReachedStep;
        m_stepBtns[i]->setEnabled(reachable && !locked);
        m_stepBtns[i]->setStyleSheet(QStringLiteral(
            "QPushButton { background: %1; color: %2; border-radius: 8px; "
            "padding: 4px 14px; font-weight: %3; }"
            "QPushButton:disabled { color: %4; background: transparent; }")
            .arg(current ? Theme::Accent : QStringLiteral("transparent"),
                 current ? Theme::AccentOnDark : Theme::TextPrimary,
                 current ? QStringLiteral("bold") : QStringLiteral("normal"),
                 Theme::TextMuted));
    }
}

void PreprocessWindow::setStep(int idx)
{
    m_currentStep = idx;
    m_stack->setCurrentIndex(idx);
    updateStepBar();
}

// ---------------------------------------------------------------------------
// ① 导入素材
// ---------------------------------------------------------------------------
QWidget *PreprocessWindow::buildPageImport()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);

    auto *hint = new QFrame(w);
    hint->setFrameShape(QFrame::StyledPanel);
    hint->setStyleSheet(QStringLiteral(
        "QFrame { border: 2px dashed %1; border-radius: 10px; background: %2; }"
        "QLabel { border: none; }").arg(Theme::Border, Theme::BgPanel));
    hint->setMinimumHeight(96);
    auto *hintLay = new QVBoxLayout(hint);
    auto *hintLbl = new QLabel(lang("把监控录像文件拖到这里\n或点击下方「添加文件」",
                                    "Drop surveillance video files here\nor click Add below"), hint);
    hintLbl->setAlignment(Qt::AlignCenter);
    hintLbl->setStyleSheet(QStringLiteral("color:%1; font-size:15px;")
                               .arg(Theme::TextSecond));
    hintLay->addWidget(hintLbl);
    lay->addWidget(hint);

    m_importSummary = new QLabel(lang("尚未导入文件", "No files imported"), w);
    lay->addWidget(m_importSummary);

    m_fileTable = new QTableWidget(0, 6, w);
    m_fileTable->setHorizontalHeaderLabels({
        lang("文件名", "File"), lang("时长", "Duration"),
        lang("分辨率", "Resolution"), lang("格式", "Format"),
        lang("大小", "Size"), lang("状态", "Status")});
    m_fileTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fileTable->verticalHeader()->setVisible(false);
    m_fileTable->setSelectionMode(QAbstractItemView::NoSelection);
    m_fileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    lay->addWidget(m_fileTable, 1);

    m_importProgress = new QProgressBar(w);
    m_importProgress->setVisible(false);
    lay->addWidget(m_importProgress);
    m_importStatus = new QLabel(w);
    m_importStatus->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    lay->addWidget(m_importStatus);

    auto *row = new QHBoxLayout();
    auto *btnAdd = new QPushButton(lang("添加文件…", "Add files…"), w);
    auto *btnClear = new QPushButton(lang("清空重选", "Clear all"), w);
    m_btnBeginSort = new QPushButton(lang("下一步：自动排序 →", "Next: auto sort →"), w);
    m_btnBeginSort->setEnabled(false);
    m_btnBeginSort->setMinimumHeight(36);
    m_btnBeginSort->setStyleSheet(QStringLiteral(
        "QPushButton { background:%1; color:%2; font-weight:bold; border-radius:8px; }"
        "QPushButton:disabled { background:%3; color:%4; }")
        .arg(Theme::Accent, Theme::AccentOnDark, Theme::BgCard, Theme::TextMuted));
    row->addWidget(btnAdd);
    row->addWidget(btnClear);
    row->addStretch(1);
    row->addWidget(m_btnBeginSort);
    lay->addLayout(row);

    connect(btnAdd, &QPushButton::clicked, this, &PreprocessWindow::onAddFiles);
    connect(btnClear, &QPushButton::clicked, this, &PreprocessWindow::onClearFiles);
    connect(m_btnBeginSort, &QPushButton::clicked, this, &PreprocessWindow::onBeginSort);
    return w;
}

void PreprocessWindow::onAddFiles()
{
    const QStringList files = QFileDialog::getOpenFileNames(
        this, lang("选择监控录像片段", "Choose surveillance clips"),
        QString(), lang("视频文件 (*.mp4 *.avi *.wmv *.flv *.ts *.mts *.m2ts "
                        "*.dav *.mov *.mkv *.mpg *.mpeg *.3gp);;所有文件 (*)",
                        "Video files (*.mp4 *.avi *.wmv *.flv *.ts *.mts *.m2ts "
                        "*.dav *.mov *.mkv *.mpg *.mpeg *.3gp);;All files (*)"));
    addFiles(files);
}

void PreprocessWindow::addFiles(const QStringList &files)
{
    if (m_coord->phase() != TaskPhase::Idle && m_coord->phase() != TaskPhase::Done
        && m_coord->phase() != TaskPhase::Failed
        && m_coord->phase() != TaskPhase::Cancelled) {
        QMessageBox::information(this, windowTitle(),
            lang("当前任务进行中，请先完成或取消。", "A task is running; finish or cancel it first."));
        return;
    }
    int added = 0;
    for (const QString &f : files) {
        if (!isVideoFile(f) || m_pendingFiles.contains(f))
            continue;
        m_pendingFiles.append(f);
        ++added;
    }
    if (added == 0 && !files.isEmpty())
        return;

    m_fileTable->setRowCount(m_pendingFiles.size());
    qint64 totalBytes = 0;
    for (int i = 0; i < m_pendingFiles.size(); ++i) {
        const QString &f = m_pendingFiles[i];
        const QFileInfo fi(f);
        totalBytes += fi.size();
        auto *name = new QTableWidgetItem(fi.fileName());
        name->setData(Qt::UserRole, f);
        name->setToolTip(f);
        m_fileTable->setItem(i, 0, name);
        for (int c = 1; c <= 3; ++c)
            m_fileTable->setItem(i, c, new QTableWidgetItem(QStringLiteral("—")));
        m_fileTable->setItem(i, 4, new QTableWidgetItem(fmtBytes(fi.size())));
        m_fileTable->setItem(i, 5, new QTableWidgetItem(lang("待分析", "pending")));
    }
    m_importSummary->setText(lang("已导入 %1 段（共 %2）",
                                  "%1 clip(s) imported (%2 total)")
                                 .arg(m_pendingFiles.size())
                                 .arg(fmtBytes(totalBytes)));
    m_btnBeginSort->setEnabled(!m_pendingFiles.isEmpty());
    m_importStatus->clear();
}

void PreprocessWindow::onClearFiles()
{
    if (m_coord->phase() != TaskPhase::Idle && m_coord->phase() != TaskPhase::Done
        && m_coord->phase() != TaskPhase::Failed
        && m_coord->phase() != TaskPhase::Cancelled)
        return;
    m_pendingFiles.clear();
    m_fileTable->setRowCount(0);
    m_importSummary->setText(lang("尚未导入文件", "No files imported"));
    m_btnBeginSort->setEnabled(false);
    m_importStatus->clear();
    m_maxReachedStep = 0;
    setStep(0);
}

void PreprocessWindow::onBeginSort()
{
    if (m_pendingFiles.isEmpty())
        return;
    m_importProgress->setVisible(true);
    m_importProgress->setValue(0);
    m_btnBeginSort->setEnabled(false);
    m_importStatus->setText(lang("正在分析素材（识别画面时间）…",
                                 "Analyzing clips (reading on-screen time)…"));
    m_coord->begin(m_pendingFiles);
    updateStepBar();
}

// ---------------------------------------------------------------------------
// ② 校对顺序
// ---------------------------------------------------------------------------
QWidget *PreprocessWindow::buildPageReview()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);

    m_reviewSummary = new QLabel(w);
    m_reviewSummary->setWordWrap(true);
    m_reviewSummary->setStyleSheet(QStringLiteral(
        "color:%1; font-size:14px; padding:6px; background:%2; border-radius:6px;")
        .arg(Theme::TextPrimary, Theme::BgPanel));
    lay->addWidget(m_reviewSummary);

    m_timeline = new ClipTimelineWidget(w);
    lay->addWidget(m_timeline);
    connect(m_timeline, &ClipTimelineWidget::clipClicked,
            this, &PreprocessWindow::onCardClicked);

    m_cardScroll = new QScrollArea(w);
    m_cardScroll->setWidgetResizable(true);
    m_cardScroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_cardScroll->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    m_cardScroll->setMinimumHeight(400);
    m_cardHost = new QWidget(m_cardScroll);
    new QHBoxLayout(m_cardHost);
    m_cardHost->layout()->setAlignment(Qt::AlignLeft | Qt::AlignVCenter);
    m_cardHost->setAcceptDrops(true);
    m_cardHost->installEventFilter(this);
    m_cardScroll->setWidget(m_cardHost);
    lay->addWidget(m_cardScroll, 1);

    auto *row = new QHBoxLayout();
    auto *btnLeft = new QPushButton(QStringLiteral("←"), w);
    auto *btnRight = new QPushButton(QStringLiteral("→"), w);
    btnLeft->setToolTip(lang("选中片段前移", "Move selected earlier"));
    btnRight->setToolTip(lang("选中片段后移", "Move selected later"));
    auto *btnManual = new QPushButton(lang("手动输入时间…", "Enter time manually…"), w);
    auto *btnRe = new QPushButton(lang("重新识别", "Re-identify"), w);
    auto *btnConfirm = new QPushButton(lang("确认顺序，下一步 →", "Confirm order, next →"), w);
    btnConfirm->setMinimumHeight(36);
    btnConfirm->setStyleSheet(m_btnBeginSort->styleSheet());
    row->addWidget(btnLeft);
    row->addWidget(btnRight);
    row->addWidget(btnManual);
    row->addWidget(btnRe);
    row->addStretch(1);
    row->addWidget(btnConfirm);
    lay->addLayout(row);

    connect(btnLeft, &QPushButton::clicked, this, [this]() { onMoveSelected(-1); });
    connect(btnRight, &QPushButton::clicked, this, [this]() { onMoveSelected(+1); });
    connect(btnManual, &QPushButton::clicked, this, &PreprocessWindow::onManualTimestamp);
    connect(btnRe, &QPushButton::clicked, this, &PreprocessWindow::onReIdentify);
    connect(btnConfirm, &QPushButton::clicked, this, &PreprocessWindow::onConfirmOrder);
    return w;
}

void PreprocessWindow::rebuildReviewViews()
{
    // --- 时间线 ---
    QVector<TimelineClip> clips;
    for (int gi = 0; gi < m_groups.size(); ++gi) {
        const auto &g = m_groups[gi];
        for (int i = 0; i < g.ordered.size(); ++i) {
            const SortEntry &e = g.ordered[i];
            TimelineClip c;
            c.filePath = e.filePath;
            c.startMs = e.startMs;
            c.durationMs = e.durationMs;
            c.timeKnown = e.startMs > 0 && e.durationMs > 0;
            c.groupIndex = gi;
            c.displayIndex = i + 1;
            for (const auto &w : g.warnings)
                if (w.type == SortWarningType::DurationDubious && w.indexA == i)
                    c.dubious = true;
            clips.append(c);
        }
    }
    m_timeline->setClips(clips, 2000);
    m_timeline->setSelectedPath(m_selectedPath);

    // --- 卡片 ---
    auto *hostLay = qobject_cast<QHBoxLayout *>(m_cardHost->layout());
    while (QLayoutItem *it = hostLay->takeAt(0)) {
        if (it->widget())
            it->widget()->deleteLater();
        delete it;
    }
    for (int gi = 0; gi < m_groups.size(); ++gi) {
        const auto &g = m_groups[gi];
        if (m_groups.size() > 1) {
            auto *hdr = new QLabel(lang("输出分组 %1：%2\n（本组将拼接为一个文件）",
                                        "Output group %1: %2\n(merged into one file)")
                                       .arg(gi + 1).arg(g.channel), m_cardHost);
            hdr->setStyleSheet(QStringLiteral("color:%1; padding:0 10px;")
                                   .arg(Theme::TextSecond));
            hdr->setAlignment(Qt::AlignCenter);
            hostLay->addWidget(hdr);
        }
        for (int i = 0; i < g.ordered.size(); ++i)
            hostLay->addWidget(makeCard(g.ordered[i], gi, g.channel));
    }
    hostLay->addStretch(1);
    refreshReviewSummary();
}

void PreprocessWindow::refreshReviewSummary()
{
    int total = 0, ocrCount = 0, manualCount = 0, unknownCount = 0;
    int gaps = 0, overlaps = 0;
    qint64 gapMs = 0, overlapMs = 0;
    int dubious = 0;
    for (const auto &g : m_groups) {
        total += g.ordered.size();
        for (const auto &e : g.ordered) {
            if (e.startSource == OcrResult::Ocr)
                ++ocrCount;
            else if (e.startSource == OcrResult::Manual)
                ++manualCount;
            if (e.startMs <= 0)
                ++unknownCount;
        }
        for (const auto &w : g.warnings) {
            if (w.type == SortWarningType::Gap) { ++gaps; gapMs += w.deltaMs; }
            else if (w.type == SortWarningType::Overlap) { ++overlaps; overlapMs += -w.deltaMs; }
            else if (w.type == SortWarningType::DurationDubious) ++dubious;
        }
    }
    QString basis;
    if (ocrCount > 0)
        basis = lang("已按画面时间自动排序", "Auto-sorted by on-screen time");
    else if (manualCount > 0)
        basis = lang("已按人工输入时间排序", "Sorted by manual times");
    else
        basis = lang("无法识别画面时间，按文件信息排序（请人工核对）",
                     "On-screen time unavailable; sorted by file info (please verify)");
    QStringList issues;
    if (gaps > 0)
        issues << lang("%1 处缺口（共约 %2）", "%1 gap(s) (~%2 total)")
                      .arg(gaps).arg(fmtDuration(gapMs));
    if (overlaps > 0)
        issues << lang("%1 处重叠（共 %2）", "%1 overlap(s) (%2 total)")
                      .arg(overlaps).arg(fmtDuration(overlapMs));
    if (unknownCount > 0)
        issues << lang("%1 段时间未知（排在末尾，需人工输入）",
                       "%1 clip(s) with unknown time (at end, need manual input)")
                      .arg(unknownCount);
    if (dubious > 0)
        issues << lang("%1 段时长存疑（可能截断）", "%1 clip(s) dubious duration")
                      .arg(dubious);
    const QString icon = issues.isEmpty() ? QStringLiteral("✓")
                                          : QStringLiteral("⚠");
    m_reviewSummary->setText(QStringLiteral("%1 %2：%3 段%4%5")
        .arg(icon, basis)
        .arg(total)
        .arg(issues.isEmpty() ? lang("，时间连续", ", continuous") : QStringLiteral("，"))
        .arg(issues.join(lang("；", "; "))));
}

QWidget *PreprocessWindow::makeCard(const SortEntry &e, int groupIdx,
                                    const QString &channel)
{
    Q_UNUSED(groupIdx);
    bool dubious = false;
    for (const auto &g : m_groups) {
        if (g.channel != channel)
            continue;
        for (int i = 0; i < g.ordered.size(); ++i)
            if (g.ordered[i].filePath == e.filePath)
                for (const auto &w : g.warnings)
                    if (w.type == SortWarningType::DurationDubious && w.indexA == i)
                        dubious = true;
    }

    auto *card = new QFrame(m_cardHost);
    card->setProperty("clip", e.filePath);
    card->setProperty("chan", channel);
    card->setFixedWidth(246);
    card->setCursor(Qt::OpenHandCursor);
    card->installEventFilter(this);

    const bool needManual = e.startMs <= 0;
    const QString baseBorder = needManual || dubious ? Theme::Danger : Theme::Border;
    card->setProperty("baseBorder", baseBorder);
    card->setStyleSheet(cardStyle(baseBorder));

    auto *lay = new QVBoxLayout(card);
    lay->setSpacing(4);
    auto *name = new QLabel(QFileInfo(e.filePath).fileName(), card);
    name->setStyleSheet(QStringLiteral("color:%1; font-weight:bold;")
                            .arg(Theme::TextPrimary));
    lay->addWidget(name);

    const auto mkImg = [&](const QPixmap &pm) {
        auto *lbl = new QLabel(card);
        lbl->setFixedSize(230, 130);
        lbl->setAlignment(Qt::AlignCenter);
        lbl->setStyleSheet(QStringLiteral("background:#000; border-radius:4px;"));
        if (!pm.isNull())
            lbl->setPixmap(pm);
        else
            lbl->setText(lang("无截图", "no frame"));
        lay->addWidget(lbl);
    };
    mkImg(thumb(e.thumbnailFirst));
    const auto fmtT = [](qint64 ms) {
        return QDateTime::fromMSecsSinceEpoch(ms, Qt::LocalTime)
            .toString(QStringLiteral("MM-dd HH:mm:ss"));
    };
    auto *startLbl = new QLabel(
        lang("起 ", "S ") + (e.startMs > 0 ? fmtT(e.startMs)
                                           : lang("未知", "unknown")), card);
    startLbl->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextPrimary));
    lay->addWidget(startLbl);
    mkImg(thumb(e.thumbnailLast));
    QString endText;
    if (e.ocrEndMs > 0)
        endText = lang("止 ", "E ") + fmtT(e.ocrEndMs);
    else if (e.startMs > 0 && e.durationMs > 0)
        endText = lang("止 ~", "E ~") + fmtT(e.startMs + e.durationMs)
            + lang("（推算）", " (est.)");
    else
        endText = lang("止 未知", "E unknown");
    auto *endLbl = new QLabel(endText, card);
    endLbl->setStyleSheet(QStringLiteral("color:%1;").arg(
        e.ocrEndMs > 0 ? Theme::TextPrimary : Theme::TextMuted));
    lay->addWidget(endLbl);

    QString badge, badgeColor;
    switch (e.startSource) {
    case OcrResult::Ocr:
        badge = lang("✓ 画面时间识别", "✓ On-screen time");
        badgeColor = Theme::Success;
        break;
    case OcrResult::Manual:
        badge = lang("✓ 人工确认", "✓ Manual");
        badgeColor = Theme::Success;
        break;
    default:
        badge = lang("⚠ 需人工输入时间", "⚠ Manual input needed");
        badgeColor = Theme::Danger;
        break;
    }
    if (dubious)
        badge += lang("　⚠ 时长存疑", "  ⚠ duration dubious");
    auto *badgeLbl = new QLabel(badge, card);
    badgeLbl->setStyleSheet(QStringLiteral("color:%1;").arg(badgeColor));
    lay->addWidget(badgeLbl);

    QString tip = e.filePath;
    if (!e.rawStartText.isEmpty())
        tip += lang("\n首帧识别原文：", "\nHead OCR raw: ") + e.rawStartText;
    if (!e.rawEndText.isEmpty())
        tip += lang("\n尾帧识别原文：", "\nTail OCR raw: ") + e.rawEndText;
    tip += lang("\n双击查看大图 / 拖动调整顺序", "\nDouble-click to enlarge / drag to reorder");
    card->setToolTip(tip);
    return card;
}

QPixmap PreprocessWindow::thumb(const QString &path)
{
    if (path.isEmpty())
        return {};
    const QString key = path + QStringLiteral("#230");
    auto it = m_thumbCache.find(key);
    if (it != m_thumbCache.end())
        return it.value();
    if (m_thumbCache.size() >= kThumbCacheCap)
        m_thumbCache.clear();
    QPixmap pm(path);
    if (!pm.isNull())
        pm = pm.scaled(230, 130, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    m_thumbCache.insert(key, pm);
    return pm;
}

void PreprocessWindow::selectCard(const QString &filePath, bool scrollTo)
{
    m_selectedPath = filePath;
    m_timeline->setSelectedPath(filePath);
    const QString sel = cardStyle(Theme::Accent);
    for (auto *card : m_cardHost->findChildren<QFrame *>()) {
        if (!card->property("clip").isValid())
            continue;
        if (card->property("clip").toString() == filePath) {
            card->setStyleSheet(sel);
            if (scrollTo)
                m_cardScroll->ensureWidgetVisible(card, 120, 0);
        } else {
            card->setStyleSheet(cardStyle(
                card->property("baseBorder").toString()));
        }
    }
}

QString PreprocessWindow::selectedFile() const
{
    return m_selectedPath;
}

void PreprocessWindow::onCardClicked(const QString &filePath)
{
    selectCard(filePath, true);
}

void PreprocessWindow::onMoveSelected(int delta)
{
    const QString file = selectedFile();
    if (file.isEmpty())
        return;
    for (const auto &g : m_groups) {
        const int idx = [&]() {
            for (int i = 0; i < g.ordered.size(); ++i)
                if (g.ordered[i].filePath == file)
                    return i;
            return -1;
        }();
        if (idx < 0)
            continue;
        const int target = idx + delta;
        if (target < 0 || target >= g.ordered.size())
            return;
        QStringList order;
        for (int i = 0; i < g.ordered.size(); ++i) {
            int src = i;
            if (i == idx) src = target;
            else if (i == target) src = idx;
            order << g.ordered[src].filePath;
        }
        m_coord->applyGroupOrder(g.channel, order);
        return;
    }
}

void PreprocessWindow::showManualDialog(const QString &filePath)
{
    if (filePath.isEmpty())
        return;
    const OcrResult ocr = m_coord->ocrMap().value(filePath);

    QDialog dlg(this);
    dlg.setWindowTitle(lang("核对画面并输入时间", "Verify frame & enter time"));
    auto *lay = new QVBoxLayout(&dlg);
    for (const QString &img : {ocr.firstFrameImg, ocr.lastFrameImg}) {
        if (img.isEmpty())
            continue;
        auto *lbl = new QLabel(&dlg);
        QPixmap pm(img);
        if (!pm.isNull())
            lbl->setPixmap(pm.scaledToWidth(760, Qt::SmoothTransformation));
        lay->addWidget(lbl);
    }
    if (!ocr.rawStartText.isEmpty())
        lay->addWidget(new QLabel(lang("首帧识别原文：%1", "Head OCR raw: %1")
                                      .arg(ocr.rawStartText), &dlg));
    if (!ocr.rawEndText.isEmpty())
        lay->addWidget(new QLabel(lang("尾帧识别原文：%1", "Tail OCR raw: %1")
                                      .arg(ocr.rawEndText), &dlg));
    auto *edit = new QDateTimeEdit(QDateTime::currentDateTime(), &dlg);
    edit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    edit->setCalendarPopup(true);
    if (ocr.wallStartMs > 0)
        edit->setDateTime(QDateTime::fromMSecsSinceEpoch(ocr.wallStartMs, Qt::LocalTime));
    lay->addWidget(edit);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    lay->addWidget(btns);
    connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() == QDialog::Accepted)
        m_coord->applyManualTimestamp(filePath, edit->dateTime().toMSecsSinceEpoch());
}

void PreprocessWindow::onManualTimestamp()
{
    showManualDialog(selectedFile());
}

void PreprocessWindow::onReIdentify()
{
    if (m_pendingFiles.isEmpty())
        return;
    const auto ret = QMessageBox::question(this, windowTitle(),
        lang("重新识别将重复画面时间识别过程，确定继续？",
             "Re-identify will repeat on-screen time recognition. Continue?"));
    if (ret != QMessageBox::Yes)
        return;
    m_coord->cancel();              // → Cancelled 态后允许重新 begin
    m_importProgress->setVisible(true);
    m_importProgress->setValue(0);
    setStep(0);
    m_coord->begin(m_pendingFiles);
}

void PreprocessWindow::onConfirmOrder()
{
    m_coord->confirmOrder();
}

// ---------------------------------------------------------------------------
// ③ 拼接设置
// ---------------------------------------------------------------------------
QWidget *PreprocessWindow::buildPageSettings()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);

    lay->addWidget(new QLabel(lang("拼接方式（已根据片段参数自动判定）：",
                                   "Merge method (auto-determined):"), w));
    m_radioLossless = new QRadioButton(
        lang("无损拼接（推荐）— 直接拼接不重编码，画质零损失",
             "Lossless concat (recommended) — no re-encode, zero quality loss"), w);
    m_radioTranscode = new QRadioButton(
        lang("统一格式后拼接 — 片段参数不一致，需要先统一格式",
             "Normalize then concat — clips differ, format unification required"), w);
    m_radioLossless->setEnabled(false);
    m_radioTranscode->setEnabled(false);
    lay->addWidget(m_radioLossless);
    lay->addWidget(m_radioTranscode);
    m_modeReason = new QLabel(w);
    m_modeReason->setWordWrap(true);
    m_modeReason->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    lay->addWidget(m_modeReason);

    m_btnDetails = new QPushButton(lang("▸ 技术详情", "▸ Technical details"), w);
    m_btnDetails->setFlat(true);
    m_btnDetails->setStyleSheet(QStringLiteral("color:%1; text-align:left;")
                                    .arg(Theme::TextSecond));
    m_precheckDetail = new QPlainTextEdit(w);
    m_precheckDetail->setReadOnly(true);
    m_precheckDetail->setVisible(false);
    m_precheckDetail->setMaximumHeight(200);
    lay->addWidget(m_btnDetails);
    lay->addWidget(m_precheckDetail);

    auto *outRow = new QHBoxLayout();
    outRow->addWidget(new QLabel(lang("输出文件夹：", "Output folder:"), w));
    m_outputDirEdit = new QLineEdit(w);
    auto *btnBrowse = new QPushButton(lang("浏览…", "Browse…"), w);
    outRow->addWidget(m_outputDirEdit, 1);
    outRow->addWidget(btnBrowse);
    lay->addLayout(outRow);
    m_outputNames = new QLabel(w);
    m_outputNames->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    lay->addWidget(m_outputNames);
    m_diskEstimate = new QLabel(w);
    lay->addWidget(m_diskEstimate);

    m_btnAdvanced = new QPushButton(lang("▸ 高级选项", "▸ Advanced options"), w);
    m_btnAdvanced->setFlat(true);
    m_btnAdvanced->setStyleSheet(m_btnDetails->styleSheet());
    m_advancedHost = new QWidget(w);
    auto *advLay = new QVBoxLayout(m_advancedHost);
    advLay->setContentsMargins(16, 0, 0, 0);
    m_normalizeCheck = new QCheckBox(
        lang("时间戳归一化（修复重叠/乱序时间轴，改变输出时间戳，默认关）",
             "Timestamp normalization (rewrites output timestamps; default off)"), w);
    m_deinterlaceCheck = new QCheckBox(
        lang("隔行源反交错（转码时生效）", "Deinterlace interlaced sources (transcode only)"), w);
    m_deinterlaceCheck->setChecked(true);
    m_sha256Check = new QCheckBox(
        lang("证据报告包含源文件 SHA-256 哈希", "Include SHA-256 of sources in evidence report"), w);
    m_sha256Check->setChecked(true);
    m_ignoreWarnCheck = new QCheckBox(
        lang("存在注意事项（WARN）时仍继续执行", "Proceed despite WARN items"), w);
    auto *crfRow = new QHBoxLayout();
    crfRow->addWidget(new QLabel(lang("转码画质 CRF（0-51，越小越清晰）：",
                                      "Transcode CRF (0-51, lower=better):"), w));
    m_crfSpin = new QSpinBox(w);
    m_crfSpin->setRange(0, 51);
    m_crfSpin->setValue(18);
    crfRow->addWidget(m_crfSpin);
    crfRow->addStretch(1);
    advLay->addWidget(m_normalizeCheck);
    advLay->addWidget(m_deinterlaceCheck);
    advLay->addWidget(m_sha256Check);
    advLay->addWidget(m_ignoreWarnCheck);
    advLay->addLayout(crfRow);
    m_advancedHost->setVisible(false);
    lay->addWidget(m_btnAdvanced);
    lay->addWidget(m_advancedHost);

    lay->addStretch(1);
    auto *row = new QHBoxLayout();
    auto *btnBack = new QPushButton(lang("← 返回修改顺序", "← Back to review"), w);
    auto *btnStart = new QPushButton(lang("开始拼接 ▶", "Start merging ▶"), w);
    btnStart->setMinimumHeight(38);
    btnStart->setStyleSheet(m_btnBeginSort->styleSheet());
    row->addWidget(btnBack);
    row->addStretch(1);
    row->addWidget(btnStart);
    lay->addLayout(row);

    connect(m_btnDetails, &QPushButton::clicked, this, &PreprocessWindow::onToggleDetails);
    connect(m_btnAdvanced, &QPushButton::clicked, this, &PreprocessWindow::onToggleAdvanced);
    connect(btnBrowse, &QPushButton::clicked, this, &PreprocessWindow::onBrowseOutput);
    connect(btnBack, &QPushButton::clicked, this, [this]() { setStep(1); });
    connect(btnStart, &QPushButton::clicked, this, &PreprocessWindow::onStartProcessing);
    connect(m_outputDirEdit, &QLineEdit::textChanged,
            this, &PreprocessWindow::updateDiskEstimate);
    return w;
}

void PreprocessWindow::onToggleDetails()
{
    const bool show = !m_precheckDetail->isVisible();
    m_precheckDetail->setVisible(show);
    m_btnDetails->setText(show ? lang("▾ 技术详情", "▾ Technical details")
                               : lang("▸ 技术详情", "▸ Technical details"));
}

void PreprocessWindow::onToggleAdvanced()
{
    const bool show = !m_advancedHost->isVisible();
    m_advancedHost->setVisible(show);
    m_btnAdvanced->setText(show ? lang("▾ 高级选项", "▾ Advanced options")
                                : lang("▸ 高级选项", "▸ Advanced options"));
}

void PreprocessWindow::onBrowseOutput()
{
    const QString dir = QFileDialog::getExistingDirectory(
        this, lang("选择输出文件夹", "Choose output folder"), m_outputDirEdit->text());
    if (!dir.isEmpty())
        m_outputDirEdit->setText(dir);
}

void PreprocessWindow::updateSettingsPage()
{
    const auto prechecks = m_coord->precheckMap();
    m_anyBlock = false;
    QString firstBlock;
    int blockCount = 0;
    for (auto it = prechecks.begin(); it != prechecks.end(); ++it) {
        for (const auto &item : it.value().items) {
            if (item.level == PrecheckLevel::Block) {
                ++blockCount;
                if (firstBlock.isEmpty())
                    firstBlock = item.detail;
            }
        }
        m_anyBlock = m_anyBlock || it.value().hasBlock();
    }
    m_radioLossless->setChecked(!m_anyBlock);
    m_radioTranscode->setChecked(m_anyBlock);
    if (m_anyBlock) {
        m_modeReason->setText(
            lang("ⓘ 检测到 %1 项不一致（如：%2），不能直接拼接，将统一格式后拼接",
                 "ⓘ %1 inconsistency(ies) detected (e.g. %2); will normalize format first")
                .arg(blockCount).arg(firstBlock));
    } else {
        int total = 0;
        for (const auto &g : m_groups)
            total += g.ordered.size();
        m_modeReason->setText(
            lang("ⓘ %1 段参数一致，可直接无损拼接（速度快、画质零损失）",
                 "ⓘ %1 clips share identical parameters; lossless concat applies")
                .arg(total));
    }

    // 技术详情（折叠内保留 BLOCK/WARN 原始分级）
    QString text;
    for (auto it = prechecks.begin(); it != prechecks.end(); ++it) {
        text += QStringLiteral("══ %1 ══\n").arg(it.key());
        for (const auto &item : it.value().items) {
            const QString tag = item.level == PrecheckLevel::Block
                ? QStringLiteral("[BLOCK]") : item.level == PrecheckLevel::Warn
                ? QStringLiteral("[WARN] ") : QStringLiteral("[OK]   ");
            text += tag + QLatin1Char(' ') + item.checkName
                + QStringLiteral(": ") + item.detail + QLatin1Char('\n');
        }
        text += QLatin1Char('\n');
    }
    m_precheckDetail->setPlainText(text);

    // 默认输出目录 + 输出文件名预告（与协调器 allocateOutput 规则一致）
    if (m_outputDirEdit->text().trimmed().isEmpty() && !m_pendingFiles.isEmpty()) {
        m_outputDirEdit->setText(
            QFileInfo(m_pendingFiles.first()).absolutePath()
            + QStringLiteral("/LumenArc_Merged_")
            + QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd_HHmm")));
    }
    QStringList names;
    for (const auto &g : m_groups) {
        QString safe = g.channel;
        safe.replace(QRegularExpression(QStringLiteral(R"([\\/:*?"<>|()])")),
                     QStringLiteral("_"));
        names << safe + QStringLiteral("_concat.mp4");
        if (names.size() >= 3) {
            names << QStringLiteral("…");
            break;
        }
    }
    m_outputNames->setText(lang("输出文件：%1（如重名自动追加序号；转码中间文件 "
                                "<原名>_lumen.mp4 在完成后清理）",
                                "Output: %1 (auto-numbered on conflict)")
                               .arg(names.join(QStringLiteral("、"))));
    updateDiskEstimate();
}

void PreprocessWindow::updateDiskEstimate()
{
    qint64 inputBytes = 0;
    for (const QString &f : m_pendingFiles)
        inputBytes += QFileInfo(f).size();
    const qint64 estimate = qint64(inputBytes * (m_anyBlock ? 1.2 : 1.05));
    QString dir = m_outputDirEdit->text().trimmed();
    QStorageInfo storage(dir);
    if (!storage.isValid() || storage.bytesAvailable() <= 0) {
        // 目录尚不存在时向上找已存在的父目录
        QString d = dir;
        while (!d.isEmpty()) {
            storage = QStorageInfo(d);
            if (storage.isValid() && storage.bytesAvailable() > 0)
                break;
            const int cut = d.lastIndexOf(QLatin1Char('/'));
            d = cut > 0 ? d.left(cut) : QString();
        }
    }
    if (storage.isValid() && storage.bytesAvailable() > 0) {
        const bool enough = storage.bytesAvailable() >= estimate;
        m_diskEstimate->setText(
            lang("预计输出约 %1，磁盘可用 %2 %3",
                 "Estimated output %1, available %2 %3")
                .arg(fmtBytes(estimate), fmtBytes(storage.bytesAvailable()),
                     enough ? QStringLiteral("✓") : QStringLiteral("⚠ 空间不足")));
        m_diskEstimate->setStyleSheet(QStringLiteral("color:%1;").arg(
            enough ? Theme::Success : Theme::Danger));
    } else {
        m_diskEstimate->setText(lang("预计输出约 %1（无法检测磁盘余量）",
                                     "Estimated output %1 (disk space unknown)")
                                    .arg(fmtBytes(estimate)));
        m_diskEstimate->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    }
}

void PreprocessWindow::onStartProcessing()
{
    ProcessingOptions opts;
    opts.outputDir = m_outputDirEdit->text().trimmed();
    opts.crf = m_crfSpin->value();
    opts.deinterlace = m_deinterlaceCheck->isChecked();
    opts.normalizeTimestamps = m_normalizeCheck->isChecked();
    opts.ignoreWarnings = m_ignoreWarnCheck->isChecked();
    opts.withSha256 = m_sha256Check->isChecked();
    m_resultCard->setVisible(false);
    m_runProgress->setValue(0);
    m_runLog->clear();
    m_runEta->clear();
    m_runTimer.start();
    m_coord->startProcessing(opts);
}

// ---------------------------------------------------------------------------
// ④ 执行与报告
// ---------------------------------------------------------------------------
QWidget *PreprocessWindow::buildPageRun()
{
    auto *w = new QWidget(this);
    auto *lay = new QVBoxLayout(w);

    m_runProgress = new QProgressBar(w);
    m_runProgress->setMinimumHeight(30);
    m_runProgress->setTextVisible(true);
    lay->addWidget(m_runProgress);
    m_runStatus = new QLabel(lang("等待开始", "Idle"), w);
    m_runStatus->setStyleSheet(QStringLiteral("font-size:14px; color:%1;")
                                   .arg(Theme::TextPrimary));
    lay->addWidget(m_runStatus);
    m_runEta = new QLabel(w);
    m_runEta->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    lay->addWidget(m_runEta);

    m_btnRunLog = new QPushButton(lang("▸ 详细日志", "▸ Detailed log"), w);
    m_btnRunLog->setFlat(true);
    m_btnRunLog->setStyleSheet(QStringLiteral("color:%1; text-align:left;")
                                   .arg(Theme::TextSecond));
    m_runLog = new QPlainTextEdit(w);
    m_runLog->setReadOnly(true);
    m_runLog->setVisible(false);
    m_runLog->setStyleSheet(QStringLiteral("font-family:Consolas,monospace;"));
    lay->addWidget(m_btnRunLog);
    lay->addWidget(m_runLog, 1);

    m_resultCard = new QFrame(w);
    m_resultCard->setVisible(false);
    m_resultCard->setStyleSheet(QStringLiteral(
        "QFrame { background:%1; border:1px solid %2; border-radius:10px; }"
        "QLabel { border:none; background:transparent; }")
        .arg(Theme::BgPanel, Theme::Border));
    auto *rcLay = new QVBoxLayout(m_resultCard);
    m_resultTitle = new QLabel(m_resultCard);
    m_resultTitle->setStyleSheet(QStringLiteral("font-size:18px; font-weight:bold;"));
    rcLay->addWidget(m_resultTitle);
    m_resultOutput = new QLabel(m_resultCard);
    m_resultOutput->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_resultOutput->setWordWrap(true);
    rcLay->addWidget(m_resultOutput);
    m_resultEvidence = new QLabel(m_resultCard);
    m_resultEvidence->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_resultEvidence->setWordWrap(true);
    m_resultEvidence->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    rcLay->addWidget(m_resultEvidence);
    auto *rcRow = new QHBoxLayout();
    m_btnOpenFolder = new QPushButton(lang("打开输出文件夹", "Open output folder"), m_resultCard);
    m_btnOpenReport = new QPushButton(lang("查看证据报告", "Open evidence report"), m_resultCard);
    auto *btnClose = new QPushButton(lang("完成", "Done"), m_resultCard);
    btnClose->setMinimumHeight(34);
    btnClose->setStyleSheet(m_btnBeginSort->styleSheet());
    rcRow->addWidget(m_btnOpenFolder);
    rcRow->addWidget(m_btnOpenReport);
    rcRow->addStretch(1);
    rcRow->addWidget(btnClose);
    rcLay->addLayout(rcRow);
    lay->addWidget(m_resultCard);
    lay->addStretch(1);

    auto *row = new QHBoxLayout();
    m_btnCancel = new QPushButton(lang("取消任务", "Cancel task"), w);
    m_btnCancel->setEnabled(false);
    row->addStretch(1);
    row->addWidget(m_btnCancel);
    lay->addLayout(row);

    connect(m_btnRunLog, &QPushButton::clicked, this, [this]() {
        const bool show = !m_runLog->isVisible();
        m_runLog->setVisible(show);
        m_btnRunLog->setText(show ? lang("▾ 详细日志", "▾ Detailed log")
                                  : lang("▸ 详细日志", "▸ Detailed log"));
    });
    connect(m_btnCancel, &QPushButton::clicked, this, &PreprocessWindow::onCancelRun);
    connect(m_btnOpenFolder, &QPushButton::clicked,
            this, &PreprocessWindow::onOpenOutputFolder);
    connect(m_btnOpenReport, &QPushButton::clicked,
            this, &PreprocessWindow::onOpenReport);
    connect(btnClose, &QPushButton::clicked, this, &QWidget::close);
    return w;
}

void PreprocessWindow::onCancelRun()
{
    m_coord->cancel();
    m_btnCancel->setEnabled(false);
    m_runStatus->setText(lang("正在取消…", "Cancelling…"));
}

void PreprocessWindow::onOpenOutputFolder()
{
    if (!m_outputDir.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_outputDir));
}

void PreprocessWindow::onOpenReport()
{
    if (!m_reportCsv.isEmpty())
        QDesktopServices::openUrl(QUrl::fromLocalFile(m_reportCsv));
}

// ---------------------------------------------------------------------------
// Coordinator 接线
// ---------------------------------------------------------------------------
void PreprocessWindow::onPhaseChanged(TaskPhase phase)
{
    switch (phase) {
    case TaskPhase::UserConfirm:
        m_maxReachedStep = qMax(m_maxReachedStep, 1);
        setStep(1);
        break;
    case TaskPhase::Precheck:
        m_maxReachedStep = qMax(m_maxReachedStep, 2);
        updateSettingsPage();
        setStep(2);
        break;
    case TaskPhase::Transcoding:
    case TaskPhase::Concat:
        m_maxReachedStep = qMax(m_maxReachedStep, 3);
        setStep(3);
        m_btnCancel->setEnabled(true);
        if (!m_runTimer.isValid())
            m_runTimer.start();
        break;
    default:
        break;
    }
    updateStepBar();
}

void PreprocessWindow::onProgress(int percent, const QString &detail)
{
    // 探测/OCR 阶段进度显示在导入页；执行阶段显示在执行页
    if (m_currentStep == 0) {
        m_importProgress->setValue(percent);
        m_importStatus->setText(detail);
    }
    m_runProgress->setValue(percent);
    m_runStatus->setText(detail);
    if (percent >= 5 && percent < 100 && m_runTimer.isValid()) {
        const qint64 elapsed = m_runTimer.elapsed();
        const qint64 remain = elapsed * (100 - percent) / percent;
        m_runEta->setText(lang("预计剩余约 %1", "ETA ~%1").arg(fmtDuration(remain)));
    }
}

void PreprocessWindow::onProbeDone(const QVector<ProbeResult> &results)
{
    for (const auto &r : results) {
        for (int i = 0; i < m_fileTable->rowCount(); ++i) {
            auto *nameItem = m_fileTable->item(i, 0);
            if (!nameItem || nameItem->data(Qt::UserRole).toString() != r.filePath)
                continue;
            if (r.ok()) {
                m_fileTable->setItem(i, 1, new QTableWidgetItem(fmtDuration(r.durationMs)));
                m_fileTable->setItem(i, 2, new QTableWidgetItem(
                    QStringLiteral("%1×%2").arg(r.width).arg(r.height)));
                m_fileTable->setItem(i, 3, new QTableWidgetItem(
                    QStringLiteral("%1/%2").arg(r.container, r.videoCodec)));
                m_fileTable->setItem(i, 5, new QTableWidgetItem(QStringLiteral("✓")));
            } else {
                auto *st = new QTableWidgetItem(lang("✗ 无法读取", "✗ unreadable"));
                st->setForeground(QColor(Theme::Danger));
                st->setToolTip(r.probeError);
                m_fileTable->setItem(i, 5, st);
            }
        }
    }
}

void PreprocessWindow::onEvidenceReady(const QVector<SortGroup> &groups)
{
    m_groups = groups;
    m_importProgress->setVisible(false);
    m_importStatus->setText(lang("分析完成，请校对顺序", "Analysis done; please review order"));
    rebuildReviewViews();
    m_maxReachedStep = qMax(m_maxReachedStep, 1);
    setStep(1);
}

void PreprocessWindow::onPrecheckReady(const QMap<QString, PrecheckResult> &)
{
    updateSettingsPage();
}

void PreprocessWindow::onFinished(const PreprocessReport &report)
{
    m_runProgress->setValue(100);
    m_runStatus->setText(lang("处理完成", "Done"));
    m_runEta->clear();
    m_btnCancel->setEnabled(false);
    m_reportCsv = report.reportCsvPath;
    m_outputDir = report.outputPath.isEmpty()
        ? report.evidenceDir : QFileInfo(report.outputPath).absolutePath();

    m_resultTitle->setText(lang("✓ 拼接完成", "✓ Merge finished"));
    m_resultTitle->setStyleSheet(QStringLiteral(
        "font-size:18px; font-weight:bold; color:%1;").arg(Theme::Success));
    const QFileInfo fi(report.outputPath);
    m_resultOutput->setText(lang("输出文件：%1（%2，用时 %3）", "Output: %1 (%2, took %3)")
        .arg(report.outputPath, fmtBytes(fi.size()),
             fmtDuration(m_runTimer.elapsed())));
    m_resultEvidence->setText(lang("证据报告：%1\n（首/尾帧截图、CSV 明细、操作日志均已留档）",
                                   "Evidence report: %1\n(frames, CSV, operation log archived)")
                                  .arg(report.evidenceDir));
    m_resultCard->setVisible(true);
    m_maxReachedStep = qMax(m_maxReachedStep, 3);
    setStep(3);
}

void PreprocessWindow::onFailed(PreprocessError error, const QString &detail)
{
    m_btnCancel->setEnabled(false);
    if (error == PreprocessError::Cancelled) {
        m_runStatus->setText(lang("已取消。证据保留于证据目录。",
                                  "Cancelled. Evidence kept."));
        return;
    }
    QString plain;
    switch (error) {
    case PreprocessError::OutputConflict:
        plain = lang("磁盘空间不足或输出冲突。建议：更换输出文件夹后重试。",
                     "Insufficient disk space or output conflict. Try another output folder.");
        break;
    case PreprocessError::OcrEngineMissing:
        plain = lang("画面时间识别组件不可用，可改用人工输入时间后继续。",
                     "Time recognition unavailable; enter times manually to proceed.");
        break;
    case PreprocessError::Timeout:
        plain = lang("处理超时。建议：分段处理或检查素材是否损坏。",
                     "Processing timed out. Split the batch or check for damaged clips.");
        break;
    default:
        plain = lang("处理失败。详细原因见日志；证据与日志已保留在证据目录。",
                     "Processing failed. See log; evidence and logs are preserved.");
        break;
    }
    m_resultTitle->setText(lang("✗ 拼接未完成", "✗ Merge not completed"));
    m_resultTitle->setStyleSheet(QStringLiteral(
        "font-size:18px; font-weight:bold; color:%1;").arg(Theme::Danger));
    m_resultOutput->setText(plain);
    m_resultEvidence->setText(lang("错误信息：%1", "Error: %1").arg(detail));
    m_resultCard->setVisible(true);
    m_maxReachedStep = qMax(m_maxReachedStep, 3);
    setStep(3);
}

void PreprocessWindow::onLogLine(const QString &line)
{
    m_runLog->appendPlainText(line);
}

// ---------------------------------------------------------------------------
// 卡片交互：点击选中 / 双击大图 / 拖拽换序
// ---------------------------------------------------------------------------
namespace {
int insertIndexForChan(QWidget *host, const QString &chan, int x,
                       QVector<QFrame *> *cardsOut = nullptr)
{
    QVector<QFrame *> cards;
    for (auto *c : host->findChildren<QFrame *>()) {
        if (c->property("chan").toString() == chan
            && c->property("clip").isValid())
            cards.append(c);
    }
    std::sort(cards.begin(), cards.end(),
              [](QFrame *a, QFrame *b) { return a->x() < b->x(); });
    if (cardsOut)
        *cardsOut = cards;
    for (int i = 0; i < cards.size(); ++i)
        if (x < cards[i]->x() + cards[i]->width() / 2)
            return i;
    return cards.size();
}
} // namespace

bool PreprocessWindow::eventFilter(QObject *watched, QEvent *event)
{
    // --- 卡片：选中/双击/拖拽启动 ---
    auto *card = qobject_cast<QFrame *>(watched);
    if (card && card->property("clip").isValid()) {
        const QString path = card->property("clip").toString();
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_dragStartPos = me->pos();
                m_dragPath = path;
                m_dragChan = card->property("chan").toString();
            }
            break;
        }
        case QEvent::MouseButtonRelease:
            if (!m_dragPath.isEmpty()) {
                selectCard(path, false);   // 未触发拖拽 → 点击选中
                m_dragPath.clear();
            }
            break;
        case QEvent::MouseButtonDblClick:
            m_dragPath.clear();
            showManualDialog(path);
            return true;
        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (!m_dragPath.isEmpty() && (me->buttons() & Qt::LeftButton)
                && (me->pos() - m_dragStartPos).manhattanLength() > 12
                && m_coord->phase() == TaskPhase::UserConfirm) {
                auto *drag = new QDrag(card);
                auto *mime = new QMimeData;
                mime->setText(path);
                mime->setData(QStringLiteral("application/x-lumenarc-chan"),
                              m_dragChan.toUtf8());
                drag->setMimeData(mime);
                drag->setPixmap(card->grab().scaled(
                    120, 150, Qt::KeepAspectRatio, Qt::SmoothTransformation));
                m_dragPath.clear();
                drag->exec(Qt::MoveAction);
                if (m_dragIndicator)
                    m_dragIndicator->hide();
                return true;
            }
            break;
        }
        default:
            break;
        }
        return false;
    }

    // --- 卡片宿主：拖拽落点 ---
    if (watched == m_cardHost) {
        const auto et = event->type();
        if (et == QEvent::DragEnter || et == QEvent::DragMove) {
            auto *de = static_cast<QDragMoveEvent *>(event);
            if (!de->mimeData()->hasFormat(
                    QStringLiteral("application/x-lumenarc-chan")))
                return false;
            de->acceptProposedAction();
            const QString chan = QString::fromUtf8(
                de->mimeData()->data(QStringLiteral("application/x-lumenarc-chan")));
            QVector<QFrame *> cards;
            const int idx = insertIndexForChan(m_cardHost, chan,
                                               de->position().toPoint().x(), &cards);
            if (!m_dragIndicator) {
                m_dragIndicator = new QFrame(m_cardHost);
                m_dragIndicator->setStyleSheet(
                    QStringLiteral("background:%1;").arg(Theme::AccentHover));
            }
            int ix = 4;
            if (!cards.isEmpty())
                ix = idx >= cards.size() ? cards.last()->x() + cards.last()->width() + 4
                                         : cards[idx]->x() - 4;
            m_dragIndicator->setGeometry(ix, 8, 3,
                                         m_cardHost->height() - 16);
            m_dragIndicator->show();
            m_dragIndicator->raise();
            return true;
        }
        if (et == QEvent::DragLeave) {
            if (m_dragIndicator)
                m_dragIndicator->hide();
            return true;
        }
        if (et == QEvent::Drop) {
            auto *de = static_cast<QDropEvent *>(event);
            if (m_dragIndicator)
                m_dragIndicator->hide();
            const QString path = de->mimeData()->text();
            const QString chan = QString::fromUtf8(
                de->mimeData()->data(QStringLiteral("application/x-lumenarc-chan")));
            QVector<QFrame *> cards;
            int idx = insertIndexForChan(m_cardHost, chan,
                                         de->position().toPoint().x(), &cards);
            for (const auto &g : m_groups) {
                if (g.channel != chan)
                    continue;
                QStringList order;
                for (const auto &e : g.ordered)
                    order << e.filePath;
                const int oldIdx = order.indexOf(path);
                if (oldIdx < 0)
                    break;
                order.removeAt(oldIdx);
                if (oldIdx < idx)
                    --idx;
                order.insert(qBound(0, idx, order.size()), path);
                m_coord->applyGroupOrder(chan, order);
                break;
            }
            de->acceptProposedAction();
            return true;
        }
    }
    return QMainWindow::eventFilter(watched, event);
}

// ---------------------------------------------------------------------------
// 拖放导入 / 关闭保护
// ---------------------------------------------------------------------------
void PreprocessWindow::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls()) {
        for (const QUrl &u : event->mimeData()->urls())
            if (isVideoFile(u.toLocalFile())) {
                event->acceptProposedAction();
                return;
            }
    }
}

void PreprocessWindow::dropEvent(QDropEvent *event)
{
    QStringList files;
    for (const QUrl &u : event->mimeData()->urls()) {
        const QString f = u.toLocalFile();
        if (isVideoFile(f))
            files << f;
    }
    if (!files.isEmpty()) {
        addFiles(files);
        setStep(0);
        event->acceptProposedAction();
    }
}

void PreprocessWindow::closeEvent(QCloseEvent *event)
{
    const TaskPhase ph = m_coord->phase();
    if (ph == TaskPhase::Probing || ph == TaskPhase::Ocr
        || ph == TaskPhase::Transcoding || ph == TaskPhase::Concat) {
        const auto ret = QMessageBox::question(this, windowTitle(),
            lang("任务进行中，关闭窗口将取消任务。确定关闭？",
                 "Task is running; closing will cancel it. Close anyway?"));
        if (ret != QMessageBox::Yes) {
            event->ignore();
            return;
        }
        m_coord->cancel();
    }
    event->accept();
}

// ---------------------------------------------------------------------------
// 工具
// ---------------------------------------------------------------------------
QString PreprocessWindow::fmtBytes(qint64 bytes)
{
    if (bytes >= 1073741824)
        return QStringLiteral("%1 GB").arg(bytes / 1073741824.0, 0, 'f', 1);
    return QStringLiteral("%1 MB").arg(bytes / 1048576.0, 0, 'f', 0);
}

QString PreprocessWindow::fmtDuration(qint64 ms)
{
    const qint64 s = qAbs(ms) / 1000;
    if (s >= 3600)
        return QStringLiteral("%1:%2:%3")
            .arg(s / 3600)
            .arg((s % 3600) / 60, 2, 10, QLatin1Char('0'))
            .arg(s % 60, 2, 10, QLatin1Char('0'));
    return QStringLiteral("%1:%2")
        .arg(s / 60)
        .arg(s % 60, 2, 10, QLatin1Char('0'));
}
