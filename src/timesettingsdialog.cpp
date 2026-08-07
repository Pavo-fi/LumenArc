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
    buildUi();
    refreshWorkingSummary();

    if (m_service) {
        connect(m_service, &CalibrationService::progress,
                this, &TimeSettingsDialog::onServiceProgress);
        connect(m_service, &CalibrationService::threePointReady,
                this, &TimeSettingsDialog::onThreePointReady);
        connect(m_service, &CalibrationService::failed,
                this, &TimeSettingsDialog::onServiceFailed);
        connect(m_service, &CalibrationService::absStartReady,
                this, &TimeSettingsDialog::onAbsStartReady);
        // absStart 候选探测（Q-3：仅预填）
        m_service->probeAbsStart(videoPath);
    }
    onTruthInputChanged();
}

void TimeSettingsDialog::buildUi()
{
    auto *lay = new QVBoxLayout(this);

    // ---- 头部：视频与当前位置 ----
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

    // ---- ① 自动识别校时 ----
    auto *grpAuto = new QGroupBox(lang("① 自动识别校时（首/当前/尾三点画面识别）",
                                       "① Auto calibration (3-point OSD OCR)"), this);
    auto *ga = new QVBoxLayout(grpAuto);
    auto *row1 = new QHBoxLayout();
    m_runBtn = new QPushButton(lang("开始三点识别", "Run 3-point OCR"), this);
    m_progressLabel = new QLabel(this);
    row1->addWidget(m_runBtn);
    row1->addWidget(m_progressLabel, 1);
    ga->addLayout(row1);

    m_sampleTable = new QTableWidget(0, 6, this);
    m_sampleTable->setHorizontalHeaderLabels(
        {lang("采用", "Use"), lang("流内位置", "Stream"),
         lang("识别时间", "OCR time"), lang("OCR 原文", "Raw text"),
         lang("置信", "Conf"), lang("证据帧", "Frame")});
    m_sampleTable->horizontalHeader()->setStretchLastSection(true);
    m_sampleTable->verticalHeader()->setVisible(false);
    m_sampleTable->setMinimumHeight(120);
    m_sampleTable->setMaximumHeight(220);
    m_sampleTable->setIconSize(QSize(160, 90));
    connect(m_sampleTable, &QTableWidget::itemChanged,
            this, &TimeSettingsDialog::onSampleItemChanged);
    ga->addWidget(m_sampleTable);

    m_fitLabel = new QLabel(this);
    m_fitLabel->setWordWrap(true);
    ga->addWidget(m_fitLabel);
    m_fitWarningLabel = new QLabel(this);
    m_fitWarningLabel->setWordWrap(true);
    m_fitWarningLabel->setStyleSheet(QStringLiteral("color:#e8a33d;"));
    ga->addWidget(m_fitWarningLabel);
    auto *row2 = new QHBoxLayout();
    m_adoptFitBtn = new QPushButton(lang("采用此识别结果", "Adopt this result"), this);
    m_adoptFitBtn->setEnabled(false);
    m_noDriftCheck = new QCheckBox(lang("不应用漂移修正（仅固定偏移）",
                                        "No drift correction (fixed offset only)"), this);
    row2->addWidget(m_adoptFitBtn);
    row2->addWidget(m_noDriftCheck);
    row2->addStretch(1);
    ga->addLayout(row2);
    lay->addWidget(grpAuto);

    // ---- ② 流内录制时间（absStart，免识别） ----
    auto *grpAbs = new QGroupBox(lang("② 流内录制时间（录像机写入，免识别）",
                                      "② In-stream recording time (no OCR needed)"), this);
    auto *gb = new QHBoxLayout(grpAbs);
    m_absLabel = new QLabel(lang("探测中…", "Probing…"), this);
    m_adoptAbsBtn = new QPushButton(lang("采用", "Adopt"), this);
    m_adoptAbsBtn->setEnabled(false);
    gb->addWidget(m_absLabel, 1);
    gb->addWidget(m_adoptAbsBtn);
    lay->addWidget(grpAbs);

    // ---- ③ 手动校时 ----
    auto *grpManual = new QGroupBox(lang("③ 手动校时（当前播放位置的画面时间）",
                                         "③ Manual (OSD time at current position)"), this);
    auto *gm = new QHBoxLayout(grpManual);
    m_manualEdit = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    m_manualEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_manualEdit->setCalendarPopup(true);
    m_adoptManualBtn = new QPushButton(lang("采用", "Adopt"), this);
    gm->addWidget(new QLabel(lang("画面显示时间：", "OSD time: "), this));
    gm->addWidget(m_manualEdit, 1);
    gm->addWidget(m_adoptManualBtn);
    lay->addWidget(grpManual);

    // ---- ④ 北京时间校验 ----
    auto *grpTruth = new QGroupBox(lang("④ 北京时间校验（监控时间 ↔ 真实北京时间整体偏移）",
                                        "④ Beijing-time check (overall offset from real time)"), this);
    auto *gt = new QGridLayout(grpTruth);
    m_monitorTimeLabel = new QLabel(this);
    gt->addWidget(new QLabel(lang("当前位置监控时间：", "Monitor time here: "), this), 0, 0);
    gt->addWidget(m_monitorTimeLabel, 0, 1, 1, 2);
    m_beijingEdit = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    m_beijingEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_beijingEdit->setCalendarPopup(true);
    gt->addWidget(new QLabel(lang("实际北京时间：", "Actual Beijing time: "), this), 1, 0);
    gt->addWidget(m_beijingEdit, 1, 1, 1, 2);
    m_truthPreviewLabel = new QLabel(this);
    m_truthPreviewLabel->setStyleSheet(QStringLiteral("font-weight:bold;"));
    gt->addWidget(m_truthPreviewLabel, 2, 1, 1, 2);
    m_truthNoteEdit = new QLineEdit(this);
    m_truthNoteEdit->setPlaceholderText(
        lang("校验说明（如：与指挥中心对时），留档用", "Note (e.g. synced with HQ), for record"));
    gt->addWidget(m_truthNoteEdit, 3, 1, 1, 2);
    m_adoptTruthBtn = new QPushButton(lang("采用偏移", "Adopt offset"), this);
    m_clearTruthBtn = new QPushButton(lang("清除偏移", "Clear"), this);
    gt->addWidget(m_adoptTruthBtn, 4, 1);
    gt->addWidget(m_clearTruthBtn, 4, 2);
    lay->addWidget(grpTruth);

    // ---- 底部 ----
    auto *bbox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bbox);

    connect(m_runBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onRunThreePoint);
    connect(m_adoptFitBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onAdoptFit);
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
    setBusy(true, lang("取样识别中…（首/当前/尾三点，可能需要几十秒）",
                       "Sampling 3 points… (may take tens of seconds)"));
    m_adoptFitBtn->setEnabled(false);
    m_service->runThreePoint(m_videoPath, m_currentPosMs, m_durationMs);
}

void TimeSettingsDialog::onServiceProgress(const QString &stage)
{
    if (m_runBtn && !m_runBtn->isEnabled())
        m_progressLabel->setText(stage);
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
    setBusy(false);
    m_fitLabel->setText(lang("自动识别失败：%1。可改用手动校时（③）",
                             "Auto OCR failed: %1. Use manual (③)").arg(error));
    m_adoptFitBtn->setEnabled(false);
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
}

void TimeSettingsDialog::onClearTruth()
{
    m_working.truthOffsetMs = 0;
    m_working.truthSet = false;
    m_working.truthCheckedAtMs = 0;
    m_working.truthNote.clear();
    m_applied = true;
    refreshWorkingSummary();
}

void TimeSettingsDialog::setBusy(bool busy, const QString &text)
{
    m_runBtn->setEnabled(!busy);
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
}

void TimeSettingsDialog::onAdoptManual()
{
    const qint64 wall = m_manualEdit->dateTime().toMSecsSinceEpoch();
    m_working = CalibrationService::fromSinglePoint(
        m_currentPosMs, wall, TimeCalibration::Source::Manual);
    m_applied = true;
    refreshWorkingSummary();
}
