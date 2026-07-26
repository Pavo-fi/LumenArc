/**
 * @file magnifierwidget.cpp
 * @brief 放大镜窗口实现：源区域计算/缩放/截图叠加同步
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "magnifierwidget.h"
#include "videowidget.h"
#include "domain/region_model.h"
#include "domain/polygon_model.h"
#include "domain/guide_line_model.h"
#include "i18n.h"

#include <QPainter>
#include <QResizeEvent>
#include <QContextMenuEvent>
#include <climits>

// =============================================================================
// ContentWidget: internal rendering surface
// =============================================================================
class MagnifierWidget::ContentWidget : public QWidget
{
public:
    explicit ContentWidget(MagnifierWidget *owner);

    void setRegionModel(RegionModel *model);
    void setPolygonModel(PolygonModel *model);
    void onFrameReady(const QImage &frame);
    void updateOverlayGeometry();
    OverlayWidget *overlay() const { return m_overlay; }

    // Snapshot fusion overlay
    void setSnapshotOverlay(const QImage &img, int brightness, int contrast, int opacity);
    void clearSnapshotOverlay();
    bool hasSnapshot() const { return !m_snapshot.isNull(); }
    void reCropSnapshot(const QRect &sourceRect, const QImage &original);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    QRect displayRect() const;

    MagnifierWidget *m_owner;
    QImage m_frameImage;
    OverlayWidget *m_overlay = nullptr;

    // Snapshot fusion data
    QImage m_snapshot;
    QImage m_adjustedSnapshot;
    int m_snapshotBrightness = 0;
    int m_snapshotContrast = 0;
    int m_snapshotOpacity = 0;
    int m_cachedBrightness = INT_MIN;
    int m_cachedContrast = INT_MIN;
};

MagnifierWidget::ContentWidget::ContentWidget(MagnifierWidget *owner)
    : QWidget(owner)
    , m_owner(owner)
{
    setMinimumSize(160, 120);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);

    m_overlay = new OverlayWidget(this);
    m_overlay->setGeometry(rect());
    m_overlay->raise();
    m_overlay->show();
}

void MagnifierWidget::ContentWidget::setRegionModel(RegionModel *model)
{
    m_overlay->setRegionModel(model);
}

void MagnifierWidget::ContentWidget::setPolygonModel(PolygonModel *model)
{
    m_overlay->setPolygonModel(model);
}

void MagnifierWidget::ContentWidget::onFrameReady(const QImage &frame)
{
    m_frameImage = frame;
    updateOverlayGeometry();
    update();
    if (m_overlay)
        m_overlay->update();
}

void MagnifierWidget::ContentWidget::setSnapshotOverlay(const QImage &img, int brightness, int contrast, int opacity)
{
    m_snapshot = img;
    m_snapshotBrightness = brightness;
    m_snapshotContrast = contrast;
    m_snapshotOpacity = opacity;
    m_cachedBrightness = INT_MIN;
    m_cachedContrast = INT_MIN;
    m_adjustedSnapshot = QImage();
    update();
}

void MagnifierWidget::ContentWidget::clearSnapshotOverlay()
{
    m_snapshot = QImage();
    m_adjustedSnapshot = QImage();
    m_cachedBrightness = INT_MIN;
    m_cachedContrast = INT_MIN;
    update();
}

void MagnifierWidget::ContentWidget::reCropSnapshot(const QRect &sourceRect, const QImage &original)
{
    if (original.isNull() || sourceRect.isEmpty()) {
        clearSnapshotOverlay();
        return;
    }
    QRect src = sourceRect.intersected(original.rect());
    if (src.isEmpty()) {
        clearSnapshotOverlay();
        return;
    }
    m_snapshot = original.copy(src);
    m_cachedBrightness = INT_MIN;
    m_cachedContrast = INT_MIN;
    m_adjustedSnapshot = QImage();
    update();
}

QRect MagnifierWidget::ContentWidget::displayRect() const
{
    if (m_frameImage.isNull())
        return rect();

    QSize imgSize = m_frameImage.size();
    QSize widgetSize = size();

    qreal imgRatio = qreal(imgSize.width()) / imgSize.height();
    qreal widgetRatio = qreal(widgetSize.width()) / widgetSize.height();

    if (imgRatio > widgetRatio) {
        int w = widgetSize.width();
        int h = int(w / imgRatio);
        int y = (widgetSize.height() - h) / 2;
        return QRect(0, y, w, h);
    } else {
        int h = widgetSize.height();
        int w = int(h * imgRatio);
        int x = (widgetSize.width() - w) / 2;
        return QRect(x, 0, w, h);
    }
}

void MagnifierWidget::ContentWidget::updateOverlayGeometry()
{
    if (!m_overlay)
        return;
    m_overlay->setGeometry(rect());
    m_overlay->setVideoDisplayRect(displayRect());
}

void MagnifierWidget::ContentWidget::paintEvent(QPaintEvent * /*event*/)
{
    QPainter painter(this);
    painter.fillRect(rect(), QColor(10, 10, 10));

    if (!m_frameImage.isNull()) {
        QRect target = displayRect();
        painter.drawImage(target, m_frameImage);

        // Draw snapshot fusion overlay if active
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

        painter.setPen(QColor(200, 200, 200, 160));
        painter.setFont(fontSans(9));
        painter.drawText(rect().adjusted(6, 4, -6, -4),
                         Qt::AlignTop | Qt::AlignRight,
                         QString("%1x").arg(m_owner->m_zoomLevel, 0, 'f', 1));
    } else {
        painter.setPen(Qt::gray);
        painter.drawText(rect(), Qt::AlignCenter,
            lang("放大镜 — 滚轮缩放", "Magnifier — scroll to zoom"));
    }
}

void MagnifierWidget::ContentWidget::resizeEvent(QResizeEvent * /*event*/)
{
    updateOverlayGeometry();
}

void MagnifierWidget::ContentWidget::contextMenuEvent(QContextMenuEvent *event)
{
    // 阻止右键事件传播到 QDockWidget 的停靠菜单
    event->accept();
}

// =============================================================================
// MagnifierWidget (QDockWidget)
// =============================================================================

/// @brief 构造放大镜：创建内容控件/连接内部Overlay信号
MagnifierWidget::MagnifierWidget(QWidget *parent)
    : QDockWidget(lang("放大镜", "Magnifier"), parent)
{
    m_content = new ContentWidget(this);
    m_overlay = m_content->overlay();
    setWidget(m_content);
    setFeatures(QDockWidget::DockWidgetMovable | QDockWidget::DockWidgetFloatable);
    setMinimumWidth(160);

    // Connect internal overlay signals so magnifier responds to
    // wheel zoom inside its own view. Cursor tracking is NOT connected here —
    // magnifier pan should only happen via middle-button drag on the main video overlay.
    if (m_overlay) {
        connect(m_overlay, &OverlayWidget::magnifierWheelZoom,
                this, &MagnifierWidget::onInternalOverlayWheelZoom);
        connect(m_overlay, &OverlayWidget::magnifierPanRequested,
                this, [this](QPoint delta) {
            if (m_invertPan)
                delta = -delta;
            QPoint newCenter = m_cursorPos + delta;
            newCenter.setX(qBound(0, newCenter.x(), m_videoWidth - 1));
            newCenter.setY(qBound(0, newCenter.y(), m_videoHeight - 1));
            if (newCenter != m_cursorPos) {
                m_cursorPos = newCenter;
                recalcSourceRect();
            }
        });
    }
}

MagnifierWidget::~MagnifierWidget() = default;

void MagnifierWidget::onInternalOverlayWheelZoom(int delta, QPoint videoPos)
{
    zoomAtPoint(delta, videoPos);
}

/// @brief 放大镜内光标移动：映射到全视频坐标
void MagnifierWidget::onInternalOverlayCursorMoved(QPoint videoPos)
{
    if (m_videoWidth <= 0 || m_videoHeight <= 0)
        return;

    // videoPos is in the overlay's local video coordinates (within sourceRect).
    // Convert to full video coordinates by adding the origin offset.
    QPoint fullVideoPos = videoPos + m_sourceRect.topLeft();
    fullVideoPos.setX(qBound(0, fullVideoPos.x(), m_videoWidth - 1));
    fullVideoPos.setY(qBound(0, fullVideoPos.y(), m_videoHeight - 1));

    if (fullVideoPos == m_cursorPos)
        return;

    m_cursorPos = fullVideoPos;
    recalcSourceRect();
}

void MagnifierWidget::setRegionModel(RegionModel *model)
{
    if (m_overlay)
        m_overlay->setRegionModel(model);
}

void MagnifierWidget::setPolygonModel(PolygonModel *model)
{
    if (m_overlay)
        m_overlay->setPolygonModel(model);
}

void MagnifierWidget::setGuideLineModel(GuideLineModel *model)
{
    if (m_overlay)
        m_overlay->setGuideLineModel(model);
}

void MagnifierWidget::setVideoSize(int width, int height)
{
    m_videoWidth = qMax(1, width);
    m_videoHeight = qMax(1, height);
    m_cursorPos = QPoint(m_videoWidth / 2, m_videoHeight / 2);
    recalcSourceRect();
}

void MagnifierWidget::setZoomLevel(qreal level)
{
    m_zoomLevel = qBound(MIN_ZOOM, level, MAX_ZOOM);
    recalcSourceRect();
    m_content->update();
}

void MagnifierWidget::adjustZoom(int delta)
{
    qreal step = ZOOM_STEP * (delta > 0 ? 1 : -1);
    setZoomLevel(m_zoomLevel + step);
}

/// @brief 以鼠标位置为锚点缩放：缩放后鼠标下的像素保持不动
void MagnifierWidget::zoomAtPoint(int delta, QPoint videoPos)
{
    if (m_videoWidth <= 0 || m_videoHeight <= 0)
        return;
    if (m_sourceRect.isEmpty())
        return;

    qreal newZoom = qBound(MIN_ZOOM, m_zoomLevel + ZOOM_STEP * (delta > 0 ? 1 : -1), MAX_ZOOM);
    int newSrcW = qMax(1, int(m_videoWidth / newZoom));
    int newSrcH = qMax(1, int(m_videoHeight / newZoom));

    // 鼠标在当前源区域内的相对比例 (0.0~1.0)
    qreal tX = qreal(videoPos.x() - m_sourceRect.x()) / m_sourceRect.width();
    qreal tY = qreal(videoPos.y() - m_sourceRect.y()) / m_sourceRect.height();

    // 锚点缩放：保持鼠标下的像素不动
    int newSrcX = qBound(0, int(videoPos.x() - tX * newSrcW), m_videoWidth - newSrcW);
    int newSrcY = qBound(0, int(videoPos.y() - tY * newSrcH), m_videoHeight - newSrcH);

    m_zoomLevel = newZoom;
    m_sourceRect = QRect(newSrcX, newSrcY, newSrcW, newSrcH);
    m_content->updateOverlayGeometry();

    if (m_overlay) {
        m_overlay->setVideoSize(newSrcW, newSrcH);
        m_overlay->setVideoOriginOffset(QPoint(newSrcX, newSrcY));
    }

    if (!m_snapshotOriginal.isNull() && m_content->hasSnapshot())
        m_content->reCropSnapshot(m_sourceRect, m_snapshotOriginal);

    m_content->update();
}

void MagnifierWidget::updateCursorPosition(QPoint videoPos)
{
    if (m_videoWidth <= 0 || m_videoHeight <= 0)
        return;

    videoPos.setX(qBound(0, videoPos.x(), m_videoWidth - 1));
    videoPos.setY(qBound(0, videoPos.y(), m_videoHeight - 1));

    if (videoPos == m_cursorPos)
        return;

    m_cursorPos = videoPos;
    recalcSourceRect();
}

void MagnifierWidget::restoreFromRect(const QRect &videoRect, int videoWidth, int videoHeight)
{
    m_videoWidth = qMax(1, videoWidth);
    m_videoHeight = qMax(1, videoHeight);

    if (videoRect.isEmpty()) {
        m_cursorPos = QPoint(m_videoWidth / 2, m_videoHeight / 2);
        m_zoomLevel = 2.0;
    } else {
        m_cursorPos = videoRect.center();
        qreal zoomX = qreal(m_videoWidth) / qMax(1, videoRect.width());
        qreal zoomY = qreal(m_videoHeight) / qMax(1, videoRect.height());
        m_zoomLevel = qBound(MIN_ZOOM, qMin(zoomX, zoomY), MAX_ZOOM);
    }
    recalcSourceRect();
}

/**
 * @brief 重算源区域：居中裁剪/snapshot重新裁剪/Overlay坐标更新
 */
void MagnifierWidget::recalcSourceRect()
{
    if (m_videoWidth <= 0 || m_videoHeight <= 0)
        return;

    int srcW = qMax(1, int(m_videoWidth / m_zoomLevel));
    int srcH = qMax(1, int(m_videoHeight / m_zoomLevel));

    int srcX = m_cursorPos.x() - srcW / 2;
    int srcY = m_cursorPos.y() - srcH / 2;

    srcX = qBound(0, srcX, m_videoWidth - srcW);
    srcY = qBound(0, srcY, m_videoHeight - srcH);

    m_sourceRect = QRect(srcX, srcY, srcW, srcH);
    m_content->updateOverlayGeometry();

    if (m_overlay) {
        m_overlay->setVideoSize(srcW, srcH);
        m_overlay->setVideoOriginOffset(QPoint(srcX, srcY));
    }

    // Re-crop snapshot overlay to match new source rect
    if (!m_snapshotOriginal.isNull() && m_content->hasSnapshot()) {
        m_content->reCropSnapshot(m_sourceRect, m_snapshotOriginal);
    } else if (!m_snapshotOriginal.isNull() && !m_content->hasSnapshot()) {
        // Edge case: m_snapshotOriginal set but content doesn't have snapshot yet
        QRect src = m_sourceRect.intersected(m_snapshotOriginal.rect());
        if (!src.isEmpty()) {
            m_content->setSnapshotOverlay(m_snapshotOriginal.copy(src),
                                         m_snapshotBrightness, m_snapshotContrast, m_snapshotOpacity);
        }
    }
}

/// @brief 接收视频帧：裁剪源区域后传给ContentWidget渲染
void MagnifierWidget::onFrameReady(const QImage &fullFrame)
{
    if (fullFrame.isNull() || m_sourceRect.isEmpty())
        return;

    // Safety: if m_snapshotOriginal was cleared but content still has snapshot, force clear
    if (m_snapshotOriginal.isNull() && m_content->hasSnapshot()) {
        m_content->clearSnapshotOverlay();
        m_content->update();
    }

    QRect src = m_sourceRect.intersected(fullFrame.rect());
    if (src.isEmpty()) {
        m_content->onFrameReady(QImage());
        return;
    }

    m_content->onFrameReady(fullFrame.copy(src));
}

/**
 * @brief 设置截图叠加：裁剪到源区域/snapshot参数存储
 */
void MagnifierWidget::setSnapshotOverlay(const QImage &img, int brightness, int contrast, int opacity)
{
    if (img.isNull() || m_sourceRect.isEmpty()) {
        // Clear everything if image is invalid or source rect not ready
        m_snapshotOriginal = QImage();
        m_snapshotBrightness = 0;
        m_snapshotContrast = 0;
        m_snapshotOpacity = 0;
        m_content->clearSnapshotOverlay();
        return;
    }

    m_snapshotOriginal = img;
    m_snapshotBrightness = brightness;
    m_snapshotContrast = contrast;
    m_snapshotOpacity = opacity;
    QRect src = m_sourceRect.intersected(img.rect());
    if (!src.isEmpty()) {
        m_content->setSnapshotOverlay(img.copy(src), brightness, contrast, opacity);
    } else {
        m_content->clearSnapshotOverlay();
    }
}

void MagnifierWidget::clearSnapshotOverlay()
{
    m_snapshotOriginal = QImage();
    m_snapshotBrightness = 0;
    m_snapshotContrast = 0;
    m_snapshotOpacity = 0;
    m_content->clearSnapshotOverlay();
    m_content->update();
}
