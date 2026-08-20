/**
 * @file preprocesswindow.cpp
 * @brief 前处理-素材转码拼接独立任务窗口实现
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
#include "sortablefiletable.h"
#include "app/case_manager.h"
#include "domain/dedupe_plan.h"
#include "domain/filename_timestamp.h"
#include "domain/smart_sorter.h"
#include "i18n.h"
#include "theme.h"

#include <functional>
#include <algorithm>

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
#include <QComboBox>
#include <QSettings>
#include "infrastructure/encoder_probe.h"
#include <QCheckBox>
#include <QTimer>
#include <QDialog>
#include <QDialogButtonBox>
#include <QPainter>
#include <QMouseEvent>

// ---------------------------------------------------------------------------
// P-60 框选时间戳对话框（问题卡「框一下画面上的时间」）：证据帧上拖框，
// 返回归一化坐标。无 Q_OBJECT（免 moc）——选择与确认经对话框按钮外递。
// ---------------------------------------------------------------------------
class RoiPickLabel : public QLabel
{
public:
    explicit RoiPickLabel(const QPixmap &pm, QWidget *parent = nullptr)
        : QLabel(parent), m_pm(pm)
    {
        const int W = 880;
        const double s = pm.width() > 0 ? qMin(1.0, double(W) / pm.width()) : 1.0;
        setFixedSize(qMax(1, int(pm.width() * s)), qMax(1, int(pm.height() * s)));
        setCursor(Qt::CrossCursor);
    }
    QRectF selectionNorm() const
    {
        if (m_sel.isNull() || width() <= 0 || height() <= 0)
            return QRectF();
        const double w = width(), h = height();
        return QRectF(m_sel.left() / w, m_sel.top() / h,
                      m_sel.width() / w, m_sel.height() / h).normalized();
    }

protected:
    void paintEvent(QPaintEvent *e) override
    {
        Q_UNUSED(e);
        QPainter p(this);
        p.drawPixmap(rect(), m_pm);
        if (!m_sel.isNull()) {
            p.setPen(QPen(QColor("#F0B429"), 2));
            p.fillRect(m_sel, QColor(240, 180, 41, 60));
            p.drawRect(m_sel);
        }
    }
    void mousePressEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton) {
            m_anchor = e->pos();
            m_sel = QRect(m_anchor, QSize(1, 1));
            update();
        }
    }
    void mouseMoveEvent(QMouseEvent *e) override
    {
        if (!m_sel.isNull()) {
            m_sel = QRect(m_anchor, e->pos()).normalized().intersected(rect());
            update();
        }
    }
    void mouseReleaseEvent(QMouseEvent *e) override
    {
        if (e->button() == Qt::LeftButton && !m_sel.isNull()) {
            m_sel = QRect(m_anchor, e->pos()).normalized().intersected(rect());
            update();
        }
    }

private:
    QPixmap m_pm;
    QPoint m_anchor;
    QRect m_sel;
};

void PreprocessWindow::roiPickForFile(const QString &filePath)
{
    const QString img = evidenceImageFor(filePath);
    const QPixmap pm(img);
    if (img.isEmpty() || pm.isNull()) {
        // 无证据帧可框 → 退到手输（C2 不静默）
        m_selectedPath = filePath;
        showManualDialog(filePath);
        return;
    }
    QDialog dlg(this);
    dlg.setWindowTitle(lang("框选画面上的时间", "Box the on-screen time"));
    auto *v = new QVBoxLayout(&dlg);
    auto *hint = new QLabel(lang("在截图上拖框住时间显示区域，其余段会自动跟着认",
                                 "Drag a box around the on-screen time; "
                                 "other clips follow automatically"), &dlg);
    hint->setWordWrap(true);
    v->addWidget(hint);
    auto *picker = new RoiPickLabel(pm, &dlg);
    v->addWidget(picker, 0, Qt::AlignCenter);
    auto *btns = new QDialogButtonBox(QDialogButtonBox::Ok
                                      | QDialogButtonBox::Cancel, &dlg);
    v->addWidget(btns);
    QObject::connect(btns, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(btns, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted)
        return;
    const QRectF roi = picker->selectionNorm();
    if (!roi.isValid() || roi.width() < 0.01 || roi.height() < 0.005)
        return;   // 框太小视为误点
    m_reviewSummary->setText(lang("正在按框选位置重新识别…",
                                  "Re-identifying with the boxed area…"));
    m_coord->reOcrWithRoi(filePath, roi);
}

#include <QFont>
#include <QRadioButton>
#include <QScrollArea>
#include <QFileDialog>
#include <QMessageBox>
#include <QCryptographicHash>
#include <QDateTime>
#include "infrastructure/tool_paths.h"
#include "infrastructure/integrity_checker.h"
#include <QFileInfo>
#include <QFile>
#include <QCollator>
#include <QGuiApplication>
#include <QDateTime>
#include <QDateTimeEdit>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDesktopServices>
#include <QUrl>
#include <QPainter>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QMimeData>
#include <QDrag>
#include <QMouseEvent>
#include <QKeyEvent>
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

/// SHA-256 内容指纹（与案件指纹 CaseManager::computeSha256 同算法同语义）；
/// 失败返回空（去重判定保守保留，见 domain/dedupe_plan.h）
QString sha256OfFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    QCryptographicHash h(QCryptographicHash::Sha256);
    if (!h.addData(&f))
        return QString();
    return QString::fromLatin1(h.result().toHex());
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
    setWindowTitle(lang("素材转码拼接", "Clip Transcode & Merge"));
    setAttribute(Qt::WA_DeleteOnClose);
    resize(1120, 780);
    m_coord->setAnalysisEngine(analysis);

    auto *central = new QWidget(this);
    auto *root = new QVBoxLayout(central);
    root->setContentsMargins(10, 8, 10, 8);
    root->setSpacing(8);
    root->addWidget(buildFormatBanner());
    root->addWidget(buildCaseBanner());
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
    connect(m_coord, &PreprocessingCoordinator::overlapDetected,
            this, &PreprocessWindow::onOverlapDetected);
    connect(m_coord, &PreprocessingCoordinator::finished,
            this, &PreprocessWindow::onFinished);
    connect(m_coord, &PreprocessingCoordinator::failed,
            this, &PreprocessWindow::onFailed);
    connect(m_coord, &PreprocessingCoordinator::logLine,
            this, &PreprocessWindow::onLogLine);
}

// ---------------------------------------------------------------------------
// 顶部横幅：支持格式说明（替代原 1234 步骤条）
// ---------------------------------------------------------------------------
QWidget *PreprocessWindow::buildFormatBanner()
{
    auto *banner = new QFrame(this);
    banner->setStyleSheet(QStringLiteral(
        "QFrame { background:%1; border:1px solid %2; border-radius:8px; }"
        "QLabel { border:none; background:transparent; }")
        .arg(Theme::BgPanel, Theme::Border));
    auto *lay = new QVBoxLayout(banner);
    lay->setContentsMargins(12, 8, 12, 8);
    lay->setSpacing(4);
    auto *title = new QLabel(lang("支持格式", "Supported formats"), banner);
    title->setStyleSheet(QStringLiteral("font-weight:bold; color:%1;")
                             .arg(Theme::TextPrimary));
    lay->addWidget(title);
    auto *desc = new QLabel(
        lang("用法：拖入视频文件 → 按顺序排好（拖拽行）→ 点右下角 GO\n"
             "│ 拖入时自动排除内容完全相同的重复文件（SHA-256 指纹，保留首个）\n"
             "│\n"
             "│ GO 会自动判断：\n"
             "│ - 全部是参数一致的 MP4 且关键帧间隔 ≤2 秒 → 直接无损拼接（不重编码、画质零损失）\n"
             "│ - 有非 MP4 格式（DAV/AVI/WMV/FLV/TS/MOV/MKV 等），或 MP4 关键帧间隔超过 2 秒"
             "（拖拽不流畅）→ 自动转码后拼接（统一为 MP4·H.264·2 秒关键帧）\n"
             "│ - 只拖入 1 个文件 → 单独转码导出为 MP4\n"
             "│\n"
             "│ 输出：MP4（H.264）拼接产物可直接在主窗口播放；证据报告自动生成。",
             "Usage: drop video clips → order them (drag rows) → hit GO (bottom right)\n"
             "│ Exact duplicates are excluded on import (SHA-256 fingerprint, first copy kept)\n"
             "│\n"
             "│ GO decides automatically:\n"
             "│ - identical MP4s with ≤2s keyframes → lossless merge (no re-encode, zero quality loss)\n"
             "│ - any non-MP4 (DAV/AVI/WMV/FLV/TS/MOV/MKV…) or MP4 with keyframes >2s "
             "(jerky scrubbing) → auto transcode to MP4·H.264·2s keyframes, then merge\n"
             "│ - a single file → transcode & export as MP4\n"
             "│\n"
             "│ Output: MP4 (H.264), playable in the main window; evidence report auto-generated."),
        banner);
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    lay->addWidget(desc);
    return banner;
}

// ---------------------------------------------------------------------------
// 案件横幅（v1.3.0 M2 任务8）：案件模式提示 +【导入案件】/【独立输出】选择
// ---------------------------------------------------------------------------
QWidget *PreprocessWindow::buildCaseBanner()
{
    m_caseBanner = new QFrame(this);
    m_caseBanner->setStyleSheet(QStringLiteral(
        "QFrame { background:%1; border:1px solid %2; border-radius:8px; }"
        "QLabel { border:none; background:transparent; }")
        .arg(Theme::BgCard, Theme::Accent));
    auto *lay = new QVBoxLayout(m_caseBanner);
    lay->setContentsMargins(12, 6, 12, 6);
    lay->setSpacing(4);
    // 行 1：案件信息 + 导入/独立模式按钮（无案件时整个模式行隐藏）
    m_caseModeRow = new QWidget(m_caseBanner);
    auto *rowLay = new QHBoxLayout(m_caseModeRow);
    rowLay->setContentsMargins(0, 0, 0, 0);
    m_caseBannerLabel = new QLabel(m_caseModeRow);
    m_caseBannerLabel->setStyleSheet(QStringLiteral(
        "font-weight:bold; color:%1;").arg(Theme::TextPrimary));
    rowLay->addWidget(m_caseBannerLabel, 1);
    m_btnCaseImport = new QPushButton(
        lang("导入案件（默认路径）", "Import into case (default)"), m_caseModeRow);
    m_btnCaseImport->setCheckable(true);
    m_btnCaseImport->setChecked(true);
    m_btnCaseImport->setToolTip(lang(
        "成果输出到案件 preprocess/<时间戳>/，完成后自动登记 case.json；"
        "sidecar 校时文件复制归类 sidecars/。",
        "Outputs go to the case preprocess/<timestamp>/ folder and are "
        "registered into case.json; calibration sidecars are copied into sidecars/."));
    m_btnCaseIndep = new QPushButton(
        lang("独立输出（自选）", "Independent output (custom)"), m_caseModeRow);
    m_btnCaseIndep->setCheckable(true);
    m_btnCaseIndep->setToolTip(lang(
        "本次不导入案件：输出到任意文件夹（点击后弹出目录选择），行为与无案件时一致。",
        "Skip the case this time: output to any folder (picker opens on click), "
        "same as no-case mode."));
    rowLay->addWidget(m_btnCaseImport);
    rowLay->addWidget(m_btnCaseIndep);
    lay->addWidget(m_caseModeRow);
    // 行 2：输出文件夹（始终可见——§45 路径选择集成到页面，替代设置页隐藏入口）
    auto *outRow = new QHBoxLayout();
    outRow->addWidget(new QLabel(lang("输出文件夹：", "Output folder:"), m_caseBanner));
    m_outputDirEdit = new QLineEdit(m_caseBanner);
    auto *btnBrowse = new QPushButton(lang("浏览…", "Browse…"), m_caseBanner);
    m_btnBrowseOutput = btnBrowse;
    outRow->addWidget(m_outputDirEdit, 1);
    outRow->addWidget(btnBrowse);
    lay->addLayout(outRow);
    connect(m_btnCaseImport, &QPushButton::clicked,
            this, [this]() { setCaseImportMode(true); });
    connect(m_btnCaseIndep, &QPushButton::clicked,
            this, [this]() { setCaseImportMode(false); });
    connect(btnBrowse, &QPushButton::clicked, this, &PreprocessWindow::onBrowseOutput);
    connect(m_outputDirEdit, &QLineEdit::textChanged,
            this, &PreprocessWindow::updateDiskEstimate);
    m_caseBanner->setVisible(true);   // 始终可见；无案件时仅隐藏模式行（refreshCaseBanner）
    return m_caseBanner;
}

void PreprocessWindow::setCaseManager(CaseManager *cm)
{
    m_caseManager = cm;
    refreshCaseBanner();
}

QString PreprocessWindow::caseSessionDir() const
{
    // 惰性生成一次：同一次处理运行复用同一目录（报告/输出/登记三者一致）
    if (m_caseSessionDir.isEmpty() && m_caseManager && m_caseManager->isOpen()) {
        m_caseSessionDir = m_caseManager->caseDir()
            + QStringLiteral("/preprocess/")
            + QDateTime::currentDateTime().toString(
                  QStringLiteral("yyyyMMdd_HHmmss"));
    }
    return m_caseSessionDir;
}

void PreprocessWindow::setCaseImportMode(bool on)
{
    m_caseImportMode = on;
    m_caseSessionDir.clear();   // 换模式：下次运行时重新生成会话目录
    if (m_btnCaseImport) m_btnCaseImport->setChecked(on);
    if (m_btnCaseIndep)  m_btnCaseIndep->setChecked(!on);
    refreshCaseBanner();
    // §45：切到自定义输出即弹出目录选择（入口直观化，取消则维持原路径）
    if (!on)
        onBrowseOutput();
}

void PreprocessWindow::refreshCaseBanner()
{
    const bool caseOpen = m_caseManager && m_caseManager->isOpen();
    // §45：横幅（含输出文件夹行）始终可见；无案件时仅隐藏模式行
    if (m_caseModeRow)
        m_caseModeRow->setVisible(caseOpen);
    if (!caseOpen) {
        if (m_outputDirEdit)
            m_outputDirEdit->setReadOnly(false);   // 无案件：路径自由填写
        return;
    }
    m_caseBannerLabel->setText(
        lang("📁 案件模式：成果自动导入《%1》",
             "📁 Case mode: results auto-imported into “%1”")
            .arg(m_caseManager->meta().caseNo
                 + QStringLiteral("-") + m_caseManager->meta().title));
    // 输出目录行：导入案件 = 会话目录（横幅控路径，禁手改）；独立输出 = 恢复手选
    if (m_outputDirEdit) {
        // v1.7.1：案件模式允许经「浏览…」自选（校验案内），编辑框只读——
        // 防手改案外路径导致登记被拒（回滚前实测 bug：前处理没入案）
        m_outputDirEdit->setReadOnly(m_caseImportMode);
        if (m_caseImportMode)
            m_outputDirEdit->setText(caseSessionDir());
    }
    if (m_btnBrowseOutput)
        m_btnBrowseOutput->setEnabled(true);   // 案内自选（onBrowseOutput 校验）
}

void PreprocessWindow::setStep(int idx)
{
    m_currentStep = idx;
    m_stack->setCurrentIndex(idx);
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

    m_fileTable = new SortableFileTable(0, 6, w);
    m_fileTable->setHorizontalHeaderLabels({
        lang("文件名", "File"), lang("时长", "Duration"),
        lang("分辨率", "Resolution"), lang("格式", "Format"),
        lang("大小", "Size"), lang("状态", "Status")});
    m_fileTable->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_fileTable->verticalHeader()->setVisible(false);
    m_fileTable->setSelectionMode(QAbstractItemView::SingleSelection);
    m_fileTable->setSelectionBehavior(QAbstractItemView::SelectRows);   // 点任意列 = 选整行
    m_fileTable->setEditTriggers(QAbstractItemView::NoEditTriggers);
    // 行拖拽排序：自建拖拽（不依赖 Qt 内置 InternalMove——其覆盖语义不可靠，
    // 现场三次验证未解决）。事件挂在 viewport：mousePress 记起点 → mouseMove
    // 发起 QDrag → drop 按行中心上/下半插位。
    m_fileTable->setDragDropMode(QAbstractItemView::NoDragDrop);
    // NoDragDrop 会连 viewport acceptDrops 一并关掉 → 自建 drop 无法到达；手动开回
    m_fileTable->setAcceptDrops(true);
    m_fileTable->viewport()->installEventFilter(this);
    m_fileTable->installEventFilter(this);   // 键盘事件（Del 删除选中行）
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
    m_btnBeginSort = new QPushButton(lang("GO", "GO"), w);
    m_btnBeginSort->setEnabled(false);
    m_btnBeginSort->setMinimumHeight(40);
    m_btnBeginSort->setMinimumWidth(88);
    m_btnBeginSort->setToolTip(lang(
        "GO 一键完成：识别画面时间 → 自动排序 → 拼接。\n认不出来的段会请你框一下画面上的时间。",
        "One-click: read on-screen time, auto-sort, merge. "
        "You'll only be asked to box the time if a clip can't be read."));
    QFont goFont = m_btnBeginSort->font();
    goFont.setPointSize(16);
    goFont.setBold(true);
    m_btnBeginSort->setFont(goFont);
    m_btnBeginSort->setStyleSheet(QStringLiteral(
        "QPushButton { background:%1; color:%2; font-weight:bold; border-radius:10px; }"
        "QPushButton:disabled { background:%3; color:%4; }")
        .arg(Theme::Accent, Theme::AccentOnDark, Theme::BgCard, Theme::TextMuted));
    m_btnQuickMerge = new QPushButton(
        lang("直接拼接（不识别时间）", "Merge directly (no time read)"), w);
    m_btnQuickMerge->setEnabled(false);
    m_btnQuickMerge->setFlat(true);
    m_btnQuickMerge->setToolTip(lang(
        "高级旁路：不识别画面时间，按列表当前顺序直接拼接（可拖行定序）。",
        "Advanced: skip time reading, merge in current list order "
        "(drag rows to reorder)."));
    row->addWidget(btnAdd);
    row->addWidget(btnClear);
    row->addStretch(1);
    row->addWidget(m_btnBeginSort);
    row->addWidget(m_btnQuickMerge);
    lay->addLayout(row);

    connect(btnAdd, &QPushButton::clicked, this, &PreprocessWindow::onAddFiles);
    connect(btnClear, &QPushButton::clicked, this, &PreprocessWindow::onClearFiles);
    connect(m_btnBeginSort, &QPushButton::clicked, this, &PreprocessWindow::onBeginSort);
    connect(m_btnQuickMerge, &QPushButton::clicked, this, &PreprocessWindow::onQuickMerge);
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

    // v1.12.0（2026-08-19 现场反馈）：导入即内容去重（排除重复文件）——同一批
    // 监控素材常混入完全相同的重复导出段，不去重会让拼接产物出现重复画面。
    // 规则见 domain/dedupe_plan.h：尺寸不同必然不同内容（免哈希）；仅同尺寸
    // 碰撞组算 SHA-256，指纹一致判重、保留首个；哈希失败保守保留。
    QVector<DuplicatePair> excludedDups;
    {
        QVector<DedupeEntry> entries;
        entries.reserve(m_pendingFiles.size());
        for (const QString &f : m_pendingFiles)
            entries.append({f, QFileInfo(f).size(), QString()});
        const QStringList needHash = filesNeedingHash(entries);
        QMap<QString, QString> hashes;
        if (!needHash.isEmpty()) {
            QGuiApplication::setOverrideCursor(Qt::WaitCursor);
            for (const QString &f : needHash)
                hashes.insert(f, sha256OfFile(f));
            QGuiApplication::restoreOverrideCursor();
        }
        for (auto &e : entries)
            e.sha256 = hashes.value(e.filePath);
        const DedupePlan plan = planDedupe(entries);
        if (!plan.duplicates.isEmpty()) {
            excludedDups = plan.duplicates;
            m_pendingFiles = plan.kept;
        }
    }

    // 2026-08-13（Bug B 加固）：导入列表立即按文件名时间戳排序——
    // 所见即所得（导入页顺序 = 校对页顺序 = 拼接顺序），不再等探测
    // 完成才由 buildListOrderGroups 纠正；用户拖拽仍可在导入后微调
    // （coordinator 侧 sortFilesByNameTime 为最终兜底，二者语义一致）。
    // 无时间戳文件保持相对序排末尾（与 coordinator 同一约定）。
    // v1.12.0 修复（2026-08-20 现场）：无时间戳兜底改用自然序（QCollator
    // 数字感知）——旧 a.file < b.file 全路径字典序会把 0,1,2… 排成
    // 0,1,10,11…19,2…，与资源管理器及用户预期均不符
    {
        struct PendingItem { QString file; qint64 ts; bool hasTs; };
        QVector<PendingItem> items;
        items.reserve(m_pendingFiles.size());
        for (const QString &f : m_pendingFiles) {
            const FilenameTimestamp ft = parseFilenameTimestamp(
                QFileInfo(f).fileName());
            items.append({f, ft.epochMs, ft.hit()});
        }
        QCollator natural;
        natural.setNumericMode(true);
        std::stable_sort(items.begin(), items.end(),
            [&](const PendingItem &a, const PendingItem &b) {
                if (a.hasTs && b.hasTs)
                    return a.ts < b.ts;
                if (a.hasTs != b.hasTs)
                    return a.hasTs;   // 有时间戳的排前
                return natural.compare(QFileInfo(a.file).fileName(),
                                       QFileInfo(b.file).fileName()) < 0;
            });
        m_pendingFiles.clear();
        for (const auto &it : items)
            m_pendingFiles.append(it.file);
    }

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
                                 .arg(fmtBytes(totalBytes))
                             + (excludedDups.isEmpty() ? QString()
                                : lang("；已排除重复 %1 段", "; %1 duplicate(s) excluded")
                                      .arg(excludedDups.size())));
    m_btnBeginSort->setEnabled(!m_pendingFiles.isEmpty());
    m_btnQuickMerge->setEnabled(!m_pendingFiles.isEmpty());
    m_importStatus->clear();
    if (!excludedDups.isEmpty()) {
        // 明示排除清单（前 5 段）：排除是数据取舍，必须留痕可见（规范 C2）
        QStringList names;
        const int showN = qMin(5, excludedDups.size());
        for (int i = 0; i < showN; ++i)
            names << lang("%1（同 %2）", "%1 (same as %2)")
                         .arg(QFileInfo(excludedDups[i].filePath).fileName(),
                              QFileInfo(excludedDups[i].keptPath).fileName());
        m_importStatus->setText(
            lang("已排除重复文件（内容与保留段完全相同）：%1%2",
                 "Duplicates excluded (identical content): %1%2")
                .arg(names.join(lang("、", ", ")))
                .arg(excludedDups.size() > showN
                         ? lang(" 等 %1 段", " … %1 in total").arg(excludedDups.size())
                         : QString()));
    }
    // v1.12.0（2026-08-20 现场反馈）：文件名全无时间信息时导入即引导——
    // 列表顺序由拖入锚点决定不可信，「直接拼接」会盲拼（护栏弹窗会被点穿）
    {
        int namedCnt = 0;
        for (const QString &f : m_pendingFiles)
            if (parseFilenameTimestamp(QFileInfo(f).fileName()).hit())
                ++namedCnt;
        if (namedCnt == 0 && m_pendingFiles.size() > 1) {
            const QString hint = lang(
                "ⓘ 文件名都不含时间信息：建议点右下角 GO 自动识别画面时间并排序；"
                "「直接拼接」将只按列表顺序执行",
                "ⓘ No filename timestamps: use GO (bottom right) to read on-screen "
                "time and sort; direct merge follows list order only");
            const QString cur = m_importStatus->text();
            m_importStatus->setText(cur.isEmpty()
                ? hint : cur + QStringLiteral("\n") + hint);
        }
    }
}

/// 表格行拖拽后同步文件顺序（直接拼接按此行序执行）
void PreprocessWindow::syncPendingFromTable()
{
    QStringList reordered;
    for (int i = 0; i < m_fileTable->rowCount(); ++i) {
        auto *nameItem = m_fileTable->item(i, 0);
        if (nameItem)
            reordered.append(nameItem->data(Qt::UserRole).toString());
    }
    if (reordered.size() == m_pendingFiles.size())
        m_pendingFiles = reordered;
    m_importSummary->setText(lang("已导入 %1 段（可拖拽行调整顺序；当前顺序即拼接顺序）",
                                  "%1 clip(s) (drag rows to reorder; row order = merge order)")
                                 .arg(m_pendingFiles.size()));
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
    m_btnQuickMerge->setEnabled(false);
    m_importStatus->clear();
    setStep(0);
}

void PreprocessWindow::onBeginSort()
{
    if (m_pendingFiles.isEmpty())
        return;
    m_overlapPendingChannels.clear();   // 新一轮分析：清除上一轮重叠记录

    if (m_coord->phase() == TaskPhase::UserConfirm) {
        // 已探测就绪：直接对当前列表重跑自动排序（覆盖顺序）
        m_importStatus->setText(lang("正在分析素材（识别画面时间）…",
                                     "Analyzing clips (reading on-screen time)…"));
        m_coord->runAutoSort();
        return;
    }
    m_importProgress->setVisible(true);
    m_importProgress->setValue(0);
    m_btnBeginSort->setEnabled(false);
    m_btnQuickMerge->setEnabled(false);
    m_importStatus->setText(lang("正在分析素材（识别画面时间）…",
                                 "Analyzing clips (reading on-screen time)…"));
    m_coord->beginWithAutoSort(m_pendingFiles);

}

void PreprocessWindow::onQuickMerge()
{
    if (m_pendingFiles.isEmpty())
        return;
    m_overlapPendingChannels.clear();   // 新一轮执行：清除上一轮重叠记录

    // v1.12.0（2026-08-20 现场反馈）：文件名全部不含时间信息且尚未识别排序时，
    // 「直接拼接」= 按拖入顺序盲拼（Explorer 拖拽锚点决定顺序）——现场实测
    // 8/18 片段被拼到 8/19 之后、时间重叠段也未修剪。护栏：强制确认并
    // 引导改用 GO（识别画面时间 → 排序 → 修剪重叠 → 拼接）。
    if (m_coord->phase() != TaskPhase::UserConfirm && m_pendingFiles.size() > 1) {
        int named = 0;
        for (const QString &f : m_pendingFiles)
            if (parseFilenameTimestamp(QFileInfo(f).fileName()).hit())
                ++named;
        if (named == 0) {
            QMessageBox box(this);
            box.setWindowTitle(lang("直接拼接可能乱序", "Direct merge may misorder"));
            box.setIcon(QMessageBox::Warning);
            box.setText(lang(
                QStringLiteral("当前 %1 个文件的文件名都不含时间信息，直接拼接将只按"
                               "列表顺序（即拖入顺序）执行，监控素材极易乱序，\n"
                               "且时间重叠的重复片段不会被修剪。\n\n"
                               "建议改用【GO】：自动识别画面时间 → 按真实时间排序 → "
                               "修剪重叠 → 拼接。").arg(m_pendingFiles.size()),
                QStringLiteral("None of the %1 filenames carry a timestamp; direct "
                               "merge follows the list (drop) order only and will not "
                               "trim time-overlapped duplicates — misordering is likely.\n\n"
                               "Recommended: use GO to read on-screen time, sort by real "
                               "time and trim overlaps before merging.")
                    .arg(m_pendingFiles.size())));
            QPushButton *goBtn = box.addButton(
                lang("改用 GO 识别排序（推荐）", "Use GO auto-sort (recommended)"),
                QMessageBox::AcceptRole);
            QPushButton *anywayBtn = box.addButton(
                lang("仍按列表顺序拼接", "Merge in list order anyway"),
                QMessageBox::DestructiveRole);
            box.addButton(QMessageBox::Cancel);
            box.exec();
            if (box.clickedButton() == goBtn) {
                onBeginSort();
                return;
            }
            if (box.clickedButton() != anywayBtn)
                return;
        }
    }

    if (m_coord->phase() == TaskPhase::UserConfirm) {
        // 已探测就绪：按当前列表顺序直接拼接（不识别画面时间）
        // 默认修剪同样生效（重叠计划基于已完成的排序结果；无重叠为空操作）
        m_coord->setTrimOverlap(overlapTrimOn());
        m_coord->startProcessing(collectProcessingOptions());   // 内部自动确认顺序
        return;
    }
    // 非强制一键：不识别画面时间，按列表当前顺序直接拼接；
    // 需要转码的文件自动转码（逐文件判定），其余无损拼接。
    m_importProgress->setVisible(true);
    m_importProgress->setValue(0);
    m_btnBeginSort->setEnabled(false);
    m_btnQuickMerge->setEnabled(false);
    m_importStatus->setText(lang("正在探测并拼接…", "Probing and merging…"));
    m_pendingQuickMerge = true;
    m_pendingOpts = collectProcessingOptions();
    m_coord->begin(m_pendingFiles);

}

ProcessingOptions PreprocessWindow::collectProcessingOptions() const
{
    ProcessingOptions opts;
    opts.outputDir = m_outputDirEdit ? m_outputDirEdit->text().trimmed() : QString();
    // 案件导入模式：输出目录锁定案件会话目录（v1.3.0 M2 任务8）
    if (m_caseImportMode && m_caseManager && m_caseManager->isOpen())
        opts.outputDir = caseSessionDir();
    opts.crf = m_crfSpin ? m_crfSpin->value() : 18;
    opts.encoder = m_encoderCombo ? m_encoderCombo->currentData().toString()
                                  : QStringLiteral("libx264");
    opts.deinterlace = !m_deinterlaceCheck || m_deinterlaceCheck->isChecked();
    opts.normalizeTimestamps = m_normalizeCheck && m_normalizeCheck->isChecked();
    opts.ignoreWarnings = m_ignoreWarnCheck && m_ignoreWarnCheck->isChecked();
    opts.withSha256 = !m_sha256Check || m_sha256Check->isChecked();
    return opts;
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

    // P-60 问题卡区（拍板 Q4A：拿不准才出现；大白话+缩略图+就地操作）
    m_problemPanel = new QWidget(w);
    m_problemLay = new QVBoxLayout(m_problemPanel);
    m_problemLay->setContentsMargins(0, 0, 0, 0);
    m_problemLay->setSpacing(8);
    m_problemPanel->setVisible(false);
    lay->addWidget(m_problemPanel);

    // 完整视图折叠开关（默认收起——初级调查员只看问题卡）
    m_showAllBtn = new QPushButton(lang("▸ 查看全部片段（时间线与证据卡）",
                                        "▸ Show all clips (timeline & cards)"), w);
    m_showAllBtn->setFlat(true);
    m_showAllBtn->setStyleSheet(QStringLiteral("color:%1; text-align:left;")
                                    .arg(Theme::TextSecond));
    connect(m_showAllBtn, &QPushButton::clicked, this, [this]() {
        const bool show = !m_fullReview->isVisible();
        m_fullReview->setVisible(show);
        m_showAllBtn->setText(show
            ? lang("▾ 收起全部片段", "▾ Hide all clips")
            : lang("▸ 查看全部片段（时间线与证据卡）",
                   "▸ Show all clips (timeline & cards)"));
    });
    lay->addWidget(m_showAllBtn);

    // 完整视图（原②页内容：时间线 + 证据卡）
    m_fullReview = new QWidget(w);
    auto *fr = new QVBoxLayout(m_fullReview);
    fr->setContentsMargins(0, 0, 0, 0);
    m_timeline = new ClipTimelineWidget(m_fullReview);
    fr->addWidget(m_timeline);
    connect(m_timeline, &ClipTimelineWidget::clipClicked,
            this, &PreprocessWindow::onCardClicked);

    m_cardScroll = new QScrollArea(m_fullReview);
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
    fr->addWidget(m_cardScroll, 1);
    m_fullReview->setVisible(false);
    lay->addWidget(m_fullReview, 1);

    auto *row = new QHBoxLayout();
    auto *btnLeft = new QPushButton(QStringLiteral("←"), w);
    auto *btnRight = new QPushButton(QStringLiteral("→"), w);
    btnLeft->setToolTip(lang("选中片段前移", "Move selected earlier"));
    btnRight->setToolTip(lang("选中片段后移", "Move selected later"));
    auto *btnManual = new QPushButton(lang("手动输入时间…", "Enter time manually…"), w);
    auto *btnRe = new QPushButton(lang("重新识别", "Re-identify"), w);
    m_confirmBtn = new QPushButton(lang("确认顺序，下一步 →", "Confirm order, next →"), w);
    m_confirmBtn->setMinimumHeight(36);
    m_confirmBtn->setStyleSheet(m_btnBeginSort->styleSheet());
    row->addWidget(btnLeft);
    row->addWidget(btnRight);
    row->addWidget(btnManual);
    row->addWidget(btnRe);
    row->addStretch(1);
    row->addWidget(m_confirmBtn);
    lay->addLayout(row);

    connect(btnLeft, &QPushButton::clicked, this, [this]() { onMoveSelected(-1); });
    connect(btnRight, &QPushButton::clicked, this, [this]() { onMoveSelected(+1); });
    connect(btnManual, &QPushButton::clicked, this, &PreprocessWindow::onManualTimestamp);
    connect(btnRe, &QPushButton::clicked, this, &PreprocessWindow::onReIdentify);
    connect(m_confirmBtn, &QPushButton::clicked, this, &PreprocessWindow::onConfirmOrder);
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
    rebuildProblemPanel();   // P-60 问题卡区
    updateConfirmGate();     // P-60 确认主按钮门控
}

// ---------------------------------------------------------------------------
// P-60 问题卡区（方案 §4.5，拍板 Q4A）：大白话 + 缩略图 + 就地操作
// ---------------------------------------------------------------------------
static QString problemKey(const SortProblem &p)
{
    switch (p.kind) {
    case SortProblem::Unidentified:
        return QStringLiteral("u|") + p.fileA;
    case SortProblem::OverlapPair:
        return QStringLiteral("o|") + p.fileA + QStringLiteral("|") + p.fileB;
    default:
        return QStringLiteral("s|") + QString::number(p.groupIndex);
    }
}

bool PreprocessWindow::overlapTrimOn() const
{
    return !(m_keepOverlapCheck && m_keepOverlapCheck->isChecked());
}

void PreprocessWindow::rebuildProblemPanel()
{
    while (QLayoutItem *it = m_problemLay->takeAt(0)) {
        if (it->widget())
            it->widget()->deleteLater();
        delete it;
    }
    const auto problems = collectSortProblems(m_groups, overlapTrimOn());
    m_problemPanel->setVisible(!problems.isEmpty());
    if (problems.isEmpty())
        return;

    int pending = 0;
    for (const auto &p : problems)
        if (!m_problemAcks.contains(problemKey(p)))
            ++pending;
    auto *hdr = new QLabel(lang("⚠ 有 %1 处需要你看一眼（处理完即可继续拼接）",
                                "⚠ %1 issue(s) need your attention before merging")
                               .arg(pending), m_problemPanel);
    hdr->setWordWrap(true);
    hdr->setStyleSheet(QStringLiteral("color:%1; font-size:15px; font-weight:bold;")
                           .arg(Theme::Accent));
    m_problemLay->addWidget(hdr);

    for (const auto &p : problems) {
        const QString key = problemKey(p);
        const bool acked = m_problemAcks.contains(key);
        auto *card = new QFrame(m_problemPanel);
        card->setStyleSheet(QStringLiteral(
            "QFrame { background:%1; border:1px solid %2; border-radius:8px; }")
            .arg(Theme::BgPanel, acked ? Theme::Border : Theme::Accent));
        auto *row = new QHBoxLayout(card);

        // 缩略图（证据帧；P-60：认得出来自哪里比文件名直观）
        const QString img = p.fileA.isEmpty() ? QString()
                                              : evidenceImageFor(p.fileA);
        auto *pic = new QLabel(card);
        pic->setFixedSize(160, 90);
        pic->setAlignment(Qt::AlignCenter);
        if (!img.isEmpty()) {
            const QPixmap pm(img);
            if (!pm.isNull())
                pic->setPixmap(pm.scaled(160, 90, Qt::KeepAspectRatio,
                                         Qt::SmoothTransformation));
            else
                pic->setText(QStringLiteral("…"));
        } else {
            pic->setText(lang("无图", "no img"));
        }
        pic->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextMuted));
        row->addWidget(pic);

        auto *mid = new QVBoxLayout();
        auto *title = new QLabel(card);
        title->setWordWrap(true);
        title->setStyleSheet(QStringLiteral("color:%1; font-size:14px;")
                                 .arg(Theme::TextPrimary));
        auto *sub = new QLabel(card);
        sub->setWordWrap(true);
        sub->setStyleSheet(QStringLiteral("color:%1; font-size:12px;")
                               .arg(Theme::TextSecond));
        mid->addWidget(title);
        mid->addWidget(sub);
        row->addLayout(mid, 1);

        auto *ops = new QVBoxLayout();
        row->addLayout(ops);

        const QString name = QFileInfo(p.fileA).fileName();
        if (p.kind == SortProblem::Unidentified) {
            title->setText(acked
                ? lang("✓ 「%1」按当前位置拼接", "✓ %1 kept as placed").arg(name)
                : lang("「%1」没认出画面上的时间", "Couldn't read time in %1")
                      .arg(name));
            sub->setText(p.detail);
            if (!acked) {
                if (!img.isEmpty()) {
                    auto *b = new QPushButton(
                        lang("框一下画面上的时间", "Box the on-screen time"), card);
                    connect(b, &QPushButton::clicked, this,
                            [this, f = p.fileA]() { roiPickForFile(f); });
                    ops->addWidget(b);
                }
                auto *bm = new QPushButton(lang("手动输入…", "Enter manually…"),
                                           card);
                connect(bm, &QPushButton::clicked, this,
                        [this, f = p.fileA]() {
                            m_selectedPath = f;
                            showManualDialog(f);
                        });
                ops->addWidget(bm);
                auto *bk = new QPushButton(
                    lang("按当前位置拼接（知道就好）", "Keep as placed (acknowledge)"),
                    card);
                connect(bk, &QPushButton::clicked, this, [this, key]() {
                    m_problemAcks.insert(key);
                    rebuildProblemPanel();
                    updateConfirmGate();
                });
                ops->addWidget(bk);
            }
        } else if (p.kind == SortProblem::OverlapPair) {
            title->setText(acked
                ? lang("✓ 两段重叠已保持现状", "✓ Overlap kept as-is")
                : lang("「%1」和「%2」的时间重叠了，哪个应该在前面？",
                       "%1 and %2 overlap in time; which comes first?")
                      .arg(name, QFileInfo(p.fileB).fileName()));
            sub->setText(p.detail);
            if (!acked) {
                auto *bs = new QPushButton(lang("交换前后", "Swap order"), card);
                connect(bs, &QPushButton::clicked, this,
                        [this, p]() {
                            if (p.groupIndex < 0 || p.groupIndex >= m_groups.size())
                                return;
                            const auto &g = m_groups[p.groupIndex];
                            QStringList ord;
                            for (const auto &e : g.ordered)
                                ord << e.filePath;
                            if (p.indexA >= 0 && p.indexB >= 0
                                && p.indexA < ord.size() && p.indexB < ord.size())
                                ord.swapItemsAt(p.indexA, p.indexB);
                            m_coord->applyGroupOrder(g.channel, ord);
                        });
                ops->addWidget(bs);
                auto *bk = new QPushButton(lang("保持现状", "Keep as-is"), card);
                connect(bk, &QPushButton::clicked, this, [this, key]() {
                    m_problemAcks.insert(key);
                    rebuildProblemPanel();
                    updateConfirmGate();
                });
                ops->addWidget(bk);
            }
        } else {   // SuspiciousGroup
            if (!p.fileA.isEmpty()) {
                title->setText(acked
                    ? lang("✓ 本组按文件信息顺序拼接", "✓ Group kept in file order")
                    : lang("这组有 %1 段没认出画面上的时间",
                           "%1 clip(s) in this group have unreadable time")
                          .arg(p.detail));
                sub->setText(acked ? QString()
                             : lang("框一段画面上的时间，其余段会自动跟着认",
                                    "Box the time in one clip; the rest follow"));
                if (!acked) {
                    auto *b = new QPushButton(
                        lang("框一段画面上的时间（全组跟着认）",
                             "Box time in one clip (applies to group)"), card);
                    connect(b, &QPushButton::clicked, this,
                            [this, f = p.fileA]() { roiPickForFile(f); });
                    ops->addWidget(b);
                    auto *bk = new QPushButton(
                        lang("按文件信息顺序拼接", "Keep file-info order"), card);
                    connect(bk, &QPushButton::clicked, this, [this, key]() {
                        m_problemAcks.insert(key);
                        rebuildProblemPanel();
                        updateConfirmGate();
                    });
                    ops->addWidget(bk);
                }
            } else {
                title->setText(acked
                    ? lang("✓ 已确认", "✓ Confirmed")
                    : lang("这组的排序证据互相矛盾，我拿不准",
                           "Conflicting ordering evidence in this group"));
                sub->setText(p.detail);
                if (!acked) {
                    auto *bk = new QPushButton(
                        lang("我确认这样排", "I confirm this order"), card);
                    connect(bk, &QPushButton::clicked, this, [this, key]() {
                        m_problemAcks.insert(key);
                        rebuildProblemPanel();
                        updateConfirmGate();
                    });
                    ops->addWidget(bk);
                }
            }
        }
        if (acked) {
            auto *undo = new QPushButton(lang("再处理", "Re-handle"), card);
            undo->setFlat(true);
            undo->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
            connect(undo, &QPushButton::clicked, this, [this, key]() {
                m_problemAcks.remove(key);
                rebuildProblemPanel();
                updateConfirmGate();
            });
            ops->addWidget(undo);
        }
        m_problemLay->addWidget(card);
    }
}

void PreprocessWindow::updateConfirmGate()
{
    if (!m_confirmBtn)
        return;
    const auto problems = collectSortProblems(m_groups, overlapTrimOn());
    int pending = 0;
    for (const auto &p : problems)
        if (!m_problemAcks.contains(problemKey(p)))
            ++pending;
    m_confirmBtn->setEnabled(pending == 0);
    m_confirmBtn->setText(pending == 0
        ? lang("✓ 确认顺序，继续拼接 →", "✓ Confirm order, merge →")
        : lang("还剩 %1 处待确认", "%1 issue(s) to confirm").arg(pending));
}

QString PreprocessWindow::evidenceImageFor(const QString &filePath) const
{
    const OcrResult o = m_coord->ocrMap().value(filePath);
    if (!o.firstFrameImg.isEmpty())
        return o.firstFrameImg;
    for (const auto &g : m_groups)
        for (const auto &e : g.ordered)
            if (e.filePath == filePath)
                return e.thumbnailFirst;
    return QString();
}

void PreprocessWindow::refreshReviewSummary()
{
    int total = 0, ocrCount = 0, manualCount = 0, unknownCount = 0;
    int absStartCount = 0, fnameCount = 0;
    int gaps = 0, overlaps = 0;
    qint64 gapMs = 0, overlapMs = 0;
    int dubious = 0;
    for (const auto &g : m_groups) {
        total += g.ordered.size();
        for (const auto &e : g.ordered) {
            if (e.sourceKind == SortEvidenceKind::Ocr)
                ++ocrCount;
            else if (e.sourceKind == SortEvidenceKind::Manual)
                ++manualCount;
            else if (e.sourceKind == SortEvidenceKind::AbsStart)
                ++absStartCount;
            else if (e.sourceKind == SortEvidenceKind::Filename)
                ++fnameCount;
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
    else if (absStartCount > 0)
        basis = lang("已按流内录制时间排序", "Sorted by in-stream record time");
    else if (fnameCount > 0)
        basis = lang("已按文件名时间排序", "Sorted by filename time");
    else
        basis = lang("无法识别画面时间，按文件信息排序（请人工核对）",
                     "On-screen time unavailable; sorted by file info (please verify)");
    QStringList issues;
    if (gaps > 0)
        issues << lang("%1 处缺口（共约 %2）", "%1 gap(s) (~%2 total)")
                      .arg(gaps).arg(fmtDuration(gapMs));
    if (overlaps > 0)
        issues << (overlapTrimOn()
            ? lang("%1 处重叠（共 %2，将自动修剪）", "%1 overlap(s) (%2, auto-trimmed)")
                  .arg(overlaps).arg(fmtDuration(overlapMs))
            : lang("%1 处重叠（共 %2）", "%1 overlap(s) (%2 total)")
                  .arg(overlaps).arg(fmtDuration(overlapMs)));
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
    switch (e.sourceKind) {
    case SortEvidenceKind::Ocr:
        badge = lang("✓ 画面时间识别", "✓ On-screen time");
        badgeColor = Theme::Success;
        break;
    case SortEvidenceKind::Manual:
        badge = lang("✓ 人工确认", "✓ Manual");
        badgeColor = Theme::Success;
        break;
    case SortEvidenceKind::AbsStart:
        badge = lang("✓ 流内录制时间", "✓ In-stream record time");
        badgeColor = Theme::Info;
        break;
    case SortEvidenceKind::Filename:
        badge = lang("✓ 文件名时间", "✓ Filename time");
        badgeColor = Theme::Info;
        break;
    case SortEvidenceKind::Creation:
        badge = lang("✓ 拍摄时间(元数据)", "✓ Creation time (metadata)");
        badgeColor = Theme::Info;
        break;
    case SortEvidenceKind::Mtime:
        badge = lang("⚠ 文件修改时间（仅供参考）", "⚠ File mtime (weak evidence)");
        badgeColor = Theme::Accent;
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
    m_coord->runAutoSort();   // 校对页阶段（UserConfirm）重新 OCR 排序
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

    // 输出文件夹行已移至页面顶部「输出设置」横幅（§45，buildCaseBanner）
    // —— 原设置页路径行废弃，控件创建/连接在 buildCaseBanner；此处保持
    // 指针引用（编辑框/浏览按钮由横幅持有）
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
    // v1.12.0（2026-08-20 拍板）：重叠默认修剪、仅留痕；此项为唯一退出开关
    m_keepOverlapCheck = new QCheckBox(
        lang("保留时间重叠段原样（默认自动修剪：剪后段开头、保前段完整，报告留痕）",
             "Keep time-overlapped segments as-is (default: auto-trim — cut the later "
             "segment's head, keep the earlier one intact; noted in report)"), w);
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
    // v1.7.0 M1：编码器选择（默认软编；硬编需探测可用，见等价性评审）
    auto *encRow = new QHBoxLayout();
    encRow->addWidget(new QLabel(lang("编码器（硬编需 GPU 支持，重启后生效）：",
                                      "Encoder (HW needs GPU):"), w));
    m_encoderCombo = new QComboBox(w);
    m_encoderCombo->addItem(lang("libx264（软件，默认）", "libx264 (software, default)"),
                            QStringLiteral("libx264"));
    if (encoder_probe::availableEncoders().contains(QStringLiteral("h264_nvenc")))
        m_encoderCombo->addItem(QStringLiteral("NVENC (NVIDIA)"),
                                QStringLiteral("h264_nvenc"));
    if (encoder_probe::availableEncoders().contains(QStringLiteral("h264_qsv")))
        m_encoderCombo->addItem(QStringLiteral("QSV (Intel)"),
                                QStringLiteral("h264_qsv"));
    {
        QSettings es("LumenArc", "LumenArc");
        const QString saved = es.value("transcodeEncoder", QStringLiteral("libx264")).toString();
        const int idx = m_encoderCombo->findData(saved);
        if (idx >= 0)
            m_encoderCombo->setCurrentIndex(idx);
    }
    connect(m_encoderCombo, &QComboBox::currentIndexChanged, this, [this](int) {
        QSettings s("LumenArc", "LumenArc");
        s.setValue("transcodeEncoder",
                   m_encoderCombo->currentData().toString());
    });
    encRow->addWidget(m_encoderCombo);
    encRow->addStretch(1);
    advLay->addWidget(m_normalizeCheck);
    advLay->addWidget(m_keepOverlapCheck);
    advLay->addWidget(m_deinterlaceCheck);
    advLay->addWidget(m_sha256Check);
    advLay->addWidget(m_ignoreWarnCheck);
    advLay->addLayout(crfRow);
    advLay->addLayout(encRow);
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
    connect(btnBack, &QPushButton::clicked, this, [this]() { setStep(1); });
    connect(btnStart, &QPushButton::clicked, this, &PreprocessWindow::onStartProcessing);
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
    if (dir.isEmpty())
        return;
    // v1.7.1：案件导入模式下可自选，但必须位于案件目录内（登记要求案内）
    if (m_caseImportMode && m_caseManager && m_caseManager->isOpen()) {
        const QString caseDir = m_caseManager->caseDir();
        const QString rel = QDir(caseDir).relativeFilePath(dir);
        if (rel.startsWith(QStringLiteral("..")) || QDir::isAbsolutePath(rel)) {
            QMessageBox::warning(this, lang("输出文件夹", "Output Folder"),
                lang("案件导入模式下输出必须位于案件目录内：\n%1\n\n"
                     "如需输出到案件外，请先切换为「独立输出」。",
                     "Case-import mode requires output inside the case:\n%1\n\n"
                     "Switch to \u201cStandalone output\u201d to write elsewhere.")
                    .arg(caseDir));
            return;
        }
    }
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
    // 逐文件转码判定（现场反馈：只转确实需要的文件）
    int needTx = 0, total = 0;
    for (const auto &g : m_groups) {
        QVector<ProbeResult> ordered;
        for (const auto &e : g.ordered)
            ordered.append(m_coord->probeMap().value(e.filePath));
        needTx += filesNeedingTranscode(ordered).size();
        total += g.ordered.size();
    }
    m_radioLossless->setChecked(!m_anyBlock);
    m_radioTranscode->setChecked(m_anyBlock);
    if (m_anyBlock) {
        if (needTx >= total) {
            m_modeReason->setText(
                lang("ⓘ 检测到 %1 项不一致（如：%2），全部文件将转码后拼接",
                     "ⓘ %1 inconsistency(ies) detected (e.g. %2); all clips will be normalized")
                    .arg(blockCount).arg(firstBlock));
        } else {
            m_modeReason->setText(
                lang("ⓘ %1 个文件需要转码（格式不兼容，如：%2），其余直接无损拼接",
                     "ⓘ %1 clip(s) need transcoding (e.g. %2); the rest merge losslessly")
                    .arg(needTx).arg(firstBlock));
        }
    } else {
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
    if (m_caseImportMode && m_caseManager && m_caseManager->isOpen()) {
        // 案件导入模式：显示锁定的会话目录（v1.3.0 M2 任务8）
        m_outputDirEdit->setText(caseSessionDir());
    } else if (m_outputDirEdit->text().trimmed().isEmpty() && !m_pendingFiles.isEmpty()) {
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
    // 案件导入模式：输出目录锁定案件会话目录（v1.3.0 M2 任务8）
    if (m_caseImportMode && m_caseManager && m_caseManager->isOpen())
        opts.outputDir = caseSessionDir();
    opts.crf = m_crfSpin->value();
    opts.encoder = m_encoderCombo ? m_encoderCombo->currentData().toString()
                                  : QStringLiteral("libx264");
    opts.deinterlace = m_deinterlaceCheck->isChecked();
    opts.normalizeTimestamps = m_normalizeCheck->isChecked();
    opts.ignoreWarnings = m_ignoreWarnCheck->isChecked();
    opts.withSha256 = m_sha256Check->isChecked();
    m_resultCard->setVisible(false);
    m_runProgress->setValue(0);
    m_runLog->clear();
    m_runEta->clear();
    m_runTimer.start();
    // v1.12.0（2026-08-20 拍板）：默认修剪重叠（Q-17 剪后段开头、保前段完整）、
    // 仅留痕标注；高级选项勾选"保留原样"退出。无重叠时为空操作，全路径统一应用。
    const bool keepOverlap = !overlapTrimOn();
    m_coord->setTrimOverlap(!keepOverlap);
    if (!m_overlapPendingChannels.isEmpty())
        m_runLog->appendPlainText(keepOverlap
            ? QStringLiteral("检测到时间重叠（组：%1）→ 按高级选项设置保留原样，报告标注")
                  .arg(m_overlapPendingChannels.join(QStringLiteral("、")))
            : QStringLiteral("检测到时间重叠（组：%1）→ 已默认修剪（剪后段开头、保前段完整），报告逐段留痕")
                  .arg(m_overlapPendingChannels.join(QStringLiteral("、"))));
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
    // 源素材统计 + 统一帧率醒目提示（2026-08 人工测试：8/12.5fps 混排拼接
    // 尾段卡住 → 统计入日志 + 完成页醒目提示）
    m_resultStats = new QLabel(m_resultCard);
    m_resultStats->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_resultStats->setWordWrap(true);
    m_resultStats->setStyleSheet(QStringLiteral(
        "QLabel { background:%1; border:1px solid %2; border-radius:6px;"
        " padding:6px 10px; color:%3; font-weight:bold; }")
        .arg(Theme::BgCard, Theme::Accent, Theme::TextPrimary));
    m_resultStats->setVisible(false);
    rcLay->addWidget(m_resultStats);
    m_resultEvidence = new QLabel(m_resultCard);
    m_resultEvidence->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_resultEvidence->setWordWrap(true);
    m_resultEvidence->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    rcLay->addWidget(m_resultEvidence);
    auto *rcRow = new QHBoxLayout();
    m_btnPlayOutput = new QPushButton(lang("在主窗口播放输出", "Play output in main window"), m_resultCard);
    m_btnOpenFolder = new QPushButton(lang("打开输出文件夹", "Open output folder"), m_resultCard);
    m_btnOpenReport = new QPushButton(lang("查看证据报告", "Open evidence report"), m_resultCard);
    auto *btnClose = new QPushButton(lang("完成", "Done"), m_resultCard);
    btnClose->setMinimumHeight(34);
    btnClose->setStyleSheet(m_btnBeginSort->styleSheet());
    rcRow->addWidget(m_btnPlayOutput);
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
    connect(m_btnPlayOutput, &QPushButton::clicked, this, [this]() {
        if (!m_reportOutputPath.isEmpty())
            emit openOutputRequested(m_reportOutputPath);
    });
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
        if (m_pendingQuickMerge)      // 直接拼接链：不切校对页
            break;

        setStep(1);
        break;
    case TaskPhase::Precheck:
        if (m_pendingQuickMerge)      // 直接拼接链：不切设置页
            break;

        updateSettingsPage();
        setStep(2);
        break;
    case TaskPhase::Transcoding:
    case TaskPhase::Concat:

        setStep(3);
        m_btnCancel->setEnabled(true);
        if (!m_runTimer.isValid())
            m_runTimer.start();
        break;
    default:
        break;
    }

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
    m_probeResults = results;   // 存档：结果页统计源素材帧率/编码分布
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
    if (m_pendingQuickMerge) {
        // 直接拼接链：列表顺序已就绪 → 自动确认并执行（不切校对页）
        rebuildReviewViews();
        m_importStatus->setText(lang("就绪，开始拼接…", "Ready, merging…"));
        m_coord->confirmOrder();
        return;
    }
    // P-60 GO 一键直通（方案 §4.1/§4.3，拍板 Q1A）：可信硬标准满足 →
    // 跳过人工校对直接执行（估算段等在完成页/报告醒目标识，不静默）
    // v1.12.0（2026-08-20 拍板）：默认修剪策略下重叠不再阻断直通——
    // 自动修剪（Q-17）+ 报告/日志留痕替代人工裁决
    if (canAutoProceed(groups, overlapTrimOn())) {
        rebuildReviewViews();   // 结果页统计与留痕仍用 m_groups
        m_importStatus->setText(
            lang("分析完成，顺序可信——直接开始拼接…",
                 "Analysis done, order trusted — merging…"));
        // 默认修剪（2026-08-20 拍板）：直通路径同样生效——可补上排序器
        // 推算视角漏掉的 OCR 实证重叠（抽帧源 ocrEndMs > endMs）；无重叠空操作
        m_coord->setTrimOverlap(overlapTrimOn());
        m_coord->startProcessing(collectProcessingOptions());
        return;
    }
    rebuildReviewViews();
    m_importStatus->setText(lang("分析完成，请校对顺序（可直接开始拼接）",
                                 "Analysis done; review order (or merge directly)"));

    setStep(1);
}

void PreprocessWindow::onPrecheckReady(const QMap<QString, PrecheckResult> &)
{
    updateSettingsPage();
    if (m_pendingQuickMerge) {
        m_pendingQuickMerge = false;
        m_coord->startProcessing(m_pendingOpts);
    }
}

/// v1.7.0 M2 → v1.12.0 改判（2026-08-20 用户拍板）：Precheck 检测到时间重叠 →
/// 默认修剪（Q-17：剪后段开头保前段完整），不再弹窗询问、仅留痕标注；
/// 实际修剪在「开始拼接」时生效（onStartProcessing），用户可在设置页
/// 高级选项勾选"保留原样"退出修剪。
void PreprocessWindow::onOverlapDetected(const QStringList &channels)
{
    if (channels.isEmpty())
        return;
    m_overlapPendingChannels = channels;
    // 设置页明示（precheckReady 先于本信号，updateSettingsPage 已填好主文案）
    if (m_modeReason)
        m_modeReason->setText(m_modeReason->text() + QStringLiteral("\n") +
            lang("ⓘ 检测到时间重叠（组 %1）：默认自动修剪（剪后段开头、保前段完整），"
                 "报告逐段留痕；如需保留原样见下方高级选项",
                 "ⓘ Time overlap detected (group %1): auto-trim by default (later "
                 "segment's head cut, earlier kept intact), noted per segment in report; "
                 "to keep as-is see Advanced options below")
                .arg(channels.join(QStringLiteral("、"))));
}

/// 源素材统计 + 统一帧率提示（与 coordinator::logProbeStats 同源同公式：
/// 全局最大 avg fps，四舍五入到 0.1，上限 60）
QString PreprocessWindow::buildProbeStatsText() const
{
    QMap<double, int> fpsCnt;
    QMap<QString, int> codecCnt;
    float maxFps = 0.0f;
    int okN = 0;
    for (const auto &r : m_probeResults) {
        if (!r.ok())
            continue;
        ++okN;
        fpsCnt[qRound(r.fps * 10.0) / 10.0] += 1;
        codecCnt[r.videoCodec] += 1;
        if (r.fps > maxFps)
            maxFps = r.fps;
    }
    if (okN == 0)
        return QString();
    const float unified = qBound(1.0f, qRound(maxFps * 10.0f) / 10.0f, 60.0f);
    QStringList fpsParts;
    for (auto it = fpsCnt.constBegin(); it != fpsCnt.constEnd(); ++it)
        fpsParts << QStringLiteral("%1fps×%2").arg(it.key()).arg(it.value());
    QStringList codecParts;
    for (auto it = codecCnt.constBegin(); it != codecCnt.constEnd(); ++it)
        codecParts << QStringLiteral("%1×%2").arg(it.key()).arg(it.value());
    const QString base = lang("源素材 %1 段：帧率 %2；编码 %3",
                              "Sources: %1 clips; fps %2; codec %3")
        .arg(okN).arg(fpsParts.join(QStringLiteral("、")),
                      codecParts.join(QStringLiteral("、")));
    if (fpsCnt.size() > 1)
        return lang("⚠ %1\n帧率不统一，已统一按 %2fps 转码拼接（低帧率段自动补帧，"
                    "体积影响可忽略；统计详见运行日志）",
                    "⚠ %1\nFPS differ; all segments were normalized to %2fps "
                    "before merging (dup frames, negligible size impact; "
                    "stats in the run log)")
            .arg(base).arg(unified);
    return lang("✓ %1（帧率统一，无补帧）", "✓ %1 (uniform FPS, no dup frames)")
        .arg(base);
}

void PreprocessWindow::onFinished(const PreprocessReport &report)
{
    m_runProgress->setValue(100);
    m_runStatus->setText(lang("处理完成", "Done"));
    m_runEta->clear();
    m_btnCancel->setEnabled(false);
    m_reportCsv = report.reportCsvPath;
    m_reportOutputPath = report.outputPath;
    m_outputDir = report.outputPath.isEmpty()
        ? report.evidenceDir : QFileInfo(report.outputPath).absolutePath();

    const QFileInfo fi(report.outputPath);
    const bool haveOutput = !report.outputPath.isEmpty()
        && fi.isFile() && fi.size() > 0;
    // 单文件已是合格 MP4：未执行转码/拼接（协调器该路径不生成 CSV 证据）
    const bool noOp = haveOutput && report.reportCsvPath.isEmpty()
        && m_pendingFiles.size() == 1
        && report.outputPath == m_pendingFiles.first();
    if (noOp) {
        m_resultTitle->setText(lang("✓ 无需处理", "✓ Nothing to do"));
        m_resultTitle->setStyleSheet(QStringLiteral(
            "font-size:18px; font-weight:bold; color:%1;").arg(Theme::Success));
        m_resultOutput->setText(lang(
            "该文件已是合格 MP4（H.264、关键帧 ≤2.5 秒），可直接在主窗口播放；"
            "无需转码或拼接。",
            "This file is already a valid MP4 (H.264, keyframes ≤2.5s); "
            "playable in the main window. No transcode or merge needed."));
        m_btnPlayOutput->setEnabled(true);
        m_resultEvidence->setText(lang("未执行转码/拼接，未生成证据报告。",
                                       "Nothing was processed; no evidence report."));
        m_resultCard->setVisible(true);
        setStep(3);
        m_btnBeginSort->setEnabled(!m_pendingFiles.isEmpty());
        m_btnQuickMerge->setEnabled(!m_pendingFiles.isEmpty());
        m_caseSessionDir.clear();   // 未产出新会话：下轮重新生成目录
        // 单文件场景也展示源统计（若已探测）
        if (!m_probeResults.isEmpty()) {
            m_resultStats->setText(buildProbeStatsText());
            m_resultStats->setVisible(true);
        }
        // 明确弹窗（与“分析完成”同风格，5 秒自动关）
        QTimer::singleShot(0, this, [this]() {
            auto *box = new QMessageBox(QMessageBox::Information, windowTitle(),
                lang("该文件已是合格 MP4（H.264、关键帧 ≤2.5 秒），无需处理。",
                     "Already a valid MP4 (H.264, keyframes ≤2.5s); nothing to do."),
                QMessageBox::Ok, this);
            box->setAttribute(Qt::WA_DeleteOnClose);
            QTimer::singleShot(5000, box, &QWidget::close);
            box->show();
        });
        return;
    }
    // 案件登记结果附注（v1.3.0 M2；拼到证据报告文案后，避免被覆写）
    QString caseRegNote;
    if (haveOutput) {
        m_resultTitle->setText(lang("✓ 拼接完成", "✓ Merge finished"));
        m_resultTitle->setStyleSheet(QStringLiteral(
            "font-size:18px; font-weight:bold; color:%1;").arg(Theme::Success));
        m_resultOutput->setText(lang("输出文件：%1（%2，用时 %3）", "Output: %1 (%2, took %3)")
            .arg(report.outputPath, fmtBytes(fi.size()),
                 fmtDuration(m_runTimer.elapsed())));
        m_btnPlayOutput->setEnabled(true);
        // 源素材统计 + 统一帧率醒目提示（2026-08；与 operations.log 同源同公式）
        {
            QString statsText = buildProbeStatsText();
            // P-60：估算段（位置为推算）完成页醒目标识（直通模式尤其需要）
            int estTotal = 0;
            for (const auto &g : m_groups)
                estTotal += estimatedCount(g);
            if (estTotal > 0)
                statsText += lang("\n⚠ 其中 %1 段未识别到时间，位置为推算"
                                  "（详见证据报告「衔接警告」列）",
                                  "\n⚠ %1 clip(s) placed by estimation "
                                  "(see report warnings column)").arg(estTotal);
            m_resultStats->setText(statsText);
            m_resultStats->setVisible(true);
        }

        // v1.3.0 M2 任务8：案件导入模式 finalize 自动登记 ——
        // 会话目录/报告/输出引用 + sidecar 复制归类 sidecars/ 入 case.json
        if (m_caseImportMode && m_caseManager && m_caseManager->isOpen()) {
            QStringList sidecars;
            const QStringList outputs = report.outputPaths.isEmpty()
                ? QStringList{report.outputPath} : report.outputPaths;
            for (const QString &o : outputs) {
                const QString sc = o + QStringLiteral(".lumencal.json");
                if (QFile::exists(sc))
                    sidecars << sc;
            }
            QString regErr;
            if (m_caseManager->addPreprocessSession(
                    m_caseSessionDir, report.reportCsvPath, outputs,
                    sidecars, report.outputChannels, &regErr)) {
                QString saveErr;
                if (!m_caseManager->saveCase(&saveErr))
                    regErr = saveErr;
            }
            caseRegNote = regErr.isEmpty()
                ? lang("\n已登记案件：%1（sidecar 归类 sidecars/）",
                       "\nRegistered into case: %1 (sidecars → sidecars/)")
                      .arg(m_caseSessionDir)
                : lang("\n⚠ 案件登记失败：%1",
                       "\n⚠ Case registration failed: %1").arg(regErr);
        }
        m_caseSessionDir.clear();   // 本次运行结束：下轮重新生成会话目录
    } else {
        // 防现场反馈：未产出文件时禁止绿勾成功
        m_resultTitle->setText(lang("⚠ 未产出输出文件", "⚠ No output file produced"));
        m_resultTitle->setStyleSheet(QStringLiteral(
            "font-size:18px; font-weight:bold; color:%1;").arg(Theme::Danger));
        m_resultOutput->setText(lang(
            "拼接流程已结束但没有任何输出文件（详见详细日志与证据报告）。",
            "Merge finished but produced no output (see log and evidence report)."));
        m_btnPlayOutput->setEnabled(false);
        m_resultStats->setVisible(false);
    }
    m_resultEvidence->setText(lang("证据报告：%1\n（首/尾帧截图、CSV 明细、操作日志均已留档）",
                                   "Evidence report: %1\n(frames, CSV, operation log archived)")
                                  .arg(report.evidenceDir)
                              + caseRegNote);
    m_resultCard->setVisible(true);

    setStep(3);
    // 任务结束：允许回导入页重试/换批次
    m_btnBeginSort->setEnabled(!m_pendingFiles.isEmpty());
    m_btnQuickMerge->setEnabled(!m_pendingFiles.isEmpty());
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
    case PreprocessError::ConcatFailed:
        plain = lang("拼接未产出任何输出文件（转码/拼接均未成功）。\n"
                     "可在导入页重新导入后重试，或检查证据目录 operations.log 定位原因。",
                     "No output produced (transcode/concat failed).\n"
                     "Re-import and retry, or inspect operations.log in the evidence dir.");
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

    setStep(3);
    // 失败后允许回导入页重试
    m_btnBeginSort->setEnabled(!m_pendingFiles.isEmpty());
    m_btnQuickMerge->setEnabled(!m_pendingFiles.isEmpty());
    // §45（2026-08-17）：转码/拼接失败 → 提议开启 NAL 完整性快检（默认不自动开启）
    // 坏文件（moov/mdat 错位）会令无损拼接/转码中止，快检逐个定位并出报告
    if (error == PreprocessError::ConcatFailed
        || error == PreprocessError::TranscodeFailed
        || error == PreprocessError::Timeout) {
        auto *box = new QMessageBox(QMessageBox::Question,
            lang("定位失败原因", "Find the cause"),
            lang("处理失败。源文件可能存在数据损坏（moov 索引与 mdat 错位会使拼接/"
                 "转码中止）。\n\n开启数据完整性快检将逐个检查全部输入文件（百文件约"
                 "1-2 分钟），坏文件标红并在报告文件中列出。",
                 "The operation failed. Source clips may be corrupted (moov/mdat "
                 "misalignment aborts merge/transcode).\n\nRunning an integrity check "
                 "scans every input file (~1-2 min per 100 clips) and lists the "
                 "corrupted ones in a report."), QMessageBox::NoButton, this);
        box->setAttribute(Qt::WA_DeleteOnClose);
        auto *btnCheck = box->addButton(
            lang("开启完整性快检…", "Run integrity check…"), QMessageBox::ActionRole);
        box->addButton(QMessageBox::Close);
        connect(btnCheck, &QPushButton::clicked,
                this, [this]() { runIntegrityCheck(); });
        box->open();
    }
}

void PreprocessWindow::runIntegrityCheck()
{
    if (m_pendingFiles.isEmpty())
        return;
    if (!m_checker)
        m_checker = new NalIntegrityChecker(this);
    connect(m_checker, &NalIntegrityChecker::finished,
            this, &PreprocessWindow::onIntegrityFinished, Qt::UniqueConnection);
    connect(m_checker, &NalIntegrityChecker::failed,
            this, [](const QString &d) {
                QMessageBox::warning(nullptr,
                    QStringLiteral("完整性快检"), QStringLiteral("无法运行：%1").arg(d));
            }, Qt::UniqueConnection);
    m_importStatus->setText(lang("完整性快检进行中…（可在结果页查看）",
                                 "Integrity check running… (see result page)"));
    m_importStatus->setStyleSheet(QStringLiteral(
        "color:%1;").arg(Theme::Accent));
    m_checker->check(m_pendingFiles, ToolPaths::findFfmpegPath());
}

void PreprocessWindow::onIntegrityFinished(
    const QVector<NalIntegrityChecker::Result> &results)
{
    QVector<NalIntegrityChecker::Result> bad;
    for (const auto &r : results)
        if (r.errorCount > 0)
            bad.append(r);
    // 文件列表状态列：坏文件标红
    for (int i = 0; i < m_fileTable->rowCount(); ++i) {
        auto *nameItem = m_fileTable->item(i, 0);
        if (!nameItem)
            continue;
        auto *st = m_fileTable->item(i, 5);
        if (!st)
            continue;
        const QString f = nameItem->data(Qt::UserRole).toString();
        bool isBad = false;
        for (const auto &r : bad)
            if (r.filePath == f) { isBad = true; break; }
        st->setText(isBad ? lang("❌ 数据损坏", "❌ corrupted")
                          : lang("✓ 完整", "✓ ok"));
        if (isBad)
            st->setForeground(Qt::red);
    }
    // 报告落盘：输出目录（未设则素材目录）
    QString dir = m_outputDirEdit ? m_outputDirEdit->text().trimmed() : QString();
    if (dir.isEmpty() && !m_pendingFiles.isEmpty())
        dir = QFileInfo(m_pendingFiles.first()).absolutePath();
    QString reportPath;
    if (!dir.isEmpty()) {
        const QString fn = QStringLiteral("完整性快检报告_%1.txt")
            .arg(QDateTime::currentDateTime().toString(
                     QStringLiteral("yyyyMMdd_HHmmss")));
        reportPath = dir + QLatin1Char('/') + fn;
        QFile f(reportPath);
        if (f.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream ts(&f);
            ts << QStringLiteral("LumenArc 数据完整性快检报告\n")
               << QStringLiteral("检查文件数：%1   损坏数：%2\n\n")
                   .arg(results.size()).arg(bad.size());
            for (const auto &r : results)
                ts << (r.errorCount > 0
                           ? QStringLiteral("❌ %1：%2 处 NAL 错误\n")
                               .arg(r.filePath).arg(r.errorCount)
                           : QStringLiteral("✓ %1\n").arg(r.filePath));
            ts.flush();
        }
    }
    // 结果汇总
    QString html = lang("检查 %1 个文件，<b>%2 个损坏</b>。\n\n",
                         "Checked %1 clip(s), <b>%2 corrupted</b>.\n\n")
                       .arg(results.size()).arg(bad.size());
    for (int i = 0; i < qMin(bad.size(), 30); ++i)
        html += QStringLiteral("%1 (%2)\n").arg(bad[i].filePath)
                    .arg(bad[i].errorCount);
    if (bad.size() > 30)
        html += QStringLiteral("…等共 %1 个\n").arg(bad.size());
    auto *box = new QMessageBox(
        bad.isEmpty() ? QMessageBox::Information : QMessageBox::Warning,
        lang("完整性快检结果", "Integrity check result"), html,
        QMessageBox::NoButton, this);
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->addButton(QMessageBox::Close);
    if (!reportPath.isEmpty())
        box->setInformativeText(lang("完整报告：%1", "Full report: %1").arg(reportPath));
    box->open();
    m_importStatus->setText(lang("完整性快检完成：%1 个损坏",
                                 "Integrity check done: %1 corrupted").arg(bad.size()));
    m_importStatus->setStyleSheet(QString());
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
    // --- 导入表键盘：Del/Backspace 删除选中行（现场反馈：个别错了无法删除） ---
    if (m_fileTable && watched == m_fileTable) {
        if (event->type() == QEvent::KeyPress) {
            auto *ke = static_cast<QKeyEvent *>(event);
            if (ke->key() == Qt::Key_Delete || ke->key() == Qt::Key_Backspace) {
                const int row = m_fileTable->currentRow();
                const TaskPhase ph = m_coord->phase();
                if (row >= 0 && (ph == TaskPhase::Idle || ph == TaskPhase::Done
                                 || ph == TaskPhase::Failed
                                 || ph == TaskPhase::Cancelled)) {
                    m_fileTable->removeRow(row);
                    syncPendingFromTable();
                    m_btnBeginSort->setEnabled(!m_pendingFiles.isEmpty());
                    m_btnQuickMerge->setEnabled(!m_pendingFiles.isEmpty());
                    m_importSummary->setText(
                        lang("已导入 %1 段（可拖拽行调整顺序；当前顺序即拼接顺序）",
                             "%1 clip(s) (drag rows to reorder; row order = merge order)")
                            .arg(m_pendingFiles.size()));
                    return true;
                }
            }
        }
        return false;
    }

    // --- 导入表 viewport：自建行拖拽（插入语义，不依赖 Qt 内置拖放） ---
    if (m_fileTable && watched == m_fileTable->viewport()) {
        switch (event->type()) {
        case QEvent::MouseButtonPress: {
            auto *me = static_cast<QMouseEvent *>(event);
            if (me->button() == Qt::LeftButton) {
                m_tableDragRow = m_fileTable->indexAt(me->pos()).row();
                m_dragStartPos = me->pos();
            }
            break;
        }
        case QEvent::MouseButtonRelease:
            m_tableDragRow = -1;
            break;
        case QEvent::MouseMove: {
            auto *me = static_cast<QMouseEvent *>(event);
            // 拖拽进行中：吞掉 MouseMove，防 Qt 默认 current/hover 蓝框
            // （现场反馈“文件名下方有一条蓝色不美观”）
            auto *st = static_cast<SortableFileTable *>(m_fileTable);
            if (st->dragActive)
                return true;
            if (m_tableDragRow >= 0 && (me->buttons() & Qt::LeftButton)
                && (me->pos() - m_dragStartPos).manhattanLength() > 10) {
                auto *drag = new QDrag(m_fileTable);
                auto *mime = new QMimeData;
                mime->setData(QStringLiteral("application/x-lumenarc-filerow"),
                              QByteArray::number(m_tableDragRow));
                drag->setMimeData(mime);
                // 拖拽视觉：整行按原比例半透明化（现场反馈：小标签卡看不清，
                // 要求“把整行拖出来”的感觉）
                const int row = m_tableDragRow;
                if (row >= 0) {
                    const QRect rowRect = m_fileTable->visualRect(
                        m_fileTable->model()->index(row, 0));
                    const QPixmap full = m_fileTable->grab(rowRect);
                    QPixmap pm(full.size());
                    pm.fill(Qt::transparent);
                    QPainter p(&pm);
                    p.setRenderHint(QPainter::SmoothPixmapTransform);
                    p.setOpacity(0.72);                      // 半透明
                    p.drawPixmap(0, 0, full);
                    p.end();
                    drag->setPixmap(pm);
                    // 热点在行首中部：落点与行对齐，视觉上“行被拎起”
                    drag->setHotSpot(QPoint(12, pm.height() / 2));
                }
                m_tableDragRow = -1;
                drag->exec(Qt::MoveAction);
                return true;
            }
            break;
        }
        case QEvent::DragEnter:
        case QEvent::DragMove: {
            auto *de = static_cast<QDragMoveEvent *>(event);
            if (de->mimeData()->hasFormat(
                    QStringLiteral("application/x-lumenarc-filerow"))) {
                auto *st = static_cast<SortableFileTable *>(m_fileTable);
                st->dragActive = true;
                // 目的地指示：目标行 + 行中心上/下缘（与校对页卡片一致）
                const QModelIndex idx = m_fileTable->indexAt(
                    de->position().toPoint());
                if (idx.isValid()) {
                    const QRect vr = m_fileTable->visualRect(idx);
                    st->setDropIndicator(idx.row(),
                        (de->position().toPoint().y() - vr.top())
                            > vr.height() / 2);
                } else {
                    st->setDropIndicator(m_fileTable->rowCount() - 1, true);
                }
                de->acceptProposedAction();
                return true;
            }
            break;
        }
        case QEvent::DragLeave: {
            auto *st = static_cast<SortableFileTable *>(m_fileTable);
            st->dragActive = false;
            st->clearDropIndicator();
            break;
        }
        case QEvent::Drop: {
            auto *de = static_cast<QDropEvent *>(event);
            auto *st = static_cast<SortableFileTable *>(m_fileTable);
            st->dragActive = false;
            st->clearDropIndicator();
            if (st->handleRowDrop(de->mimeData(), de->position().toPoint())) {
                syncPendingFromTable();
                de->acceptProposedAction();
                return true;
            }
            if (de->mimeData()->hasFormat(
                    QStringLiteral("application/x-lumenarc-filerow"))) {
                de->acceptProposedAction();   // 落点无效但属于行拖拽：消化掉
                return true;
            }
            break;
        }
        default:
            break;
        }
        return false;
    }

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
