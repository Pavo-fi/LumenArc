/**
 * @file microdiffdialog.cpp
 * @brief 微变分析设置面板实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-09-10
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "microdiffdialog.h"

#include "domain/microdiff_core.h"
#include "i18n.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QSlider>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>

MicroDiffDialog::MicroDiffDialog(QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(lang("微变分析", "Micro-change analysis"));
    setMinimumWidth(430);

    // ---------- 显示 ----------
    auto *dispBox = new QGroupBox(lang("显示", "Display"), this);
    auto *dispForm = new QFormLayout(dispBox);

    m_enable = new QCheckBox(lang("启用微变分析", "Enable micro-change"), dispBox);
    m_enable->setToolTip(lang("按当前等级对画面做微变彩色增强；计算只用亮度，显示保留原彩画面",
                              "Colour-enhance subtle changes at the current level; "
                              "computation uses luminance, display keeps the original colour"));
    dispForm->addRow(QString(), m_enable);

    m_mode = new QComboBox(dispBox);
    m_mode->addItem(lang("原彩 + 微变叠加（推荐）", "Original colour + overlay"), 0);
    m_mode->addItem(lang("纯微变彩场", "Micro-change field only"), 1);
    m_mode->addItem(lang("并排对比（左原图 / 右微变）", "Side by side"), 2);
    m_mode->addItem(lang("仅原图（关闭增强）", "Original only"), 3);
    dispForm->addRow(lang("显示模式", "Mode"), m_mode);

    auto *levelRow = new QWidget(dispBox);
    auto *levelLay = new QHBoxLayout(levelRow);
    levelLay->setContentsMargins(0, 0, 0, 0);
    m_level = new QSlider(Qt::Horizontal, levelRow);
    m_level->setRange(1, 5);
    m_level->setPageStep(1);
    m_level->setToolTip(lang("等级越小越敏感（增益越大），也越容易把压缩噪点放大",
                             "Lower level = more sensitive (higher gain), but also amplifies "
                             "compression noise"));
    m_levelLabel = new QLabel(levelRow);
    m_levelLabel->setMinimumWidth(120);
    levelLay->addWidget(m_level, 1);
    levelLay->addWidget(m_levelLabel);
    dispForm->addRow(lang("微变等级", "Level"), levelRow);

    auto *strengthRow = new QWidget(dispBox);
    auto *strengthLay = new QHBoxLayout(strengthRow);
    strengthLay->setContentsMargins(0, 0, 0, 0);
    m_strength = new QSlider(Qt::Horizontal, strengthRow);
    m_strength->setRange(0, 100);
    m_strengthLabel = new QLabel(strengthRow);
    m_strengthLabel->setMinimumWidth(50);
    strengthLay->addWidget(m_strength, 1);
    strengthLay->addWidget(m_strengthLabel);
    dispForm->addRow(lang("叠加强度", "Overlay strength"), strengthRow);

    m_scope = new QComboBox(dispBox);
    m_scope->addItem(lang("仅当前 ROI 区域", "Current ROI only"), 0);
    m_scope->addItem(lang("全画面", "Full frame"), 1);
    dispForm->addRow(lang("处理范围", "Scope"), m_scope);

    m_temporal = new QSpinBox(dispBox);
    m_temporal->setRange(1, 41);
    m_temporal->setToolTip(lang("时域一致性窗口：多帧偏差求平均，噪声互相抵消、持续的烟保留。"
                                "窗口越大画面越干净、对快速变化越迟钝",
                                "Temporal consistency window: averaging removes noise while "
                                "persistent smoke survives. Larger = cleaner but slower"));
    dispForm->addRow(lang("时域窗（帧）", "Temporal window"), m_temporal);

    m_roiLabel = new QLabel(dispBox);
    m_roiLabel->setWordWrap(true);
    dispForm->addRow(lang("区域状态", "Region"), m_roiLabel);

    // ---------- 基准段 ----------
    auto *baseBox = new QGroupBox(lang("基准段（起火前无火画面）", "Baseline segment"), this);
    auto *baseForm = new QFormLayout(baseBox);

    m_baseStart = new QDoubleSpinBox(baseBox);
    m_baseStart->setRange(0.0, 360000.0);
    m_baseStart->setDecimals(1);
    m_baseStart->setSuffix(lang(" 秒", " s"));
    baseForm->addRow(lang("起点（视频时间轴）", "Start"), m_baseStart);

    m_baseDur = new QDoubleSpinBox(baseBox);
    m_baseDur->setRange(5.0, 1800.0);
    m_baseDur->setDecimals(0);
    m_baseDur->setSuffix(lang(" 秒", " s"));
    baseForm->addRow(lang("时长", "Duration"), m_baseDur);

    auto *btnRow = new QWidget(baseBox);
    auto *btnLay = new QHBoxLayout(btnRow);
    btnLay->setContentsMargins(0, 0, 0, 0);
    m_collectBtn = new QPushButton(lang("采集基准", "Collect baseline"), btnRow);
    m_curveBtn = new QPushButton(lang("计算变化率曲线", "Rate-of-change curve"), btnRow);
    m_curveBtn->setEnabled(false);   // 基准采集成功后才可用
    m_curveBtn->setToolTip(lang("先采集基准段，再对全片逐秒计算微变强度曲线并判定首帧微变",
                                "Collect a clean baseline first; then the whole clip is scored per second"));
    m_cancelBtn = new QPushButton(lang("取消", "Cancel"), btnRow);
    m_cancelBtn->setEnabled(false);
    btnLay->addWidget(m_collectBtn);
    btnLay->addWidget(m_curveBtn);
    btnLay->addWidget(m_cancelBtn);
    baseForm->addRow(QString(), btnRow);

    m_progress = new QProgressBar(baseBox);
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setVisible(false);
    baseForm->addRow(QString(), m_progress);

    m_status = new QLabel(lang("尚未采集基准。请把起点/时长设在起火前的干净画面上。",
                               "No baseline yet. Set the segment to a clean period before the fire."),
                          baseBox);
    m_status->setWordWrap(true);
    baseForm->addRow(QString(), m_status);

    auto *root = new QVBoxLayout(this);
    root->addWidget(dispBox);
    root->addWidget(baseBox);
    root->addStretch(1);

    // ---------- 默认值 ----------
    m_enable->setChecked(false);
    m_mode->setCurrentIndex(0);
    m_scope->setCurrentIndex(0);
    m_level->setValue(3);
    m_strength->setValue(90);
    m_temporal->setValue(11);
    m_baseStart->setValue(0.0);
    m_baseDur->setValue(120.0);
    refreshLevelLabel();
    m_strengthLabel->setText(QStringLiteral("%1%").arg(m_strength->value()));

    connect(m_enable, &QCheckBox::toggled, this, [this] { emitChanged(); });
    connect(m_mode, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { emitChanged(); });
    connect(m_scope, QOverload<int>::of(&QComboBox::currentIndexChanged), this,
            [this] { emitChanged(); });
    connect(m_level, &QSlider::valueChanged, this, [this] {
        refreshLevelLabel();
        emitChanged();
    });
    connect(m_strength, &QSlider::valueChanged, this, [this] {
        m_strengthLabel->setText(QStringLiteral("%1%").arg(m_strength->value()));
        emitChanged();
    });
    connect(m_temporal, QOverload<int>::of(&QSpinBox::valueChanged), this,
            [this] { emitChanged(); });
    connect(m_collectBtn, &QPushButton::clicked, this, [this] {
        emit baselineRequested(m_baseStart->value(), m_baseDur->value());
    });
    connect(m_curveBtn, &QPushButton::clicked, this, [this] {
        emit curveRequested(m_baseStart->value(), m_baseDur->value());
    });
    connect(m_cancelBtn, &QPushButton::clicked, this, [this] { emit cancelRequested(); });
}

void MicroDiffDialog::refreshLevelLabel()
{
    m_levelLabel->setText(lang("等级 %1（增益 %2）", "Level %1 (gain %2)")
                              .arg(m_level->value())
                              .arg(microdiff::gainForLevel(m_level->value()), 0, 'f', 1));
}

void MicroDiffDialog::emitChanged()
{
    if (m_updating)
        return;
    emit paramsChanged();
}

MicroDiffParams MicroDiffDialog::params() const
{
    MicroDiffParams p;
    p.enabled = m_enable->isChecked();
    p.mode = static_cast<MicroDiffMode>(std::max(0, m_mode->currentData().toInt()));
    p.level = m_level->value();
    p.strengthPercent = m_strength->value();
    p.temporalFrames = m_temporal->value();
    p.sigma = 5.0;
    p.fullFrame = (m_scope->currentData().toInt() == 1);
    p.roi = QRect();       // 由 MainWindow 依据 RoiModel 填充
    return p;
}

void MicroDiffDialog::setParams(const MicroDiffParams &p)
{
    m_updating = true;
    m_enable->setChecked(p.enabled);
    m_mode->setCurrentIndex(m_mode->findData(static_cast<int>(p.mode)));
    m_scope->setCurrentIndex(p.fullFrame ? 1 : 0);
    m_level->setValue(std::max(1, std::min(5, p.level)));
    m_strength->setValue(std::max(0, std::min(100, p.strengthPercent)));
    m_temporal->setValue(std::max(1, std::min(41, p.temporalFrames)));
    refreshLevelLabel();
    m_strengthLabel->setText(QStringLiteral("%1%").arg(m_strength->value()));
    m_updating = false;
}

void MicroDiffDialog::setDefaultSegment(double videoDurationSec, double currentSec)
{
    const double dur = 120.0;
    double start = std::max(0.0, currentSec - 180.0);
    if (videoDurationSec > dur)
        start = std::min(start, videoDurationSec - dur);
    m_updating = true;
    m_baseDur->setValue(dur);
    m_baseStart->setValue(start);
    m_updating = false;
}

void MicroDiffDialog::setRoiSummary(int rectCount, int polygonCount)
{
    if (rectCount <= 0 && polygonCount <= 0) {
        m_roiLabel->setText(lang("未绘制 ROI —— 按“仅当前 ROI 区域”时会自动使用全画面",
                                 "No ROI drawn — falls back to the full frame"));
    } else {
        m_roiLabel->setText(lang("矩形 %1 个 / 多边形 %2 个（多边形本期不参与微变）",
                                 "Rect %1 / Polygon %2 (polygons not used yet)")
                                .arg(rectCount)
                                .arg(polygonCount));
    }
}

void MicroDiffDialog::setBusy(bool on)
{
    m_collectBtn->setEnabled(!on);
    m_curveBtn->setEnabled(!on && m_baselineReady);
    m_cancelBtn->setEnabled(on);
    m_progress->setVisible(on);
    if (on)
        m_progress->setValue(0);
}

void MicroDiffDialog::setBaselineReady(bool ok)
{
    m_baselineReady = ok;
    m_curveBtn->setEnabled(ok);
}

void MicroDiffDialog::setStatus(const QString &text)
{
    m_status->setText(text);
}

void MicroDiffDialog::setProgress(int percent)
{
    m_progress->setValue(std::max(0, std::min(100, percent)));
}

void MicroDiffDialog::closeEvent(QCloseEvent *event)
{
    event->accept();   // 非模态：关闭即隐藏，MainWindow 保留指针
}
