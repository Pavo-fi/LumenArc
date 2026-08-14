/**
 * @file chartpanel.h
 * @brief QChartView 子类：折线图/时间标签/光标/缩放平移/标签管理
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include <QtCharts/QChartView>
#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QGraphicsLineItem>
#include <QGraphicsSimpleTextItem>
#include <QVector>
#include <QTime>
#include <QWheelEvent>
#include <QPushButton>
#include <QElapsedTimer>

#include "domain/analysis_snapshot.h"

class RoiModel;
class RoiModel;
class TimelineModel;
class QLabel;

/**
 * @brief QChartView subclass with custom time labels, draggable cursor,
 * viewport-aware decimation, Y-axis auto-range, and interactive zoom/pan.
 */
class ChartPanel : public QChartView
{
    Q_OBJECT

public:
    explicit ChartPanel(QWidget *parent = nullptr);
    ~ChartPanel();

    void setRegionModel(RoiModel *model);
    void setPolygonModel(RoiModel *model);
    void setTimelineModel(TimelineModel *model);

    void setCursorTime(qint64 timeMs);
    qint64 cursorTime() const;
    void setDuration(qint64 durationMs);
    /// 设置视频帧时长（拖拽匀速化量化用；0=未知，退化为原始直通）
    void setFrameDuration(qint64 frameMs) { m_frameMs = frameMs; }
    /// 设置校时模型（SSOT 由 VideoState 持有，此处仅为渲染派生值）
    void setCalibration(const TimeCalibration &cal);
    TimeCalibration calibration() const { return m_calibration; }
    bool isDraggingCursor() const { return m_draggingCursor; }
    QRectF plotArea() const;
    QValueAxis *axisX() const { return m_axisX; }

    /// Toggle Y-axis auto-range on/off
    void setAutoYRange(bool enabled);
    bool autoYRange() const { return m_yAxisAutoRange; }

    /// 离屏矢量重渲染（2026-08-14 §14 快照全面化）：把图表按目标尺寸重新布局
    /// 并用 CPU 矢量绘制（不经 widget grab——dock 内 resize+grab 实测不可靠，
    /// 曲线/坐标出不来）。曲线/标签/光标/辅助线全部随新布局重排，清晰不糊。
    /// 返回目标尺寸图像（BgPanel 底）；无图表场景时返回空图。
    QImage renderToImage(const QSize &targetSize);

    QVector<ChartLabel> labels() const { return m_labels; }
    void setLabels(const QVector<ChartLabel> &l) { m_labels = l; updateLabelItems(); }
    /// 当前可见折线数（有数据的亮度曲线 + 音量曲线）——标签文字自动隐藏判定用
    int visibleSeriesCount() const;

    /// @brief 在指定时间点添加图表标签
    void addLabelAtTime(qint64 timeMs);

    // A/B region playback
    void setPointA(qint64 timeMs);
    void setPointB(qint64 timeMs);
    void clearAB();
    bool isABRegionSet() const { return m_abPointA >= 0 && m_abPointB >= 0; }
    bool isABLoop() const { return m_abLoop; }
    void setABLoop(bool loop);
    qint64 abPointA() const { return m_abPointA; }
    qint64 abPointB() const { return m_abPointB; }

    // Chart guide lines
    void addHorizontalGuideLine(qreal yValue, const QColor &color = QColor(255, 255, 0, 180));
    void addVerticalGuideLine(qreal xTimeMs, const QColor &color = QColor(0, 255, 255, 180));
    void removeChartGuideLine(int index);
    void clearChartGuideLines();
    int chartGuideLineCount() const { return m_chartGuideLines.size(); }
    /// 图表辅助线的可序列化数据（逐视频状态保存/恢复用）
    QVector<ChartGuideData> chartGuideLinesData() const;
    void setChartGuideLinesData(const QVector<ChartGuideData> &lines);

    /// 标签标记点悬浮窗（LabelDotItem 调用）：250ms 延迟后显示在点右上方
    void scheduleLabelTip(const QColor &color, const QString &text,
                          const QString &timeStr, const QPoint &anchorGlobal);
    /// 立即隐藏悬浮窗（取消未触发的延迟显示）
    void hideLabelTip();

signals:
    void seekRequested(qint64 timeMs);
    /// v0.3: Emitted when X-axis range changes (for spectrogram sync)
    void xAxisRangeChanged(qreal xMin, qreal xMax);
    /// Emitted when chart plot area changes (for spectrogram axis alignment)
    void plotAreaUpdated(QRectF plotArea);
    /// Emitted when A/B region changes
    void abRegionChanged();
    /// Emitted when cursor drag ends (Scrub → Paused transition)
    void scrubEnded();

public slots:
    void onRegionsChanged();
    void onDataReplaced();
    void onDataCleared();

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void rebuildSeries();
    void updateCursorPosition();
    void updateTimeLabels();
    void updateTimeLabelPositions();
    void updateYAxisRange();
    void updateLabelItems();
    void fitAll();
    void fitAllX();
    static qint64 computeTimeStep(qint64 durationMs, int plotWidthPx = 800);

    qint64 mapXToTime(qreal x) const;
    qreal mapTimeToX(qint64 timeMs) const;
    qreal clampX(qreal x) const;
    static QString formatTimeMs(qint64 ms);
    static QString formatTimeHMS(qint64 ms);
    /// 显示域换算：dateKnown=false → 旧日内偏移（行为不变）；true → 北京时间
    qint64 displayMsOf(qint64 streamMs) const;
    /// 显示域 → 流内（刻度对齐逆运算）
    qint64 streamMsFromDisplay(qint64 displayMs) const;
    /// 显示域格式化：dateKnown 时跨天带日期；否则 HH:MM:SS
    QString formatDisplayTime(qint64 displayMs) const;
    /// 光标用完整格式：dateKnown 时 yyyy-MM-dd HH:mm:ss.zzz
    QString formatDisplayTimeFull(qint64 displayMs) const;

    RoiModel *m_regionModel = nullptr;
    RoiModel *m_polygonModel = nullptr;
    TimelineModel *m_timelineModel = nullptr;

    QChart *m_chart = nullptr;
    QValueAxis *m_axisX = nullptr;
    QValueAxis *m_axisY = nullptr;

    QGraphicsLineItem *m_cursorLine = nullptr;
    QGraphicsRectItem *m_cursorTimeBg = nullptr;
    QGraphicsSimpleTextItem *m_cursorTimeLabel = nullptr;
    QGraphicsSimpleTextItem *m_cursorDataLabel = nullptr;
    bool m_draggingCursor = false;
    qint64 m_cursorTimeMs = 0;

    // --- 拖拽匀速化（第一层：输入侧速度自适应帧网格量化） ---
    // 估计目标速度（时间轴 ms / 墙钟 ms），据此选取帧步长 n，并把发出的
    // seek 目标量化到以拖拽起点为锚的 n·frameMs 网格——慢拖逐帧均匀推进，
    // 快拖等距跳帧，消除鼠标事件间隔抖动导致的目标序列不均匀。
    qint64 m_frameMs = 0;            // 视频帧时长（0=未知，不量化）
    QElapsedTimer m_dragWallClock;   // 拖拽墙钟（速度估计）
    qint64 m_dragLastWallMs = -1;    // 上次 mouseMove 的墙钟
    qint64 m_dragLastRawMs = -1;     // 上次 mouseMove 的原始目标
    double m_dragVelocity = 0.0;     // 目标速度 EMA（带符号，ms/ms）
    qint64 m_dragAnchorMs = 0;       // 量化网格锚点（拖拽起点）
    qint64 m_dragLastEmittedMs = -1; // 上次实际发出的量化目标（去重）
    qint64 m_dragQuantum = 0;        // 当前量化步长（步长切换时平移锚点保持相位连续）
    qint64 quantizeDragTarget(qint64 t);
    static constexpr qint64 DRAG_DISPLAY_CADENCE_MS = 25; // 显示节拍（与引擎节拍闸一致）
    qint64 m_durationMs = 0;
    TimeCalibration m_calibration;      // 校时模型（渲染派生值，SSOT 在 VideoState）
    bool m_spanCrossDay = false;        // 当前可视范围跨天（updateTimeLabels 刷新）

    QVector<QLineSeries *> m_seriesList;
    QLineSeries *m_volumeSeries = nullptr;       // v0.3: Volume line
    QValueAxis *m_axisYVolume = nullptr;         // v0.3: Right Y-axis for volume
    QVector<QGraphicsSimpleTextItem *> m_timeLabelItems;
    QVector<qint64> m_labelVideoTimes;

    // Y-axis auto range
    bool m_yAxisAutoRange = true;

    // Tick marks
    bool m_showTickMarks = true;
    /// 标签文字显示模式：0=自动（折线 ≥2 时隐藏文字只留标记点），1=强制显示，2=强制隐藏
    int m_labelsTextMode = 0;
    // 标签标记点悬浮窗（自控 QLabel，替代 QToolTip——QToolTip 显示时长/隐藏时序不可控）
    QLabel *m_labelTip = nullptr;
    QTimer *m_labelTipTimer = nullptr;
    QColor m_labelTipColor;
    QString m_labelTipText;
    QString m_labelTipTime;
    QPoint m_labelTipAnchor;
    void showLabelTipNow();
    QVector<QGraphicsLineItem *> m_tickMarkItems;
    QGraphicsSimpleTextItem *m_startTimeLabel = nullptr;
    QGraphicsSimpleTextItem *m_endTimeLabel = nullptr;

    // Pan support (middle-mouse or Shift+LMB)
    bool m_isPanning = false;
    QPoint m_panStartPos;
    qreal m_panStartMin = 0;
    qreal m_panStartMax = 0;

    // User chart labels
    QVector<ChartLabel> m_labels;
    QVector<QGraphicsItem *> m_labelGraphicsItems;  // owned items for cleanup
    QVector<QPair<QRectF, qint64>> m_labelHitAreas; // clickable rect → timeMs

    // A/B region playback
    qint64 m_abPointA = -1;
    qint64 m_abPointB = -1;
    bool m_abLoop = false;
    QGraphicsLineItem *m_lineA = nullptr;
    QGraphicsLineItem *m_lineB = nullptr;
    QGraphicsSimpleTextItem *m_labelAText = nullptr;
    QGraphicsSimpleTextItem *m_labelBText = nullptr;
    QGraphicsRectItem *m_abHighlight = nullptr;
    void updateABMarkers();
    void zoomToABRegion();

    // Mapping from series index to data index (by roiId matching)
    struct SeriesMapping {
        DataEntry::Type type = DataEntry::Rect;
        int roiId = -1;
        int dataIndex = -1;  // index into snapshot.values[], -1 = no data
    };
    QVector<SeriesMapping> m_seriesMapping;

    // Chart guide lines (horizontal at Y value, vertical at X time)
    struct ChartGuideLine {
        enum Orientation { Horizontal, Vertical };
        Orientation orientation;
        qreal value;  // Y value for horizontal, X time (ms) for vertical
        QColor color;
        QGraphicsLineItem *lineItem = nullptr;
        QGraphicsSimpleTextItem *labelItem = nullptr;      // 水平：左标签（亮度）；垂直：时间
        QGraphicsSimpleTextItem *labelItemRight = nullptr; // 水平：右标签（响度，随拖动联动）
    };
    QVector<ChartGuideLine> m_chartGuideLines;
    QVector<QGraphicsSimpleTextItem *> m_chartGuideLineDeltaLabels; // delta labels between horizontal guides
    int m_hoveredGuideLine = -1;
    int m_draggingGuideLine = -1;
    qreal m_dragGuideLineStartValue = 0;
    void drawChartGuideLines();
    int hitTestChartGuideLine(const QPoint &pos) const;

    // Guard flag: prevents onDataReplaced() from calling rebuildSeries() recursively
    bool m_rebuilding = false;

    static constexpr qreal CURSOR_Z_VALUE = 100.0;
    static constexpr qreal LABEL_Z_VALUE = 50.0;
    static constexpr qreal USER_LABEL_Z_VALUE = 75.0;
};
