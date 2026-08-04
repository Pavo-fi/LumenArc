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

void PinnedWidget::setVideoSize(int width, int height)
{
    m_videoSize = QSize(qMax(1, width), qMax(1, height));
}

void PinnedWidget::setPinnedImage(const QImage &fullFrame, const QRect &videoRect)
{
    // scrub 拖拽期间引擎输出降采样预览帧（宽 ≤1280），尺寸 ≠ 原生分辨率：
    // 视频坐标矩形必须按比例换算到帧坐标，否则裁剪区域漂移/为空（黑屏）。
    QRect src = videoRect;
    if (!m_videoSize.isEmpty() && fullFrame.size() != m_videoSize) {
        const qreal sx = qreal(fullFrame.width()) / m_videoSize.width();
        const qreal sy = qreal(fullFrame.height()) / m_videoSize.height();
        src = QRect(qRound64(videoRect.x() * sx),
                    qRound64(videoRect.y() * sy),
                    qMax(1, qRound64(videoRect.width() * sx)),
                    qMax(1, qRound64(videoRect.height() * sy)));
    }
    src = src.intersected(fullFrame.rect());
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
