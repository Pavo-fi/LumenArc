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

// 取证日志门控（2026-09-28）：校时排查用的 calib_debug.log 只在显式开启时写，
// 默认**不往程序目录写文件**（避免用户机器上留调试产物）。
// 需要排查时设环境变量 LUMENARC_CALIB_DEBUG=1。
static bool calibDebugEnabled()
{
    static const bool on =
        qEnvironmentVariableIsSet("LUMENARC_CALIB_DEBUG");
    return on;
}
#include "app/calibration_service.h"
#include "calibphotodialog.h"
#include "domain/truth_time_parse.h"
#include "domain/filename_timestamp.h"   // v1.18.x：文件名时间↔画面时间日期差提示
#include "i18n.h"
#include "theme.h"

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
#include <QToolButton>
#include <QDateTime>
#include <QFileInfo>
#include <QFileDialog>
#include <QMessageBox>
#include <QComboBox>
#include <QSpinBox>
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
    setWindowFlags(windowFlags() | Qt::WindowMinimizeButtonHint
                   | Qt::WindowMaximizeButtonHint);
    buildUi();
    refreshWorkingSummary();

    if (m_service) {
        connect(m_service, &CalibrationService::progress,
                this, &TimeSettingsDialog::onServiceProgress);
        connect(m_service, &CalibrationService::calibPhotoFinished,
                this, &TimeSettingsDialog::onCalibPhotoFinished);
        connect(m_service, &CalibrationService::threePointReady,
                this, &TimeSettingsDialog::onThreePointReady);
        connect(m_service, &CalibrationService::reconstructionReady,
                this, &TimeSettingsDialog::onReconstructionReady);
        connect(m_service, &CalibrationService::quickCheckReady,
                this, &TimeSettingsDialog::onQuickCheckReady);
        connect(m_service, &CalibrationService::tickAlignReady,
                this, &TimeSettingsDialog::onTickAlignReady);
        connect(m_service, &CalibrationService::tickAlignFailed,
                this, &TimeSettingsDialog::onTickAlignFailed);
        connect(m_service, &CalibrationService::failed,
                this, &TimeSettingsDialog::onServiceFailed);
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
    m_workingSummary->setStyleSheet(QStringLiteral("color:%1;font-weight:bold;")
                                        .arg(Theme::Success));
    lay->addWidget(m_workingSummary);

    if (!m_sidecarWarning.isEmpty()) {
        m_sidecarWarnLabel = new QLabel(
            lang("⚠ 此拼接文件段间存在时间缺口/重叠，首段之后的墙钟可能不准（详见报告）",
                 "⚠ Concatenated file has time gaps/overlaps; wall clock after the "
                 "first segment may drift (see report)"), this);
        m_sidecarWarnLabel->setWordWrap(true);
        m_sidecarWarnLabel->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::AccentTk::Text));
        lay->addWidget(m_sidecarWarnLabel);
    }

    // ---- 顶部用法横幅（参考拼接窗口格式横幅）----
    lay->addWidget(buildUsageBanner());

    // ---- 第 1 步：自动校时 ----
    auto *grpGo = new QGroupBox(lang("第 1 步 · 自动校时", "Step 1 · Auto calibrate"), this);
    auto *gg = new QVBoxLayout(grpGo);
    auto *goRow = new QHBoxLayout();
    m_goBtn = new QPushButton(lang("自动校时", "GO"), this);
    m_goBtn->setMinimumHeight(44);
    m_goBtn->setStyleSheet(QStringLiteral(
        "QPushButton { font-size:13px; font-weight:600; }"));
    m_cancelBtn = new QPushButton(lang("取消", "Cancel"), this);
    m_cancelBtn->setVisible(false);
    m_roiBtn = new QPushButton(lang("框选时间戳区域", "Select timestamp area"), this);
    m_roiBtn->setToolTip(lang(
        "在画面上框住时间戳（识别更准）。同一摄像头只需框一次，之后自动复用。",
        "Box the timestamp on screen for better OCR. Set once per camera."));
    goRow->addWidget(m_goBtn, 3);
    goRow->addWidget(m_cancelBtn, 1);
    goRow->addWidget(m_roiBtn, 1);
    gg->addLayout(goRow);

    // ---- 第 1 步的手动出路（v1.18.3 简化版）----
    // 背景：自动 OCR 对某些 OSD 版式天然无效（如“今日水印相机”：时分 19:32 +
    // 灰框秒 37，中间无冒号，而解析器要求 H:M:S）。
    // 用户反馈：“输入后没有确认结果的方法 / 页面字太多太复杂”，故：
    //   默认 = 一条（填画面时间 + 采用，位置自动取当前播放头，零前置操作），
    //   应用后就地给出确认；两点（修正时钟快慢）收进可展开高级区，默认折叠。
    auto *manualBox = new QFrame(this);
    // 用主题令牌（勿硬编码浅色：本程序为深色主题，浅底会变成一块白砖）
    manualBox->setStyleSheet(QStringLiteral(
        "QFrame { background:%1; border:1px solid %2; border-radius:6px; }"
        "QLabel { border:none; background:transparent; }")
        .arg(Theme::BgCard, Theme::Border));
    auto *mb = new QVBoxLayout(manualBox);
    mb->setContentsMargins(10, 8, 10, 8);
    mb->setSpacing(6);

    // 默认路径：单点。位置自动取当前播放头——不需要任何“先取位置”的前置动作
    m_manualSimpleBox = new QWidget(manualBox);
    m_manualSimpleBox->setObjectName(QStringLiteral("manualSimpleBox"));
    auto *msRow = new QHBoxLayout(m_manualSimpleBox);
    msRow->setContentsMargins(0, 0, 0, 0);
    msRow->setSpacing(6);
    msRow->addWidget(new QLabel(lang("画面时间读不出？填画面上的时间：",
                                     "OCR failed? Type the on-screen time:"),
                                m_manualSimpleBox));
    m_manualSimpleEdit = new QDateTimeEdit(QDateTime::currentDateTime(),
                                           m_manualSimpleBox);
    m_manualSimpleEdit->setObjectName(QStringLiteral("manualSimpleEdit"));
    m_manualSimpleEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_manualSimpleEdit->setCalendarPopup(true);
    m_manualSimpleEdit->setToolTip(lang(
        "照拄画面上此刻显示的完整日期时间（对应当前播放位置）",
        "Type the full date/time shown on screen at the current playhead"));
    msRow->addWidget(m_manualSimpleEdit, 2);
    auto *adoptBtn = new QPushButton(lang("采用", "Apply"), m_manualSimpleBox);
    adoptBtn->setObjectName(QStringLiteral("adoptManualBaseBtn"));
    msRow->addWidget(adoptBtn);
    mb->addWidget(m_manualSimpleBox);
    connect(adoptBtn, &QPushButton::clicked,
            this, &TimeSettingsDialog::onAdoptManualBase);

    // 高级路径：两点（默认折叠，需修正“时钟快慢”时才展开）
    m_manualAdvBtn = new QToolButton(manualBox);
    m_manualAdvBtn->setObjectName(QStringLiteral("manualAdvBtn"));
    m_manualAdvBtn->setCheckable(true);
    m_manualAdvBtn->setAutoRaise(true);
    m_manualAdvBtn->setToolButtonStyle(Qt::ToolButtonTextOnly);
    m_manualAdvBtn->setText(lang("▸ 需要修正时钟快慢？（两点，需 ≥10 分钟录像）",
                                 "▸ Correct clock drift? (two points, >=10 min)"));
    m_manualAdvBtn->setToolTip(lang(
        "两点分处录像首尾：既能定基准，又能算出画面时钟每天快/慢多少。\n"
        "注意：OSD 只有秒级精度，跨度不足 10 分钟时算不出可信的漂移。",
        "One point near each end: sets the base and measures drift.\n"
        "Note: with 1s OSD resolution, spans under ~10 min cannot resolve drift."));
    mb->addWidget(m_manualAdvBtn);

    m_manualAdvBox = new QWidget(manualBox);
    m_manualAdvBox->setObjectName(QStringLiteral("manualAdvBox"));
    auto *mab = new QVBoxLayout(m_manualAdvBox);
    mab->setContentsMargins(0, 0, 0, 0);
    mab->setSpacing(4);

    const auto mkAdvRow = [&](const QString &label, const QString &posObj,
                              const QString &editObj, const QString &takeObj,
                              QLabel *&posOut, QDateTimeEdit *&editOut,
                              QPushButton *&takeOut) {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(label, m_manualAdvBox));
        posOut = new QLabel(QStringLiteral("—"), m_manualAdvBox);
        posOut->setObjectName(posObj);
        posOut->setMinimumWidth(80);
        posOut->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
        row->addWidget(posOut);
        editOut = new QDateTimeEdit(QDateTime::currentDateTime(), m_manualAdvBox);
        editOut->setObjectName(editObj);
        editOut->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        editOut->setCalendarPopup(true);
        row->addWidget(editOut, 2);
        takeOut = new QPushButton(lang("取当前", "Grab"), m_manualAdvBox);
        takeOut->setObjectName(takeObj);
        row->addWidget(takeOut);
        mab->addLayout(row);
    };
    QPushButton *p1TakeBtn = nullptr;
    QPushButton *p2TakeBtn = nullptr;
    mkAdvRow(lang("点1", "Pt1"), QStringLiteral("manualP1Pos"),
             QStringLiteral("manualP1Edit"), QStringLiteral("manualP1TakeBtn"),
             m_manualP1Pos, m_manualP1Edit, p1TakeBtn);
    mkAdvRow(lang("点2", "Pt2"), QStringLiteral("manualP2Pos"),
             QStringLiteral("manualP2Edit"), QStringLiteral("manualP2TakeBtn"),
             m_manualP2Pos, m_manualP2Edit, p2TakeBtn);
    auto *advFoot = new QHBoxLayout();
    advFoot->addStretch(1);
    auto *adoptTwoBtn = new QPushButton(lang("采用两点", "Apply two points"),
                                       m_manualAdvBox);
    adoptTwoBtn->setObjectName(QStringLiteral("adoptManualTwoPointBtn"));
    advFoot->addWidget(adoptTwoBtn);
    mab->addLayout(advFoot);
    m_manualAdvBox->hide();
    mb->addWidget(m_manualAdvBox);

    connect(p1TakeBtn, &QPushButton::clicked,
            this, &TimeSettingsDialog::onTakeManualP1);
    connect(p2TakeBtn, &QPushButton::clicked,
            this, &TimeSettingsDialog::onTakeManualP2);
    connect(adoptTwoBtn, &QPushButton::clicked,
            this, &TimeSettingsDialog::onAdoptManualTwoPoint);
    connect(m_manualAdvBtn, &QToolButton::toggled, this, [this](bool on) {
        m_manualAdvBox->setVisible(on);
        m_manualSimpleBox->setVisible(!on);
        m_manualAdvBtn->setText(on
            ? lang("▾ 修正时钟快慢（两点）", "▾ Correct clock drift (two points)")
            : lang("▸ 需要修正时钟快慢？（两点，需 ≥10 分钟录像）",
                   "▸ Correct clock drift? (two points, >=10 min)"));
    });

    // 位置提示 + 应用后的确认（用户反馈：输入后没有确认结果的方法）
    m_manualLivePos = new QLabel(lang("位置取当前播放头：00:00:00",
                                      "Uses playhead: 00:00:00"), manualBox);
    m_manualLivePos->setObjectName(QStringLiteral("manualLivePos"));
    m_manualLivePos->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    mb->addWidget(m_manualLivePos);

    m_manualResultLabel = new QLabel(manualBox);
    m_manualResultLabel->setObjectName(QStringLiteral("manualResultLabel"));
    m_manualResultLabel->setWordWrap(true);
    m_manualResultLabel->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::Success));
    m_manualResultLabel->hide();
    mb->addWidget(m_manualResultLabel);

    gg->addWidget(manualBox);
    m_progressLabel = new QLabel(this);
    m_progressLabel->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
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
    // v1.18.x：整片一致的变速件（平台导出加速/抽帧）默认走「按此倍率校时」快路；
    // 需要分段精修时才手动起重建（长文件重建可达数十分钟——OCR 逐点 ~45s）
    m_reconBtn = new QPushButton(lang("时间重建", "Rebuild"), this);
    m_reconBtn->setEnabled(false);
    m_reconBtn->setToolTip(lang(
        "按画面时间逐段重建（分段变速文件用；长文件可能需数十分钟）",
        "Rebuild time from frames (variable-rate files; may take tens of minutes)"));
    // P-98 秒级跳变对齐（2026-09-27 拍板）：像素盯 OSD 秒位跳变 + 稀疏 OCR 锚点，
    // 45 分钟片实测 ~3 分钟、抽检 11/12 完全一致（vs 单直线 ±10~19 秒 /
    // 时间重建 40~60 分钟）。变速件会自动触发，也可手动点。
    m_tickBtn = new QPushButton(lang("秒级精细对齐", "Second-level align"), this);
    m_tickBtn->setObjectName(QStringLiteral("tickAlignBtn"));
    m_tickBtn->setEnabled(false);
    m_tickBtn->setToolTip(lang(
        "像素盯画面时间秒位跳变 + 稀疏取样标定：秒级精度（实测 ±1 秒），"
        "约 2~4 分钟（时长越长越久）；失败会自动提示改用「时间重建」",
        "Second-level alignment via OSD second ticks (measured ±1 s), ~2-4 min; "
        "falls back to Rebuild if unusable"));
    m_useBtn = new QPushButton(lang("✓ 使用此结果", "✓ Use this result"), this);
    m_useBtn->setObjectName(QStringLiteral("fitUseBtn"));   // v1.18.x：ui_chain 回归锁定位用
    m_useBtn->setEnabled(false);
    m_useBtn->setMinimumWidth(140);
    rr->addWidget(m_detailsBtn);
    rr->addWidget(m_tickBtn);
    rr->addWidget(m_reconBtn);
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
    m_fitWarningLabel->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::AccentTk::Text));
    gd->addWidget(m_fitWarningLabel);
    m_noDriftCheck = new QCheckBox(lang("不修正时钟快慢（仅对基准）",
                                        "Ignore clock drift (offset only)"), this);
    m_noDriftCheck->setObjectName(QStringLiteral("noDriftCheck"));   // v1.18.x：ui_chain 回归锁定位用
    gd->addWidget(m_noDriftCheck);
    m_detailsBox->hide();
    gg->addWidget(m_detailsBox);
    lay->addWidget(grpGo);

    // ---- 第 2 步：对真实时间（北京时间，可选；v1.12.5 重做）----
    // 拍板（2026-08-21）：取证惯例 = 对监控屏幕拍照时同框拍入标准时间参照物
    // （手机授时网页等）。三种方式：
    //   ① 校时图片框选 OCR（一张图两个框：监控主机时间 / 北京时间，自动算偏差）
    //   ② 手动输入两个时间（自动算偏差）
    //   ③ 直接输入偏移量「监控主机时间比北京时间 快/慢 X日X时X分X秒」
    auto *grpTruth = new QGroupBox(lang("第 2 步 · 对真实时间（可选）",
                                        "Step 2 · Align to real time (optional)"), this);
    auto *gt = new QVBoxLayout(grpTruth);
    auto *truthExplain = new QLabel(lang(
        "第 1 步对的是「视频进度 ↔ 画面时间」，这一步对「画面时间 ↔ 真实北京时间」\n"
        "（如录像机从未对时）。推荐拍照校时：屏幕与手机标准时间同框拍入，框选两处自动算偏差。",
        "Step 1 maps playback↔on-screen time; this step maps on-screen↔real time "
        "(e.g. the recorder was never synced). Recommended: photograph the screen "
        "together with a standard clock and box both."), this);
    truthExplain->setWordWrap(true);
    truthExplain->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    gt->addWidget(truthExplain);

    // 方式一：校时图片框选 OCR（v1.12.7 用户反馈：按钮不够显眼 →
    // 与 GO 同级强调：大按钮 + 主题强调色）
    auto *photoRow = new QHBoxLayout();
    m_truthPhotoBtn = new QPushButton(
        lang("从校时图片识别（推荐：框选监控主机时间 + 北京时间）…",
             "From calibration photo (recommended: box both clocks)…"), this);
    m_truthPhotoBtn->setMinimumHeight(40);
    m_truthPhotoBtn->setStyleSheet(QStringLiteral(
        "QPushButton { font-size:12px; font-weight:bold; "
        "background:%1; color:%3; border-radius:6px; padding:0 14px; }"
        "QPushButton:hover { background:%2; }"
        "QPushButton:disabled { background:%4; color:%5; }")
        .arg(Theme::Accent, Theme::AccentHover, Theme::AccentOnDark,
             Theme::BgPressed, Theme::TextMuted));
    m_truthPhotoBtn->setEnabled(m_service != nullptr);
    if (!m_service)
        m_truthPhotoBtn->setToolTip(lang("OCR 引擎不可用", "OCR engine unavailable"));
    photoRow->addWidget(m_truthPhotoBtn, 1);
    gt->addLayout(photoRow);

    // 方式二：手动输入两个时间（自动算偏差）
    auto *grid = new QGridLayout();
    auto *manualTitle = new QLabel(lang("方式二 · 手动输入两个时间：",
                                        "Manual — enter both times:"), this);
    manualTitle->setStyleSheet(QStringLiteral("font-weight:bold;"));
    grid->addWidget(manualTitle, 0, 0, 1, 2);
    grid->addWidget(new QLabel(lang("画面上的时间（当前播放位置）：",
                                    "On-screen time (playhead): "), this), 1, 0);
    m_monitorTimeLabel = new QLabel(this);
    grid->addWidget(m_monitorTimeLabel, 1, 1);
    grid->addWidget(new QLabel(lang("监控主机时间：", "Recorder time: "), this), 2, 0);
    auto *monRow = new QHBoxLayout();
    m_monitorEdit = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    m_monitorEdit->setObjectName(QStringLiteral("truthMonitorEdit"));
    m_monitorEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_monitorEdit->setCalendarPopup(true);
    monRow->addWidget(m_monitorEdit);
    auto *takeCurBtn = new QPushButton(lang("取当前画面时间", "Use playhead time"), this);
    monRow->addWidget(takeCurBtn);
    monRow->addStretch(1);
    grid->addLayout(monRow, 2, 1);
    grid->addWidget(new QLabel(lang("真实北京时间：", "Actual Beijing time: "), this), 3, 0);
    m_beijingEdit = new QDateTimeEdit(QDateTime::currentDateTime(), this);
    m_beijingEdit->setObjectName(QStringLiteral("truthBeijingEdit"));
    m_beijingEdit->setDisplayFormat(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    m_beijingEdit->setCalendarPopup(true);
    grid->addWidget(m_beijingEdit, 3, 1);
    m_truthPreviewLabel = new QLabel(this);
    m_truthPreviewLabel->setObjectName(QStringLiteral("truthPreviewLabel"));
    m_truthPreviewLabel->setStyleSheet(QStringLiteral("font-weight:bold;"));
    grid->addWidget(m_truthPreviewLabel, 4, 1);
    grid->addWidget(m_adoptTruthBtn = new QPushButton(
                        lang("使用此偏移", "Use this offset"), this), 5, 0);
    m_adoptTruthBtn->setObjectName(QStringLiteral("adoptTruthBtn"));
    grid->addWidget(m_clearTruthBtn = new QPushButton(
                        lang("清除偏移", "Clear"), this), 5, 1);
    m_clearTruthBtn->setObjectName(QStringLiteral("clearTruthBtn"));
    gt->addLayout(grid);

    // 方式三：直接输入偏移量「比北京时间 快/慢 X日X时X分X秒」
    auto *offTitle = new QLabel(lang("方式三 · 直接输入偏移量：",
                                     "Direct offset:"), this);
    offTitle->setStyleSheet(QStringLiteral("font-weight:bold;"));
    gt->addWidget(offTitle);
    auto *offRow = new QHBoxLayout();
    offRow->addWidget(new QLabel(lang("监控主机时间比北京时间",
                                      "Recorder clock is"), this));
    m_offsetDirCombo = new QComboBox(this);
    m_offsetDirCombo->setObjectName(QStringLiteral("truthOffsetDir"));
    m_offsetDirCombo->addItem(lang("慢", "slower than Beijing by"));
    m_offsetDirCombo->addItem(lang("快", "faster than Beijing by"));
    offRow->addWidget(m_offsetDirCombo);
    const auto mkSpin = [this](int maxV, const QString &suffix) {
        auto *sp = new QSpinBox(this);
        sp->setRange(0, maxV);
        sp->setSuffix(suffix);
        return sp;
    };
    m_offsetDays  = mkSpin(3650, lang(" 日", " d"));
    m_offsetHours = mkSpin(23,   lang(" 时", " h"));
    m_offsetMins  = mkSpin(59,   lang(" 分", " m"));
    m_offsetSecs  = mkSpin(59,   lang(" 秒", " s"));
    m_offsetDays->setObjectName(QStringLiteral("truthOffsetDays"));
    m_offsetHours->setObjectName(QStringLiteral("truthOffsetHours"));
    m_offsetMins->setObjectName(QStringLiteral("truthOffsetMins"));
    m_offsetSecs->setObjectName(QStringLiteral("truthOffsetSecs"));
    offRow->addWidget(m_offsetDays);
    offRow->addWidget(m_offsetHours);
    offRow->addWidget(m_offsetMins);
    offRow->addWidget(m_offsetSecs);
    auto *adoptOffBtn = new QPushButton(lang("采用此偏移量", "Use this offset"), this);
    adoptOffBtn->setObjectName(QStringLiteral("adoptManualOffsetBtn"));
    offRow->addWidget(adoptOffBtn);
    offRow->addStretch(1);
    gt->addLayout(offRow);

    m_truthNoteEdit = new QLineEdit(this);
    m_truthNoteEdit->setPlaceholderText(
        lang("说明（如：与指挥中心对时），留档用", "Note (e.g. synced with HQ), for record"));
    gt->addWidget(m_truthNoteEdit);
    lay->addWidget(grpTruth);

    connect(takeCurBtn, &QPushButton::clicked, this, [this]() {
        if (m_working.isValid() && m_working.dateKnown)
            m_monitorEdit->setDateTime(QDateTime::fromMSecsSinceEpoch(
                m_working.wallMsOf(m_currentPosMs)));
    });
    connect(m_monitorEdit, &QDateTimeEdit::dateTimeChanged,
            this, [this](const QDateTime &) { onTruthInputChanged(); });
    connect(m_truthPhotoBtn, &QPushButton::clicked,
            this, &TimeSettingsDialog::onTruthPhotoPick);
    connect(adoptOffBtn, &QPushButton::clicked,
            this, &TimeSettingsDialog::onAdoptTruthManualOffset);

    // ---- 底部 ----
    auto *hint = new QLabel(lang(
        "识别在后台进行，可最小化窗口继续操作。",
        "Recognition runs in background; you can minimize and keep working."), this);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
    lay->addWidget(hint);
    auto *bbox = new QDialogButtonBox(QDialogButtonBox::Close, this);
    connect(bbox, &QDialogButtonBox::rejected, this, &QDialog::reject);
    lay->addWidget(bbox);

    connect(m_goBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onRunGo);
    connect(m_roiBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onRoiButton);
    connect(m_cancelBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onCancelGo);
    connect(m_detailsBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onToggleDetails);
    connect(m_useBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onUseResult);
    connect(m_reconBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onRunReconstruction);
    connect(m_tickBtn, &QPushButton::clicked, this, &TimeSettingsDialog::onRunTickAlign);
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
    // 主题令牌（原为硬编码浅色，与深色主题不一致——用户反馈：面板颜色不一致）
    banner->setStyleSheet(QStringLiteral(
        "QFrame { background:%1; border:1px solid %2; border-radius:8px; }"
        "QLabel { border:none; background:transparent; }")
        .arg(Theme::BgCard, Theme::Border));
    auto *lay = new QVBoxLayout(banner);
    lay->setContentsMargins(12, 8, 12, 8);
    lay->setSpacing(4);
    auto *title = new QLabel(lang("用法", "Usage"), banner);
    title->setStyleSheet(QStringLiteral("font-weight:bold;"));
    lay->addWidget(title);
    auto *desc = new QLabel(
        lang("点 GO 自动识别画面时间并应用；识别不出就用下面的手动录入。\n"
             "画面时间与真实时间有整体偏差时，用第 2 步对真实时间。",
             "Press GO to auto-read and apply the on-screen time; if it fails, "
             "use manual entry below.\n"
             "Use Step 2 if the on-screen clock differs from real time."),
        banner);
    desc->setWordWrap(true);
    desc->setStyleSheet(QStringLiteral("color:%1;").arg(Theme::TextSecond));
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
        // v1.18.x：非实时导出/变速件（画面时间 ≈ rate× 播放进度）——报告与徽标需一眼可见
        // （piecewise 路径已有“分段重建 N 段（变速）”，此处只管全局倍率那类）
        if (m_working.speedVariant && m_working.rateApplied && !m_working.piecewiseMode())
            text += lang("；非实时导出件（画面时间 ≈ %1× 播放进度）",
                         "; non-realtime export (on-screen ≈ %1x playback)")
                .arg(m_working.rate, 0, 'f', 3);
        if (m_working.piecewiseMode())
            text += lang("；分段重建 %1 段（变速）", "; piecewise %1 segs (variable-rate)")
                .arg(m_working.piecewise.size());
        if (m_working.truthSet)
            text += lang("；北京时间偏移 %1", "; Beijing offset %1")
                .arg(fmtOffset(m_working.truthOffsetMs));
        m_workingSummary->setText(text);
    }
    // 北京时间对齐区显示当前播放位置换算出的画面时间
    if (m_working.isValid() && m_working.dateKnown) {
        const qint64 curWall = m_working.wallMsOf(m_currentPosMs);
        m_monitorTimeLabel->setText(fmtWall(curWall));
        // 方式二手输默认取当前画面时间（可编辑；取后用户自行微调）
        if (!m_monitorEdit->hasFocus())
            m_monitorEdit->setDateTime(QDateTime::fromMSecsSinceEpoch(curWall));
        // v1.18.2：手动取样位置由用户点「取当前」显式记录，
        // 不随播放头漂移（输入框保持用户填的画面时间）
    } else {
        m_monitorTimeLabel->setText(lang("（需先完成自动校时）",
                                         "(run auto calibrate first)"));
    }
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
    // 框选就绪待确认：ROI 有效 → 开始校时（「确认并开始校时」按钮语义）
    if (m_goStage == GoStage::Staged) {
        if (m_roi.isValid())
            startGo();
        else {
            m_goStage = GoStage::Idle;
            onRunGo();   // 无效 ROI（不应发生）：回退正常流程
        }
        return;
    }
    // 无已存时间戳区域 → 先请用户在画面上框选（识别率更高）
    if (!m_roi.isValid()) {
        m_goStage = GoStage::Staged;   // 框选就绪后由 stageTimestampRoi 接回
        m_waitingRoi = true;
        m_resultLabel->setText(lang(
            "本窗口已最小化：请在主窗口画面上框住时间戳区域。\n"
            "松开鼠标后本窗口自动恢复，点「确认并开始校时」。\n"
            "不确定区域可点画面右下角「跳过（自动扫描）」。",
            "Window minimized: draw a box around the on-screen timestamp. "
            "On release this window returns with a confirm button; "
            "or use \"skip\" on screen for auto-scan."));
        showMinimized();   // 框选不挡画面（现场反馈 UX）
        emit requestTimestampRoi();
        return;
    }
    startGo();
}

void TimeSettingsDialog::onCancelGo()
{
    if (m_service)
        m_service->cancel();
    if (m_waitingRoi) {
        m_waitingRoi = false;
        emit cancelTimestampRoiRequest();
    }
    m_goStage = GoStage::Idle;
    // v1.18.x：取消 = 本轮无结果 → 清 pending（否则第 2 步会把上一轮的 fit 当结果并入）
    m_fitPending = false;
    if (isMinimized())
        showNormal();   // 框选取消时恢复窗口
    setGoBusy(false, QString());
    m_resultLabel->setText(lang("已取消。可重新点击「自动校时」。",
                                "Cancelled. Click \"Auto calibrate\" to retry."));
}

void TimeSettingsDialog::startGo()
{
    m_goStage = GoStage::Quick;
    m_roiRetried = false;   // v1.7.1：新一轮校时重置自动重试标记
    m_autoApplied = false;
    m_useBtn->setEnabled(false);
    m_useBtn->setText(lang("✓ 使用此结果", "✓ Use this result"));
    m_detailsBtn->setEnabled(false);
    m_detailsBox->hide();
    m_detailsVisible = false;
    m_detailsBtn->setText(lang("查看细节 ▸", "Details ▸"));
    m_resultLabel->setText(lang(
        "快速检查中…（读取首尾画面时间；大文件尾部定位较慢，\n"
        "可能需 1~2 分钟，请稍候）",
        "Quick-checking… (reading first/last on-screen time; tail seek on "
        "large files can take 1-2 min)"));
    setGoBusy(true, lang("快速检查中…", "Quick-checking…"));
    m_service->runQuickCheck(m_videoPath, m_durationMs, m_roi);
}

void TimeSettingsDialog::onRoiButton()
{
    m_waitingRoi = true;
    // 「框选时间戳区域」= 先框后测入口：框选就绪后窗口恢复并给出
    // 「确认并开始校时」（现场反馈 UX：确认键要显眼、流程要自动接回）
    if (m_goStage == GoStage::Idle || m_goStage == GoStage::Failed
        || m_goStage == GoStage::Done)
        m_goStage = GoStage::Staged;
    m_resultLabel->setText(lang(
        "本窗口已最小化：请在主窗口画面上框住时间戳区域。\n"
        "松开鼠标后本窗口自动恢复，点「确认并开始校时」。",
        "Window minimized: draw a box around the timestamp. "
        "On release this window returns with a confirm button."));
    showMinimized();
    emit requestTimestampRoi();
}

void TimeSettingsDialog::setTimestampRoi(const QRectF &rect)
{
    m_roi = rect;
    if (rect.isValid())
        m_roiSticky = rect;      // 粘性：供秒级对齐定位秒位
    m_waitingRoi = false;
    if (m_roi.isValid()) {
        m_roiBtn->setText(lang("重新框选时间戳", "Re-select timestamp"));
        if (m_goStage == GoStage::Idle || m_goStage == GoStage::Failed
            || m_goStage == GoStage::Done)
            m_resultLabel->setText(lang(
                "时间戳区域已选定（画面坐标 %1~%2, %3~%4）。"
                "点击 GO 开始自动校时；如需调整可点「重新框选时间戳」。",
                "Timestamp area set (frame coords %1~%2, %3~%4). "
                "Click GO to calibrate; re-select to adjust.")
                    .arg(m_roi.left(), 0, 'f', 2).arg(m_roi.right(), 0, 'f', 2)
                    .arg(m_roi.top(), 0, 'f', 2).arg(m_roi.bottom(), 0, 'f', 2));
    }
}

void TimeSettingsDialog::stageTimestampRoi(const QRectF &rect)
{
    m_roi = rect;
    if (rect.isValid())
        m_roiSticky = rect;
    m_waitingRoi = false;
    // 框选（或跳过）后窗口自动恢复（现场反馈 UX）
    if (isMinimized())
        showNormal();
    raise();
    activateWindow();
    if (!m_roi.isValid()) {
        // 用户跳过框选：GO 流程直接自动扫描开始（无可确认内容）
        if (m_goStage == GoStage::Staged)
            startGo();
        return;
    }
    m_roiBtn->setText(lang("重新框选时间戳", "Re-select timestamp"));
    m_resultLabel->setText(lang(
        "已框选时间戳区域（画面坐标 %1~%2, %3~%4）。\n"
        "点击下方「确认并开始校时」。",
        "Timestamp area boxed (frame coords %1~%2, %3~%4). "
        "Click \"Confirm & start\" below.")
            .arg(m_roi.left(), 0, 'f', 2).arg(m_roi.right(), 0, 'f', 2)
            .arg(m_roi.top(), 0, 'f', 2).arg(m_roi.bottom(), 0, 'f', 2));
    if (m_goStage == GoStage::Staged) {
        // 醒目的主按钮：确认并开始校时（替代叠加层角落的小确认键）
        m_goBtn->setText(lang("✓ 确认并开始校时", "✓ Confirm & start"));
        m_progressLabel->clear();
    }
}

void TimeSettingsDialog::onQuickCheckReady(const QString &videoPath,
                                           double overallRate,
                                           bool suspicious,
                                           bool ocrSuspect)
{
    if (videoPath != m_videoPath)
        return;
    if (ocrSuspect) {
        // 第三点确认失败（v1.2.2）：首尾/中点任一点疑似被 OCR 错读。
        // v1.18.x：服务层已先剔除 1 个离群点后重算，仍不成直线才走到这里——
        // 不再直接判死（旧版只提示“请重新框选”，配合框选 ROI 反而更糟：
        // 实测带 ROI 会把年份 2026 读成 2022），改为继续三点识别：
        // 三点路径会自动剔除错读点并把该行标 ⚠，用户在测点表里一眼能看见。
        m_goStage = GoStage::Ocr;
        if (m_reconBtn)
            m_reconBtn->setEnabled(false);
        m_resultLabel->setText(lang(
            "⚠ 预检有取样点疑似错读（已自动剔除最离群的一个）。\n"
            "已继续三点识别——完成后请看结果与测点表的 ⚠ 行核对。",
            "⚠ Quick-check suspects a misread sample (worst outlier dropped). "
            "Continuing 3-point OCR — check the result and the ⚠ row afterwards."));
        setGoBusy(true, lang("识别中…", "Recognizing…"));
        m_service->runThreePoint(m_videoPath, m_currentPosMs, m_durationMs,
                                 m_roi);
        return;
    }
    if (suspicious) {
        // v1.18.x（2026-09-24 顺德公安件实测）：整片一致的变速（三点共线）不再盲目启重建——
        // 重建逐点 OCR（~45s/点，长文件数十分钟）而结果与“全局倍率”等价（片内抖动 <10%
        // 不足以切段）。改为先走三点识别 → 用户点「按此倍率校时」立即采用；
        // 确实需要分段精修的，点「时间重建」。
        if (!ocrSuspect) {
            m_goStage = GoStage::Ocr;
            if (m_reconBtn)
                m_reconBtn->setEnabled(true);
            m_resultLabel->setText(lang(
                "画面时间约为播放进度的 %1 倍（整片一致，疑似非实时导出/抽帧）。\n"
                "先做三点识别——完成后可点「按此倍率校时」立即采用；\n"
                "需要逐段精修（长文件较慢）再点「时间重建」。",
                "On-screen time runs ~%1x of playback (uniform → likely non-realtime export). "
                "Running 3-point OCR — then click \"Calibrate at this rate\"; "
                "use \"Rebuild\" for per-segment refinement (slow on long files).")
                    .arg(overallRate, 0, 'f', 2));
            setGoBusy(true, lang("识别中…", "Recognizing…"));
            m_service->runThreePoint(m_videoPath, m_currentPosMs, m_durationMs,
                                     m_roi);
            return;
        }
        // 点不成直线（可能是分段变速）→ 时间重建
        m_goStage = GoStage::Recon;
        if (m_reconBtn)
            m_reconBtn->setEnabled(false);
        m_resultLabel->setText(lang(
            "检测到疑似变速文件（画面时间约为播放进度的 %1 倍），"
            "正在按画面时间重建…（需数分钟，可最小化窗口）",
            "Variable-rate file detected (on-screen time ~%1x playback). "
            "Reconstructing… (minutes; window can be minimized).")
                .arg(overallRate, 0, 'f', 2));
        m_service->runReconstruction(m_videoPath, m_durationMs, m_roi);
    } else {
        // 正常文件：自动进入三点识别
        m_goStage = GoStage::Ocr;
        m_resultLabel->setText(lang(
            "文件时间正常。正在三点识别…（几十秒）",
            "Normal recording. Running 3-point OCR… (tens of seconds)"));
        setGoBusy(true, lang("识别中…", "Recognizing…"));
        m_service->runThreePoint(m_videoPath, m_currentPosMs, m_durationMs,
                                 m_roi);
    }
}

void TimeSettingsDialog::onThreePointReady(const QString &videoPath,
                                           const TimeCalibration &proposed)
{
    // TEMP-DEBUG（2026-09-26 排查用，定位后删）
    auto dbg = [](const QString &s) {
        QFile f(QCoreApplication::applicationDirPath()
                + QStringLiteral("/calib_debug.log"));
        if (calibDebugEnabled()
            && f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            f.write((QStringLiteral("[%1] ")
                         .arg(QDateTime::currentDateTime().toString(
                             QStringLiteral("HH:mm:ss.zzz")))
                     + s + QStringLiteral("\n"))
                        .toUtf8());
    };
    {
        QString s = QStringLiteral("onThreePointReady video=%1 match=%2 samples=%3")
                        .arg(videoPath)
                        .arg(videoPath == m_videoPath ? 1 : 0)
                        .arg(proposed.samples.size());
        for (const auto &x : proposed.samples)
            s += QStringLiteral("\n    s stream=%1 wall=%2 used=%3 susp=%4 text=%5")
                     .arg(x.streamMs).arg(x.wallMs).arg(x.used)
                     .arg(x.ocrSuspicious).arg(x.rawText);
        const auto f = TimeCalibration::fitDroppingWorstOutlier(proposed.samples);
        s += QStringLiteral("\n    fit ok=%1 n=%2 rate=%3 warn=%4 residual=%5")
                 .arg(f.ok).arg(f.pointsUsed).arg(f.rate, 0, 'f', 6)
                 .arg(int(f.warning)).arg(f.maxResidualMs);
        s += QStringLiteral("\n    noDriftChecked=%1")
                 .arg(m_noDriftCheck && m_noDriftCheck->isChecked() ? 1 : 0);
        dbg(s);
    }
    if (videoPath != m_videoPath)
        return;
    m_goStage = GoStage::Done;
    setGoBusy(false, QString());
    m_fitResult = proposed;
    m_fitPending = true;   // v1.18.x：标记「已算出、待应用」——第 2 步落库前必须并入
    if (m_tickBtn)
        m_tickBtn->setEnabled(true);   // P2-4b：有结果即可手动起秒级对齐
    fillSampleTable(proposed);
    refitSummaryRefresh();
    // 无异常 → 自动应用（结果区与状态栏即时反馈）
    maybeAutoApply();
    emit goTaskFinished(lang("校时完成", "Calibration finished"),
                        lang("%1：三点识别完成，结果在校时窗口中。",
                             "%1: 3-point recognition done, see calibration "
                             "window.").arg(QFileInfo(videoPath).fileName()));
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
        // v1.18.x：重建已取代三点候选 → 清 pending，免得第 2 步又把上一轮
        // 三点 fit 隐式并入（与「正常录像」结论自相矛盾，reviewer 2026-09-26）
        m_fitPending = false;
        m_resultLabel->setText(lang(
            "结果：正常录像（无变速边界）。可用「自动校时」重新识别。",
            "Result: normal recording (no rate boundaries). "
            "Re-run \"Auto calibrate\" if needed."));
        m_useBtn->setEnabled(true);
        m_detailsBtn->setEnabled(true);
        emit goTaskFinished(lang("校时完成", "Calibration finished"),
                            lang("%1：重建完成，判定为正常录像。",
                                 "%1: reconstruction done, normal recording.")
                                .arg(QFileInfo(videoPath).fileName()));
        return;
    }

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
    m_useBtn->setEnabled(true);
    m_detailsBtn->setEnabled(true);
    // 无异常 → 自动应用（结果区与状态栏即时反馈）
    maybeAutoApply();
    emit goTaskFinished(lang("校时完成", "Calibration finished"),
                        lang("%1：时间重建完成，结果在校时窗口中。",
                             "%1: time reconstruction done, see calibration "
                             "window.").arg(QFileInfo(videoPath).fileName()));
}

void TimeSettingsDialog::onUseResult()
{
    if (m_fitResult.isValid() && m_fitResult.source == TimeCalibration::Source::Ocr
        && !m_fitResult.piecewiseMode()) {
        // v1.18.x：门控下沉到 domain::applyFitDecision（与第 2 步对真实时间共用
        // 同一实现，防两处判定漂移）
        TimeCalibration::applyFitDecision(m_fitResult,
                                          m_noDriftCheck->isChecked());
        applyWorking(m_fitResult);
    } else if (m_reconResult.piecewiseMode()) {
        applyWorking(m_reconResult);
    }
}

bool TimeSettingsDialog::absorbPendingFit()
{
    // v1.18.x（2026-09-26 顺德公安件实测）：用户点 GO → 三点算出 1.139× 自洽大倍率，
    // 但「非实时导出件」不自动应用（等用户点「按此倍率校时」）；用户接着去做
    // 第 2 步「对真实时间」——旧代码直接 emit calibrationApplied(m_working)，落库的是
    // **继承来的旧校准**（本例 2 点 rate=1.0）→ 时间轴永远偏 6 分钟。
    // 修：第 2 步落库前，把已算出但未应用的第 1 步结果按同款门控并入工作面。
    // 判据在 domain（TimeCalibration::absorbPendingFit，可测），此处只管标志与 UI。
    if (!TimeCalibration::absorbPendingFit(m_working, m_fitResult, m_fitPending,
                                           m_noDriftCheck
                                               && m_noDriftCheck->isChecked()))
        return false;
    m_fitPending = false;
    return true;
}

void TimeSettingsDialog::onRunReconstruction()
{
    if (!m_service || m_videoPath.isEmpty() || m_durationMs <= 0)
        return;
    m_goStage = GoStage::Recon;
    if (m_reconBtn)
        m_reconBtn->setEnabled(false);
    setGoBusy(true, lang("重建中…", "Rebuilding…"));
    m_resultLabel->setText(lang(
        "正在按画面时间逐段重建…（逐点识别画面时间；长文件可能需数十分钟，可最小化窗口）",
        "Rebuilding time from frames… (per-point OCR; may take tens of minutes on long "
        "files; window can be minimized)"));
    m_service->runReconstruction(m_videoPath, m_durationMs, m_roi);
}

void TimeSettingsDialog::onRunTickAlign()
{
    startTickAlign(false);
}

bool TimeSettingsDialog::tickAlignWorthIt(const TimeCalibration::FitResult &fr) const
{
    // P-98 触发条件（�previously 拍板四条）：三点可行且「片内速率有波动/整体变速」
    // 才值得花 2~4 分钟。片内一致（残差 <=2 秒）时单直线已够，不打扰。
    if (!fr.ok || !m_service || m_videoPath.isEmpty() || m_durationMs <= 0)
        return false;
    // P-98 需框选区域定位秒位；用粘性 ROI（m_roi 可能已被「全画面重试」清空）
    if (!m_roiSticky.isValid() && !m_roi.isValid())
        return false;
    return PiecewiseTimeMap::isVariableRate(fr.rate)
           || fr.maxResidualMs > 2000.0;
}

void TimeSettingsDialog::startTickAlign(bool autoTriggered)
{
    if (!m_service || m_videoPath.isEmpty() || m_durationMs <= 0)
        return;
    if (!m_roiSticky.isValid() && !m_roi.isValid()) {
        // 2026-09-28 实测：整帧自动找秒位不可靠（会锁到别的高频区域 → 跳变数 2 倍、
        // 聚类类数爆炸、白跑 20 分钟）。故无框选时**先请用户框一下**（一次点击，
        // 之后粘性记住；框要盖住完整时间含秒）。
        m_resultLabel->setText(lang(
            "秒级精细对齐需要先框住画面上的时间戳区域（用来定位“秒”那两位数字）。\n"
            "请点上方「框选时间戳区域」把整行时间（含秒）框进去，再点「秒级精细对齐」。",
            "Second-level alignment needs the timestamp area boxed (to locate the "
            "seconds digits). Click \"Select timestamp\" above, then retry."));
        if (!m_waitingRoi)
            onRoiButton();
        return;
    }
    m_goStage = GoStage::Ocr;          // 复用忙碌态（防重入）
    if (m_tickBtn)
        m_tickBtn->setEnabled(false);
    if (m_reconBtn)
        m_reconBtn->setEnabled(false);
    setGoBusy(true, lang("秒级对齐中…", "Second-level aligning…"));
    m_resultLabel->setText(lang(
        "正在做秒级精细对齐（像素盯画面时间秒位跳变 + 稀疏取样标定）：\n"
        "%1\n预计 2~4 分钟（长片更久），可最小化窗口；失败会自动提示改用「时间重建」。",
        "Second-level alignment running (OSD second ticks + sparse anchors):\n"
        "%1\nTakes ~2-4 min (longer for long files); window can be minimized.")
        .arg(autoTriggered
                 ? lang("三点结果显示片内速率有波动，自动升级到秒级对齐。",
                        "Intra-file rate wobble detected - upgrading to second-level.")
                 : QString()));
    m_service->runTickAlign(m_videoPath, m_durationMs,
                            m_roi.isValid() ? m_roi : m_roiSticky);
}

void TimeSettingsDialog::onTickAlignReady(const QString &videoPath,
                                          const TimeCalibration &proposed)
{
    if (videoPath != m_videoPath)
        return;
    // P1-3（reviewer）：秒级对齐是异步 2~4 分钟，用户完全可能在这期间做
    // 第 2 步「对真实时间」；而候选里不含 truth* 字段 → 整包替换会静默丢掉
    // 北京时间偏移（报告时间口径错）。此处把工作面的对时字段补进候选。
    TimeCalibration merged = proposed;
    if (m_working.isValid() && !m_working.truthSource.isEmpty()
        && merged.truthSource.isEmpty()) {
        merged.truthOffsetMs = m_working.truthOffsetMs;
        merged.truthSet = m_working.truthSet;
        merged.truthCheckedAtMs = m_working.truthCheckedAtMs;
        merged.truthNote = m_working.truthNote;
        merged.truthSource = m_working.truthSource;
        merged.truthImagePath = m_working.truthImagePath;
        merged.truthMonitorBox = m_working.truthMonitorBox;
        merged.truthBeijingBox = m_working.truthBeijingBox;
        merged.truthMonitorText = m_working.truthMonitorText;
        merged.truthBeijingText = m_working.truthBeijingText;
        if (merged.calibNote.isEmpty())
            merged.calibNote = m_working.calibNote;
        if (m_truthPreviewLabel)
            m_truthPreviewLabel->setText(lang(
                "（秒级对齐已保留此前的对时：偏差 %1）",
                "(second-level align kept the previous truth offset: %1)")
                    .arg(TruthPhotoConfirmDialog::fmtOffsetVerbose(
                        merged.truthOffsetMs)));
    }
    TimeCalibration &tickCal = merged;
    m_fitResult = tickCal;             // 秒级表即当前最优结果
    m_fitPending = false;              // 直接应用，不再等用户点
    fillSampleTable(tickCal);
    refitSummaryRefresh();
    applyWorking(tickCal);
    m_goStage = GoStage::Done;
    setGoBusy(false, QString());
    if (m_tickBtn)
        m_tickBtn->setEnabled(true);
    if (m_reconBtn)
        m_reconBtn->setEnabled(true);
    m_useBtn->setText(lang("✓ 已应用", "✓ Applied"));
    m_resultLabel->setText(lang(
        "✓ 已应用秒级精细对齐：%1 个秒级锚点（跳过 %2 个画面秒 = 加速导出抽真丢帧）\n%3",
        "✓ Second-level align applied: %1 ticks (%2 display seconds skipped = "
        "frame drops in accelerated export)\n%3")
        .arg(tickCal.tickAnchors.size())
        .arg(tickCal.tickSkippedSeconds, 0, 'f', 0)
        .arg(m_resultLabel->text()));
    emit goTaskFinished(lang("校时完成", "Calibration finished"),
                        lang("%1：秒级精细对齐完成。",
                             "%1: second-level alignment done.")
                            .arg(QFileInfo(videoPath).fileName()));
}

void TimeSettingsDialog::onTickAlignFailed(const QString &videoPath,
                                           const QString &error)
{
    if (videoPath != m_videoPath)
        return;
    m_goStage = GoStage::Done;
    setGoBusy(false, QString());
    if (m_tickBtn)
        m_tickBtn->setEnabled(true);
    if (m_reconBtn)
        m_reconBtn->setEnabled(true);
    m_resultLabel->setText(lang(
        "秒级对齐不可用（%1）。\n可能原因：画面时间戳无秒位/被遮挡/秒位不跳变。\n"
        "→ 已保留三点结果（全局倍率）；要更准可点「时间重建」逐段精修（长文件 40~60 分钟）。",
        "Second-level alignment unavailable (%1).\n"
        "Cause: no seconds field / occluded / not ticking.\n"
        "-> Kept the 3-point result; use \"Rebuild\" for per-segment refinement.")
        .arg(error));
}

bool TimeSettingsDialog::fitRateIsConfirmedVariable() const
{
    if (!m_fitResult.isValid() || m_fitResult.samples.isEmpty())
        return false;
    const TimeCalibration::FitResult fr = TimeCalibration::fit(m_fitResult.samples);
    return fr.ok && fr.warning == TimeCalibration::FitWarning::RateInsane
        && TimeCalibration::rateChangeSelfConsistent(fr);
}

QString TimeSettingsDialog::filenameDateHint(qint64 ocrWallMs) const
{
    if (ocrWallMs <= 0 || m_videoPath.isEmpty())
        return QString();
    const FilenameTimestamp ft = parseFilenameTimestamp(QFileInfo(m_videoPath).fileName());
    if (!ft.hit() || ft.epochMs <= 0)
        return QString();
    const QDate dFile = QDateTime::fromMSecsSinceEpoch(ft.epochMs).date();
    const QDate dOcr = QDateTime::fromMSecsSinceEpoch(ocrWallMs).date();
    const qint64 diffDays = dFile.daysTo(dOcr);
    if (qAbs(diffDays) < 1)
        return QString();
    return lang("\n　 ⚠ 文件名时间（%1）与画面时间（%2）相差 %3 天——文件名通常是「导出/下载时刻」，校时以画面时间戳为准",
                "\n   ⚠ Filename time (%1) is %3 day(s) away from on-screen time (%2) — filename is "
                "usually the export time; on-screen wins")
        .arg(dFile.toString(QStringLiteral("yyyy-MM-dd")),
             dOcr.toString(QStringLiteral("yyyy-MM-dd")))
        .arg(qAbs(diffDays));
}

void TimeSettingsDialog::applyWorking(const TimeCalibration &cal)
{
    // TEMP-DEBUG（2026-09-26 排查用，定位后删）
    {
        QFile f(QCoreApplication::applicationDirPath()
                + QStringLiteral("/calib_debug.log"));
        if (calibDebugEnabled()
            && f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            f.write(QStringLiteral("[%1] APPLY rate=%2 applied=%3 speedVariant=%4 "
                                   "offset=%5 samples=%6 truthSet=%7\n")
                        .arg(QDateTime::currentDateTime().toString(
                            QStringLiteral("HH:mm:ss.zzz")))
                        .arg(cal.rate, 0, 'f', 6).arg(cal.rateApplied ? 1 : 0)
                        .arg(cal.speedVariant ? 1 : 0).arg(cal.offsetMs)
                        .arg(cal.samples.size()).arg(cal.truthSet ? 1 : 0)
                        .toUtf8());
    }
    m_working = cal;
    m_working.calibratedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_applied = true;
    m_autoApplied = true;
    m_fitPending = false;   // 落库即消费掉未应用的第 1 步结果（防第 2 步旧值回写）
    refreshWorkingSummary();
    emit calibrationApplied(m_working);
}

void TimeSettingsDialog::maybeAutoApply()
{
    // TEMP-DEBUG（2026-09-26 排查用，定位后删）
    auto dbg = [](const QString &t) {
        QFile f(QCoreApplication::applicationDirPath()
                + QStringLiteral("/calib_debug.log"));
        if (calibDebugEnabled()
            && f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            f.write((QStringLiteral("[%1] maybeAutoApply: ")
                         .arg(QDateTime::currentDateTime().toString(
                             QStringLiteral("HH:mm:ss.zzz")))
                     + t + QLatin1Char('\n')).toUtf8());
    };
    // 三点结果：拟合有效且无"速率异常"警告 → 自动应用
    if (m_fitResult.isValid() && m_fitResult.source == TimeCalibration::Source::Ocr
        && !m_fitResult.piecewiseMode()) {
        const TimeCalibration::FitResult fr = TimeCalibration::fit(m_fitResult.samples);
        dbg(QStringLiteral("overallRate fr.ok=%1 warn=%2 rate=%3 res=%4 selfConsist=%5")
                .arg(fr.ok).arg(int(fr.warning)).arg(fr.rate, 0, 'f', 6)
                .arg(fr.maxResidualMs)
                .arg(TimeCalibration::rateChangeSelfConsistent(fr) ? 1 : 0));
        if (fr.ok
            && fr.warning != TimeCalibration::FitWarning::RateInsane) {
            if (m_noDriftCheck->isChecked())
                m_fitResult.rateApplied = false;
            applyWorking(m_fitResult);
            m_useBtn->setEnabled(false);
            m_useBtn->setText(lang("✓ 已应用", "✓ Applied"));
            m_resultLabel->setText(lang("✓ 已应用：%1", "✓ Applied: %1")
                                       .arg(m_resultLabel->text()));
            return;
        }
        // 速率异常：分两种——
        // ① 自洽的大倍率 = 非实时导出件（画面时间 N× 播放进度）。
        //    v1.18.x（2026-09-26 真机两轮实测）：旧版把这一步留给用户点「按此倍率校时」，
        //    而用户点完「自动校时」就去看时间轴了（两轮复测 .vla 均未更新，用户报
        //    「时间还是对不上」）——「必须多点一下」的确认步在实战里等于不生效。
        //    自洽判据已足够保守（n≥3 共线、残差 ≤3s、|rate−1| ≤50%），故改为**自动应用**
        //    + 醒目标注；不想要的用户可勾「不校正时钟快慢」后重新应用，或点「时间重建」分段精修。
        // ② 不自洽（疑似 OCR 误读）→ 仍不自动应用（静默采纳错字会污染整条时间轴），
        //    留在结果区等用户「确认使用此结果」。
        if (fr.ok && fr.warning == TimeCalibration::FitWarning::RateInsane
            && TimeCalibration::rateChangeSelfConsistent(fr)) {
            TimeCalibration::applyFitDecision(m_fitResult,
                                              m_noDriftCheck->isChecked());
            applyWorking(m_fitResult);
            m_useBtn->setEnabled(true);
            m_useBtn->setText(lang("✓ 已应用", "✓ Applied"));
            m_resultLabel->setText(
                m_fitResult.rateApplied
                    ? lang("✓ 已按 %1× 应用（非实时导出件，时间轴已按画面时间校正）：%2",
                           "✓ Applied at %1x (non-realtime export; timeline now "
                           "follows on-screen time): %2")
                          .arg(fr.rate, 0, 'f', 3).arg(m_resultLabel->text())
                    : lang("✓ 已应用（仅定基准，未校正快慢）：%1",
                           "✓ Applied (offset only, rate not applied): %1")
                          .arg(m_resultLabel->text()));
            // P-98：片内速率有波动 → 单直线不够，自动升级到秒级跳变对齐
            if (tickAlignWorthIt(fr))
                startTickAlign(true);
            return;
        }
        if (!fitRateIsConfirmedVariable())
            m_useBtn->setText(lang("确认使用此结果", "Use anyway"));
        return;
    }
    // 重建结果：分段有效 → 自动应用
    if (m_reconResult.piecewiseMode()) {
        applyWorking(m_reconResult);
        m_useBtn->setEnabled(false);
        m_useBtn->setText(lang("✓ 已应用", "✓ Applied"));
        m_resultLabel->setText(lang("✓ 已应用：%1", "✓ Applied: %1")
                                   .arg(m_resultLabel->text()));
    }
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
    // 快速检查失败：OCR 类错误（首尾帧可能恰好黑屏/片头）→ 降级直接三点识别；
    // 系统性错误（python/脚本缺失等）→ 直接报错。
    if (m_goStage == GoStage::Quick) {
        if (error.contains(QStringLiteral("ocr"))) {
            m_goStage = GoStage::Ocr;
            m_resultLabel->setText(lang(
                "快速检查未读取到画面时间，正在尝试三点识别…",
                "Quick check found no time; trying 3-point OCR…"));
            setGoBusy(true, lang("识别中…", "Recognizing…"));
            m_service->runThreePoint(m_videoPath, m_currentPosMs, m_durationMs,
                                     m_roi);
        } else {
            m_goStage = GoStage::Failed;
            setGoBusy(false, QString());
            m_resultLabel->setText(lang(
                "自动校时无法启动：%1。\n可检查软件目录完整性后重试。",
                "Auto calibration cannot start: %1. "
                "Check install integrity and retry.").arg(error));
        }
        return;
    }
    // TEMP-DEBUG（2026-09-26 排查用，定位后删）
    {
        QFile f(QCoreApplication::applicationDirPath()
                + QStringLiteral("/calib_debug.log"));
        if (calibDebugEnabled()
            && f.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
            f.write(QStringLiteral("[%1] SERVICE FAILED stage=%2 err=%3")
                        .arg(QDateTime::currentDateTime().toString(
                            QStringLiteral("HH:mm:ss.zzz")))
                        .arg(int(m_goStage)).arg(error).toUtf8()
                    + QByteArrayLiteral("\n"));
    }
    if (m_goStage == GoStage::Ocr || m_goStage == GoStage::Recon) {
        // v1.7.1：带框选识别失败 → 自动清框选全画面重试一次（用户实测：
        // 拼接产物沿用旧框选记忆，位置不匹配致 ocr_all_failed；全画面
        // 自适应搜索实测可成功识别）
        if (m_goStage == GoStage::Ocr && m_roi.isValid() && !m_roiRetried
            && error.contains(QStringLiteral("ocr"))) {
            m_roiRetried = true;
            m_roi = QRectF();
            m_resultLabel->setText(lang(
                "框选区域未识别到时间，已切换全画面自动识别重试…",
                "Boxed region had no time; retrying with full-frame OCR…"));
            setGoBusy(true, lang("识别中…", "Recognizing…"));
            m_service->runThreePoint(m_videoPath, m_currentPosMs, m_durationMs,
                                     m_roi);
            return;
        }
        m_goStage = GoStage::Failed;
        setGoBusy(false, QString());
        // v1.18.x：终态清 pending——本轮三点没算出可用结果，不能让第 2 步
        // 把上一轮（已被本次取代）的 fit 当“第 1 步结果”并入
        m_fitPending = false;
        m_resultLabel->setText(lang(
            "未能识别画面中的时间（%1）。\n"
            "可能原因：\n"
            "· 画面中没有时间显示，或时间不含日期（需 年月日 时分秒）\n"
            "· 时间格式为「时分 + 独立秒框」之类非标准写法（如水印相机）\n"
            "· 时间字体/位置特殊\n"
            "· 框选区域与时间戳位置不匹配（可重新框选）\n"
            "→ 不用卡在这里：用上方「画面时间读不出？手动录入」照拄画面时间\n"
            "   建立基准，第 2 步即可继续。\n"
            "   （单点只能定基准；如需修正“时钟快慢”仍建议修好框选后重跑 GO）",
            "Could not read on-screen time (%1).\n"
            "Possible causes:\n"
            "- no timestamp on screen, or no date (needs yyyy-mm-dd hh:mm:ss)\n"
            "- non-standard layout, e.g. hh:mm plus a separate seconds chip\n"
            "- unusual font/position\n"
            "- boxed area does not match the timestamp (re-select it)\n"
            "-> Do not get stuck: use \"Enter on-screen time\" above to set the base\n"
            "   manually, then continue with step 2.\n"
            "   (A single point fixes the offset only; GO is still needed for drift.)")
                .arg(error));
        emit goTaskFinished(lang("校时失败", "Calibration failed"),
                            lang("%1：%2", "%1: %2")
                                .arg(QFileInfo(videoPath).fileName(), error));
        return;
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
    if (!m_fitResult.isValid())
        return;
    refitSummaryRefresh();
    // v1.18.x：已自动应用过的结果（含自动应用的「非实时导出件」倍率）——
    // 勾选状态一变就得重落库，否则用户勾了「不校正时钟快慢」而时间轴仍按倍率走
    // （逃生门必须真的能逃）。手动/重建来源不在此列（不归本勾选管）。
    if (m_autoApplied && m_fitResult.source == TimeCalibration::Source::Ocr
        && !m_fitResult.piecewiseMode()) {
        TimeCalibration::applyFitDecision(m_fitResult,
                                          m_noDriftCheck->isChecked());
        applyWorking(m_fitResult);
        m_resultLabel->setText(
            m_fitResult.rateApplied
                ? lang("✓ 已按 %1× 应用（非实时导出件）：%2",
                       "✓ Applied at %1x (non-realtime export): %2")
                      .arg(m_fitResult.rate, 0, 'f', 3).arg(m_resultLabel->text())
                : (m_working.tickMode()
                       ? lang("✓ 已应用（注：秒级对齐表按画面时间逐秒锚定，不受"
                              "「不修正时钟快慢」勾选影响）：%1",
                              "✓ Applied (note: the second-level tick table anchors "
                              "every on-screen second, so the drift checkbox does not "
                              "affect it): %1")
                       : lang("✓ 已改为仅定基准（不校正快慢）：%1",
                              "✓ Switched to offset-only (no drift correction): %1"))
                      .arg(m_resultLabel->text()));
    }
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
    // v1.18.x：取样跨度太短 → 只能定基准，不可能测出快慢（实测踩过：两点相隔 2 秒
    // 被当成“校时完成”，实际只定了基准 → 时间仍然越走越偏）
    if (fr.ok && fr.pointsUsed >= 2) {
        qint64 lo = 0, hi = 0;
        bool first = true;
        for (const auto &s : m_fitResult.samples) {
            if (!s.used || s.streamMs < 0)
                continue;
            if (first) { lo = hi = s.streamMs; first = false; }
            else { lo = qMin(lo, s.streamMs); hi = qMax(hi, s.streamMs); }
        }
        const qint64 spanMs = hi - lo;
        const bool rateMeasurable = std::fabs(fr.rate - 1.0)
                                    > TimeCalibration::kMinSignificantRateDev;
        if (!first && spanMs < 60000 && !rateMeasurable) {
            text += lang("\n　 ⚠ 取样跨度仅 %1 秒：只能定基准，**无法判断时钟快慢**——"
                         "若画面时间比播放进度快/慢（如平台加速导出），后面会越走越偏；"
                         "请把两点取到片头/片尾（或点 GO 自动校时）。",
                         "\n   ⚠ Sample span only %1 s: offset only, clock rate NOT measurable — "
                         "if on-screen time runs faster/slower than playback, later times will "
                         "drift; take the two points at the clip's head/tail (or click GO).")
                       .arg(spanMs / 1000);
        }
    }
    // v1.18.x：文件名时间戳与画面日期不一致时的显式提示（公安导出件常见：
    // 文件名是导出/下载时刻，画面才是内容时刻——不提示容易人工误采信）
    if (fr.ok && !m_fitResult.samples.isEmpty())
        text += filenameDateHint(m_fitResult.samples.first().wallMs);
    m_resultLabel->setText(text);
    QString warn;
    bool adoptable = fr.ok;
    if (fr.warning == TimeCalibration::FitWarning::RateInsane) {
        if (TimeCalibration::rateChangeSelfConsistent(fr)) {
            // v1.18.x：测点共线的大倍率 = 非实时导出/变速件（不是误读）→
            // 允许用户确认后按此倍率校时（结果标注变速）
            warn = lang("⚠ 画面时间约为播放进度的 %1 倍（整个文件一致，疑似非实时导出/抽帧，"
                        "不是读数错误）——核对测点无误后点「按此倍率校时」",
                        "⚠ On-screen time runs ~%1x of playback (consistent → likely non-realtime "
                        "export) — verify samples, then \"Calibrate at this rate\"")
                       .arg(fr.rate, 0, 'f', 3);
            adoptable = true;
            if (!m_autoApplied)
                m_useBtn->setText(lang("按此倍率校时", "Calibrate at this rate"));
        } else {
            warn = lang("⚠ 识别速率异常且测点不成直线（疑似读数错误）——请核对或重取测点",
                        "⚠ Insane rate with scattered samples (likely misread) — re-check samples");
            adoptable = false;
        }
    } else if (fr.warning == TimeCalibration::FitWarning::OutlierSuspected) {
        warn = lang("⚠ 有取样点异常：可取消勾选该点，将自动重新计算",
                    "⚠ Outlier suspected: uncheck the row to recompute");
    }
    if (!m_autoApplied && fr.warning != TimeCalibration::FitWarning::RateInsane)
        m_useBtn->setText(lang("✓ 使用此结果", "✓ Use this result"));
    // v1.18.x（reviewer 2026-09-26）：用户把测点全勾掉 → fr 无效、结果区显示
    // 「有效取样点不足」；此时不能还挂着 pending，否则第 2 步会拿「最后一次
    // 有效 refit」的旧值当第 1 步结果并入，且提示文案（含倍率）与实际不符。
    if (!fr.ok)
        m_fitPending = false;
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
            : lang("自动校时", "Auto calibrate"));
    }
    m_progressLabel->setText(busy ? stageText : QString());
}

// ---------------------------------------------------------------------------
// 第 1 步的手动出路（v1.18.1 单点 / v1.18.2 两点）：OSD 无法 OCR 时的唯一入口
// 语义：记录「某播放位置上，画面显示的日期时间」。
//   仅点1 → 固定偏移（rate=1，等同 v1.18.1 行为）
//   点1+点2 → 复用 TimeCalibration::fit 做最小二乘，同时得偏移与速率
// （复用 domain 拟合而不自写数学：显著性/合理上限/标准误全走既有规则，R9）
// ---------------------------------------------------------------------------
namespace {
/// 手动取样点（rawText 留痕，便于在细节表里看出是人填的而非 OCR）
TimeCalibration::Sample makeManualSample(qint64 streamMs, qint64 wallMs)
{
    TimeCalibration::Sample s;
    s.streamMs = streamMs;
    s.wallMs = wallMs;
    s.rawText = QStringLiteral("手动录入");
    s.conf = 1.0;
    s.used = true;
    return s;
}
} // namespace

void TimeSettingsDialog::setPlayhead(qint64 ms)
{
    m_playheadMs = qMax<qint64>(0, ms);
    if (m_manualLivePos)
        m_manualLivePos->setText(lang("位置取当前播放头：%1", "Uses playhead: %1")
                                     .arg(fmtStreamMs(m_playheadMs)));
}

void TimeSettingsDialog::onTakeManualP1()
{
    m_manualP1PosMs = m_playheadMs;
    if (m_manualP1Pos)
        m_manualP1Pos->setText(fmtStreamMs(m_manualP1PosMs));
}

void TimeSettingsDialog::onTakeManualP2()
{
    m_manualP2PosMs = m_playheadMs;
    if (m_manualP2Pos)
        m_manualP2Pos->setText(fmtStreamMs(m_manualP2PosMs));
}

bool TimeSettingsDialog::applyManualSamples(
    const QVector<TimeCalibration::Sample> &samples, bool twoPoint)
{
    // v1.18.x（reviewer 2026-09-26 P2-5）：手动录入会取代自动识别结论。若此时还有
    // 「已算出未应用」的三点结果（如非实时导出件 1.139×），必须显式告知——
    // 否则用户改走手动后速率结论静默消失（后续第 2 步也不会再并入）。
    // 注意在确认框/早退之前取，取消时不清 pending。
    const bool replacedPending = m_fitPending;
    const double replacedRate = m_fitResult.rate;
    const TimeCalibration::FitResult fr = TimeCalibration::fit(samples);
    if (!fr.ok) {
        QMessageBox::warning(this, lang("无法应用", "Cannot apply"),
            lang("取样点无效，无法建立基准。", "Invalid samples."));
        return false;
    }
    bool forceRate = false;   // 用户确认的非实时导出倍率 → 应用后标注变速
    if (fr.warning == TimeCalibration::FitWarning::RateInsane) {
        if (!TimeCalibration::rateChangeSelfConsistent(fr)) {
            // 测点不成直线 → 更像读数错误：拒绝（静默采纳错字会污染整条时间轴）
            QMessageBox::warning(this, lang("数值不合理", "Implausible"),
                lang("推算出的时钟快慢超出合理范围（每天 %1 秒），且各测点不成直线，"
                     "疑似时间戳读错。\n请核对两点的画面时间后重试。",
                     "Implied drift %1 s/day is out of range and samples are scattered "
                     "(likely misread); check both times.")
                    .arg(fr.driftSecondsPerDay(), 0, 'f', 1));
            return false;
        }
        // v1.18.x：测点自洽的大倍率 = 非实时导出/抽帧件（顺德公安件实测 1.139×）
        // ——不是读数错误；用户确认两点无误后按此倍率校时，结果标注「变速」
        const auto reply = QMessageBox::question(this,
            lang("疑似非实时导出", "Likely non-realtime export"),
            lang("推算出的画面时间约为播放进度的 %1 倍（每天 %2 秒）。\n"
                 "各测点成直线，通常意味着导出件被加速回放/抽帧（不是摄像机钟快慢，"
                 "也不是读错）。\n核对两点画面时间无误后，是否按此倍率校时？\n"
                 "（结果将标注为变速文件）",
                 "On-screen time runs ~%1x of playback (%2 s/day). Samples are collinear → "
                 "likely an accelerated/decimated export, not a misread.\n"
                 "Calibrate at this rate? (result will be marked variable-rate)")
                .arg(fr.rate, 0, 'f', 3).arg(fr.driftSecondsPerDay(), 0, 'f', 0),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
        if (reply != QMessageBox::Yes)
            return false;
        forceRate = true;
    }

    TimeCalibration c = m_working;
    c.source = TimeCalibration::Source::Manual;
    c.dateKnown = true;
    c.samples = samples;
    c.conf = 1.0;
    // 手动取样替代 OCR/重建结论：清掉它们遗留的变速态，避免与本次拟合矛盾
    c.piecewise = PiecewiseTimeMap();
    c.piecewiseApplied = false;
    c.speedVariant = false;
    c.boundaryCount = 0;
    c.applyFit(fr);   // offsetMs/rate/sigmaRate；rateApplied 走既有显著性规则
    if (forceRate) {
        // 用户已确认：这是非实时导出件（画面时间 ≈ rate× 播放进度）——
        // 按拟合倍率应用并在报告/徽标标注「变速」
        c.rateApplied = true;
        c.speedVariant = true;
    }

    applyWorking(c);
    m_goStage = GoStage::Done;
    setGoBusy(false, QString());
    m_detailsBtn->setEnabled(false);
    m_useBtn->setEnabled(false);

    // 就地确认（用户反馈：输入后没有确认结果的方法）——把“哪个位置↔哪个时间”
    // 这对关键映射显式回读出来，用户一眼可核
    QString msg = lang("✓ 已应用：位置 %1 的画面时间 = %2",
                       "✓ Applied: on-screen time at %1 = %2")
                      .arg(fmtStreamMs(samples.first().streamMs),
                           fmtWall(samples.first().wallMs));
    if (replacedPending)
        msg += lang("\n　 ⚠ 已用手动录入取代自动识别结果（原拟合倍率 %1× 未采用；"
                    "如需按画面时间快慢校时，请改用「按此倍率校时」）",
                    "\n   ⚠ Manual entry replaced the auto result (fitted rate "
                    "%1x not applied; use \"Calibrate at this rate\" to keep it)")
                   .arg(replacedRate, 0, 'f', 3);
    if (twoPoint) {
        msg += lang("\n　 位置 %1 的画面时间 = %2",
                    "\n   on-screen time at %1 = %2")
                   .arg(fmtStreamMs(samples.last().streamMs),
                        fmtWall(samples.last().wallMs));
        if (c.rateApplied)
            msg += lang("\n　 已修正时钟快慢：每天 %1 秒",
                        "\n   drift corrected: %1 s/day")
                       .arg(fr.driftSecondsPerDay(), 0, 'f', 1);
        else
            msg += lang("\n　 跨度内时钟快慢不显著（每天 %1 秒），仅对基准",
                        "\n   drift not significant (%1 s/day); offset only")
                       .arg(fr.driftSecondsPerDay(), 0, 'f', 1);
    } else {
        msg += lang("\n　 仅对基准；如需修正时钟快慢，展开下方两点模式",
                    "\n   offset only; expand two-point mode for drift");
    }
    msg += lang("\n　 第 2 步「对真实时间」已解锁。",
                "\n   Step 2 (align to real time) is now unlocked.");
    if (m_manualResultLabel) {
        m_manualResultLabel->setText(msg);
        m_manualResultLabel->show();
    }
    m_resultLabel->setText(lang("已按手动录入建立画面时间基准（见上方确认）。",
                                "On-screen time base set from manual input "
                                "(see confirmation above)."));
    return true;
}

void TimeSettingsDialog::onAdoptManualBase()
{
    // 默认路径：位置 = 当前播放头。不要求任何“先取位置”的前置动作
    // （v1.18.2 曾要求先点「取当前」，用户实测因此以为功能坏了）
    if (!m_manualSimpleEdit)
        return;
    const qint64 wall = m_manualSimpleEdit->dateTime().toMSecsSinceEpoch();
    QVector<TimeCalibration::Sample> samples;
    samples.append(makeManualSample(m_playheadMs, wall));
    applyManualSamples(samples, /*twoPoint=*/false);
}

void TimeSettingsDialog::onAdoptManualTwoPoint()
{
    if (!m_manualP1Edit || !m_manualP2Edit)
        return;
    if (m_manualP1PosMs < 0 || m_manualP2PosMs < 0) {
        QMessageBox::warning(this, lang("无法应用", "Cannot apply"),
            lang("两点模式需先各自点「取当前」记录位置：\n"
                 "① 把播放头停到点1 能看清画面时间的位置 → 点1 行「取当前」；\n"
                 "② 再停到点2 → 点2 行「取当前」。",
                 "Two-point mode needs each row's Grab first."));
        return;
    }
    if (m_manualP2PosMs <= m_manualP1PosMs) {
        QMessageBox::warning(this, lang("无法应用", "Cannot apply"),
            lang("点2 的播放位置必须晚于点1。",
                 "Point 2 must be later than point 1."));
        return;
    }
    const qint64 w1 = m_manualP1Edit->dateTime().toMSecsSinceEpoch();
    const qint64 w2 = m_manualP2Edit->dateTime().toMSecsSinceEpoch();
    if (w2 == w1) {
        QMessageBox::warning(this, lang("无法应用", "Cannot apply"),
            lang("两点的画面时间完全相同，无法推算时钟快慢。",
                 "Both points show the same on-screen time."));
        return;
    }
    QVector<TimeCalibration::Sample> samples;
    samples.append(makeManualSample(m_manualP1PosMs, w1));
    samples.append(makeManualSample(m_manualP2PosMs, w2));
    applyManualSamples(samples, /*twoPoint=*/true);
}

// ---------------------------------------------------------------------------
// 高级区：北京时间对齐 / 手动 / 录像机自带
// ---------------------------------------------------------------------------
void TimeSettingsDialog::onTruthInputChanged()
{
    // v1.12.5：方式二预览——两个时间都可编辑，偏移 = 北京时间 − 监控主机时间
    const qint64 monitorWall = m_monitorEdit->dateTime().toMSecsSinceEpoch();
    const qint64 offset = m_beijingEdit->dateTime().toMSecsSinceEpoch()
                          - monitorWall;
    m_truthPreviewLabel->setText(lang(
        "偏移：监控主机时间比北京时间 %1",
        "Offset: recorder clock is %1")
            .arg(TruthPhotoConfirmDialog::fmtOffsetVerbose(offset)));
}

void TimeSettingsDialog::onAdoptTruth()
{
    // 方式二：手动输入两个时间（自动算偏差）
    // v1.18.x：先并入未应用的第 1 步结果（否则落库旧工作面 → 时间轴仍偏）
    const bool merged = absorbPendingFit();
    if (!m_working.isValid() || !m_working.dateKnown) {
        QMessageBox::warning(this, lang("无法应用", "Cannot apply"),
            lang("需先完成第 1 步（画面时间校时），再对真实时间。",
                 "Finish step 1 (on-screen time) before aligning to real time."));
        return;
    }
    if (merged)
        m_resultLabel->setText(lang("✓ 已同时采用第 1 步的三点结果（画面时间倍率 %1×）",
                                    "✓ Step-1 3-point result also applied (rate %1x)")
                                   .arg(m_working.rate, 0, 'f', 3));
    m_working.truthOffsetMs = m_beijingEdit->dateTime().toMSecsSinceEpoch()
                              - m_monitorEdit->dateTime().toMSecsSinceEpoch();
    m_working.truthSet = true;
    m_working.truthCheckedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_working.truthNote = m_truthNoteEdit->text().trimmed();
    m_working.truthSource = QStringLiteral("manualTimes");
    // 来源切换：清掉图片来源的留档字段（SSOT：留档与来源一致）
    m_working.truthImagePath.clear();
    m_working.truthMonitorBox = QRect();
    m_working.truthBeijingBox = QRect();
    m_working.truthMonitorText.clear();
    m_working.truthBeijingText.clear();
    m_applied = true;
    refreshWorkingSummary();
    emit calibrationApplied(m_working);
}

void TimeSettingsDialog::onAdoptTruthManualOffset()
{
    // 方式三：直输偏移量「监控主机时间比北京时间 快/慢 X日X时X分X秒」
    // v1.18.x：先并入未应用的第 1 步结果（实测根因：本步旧代码直接落库 m_working，
    // 把刚算出的三点结果静默丢弃 → 非实时导出件永远按 rate=1.0 走）
    const bool merged = absorbPendingFit();
    if (!m_working.isValid() || !m_working.dateKnown) {
        QMessageBox::warning(this, lang("无法应用", "Cannot apply"),
            lang("需先完成第 1 步（画面时间校时），再对真实时间。",
                 "Finish step 1 (on-screen time) before aligning to real time."));
        return;
    }
    if (merged)
        m_resultLabel->setText(lang("✓ 已同时采用第 1 步的三点结果（画面时间倍率 %1×）",
                                    "✓ Step-1 3-point result also applied (rate %1x)")
                                   .arg(m_working.rate, 0, 'f', 3));
    const qint64 total =
        ((static_cast<qint64>(m_offsetDays->value()) * 24
          + m_offsetHours->value()) * 3600
         + m_offsetMins->value() * 60 + m_offsetSecs->value()) * 1000;
    const bool slower = (m_offsetDirCombo->currentIndex() == 0);  // 慢=正偏移
    m_working.truthOffsetMs = slower ? total : -total;
    m_working.truthSet = true;
    m_working.truthCheckedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_working.truthNote = m_truthNoteEdit->text().trimmed();
    m_working.truthSource = QStringLiteral("manualOffset");
    m_working.truthImagePath.clear();
    m_working.truthMonitorBox = QRect();
    m_working.truthBeijingBox = QRect();
    m_working.truthMonitorText.clear();
    m_working.truthBeijingText.clear();
    m_applied = true;
    refreshWorkingSummary();
    emit calibrationApplied(m_working);
}

void TimeSettingsDialog::onClearTruth()
{
    // v1.18.x：清对时同样以 m_working 落库——先并入未应用的第 1 步结果，
    // 免得「去对时」把三点结果一起抹回旧工作面。
    absorbPendingFit();
    m_working.truthOffsetMs = 0;
    m_working.truthSet = false;
    m_working.truthCheckedAtMs = 0;
    m_working.truthNote.clear();
    m_working.truthSource.clear();
    m_working.truthImagePath.clear();
    m_working.truthMonitorBox = QRect();
    m_working.truthBeijingBox = QRect();
    m_working.truthMonitorText.clear();
    m_working.truthBeijingText.clear();
    m_applied = true;
    refreshWorkingSummary();
    emit calibrationApplied(m_working);
}

// ---------------------------------------------------------------------------
// v1.12.5 校时图片框选识别（方式一）：选图 → CalibPhotoDialog 两框 →
// CalibrationService::runCalibPhoto → 域解析器算偏差 → 确认卡（原文核对）
// ---------------------------------------------------------------------------
void TimeSettingsDialog::onTruthPhotoPick()
{
    if (!m_service)
        return;
    if (!m_working.isValid() || !m_working.dateKnown) {
        QMessageBox::warning(this, lang("无法识别", "Cannot recognize"),
            lang("需先完成第 1 步（画面时间校时），再对真实时间。",
                 "Finish step 1 (on-screen time) before aligning to real time."));
        return;
    }
    const QString img = QFileDialog::getOpenFileName(
        this, lang("选择校时图片（监控屏幕与标准时间同框照片）",
                   "Choose calibration photo"),
        QString(),
        lang("图片 (*.jpg *.jpeg *.png *.bmp);;所有文件 (*)",
             "Images (*.jpg *.jpeg *.png *.bmp);;All files (*)"));
    if (img.isEmpty())
        return;
    CalibPhotoDialog dlg(img, this);
    if (!dlg.isValidImage()) {
        QMessageBox::warning(this, lang("无法读取", "Unreadable"),
            lang("图片无法读取：%1", "Cannot read image: %1").arg(img));
        return;
    }
    if (dlg.exec() != QDialog::Accepted)
        return;
    m_pendingTruthImage = img;
    m_pendingTruthBox1 = dlg.monitorBox();
    m_pendingTruthBox2 = dlg.beijingBox();
    m_truthPhotoBtn->setEnabled(false);
    m_truthPhotoBtn->setText(lang("识别中…", "Recognizing…"));
    m_service->runCalibPhoto(img, m_pendingTruthBox1, m_pendingTruthBox2);
}

void TimeSettingsDialog::onCalibPhotoFinished(
    bool ok, const QVector<QPair<QString, double>> &monitorLines,
    const QVector<QPair<QString, double>> &beijingLines, const QString &error)
{
    m_truthPhotoBtn->setEnabled(true);
    m_truthPhotoBtn->setText(
        lang("从校时图片识别（框选监控主机时间 + 北京时间）…",
             "From calibration photo (box both clocks)…"));
    if (!ok) {
        QMessageBox::warning(this, lang("识别失败", "OCR failed"),
            lang("校时图片识别失败：%1\n可改用方式二/三手动输入。",
                 "Photo OCR failed: %1\nYou can use manual input instead.")
                .arg(error));
        return;
    }
    const auto toTexts = [](const QVector<QPair<QString, double>> &lines) {
        QStringList out;
        for (const auto &p : lines)
            out.append(p.first);
        return out;
    };
    // 框 1 监控主机时间：须含完整日期+时分秒（取证留痕要求日期可考）
    const TruthTimeParse mon = parseTruthTimeText(toTexts(monitorLines), QDate());
    if (!mon.ok || !mon.dateFromText) {
        QMessageBox::warning(this, lang("框 1 未识出完整时间", "Box 1 failed"),
            lang("框 1（监控主机时间）需包含完整日期与带秒的时间，\n"
                 "如「2026年07月22日 星期三 12:25:47」。请重新框选或手动输入。\n"
                 "（错误：%1）",
                 "Box 1 (recorder clock) must contain full date and time with "
                 "seconds. Re-box or enter manually. (error: %1)")
                .arg(mon.error));
        return;
    }
    // 框 2 北京时间：完整/跨行组合/纯时间（日期取框 1 同日，跨日疑义自动 ±1 日取小）
    const QDate monDate =
        QDateTime::fromMSecsSinceEpoch(mon.wallMs).date();
    TruthTimeParse bj = parseTruthTimeText(toTexts(beijingLines), monDate);
    if (!bj.ok) {
        const QString hint = bj.error.startsWith(QStringLiteral("noseconds:"))
            ? lang("框 2（北京时间）识别到的时间不含秒（如手机状态栏 12:39）。\n"
                   "请框含秒的时间显示（如授时网页大时钟），或手动输入。",
                   "Box 2 time has no seconds. Box a clock with seconds, or "
                   "enter manually.")
            : lang("框 2（北京时间）未识出有效时间。请重新框选或手动输入。\n"
                   "（错误：%1）",
                   "Box 2 (Beijing time) not recognized. Re-box or enter "
                   "manually. (error: %1)").arg(bj.error);
        QMessageBox::warning(this, lang("框 2 未识出", "Box 2 failed"), hint);
        return;
    }
    // 跨日疑义：框 2 仅时间且 |偏差|>12h → 试 ±1 日取 |偏差| 较小者（取证稳妥）
    QString crossDayNote;
    if (!bj.dateFromText && qAbs(bj.wallMs - mon.wallMs) > 12LL * 3600000) {
        const qint64 day = 86400000LL;
        const qint64 o0 = bj.wallMs - mon.wallMs;
        const qint64 oM = bj.wallMs - day - mon.wallMs;
        const qint64 oP = bj.wallMs + day - mon.wallMs;
        if (qAbs(oM) < qAbs(o0) && qAbs(oM) <= qAbs(oP)) {
            bj.wallMs -= day;
            crossDayNote = lang("（北京时间按前一日处理）",
                                "(Beijing time taken as previous day)");
        } else if (qAbs(oP) < qAbs(o0)) {
            bj.wallMs += day;
            crossDayNote = lang("（北京时间按后一日处理）",
                                "(Beijing time taken as next day)");
        }
    }
    // 确认卡（v1.12.6：图片集成可放大校对 + 两个时间可直接修改、
    // 偏差实时重算；采用时以卡上（可能被用户修正的）时间为准）
    TruthPhotoConfirmDialog confirmDlg(
        m_pendingTruthImage, m_pendingTruthBox1, m_pendingTruthBox2,
        mon.wallMs, mon.matchedText,
        bj.wallMs, bj.matchedText,
        crossDayNote, this);
    if (confirmDlg.exec() != QDialog::Accepted)
        return;
    // v1.15.1 热修：v1.12.6 确认卡改可编辑重构时漏了 truthOffsetMs 赋值
    // （offset 算出后无人使用）——抽成 adoptPhotoTruth 由回归测试直驱锁死
    adoptPhotoTruth(confirmDlg.offsetMs(), m_pendingTruthImage,
                    m_pendingTruthBox1, m_pendingTruthBox2,
                    mon.matchedText, bj.matchedText, confirmDlg.userEdited());
}

void TimeSettingsDialog::adoptPhotoTruth(
    qint64 offsetMs, const QString &imagePath,
    const QRect &monitorBox, const QRect &beijingBox,
    const QString &monitorText, const QString &beijingText, bool userEdited)
{
    // v1.18.x：同 onAdoptTruth*——图片来源的对时也是「第 2 步」，落库前必须
    // 并入未应用的第 1 步结果，否则三点结果被旧工作面静默覆盖。
    absorbPendingFit();
    m_working.truthOffsetMs = offsetMs;   // ← v1.12.6~1.15.0 漏掉的赋值
    m_working.truthSet = true;
    m_working.truthCheckedAtMs = QDateTime::currentMSecsSinceEpoch();
    m_working.truthSource = QStringLiteral("photo");
    m_working.truthImagePath = imagePath;
    m_working.truthMonitorBox = monitorBox;
    m_working.truthBeijingBox = beijingBox;
    m_working.truthMonitorText = monitorText;
    m_working.truthBeijingText = beijingText;
    if (m_working.truthNote.isEmpty())
        m_working.truthNote = lang("校时图片对时", "Photo time check");
    if (userEdited)
        m_working.truthNote += lang("（确认卡人工修正读数）",
                                    " (readings hand-corrected in confirm card)");
    m_applied = true;
    refreshWorkingSummary();
    emit calibrationApplied(m_working);
}
