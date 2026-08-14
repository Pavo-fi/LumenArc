/**
 * @file playbackadjustpanel.cpp
 * @brief 播放画面调节面板实现（见 .h 设计要点）
 * @date 2026-08-14
 */
#include "playbackadjustpanel.h"
#include "i18n.h"
#include "theme.h"

#include <QWidget>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QSlider>
#include <QLabel>
#include <QPushButton>
#include <QCheckBox>

PlaybackAdjustPanel::PlaybackAdjustPanel(QWidget *parent)
    : QDockWidget(lang("画面调节", "Display Adjust"), parent)
{
    setObjectName(QStringLiteral("PlaybackAdjustPanel"));
    setFeatures(QDockWidget::DockWidgetClosable | QDockWidget::DockWidgetMovable);

    auto *body = new QWidget(this);
    auto *lay = new QVBoxLayout(body);
    lay->setContentsMargins(10, 10, 10, 10);
    lay->setSpacing(8);

    lay->addWidget(makeRow(lang("亮度", "Brightness"),
                           m_brightnessSlider, m_bValLabel, -50, 50));
    lay->addWidget(makeRow(lang("对比度", "Contrast"),
                           m_contrastSlider, m_cValLabel, -50, 50));
    // 伽马：30~300（%），100 = γ1.0 不变；>100 提亮暗部/中间调（夜暗监控常用）
    lay->addWidget(makeRow(lang("伽马", "Gamma"),
                           m_gammaSlider, m_gValLabel, 30, 300));
    m_gammaSlider->setValue(100);
    // 色阶：黑点 0~127 / 白点 128~255（烟雾低对比素材的对比度拉伸）
    lay->addWidget(makeRow(lang("黑点", "Black pt"),
                           m_blackPointSlider, m_bpValLabel, 0, 127));
    lay->addWidget(makeRow(lang("白点", "White pt"),
                           m_whitePointSlider, m_wpValLabel, 128, 255));
    m_whitePointSlider->setValue(255);

    m_invertBox = new QCheckBox(lang("反色（负片）", "Invert (negative)"), body);
    lay->addWidget(m_invertBox);

    // 旋转行（Q1 拍板方案 A，2026-08-14）：90° 步进循环，覆盖物随画面一起转
    {
        auto *row = new QWidget(body);
        auto *h = new QHBoxLayout(row);
        h->setContentsMargins(0, 0, 0, 0);
        h->setSpacing(8);
        auto *lab = new QLabel(lang("旋转", "Rotation"), row);
        lab->setFixedWidth(44);
        m_rotateBtn = new QPushButton(lang("⟳ 顺时针 90°", "⟳ 90° CW"), row);
        m_rotateBtn->setFixedHeight(26);
        m_rotValLabel = new QLabel(QStringLiteral("0°"), row);
        m_rotValLabel->setFixedWidth(34);
        m_rotValLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
        h->addWidget(lab);
        h->addWidget(m_rotateBtn, 1);
        h->addWidget(m_rotValLabel);
        lay->addWidget(row);
    }

    m_resetBtn = new QPushButton(lang("复位", "Reset"), body);
    m_resetBtn->setFixedHeight(28);
    lay->addWidget(m_resetBtn, 0, Qt::AlignRight);

    auto *hint = new QLabel(lang("仅影响显示与截图快照；分析数据与证据文件不变。",
                                 "Affects display & snapshots only; analysis/evidence untouched."),
                            body);
    hint->setWordWrap(true);
    hint->setStyleSheet(QStringLiteral("color:%1; font-size:11px;")
                            .arg(Theme::TextMuted));
    lay->addWidget(hint);
    lay->addStretch(1);

    setWidget(body);

    connect(m_brightnessSlider, &QSlider::valueChanged, this, [this](int v) {
        m_adjust.brightness = v;
        refreshLabels();
        emitAdjust();
    });
    connect(m_contrastSlider, &QSlider::valueChanged, this, [this](int v) {
        m_adjust.contrast = v;
        refreshLabels();
        emitAdjust();
    });
    connect(m_gammaSlider, &QSlider::valueChanged, this, [this](int v) {
        m_adjust.gammaPercent = v;
        refreshLabels();
        emitAdjust();
    });
    connect(m_blackPointSlider, &QSlider::valueChanged, this, [this](int v) {
        m_adjust.blackPoint = v;
        refreshLabels();
        emitAdjust();
    });
    connect(m_whitePointSlider, &QSlider::valueChanged, this, [this](int v) {
        m_adjust.whitePoint = v;
        refreshLabels();
        emitAdjust();
    });
    connect(m_invertBox, &QCheckBox::toggled, this, [this](bool on) {
        m_adjust.invert = on;
        emitAdjust();
    });
    connect(m_resetBtn, &QPushButton::clicked, this, [this]() {
        setValues(DisplayAdjust(), 0);
        emitAdjust();
        emit rotationChanged(0);
    });
    connect(m_rotateBtn, &QPushButton::clicked, this, [this]() {
        m_rotation = (m_rotation + 90) % 360;
        m_rotValLabel->setText(QString::number(m_rotation) + QStringLiteral("°"));
        emit rotationChanged(m_rotation);
    });
    refreshLabels();
}

QWidget *PlaybackAdjustPanel::makeRow(const QString &label, QSlider *&slider,
                                      QLabel *&valLabel, int minV, int maxV)
{
    auto *row = new QWidget(this);
    auto *h = new QHBoxLayout(row);
    h->setContentsMargins(0, 0, 0, 0);
    h->setSpacing(8);

    auto *lab = new QLabel(label, row);
    lab->setFixedWidth(44);
    slider = new QSlider(Qt::Horizontal, row);
    slider->setRange(minV, maxV);
    slider->setValue(minV);
    valLabel = new QLabel(row);
    valLabel->setFixedWidth(34);
    valLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    h->addWidget(lab);
    h->addWidget(slider, 1);
    h->addWidget(valLabel);
    return row;
}

void PlaybackAdjustPanel::refreshLabels()
{
    if (m_bValLabel)
        m_bValLabel->setText(QString::number(m_adjust.brightness));
    if (m_cValLabel)
        m_cValLabel->setText(QString::number(m_adjust.contrast));
    if (m_gValLabel)
        m_gValLabel->setText(QString::number(m_adjust.gammaPercent / 100.0, 'f', 2));
    if (m_bpValLabel)
        m_bpValLabel->setText(QString::number(m_adjust.blackPoint));
    if (m_wpValLabel)
        m_wpValLabel->setText(QString::number(m_adjust.whitePoint));
}

void PlaybackAdjustPanel::emitAdjust()
{
    emit adjustChanged(m_adjust);
}

void PlaybackAdjustPanel::setValues(const DisplayAdjust &adj, int rotation)
{
    m_adjust = adj;
    int d = rotation % 360;
    if (d < 0) d += 360;
    m_rotation = ((d + 45) / 90) * 90 % 360;
    const QSignalBlocker b1(m_brightnessSlider), b2(m_contrastSlider),
        b3(m_gammaSlider), b4(m_blackPointSlider), b5(m_whitePointSlider),
        b6(m_invertBox);
    if (m_brightnessSlider) {
        m_brightnessSlider->setValue(adj.brightness);
        m_contrastSlider->setValue(adj.contrast);
        m_gammaSlider->setValue(adj.gammaPercent);
        m_blackPointSlider->setValue(adj.blackPoint);
        m_whitePointSlider->setValue(adj.whitePoint);
        m_invertBox->setChecked(adj.invert);
    }
    refreshLabels();
    if (m_rotValLabel)
        m_rotValLabel->setText(QString::number(m_rotation) + QStringLiteral("°"));
}
