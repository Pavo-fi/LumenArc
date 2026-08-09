/**
 * @file timesettingsdialog.cpp
 * @brief 校时窗口实现（v1.2.0）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-05
 * @version 1.0
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
    setMinimumWidth(720);
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
        // absStart 候选探测（Q-3：仅预填）
        m_service->probeAbsStart(videoPath);
        // v1.2.1 秒级预检：自动推荐合适方式（正常→① 自动；变速→③ 重建）
        if (m_service->isRunning()) {
            // 已有任务（识别/重建/预检）进行中：按钮可用，进度由状态栏展示
            m_quickLabel->setText(lang(
                "已有校时任务进行中，进度见主窗口状态栏。",
                "A calibration task is running; see the status bar."));
            m_runBtn->setEnabled(true);
            m_reconBtn->setEnabled(true);
        } else {
            m_quickLabel->setText(lang("正在快速检查文件…", "Quick-checking file…"));
            m_runBtn->setEnabled(false);
            m_reconBtn->setEnabled(false);
            m_service->runQuickCheck(videoPath, m_durationMs);
        }
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

    // v1.2.1 秒级预检推荐条
    m_quickLabel = new QLabel(this);
    m_quickLabel->setWordWrap(true);
    m_quickLabel->setStyleSheet(QStringLiteral("color:#4a7ab5;"));
    lay->addWidget(m_quickLabel);

    if (!m_sidecarWarning.isEmpty()) {
        m_sidecarWarnLabel = new QLabel(
            lang("⚠ 此拼接文件段间存在时间缺口/重叠，首段之后的墙钟可能不准（详见报告）",
                 "⚠ Concatenated file has time gaps/overlaps; wall clock after the "
                 "first segment may drift (see report)"), this);
        m_sidecarWarnLabel->setWordWrap(true);
        m_sidecarWarnLabel->setStyleSheet(QStringLiteral("color:#e8a33d;"));
        lay->addWidget(m_sidecarWarnLabel);
    }

    // ---- ① 自动校时（推荐）----
    auto *grpAuto = new QGroupBox(lang("① 自动校时（推荐）", "① Auto calibration (recommended)"), this);
    auto *ga = new QVBoxLayout(grpAuto);
    auto *row1 = new QHBoxLayout();
    m_runBtn = new QPushButton(lang("开始自动识别", "Run auto OCR"), this);
    m_runBtn->setToolTip(lang(
        "自动在开头/当前位置/结尾三处读取画面上的时间，"
        "算出时间基准和画面时钟快慢。",
        "Reads on-screen time at head/current/tail to establish the time base "
        "and camera clock drift."));
    m_progressLabel = new QLabel(this);
    m_detailsBtn = new QPushButton(lang("查看细节 ▸", "Details ▸"), this);
    m_detailsBtn->setCheckable(false);
    row1->addWidget(m_runBtn);
    row1->addWidget(m_progressLabel, 1);
    row1->addWidget(m_detailsBtn);
    ga->addLayout(row1);

    m_fitLabel = new QLabel(this);   // 一句话结果（始终可见）
    m_fitLabel->setWordWrap(true);
    m_fitLabel->setStyleSheet(QStringLiteral("font-weight:bold;"));
    ga->addWidget(m_fitLabel);

    // 详情折叠容器（默认收起：取样点表/警告/漂移开关/使用按钮）
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
    auto *row2 = new QHBoxLayout();
    m_adoptFitBtn = new QPushButton(lang("使用此结果", "Use this result"), this);
    m_adoptFitBtn->setEnabled(false);
    m_noDriftCheck = new QCheckBox(lang("不修正时钟快慢（仅对基准）",
                                        "Ignore clock drift (offset only)"), this);
    row2->addWidget(m_adoptFitBtn);
    row2->addWidget(m_noDriftCheck);
    row2->addStretch(1);
    gd->addLayout(row2);
    m_detailsBox->hide();
    ga->addWidget(m_detailsBox);
    lay->addWidget(grpAuto);

    // ---- ② 对真实时间（北京时间）----
    auto *grpTruth = new QGroupBox(lang("② 对真实时间（北京时间）",
                                        "② Align to real time (Beijing)"), this);
    auto *gt = new QGridLayout(grpTruth);
    m_monitorTimeLabel = new QLabel(this);
    gt->addWidget(new QLabel(lang("当前位置画面上的时间：", "On-screen time here: "), this), 0, 0);
    gt->addWidget(m_monitorTimeLabel, 0, 1, 1, 2);
    m_beijingEdit = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    m_beijingEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_beijingEdit->setCalendarPopup(true);
    gt->addWidget(new QLabel(lang("真实北京时间：", "Actual Beijing time: "), this), 1, 0);
    gt->addWidget(m_beijingEdit, 1, 1, 1, 2);
    m_truthPreviewLabel = new QLabel(this);
    m_truthPreviewLabel->setStyleSheet(QStringLiteral("font-weight:bold;"));
    gt->addWidget(m_truthPreviewLabel, 2, 1, 1, 2);
    m_truthNoteEdit = new QLineEdit(this);
    m_truthNoteEdit->setPlaceholderText(
        lang("说明（如：与指挥中心对时），留档用", "Note (e.g. synced with HQ), for record"));
    gt->addWidget(m_truthNoteEdit, 3, 1, 1, 2);
    m_adoptTruthBtn = new QPushButton(lang("使用此偏移", "Use this offset"), this);
    m_clearTruthBtn = new QPushButton(lang("清除偏移", "Clear"), this);
    gt->addWidget(m_adoptTruthBtn, 4, 1);
    gt->addWidget(m_clearTruthBtn, 4, 2);
    lay->addWidget(grpTruth);

    // ---- ③ 更多方式（折叠，默认收起）----
    auto *grpMore = new QGroupBox(lang("③ 更多方式 ▸", "③ More options ▸"), this);
    grpMore->setCheckable(true);
    grpMore->setChecked(false);
    auto *gm = new QVBoxLayout(grpMore);

    // 3a 时间重建（变速/抽帧文件）
    auto *grpRecon = new QGroupBox(lang("变速文件时间重建（抽帧录像专用）",
                                        "Variable-rate reconstruction (sampled recordings)"), this);
    auto *gr = new QVBoxLayout(grpRecon);
    auto *rr1 = new QHBoxLayout();
    m_reconBtn = new QPushButton(lang("开始时间重建", "Run reconstruction"), this);
    m_reconBtn->setToolTip(lang(
        "对疑似变速/抽帧文件做全片密集取样，按画面上的时间重建时间映射表"
        "（耗时数分钟，可最小化窗口继续操作）。",
        "Dense sampling over the whole clip; rebuilds the time map from "
        "on-screen time (takes minutes; window can be minimized)."));
    rr1->addWidget(m_reconBtn);
    rr1->addStretch(1);
    gr->addLayout(rr1);
    m_reconSummaryLabel = new QLabel(lang("未运行。", "Not run yet."), this);
    m_reconSummaryLabel->setWordWrap(true);
    gr->addWidget(m_reconSummaryLabel);
    m_segmentTable = new QTableWidget(0, 5, this);
    m_segmentTable->setHorizontalHeaderLabels(
        {lang("段", "Seg"), lang("播放范围", "Range"),
         lang("时钟快慢", "Rate"), lang("画面时间起点", "OSD start"),
         lang("说明", "Note")});
    m_segmentTable->horizontalHeader()->setStretchLastSection(true);
    m_segmentTable->verticalHeader()->setVisible(false);
    m_segmentTable->setMinimumHeight(90);
    m_segmentTable->setMaximumHeight(160);
    gr->addWidget(m_segmentTable);
    auto *rr2 = new QHBoxLayout();
    m_adoptReconBtn = new QPushButton(lang("使用重建结果", "Use reconstruction"), this);
    m_adoptReconBtn->setEnabled(false);
    rr2->addWidget(m_adoptReconBtn);
    rr2->addStretch(1);
    gr->addLayout(rr2);
    gm->addWidget(grpRecon);

    // 3b 录像机自带时间
    auto *grpAbs = new QGroupBox(lang("录像机自带时间（免识别）",
                                      "Recorder-provided time (no OCR)"), this);
    auto *gb = new QHBoxLayout(grpAbs);
    m_absLabel = new QLabel(lang("探测中…", "Probing…"), this);
    m_adoptAbsBtn = new QPushButton(lang("使用", "Use"), this);
    m_adoptAbsBtn->setEnabled(false);
    gb->addWidget(m_absLabel, 1);
    gb->addWidget(m_adoptAbsBtn);
    gm->addWidget(grpAbs);

    // 3c 手动输入
    auto *grpManual = new QGroupBox(lang("手动输入（当前播放位置的画面时间）",
                                         "Manual (on-screen time at current position)"), this);
    auto *gman = new QHBoxLayout(grpManual);
    m_manualEdit = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    m_manualEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_manualEdit->setCalendarPopup(true);
    m_adoptManualBtn = new QPushButton(lang("使用", "Use"), this);
    gman->addWidget(new QLabel(lang("画面显示时间：", "On-screen time: "), this));
    gman->addWidget(m_manualEdit, 1);
    gman->addWidget(m_adoptManualBtn);
    gm->addWidget(grpManual);
    lay->addWidget(grpMore);

    // ---- 底部 ----
    auto *hint = new QLabel(lang(
        "提示：识别/重建在后台进行，可最小化窗口继续其他操作；"
        "关闭窗口不取消任务，重新打开可查看进度与结果。",
        "Tip: recognition runs in background; minimize to keep working. "
        "Closing keeps the task running; reopen to see progress/result."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:#888;"));
    lay->addWidget(hint);
    auto *bbox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bbox);

    connect(m_runBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onRunThreePoint);
    connect(m_detailsBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onToggleDetails);
    connect(m_reconBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onRunRecon);
    connect(m_adoptFitBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onAdoptFit);
    connect(m_adoptReconBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onAdoptRecon);
    connect(m_adoptAbsBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onAdoptAbsStart);
    connect(m_adoptManualBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onAdoptManual);
    connect(m_beijingEdit, &QDateTimeEdit::dateTimeChanged,
            this, [this](const QDateTime &) { onTruthInputChanged(); });
    connect(m_adoptTruthBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onAdoptTruth);
    connect(m_clearTruthBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onClearTruth);
    connect(m_noDriftCheck, &QCheckBox::toggled,
            this, &TimeSettingsDialog::onNoDriftCorrectionToggled);
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
        case TimeCalibration::Source::AbsStart:  src = lang("流内录制时间", "In-stream"); break;
        case TimeCalibration::Source::Manual:    src = lang("手动", "Manual"); break;
        case TimeCalibration::Source::Inherited: src = lang("前处理继承", "Inherited"); break;
        default: src = QStringLiteral("—"); break;
        }
        QString text = lang("当前状态：已校时（来源：%1）流内 0 点 = %2",
                            "Calibrated (source: %1); stream 0 = %2")
            .arg(src).arg(m_working.dateKnown ? fmtWall(m_working.offsetMs)
                                              : fmtStreamMs(m_working.offsetMs));
        if (m_working.rateApplied)
            text += lang("；漂移修正 %1 秒/天", "; drift %1 s/day")
                .arg(m_working.driftSecondsPerDay(), 0, 'f', 1);
        if (m_working.piecewiseMode())
            text += lang("；分段重建 %1 段（变速）", "; piecewise %1 segs (variable-rate)")
                .arg(m_working.piecewise.size());
        if (m_working.truthSet)
            text += lang("；北京时间偏移 %1", "; Beijing offset %1")
                .arg(fmtOffset(m_working.truthOffsetMs));
        m_workingSummary->setText(text);
    }
    // 北京时间校验区的监控时间随工作副本刷新
    if (m_working.isValid() && m_working.dateKnown)
        m_monitorTimeLabel->setText(fmtWall(m_working.wallMsOf(m_currentPosMs)));
    else
        m_monitorTimeLabel->setText(lang("（需先完成①/②/③校时）",
                                         "(calibrate via ①/②/③ first)"));
    onTruthInputChanged();
}

// ---------------------------------------------------------------------------
void TimeSettingsDialog::onRunThreePoint()
{
    if (!m_service)
        return;
    m_taskStarted = true;
    setBusy(true, lang("取样识别中…（首/当前/尾三点，可能需要几十秒）",
                       "Sampling 3 points… (may take tens of seconds)"));
    m_adoptFitBtn->setEnabled(false);
    m_service->runThreePoint(m_videoPath, m_currentPosMs, m_durationMs);
}

void TimeSettingsDialog::onRunRecon()
{
    if (!m_service)
        return;
    m_taskStarted = true;
    m_adoptReconBtn->setEnabled(false);
    m_segmentTable->setRowCount(0);
    m_reconSummaryLabel->setText(lang(
        "重建中…（粗采样分段 + 边界加密，全程可能数分钟）",
        "Reconstructing… (coarse sampling + boundary refinement, may take minutes)"));
    setBusy(true, lang("时间重建中…", "Reconstructing…"));
    m_service->runReconstruction(m_videoPath, m_durationMs);
}

void TimeSettingsDialog::onReconstructionReady(const QString &videoPath,
                                               const TimeCalibration &proposed)
{
    if (videoPath != m_videoPath)
        return;
    setBusy(false);
    m_reconResult = proposed;

    if (!proposed.piecewiseMode()) {
        // 正常录像：无分段，走仿射（与三点识别等效）
        m_reconSummaryLabel->setText(lang(
            "检测结果：正常录像（无变速边界，整体速率 %1）。可切换用 ① 三点识别查看拟合详情。",
            "Result: normal recording (no rate boundaries, overall rate %1). "
            "Use ① 3-point OCR for fit details.")
                .arg(proposed.rate, 0, 'f', 4));
        m_segmentTable->setRowCount(0);
        return;
    }

    // 分段表
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
                "按画面 OSD 重建", "rebuilt from OSD")));
    }

    QString summary = lang(
        "检测到 %1 个变速边界（%2 段），疑似抽帧/变速导出，"
        "已按画面 OSD 重建时间映射。",
        "%1 rate boundaries (%2 segments) found: variable-rate file; "
        "time map rebuilt from OSD.")
        .arg(proposed.boundaryCount).arg(segs.size());
    // v1.2.1：OCR 异常测点提示（错读点时间不可信，报告中须标注）
    int suspicious = 0;
    for (const auto &s : proposed.samples)
        if (s.ocrSuspicious)
            ++suspicious;
    if (suspicious > 0) {
        summary += lang(
            " 检测到 %1 个 OCR 异常测点（⚠，已自动排除）："
            "其 OSD 读数疑似错读，引用时须以证据帧为准。",
            " %1 OCR-suspect samples (⚠, auto-excluded): readings may be "
            "wrong; verify against evidence frames.")
            .arg(suspicious);
    }
    if (proposed.audioKnown) {
        summary += lang(
            " 音频时长校验：%1（%2 分 vs OSD 跨度 %3 分）。",
            " Audio check: %1 (%2 min vs OSD span %3 min).")
            .arg(proposed.audioConsistent ? lang("吻合", "OK")
                                          : lang("不吻合", "MISMATCH"))
            .arg(proposed.totalWallSpanSec / 60.0, 0, 'f', 0);
        // 上句里 audioKnown 分支的第二个参数复用同一跨度
        if (!proposed.audioConsistent) {
            summary += lang(
                " 注意：OSD 跨度与音频时长不一致，边界可能漏检，建议复核。",
                " OSD span differs from audio: boundaries may be missed, re-check.");
        }
    }
    m_reconSummaryLabel->setText(summary);
    m_adoptReconBtn->setEnabled(true);
}

void TimeSettingsDialog::onAdoptRecon()
{
    if (!m_reconResult.piecewiseMode())
        return;
    m_working = m_reconResult;
    m_applied = true;
    refreshWorkingSummary();
    emit calibrationApplied(m_working);
}

void TimeSettingsDialog::onServiceProgress(const QString &stage)
{
    if (m_runBtn && !m_runBtn->isEnabled())
        m_progressLabel->setText(stage);
    // 进度同时转发主窗口状态栏（由 MainWindow 统一连 service，此处不再转发）
    Q_UNUSED(stage)
}

void TimeSettingsDialog::onQuickCheckReady(const QString &videoPath,
                                           double overallRate,
                                           bool suspicious)
{
    if (videoPath != m_videoPath)
        return;
    if (!suspicious) {
        m_quickLabel->setText(lang(
            "✅ 快速检查：文件时间正常。使用 ① 自动校时即可。",
            "✅ Quick check: normal recording. Use ① auto calibration."));
    } else {
        m_quickLabel->setText(lang(
            "⚠ 快速检查：画面时间与播放进度差异较大（约 %1 倍），"
            "疑似抽帧/变速文件。请使用 ③ 更多方式 → 变速文件时间重建。",
            "⚠ Quick check: on-screen time vs playback rate differs (~%1x); "
            "likely a sampled/variable-rate file. Use ③ More → reconstruction.")
                .arg(overallRate, 0, 'f', 2));
    }
    m_runBtn->setEnabled(true);
    m_reconBtn->setEnabled(true);
}

void TimeSettingsDialog::onToggleDetails()
{
    m_detailsVisible = !m_detailsVisible;
    m_detailsBox->setVisible(m_detailsVisible);
    m_detailsBtn->setText(lang(m_detailsVisible ? "收起细节 ▾" : "查看细节 ▸",
                               m_detailsVisible ? "Hide details ▾" : "Details ▸"));
}

void TimeSettingsDialog::onThreePointReady(const QString &videoPath,
                                           const TimeCalibration &proposed)
{
    if (videoPath != m_videoPath)
        return;
    setBusy(false);
    m_fitResult = proposed;

    // 填充测点表
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
        // v1.2.1：OCR 异常标注（错读点已自动排除，时间不可信）
        auto *sus = new QTableWidgetItem(
            s.ocrSuspicious ? QStringLiteral("⚠") : QString());
        sus->setFlags(sus->flags() & ~Qt::ItemIsEditable);
        if (s.ocrSuspicious)
            sus->setToolTip(lang("OCR 疑似错读，已自动排除",
                                 "OCR suspect (auto-excluded)"));
        m_sampleTable->setItem(i, 6, sus);
    }
    m_updatingTable = false;

    // 拟合结果摘要（复用重拟合刷新逻辑）
    refitSummaryRefresh();
}

void TimeSettingsDialog::onServiceFailed(const QString &videoPath,
                                         const QString &error)
{
    if (videoPath != m_videoPath)
        return;
    // 未启动任何识别/重建 → 这是秒级预检失败（如无 OSD）：只更新推荐条
    if (!m_taskStarted) {
        m_quickLabel->setText(lang(
            "⚠ 快速检查失败（%1）。可能是画面中没有时间显示，"
            "可尝试 ③ 更多方式 → 手动输入。",
            "⚠ Quick check failed (%1). No on-screen time? Try ③ More → manual.")
                .arg(error));
        m_runBtn->setEnabled(true);
        m_reconBtn->setEnabled(true);
        return;
    }
    setBusy(false);
    m_fitLabel->setText(lang("自动识别失败：%1。可改用手动校时（③）",
                             "Auto OCR failed: %1. Use manual (③)").arg(error));
    m_adoptFitBtn->setEnabled(false);
    m_reconSummaryLabel->setText(lang(
        "重建失败：%1。可改用手动校时（③）。",
        "Reconstruction failed: %1. Use manual (③).").arg(error));
    m_adoptReconBtn->setEnabled(false);
}

void TimeSettingsDialog::onAbsStartReady(const QString &videoPath,
                                         qint64 absStartEpochMs)
{
    if (videoPath != m_videoPath)
        return;
    m_absStartMs = absStartEpochMs;
    m_absLabel->setText(lang("检测到流内录制起点：%1", "In-stream start detected: %1")
        .arg(fmtWall(absStartEpochMs)));
    m_adoptAbsBtn->setEnabled(true);
}

void TimeSettingsDialog::onSampleItemChanged(QTableWidgetItem *item)
{
    if (m_updatingTable || !item || item->column() != 0)
        return;
    refitFromTable();
}

void TimeSettingsDialog::refitFromTable()
{
    // 按勾选状态更新测点（Q-2 方案：野点剔除重拟合）
    for (int i = 0; i < m_sampleTable->rowCount() && i < m_fitResult.samples.size(); ++i)
        m_fitResult.samples[i].used =
            m_sampleTable->item(i, 0)->checkState() == Qt::Checked;
    refitSummaryRefresh();
}

void TimeSettingsDialog::onNoDriftCorrectionToggled(bool on)
{
    // 仅影响采用时的 rateApplied 标志（摘要行在采用时一并处理）
    if (on)
        m_fitResult.rateApplied = false;
    else
        m_fitResult.applyFit(TimeCalibration::fit(m_fitResult.samples));
}

void TimeSettingsDialog::refitSummaryRefresh()
{
    // 按勾选状态重拟合（Q-2 方案：野点剔除重拟合）后重刷摘要与警告
    const TimeCalibration::FitResult fr = TimeCalibration::fit(m_fitResult.samples);
    m_fitResult.applyFit(fr);
    const double drift = fr.driftSecondsPerDay();
    QString fitText = fr.ok
        ? lang("拟合：%1 个测点；流内 0 点 = %2",
               "Fit: %1 samples; stream 0 = %2")
              .arg(fr.pointsUsed).arg(fmtWall(fr.offsetMs))
        : lang("有效测点不足", "Not enough samples");
    if (fr.ok && fr.pointsUsed >= 2) {
        fitText += lang("；走时速率偏差 %1 秒/天", "; drift %1 s/day").arg(drift, 0, 'f', 1);
        fitText += fr.rateSignificant
            ? lang("（显著，将应用修正）", " (significant, will apply)")
            : lang("（不显著，按固定偏移）", " (not significant, fixed offset)");
    }
    m_fitLabel->setText(fitText);

    QString warn;
    bool adoptable = fr.ok;
    if (fr.warning == TimeCalibration::FitWarning::RateInsane) {
        warn = lang("⚠ 识别速率异常（疑似 OCR 误读），不予采用；请检查测点或改手动校时",
                    "⚠ Insane fitted rate (likely OCR misread); inspect samples or use manual");
        adoptable = false;
    } else if (fr.warning == TimeCalibration::FitWarning::OutlierSuspected) {
        warn = lang("⚠ 有测点残差异常：可取消勾选该测点，将自动重新拟合",
                    "⚠ Outlier suspected: uncheck the row to refit automatically");
    }
    m_fitWarningLabel->setText(warn);
    m_adoptFitBtn->setEnabled(adoptable);
}

void TimeSettingsDialog::onTruthInputChanged()
{
    const bool canCheck = m_working.isValid() && m_working.dateKnown;
    const qint64 monitorWall = canCheck ? m_working.wallMsOf(m_currentPosMs) : 0;
    const qint64 beijing = m_beijingEdit->dateTime().toMSecsSinceEpoch();
    const qint64 offset = canCheck ? beijing - monitorWall : 0;
    if (canCheck) {
        m_truthPreviewLabel->setText(
            lang("整体偏移 = 北京时间 − 监控时间 = %1（将全局应用于此视频）",
                 "Offset = Beijing − monitor = %1 (applies to whole video)")
                .arg(fmtOffset(offset)));
        m_adoptTruthBtn->setEnabled(true);
    } else {
        m_truthPreviewLabel->setText(lang("请先完成校时，再校验北京时间",
                                          "Calibrate first, then check Beijing time"));
        m_adoptTruthBtn->setEnabled(false);
    }
    m_clearTruthBtn->setEnabled(m_working.truthSet);
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

void TimeSettingsDialog::setBusy(bool busy, const QString &text)
{
    m_runBtn->setEnabled(!busy);
    m_reconBtn->setEnabled(!busy);
    m_progressLabel->setText(busy ? text : QString());
}

// ---------------------------------------------------------------------------
// 采用（Q-3：仅此时才生效）
// ---------------------------------------------------------------------------
void TimeSettingsDialog::onAdoptFit()
{
    // 尊重「不应用漂移修正」勾选
    if (m_noDriftCheck->isChecked())
        m_fitResult.rateApplied = false;
    m_working = m_fitResult;
    m_working.calibratedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_applied = true;
    refreshWorkingSummary();
    emit calibrationApplied(m_working);
    m_adoptFitBtn->setEnabled(false);
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
