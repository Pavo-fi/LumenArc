/**
 * @file chartpanel.h
 * @brief QChartView 子类：折线图/时间标签/光标/缩放平移/标签管理
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
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

#include "domain/analysis_snapshot.h"

class RegionModel;
class TimelineModel;

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

    void setRegionModel(RegionModel *model);
    void setTimelineModel(TimelineModel *model);

    void setCursorTime(qint64 timeMs);
    qint64 cursorTime() const;
    void setDuration(qint64 durationMs);
    void setTimeOffset(qint64 offsetMs);
    qint64 timeOffset() const { return m_startTimeOfDayMs; }

    /// Toggle Y-axis auto-range on/off
    void setAutoYRange(bool enabled);
    bool autoYRange() const { return m_yAxisAutoRange; }

    QVector<ChartLabel> labels() const { return m_labels; }
    void setLabels(const QVector<ChartLabel> &l) { m_labels = l; updateLabelItems(); }

    /// @brief 在指定时间点添加图表标签
    void addLabelAtTime(qint64 timeMs);

signals:
    void seekRequested(qint64 timeMs);

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
    static qint64 computeTimeStep(qint64 durationMs);

    qint64 mapXToTime(qreal x) const;
    qreal mapTimeToX(qint64 timeMs) const;
    qreal clampX(qreal x) const;
    static QString formatTimeMs(qint64 ms);

    RegionModel *m_regionModel = nullptr;
    TimelineModel *m_timelineModel = nullptr;

    QChart *m_chart = nullptr;
    QValueAxis *m_axisX = nullptr;
    QValueAxis *m_axisY = nullptr;

    QGraphicsLineItem *m_cursorLine = nullptr;
    QGraphicsRectItem *m_cursorTimeBg = nullptr;
    QGraphicsSimpleTextItem *m_cursorTimeLabel = nullptr;
    bool m_draggingCursor = false;
    qint64 m_cursorTimeMs = 0;
    qint64 m_durationMs = 0;
    qint64 m_startTimeOfDayMs = 0;

    QVector<QLineSeries *> m_seriesList;
    QVector<QGraphicsSimpleTextItem *> m_timeLabelItems;
    QVector<qint64> m_labelVideoTimes;

    // Y-axis auto range
    bool m_yAxisAutoRange = true;

    // Tick marks
    bool m_showTickMarks = true;
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

    static constexpr qreal CURSOR_Z_VALUE = 100.0;
    static constexpr qreal LABEL_Z_VALUE = 50.0;
    static constexpr qreal USER_LABEL_Z_VALUE = 75.0;
};
