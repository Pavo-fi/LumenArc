/**
 * @file camtilewidget.cpp
 * @brief 多机同步播放瓦片实现（P-57 N-7）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-18
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "camtilewidget.h"

#include <QPainter>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>

#include "infrastructure/ivideo_engine.h"
#include "theme.h"

CamTileWidget::CamTileWidget(QWidget *parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
    setMinimumSize(240, 160);
    setMouseTracking(false);
    setFocusPolicy(Qt::NoFocus);
    setCursor(Qt::PointingHandCursor);
}

void CamTileWidget::setEngine(IVideoEngine *engine)
{
    if (m_engine)
        disconnect(m_engine, nullptr, this, nullptr);
    m_engine = engine;
    if (m_engine) {
        connect(m_engine, &IVideoEngine::frameReady, this,
                [this](const QImage &img) {
            m_frame = img;            // COW 浅拷贝；引擎发出后不再改
            if (m_engine)
                m_engine->ackFrame(); // 归还配额（引擎有界化丢帧契约）
            update();
        });
    }
}

void CamTileWidget::clearFrame()
{
    m_frame = QImage();
    update();
}

void CamTileWidget::setOsdLines(const QString &line1, const QString &line2)
{
    if (m_osd1 == line1 && m_osd2 == line2)
        return;
    m_osd1 = line1;
    m_osd2 = line2;
    update();
}

void CamTileWidget::setOsdVisible(bool on)
{
    if (m_osdVisible == on)
        return;
    m_osdVisible = on;
    update();
}

void CamTileWidget::setAudible(bool on)
{
    if (m_audible == on)
        return;
    m_audible = on;
    update();
}

void CamTileWidget::setTemporaryBadge(bool on)
{
    if (m_tempBadge == on)
        return;
    m_tempBadge = on;
    update();
}

void CamTileWidget::setLowresBadge(bool on)
{
    if (m_lowresBadge == on)
        return;
    m_lowresBadge = on;
    update();
}

void CamTileWidget::setPlaceholder(const QString &text)
{
    if (m_placeholder == text)
        return;
    m_placeholder = text;
    update();
}

// ---------------------------------------------------------------------------
// 放大镜几何（归一化源坐标：可视半宽 = 0.5/zoom）
// ---------------------------------------------------------------------------
QRectF CamTileWidget::frameFitRect() const
{
    if (m_frame.isNull())
        return rect();
    const QSizeF fs = m_frame.size();
    const QSizeF ws = size();
    const qreal s = qMin(ws.width() / fs.width(), ws.height() / fs.height());
    const QSizeF fit(fs.width() * s, fs.height() * s);
    return QRectF((ws.width() - fit.width()) / 2.0,
                  (ws.height() - fit.height()) / 2.0,
                  fit.width(), fit.height());
}

void CamTileWidget::clampCenter()
{
    if (m_zoom <= 1.0) {
        m_zoom = 1.0;
        m_center = QPointF(0.5, 0.5);
        return;
    }
    const qreal half = 0.5 / m_zoom;
    m_center.setX(qBound(half, m_center.x(), 1.0 - half));
    m_center.setY(qBound(half, m_center.y(), 1.0 - half));
}

void CamTileWidget::wheelEvent(QWheelEvent *event)
{
    if (m_frame.isNull())
        return;
    // 以指针位置为中心缩放：缩放前后指针下的源点保持不动
    const QRectF fit = frameFitRect();
    if (!fit.contains(event->position())) {
        event->ignore();
        return;
    }
    const QPointF c((event->position().x() - fit.left()) / fit.width(),
                    (event->position().y() - fit.top()) / fit.height());
    const qreal oldZoom = m_zoom;
    const int steps = event->angleDelta().y() / 120;
    if (steps == 0)
        return;
    const qreal newZoom = qBound<qreal>(1.0, oldZoom * qPow(1.25, steps), 8.0);
    if (qFuzzyCompare(newZoom, oldZoom))
        return;
    // p = center + (c - 0.5)/oldZoom；令 p 在新缩放下仍在 c 处
    const QPointF p = m_center + (c - QPointF(0.5, 0.5)) / oldZoom;
    m_center = p - (c - QPointF(0.5, 0.5)) / newZoom;
    m_zoom = newZoom;
    clampCenter();
    update();
    event->accept();
}

void CamTileWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton && m_zoom > 1.0) {
        m_panning = true;
        m_panLast = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
        return;
    }
    QWidget::mousePressEvent(event);
}

void CamTileWidget::mouseMoveEvent(QMouseEvent *event)
{
    if (m_panning) {
        const QRectF fit = frameFitRect();
        const QPointF d = event->pos() - m_panLast;
        m_panLast = event->pos();
        m_center -= QPointF(d.x() / (fit.width() * m_zoom),
                            d.y() / (fit.height() * m_zoom));
        clampCenter();
        update();
        event->accept();
        return;
    }
    QWidget::mouseMoveEvent(event);
}

void CamTileWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton && m_panning) {
        m_panning = false;
        setCursor(Qt::PointingHandCursor);
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        emit clicked();   // 切听（U-2）
        event->accept();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void CamTileWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        emit openRequested();   // 回单路分析（U-6）
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

// ---------------------------------------------------------------------------
// 绘制
// ---------------------------------------------------------------------------
void CamTileWidget::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.fillRect(rect(), QColor(Theme::BgPanel));

    if (!m_frame.isNull()) {
        const QRectF fit = frameFitRect();
        if (m_zoom <= 1.0) {
            p.drawImage(fit, m_frame);
        } else {
            // 渲染侧裁剪放大：源矩形 = 归一化中心 ± 半宽
            const qreal halfW = 0.5 / m_zoom, halfH = 0.5 / m_zoom;
            const QRectF srcNdc(m_center.x() - halfW, m_center.y() - halfH,
                                halfW * 2.0, halfH * 2.0);
            const QRectF src(srcNdc.x() * m_frame.width(),
                             srcNdc.y() * m_frame.height(),
                             srcNdc.width() * m_frame.width(),
                             srcNdc.height() * m_frame.height());
            p.drawImage(fit, m_frame, src);
            // 放大时外框提示倍率
            p.setPen(QPen(QColor(Theme::Accent), 2));
            p.drawRect(fit.adjusted(1, 1, -1, -1));
        }
    } else {
        p.setPen(QColor(Theme::TextMuted));
        p.drawText(rect(), Qt::AlignCenter, m_name);
    }

    // 缺口/失败占位：暗纹 + 文案（保留末帧垫底）
    if (!m_placeholder.isEmpty()) {
        QColor veil(10, 12, 16, 190);
        p.setPen(Qt::NoPen);
        p.setBrush(veil);
        p.drawRect(rect());
        p.setPen(QColor(Theme::TextSecond));
        QFont f = p.font();
        f.setPixelSize(14);
        p.setFont(f);
        p.drawText(rect(), Qt::AlignCenter, m_placeholder);
    }

    // OSD（U-5 可关）
    if (m_osdVisible) {
        QFont f = p.font();
        f.setPixelSize(12);
        p.setFont(f);
        // 底部：路名 + 两行时间
        const int lh = 18;
        QRect base(8, height() - 8 - lh * 2, width() - 16, lh * 2);
        QColor bg(0, 0, 0, 120);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(base.adjusted(-4, -2, 4, 2), 4, 4);
        p.setPen(QColor(0xF5, 0xF0, 0xE8));
        p.drawText(base.adjusted(6, 0, 0, -lh), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("%1  %2").arg(m_name, m_osd1));
        if (!m_osd2.isEmpty())
            p.drawText(base.adjusted(6, lh, 0, 0), Qt::AlignVCenter | Qt::AlignLeft,
                       m_osd2);

        // 右上：发声路 🔊；左上：临时/降清角标
        if (m_audible) {
            f.setPixelSize(16);
            p.setFont(f);
            p.drawText(QRect(width() - 30, 4, 26, 24), Qt::AlignCenter,
                       QStringLiteral("🔊"));
            f.setPixelSize(12);
            p.setFont(f);
        }
        int badgeY = 6;
        auto badge = [&](const QString &text, const QColor &color) {
            f.setBold(true);
            p.setFont(f);
            const int w = p.fontMetrics().horizontalAdvance(text) + 12;
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(color.red(), color.green(), color.blue(), 150));
            p.drawRoundedRect(QRect(6, badgeY, w, 20), 4, 4);
            p.setPen(QColor(0xF5, 0xF0, 0xE8));
            p.drawText(QRect(6, badgeY, w, 20), Qt::AlignCenter, text);
            f.setBold(false);
            p.setFont(f);
            badgeY += 24;
        };
        if (m_tempBadge)
            badge(QStringLiteral("临时对齐"), QColor(Theme::Accent));
        if (m_lowresBadge)
            badge(QStringLiteral("预览降清档"), QColor(Theme::TextMuted));
        if (m_zoom > 1.0)
            badge(QStringLiteral("×%1").arg(m_zoom, 0, 'f', 1),
                  QColor(Theme::Accent));
    }
}
