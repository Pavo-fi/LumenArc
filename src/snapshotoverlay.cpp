/**
 * @file snapshotoverlay.cpp
 * @brief 截图叠加浮窗实现：缩略图/编辑器/滑块/放置
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "snapshotoverlay.h"
#include "i18n.h"
#include "theme.h"

#include <QPainter>
#include <QPainterPath>
#include <QMouseEvent>
#include <QStyle>

SnapshotOverlay::SnapshotOverlay(QWidget *parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_Hover);
    setFixedSize(THUMB_W, THUMB_H);
    setCursor(Qt::PointingHandCursor);

    m_brightnessSlider = new QSlider(Qt::Horizontal, this);
    m_brightnessSlider->setRange(-50, 50);
    m_brightnessSlider->setValue(0);
    m_brightnessSlider->hide();

    m_contrastSlider = new QSlider(Qt::Horizontal, this);
    m_contrastSlider->setRange(-50, 50);
    m_contrastSlider->setValue(0);
    m_contrastSlider->hide();

    m_opacitySlider = new QSlider(Qt::Horizontal, this);
    m_opacitySlider->setRange(0, 100);
    m_opacitySlider->setValue(0);
    m_opacitySlider->hide();

    m_bValLabel = new QLabel("0", this);
    m_bValLabel->hide();
    m_cValLabel = new QLabel("0", this);
    m_cValLabel->hide();
    m_oValLabel = new QLabel("0", this);
    m_oValLabel->hide();

    m_placeBtn = new QPushButton(this);
    m_placeBtn->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    m_placeBtn->setToolTip(lang("放置叠加到视频", "Place overlay on video"));
    m_placeBtn->setFixedSize(26, 26);
    m_placeBtn->hide();

    m_clearBtn = new QPushButton(this);
    m_clearBtn->setIcon(style()->standardIcon(QStyle::SP_TrashIcon));
    m_clearBtn->setToolTip(lang("清除截图", "Clear snapshot"));
    m_clearBtn->setFixedSize(26, 26);
    m_clearBtn->hide();

    m_closeBtn = new QPushButton(this);
    m_closeBtn->setIcon(style()->standardIcon(QStyle::SP_DialogCloseButton));
    m_closeBtn->setToolTip(lang("关闭编辑器", "Close editor"));
    m_closeBtn->setFixedSize(26, 26);
    m_closeBtn->hide();

    QString sliderStyle =
        "QSlider::groove:horizontal { height: 3px; background: " + Theme::Border + "; border-radius: 1px; }"
        "QSlider::handle:horizontal { background: " + Theme::Accent + "; width: 10px; height: 10px; "
        "  margin: -4px 0; border-radius: 5px; }"
        "QSlider::sub-page:horizontal { background: " + Theme::Accent + "; border-radius: 1px; }";
    m_brightnessSlider->setStyleSheet(sliderStyle);
    m_contrastSlider->setStyleSheet(sliderStyle);
    m_opacitySlider->setStyleSheet(sliderStyle);

    QString labelStyle = "QLabel { color: #ccc; font-size: 9px; background: transparent; }";
    m_bValLabel->setStyleSheet(labelStyle);
    m_cValLabel->setStyleSheet(labelStyle);
    m_oValLabel->setStyleSheet(labelStyle);

    QString btnStyle =
        "QPushButton { border: none; background: transparent; padding: 2px; }"
        "QPushButton:hover { background: rgba(255,255,255,30); border-radius: 3px; }";
    m_placeBtn->setStyleSheet(btnStyle);
    m_clearBtn->setStyleSheet(btnStyle);
    m_closeBtn->setStyleSheet(btnStyle);

    // Brightness slider
    connect(m_brightnessSlider, &QSlider::valueChanged, this, [this](int v) {
        m_bValLabel->setText(QString::number(v));
        m_dirty = true;
        update();
        emit snapshotChanged();
    });
    // Contrast slider
    connect(m_contrastSlider, &QSlider::valueChanged, this, [this](int v) {
        m_cValLabel->setText(QString::number(v));
        m_dirty = true;
        update();
        emit snapshotChanged();
    });
    // Opacity slider
    connect(m_opacitySlider, &QSlider::valueChanged, this, [this](int v) {
        m_oValLabel->setText(QString::number(v));
        update();
        emit snapshotChanged();
    });

    connect(m_placeBtn, &QPushButton::clicked, this, [this]() {
        m_overlayActive = !m_overlayActive;
        m_placeBtn->setIcon(style()->standardIcon(
            m_overlayActive ? QStyle::SP_DialogDiscardButton : QStyle::SP_DialogApplyButton));
        emit placeToggled(m_overlayActive);
    });
    connect(m_clearBtn, &QPushButton::clicked, this, [this]() {
        m_overlayActive = false;
        m_placeBtn->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
        clearSnapshot();
        emit clearRequested();
    });
    connect(m_closeBtn, &QPushButton::clicked, this, [this]() {
        // Collapse editor back to thumbnail
        m_expanded = false;
        m_brightnessSlider->hide();
        m_contrastSlider->hide();
        m_opacitySlider->hide();
        m_bValLabel->hide();
        m_cValLabel->hide();
        m_oValLabel->hide();
        m_placeBtn->hide();
        m_clearBtn->hide();
        m_closeBtn->hide();
        setFixedSize(THUMB_W, THUMB_H);
        update();
    });
}

/// @brief 设置截图：存储图像/重置编辑器/定位到父控件左侧
void SnapshotOverlay::setSnapshot(const QImage &img)
{
    m_snapshot = img.copy();
    m_adjustedCache = QImage();
    m_dirty = true;
    m_expanded = false;
    m_overlayActive = false;
    m_placeBtn->setIcon(style()->standardIcon(QStyle::SP_DialogApplyButton));
    setFixedSize(THUMB_W, THUMB_H);

    // Hide editor controls initially
    m_brightnessSlider->hide();
    m_contrastSlider->hide();
    m_opacitySlider->hide();
    m_bValLabel->hide();
    m_cValLabel->hide();
    m_oValLabel->hide();
    m_placeBtn->hide();
    m_clearBtn->hide();
    m_closeBtn->hide();

    // Reset sliders
    m_brightnessSlider->setValue(0);
    m_contrastSlider->setValue(0);
    m_opacitySlider->setValue(0);

    // Position at left-center of parent
    if (parentWidget()) {
        int px = 8;
        int py = (parentWidget()->height() - height()) / 2;
        move(px, py);
    }

    show();
    raise();
    update();
}

void SnapshotOverlay::clearSnapshot()
{
    m_snapshot = QImage();
    m_adjustedCache = QImage();
    m_dirty = true;
    m_expanded = false;
    m_overlayActive = false;
    setFixedSize(THUMB_W, THUMB_H);

    m_brightnessSlider->hide();
    m_contrastSlider->hide();
    m_opacitySlider->hide();
    m_bValLabel->hide();
    m_cValLabel->hide();
    m_oValLabel->hide();
    m_placeBtn->hide();
    m_clearBtn->hide();
    m_closeBtn->hide();

    hide();
    update();
}

void SnapshotOverlay::setParameters(int brightness, int contrast, int opacity)
{
    m_brightnessSlider->blockSignals(true);
    m_contrastSlider->blockSignals(true);
    m_opacitySlider->blockSignals(true);
    m_brightnessSlider->setValue(brightness);
    m_contrastSlider->setValue(contrast);
    m_opacitySlider->setValue(opacity);
    m_bValLabel->setText(QString::number(brightness));
    m_cValLabel->setText(QString::number(contrast));
    m_oValLabel->setText(QString::number(opacity));
    m_brightnessSlider->blockSignals(false);
    m_contrastSlider->blockSignals(false);
    m_opacitySlider->blockSignals(false);
    m_dirty = true;
    update();
}

QImage SnapshotOverlay::applyAdjustments(const QImage &src) const
{
    if (src.isNull()) return src;

    int b = m_brightnessSlider->value();
    int c = m_contrastSlider->value();

    return applyBrightnessContrast(src, b, c);
}

void SnapshotOverlay::layoutControls()
{
    int y = 176;
    int sliderX = 52;
    int sliderW = 140;
    int valueX = 196;
    int valueW = 36;
    int rowH = 20;
    int gap = 6;

    m_brightnessSlider->setGeometry(sliderX, y, sliderW, rowH);
    m_bValLabel->setGeometry(valueX, y, valueW, rowH);

    m_contrastSlider->setGeometry(sliderX, y + rowH + gap, sliderW, rowH);
    m_cValLabel->setGeometry(valueX, y + rowH + gap, valueW, rowH);

    m_opacitySlider->setGeometry(sliderX, y + (rowH + gap) * 2, sliderW, rowH);
    m_oValLabel->setGeometry(valueX, y + (rowH + gap) * 2, valueW, rowH);

    int btnY = EDITOR_H - 34;
    m_placeBtn->setGeometry(EDITOR_W - 90, btnY, 26, 26);
    m_clearBtn->setGeometry(EDITOR_W - 58, btnY, 26, 26);
    m_closeBtn->setGeometry(EDITOR_W - 26, btnY, 26, 26);
}

/// @brief 绘制缩略图或编辑器预览
void SnapshotOverlay::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    if (m_snapshot.isNull())
        return;

    if (m_expanded) {
        // Editor mode: dark background
        QPainterPath bg;
        bg.addRoundedRect(rect(), 6, 6);
        p.fillPath(bg, QColor(30, 30, 30, 230));
        p.setPen(QColor(70, 70, 70));
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 6, 6);

        // Preview image with brightness+contrast applied (no opacity)
        QRect previewRect(8, 8, EDITOR_W - 16, 160);
        if (m_dirty || m_adjustedCache.isNull()) {
            m_adjustedCache = applyAdjustments(m_snapshot);
            m_dirty = false;
        }
        QImage scaled = m_adjustedCache.scaled(previewRect.size(),
                                                Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation);
        QRect imageRect(previewRect.center().x() - scaled.width() / 2,
                       previewRect.center().y() - scaled.height() / 2,
                       scaled.width(), scaled.height());
        p.drawImage(imageRect, scaled);

        // Labels
        int y = 176;
        int labelX = 10;
        int labelW = 40;
        int rowH = 20;
        int gap = 6;

        p.setPen(QColor(180, 180, 180));
        p.setFont(fontSans(8));
        p.drawText(labelX, y, labelW, rowH, Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("\u4eae  \u5ea6"));
        p.drawText(labelX, y + rowH + gap, labelW, rowH, Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("\u5bf9\u6bd4\u5ea6"));
        p.drawText(labelX, y + (rowH + gap) * 2, labelW, rowH, Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("\u900f\u660e\u5ea6"));

        p.setPen(QColor(120, 120, 120));
        p.drawText(234, y + (rowH + gap) * 2, 14, rowH, Qt::AlignVCenter | Qt::AlignLeft, "%");

    } else {
        // Thumbnail mode
        QPainterPath border;
        border.addRoundedRect(rect(), 4, 4);
        p.fillPath(border, QColor(0, 0, 0, 160));
        p.setPen(QColor(0, 188, 212, 180));
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 4, 4);

        QImage scaled = m_snapshot.scaled(THUMB_W - 8, THUMB_H - 8,
                                          Qt::KeepAspectRatio,
                                          Qt::SmoothTransformation);
        int cx = (THUMB_W - scaled.width()) / 2;
        int cy = (THUMB_H - scaled.height()) / 2;
        p.drawImage(QRect(cx, cy, scaled.width(), scaled.height()), scaled);

        // Small click hint
        p.setPen(QColor(0, 188, 212, 120));
        p.setFont(fontSans(7));
        p.drawText(rect().adjusted(0, 0, 0, -2), Qt::AlignBottom | Qt::AlignHCenter, "\u25b6 Edit");
    }
}

void SnapshotOverlay::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && !m_snapshot.isNull()) {
        if (!m_expanded) {
            // Expand thumbnail to editor
            m_expanded = true;
            m_dirty = true;
            setFixedSize(EDITOR_W, EDITOR_H);
            m_brightnessSlider->show();
            m_contrastSlider->show();
            m_opacitySlider->show();
            m_bValLabel->show();
            m_cValLabel->show();
            m_oValLabel->show();
            m_placeBtn->show();
            m_clearBtn->show();
            m_closeBtn->show();
            layoutControls();
            update();
        } else {
            // In editor mode: check if click is on the preview image area
            QRect previewRect(8, 8, EDITOR_W - 16, 160);
            if (previewRect.contains(event->pos())) {
                // Toggle back to thumbnail mode
                m_expanded = false;
                m_brightnessSlider->hide();
                m_contrastSlider->hide();
                m_opacitySlider->hide();
                m_bValLabel->hide();
                m_cValLabel->hide();
                m_oValLabel->hide();
                m_placeBtn->hide();
                m_clearBtn->hide();
                m_closeBtn->hide();
                setFixedSize(THUMB_W, THUMB_H);
                update();
            } else {
                // Start drag (on non-preview area)
                m_dragOffset = event->pos();
            }
        }
    }
    QWidget::mousePressEvent(event);
}

void SnapshotOverlay::mouseMoveEvent(QMouseEvent *event)
{
    if ((event->buttons() & Qt::LeftButton) && m_expanded) {
        move(pos() + event->pos() - m_dragOffset);
    }
    QWidget::mouseMoveEvent(event);
}

void SnapshotOverlay::enterEvent(QEnterEvent *)
{
    if (!m_snapshot.isNull() && !m_expanded) {
        update();
    }
}

void SnapshotOverlay::leaveEvent(QEvent *)
{
    if (!m_expanded) {
        update();
    }
}

void SnapshotOverlay::resizeEvent(QResizeEvent *)
{
    if (m_expanded)
        layoutControls();
}
