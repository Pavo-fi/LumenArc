/**
 * @file videowidget.h
 * @brief 视频渲染组件 + OverlayWidget（ROI 交互层）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include <QWidget>
#include <QRect>
#include <QPolygon>
#include <QVector>
#include <QPointer>
#include <QImage>

class IVideoEngine;
class RegionModel;
class PolygonModel;
class GuideLineModel;
#include "domain/guide_line.h"

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
    void setPolygonModel(PolygonModel *model);
    void setGuideLineModel(GuideLineModel *model);
    void setVideoSize(int width, int height);
    void setVideoDisplayRect(const QRect &rect);
    void setVideoOriginOffset(const QPoint &offset);
    QPoint videoOriginOffset() const { return m_videoOriginOffset; }

    // 模式切换
    void setPolygonMode(bool enabled);
    bool isPolygonMode() const { return m_polygonMode; }
    void setGuideLineMode(bool enabled);
    bool isGuideLineMode() const { return m_guideLineMode; }

signals:
    void regionInteracted();
    void magnifierWheelZoom(int delta, QPoint videoPos);
    void magnifierCursorMoved(QPoint videoPos);
    void magnifierPanRequested(QPoint videoDelta);
    void pinnedRequested(const QRect &videoRect);
    void regionAdjustmentFinished(int regionIndex, const QRect &originalRect, const QRect &newRect);
    void polygonAdjustmentFinished(int polygonIndex, const QPolygon &originalPolygon, const QPolygon &newPolygon);
    void modeChanged(const QString &modeName);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool event(QEvent *event) override;

private slots:
    // (no slots needed)

private:
    enum class DragMode {
        None,
        CreateNew,
        MoveRect,
        ResizeHandle,
        PinSelect,
        MagnifierPan,
        CreatePolygon,
        MovePolygon,
        ResizePolygonVertex,
        DrawGuideLine,
        MoveGuideLine,
        ResizeGuideEndpoint
    };

    struct ResizeHandle {
        int regionIndex = -1;
        int handleIndex = -1; // 0=top-left, 1=top-right, 2=bottom-right, 3=bottom-left
    };

    // 矩形ROI绘制
    void drawRegions(QPainter &painter);
    void drawResizeHandles(QPainter &painter, const QRect &rect);
    int hitTestHandle(const QPoint &pos, const QRect &rect) const;
    QRect clampRectToVideo(const QRect &rect) const;
    QPoint clampPointToVideo(const QPoint &pt) const;

    // 多边形ROI绘制
    void drawPolygons(QPainter &painter);
    void drawPolygonHandles(QPainter &painter, const QPolygon &polygon);
    int hitTestPolygonVertex(const QPoint &pos, int *polygonIndex = nullptr) const;

    // 辅助线绘制
    void drawGuideLines(QPainter &painter);
    int hitTestGuideEndpoint(const QPoint &pos, int *lineIndex = nullptr) const;
    int hitTestGuideLine(const QPoint &pos) const;

    // 坐标映射
    QPoint mapToVideo(const QPoint &widgetPos) const;
    QPoint mapFromVideo(const QPoint &videoPos) const;
    QRect mapToVideo(const QRect &widgetRect) const;
    QRect mapFromVideo(const QRect &videoRect) const;

    QRect m_videoDisplayRect;

    QPointer<RegionModel> m_regionModel;
    QPointer<PolygonModel> m_polygonModel;
    QPointer<GuideLineModel> m_guideLineModel;
    int m_videoWidth = 0;
    int m_videoHeight = 0;
    QPoint m_videoOriginOffset;

    DragMode m_dragMode = DragMode::None;
    QPoint m_dragStart;
    QRect m_dragOriginalRect;
    int m_selectedRegion = -1;
    ResizeHandle m_activeHandle;
    QRect m_newRect;

    // 多边形模式
    bool m_polygonMode = false;
    QVector<QPoint> m_polygonPoints;
    int m_selectedPolygon = -1;
    QPolygon m_dragOriginalPolygon;    // 拖拽前的多边形副本
    int m_dragPolygonVertexIndex = -1; // 正在拖拽的顶点索引

    // 辅助线模式
    bool m_guideLineMode = false;
    QPoint m_guideLineStart;
    bool m_drawingGuideLine = false;
    int m_selectedGuideLine = -1;
    int m_hoveredGuideLine = -1;
    int m_guideEndpointIndex = -1;
    GuideLine m_dragOriginalLine;

    QPoint m_currentMousePos;  // 当前鼠标位置（widget坐标）
    bool m_contextMenuSuppressed = false;  // 右键已处理，抑制上下文菜单

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
    void setPolygonModel(PolygonModel *model);
    void setGuideLineModel(GuideLineModel *model);
    OverlayWidget *overlay() const { return m_overlay; }

    void setSnapshot(const QImage &snapshot, int brightness = 0, int contrast = 0, int opacity = 0);
    void clearSnapshot();
    /// @brief Clear the displayed video frame (back to "no video" placeholder).
    void clearFrame();
    /// @brief 大视频加载期间的“导入中…”提示（首帧到达后由调用方关闭）
    void setLoading(bool on);
    void grabFrameSnapshot();
    const QImage& currentFrame() const { return m_frameImage; }

signals:
    void frameSnapshotReady(const QImage &image);

public slots:
    void onFrameReady(const QImage &image);
    QRect videoDisplayRect() const;
    void updateOverlayGeometry();

public:
protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    OverlayWidget *m_overlay = nullptr;
    QPointer<IVideoEngine> m_engine;
    QImage m_frameImage;
    bool m_loading = false;   // 大视频加载提示（首帧到达后清除）

    // Snapshot fusion data
    QImage m_snapshot;
    QImage m_adjustedSnapshot;  // cached adjusted snapshot
    int m_snapshotBrightness = 0;
    int m_snapshotContrast = 0;
    int m_snapshotOpacity = 0;
    int m_cachedBrightness = INT_MIN;
    int m_cachedContrast = -999;
};
