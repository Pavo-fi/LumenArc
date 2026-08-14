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
        m_brightness = v;
        m_bValLabel->setText(QString::number(v));
        emit adjustChanged(m_brightness, m_contrast);
    });
    connect(m_contrastSlider, &QSlider::valueChanged, this, [this](int v) {
        m_contrast = v;
        m_cValLabel->setText(QString::number(v));
        emit adjustChanged(m_brightness, m_contrast);
    });
    connect(m_resetBtn, &QPushButton::clicked, this, [this]() {
        setValues(0, 0);
        emit adjustChanged(0, 0);
    });
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
    slider->setValue(0);
    valLabel = new QLabel(QStringLiteral("0"), row);
    valLabel->setFixedWidth(30);
    valLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);

    h->addWidget(lab);
    h->addWidget(slider, 1);
    h->addWidget(valLabel);
    return row;
}

void PlaybackAdjustPanel::setValues(int brightness, int contrast)
{
    m_brightness = brightness;
    m_contrast = contrast;
    if (m_brightnessSlider) {
        QSignalBlocker b1(m_brightnessSlider), b2(m_contrastSlider);
        m_brightnessSlider->setValue(brightness);
        m_contrastSlider->setValue(contrast);
    }
    if (m_bValLabel) {
        m_bValLabel->setText(QString::number(brightness));
        m_cValLabel->setText(QString::number(contrast));
    }
}
