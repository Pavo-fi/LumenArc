/**
 * @file videowidget.cpp
 * @brief 视频渲染 + OverlayWidget ROI 交互实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "videowidget.h"
#include "domain/region_model.h"
#include "infrastructure/ivideo_engine.h"
#include "i18n.h"

#include <QPainter>
#include <QMouseEvent>
#include <QKeyEvent>
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

/// @brief 鼠标按下：区域创建/选中/调整大小/pin选择
void OverlayWidget::mousePressEvent(QMouseEvent *event)
{
    if (!m_regionModel)
        return;

    if (event->button() == Qt::LeftButton) {
        QPoint pos = event->pos();
        bool ctrl = (event->modifiers() & Qt::ControlModifier);
        bool shift = (event->modifiers() & Qt::ShiftModifier);

        // Ctrl+Shift: pin timestamp selection
        if (ctrl && shift) {
            m_dragMode = DragMode::PinSelect;
            m_dragStart = mapToVideo(pos);
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
                    m_dragStart = mapToVideo(pos);
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
            m_dragMode = DragMode::MoveRect;
            m_dragStart = mapToVideo(pos);
            m_dragOriginalRect = m_regionModel->regions()[hit];
            update();
            return;
        }

        // Start creating new region
        m_dragMode = DragMode::CreateNew;
        m_dragStart = mapToVideo(pos);
        m_newRect = QRect(m_dragStart, QSize(0, 0));
        m_selectedRegion = -1;
        update();
    }
}

/// @brief 鼠标移动：ROI拖拽/光标追踪/区域创建
void OverlayWidget::mouseMoveEvent(QMouseEvent *event)
{
    QPoint pos = event->pos();
    QPoint videoPos = mapToVideo(pos);

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
    } else {
        // Only emit cursor position for magnifier tracking when
        // the mouse is actually within the video display area.
        if (m_videoDisplayRect.contains(pos)) {
            emit magnifierCursorMoved(videoPos);
        }

        // Cursor feedback (use widget coords for hit testing)
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
        QVector<QRect> regions = m_regionModel->regions();
        bool overRegion = false;
        for (int i = regions.size() - 1; i >= 0; --i) {
            if (mapFromVideo(regions[i]).contains(pos)) {
                overRegion = true;
                break;
            }
        }
        if (overRegion)
            setCursor(Qt::OpenHandCursor);
        else
            setCursor(Qt::CrossCursor);
    }
}

/**
 * @brief 鼠标释放：完成区域操作/触发数据保护警告
 */
void OverlayWidget::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;

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
    } else if (m_dragMode == DragMode::PinSelect) {
        m_newRect = clampRectToVideo(m_newRect.normalized());
        if (m_newRect.width() > 4 && m_newRect.height() > 4) {
            emit pinnedRequested(m_newRect);
        }
        m_newRect = QRect();
    }

    m_dragMode = DragMode::None;
    update();
}

void OverlayWidget::wheelEvent(QWheelEvent *event)
{
    emit magnifierWheelZoom(event->angleDelta().y());
    event->accept();
}

void OverlayWidget::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete) {
        if (m_selectedRegion >= 0 && m_regionModel) {
            m_regionModel->removeRegion(m_selectedRegion);
            m_selectedRegion = -1;
            emit regionInteracted();
            update();
        }
    } else {
        event->ignore();
    }
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

void VideoWidget::onFrameReady(const QImage &image)
{
    m_frameImage = image.copy();
    updateOverlayGeometry();
    update();
    if (m_overlay)
        m_overlay->update();
}

void VideoWidget::setSnapshot(const QImage &snapshot, int brightness, qreal contrast, qreal opacity)
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
    if (m_cachedBrightness != brightness || !qFuzzyCompare(m_cachedContrast, contrast)) {
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

    painter.fillRect(rect(), QColor(10, 10, 10));

    if (!m_frameImage.isNull()) {
        QRect target = videoDisplayRect();
        painter.drawImage(target, m_frameImage);

        // Draw snapshot overlay if active
        if (!m_snapshot.isNull()) {
            // Rebuild cache if parameters changed
            if (m_adjustedSnapshot.isNull() ||
                m_cachedBrightness != m_snapshotBrightness ||
                !qFuzzyCompare(m_cachedContrast, m_snapshotContrast)) {
                m_adjustedSnapshot = m_snapshot.convertToFormat(QImage::Format_ARGB32);
                int b = m_snapshotBrightness;
                int c = int((m_snapshotContrast - 1.0) * 50.0);
                if (b != 0 || c != 0) {
                    double cf = (259.0 * (c + 255)) / (255.0 * (259 - c));
                    for (int y = 0; y < m_adjustedSnapshot.height(); ++y) {
                        QRgb *line = reinterpret_cast<QRgb*>(m_adjustedSnapshot.scanLine(y));
                        for (int x = 0; x < m_adjustedSnapshot.width(); ++x) {
                            QRgb px = line[x];
                            int r = qBound(0, int(cf * (qRed(px) + b * 2 - 128) + 128), 255);
                            int g = qBound(0, int(cf * (qGreen(px) + b * 2 - 128) + 128), 255);
                            int bl = qBound(0, int(cf * (qBlue(px) + b * 2 - 128) + 128), 255);
                            line[x] = qRgba(r, g, bl, qAlpha(px));
                        }
                    }
                }
                m_cachedBrightness = m_snapshotBrightness;
                m_cachedContrast = m_snapshotContrast;
            }
            painter.setOpacity(1.0 - m_snapshotOpacity);
            painter.drawImage(target, m_adjustedSnapshot);
            painter.setOpacity(1.0);
        }
    } else {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter, lang("未加载视频", "No video loaded"));
    }
}

void VideoWidget::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    updateOverlayGeometry();
}
