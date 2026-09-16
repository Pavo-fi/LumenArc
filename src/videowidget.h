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
#include <QPushButton>
#include <QRect>
#include <QPolygon>
#include <QVector>
#include <QPointer>
#include <QImage>
#include "microdiff.h"

class IVideoEngine;
class RoiModel;
class RoiModel;
class GuideLineModel;
struct DisplayAdjust;
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

    void setRegionModel(RoiModel *model);
    void setPolygonModel(RoiModel *model);
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

    // 时间戳区域框选（v1.2.1：GO 校时用）
    void beginTimestampRoiSelection(const QRectF &defaultRoi);
    void endTimestampRoiSelection();
    bool isTimestampRoiMode() const { return m_timestampRoiMode; }

    /// 显示旋转（2026-08-14 Q1 拍板方案 A）：0/90/180/270 顺时针。
    /// 覆盖物存储坐标始终是【原视频系】；旋转只改变显示与输入映射：
    /// mapFromVideo/mapToVideo 内部完成双向换算，调用点零改动。
    /// m_videoWidth/m_videoHeight 保持【原视频尺寸】（引擎上报值），
    /// 显示尺寸在映射函数内按档位推导（90/270 宽高互换）。
    void setDisplayRotation(int degrees);
    int displayRotation() const { return m_displayRotation; }

    /// 原视频尺寸（存储系坐标基准；放大镜为当前源区域尺寸）
    QSize videoSize() const { return QSize(m_videoWidth, m_videoHeight); }

    /// 放大镜来源标识框（2026-08-14 §14 Q1 拍板）：原视频系源区域，
    /// 空矩形 = 隐藏。仅绘制、不参与命中检测；放大镜 dock 开即显示、关即清。
    /// zoom 用于倍率徽章（≤0 不画徽章）。绘制样式见 FrameAnnotation::drawMagnifierIndicator（v1.17.0 C0 迁出 widgets 层）。
    void setMagnifierRect(const QRect &storedRect, qreal zoom);
    QRect magnifierRect() const { return m_magnifierRect; }
    qreal magnifierZoom() const { return m_magnifierZoom; }
    /// 标识框的 widget 显示坐标（含旋转映射；测试断言用，无效时返回空）
    QRect magnifierRectWidget() const;


signals:
    void timestampRoiConfirmed(const QRectF &normalized);
    void timestampRoiCancelled();
    /// 拖拽松开/点「确认」：框选就绪（归一化 ROI；无效=未框出）
    /// v1.2.x UX：拖拽结束即交回校时窗口，由窗口提供「确认并开始校时」
    void timestampRoiReady(const QRectF &normalized);
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

    // 时间戳框选辅助
    QRectF normalizedRoi(const QRect &widgetRect) const;
    void placeRoiButtons();

    // 坐标映射
    QPoint mapToVideo(const QPoint &widgetPos) const;
    QPoint mapFromVideo(const QPoint &videoPos) const;
    QRect mapToVideo(const QRect &widgetRect) const;
    QRect mapFromVideo(const QRect &videoRect) const;

    // 旋转双向换算（原视频系 ↔ 旋转后显示系；90° 步进精确整数映射）
    QPoint storedToDisplay(const QPoint &stored) const;
    QPoint displayToStored(const QPoint &display) const;
    /// 位移向量的显示系→存储系换算（无 -1 偏移/无需尺寸；放大镜平移用）
    QPoint displayDeltaToStored(const QPoint &delta) const;
    /// 旋转后显示系尺寸（90/270 宽高互换）
    QSize displayVideoSize() const;

    int m_displayRotation = 0;   ///< 顺时针档位：0/90/180/270

    // 放大镜来源标识框（§14：仅绘制，不参与命中检测）
    QRect m_magnifierRect;       ///< 原视频系；空 = 隐藏
    qreal m_magnifierZoom = 0.0;

    QRect m_videoDisplayRect;

    QPointer<RoiModel> m_regionModel;
    QPointer<RoiModel> m_polygonModel;
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

    // 时间戳框选模式（v1.2.1）
    bool m_timestampRoiMode = false;
    QRect m_timestampRoiRect;      // widget 坐标（画框中）
    QPushButton *m_roiConfirmBtn = nullptr;
    QPushButton *m_roiSkipBtn = nullptr;

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
    void setRegionModel(RoiModel *model);
    void setPolygonModel(RoiModel *model);
    void setGuideLineModel(GuideLineModel *model);
    OverlayWidget *overlay() const { return m_overlay; }

    void setSnapshot(const QImage &snapshot, int brightness = 0, int contrast = 0, int opacity = 0);
    void clearSnapshot();
    /// 时间戳区域框选（转发 OverlayWidget）
    void beginTimestampRoiSelection(const QRectF &defaultRoi);
    void endTimestampRoiSelection();
    /// @brief Clear the displayed video frame (back to "no video" placeholder).
    void clearFrame();
    /// @brief 大视频加载期间的“导入中…”提示（首帧到达后由调用方关闭）
    void setLoading(bool on);
    void grabFrameSnapshot();
    const QImage& currentFrame() const { return m_frameImage; }

    /// 播放画面调节（2026-08-14 播放选项包）：亮度/对比度/反色/色阶/伽马
    /// 作用于显示链路，预计算 256 级 LUT（displayadjust.h），恒等时零开销。
    /// 仅影响显示与截图快照（所见即所得）；分析/ROI/语谱仍走原始帧。
    /// 暂停时拖动滑杆也实时预览（从保留的原始帧重算显示帧）。
    void setDisplayAdjust(const DisplayAdjust &adj);
    bool displayAdjustActive() const { return !m_displayLut.isEmpty(); }

    /// 显示旋转（Q1 方案 A）：0/90/180/270 顺时针。显示链：
    ///   原始帧 → 旋转(QImage::transformed) → 亮度/对比度 LUT → m_frameImage
    /// 覆盖物随转由 OverlayWidget 双向映射保证；分析永远走原始帧。
    /// 证据快照（currentFrame/grabFrameSnapshot 链路）所见即所得。
    void setDisplayRotation(int degrees);
    int displayRotation() const { return m_displayRotation; }
    /// 未调节/未旋转的原始帧（放大镜首帧等需要【原视频系】像素的场景）
    const QImage& rawFrame() const { return m_rawFrameImage; }

    /// 微变分析显示环节（2026-09-10）。实际变换顺序：
    ///   原始帧 → 【微变假彩色叠加】 → 旋转 → 调节 LUT → m_frameImage
    /// （微变在旋转前：ROI/基准均定义在原视频坐标系，先叠加后旋转不下错位）
    /// 计算只用亮度，显示保留原彩画面（alpha=0 处完全保留）。
    /// 仅影响显示与截图快照；分析/ROI/导出证据走原始帧（取证红线）。
    /// @param p 参数；p.enabled=false 时零开销直通
    void setMicroDiffParams(const MicroDiffParams &p);
    /// @brief 下发灰度基准（采集基准动作产出）
    /// @return 尺寸不合法时 false
    bool setMicroDiffBaseline(const QImage &gray, double noiseFloor);
    void clearMicroDiffBaseline();
    /// @brief seek/跳转后清空时域环缓冲（避免跨段平均产生假变化）
    void resetMicroDiffTemporal();
    bool microDiffHasBaseline() const { return m_microDiffState.hasBaseline(); }
    /// @brief 时域窗内已有帧数（< temporalFrames 说明还在预热）
    int microDiffWarmupFrames() const { return m_microDiffState.framesInWindow(); }
    /// @brief 当前噪声基底（基准采集时自动标定）
    double microDiffNoiseFloor() const { return m_microDiffState.noiseFloor(); }
    /// @brief 当前帧算好的微变数据（纯数据；供放大镜/钉图/全屏共享重渲染）
    /// @note 指针生命周期到下一帧到达为止；renderMicroDiff 不推进时域环缓冲
    const MicroDiffFrame &microDiffFrame() const { return m_microDiffFrame; }
    /// @brief 把当前帧的微变叠加应用到给定原彩帧（纯渲染，不推进时域环缓冲）。
    /// 放大镜/钉图/副屏全屏用同一份 MicroDiffFrame 各自重渲染 → "微变局部放大"。
    QImage applyMicroDiffTo(const QImage &raw) const;
    const MicroDiffParams& microDiffParams() const { return m_microDiffParams; }

signals:
    void frameSnapshotReady(const QImage &image);
    void timestampRoiConfirmed(const QRectF &normalized);
    void timestampRoiCancelled();
    void timestampRoiReady(const QRectF &normalized);

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
    int m_loadingAngle = 0;   // 加载旋转动画角度
    QTimer *m_loadingTimer = nullptr;

    // Snapshot fusion data
    QImage m_snapshot;
    QImage m_adjustedSnapshot;  // cached adjusted snapshot
    int m_snapshotBrightness = 0;
    int m_snapshotContrast = 0;
    int m_snapshotOpacity = 0;
    int m_cachedBrightness = INT_MIN;
    int m_cachedContrast = -999;

    // 播放画面调节 LUT（空 = 恒等/关闭）
    QByteArray m_displayLut;
    QImage m_rawFrameImage;      ///< 未调节的原始帧（调节参数变化时重建显示帧）
    int m_displayRotation = 0;   ///< 显示旋转档位（0/90/180/270 顺时针）
    int m_cachedRotation = -1;   ///< 截图叠加缓存的旋转档位（缓存键一部分）
    void rebuildAdjustedFrame(); ///< 按当前旋转+LUT+微变 从原始帧重建 m_frameImage

    // 微变分析（显示链末级）
    MicroDiffParams m_microDiffParams;
    MicroDiffDisplayState m_microDiffState;
    MicroDiffFrame m_microDiffFrame;   ///< 当前帧计算结果（每帧只算一次）
};
