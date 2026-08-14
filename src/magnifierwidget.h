/**
 * @file magnifierwidget.h
 * @brief 放大镜停靠窗口：滚轮缩放/光标跟随/截图叠加同步
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include <QDockWidget>
#include <QRect>
#include <QImage>
#include "displayadjust.h"

class OverlayWidget;
class RoiModel;
class RoiModel;
class GuideLineModel;
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

    void setRegionModel(RoiModel *model);
    void setPolygonModel(RoiModel *model);
    void setGuideLineModel(GuideLineModel *model);
    /// @brief 设置源视频尺寸，用于坐标映射与裁剪范围计算
    void setVideoSize(int width, int height);

    /// @brief 设置缩放倍率
    void setZoomLevel(qreal level);
    qreal zoomLevel() const { return m_zoomLevel; }
    /// @brief 滚轮步进调整缩放倍率
    void adjustZoom(int delta);
    /// @brief 以鼠标位置为锚点缩放（保持像素不动）
    void zoomAtPoint(int delta, QPoint videoPos);

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
    /// @brief 获取内部 overlay（用于信号连接）
    OverlayWidget *overlay() const { return m_overlay; }

    /// @brief 反向平移模式（中键拖拽方向取反）
    void setInvertPan(bool invert) { m_invertPan = invert; }
    bool isinvertPan() const { return m_invertPan; }

    /// 显示旋转（Q1 方案 A）：放大视图随主画面一起转。
    /// 源区域/光标/内部 overlay 仍全部工作在【原视频系】（内部逻辑不动），
    /// 仅在 ContentWidget 显示前把裁剪图旋转；内部 overlay 由双向映射保证。
    void setDisplayRotation(int degrees);
    int displayRotation() const { return m_displayRotation; }

    /// 画面调节（亮度/对比度/伽马/色阶/反色）：与主画面同一 LUT，
    /// 作用于帧裁剪显示前（截图叠加融合参数独立，不受影响）。
    void setDisplayAdjust(const DisplayAdjust &adj);

    /// 当前放大视图的裁剪图（旋转+画面调节已应用，与放大视图逐位一致；
    /// §14 快照全面化用）。未收到帧时返回空图。
    QImage currentMagnifiedImage() const;

signals:
    /// 源区域变化（原视频系）：光标跟随/滚轮缩放/中键平移/旋转/切视频
    /// 全路径汇总于此（§14 放大镜来源标识框驱动主画面 overlay 同步）。
    void sourceRectChanged(const QRect &storedRect, qreal zoom);

private slots:
    void onInternalOverlayWheelZoom(int delta, QPoint videoPos);

private:
    class ContentWidget;
    ContentWidget *m_content = nullptr;

    QImage m_snapshotOriginal;  // full-frame snapshot for re-cropping
    QImage m_lastFullFrame;     // 最近一帧完整画面：暂停时平移/缩放后立即重裁刷新

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
    bool m_invertPan = false;
    int m_displayRotation = 0;   ///< 显示旋转档位（0/90/180/270 顺时针）

    void recalcSourceRect();
    /// 视频坐标源矩形 → 图像帧坐标（等比换算）。帧 = 原生分辨率时恒等；
    /// scrub 拖拽期间引擎输出降采样预览帧（宽 ≤1280）时按比例缩放。
    QRect sourceRectForImage(const QImage &img) const;

    static constexpr qreal MIN_ZOOM = 1.5;
    static constexpr qreal MAX_ZOOM = 10.0;
    static constexpr qreal ZOOM_STEP = 0.25;
};
