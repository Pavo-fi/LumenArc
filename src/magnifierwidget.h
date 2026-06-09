/**
 * @file magnifierwidget.h
 * @brief 放大镜停靠窗口：滚轮缩放/光标跟随/截图叠加同步
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include <QDockWidget>
#include <QRect>
#include <QImage>

class OverlayWidget;
class RegionModel;
class QWidget;

/**
 * @brief Magnified view of a sub-region of the video frame (dockable).
 *
 * Implemented as a QDockWidget to avoid reparenting the VideoWidget.
 * Contains an internal content widget that renders the magnified frame
 * and hosts an OverlayWidget for ROI drawing.
 */
class MagnifierWidget : public QDockWidget
{
    Q_OBJECT

public:
    explicit MagnifierWidget(QWidget *parent = nullptr);
    ~MagnifierWidget();

    void setRegionModel(RegionModel *model);
    /// @brief 设置源视频尺寸，用于坐标映射与裁剪范围计算
    void setVideoSize(int width, int height);

    /// @brief 设置缩放倍率
    void setZoomLevel(qreal level);
    qreal zoomLevel() const { return m_zoomLevel; }
    /// @brief 滚轮步进调整缩放倍率
    void adjustZoom(int delta);

    /// @brief 更新光标在视频中的位置，触发放大区域重绘
    void updateCursorPosition(QPoint videoPos);
    QPoint cursorPosition() const { return m_cursorPos; }
    QRect currentSourceRect() const { return m_sourceRect; }
    void restoreFromRect(const QRect &videoRect, int videoWidth, int videoHeight);

    /// @brief 接收完整帧并裁剪放大区域
    void onFrameReady(const QImage &fullFrame);

    /// @brief 设置截图叠加参考图及参数
    void setSnapshotOverlay(const QImage &img, int brightness, int contrast, int opacity);
    /// @brief 清除截图叠加
    void clearSnapshotOverlay();

private slots:
    void onInternalOverlayWheelZoom(int delta);
    void onInternalOverlayCursorMoved(QPoint videoPos);

private:
    class ContentWidget;
    ContentWidget *m_content = nullptr;

    QImage m_snapshotOriginal;  // full-frame snapshot for re-cropping

    qreal m_zoomLevel = 2.0;
    QPoint m_cursorPos;
    int m_videoWidth = 0;
    int m_videoHeight = 0;
    QRect m_sourceRect;
    OverlayWidget *m_overlay = nullptr;

    // Snapshot fusion parameters (for re-cropping on zoom/pan)
    int m_snapshotBrightness = 0;
    int m_snapshotContrast = 0;
    int m_snapshotOpacity = 0;

    void recalcSourceRect();

    static constexpr qreal MIN_ZOOM = 1.5;
    static constexpr qreal MAX_ZOOM = 10.0;
    static constexpr qreal ZOOM_STEP = 0.25;
};
