/**
 * @file videowidget.cpp
 * @brief 视频渲染 + OverlayWidget ROI 交互实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "videowidget.h"
#include "domain/region_model.h"
#include "domain/polygon_model.h"
#include "domain/guide_line_model.h"
#include "infrastructure/ivideo_engine.h"
#include "i18n.h"
#include "theme.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QApplication>
#include <QWheelEvent>
#include <QVBoxLayout>
#include <QDebug>
#include <climits>

// =============================================================================
// OverlayWidget
// =============================================================================

OverlayWidget::OverlayWidget(QWidget *parent)
    : QWidget(parent)
{
    setFocusPolicy(Qt::StrongFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_TransparentForMouseEvents, false);
    setAttribute(Qt::WA_NoSystemBackground, true);
    setAttribute(Qt::WA_OpaquePaintEvent, false);
}

void OverlayWidget::setRegionModel(RegionModel *model)
{
    m_regionModel = model;
}

void OverlayWidget::setPolygonModel(PolygonModel *model)
{
    m_polygonModel = model;
}

void OverlayWidget::setGuideLineModel(GuideLineModel *model)
{
    m_guideLineModel = model;
}

void OverlayWidget::setPolygonMode(bool enabled)
{
    m_polygonMode = enabled;
    if (enabled) {
        m_guideLineMode = false;
        m_drawingGuideLine = false;
        m_polygonPoints.clear();
        m_dragMode = DragMode::None;
        emit modeChanged(lang("多边形模式", "Polygon Mode"));
    } else {
        m_polygonPoints.clear();
        if (m_dragMode == DragMode::CreatePolygon) {
            m_dragMode = DragMode::None;
        }
        emit modeChanged(lang("矩形模式", "Rect Mode"));
    }
    update();
}

void OverlayWidget::setGuideLineMode(bool enabled)
{
    m_guideLineMode = enabled;
    if (enabled) {
        m_polygonMode = false;
        m_polygonPoints.clear();
        m_drawingGuideLine = false;
        m_hoveredGuideLine = -1;
        m_dragMode = DragMode::None;
        emit modeChanged(lang("辅助线模式", "Guide Line Mode"));
    } else {
        m_drawingGuideLine = false;
        m_hoveredGuideLine = -1;
        if (m_dragMode == DragMode::DrawGuideLine
            || m_dragMode == DragMode::MoveGuideLine
            || m_dragMode == DragMode::ResizeGuideEndpoint) {
            m_dragMode = DragMode::None;
        }
        emit modeChanged(lang("矩形模式", "Rect Mode"));
    }
    update();
}

void OverlayWidget::setVideoSize(int width, int height)
{
    m_videoWidth = qMax(1, width);
    m_videoHeight = qMax(1, height);
}

void OverlayWidget::setVideoDisplayRect(const QRect &rect)
{
    m_videoDisplayRect = rect;
}

void OverlayWidget::setVideoOriginOffset(const QPoint &offset)
{
    m_videoOriginOffset = offset;
}

void OverlayWidget::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    drawRegions(painter);
    drawPolygons(painter);
    drawGuideLines(painter);
}

void OverlayWidget::drawRegions(QPainter &painter)
{
    if (!m_regionModel)
        return;

    QVector<QRect> regions = m_regionModel->regions();
    for (int i = 0; i < regions.size(); ++i) {
        QRect rc = mapFromVideo(regions[i]);
        bool selected = (i == m_selectedRegion);
        QColor baseColor = RegionModel::regionColor(i);

        QColor fillColor = baseColor;
        fillColor.setAlpha(selected ? 40 : 20);
        painter.fillRect(rc, fillColor);

        QPen pen(baseColor);
        pen.setWidth(1);
        painter.setPen(pen);
        painter.drawRect(rc);

        painter.setPen(Qt::white);
        painter.drawText(rc.topLeft() + QPoint(4, 14), QString("R%1").arg(i + 1));

        if (selected) {
            drawResizeHandles(painter, rc);
        }
    }

    // Draw in-progress selection rectangle (m_newRect is in video coords)
    if (m_newRect.isValid()) {
        QRect widgetRc = mapFromVideo(m_newRect);
        if (m_dragMode == DragMode::CreateNew) {
            QPen pen(Qt::white, 2, Qt::DashLine);
            painter.setPen(pen);
            painter.fillRect(widgetRc, QColor(255, 255, 255, 40));
            painter.drawRect(widgetRc);
        } else if (m_dragMode == DragMode::PinSelect) {
            QPen pen(QColor(255, 200, 0), 2, Qt::DashLine);
            painter.setPen(pen);
            painter.drawRect(widgetRc);
        }
    }
}

void OverlayWidget::drawPolygons(QPainter &painter)
{
    if (!m_polygonModel)
        return;

    QVector<QPolygon> polygons = m_polygonModel->polygons();
    for (int i = 0; i < polygons.size(); ++i) {
        QPolygon widgetPoly;
        for (const QPoint &pt : polygons[i]) {
            widgetPoly.append(mapFromVideo(pt));
        }

        bool selected = (i == m_selectedPolygon);
        QColor baseColor = PolygonModel::polygonColor(i);

        QColor fillColor = baseColor;
        fillColor.setAlpha(selected ? 40 : 20);
        painter.setBrush(fillColor);

        QPen pen(baseColor);
        pen.setWidth(1);
        painter.setPen(pen);
        painter.drawPolygon(widgetPoly);

        // 标签
        painter.setPen(Qt::white);
        painter.drawText(widgetPoly.boundingRect().topLeft() + QPoint(4, 14),
                         QString("P%1").arg(i + 1));

        if (selected) {
            drawPolygonHandles(painter, widgetPoly);
        }
    }

    // 正在绘制的多边形预览
    if (m_polygonMode && m_polygonPoints.size() > 0) {
        QPen pen(Qt::white, 2, Qt::DashLine);
        painter.setPen(pen);
        painter.setBrush(QColor(255, 255, 255, 40));

        // 绘制已有的边
        for (int i = 0; i < m_polygonPoints.size() - 1; ++i) {
            QPoint p1 = mapFromVideo(m_polygonPoints[i]);
            QPoint p2 = mapFromVideo(m_polygonPoints[i + 1]);
            painter.drawLine(p1, p2);
        }

        // 绘制从最后一个点到当前鼠标的预览线
        if (!m_polygonPoints.isEmpty()) {
            QPoint lastPt = mapFromVideo(m_polygonPoints.last());
            painter.drawLine(lastPt, m_currentMousePos);
        }

        // 绘制顶点
        painter.setBrush(Qt::white);
        for (const QPoint &pt : m_polygonPoints) {
            QPoint widgetPt = mapFromVideo(pt);
            painter.drawEllipse(widgetPt, 3, 3);
        }
    }
}

void OverlayWidget::drawPolygonHandles(QPainter &painter, const QPolygon &polygon)
{
    painter.save();
    painter.setBrush(Qt::white);
    painter.setPen(QPen(Qt::black, 1));

    for (int i = 0; i < polygon.size(); ++i) {
        QRect handle(polygon[i].x() - HANDLE_SIZE / 2,
                     polygon[i].y() - HANDLE_SIZE / 2,
                     HANDLE_SIZE, HANDLE_SIZE);
        painter.drawRect(handle);
    }
    painter.restore();
}

int OverlayWidget::hitTestPolygonVertex(const QPoint &pos, int *polygonIndex) const
{
    if (!m_polygonModel)
        return -1;

    QVector<QPolygon> polygons = m_polygonModel->polygons();
    for (int pi = polygons.size() - 1; pi >= 0; --pi) {
        QPolygon widgetPoly;
        for (const QPoint &pt : polygons[pi])
            widgetPoly.append(mapFromVideo(pt));
        for (int vi = 0; vi < widgetPoly.size(); ++vi) {
            int dx = pos.x() - widgetPoly[vi].x();
            int dy = pos.y() - widgetPoly[vi].y();
            if (dx * dx + dy * dy <= HANDLE_HIT_RADIUS * HANDLE_HIT_RADIUS) {
                if (polygonIndex) *polygonIndex = pi;
                return vi;
            }
        }
    }
    return -1;
}

void OverlayWidget::drawGuideLines(QPainter &painter)
{
    if (!m_guideLineModel)
        return;

    QVector<GuideLine> lines = m_guideLineModel->lines();
    for (int i = 0; i < lines.size(); ++i) {
        QPoint start = mapFromVideo(lines[i].start);
        QPoint end = mapFromVideo(lines[i].end);

        bool selected = (i == m_selectedGuideLine);
        bool hovered = (i == m_hoveredGuideLine && !selected);
        QPen pen(lines[i].color);
        if (selected) {
            pen.setWidth(2);
        } else if (hovered) {
            pen.setWidth(2);
            pen.setColor(lines[i].color.lighter(140));
        } else {
            pen.setWidth(1);
        }
        pen.setStyle(Qt::DashLine);
        painter.setPen(pen);
        painter.drawLine(start, end);

        // 选中或悬停时显示端点手柄
        if (selected || hovered) {
            painter.setBrush(Qt::white);
            painter.setPen(QPen(Qt::black, 1));
            painter.drawRect(QRect(start - QPoint(3, 3), QSize(6, 6)));
            painter.drawRect(QRect(end - QPoint(3, 3), QSize(6, 6)));
        }
    }

    // 正在画的预览线
    if (m_drawingGuideLine) {
        QPen pen(QColor(255, 255, 0, 128), 1, Qt::DashLine);
        painter.setPen(pen);
        QPoint start = mapFromVideo(m_guideLineStart);
        painter.drawLine(start, m_currentMousePos);
    }
}

void OverlayWidget::drawResizeHandles(QPainter &painter, const QRect &rect)
{
    painter.save();
    painter.setBrush(Qt::white);
    painter.setPen(QPen(Qt::black, 1));

    QPoint centers[4] = {
        rect.topLeft(), rect.topRight(),
        rect.bottomRight(), rect.bottomLeft()
    };

    for (int i = 0; i < 4; ++i) {
        QRect handle(centers[i].x() - HANDLE_SIZE / 2,
                     centers[i].y() - HANDLE_SIZE / 2,
                     HANDLE_SIZE, HANDLE_SIZE);
        painter.drawRect(handle);
    }
    painter.restore();
}

int OverlayWidget::hitTestHandle(const QPoint &pos, const QRect &rect) const
{
    QPoint centers[4] = {
        rect.topLeft(), rect.topRight(),
        rect.bottomRight(), rect.bottomLeft()
    };
    for (int i = 0; i < 4; ++i) {
        if (QLineF(pos, centers[i]).length() <= HANDLE_HIT_RADIUS)
            return i;
    }
    return -1;
}

QRect OverlayWidget::clampRectToVideo(const QRect &rect) const
{
    int x = qMax(m_videoOriginOffset.x(), rect.left());
    int y = qMax(m_videoOriginOffset.y(), rect.top());
    int r = qMin(m_videoWidth - 1 + m_videoOriginOffset.x(), rect.right());
    int b = qMin(m_videoHeight - 1 + m_videoOriginOffset.y(), rect.bottom());
    if (r < x) r = x;
    if (b < y) b = y;
    return QRect(QPoint(x, y), QPoint(r, b)).normalized();
}

QPoint OverlayWidget::clampPointToVideo(const QPoint &pt) const
{
    int x = qBound(m_videoOriginOffset.x(), pt.x(), m_videoWidth - 1 + m_videoOriginOffset.x());
    int y = qBound(m_videoOriginOffset.y(), pt.y(), m_videoHeight - 1 + m_videoOriginOffset.y());
    return QPoint(x, y);
}

int OverlayWidget::hitTestGuideEndpoint(const QPoint &pos, int *lineIndex) const
{
    if (!m_guideLineModel) return -1;
    QVector<GuideLine> lines = m_guideLineModel->lines();
    for (int i = 0; i < lines.size(); ++i) {
        QPoint start = mapFromVideo(lines[i].start);
        QPoint end = mapFromVideo(lines[i].end);
        if (QLineF(pos, start).length() <= HANDLE_HIT_RADIUS) {
            if (lineIndex) *lineIndex = i;
            return 0;
        }
        if (QLineF(pos, end).length() <= HANDLE_HIT_RADIUS) {
            if (lineIndex) *lineIndex = i;
            return 1;
        }
    }
    return -1;
}

int OverlayWidget::hitTestGuideLine(const QPoint &pos) const
{
    if (!m_guideLineModel) return -1;
    QVector<GuideLine> lines = m_guideLineModel->lines();
    for (int i = lines.size() - 1; i >= 0; --i) {
        QPoint start = mapFromVideo(lines[i].start);
        QPoint end = mapFromVideo(lines[i].end);
        QLineF line(start, end);
        qreal len = line.length();
        if (len < 1) continue;
        qreal t = QLineF(start, pos).dx() * line.dx() + QLineF(start, pos).dy() * line.dy();
        t /= (len * len);
        t = qBound(0.0, t, 1.0);
        QPointF closest = start + t * (end - start);
        qreal dist = QLineF(pos, closest.toPoint()).length();
        if (dist < 5) return i;
    }
    return -1;
}

QPoint OverlayWidget::mapToVideo(const QPoint &widgetPos) const
{
    if (m_videoWidth <= 0 || m_videoHeight <= 0 || m_videoDisplayRect.isEmpty())
        return widgetPos;
    int x = (widgetPos.x() - m_videoDisplayRect.left()) * m_videoWidth / m_videoDisplayRect.width();
    int y = (widgetPos.y() - m_videoDisplayRect.top()) * m_videoHeight / m_videoDisplayRect.height();
    return QPoint(x + m_videoOriginOffset.x(), y + m_videoOriginOffset.y());
}

QPoint OverlayWidget::mapFromVideo(const QPoint &videoPos) const
{
    if (m_videoWidth <= 0 || m_videoHeight <= 0 || m_videoDisplayRect.isEmpty())
        return videoPos;
    int x = videoPos.x() - m_videoOriginOffset.x();
    int y = videoPos.y() - m_videoOriginOffset.y();
    x = m_videoDisplayRect.left() + x * m_videoDisplayRect.width() / m_videoWidth;
    y = m_videoDisplayRect.top() + y * m_videoDisplayRect.height() / m_videoHeight;
    return QPoint(x, y);
}

QRect OverlayWidget::mapToVideo(const QRect &widgetRect) const
{
    return QRect(mapToVideo(widgetRect.topLeft()), mapToVideo(widgetRect.bottomRight())).normalized();
}

QRect OverlayWidget::mapFromVideo(const QRect &videoRect) const
{
    return QRect(mapFromVideo(videoRect.topLeft()), mapFromVideo(videoRect.bottomRight())).normalized();
}

/// @brief 鼠标按下：区域创建/选中/调整大小/pin选择/中键平移放大镜
void OverlayWidget::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        m_dragMode = DragMode::MagnifierPan;
        m_dragStart = event->pos();
        event->accept();
        return;
    }

    if (event->button() == Qt::LeftButton) {
        QPoint pos = event->pos();
        QPoint videoPos = mapToVideo(pos);
        bool ctrl = (event->modifiers() & Qt::ControlModifier);
        bool shift = (event->modifiers() & Qt::ShiftModifier);

        // 辅助线模式
        if (m_guideLineMode) {
            // 1) 端点命中 → 拖动端点
            int epLineIdx = -1;
            int epIdx = hitTestGuideEndpoint(pos, &epLineIdx);
            if (epIdx >= 0) {
                m_dragMode = DragMode::ResizeGuideEndpoint;
                m_selectedGuideLine = epLineIdx;
                m_guideEndpointIndex = epIdx;
                m_dragOriginalLine = m_guideLineModel->lines()[epLineIdx];
                m_dragStart = videoPos;
                setCursor(Qt::ClosedHandCursor);
                update();
                return;
            }
            // 2) 线段命中 → 平移整条线
            int lineIdx = hitTestGuideLine(pos);
            if (lineIdx >= 0) {
                m_dragMode = DragMode::MoveGuideLine;
                m_selectedGuideLine = lineIdx;
                m_guideEndpointIndex = -1;
                m_dragOriginalLine = m_guideLineModel->lines()[lineIdx];
                m_dragStart = videoPos;
                setCursor(Qt::ClosedHandCursor);
                update();
                return;
            }
            // 3) 无命中 → 画新线
            m_dragMode = DragMode::DrawGuideLine;
            m_guideLineStart = videoPos;
            m_drawingGuideLine = true;
            m_currentMousePos = pos;
            update();
            return;
        }

        // 多边形模式
        if (m_polygonMode) {
            // 单击：添加点
            if (m_dragMode != DragMode::CreatePolygon) {
                m_dragMode = DragMode::CreatePolygon;
                m_polygonPoints.clear();
            }
            m_polygonPoints.append(videoPos);
            update();
            return;
        }

        // 矩形模式（默认）
        if (!m_regionModel)
            return;

        // Ctrl+Shift: pin timestamp selection
        if (ctrl && shift) {
            m_dragMode = DragMode::PinSelect;
            m_dragStart = videoPos;
            m_newRect = QRect(m_dragStart, QSize(0, 0));
            update();
            return;
        }

        // Check handles of selected region first
        if (m_selectedRegion >= 0) {
            QVector<QRect> regions = m_regionModel->regions();
            if (m_selectedRegion < regions.size()) {
                QRect widgetRect = mapFromVideo(regions[m_selectedRegion]);
                int handle = hitTestHandle(pos, widgetRect);
                if (handle >= 0) {
                    m_dragMode = DragMode::ResizeHandle;
                    m_activeHandle = {m_selectedRegion, handle};
                    m_dragStart = videoPos;
                    m_dragOriginalRect = regions[m_selectedRegion];
                    return;
                }
            }
        }

        // Check region bodies (convert video rects to widget coords for hit testing)
        QVector<QRect> regions = m_regionModel->regions();
        int hit = -1;
        for (int i = regions.size() - 1; i >= 0; --i) {
            if (mapFromVideo(regions[i]).contains(pos)) {
                hit = i;
                break;
            }
        }
        if (hit >= 0) {
            m_selectedRegion = hit;
            m_selectedPolygon = -1;
            m_selectedGuideLine = -1;
            m_dragMode = DragMode::MoveRect;
            m_dragStart = videoPos;
            m_dragOriginalRect = m_regionModel->regions()[hit];
            update();
            return;
        }

        // Check polygon vertices of selected polygon
        if (m_selectedPolygon >= 0 && m_polygonModel) {
            QVector<QPolygon> polygons = m_polygonModel->polygons();
            if (m_selectedPolygon < polygons.size()) {
                QPolygon widgetPoly;
                for (const QPoint &pt : polygons[m_selectedPolygon])
                    widgetPoly.append(mapFromVideo(pt));
                for (int vi = 0; vi < widgetPoly.size(); ++vi) {
                    int dx = pos.x() - widgetPoly[vi].x();
                    int dy = pos.y() - widgetPoly[vi].y();
                    if (dx * dx + dy * dy <= HANDLE_HIT_RADIUS * HANDLE_HIT_RADIUS) {
                        m_dragMode = DragMode::ResizePolygonVertex;
                        m_dragPolygonVertexIndex = vi;
                        m_dragOriginalPolygon = polygons[m_selectedPolygon];
                        m_dragStart = videoPos;
                        return;
                    }
                }
            }
        }

        // Check polygon bodies
        if (m_polygonModel) {
            QVector<QPolygon> polygons = m_polygonModel->polygons();
            for (int i = polygons.size() - 1; i >= 0; --i) {
                QPolygon widgetPoly;
                for (const QPoint &pt : polygons[i]) {
                    widgetPoly.append(mapFromVideo(pt));
                }
                if (widgetPoly.containsPoint(pos, Qt::OddEvenFill)) {
                    m_selectedPolygon = i;
                    m_selectedRegion = -1;
                    m_selectedGuideLine = -1;
                    m_dragMode = DragMode::MovePolygon;
                    m_dragStart = videoPos;
                    m_dragOriginalPolygon = polygons[i];
                    update();
                    return;
                }
            }
        }

        // No hit: deselect all
        m_selectedRegion = -1;
        m_selectedPolygon = -1;
        m_selectedGuideLine = -1;

        // Start creating new region
        m_dragMode = DragMode::CreateNew;
        m_dragStart = videoPos;
        m_newRect = QRect(m_dragStart, QSize(0, 0));
        update();
    }

    // 右键：取消操作或删除
    if (event->button() == Qt::RightButton) {
        // 多边形绘制中：取消绘制
        if (m_dragMode == DragMode::CreatePolygon) {
            m_polygonPoints.clear();
            m_dragMode = DragMode::None;
            update();
            m_contextMenuSuppressed = true;
            event->accept();
            return;
        }

        // 矩形拖拽中：取消创建
        if (m_dragMode == DragMode::CreateNew) {
            m_newRect = QRect();
            m_dragMode = DragMode::None;
            update();
            m_contextMenuSuppressed = true;
            event->accept();
            return;
        }

        // 辅助线绘制/拖拽中：取消
        if (m_dragMode == DragMode::DrawGuideLine
            || m_dragMode == DragMode::MoveGuideLine
            || m_dragMode == DragMode::ResizeGuideEndpoint) {
            m_drawingGuideLine = false;
            m_dragMode = DragMode::None;
            m_selectedGuideLine = -1;
            m_hoveredGuideLine = -1;
            update();
            m_contextMenuSuppressed = true;
            event->accept();
            return;
        }

        QPoint pos = event->pos();

        // 先命中检测：直接删除鼠标下的元素
        // 辅助线端点
        int epLineIdx = -1;
        int epIdx = hitTestGuideEndpoint(pos, &epLineIdx);
        if (epIdx >= 0 && m_guideLineModel) {
            m_guideLineModel->removeLine(epLineIdx);
            m_selectedGuideLine = -1;
            m_hoveredGuideLine = -1;
            m_dragMode = DragMode::None;
            setFocus();
            update();
            m_contextMenuSuppressed = true;
            event->accept();
            return;
        }
        // 辅助线线体
        int lineIdx = hitTestGuideLine(pos);
        if (lineIdx >= 0 && m_guideLineModel) {
            m_guideLineModel->removeLine(lineIdx);
            m_selectedGuideLine = -1;
            m_hoveredGuideLine = -1;
            m_dragMode = DragMode::None;
            setFocus();
            update();
            m_contextMenuSuppressed = true;
            event->accept();
            return;
        }
        // 矩形ROI
        if (m_regionModel) {
            QVector<QRect> regions = m_regionModel->regions();
            for (int i = regions.size() - 1; i >= 0; --i) {
                if (mapFromVideo(regions[i]).contains(pos)) {
                    m_regionModel->removeRegion(i);
                    m_selectedRegion = -1;
                    m_dragMode = DragMode::None;
                    setFocus();
                    emit regionInteracted();
                    update();
                    m_contextMenuSuppressed = true;
                    event->accept();
                    return;
                }
            }
        }
        // 多边形ROI
        if (m_polygonModel) {
            QVector<QPolygon> polygons = m_polygonModel->polygons();
            for (int i = polygons.size() - 1; i >= 0; --i) {
                QPolygon widgetPoly;
                for (const QPoint &pt : polygons[i]) {
                    widgetPoly.append(mapFromVideo(pt));
                }
                if (widgetPoly.containsPoint(pos, Qt::OddEvenFill)) {
                    m_polygonModel->removePolygon(i);
                    m_selectedPolygon = -1;
                    m_dragMode = DragMode::None;
                    setFocus();
                    emit regionInteracted();
                    update();
                    m_contextMenuSuppressed = true;
                    event->accept();
                    return;
                }
            }
        }

        // 其他情况：不处理，让Qt触发右键菜单信号
        event->ignore();
    }
}

/// @brief 鼠标移动：ROI拖拽/光标追踪/区域创建
void OverlayWidget::mouseMoveEvent(QMouseEvent *event)
{
    QPoint pos = event->pos();
    QPoint videoPos = mapToVideo(pos);
    m_currentMousePos = pos;  // 用于绘制预览

    // 辅助线绘制中
    if (m_drawingGuideLine) {
        m_currentMousePos = pos;
        update();
        return;
    }

    // 多边形绘制中（预览）
    if (m_dragMode == DragMode::CreatePolygon) {
        m_currentMousePos = pos;
        update();
        return;
    }

    // 辅助线平移中
    if (m_dragMode == DragMode::MoveGuideLine && m_selectedGuideLine >= 0) {
        QPoint delta = videoPos - m_dragStart;
        GuideLine moved;
        moved.start = m_dragOriginalLine.start + delta;
        moved.end = m_dragOriginalLine.end + delta;
        moved.color = m_dragOriginalLine.color;
        m_guideLineModel->updateLine(m_selectedGuideLine, moved);
        return;
    }

    // 辅助线端点拖拽中
    if (m_dragMode == DragMode::ResizeGuideEndpoint && m_selectedGuideLine >= 0) {
        QPoint endPos = videoPos;
        bool shift = (event->modifiers() & Qt::ShiftModifier);
        GuideLine modified = m_dragOriginalLine;
        if (shift) {
            // 约束到水平或垂直（相对于另一端点）
            QPoint otherEnd = (m_guideEndpointIndex == 0) ? modified.end : modified.start;
            int dx = qAbs(endPos.x() - otherEnd.x());
            int dy = qAbs(endPos.y() - otherEnd.y());
            if (dx > dy) {
                endPos.setY(otherEnd.y());
            } else {
                endPos.setX(otherEnd.x());
            }
        }
        if (m_guideEndpointIndex == 0) {
            modified.start = endPos;
        } else {
            modified.end = endPos;
        }
        m_guideLineModel->updateLine(m_selectedGuideLine, modified);
        return;
    }

    if (m_dragMode == DragMode::CreateNew || m_dragMode == DragMode::PinSelect) {
        m_newRect = QRect(m_dragStart, videoPos).normalized();
        update();
    } else if (m_dragMode == DragMode::MoveRect && m_selectedRegion >= 0) {
        QPoint delta = videoPos - m_dragStart;
        QRect newRect = m_dragOriginalRect.translated(delta);
        newRect = clampRectToVideo(newRect);
        m_regionModel->updateRegion(m_selectedRegion, newRect);
    } else if (m_dragMode == DragMode::ResizeHandle && m_activeHandle.regionIndex >= 0) {
        QPoint delta = videoPos - m_dragStart;
        QRect r = m_dragOriginalRect;
        int handle = m_activeHandle.handleIndex;

        if (handle == 0) {         // top-left
            r.setTopLeft(r.topLeft() + delta);
        } else if (handle == 1) {  // top-right
            r.setTopRight(r.topRight() + delta);
        } else if (handle == 2) {  // bottom-right
            r.setBottomRight(r.bottomRight() + delta);
        } else if (handle == 3) {  // bottom-left
            r.setBottomLeft(r.bottomLeft() + delta);
        }
        r = clampRectToVideo(r);
        m_regionModel->updateRegion(m_activeHandle.regionIndex, r);
    } else if (m_dragMode == DragMode::MovePolygon && m_selectedPolygon >= 0) {
        QPoint delta = videoPos - m_dragStart;
        QPolygon moved;
        for (const QPoint &pt : m_dragOriginalPolygon)
            moved.append(clampPointToVideo(pt + delta));
        m_polygonModel->updatePolygon(m_selectedPolygon, moved);
    } else if (m_dragMode == DragMode::ResizePolygonVertex && m_selectedPolygon >= 0
               && m_dragPolygonVertexIndex >= 0) {
        QPolygon modified = m_dragOriginalPolygon;
        modified[m_dragPolygonVertexIndex] = clampPointToVideo(videoPos);
        m_polygonModel->updatePolygon(m_selectedPolygon, modified);
    } else if (m_dragMode == DragMode::MagnifierPan) {
        // Convert widget pixel delta to video coordinate delta
        QPoint widgetDelta = event->pos() - m_dragStart;
        m_dragStart = event->pos();
        if (m_videoDisplayRect.isEmpty() || m_videoWidth <= 0 || m_videoHeight <= 0)
            return;
        qreal scaleX = qreal(m_videoWidth) / m_videoDisplayRect.width();
        qreal scaleY = qreal(m_videoHeight) / m_videoDisplayRect.height();
        QPoint videoDelta(int(widgetDelta.x() * scaleX), int(widgetDelta.y() * scaleY));
        if (!videoDelta.isNull())
            emit magnifierPanRequested(videoDelta);
    } else {
        // Only emit cursor position for magnifier tracking when
        // the mouse is actually within the video display area.
        if (m_videoDisplayRect.contains(pos)) {
            emit magnifierCursorMoved(videoPos);
        }

        // 光标样式反馈
        if (m_guideLineMode) {
            int oldHover = m_hoveredGuideLine;
            m_hoveredGuideLine = -1;
            // 端点优先
            int epLineIdx = -1;
            int epIdx = hitTestGuideEndpoint(pos, &epLineIdx);
            if (epIdx >= 0) {
                m_hoveredGuideLine = epLineIdx;
                setCursor(Qt::SizeAllCursor);
            } else {
                int lineIdx = hitTestGuideLine(pos);
                if (lineIdx >= 0) {
                    m_hoveredGuideLine = lineIdx;
                    setCursor(Qt::OpenHandCursor);
                } else {
                    setCursor(Qt::CrossCursor);
                }
            }
            if (m_hoveredGuideLine != oldHover)
                update();
            return;
        }

        if (m_polygonMode) {
            // Check polygon vertices first
            int vertPolyIdx = -1;
            int vertIdx = hitTestPolygonVertex(pos, &vertPolyIdx);
            if (vertIdx >= 0 && vertPolyIdx == m_selectedPolygon) {
                setCursor(Qt::SizeAllCursor);
            } else {
                // Check if over selected polygon body
                bool overSelected = false;
                if (m_selectedPolygon >= 0 && m_polygonModel) {
                    QVector<QPolygon> polygons = m_polygonModel->polygons();
                    if (m_selectedPolygon < polygons.size()) {
                        QPolygon widgetPoly;
                        for (const QPoint &pt : polygons[m_selectedPolygon])
                            widgetPoly.append(mapFromVideo(pt));
                        if (widgetPoly.containsPoint(pos, Qt::OddEvenFill))
                            overSelected = true;
                    }
                }
                setCursor(overSelected ? Qt::SizeAllCursor : Qt::CrossCursor);
            }
            return;
        }

        // 矩形模式光标反馈
        if (m_selectedRegion >= 0) {
            QVector<QRect> regions = m_regionModel->regions();
            if (m_selectedRegion < regions.size()) {
                QRect widgetRect = mapFromVideo(regions[m_selectedRegion]);
                int handle = hitTestHandle(pos, widgetRect);
                if (handle >= 0) {
                    setCursor(Qt::SizeAllCursor);
                    return;
                }
            }
        }

        // Check if over any region (rect, polygon, or guide line)
        bool overElement = false;

        // Check rectangles
        if (m_regionModel) {
            QVector<QRect> regions = m_regionModel->regions();
            for (int i = regions.size() - 1; i >= 0; --i) {
                if (mapFromVideo(regions[i]).contains(pos)) {
                    overElement = true;
                    break;
                }
            }
        }

        // Check polygons
        if (!overElement && m_polygonModel) {
            QVector<QPolygon> polygons = m_polygonModel->polygons();
            for (int i = polygons.size() - 1; i >= 0; --i) {
                QPolygon widgetPoly;
                for (const QPoint &pt : polygons[i]) {
                    widgetPoly.append(mapFromVideo(pt));
                }
                if (widgetPoly.containsPoint(pos, Qt::OddEvenFill)) {
                    overElement = true;
                    break;
                }
            }
        }

        if (overElement)
            setCursor(Qt::OpenHandCursor);
        else
            setCursor(Qt::CrossCursor);
    }
}

/**
 * @brief 鼠标释放：完成区域操作/触发数据保护警告/中键平移结束
 */
void OverlayWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::MiddleButton) {
        if (m_dragMode == DragMode::MagnifierPan)
            m_dragMode = DragMode::None;
        return;
    }

    if (event->button() == Qt::LeftButton) {
        // 辅助线完成
        if (m_drawingGuideLine && m_guideLineModel) {
            QPoint videoPos = mapToVideo(event->pos());
            QPoint endPos = videoPos;

            // Shift约束：水平或垂直
            if (event->modifiers() & Qt::ShiftModifier) {
                int dx = abs(endPos.x() - m_guideLineStart.x());
                int dy = abs(endPos.y() - m_guideLineStart.y());
                if (dx > dy) {
                    endPos.setY(m_guideLineStart.y());  // 水平
                } else {
                    endPos.setX(m_guideLineStart.x());  // 垂直
                }
            }

            // 只有长度大于5像素才创建
            if ((endPos - m_guideLineStart).manhattanLength() > 5) {
                GuideLine line;
                line.start = m_guideLineStart;
                line.end = endPos;
                m_guideLineModel->addLine(line);
            }

            m_drawingGuideLine = false;
            m_dragMode = DragMode::None;
            update();
            return;
        }

        // 辅助线拖拽完成
        if ((m_dragMode == DragMode::MoveGuideLine || m_dragMode == DragMode::ResizeGuideEndpoint)
            && m_selectedGuideLine >= 0 && m_guideLineModel) {
            m_dragMode = DragMode::None;
            update();
            return;
        }

        if (m_dragMode == DragMode::CreateNew) {
            m_newRect = clampRectToVideo(m_newRect.normalized());
            if (m_newRect.width() > 4 && m_newRect.height() > 4) {
                m_regionModel->addRegion(m_newRect);
                m_selectedRegion = m_regionModel->regionCount() - 1;
                emit regionInteracted();
            }
            m_newRect = QRect();
        } else if (m_dragMode == DragMode::MoveRect && m_selectedRegion >= 0) {
            // Region was moved — check if it actually changed
            QVector<QRect> regions = m_regionModel->regions();
            if (m_selectedRegion < regions.size()) {
                QRect currentRect = regions[m_selectedRegion];
                if (currentRect != m_dragOriginalRect) {
                    emit regionInteracted();
                    emit regionAdjustmentFinished(m_selectedRegion, m_dragOriginalRect, currentRect);
                }
            }
        } else if (m_dragMode == DragMode::ResizeHandle && m_activeHandle.regionIndex >= 0) {
            // Region was resized — check if it actually changed
            QVector<QRect> regions = m_regionModel->regions();
            int idx = m_activeHandle.regionIndex;
            if (idx < regions.size()) {
                QRect currentRect = regions[idx];
                if (currentRect != m_dragOriginalRect) {
                    emit regionInteracted();
                    emit regionAdjustmentFinished(idx, m_dragOriginalRect, currentRect);
                }
            }
        } else if (m_dragMode == DragMode::MovePolygon && m_selectedPolygon >= 0) {
            if (m_polygonModel) {
                QVector<QPolygon> polygons = m_polygonModel->polygons();
                if (m_selectedPolygon < polygons.size()
                    && polygons[m_selectedPolygon] != m_dragOriginalPolygon) {
                    emit regionInteracted();
                    emit polygonAdjustmentFinished(m_selectedPolygon, m_dragOriginalPolygon, polygons[m_selectedPolygon]);
                }
            }
        } else if (m_dragMode == DragMode::ResizePolygonVertex && m_selectedPolygon >= 0) {
            if (m_polygonModel) {
                QVector<QPolygon> polygons = m_polygonModel->polygons();
                if (m_selectedPolygon < polygons.size()
                    && polygons[m_selectedPolygon] != m_dragOriginalPolygon) {
                    emit regionInteracted();
                    emit polygonAdjustmentFinished(m_selectedPolygon, m_dragOriginalPolygon, polygons[m_selectedPolygon]);
                }
            }
        } else if (m_dragMode == DragMode::PinSelect) {
            m_newRect = clampRectToVideo(m_newRect.normalized());
            if (m_newRect.width() > 4 && m_newRect.height() > 4) {
                emit pinnedRequested(m_newRect);
            }
            m_newRect = QRect();
        }
    }

    if (m_dragMode != DragMode::CreatePolygon) {
        m_dragMode = DragMode::None;
    }
    update();
}

void OverlayWidget::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_polygonMode
        && m_dragMode == DragMode::CreatePolygon) {
        // 双击完成多边形：移除最后一个重复点（双击的第二次 press 已添加）
        if (!m_polygonPoints.isEmpty())
            m_polygonPoints.removeLast();

        if (m_polygonPoints.size() >= 3 && m_polygonModel) {
            QPolygon polygon(m_polygonPoints);
            m_polygonModel->addPolygon(polygon);
            emit regionInteracted();
        }
        m_polygonPoints.clear();
        m_dragMode = DragMode::None;
        // 保持多边形模式，允许继续绘制下一个多边形
        update();
        event->accept();
        return;
    }
    QWidget::mouseDoubleClickEvent(event);
}

void OverlayWidget::wheelEvent(QWheelEvent *event)
{
    QPoint pos = event->position().toPoint();
    QPoint videoPos = mapToVideo(pos);
    emit magnifierWheelZoom(event->angleDelta().y(), videoPos);
    event->accept();
}

void OverlayWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        // 辅助线模式：退出辅助线模式
        if (m_guideLineMode) {
            m_guideLineMode = false;
            m_drawingGuideLine = false;
            m_hoveredGuideLine = -1;
            if (m_dragMode == DragMode::DrawGuideLine
                || m_dragMode == DragMode::MoveGuideLine
                || m_dragMode == DragMode::ResizeGuideEndpoint) {
                m_dragMode = DragMode::None;
            }
            emit modeChanged(lang("矩形模式", "Rect Mode"));
            update();
            event->accept();
            return;
        }
        // 多边形模式：退出多边形模式，取消绘制
        if (m_polygonMode) {
            m_polygonMode = false;
            m_polygonPoints.clear();
            if (m_dragMode == DragMode::CreatePolygon) {
                m_dragMode = DragMode::None;
            }
            emit modeChanged(lang("矩形模式", "Rect Mode"));
            update();
            event->accept();
            return;
        }
        // 否则传递给MainWindow（关闭放大镜）
        event->ignore();
        return;
    }

    if (event->key() == Qt::Key_Delete) {
        // 删除选中的矩形ROI
        if (m_selectedRegion >= 0 && m_regionModel) {
            m_regionModel->removeRegion(m_selectedRegion);
            m_selectedRegion = -1;
            emit regionInteracted();
            update();
            event->accept();
            return;
        }
        // 删除选中的多边形ROI
        if (m_selectedPolygon >= 0 && m_polygonModel) {
            m_polygonModel->removePolygon(m_selectedPolygon);
            m_selectedPolygon = -1;
            emit regionInteracted();
            update();
            event->accept();
            return;
        }
        // 删除选中的辅助线
        if (m_selectedGuideLine >= 0 && m_guideLineModel) {
            m_guideLineModel->removeLine(m_selectedGuideLine);
            m_selectedGuideLine = -1;
            update();
            event->accept();
            return;
        }
    } else {
        event->ignore();
    }
}

bool OverlayWidget::event(QEvent *event)
{
    if (event->type() == QEvent::ContextMenu && m_contextMenuSuppressed) {
        m_contextMenuSuppressed = false;
        event->accept();
        return true;
    }
    return QWidget::event(event);
}

// =============================================================================
// VideoWidget
// =============================================================================

VideoWidget::VideoWidget(QWidget *parent)
    : QWidget(parent)
{
    setMinimumSize(320, 240);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_overlay = new OverlayWidget(this);
    m_overlay->setGeometry(rect());
    m_overlay->raise();
    m_overlay->show();
}

VideoWidget::~VideoWidget() = default;

void VideoWidget::setVideoEngine(IVideoEngine *engine)
{
    if (m_engine) {
        disconnect(m_engine, nullptr, this, nullptr);
    }
    m_engine = engine;
    if (m_engine) {
        connect(m_engine, &IVideoEngine::frameReady,
                this, &VideoWidget::onFrameReady);
        connect(m_engine, &IVideoEngine::videoSizeChanged,
                m_overlay, &OverlayWidget::setVideoSize);
    }
}

void VideoWidget::setRegionModel(RegionModel *model)
{
    m_overlay->setRegionModel(model);
}

void VideoWidget::setPolygonModel(PolygonModel *model)
{
    m_overlay->setPolygonModel(model);
}

void VideoWidget::setGuideLineModel(GuideLineModel *model)
{
    m_overlay->setGuideLineModel(model);
}

void VideoWidget::onFrameReady(const QImage &image)
{
    // No deep copy: QImage is implicitly shared and m_frameImage is only read afterwards.
    m_frameImage = image;
    if (m_engine)
        m_engine->ackFrame();   // 归还配额（引擎有界化丢帧）
    updateOverlayGeometry();
    update();
    if (m_overlay)
        m_overlay->update();
}

void VideoWidget::clearFrame()
{
    m_frameImage = QImage();
    m_loading = false;
    update();
    if (m_overlay)
        m_overlay->update();
}

void VideoWidget::setLoading(bool on)
{
    m_loading = on;
    update();
}

void VideoWidget::setSnapshot(const QImage &snapshot, int brightness, int contrast, int opacity)
{
    if (snapshot.isNull()) {
        clearSnapshot();
        return;
    }
    m_snapshot = snapshot.copy();
    m_snapshotBrightness = brightness;
    m_snapshotContrast = contrast;
    m_snapshotOpacity = opacity;
    // Invalidate cache if parameters changed
    if (m_cachedBrightness != brightness || m_cachedContrast != contrast) {
        m_cachedBrightness = INT_MIN;
        m_cachedContrast = -999;
        m_adjustedSnapshot = QImage();
    }
    update();
}

void VideoWidget::clearSnapshot()
{
    m_snapshot = QImage();
    m_adjustedSnapshot = QImage();
    m_cachedBrightness = INT_MIN;
    m_cachedContrast = -999;
    update();
}

void VideoWidget::grabFrameSnapshot()
{
    if (!m_frameImage.isNull())
        emit frameSnapshotReady(m_frameImage);
}

QRect VideoWidget::videoDisplayRect() const
{
    if (m_frameImage.isNull())
        return rect();

    QSize videoSize = m_frameImage.size();
    QSize widgetSize = size();

    qreal videoRatio = qreal(videoSize.width()) / videoSize.height();
    qreal widgetRatio = qreal(widgetSize.width()) / widgetSize.height();

    QRect targetRect;
    if (videoRatio > widgetRatio) {
        // Video is wider relative to widget: fit to width, black bars top/bottom
        int w = widgetSize.width();
        int h = int(w / videoRatio);
        int y = (widgetSize.height() - h) / 2;
        targetRect = QRect(0, y, w, h);
    } else {
        // Video is taller relative to widget: fit to height, black bars left/right
        int h = widgetSize.height();
        int w = int(h * videoRatio);
        int x = (widgetSize.width() - w) / 2;
        targetRect = QRect(x, 0, w, h);
    }
    return targetRect;
}

void VideoWidget::updateOverlayGeometry()
{
    if (!m_overlay)
        return;
    m_overlay->setGeometry(rect());
    m_overlay->setVideoDisplayRect(videoDisplayRect());
}

/// @brief 绘制视频帧+截图叠加（含亮度对比度缓存）
void VideoWidget::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    QPainter painter(this);

    painter.fillRect(rect(), QColor(16, 18, 22));  // 近黑视频底色

    if (!m_frameImage.isNull()) {
        QRect target = videoDisplayRect();
        painter.drawImage(target, m_frameImage);

        // Draw snapshot overlay if active
        if (!m_snapshot.isNull()) {
            // Rebuild cache if parameters changed
            if (m_adjustedSnapshot.isNull() ||
                m_cachedBrightness != m_snapshotBrightness ||
                m_cachedContrast != m_snapshotContrast) {
                m_adjustedSnapshot = applyBrightnessContrast(m_snapshot, m_snapshotBrightness, m_snapshotContrast);
                m_cachedBrightness = m_snapshotBrightness;
                m_cachedContrast = m_snapshotContrast;
            }
            painter.setOpacity(1.0 - m_snapshotOpacity / 100.0);
            painter.drawImage(target, m_adjustedSnapshot);
            painter.setOpacity(1.0);
        }
    } else if (m_frameImage.isNull()) {
        // 品牌空状态：logo 水印 + 引导语（加载大视频期间显示“导入中…”）
        if (m_loading) {
            // 半透明圆角提示块
            const int boxW = qMin(320, width() - 40);
            const int boxH = 64;
            const QRect box((width() - boxW) / 2, (height() - boxH) / 2, boxW, boxH);
            painter.setRenderHint(QPainter::Antialiasing);
            painter.setBrush(QColor(24, 28, 34, 210));
            painter.setPen(QColor(Theme::Border));
            painter.drawRoundedRect(box, 10, 10);
            QFont f = painter.font();
            f.setPointSize(12);
            painter.setFont(f);
            painter.setPen(QColor(Theme::TextPrimary));
            painter.drawText(box, Qt::AlignCenter, lang("导入中… 正在解析视频\n请稍候",
                                                         "Importing… parsing video\nplease wait"));
            return;
        }
        static const QImage logo(QStringLiteral(":/logo.png"));
        if (!logo.isNull()) {
            const int side = qMin(width(), height()) / 3;
            if (side > 0) {
                QRect logoRect((width() - side) / 2, (height() - side) / 2 - side / 4, side, side);
                painter.setOpacity(0.08);
                painter.drawImage(logoRect, logo);
                painter.setOpacity(1.0);
            }
        }
        painter.setPen(QColor(Theme::TextMuted));
        QFont hintFont = painter.font();
        hintFont.setPointSize(11);
        painter.setFont(hintFont);
        const int textY = height() / 2 + qMin(width(), height()) / 12;
        painter.drawText(QRect(0, textY, width(), 60),
                         Qt::AlignHCenter | Qt::AlignTop,
                         lang("拖入视频，或按 Ctrl+O 打开", "Drop a video here, or press Ctrl+O to open"));
    }
}

void VideoWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateOverlayGeometry();
}
