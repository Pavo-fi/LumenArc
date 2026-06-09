/**
 * @file videowidget.h
 * @brief 视频渲染组件 + OverlayWidget（ROI 交互层）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include <QWidget>
#include <QRect>
#include <QVector>
#include <QPointer>
#include <QImage>

class IVideoEngine;
class RegionModel;

/**
 * @brief Overlay widget that sits on top of the video container.
 * Handles mouse events and draws the region selection rectangles.
 */
class OverlayWidget : public QWidget
{
    Q_OBJECT
public:
    explicit OverlayWidget(QWidget *parent = nullptr);

    void setRegionModel(RegionModel *model);
    void setVideoSize(int width, int height);
    void setVideoDisplayRect(const QRect &rect);
    void setVideoOriginOffset(const QPoint &offset);
    QPoint videoOriginOffset() const { return m_videoOriginOffset; }

signals:
    void regionInteracted();
    void magnifierWheelZoom(int delta);
    void magnifierCursorMoved(QPoint videoPos);
    void pinnedRequested(const QRect &videoRect);
    void regionAdjustmentFinished(int regionIndex, const QRect &originalRect, const QRect &newRect);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;

private:
    enum class DragMode {
        None,
        CreateNew,
        MoveRect,
        ResizeHandle,
        PinSelect
    };

    struct ResizeHandle {
        int regionIndex = -1;
        int handleIndex = -1; // 0=top-left, 1=top-right, 2=bottom-right, 3=bottom-left
    };

    void drawRegions(QPainter &painter);
    void drawResizeHandles(QPainter &painter, const QRect &rect);
    int hitTestHandle(const QPoint &pos, const QRect &rect) const;
    QRect clampRectToVideo(const QRect &rect) const;
    QPoint mapToVideo(const QPoint &widgetPos) const;
    QPoint mapFromVideo(const QPoint &videoPos) const;
    QRect mapToVideo(const QRect &widgetRect) const;
    QRect mapFromVideo(const QRect &videoRect) const;

    QRect m_videoDisplayRect;

    QPointer<RegionModel> m_regionModel;
    int m_videoWidth = 0;
    int m_videoHeight = 0;
    QPoint m_videoOriginOffset;

    DragMode m_dragMode = DragMode::None;
    QPoint m_dragStart;
    QRect m_dragOriginalRect;
    int m_selectedRegion = -1;
    ResizeHandle m_activeHandle;
    QRect m_newRect;

    static constexpr int HANDLE_SIZE = 4;
    static constexpr int HANDLE_HIT_RADIUS = 5;
};

/**
 * @brief VideoWidget renders the video frame on the bottom layer and hosts
 * an OverlayWidget on top to draw selection rectangles.
 */
class VideoWidget : public QWidget
{
    Q_OBJECT

public:
    explicit VideoWidget(QWidget *parent = nullptr);
    ~VideoWidget();

    void setVideoEngine(IVideoEngine *engine);
    void setRegionModel(RegionModel *model);
    OverlayWidget *overlay() const { return m_overlay; }

    void setSnapshot(const QImage &snapshot, int brightness = 0, qreal contrast = 1.0, qreal opacity = 0.5);
    void clearSnapshot();
    void grabFrameSnapshot();

signals:
    void frameSnapshotReady(const QImage &image);

public slots:
    void onFrameReady(const QImage &image);
    QRect videoDisplayRect() const;
    void updateOverlayGeometry();

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    OverlayWidget *m_overlay = nullptr;
    QPointer<IVideoEngine> m_engine;
    QImage m_frameImage;

    // Snapshot fusion data
    QImage m_snapshot;
    QImage m_adjustedSnapshot;  // cached adjusted snapshot
    int m_snapshotBrightness = 0;
    qreal m_snapshotContrast = 1.0;
    qreal m_snapshotOpacity = 0.5;
    int m_cachedBrightness = INT_MIN;
    qreal m_cachedContrast = -999;
};
