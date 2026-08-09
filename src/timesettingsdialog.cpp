/**
 * @file timesettingsdialog.cpp
 * @brief 校时窗口实现：GO 一键自动校时 + 高级折叠区（v1.2.1 重构）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-09
 * @version 2.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "timesettingsdialog.h"
#include "app/calibration_service.h"
#include "i18n.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QLabel>
#include <QPushButton>
#include <QTableWidget>
#include <QHeaderView>
#include <QDateTimeEdit>
#include <QLineEdit>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFrame>
#include <QDateTime>
#include <QFileInfo>
#include <QPixmap>

namespace {
QString fmtStreamMs(qint64 ms)
{
    const int h = int(ms / 3600000);
    const int m = int(ms % 3600000 / 60000);
    const int s = int(ms % 60000 / 1000);
    return QStringLiteral("%1:%2:%3")
        .arg(h, 2, 10, QChar('0')).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}
} // namespace

TimeSettingsDialog::TimeSettingsDialog(const QString &videoPath,
                                       qint64 currentPosMs,
                                       qint64 durationMs,
                                       const TimeCalibration &current,
                                       const QString &sidecarWarning,
                                       CalibrationService *service,
                                       QWidget *parent)
    : QDialog(parent)
    , m_videoPath(videoPath)
    , m_currentPosMs(currentPosMs)
    , m_durationMs(durationMs)
    , m_working(current)
    , m_sidecarWarning(sidecarWarning)
    , m_service(service)
{
    setWindowTitle(lang("视频校时", "Time Calibration"));
    setMinimumWidth(780);
    setWindowFlag(Qt::Window, true);   // 非模态：可最小化/关闭，主窗口照常操作
    buildUi();
    refreshWorkingSummary();

    if (m_service) {
        connect(m_service, &CalibrationService::progress,
                this, &TimeSettingsDialog::onServiceProgress);
        connect(m_service, &CalibrationService::threePointReady,
                this, &TimeSettingsDialog::onThreePointReady);
        connect(m_service, &CalibrationService::reconstructionReady,
                this, &TimeSettingsDialog::onReconstructionReady);
        connect(m_service, &CalibrationService::quickCheckReady,
                this, &TimeSettingsDialog::onQuickCheckReady);
        connect(m_service, &CalibrationService::failed,
                this, &TimeSettingsDialog::onServiceFailed);
        connect(m_service, &CalibrationService::absStartReady,
                this, &TimeSettingsDialog::onAbsStartReady);
        // 录像机自带时间探测（不阻塞，结果放高级区）
        m_service->probeAbsStart(videoPath);
    }
    onTruthInputChanged();
}

void TimeSettingsDialog::buildUi()
{
    auto *lay = new QVBoxLayout(this);

    // ---- 头部：视频与当前状态 ----
    m_videoLabel = new QLabel(QStringLiteral("📹 %1\n%2 %3")
        .arg(QFileInfo(m_videoPath).fileName())
        .arg(lang("当前播放位置：", "Current position: "))
        .arg(fmtStreamMs(m_currentPosMs)), this);
    m_videoLabel->setWordWrap(true);
    lay->addWidget(m_videoLabel);

    m_workingSummary = new QLabel(this);
    m_workingSummary->setWordWrap(true);
    m_workingSummary->setStyleSheet(QStringLiteral("color:#8a8;font-weight:bold;"));
    lay->addWidget(m_workingSummary);

    if (!m_sidecarWarning.isEmpty()) {
        m_sidecarWarnLabel = new QLabel(
            lang("⚠ 此拼接文件段间存在时间缺口/重叠，首段之后的墙钟可能不准（详见报告）",
                 "⚠ Concatenated file has time gaps/overlaps; wall clock after the "
                 "first segment may drift (see report)"), this);
        m_sidecarWarnLabel->setWordWrap(true);
        m_sidecarWarnLabel->setStyleSheet(QStringLiteral("color:#e8a33d;"));
        lay->addWidget(m_sidecarWarnLabel);
    }

    // ---- 顶部用法横幅（参考拼接窗口格式横幅）----
    lay->addWidget(buildUsageBanner());

    // ---- 第 1 步：自动校时 ----
    auto *grpGo = new QGroupBox(lang("第 1 步 · 自动校时", "Step 1 · Auto calibrate"), this);
    auto *gg = new QVBoxLayout(grpGo);
    auto *goRow = new QHBoxLayout();
    m_goBtn = new QPushButton(lang("🔍 GO 自动校时", "🔍 GO"), this);
    m_goBtn->setMinimumHeight(44);
    m_goBtn->setStyleSheet(QStringLiteral(
        "QPushButton { font-size:15px; font-weight:bold; }"));
    m_cancelBtn = new QPushButton(lang("取消", "Cancel"), this);
    m_cancelBtn->setVisible(false);
    goRow->addWidget(m_goBtn, 3);
    goRow->addWidget(m_cancelBtn, 1);
    gg->addLayout(goRow);
    m_progressLabel = new QLabel(this);
    m_progressLabel->setStyleSheet(QStringLiteral("color:#666;"));
    gg->addWidget(m_progressLabel);
    // 结果（GO 完成后出现）
    m_resultLabel = new QLabel(lang(
        "（点击上方 GO 开始）", "(click GO above to start)"), this);
    m_resultLabel->setWordWrap(true);
    m_resultLabel->setStyleSheet(QStringLiteral("font-weight:bold;"));
    gg->addWidget(m_resultLabel);
    auto *rr = new QHBoxLayout();
    m_detailsBtn = new QPushButton(lang("查看细节 ▸", "Details ▸"), this);
    m_detailsBtn->setEnabled(false);
    m_useBtn = new QPushButton(lang("✅ 使用此结果", "✅ Use this result"), this);
    m_useBtn->setEnabled(false);
    m_useBtn->setMinimumWidth(140);
    rr->addWidget(m_detailsBtn);
    rr->addStretch(1);
    rr->addWidget(m_useBtn);
    gg->addLayout(rr);
    // 细节折叠容器
    m_detailsBox = new QWidget(this);
    auto *gd = new QVBoxLayout(m_detailsBox);
    gd->setContentsMargins(0, 0, 0, 0);
    m_sampleTable = new QTableWidget(0, 7, this);
    m_sampleTable->setHorizontalHeaderLabels(
        {lang("采用", "Use"), lang("播放位置", "Position"),
         lang("识别时间", "OCR time"), lang("OCR 原文", "Raw text"),
         lang("可靠度", "Conf"), lang("证据帧", "Frame"),
         lang("异常", "Suspect")});
    m_sampleTable->horizontalHeader()->setStretchLastSection(true);
    m_sampleTable->verticalHeader()->setVisible(false);
    m_sampleTable->setMinimumHeight(120);
    m_sampleTable->setMaximumHeight(220);
    m_sampleTable->setIconSize(QSize(160, 90));
    connect(m_sampleTable, &QTableWidget::itemChanged,
            this, &TimeSettingsDialog::onSampleItemChanged);
    gd->addWidget(m_sampleTable);
    m_fitWarningLabel = new QLabel(this);
    m_fitWarningLabel->setWordWrap(true);
    m_fitWarningLabel->setStyleSheet(QStringLiteral("color:#e8a33d;"));
    gd->addWidget(m_fitWarningLabel);
    m_noDriftCheck = new QCheckBox(lang("不修正时钟快慢（仅对基准）",
                                        "Ignore clock drift (offset only)"), this);
    gd->addWidget(m_noDriftCheck);
    m_detailsBox->hide();
    gg->addWidget(m_detailsBox);
    lay->addWidget(grpGo);

    // ---- 第 2 步：对真实时间（北京时间，可选）----
    auto *grpTruth = new QGroupBox(lang("第 2 步 · 对真实时间（可选）",
                                        "Step 2 · Align to real time (optional)"), this);
    auto *gt = new QVBoxLayout(grpTruth);
    auto *truthExplain = new QLabel(lang(
        "监控画面里的时间可能整体慢/快于真实北京时间（如监控主机从未对时）。\n"
        "第 1 步解决「视频进度 ↔ 画面时间」，这一步解决「画面时间 ↔ 真实时间」。\n"
        "做法：暂停画面读当前显示时间（或拍照同时拍到手机时间与画面时间），"
        "把真实北京时间填到下方，软件自动算出偏移并应用。",
        "The on-screen clock may be offset from real Beijing time (e.g. recorder "
        "never synced). Step 1 maps playback↔on-screen time; this step maps "
        "on-screen↔real time. Pause and read the on-screen time (or photograph "
        "phone + screen together), enter the real Beijing time below."), this);
    truthExplain->setWordWrap(true);
    truthExplain->setStyleSheet(QStringLiteral("color:#666;"));
    gt->addWidget(truthExplain);
    auto *grid = new QGridLayout();
    m_monitorTimeLabel = new QLabel(this);
    grid->addWidget(new QLabel(lang("画面上的时间（自动读取）：", "On-screen time (auto): "), this), 0, 0);
    grid->addWidget(m_monitorTimeLabel, 0, 1);
    grid->addWidget(new QLabel(lang("真实北京时间：", "Actual Beijing time: "), this), 1, 0);
    m_beijingEdit = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    m_beijingEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_beijingEdit->setCalendarPopup(true);
    grid->addWidget(m_beijingEdit, 1, 1);
    m_truthPreviewLabel = new QLabel(this);
    m_truthPreviewLabel->setStyleSheet(QStringLiteral("font-weight:bold;"));
    grid->addWidget(m_truthPreviewLabel, 2, 1);
    m_truthNoteEdit = new QLineEdit(this);
    m_truthNoteEdit->setPlaceholderText(
        lang("说明（如：与指挥中心对时），留档用", "Note (e.g. synced with HQ), for record"));
    grid->addWidget(m_truthNoteEdit, 3, 0, 1, 2);
    m_adoptTruthBtn = new QPushButton(lang("使用此偏移", "Use this offset"), this);
    m_clearTruthBtn = new QPushButton(lang("清除偏移", "Clear"), this);
    grid->addWidget(m_adoptTruthBtn, 4, 0);
    grid->addWidget(m_clearTruthBtn, 4, 1);
    gt->addLayout(grid);
    lay->addWidget(grpTruth);

    // ---- 第 3 步：高级（折叠）----
    auto *grpMore = new QGroupBox(lang("第 3 步 · 高级（点击展开）",
                                       "Step 3 · Advanced (expand)"), this);
    grpMore->setCheckable(true);
    grpMore->setChecked(false);
    auto *gm = new QVBoxLayout(grpMore);
    m_advancedBox = new QWidget(this);
    auto *ga = new QVBoxLayout(m_advancedBox);
    ga->setContentsMargins(0, 0, 0, 0);

    // 手动输入
    auto *gManual = new QGroupBox(lang("手动输入画面时间", "Manual on-screen time"), this);
    auto *gman = new QHBoxLayout(gManual);
    m_manualEdit = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    m_manualEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_manualEdit->setCalendarPopup(true);
    m_adoptManualBtn = new QPushButton(lang("使用", "Use"), this);
    gman->addWidget(new QLabel(lang("当前播放位置画面显示：", "On-screen time here: "), this));
    gman->addWidget(m_manualEdit, 1);
    gman->addWidget(m_adoptManualBtn);
    ga->addWidget(gManual);

    // 录像机自带时间
    auto *gAbs = new QGroupBox(lang("录像机自带时间（免识别）", "Recorder time (no OCR)"), this);
    auto *gab = new QHBoxLayout(gAbs);
    m_absLabel = new QLabel(lang("探测中…", "Probing…"), this);
    m_adoptAbsBtn = new QPushButton(lang("使用", "Use"), this);
    m_adoptAbsBtn->setEnabled(false);
    gab->addWidget(m_absLabel, 1);
    gab->addWidget(m_adoptAbsBtn);
    ga->addWidget(gAbs);

    // 变速文件重建（强制入口）
    auto *gRecon = new QGroupBox(lang("变速文件时间重建", "Variable-rate reconstruction"), this);
    auto *grec = new QVBoxLayout(gRecon);
    auto *rrow = new QHBoxLayout();
    m_reconForceBtn = new QPushButton(lang("强制变速重建", "Force reconstruction"), this);
    m_reconForceBtn->setToolTip(lang(
        "对疑似抽帧/变速文件做全片密集取样重建时间映射（耗时数分钟）。"
        "通常「GO 自动校时」会自动判断并执行，此按钮供手动触发。",
        "Dense sampling over the whole clip to rebuild the time map for "
        "variable-rate files (minutes). GO usually handles this automatically."));
    rrow->addWidget(m_reconForceBtn);
    rrow->addStretch(1);
    grec->addLayout(rrow);
    m_reconSummaryLabel = new QLabel(lang("未运行。", "Not run yet."), this);
    m_reconSummaryLabel->setWordWrap(true);
    grec->addWidget(m_reconSummaryLabel);
    m_segmentTable = new QTableWidget(0, 5, this);
    m_segmentTable->setHorizontalHeaderLabels(
        {lang("段", "Seg"), lang("播放范围", "Range"),
         lang("时钟快慢", "Rate"), lang("画面时间起点", "OSD start"),
         lang("说明", "Note")});
    m_segmentTable->horizontalHeader()->setStretchLastSection(true);
    m_segmentTable->verticalHeader()->setVisible(false);
    m_segmentTable->setMinimumHeight(90);
    m_segmentTable->setMaximumHeight(160);
    grec->addWidget(m_segmentTable);
    ga->addWidget(gRecon);

    gm->addWidget(m_advancedBox);
    m_advancedBox->hide();
    lay->addWidget(grpMore);
    connect(grpMore, &QGroupBox::toggled, this, [this, grpMore](bool on) {
        m_advancedBox->setVisible(on);
        grpMore->setTitle(lang(on ? "第 3 步 · 高级（点击收起）"
                                  : "第 3 步 · 高级（点击展开）",
                               on ? "Step 3 · Advanced (collapse)"
                                  : "Step 3 · Advanced (expand)"));
    });

    // ---- 底部 ----
    auto *hint = new QLabel(lang(
        "提示：识别/重建在后台进行，可最小化窗口继续其他操作；"
        "关闭窗口不取消任务，重新打开可查看进度与结果。",
        "Tip: tasks run in background; minimize to keep working. "
        "Closing keeps the task running; reopen to see progress/result."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#888;"));
    lay->addWidget(hint);
    auto *bbox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bbox);

    connect(m_goBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onRunGo);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onCancelGo);
    connect(m_detailsBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onToggleDetails);
    connect(m_useBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onUseResult);
    connect(m_reconForceBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onRunReconForce);
    connect(m_adoptAbsBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onAdoptAbsStart);
    connect(m_adoptManualBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onAdoptManual);
    connect(m_beijingEdit, &QDateTimeEdit::dateTimeChanged,
            this, [this](const QDateTime &) { onTruthInputChanged(); });
    connect(m_adoptTruthBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onAdoptTruth);
    connect(m_clearTruthBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onClearTruth);
    connect(m_noDriftCheck, &QCheckBox::toggled,
            this, &TimeSettingsDialog::onNoDriftCorrectionToggled);
}

QWidget *TimeSettingsDialog::buildUsageBanner()
{
    auto *banner = new QFrame(this);
    banner->setStyleSheet(QStringLiteral(
        "QFrame { background:#f4f7fb; border:1px solid #c8d4e4; border-radius:8px; }"
        "QLabel { border:none; background:transparent; }"));
    auto *lay = new QVBoxLayout(banner);
    lay->setContentsMargins(12, 8, 12, 8);
    lay->setSpacing(4);
    auto *title = new QLabel(lang("用法", "Usage"), banner);
    title->setStyleSheet(QStringLiteral("font-weight:bold;"));
    lay->addWidget(title);
    auto *desc = new QLabel(
        lang("点下方 GO → 自动识别画面时间 → 自动应用\n"
             "│\n"
             "│ GO 会自动判断：\n"
             "│ - 正常录像 → 自动三点识别（画面时间基准 + 时钟快慢）\n"
             "│ - 抽帧/变速文件 → 自动重建（按画面时间恢复整片映射，需数分钟）\n"
             "│ - 完成后自动应用，图表时间轴立即变为画面时间\n"
             "│\n"
             "│ 画面时间与真实时间对不上（如监控钟慢）？用第 2 步对真实时间。",
             "Click GO below → auto-read on-screen time → auto-applied\n"
             "│\n"
             "│ GO decides automatically:\n"
             "│ - normal recording → 3-point OCR (time base + clock drift)\n"
             "│ - variable-rate / sampled file → reconstruction from frames (minutes)\n"
             "│ - applied automatically; chart axis becomes on-screen time\n"
             "│\n"
             "│ On-screen time differs from real time? Use Step 2."),
        banner);
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color:#555;"));
    lay->addWidget(desc);
    return banner;
}

// ---------------------------------------------------------------------------
QString TimeSettingsDialog::fmtWall(qint64 epochMs)
{
    return QDateTime::fromMSecsSinceEpoch(epochMs)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
}

QString TimeSettingsDialog::fmtOffset(qint64 offsetMs)
{
    const bool neg = offsetMs < 0;
    const qint64 a = qAbs(offsetMs);
    QString body;
    if (a >= 3600000)
        body = QStringLiteral("%1h%2m").arg(a / 3600000).arg(a % 3600000 / 60000);
    else if (a >= 60000)
        body = QStringLiteral("%1m%2s").arg(a / 60000).arg(a % 60000 / 1000);
    else
        body = QStringLiteral("%1s").arg(a / 1000.0, 0, 'f', 1);
    return (neg ? QStringLiteral("-") : QStringLiteral("+")) + body;
}

void TimeSettingsDialog::refreshWorkingSummary()
{
    if (!m_working.isValid()) {
        m_workingSummary->setText(lang("当前状态：未校时", "Status: not calibrated"));
    } else {
        QString src;
        switch (m_working.source) {
        case TimeCalibration::Source::Ocr:       src = lang("画面识别", "OCR"); break;
        case TimeCalibration::Source::AbsStart:  src = lang("录像机自带", "In-stream"); break;
        case TimeCalibration::Source::Manual:    src = lang("手动", "Manual"); break;
        case TimeCalibration::Source::Inherited: src = lang("前处理继承", "Inherited"); break;
        default: src = QStringLiteral("—"); break;
        }
        QString text = lang("当前状态：已校时（来源：%1）画面时间基准 = %2",
                            "Calibrated (source: %1); on-screen base = %2")
            .arg(src).arg(m_working.dateKnown ? fmtWall(m_working.offsetMs)
                                              : fmtStreamMs(m_working.offsetMs));
        if (m_working.rateApplied)
            text += lang("；时钟每天快/慢 %1 秒", "; clock drift %1 s/day")
                .arg(m_working.driftSecondsPerDay(), 0, 'f', 1);
        if (m_working.piecewiseMode())
            text += lang("；分段重建 %1 段（变速）", "; piecewise %1 segs (variable-rate)")
                .arg(m_working.piecewise.size());
        if (m_working.truthSet)
            text += lang("；北京时间偏移 %1", "; Beijing offset %1")
                .arg(fmtOffset(m_working.truthOffsetMs));
        m_workingSummary->setText(text);
    }
    // 北京时间对齐区显示当前播放位置换算出的画面时间
    if (m_working.isValid() && m_working.dateKnown)
        m_monitorTimeLabel->setText(fmtWall(m_working.wallMsOf(m_currentPosMs)));
    else
        m_monitorTimeLabel->setText(lang("（需先完成自动校时）",
                                         "(run auto calibrate first)"));
    onTruthInputChanged();
}

// ---------------------------------------------------------------------------
// GO 状态机：快速检查 → 自动路由（正常→三点识别 / 变速→时间重建）
// ---------------------------------------------------------------------------
void TimeSettingsDialog::onRunGo()
{
    if (!m_service)
        return;
    if (m_goStage == GoStage::Quick || m_goStage == GoStage::Ocr
        || m_goStage == GoStage::Recon)
        return;
    m_goStage = GoStage::Quick;
    m_autoApplied = false;
    m_useBtn->setEnabled(false);
    m_useBtn->setText(lang("✅ 使用此结果", "✅ Use this result"));
    m_detailsBtn->setEnabled(false);
    m_detailsBox->hide();
    m_detailsVisible = false;
    m_detailsBtn->setText(lang("查看细节 ▸", "Details ▸"));
    m_resultLabel->setText(lang(
        "快速检查中…（读取首尾画面时间，约 10 秒）",
        "Quick-checking… (reading first/last on-screen time, ~10s)"));
    setGoBusy(true, lang("快速检查中…", "Quick-checking…"));
    m_service->runQuickCheck(m_videoPath, m_durationMs);
}

void TimeSettingsDialog::onCancelGo()
{
    if (m_service)
        m_service->cancel();
    m_goStage = GoStage::Idle;
    setGoBusy(false, QString());
    m_resultLabel->setText(lang("已取消。可重新点击「自动校时」。",
                                "Cancelled. Click \"Auto calibrate\" to retry."));
}

void TimeSettingsDialog::onQuickCheckReady(const QString &videoPath,
                                           double overallRate,
                                           bool suspicious)
{
    if (videoPath != m_videoPath)
        return;
    if (suspicious) {
        // 疑似变速：自动进入时间重建
        m_goStage = GoStage::Recon;
        m_resultLabel->setText(lang(
            "检测到疑似变速文件（画面时间约为播放进度的 %1 倍），"
            "正在按画面时间重建…（需数分钟，可最小化窗口）",
            "Variable-rate file detected (on-screen time ~%1x playback). "
            "Reconstructing… (minutes; window can be minimized).")
                .arg(overallRate, 0, 'f', 2));
        m_reconSummaryLabel->setText(lang("重建中…", "Reconstructing…"));
        m_segmentTable->setRowCount(0);
        setGoBusy(true, lang("重建中…", "Reconstructing…"));
        m_service->runReconstruction(m_videoPath, m_durationMs);
    } else {
        // 正常文件：自动进入三点识别
        m_goStage = GoStage::Ocr;
        m_resultLabel->setText(lang(
            "文件时间正常。正在三点识别…（几十秒）",
            "Normal recording. Running 3-point OCR… (tens of seconds)"));
        setGoBusy(true, lang("识别中…", "Recognizing…"));
        m_service->runThreePoint(m_videoPath, m_currentPosMs, m_durationMs);
    }
}

void TimeSettingsDialog::onThreePointReady(const QString &videoPath,
                                           const TimeCalibration &proposed)
{
    if (videoPath != m_videoPath)
        return;
    m_goStage = GoStage::Done;
    setGoBusy(false, QString());
    m_fitResult = proposed;
    fillSampleTable(proposed);
    refitSummaryRefresh();
    // 无异常 → 自动应用（结果区与状态栏即时反馈）
    maybeAutoApply();
}

void TimeSettingsDialog::onReconstructionReady(const QString &videoPath,
                                               const TimeCalibration &proposed)
{
    if (videoPath != m_videoPath)
        return;
    m_goStage = GoStage::Done;
    setGoBusy(false, QString());
    m_reconResult = proposed;
    fillSampleTable(proposed);

    if (!proposed.piecewiseMode()) {
        // 预检误判或强制重建遇到正常文件：走仿射结果
        m_resultLabel->setText(lang(
            "结果：正常录像（无变速边界）。可用「自动校时」重新识别。",
            "Result: normal recording (no rate boundaries). "
            "Re-run \"Auto calibrate\" if needed."));
        m_useBtn->setEnabled(true);
        m_detailsBtn->setEnabled(true);
        return;
    }

    fillSegmentTable(proposed);
    int suspicious = 0;
    for (const auto &s : proposed.samples)
        if (s.ocrSuspicious)
            ++suspicious;
    QString summary = lang(
        "检测到 %1 段变速（画面时间已按帧重建，精度 ±2 秒）。",
        "%1 variable-rate segments found; time rebuilt from frames (±2s).")
        .arg(proposed.piecewise.size());
    if (suspicious > 0)
        summary += lang(" %1 个异常测点已自动排除（⚠，见细节）。",
                        " %1 OCR-suspect samples auto-excluded (⚠, see details).")
                       .arg(suspicious);
    if (proposed.audioKnown) {
        summary += lang(" 音频校验：%1。",
                        " Audio check: %1.")
                       .arg(proposed.audioConsistent ? lang("吻合", "OK")
                                                     : lang("不吻合", "MISMATCH"));
    }
    m_resultLabel->setText(summary);
    m_reconSummaryLabel->setText(summary);
    m_useBtn->setEnabled(true);
    m_detailsBtn->setEnabled(true);
    // 无异常 → 自动应用（结果区与状态栏即时反馈）
    maybeAutoApply();
}

void TimeSettingsDialog::onUseResult()
{
    if (m_fitResult.isValid() && m_fitResult.source == TimeCalibration::Source::Ocr
        && !m_fitResult.piecewiseMode()) {
        if (m_noDriftCheck->isChecked())
            m_fitResult.rateApplied = false;
        applyWorking(m_fitResult);
    } else if (m_reconResult.piecewiseMode()) {
        applyWorking(m_reconResult);
    }
}

void TimeSettingsDialog::applyWorking(const TimeCalibration &cal)
{
    m_working = cal;
    m_working.calibratedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_applied = true;
    m_autoApplied = true;
    refreshWorkingSummary();
    emit calibrationApplied(m_working);
}

void TimeSettingsDialog::maybeAutoApply()
{
    // 三点结果：拟合有效且无"速率异常"警告 → 自动应用
    if (m_fitResult.isValid() && m_fitResult.source == TimeCalibration::Source::Ocr
        && !m_fitResult.piecewiseMode()) {
        const TimeCalibration::FitResult fr = TimeCalibration::fit(m_fitResult.samples);
        if (fr.ok
            && fr.warning != TimeCalibration::FitWarning::RateInsane) {
            if (m_noDriftCheck->isChecked())
                m_fitResult.rateApplied = false;
            applyWorking(m_fitResult);
            m_useBtn->setEnabled(false);
            m_useBtn->setText(lang("✅ 已应用", "✅ Applied"));
            m_resultLabel->setText(lang("✅ 已应用：%1", "✅ Applied: %1")
                                       .arg(m_resultLabel->text()));
            return;
        }
        // 速率异常：不自动应用，等用户确认
        m_useBtn->setText(lang("确认使用此结果", "Use anyway"));
        return;
    }
    // 重建结果：分段有效 → 自动应用
    if (m_reconResult.piecewiseMode()) {
        applyWorking(m_reconResult);
        m_useBtn->setEnabled(false);
        m_useBtn->setText(lang("✅ 已应用", "✅ Applied"));
        m_resultLabel->setText(lang("✅ 已应用：%1", "✅ Applied: %1")
                                   .arg(m_resultLabel->text()));
    }
}

void TimeSettingsDialog::onRunReconForce()
{
    if (!m_service)
        return;
    m_goStage = GoStage::Recon;
    m_useBtn->setEnabled(false);
    m_resultLabel->setText(lang(
        "正在按画面时间重建…（需数分钟，可最小化窗口）",
        "Reconstructing… (minutes; window can be minimized)."));
    m_reconSummaryLabel->setText(lang("重建中…", "Reconstructing…"));
    m_segmentTable->setRowCount(0);
    setGoBusy(true, lang("重建中…", "Reconstructing…"));
    m_service->runReconstruction(m_videoPath, m_durationMs);
}

void TimeSettingsDialog::onServiceProgress(const QString &stage)
{
    m_progressLabel->setText(stage);
}

void TimeSettingsDialog::onServiceFailed(const QString &videoPath,
                                         const QString &error)
{
    if (videoPath != m_videoPath)
        return;
    if (m_goStage == GoStage::Quick || m_goStage == GoStage::Ocr
        || m_goStage == GoStage::Recon) {
        m_goStage = GoStage::Failed;
        setGoBusy(false, QString());
        m_resultLabel->setText(lang(
            "失败：%1。可在「高级」中手动输入画面时间。",
            "Failed: %1. Use Advanced → manual input.").arg(error));
        return;
    }
    // 高级区强制重建失败
    m_reconSummaryLabel->setText(lang("重建失败：%1。", "Reconstruction failed: %1.")
                                     .arg(error));
}

void TimeSettingsDialog::onAbsStartReady(const QString &videoPath,
                                         qint64 absStartEpochMs)
{
    if (videoPath != m_videoPath)
        return;
    m_absStartMs = absStartEpochMs;
    if (absStartEpochMs > 0) {
        m_absLabel->setText(lang("录像机记录：%1", "Recorder time: %1")
                                .arg(fmtWall(absStartEpochMs)));
        m_adoptAbsBtn->setEnabled(true);
    } else {
        m_absLabel->setText(lang("未检出（多数文件无此信息）",
                                 "Not found (most files lack this)"));
    }
}

void TimeSettingsDialog::onSampleItemChanged(QTableWidgetItem *item)
{
    Q_UNUSED(item)
    if (!m_updatingTable && m_fitResult.isValid())
        refitFromTable();
}

void TimeSettingsDialog::refitFromTable()
{
    if (m_updatingTable || m_fitResult.samples.isEmpty())
        return;
    m_updatingTable = true;
    for (int i = 0; i < m_fitResult.samples.size()
                    && i < m_sampleTable->rowCount(); ++i) {
        auto *chk = m_sampleTable->item(i, 0);
        m_fitResult.samples[i].used =
            chk && chk->checkState() == Qt::Checked;
    }
    m_updatingTable = false;
    refitSummaryRefresh();
}

void TimeSettingsDialog::onNoDriftCorrectionToggled(bool)
{
    if (m_fitResult.isValid())
        refitSummaryRefresh();
}

void TimeSettingsDialog::refitSummaryRefresh()
{
    // 按勾选状态重拟合（野点剔除重拟合）后刷新一句话结果
    const TimeCalibration::FitResult fr = TimeCalibration::fit(m_fitResult.samples);
    m_fitResult.applyFit(fr);
    QString text = fr.ok
        ? lang("识别 %1 个取样点；画面时间基准 = %2",
               "%1 samples; on-screen time base = %2")
              .arg(fr.pointsUsed).arg(fmtWall(fr.offsetMs))
        : lang("有效取样点不足", "Not enough samples");
    if (fr.ok && fr.pointsUsed >= 2) {
        text += lang("；时钟每天快/慢 %1 秒", "; clock drift %1 s/day")
                    .arg(fr.driftSecondsPerDay(), 0, 'f', 1);
    }
    m_resultLabel->setText(text);
    QString warn;
    bool adoptable = fr.ok;
    if (fr.warning == TimeCalibration::FitWarning::RateInsane) {
        warn = lang("⚠ 识别速率异常（疑似误读），不予采用；可在高级区手动输入",
                    "⚠ Insane fitted rate (likely misread); use manual input");
        adoptable = false;
    } else if (fr.warning == TimeCalibration::FitWarning::OutlierSuspected) {
        warn = lang("⚠ 有取样点异常：可取消勾选该点，将自动重新计算",
                    "⚠ Outlier suspected: uncheck the row to recompute");
    }
    m_fitWarningLabel->setText(warn);
    if (!m_autoApplied)
        m_useBtn->setEnabled(adoptable);
    m_detailsBtn->setEnabled(true);
}

void TimeSettingsDialog::fillSampleTable(const TimeCalibration &proposed)
{
    m_updatingTable = true;
    m_sampleTable->setRowCount(proposed.samples.size());
    for (int i = 0; i < proposed.samples.size(); ++i) {
        const auto &s = proposed.samples[i];
        auto *chk = new QTableWidgetItem();
        chk->setCheckState(s.used ? Qt::Checked : Qt::Unchecked);
        chk->setFlags(Qt::ItemIsUserCheckable | Qt::ItemIsEnabled);
        m_sampleTable->setItem(i, 0, chk);
        auto *pos = new QTableWidgetItem(fmtStreamMs(s.streamMs));
        pos->setFlags(pos->flags() & ~Qt::ItemIsEditable);
        m_sampleTable->setItem(i, 1, pos);
        auto *wall = new QTableWidgetItem(fmtWall(s.wallMs));
        wall->setFlags(wall->flags() & ~Qt::ItemIsEditable);
        m_sampleTable->setItem(i, 2, wall);
        auto *raw = new QTableWidgetItem(s.rawText);
        raw->setFlags(raw->flags() & ~Qt::ItemIsEditable);
        raw->setToolTip(s.rawText);
        m_sampleTable->setItem(i, 3, raw);
        auto *conf = new QTableWidgetItem(QString::number(s.conf, 'f', 2));
        conf->setFlags(conf->flags() & ~Qt::ItemIsEditable);
        m_sampleTable->setItem(i, 4, conf);
        auto *img = new QTableWidgetItem();
        img->setFlags(img->flags() & ~Qt::ItemIsEditable);
        if (!s.frameImgPath.isEmpty() && QFileInfo::exists(s.frameImgPath)) {
            QPixmap pm(s.frameImgPath);
            if (!pm.isNull())
                img->setData(Qt::DecorationRole,
                             pm.scaled(160, 90, Qt::KeepAspectRatio,
                                       Qt::SmoothTransformation));
        }
        img->setToolTip(s.frameImgPath);
        m_sampleTable->setItem(i, 5, img);
        auto *sus = new QTableWidgetItem(
            s.ocrSuspicious ? QStringLiteral("⚠") : QString());
        sus->setFlags(sus->flags() & ~Qt::ItemIsEditable);
        if (s.ocrSuspicious)
            sus->setToolTip(lang("OCR 疑似错读，已自动排除",
                                 "OCR suspect (auto-excluded)"));
        m_sampleTable->setItem(i, 6, sus);
    }
    m_updatingTable = false;
}

void TimeSettingsDialog::fillSegmentTable(const TimeCalibration &proposed)
{
    const auto &segs = proposed.piecewise.segments;
    m_segmentTable->setRowCount(segs.size());
    for (int i = 0; i < segs.size(); ++i) {
        const auto &s = segs[i];
        m_segmentTable->setItem(i, 0,
            new QTableWidgetItem(QString::number(i + 1)));
        const QString range = (i + 1 < segs.size())
            ? QStringLiteral("%1 – %2")
                  .arg(fmtStreamMs(s.streamStartMs))
                  .arg(fmtStreamMs(segs[i + 1].streamStartMs))
            : QStringLiteral("%1 – …").arg(fmtStreamMs(s.streamStartMs));
        m_segmentTable->setItem(i, 1, new QTableWidgetItem(range));
        m_segmentTable->setItem(i, 2,
            new QTableWidgetItem(QString::number(s.rate, 'f', 3)));
        m_segmentTable->setItem(i, 3,
            new QTableWidgetItem(fmtWall(s.wallStartMs)));
        m_segmentTable->setItem(i, 4,
            new QTableWidgetItem(lang(
                "按画面时间重建", "rebuilt from frames")));
    }
}

void TimeSettingsDialog::onToggleDetails()
{
    m_detailsVisible = !m_detailsVisible;
    m_detailsBox->setVisible(m_detailsVisible);
    m_detailsBtn->setText(lang(m_detailsVisible ? "收起细节 ▾" : "查看细节 ▸",
                               m_detailsVisible ? "Hide details ▾" : "Details ▸"));
}

void TimeSettingsDialog::setGoBusy(bool busy, const QString &stageText)
{
    m_cancelBtn->setVisible(busy);
    if (busy) {
        m_goBtn->setText(stageText);
    } else {
        m_goBtn->setText(m_goStage == GoStage::Done
            ? lang("✓ 完成（可重新校时）", "✓ Done (re-run)")
            : lang("🔍 自动校时", "🔍 Auto calibrate"));
    }
    m_progressLabel->setText(busy ? stageText : QString());
}

// ---------------------------------------------------------------------------
// 高级区：北京时间对齐 / 手动 / 录像机自带
// ---------------------------------------------------------------------------
void TimeSettingsDialog::onTruthInputChanged()
{
    if (!m_working.isValid() || !m_working.dateKnown) {
        m_truthPreviewLabel->clear();
        return;
    }
    const qint64 monitorWall = m_working.wallMsOf(m_currentPosMs);
    const qint64 offset = m_beijingEdit->dateTime().toMSecsSinceEpoch()
                          - monitorWall;
    m_truthPreviewLabel->setText(lang(
        "偏移：%1（画面时间 %2）", "Offset: %1 (on-screen %2)")
        .arg(fmtOffset(offset)).arg(fmtWall(monitorWall)));
}

void TimeSettingsDialog::onAdoptTruth()
{
    const qint64 monitorWall = m_working.wallMsOf(m_currentPosMs);
    m_working.truthOffsetMs = m_beijingEdit->dateTime().toMSecsSinceEpoch() - monitorWall;
    m_working.truthSet = true;
    m_working.truthCheckedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_working.truthNote = m_truthNoteEdit->text().trimmed();
    m_applied = true;
    refreshWorkingSummary();
    emit calibrationApplied(m_working);
}

void TimeSettingsDialog::onClearTruth()
{
    m_working.truthOffsetMs = 0;
    m_working.truthSet = false;
    m_working.truthCheckedAtMs = 0;
    m_working.truthNote.clear();
    m_applied = true;
    refreshWorkingSummary();
    emit calibrationApplied(m_working);
}

void TimeSettingsDialog::onAdoptAbsStart()
{
    if (m_absStartMs <= 0)
        return;
    m_working = CalibrationService::fromAbsStart(m_absStartMs);
    m_applied = true;
    refreshWorkingSummary();
    m_adoptAbsBtn->setEnabled(false);
    emit calibrationApplied(m_working);
}

void TimeSettingsDialog::onAdoptManual()
{
    const qint64 wall = m_manualEdit->dateTime().toMSecsSinceEpoch();
    m_working = CalibrationService::fromSinglePoint(
        m_currentPosMs, wall, TimeCalibration::Source::Manual);
    m_applied = true;
    refreshWorkingSummary();
    emit calibrationApplied(m_working);
}
