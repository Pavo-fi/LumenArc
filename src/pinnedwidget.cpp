/**
 * @file pinnedwidget.cpp
 * @brief 固定时间戳浮动窗口实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "pinnedwidget.h"
#include <QPainter>
#include <QResizeEvent>

PinnedWidget::PinnedWidget(QWidget *parent)
    : QWidget(parent, Qt::Tool | Qt::WindowStaysOnTopHint)
{
    setMinimumSize(60, 20);
    setWindowTitle("Pinned");
    setVisible(false);
}

void PinnedWidget::setPinnedImage(const QImage &fullFrame, const QRect &videoRect)
{
    QRect src = videoRect.intersected(fullFrame.rect());
    if (src.isEmpty()) {
        clear();
        return;
    }

    m_pinnedImage = fullFrame.copy(src);
    // Scale to fit typical timestamp size, keeping aspect ratio
    int h = 30;
    int w = src.width() * h / qMax(1, src.height());
    if (w < 60) w = 60;
    resize(w, h);
    setVisible(true);
    update();
}

void PinnedWidget::clear()
{
    m_pinnedImage = QImage();
    setVisible(false);
}

void PinnedWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(0, 0, 0, 180));
    if (!m_pinnedImage.isNull())
        painter.drawImage(rect(), m_pinnedImage);
}
