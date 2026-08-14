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
#include "displayadjust.h"
#include "domain/region_model.h"
#include "domain/polygon_model.h"
#include "domain/guide_line_model.h"
#include "i18n.h"

#include <QPainter>
#include <QResizeEvent>
#include <QContextMenuEvent>
#include <QTransform>
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
    /// 显示旋转档位（旋转在裁剪之后、显示之前应用）
    void setDisplayRotation(int degrees) { m_displayRotation = degrees; }
    int displayRotation() const { return m_displayRotation; }
    /// 画面调节 LUT（空表 = 恒等；在旋转之后应用，与主画面同一张表）
    void setDisplayLut(const QByteArray &lut) { m_displayLut = lut; }
    /// 当前裁剪显示图（旋转+LUT 已应用）——快照全面化取用
    QImage currentImage() const { return m_frameImage; }

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
    int m_displayRotation = 0;   ///< 显示旋转档位（0/90/180/270 顺时针）
    QByteArray m_displayLut;     ///< 画面调节 LUT（空 = 恒等）
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
    // 裁剪图（原视频系）→ 旋转 → 画面调节 LUT（与主画面同一显示链）
    QImage img = (m_displayRotation != 0)
        ? frame.transformed(QTransform().rotate(m_displayRotation))
        : frame;
    m_frameImage = applyDisplayLut(img, m_displayLut);
    updateOverlayGeometry();
    update();
    if (m_overlay)
        m_overlay->update();
}

void MagnifierWidget::ContentWidget::setSnapshotOverlay(const QImage &img, int brightness, int contrast, int opacity)
{
    // 裁剪图（原视频系）→ 显示前旋转，与帧裁剪同一档位保持对齐
    m_snapshot = (m_displayRotation != 0 && !img.isNull())
        ? img.transformed(QTransform().rotate(m_displayRotation))
        : img;
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
    if (m_displayRotation != 0)
        m_snapshot = m_snapshot.transformed(QTransform().rotate(m_displayRotation));
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

void MagnifierWidget::setDisplayRotation(int degrees)
{
    int d = degrees % 360;
    if (d < 0) d += 360;
    d = ((d + 45) / 90) * 90 % 360;
    if (m_displayRotation == d)
        return;
    m_displayRotation = d;
    if (m_overlay)
        m_overlay->setDisplayRotation(d);
    if (m_content)
        m_content->setDisplayRotation(d);
    // 立即刷新：recalcSourceRect 内部会以最近一帧重裁（ContentWidget 显示前
    // 旋转），并按同一链路重裁截图叠加（含 m_snapshotOriginal 已置位但
    // content 无快照的边界分支），源区域/光标/overlay 坐标全部不动。
    recalcSourceRect();
}

void MagnifierWidget::setDisplayAdjust(const DisplayAdjust &adj)
{
    if (m_content)
        m_content->setDisplayLut(adj.buildLut());
    // 立即用最近一帧重裁刷新（暂停/停止时也实时预览，与主画面一致）
    if (!m_lastFullFrame.isNull())
        onFrameReady(m_lastFullFrame);
}

QImage MagnifierWidget::currentMagnifiedImage() const
{
    return m_content ? m_content->currentImage() : QImage();
}

void MagnifierWidget::onInternalOverlayWheelZoom(int delta, QPoint videoPos)
{
    zoomAtPoint(delta, videoPos);
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
    m_cursorPos = QPoint((m_videoWidth / 2) & ~1, (m_videoHeight / 2) & ~1);  // 偶数网格
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
    // 源区域全部取偶：scrub 降采样帧为偶数网格（0.5 缩放），奇数坐标/尺寸会
    // 在换算 round(v*0.5) 时引入 ±1 视频像素误差（高 zoom 显示下可见漂移）
    const int newSrcW = qMax(2, (int(m_videoWidth / newZoom)) & ~1);
    const int newSrcH = qMax(2, (int(m_videoHeight / newZoom)) & ~1);

    // 鼠标在当前源区域内的相对比例 (0.0~1.0)
    qreal tX = qreal(videoPos.x() - m_sourceRect.x()) / m_sourceRect.width();
    qreal tY = qreal(videoPos.y() - m_sourceRect.y()) / m_sourceRect.height();

    // 锚点缩放：保持鼠标下的像素不动
    int newSrcX = (qBound(0, int(videoPos.x() - tX * newSrcW), m_videoWidth - newSrcW)) & ~1;
    int newSrcY = (qBound(0, int(videoPos.y() - tY * newSrcH), m_videoHeight - newSrcH)) & ~1;

    m_zoomLevel = newZoom;
    const QRect newRect(newSrcX, newSrcY, newSrcW, newSrcH);
    const bool changed = (newRect != m_sourceRect);
    m_sourceRect = newRect;
    m_content->updateOverlayGeometry();

    if (m_overlay) {
        m_overlay->setVideoSize(newSrcW, newSrcH);
        m_overlay->setVideoOriginOffset(QPoint(newSrcX, newSrcY));
    }
    if (changed)
        emit sourceRectChanged(m_sourceRect, m_zoomLevel);

    if (!m_snapshotOriginal.isNull() && m_content->hasSnapshot())
        m_content->reCropSnapshot(sourceRectForImage(m_snapshotOriginal), m_snapshotOriginal);

    // 暂停/停止时无新帧到达：缩放后立即用最近一帧重裁（与 recalcSourceRect 同理）
    if (!m_lastFullFrame.isNull())
        onFrameReady(m_lastFullFrame);

    m_content->update();
}

/**
 * @brief 视频坐标源矩形 → 图像帧坐标（等比换算，clamp 到帧内）。
 *
 * scrub 拖拽期间引擎输出降采样预览帧（displayFrame：宽 ≤1280），
 * 与全分辨率视频尺寸不一致。直接用视频坐标矩形 intersect 降采样帧
 * 会导致裁剪区域漂移（交集落在帧内错误位置）或为空（黑屏）。
 * 帧 = 原生分辨率时换算为恒等，零开销。
 */
QRect MagnifierWidget::sourceRectForImage(const QImage &img) const
{
    if (img.isNull() || m_sourceRect.isEmpty()
        || m_videoWidth <= 0 || m_videoHeight <= 0)
        return QRect();

    if (img.size() == QSize(m_videoWidth, m_videoHeight))
        return m_sourceRect.intersected(img.rect());

    // 降采样帧：按 帧/视频 比例换算（qRound64 保留亚像素精度，误差 <1px）
    const qreal sx = qreal(img.width()) / m_videoWidth;
    const qreal sy = qreal(img.height()) / m_videoHeight;
    const int x = qRound64(m_sourceRect.x() * sx);
    const int y = qRound64(m_sourceRect.y() * sy);
    const int w = qMax(1, qRound64(m_sourceRect.width() * sx));
    const int h = qMax(1, qRound64(m_sourceRect.height() * sy));
    return QRect(x, y, w, h).intersected(img.rect());
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
        m_cursorPos = QPoint((m_videoWidth / 2) & ~1, (m_videoHeight / 2) & ~1);
        m_zoomLevel = 2.0;
    } else {
        // 偶数网格（scrub 换算零舍入前提）
        m_cursorPos = QPoint(videoRect.center().x() & ~1, videoRect.center().y() & ~1);
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

    // 源区域全部取偶（scrub 偶数网格换算零舍入）：
    // 奇数坐标/尺寸在 sourceRectForImage 的 round(v*0.5) 中引入
    // ±1 视频像素误差，高 zoom 显示下放大为可见漂移（GUI 实测 zoom6 → +3px）
    const int srcW = qMax(2, (int(m_videoWidth / m_zoomLevel)) & ~1);
    const int srcH = qMax(2, (int(m_videoHeight / m_zoomLevel)) & ~1);

    int srcX = (m_cursorPos.x() - srcW / 2) & ~1;
    int srcY = (m_cursorPos.y() - srcH / 2) & ~1;

    srcX = qBound(0, srcX, m_videoWidth - srcW);
    srcY = qBound(0, srcY, m_videoHeight - srcH);
    srcX &= ~1;
    srcY &= ~1;

    const QRect oldRect = m_sourceRect;
    const QRect newRect(srcX, srcY, srcW, srcH);
    m_sourceRect = newRect;
    m_content->updateOverlayGeometry();

    if (m_overlay) {
        m_overlay->setVideoSize(srcW, srcH);
        m_overlay->setVideoOriginOffset(QPoint(srcX, srcY));
    }
    if (newRect != oldRect)
        emit sourceRectChanged(m_sourceRect, m_zoomLevel);

    // Re-crop snapshot overlay to match new source rect
    if (!m_snapshotOriginal.isNull() && m_content->hasSnapshot()) {
        m_content->reCropSnapshot(sourceRectForImage(m_snapshotOriginal), m_snapshotOriginal);
    } else if (!m_snapshotOriginal.isNull() && !m_content->hasSnapshot()) {
        // Edge case: m_snapshotOriginal set but content doesn't have snapshot yet
        QRect src = sourceRectForImage(m_snapshotOriginal);
        if (!src.isEmpty()) {
            m_content->setSnapshotOverlay(m_snapshotOriginal.copy(src),
                                         m_snapshotBrightness, m_snapshotContrast, m_snapshotOpacity);
        }
    }

    // 暂停/停止时无新帧到达：源区域变化后立即用最近一帧重裁，
    // 放大镜视野随中键拖动/滚轮缩放实时刷新
    if (!m_lastFullFrame.isNull())
        onFrameReady(m_lastFullFrame);
}

/// @brief 接收视频帧：裁剪源区域后传给ContentWidget渲染
void MagnifierWidget::onFrameReady(const QImage &fullFrame)
{
    m_lastFullFrame = fullFrame;   // 隐式共享，赋值近乎零开销；暂停时供重裁刷新
    if (fullFrame.isNull() || m_sourceRect.isEmpty())
        return;

    // Safety: if m_snapshotOriginal was cleared but content still has snapshot, force clear
    if (m_snapshotOriginal.isNull() && m_content->hasSnapshot()) {
        m_content->clearSnapshotOverlay();
        m_content->update();
    }

    // 源矩形换算到帧坐标（scrub 降采样帧 ≠ 原生分辨率时按比例缩放）
    const QRect src = sourceRectForImage(fullFrame);
    if (src.isEmpty())
        return;   // 保留上一帧：scrub 快速拖拽丢帧期间避免黑屏闪烁

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
    QRect src = sourceRectForImage(img);
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
