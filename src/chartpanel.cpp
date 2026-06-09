/**
 * @file chartpanel.cpp
 * @brief 折线图面板实现：时间标签/光标/缩放平移/刻度尺/标签管理
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "chartpanel.h"
#include "domain/region_model.h"
#include "domain/timeline_model.h"
#include "i18n.h"

#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QDebug>
#include <QMenu>
#include <QAction>
#include <QInputDialog>
#include <QColorDialog>
#include <QDialog>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPushButton>
#include <QLabel>
#include <cmath>

/// @brief 构造图表：配置坐标轴/光标线/右键菜单
ChartPanel::ChartPanel(QWidget *parent)
    : QChartView(parent)
{
    setRenderHint(QPainter::Antialiasing);
    setRubberBand(QChartView::NoRubberBand);
    setDragMode(QGraphicsView::NoDrag);

    m_chart = new QChart();
    m_chart->setTitle(lang("亮度变化曲线", "Luminance Over Time"));
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);
    m_chart->setBackgroundBrush(QBrush(QColor(245, 245, 245)));

    connect(m_chart, &QChart::plotAreaChanged, this, &ChartPanel::updateTimeLabelPositions);
    connect(m_chart, &QChart::plotAreaChanged, this, &ChartPanel::updateLabelItems);

    m_axisX = new QValueAxis();
    m_axisX->setTitleText("Time");
    m_axisX->setLabelFormat("%.0f");
    m_axisX->setLabelsVisible(false); // we draw custom HH:MM:SS labels manually

    m_axisY = new QValueAxis();
    m_axisY->setTitleText("Brightness (Y avg)");
    m_axisY->setRange(0, 255);

    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    setChart(m_chart);

    m_cursorLine = new QGraphicsLineItem(m_chart);
    m_cursorLine->setZValue(CURSOR_Z_VALUE);
    QPen cursorPen(QColor(0, 255, 255)); // cyan
    cursorPen.setWidth(2);
    cursorPen.setStyle(Qt::DashLine);
    m_cursorLine->setPen(cursorPen);
    m_cursorLine->setVisible(false);

    // Time label above cursor line
    m_cursorTimeBg = new QGraphicsRectItem(m_chart);
    m_cursorTimeBg->setZValue(CURSOR_Z_VALUE);
    m_cursorTimeBg->setBrush(QBrush(QColor(30, 30, 30, 200)));
    m_cursorTimeBg->setPen(Qt::NoPen);
    m_cursorTimeBg->setVisible(false);

    m_cursorTimeLabel = new QGraphicsSimpleTextItem(m_chart);
    m_cursorTimeLabel->setZValue(CURSOR_Z_VALUE + 1);
    m_cursorTimeLabel->setFont(fontMono(9));
    m_cursorTimeLabel->setBrush(QBrush(QColor(200, 220, 255)));
    m_cursorTimeLabel->setVisible(false);

    setContextMenuPolicy(Qt::CustomContextMenu);
    connect(this, &QWidget::customContextMenuRequested, this, [this](const QPoint &pos) {
        QMenu menu;
        QAction *fitAllAction = menu.addAction(lang("全部适应", "Fit All"));
        QAction *fitXAction = menu.addAction(lang("X轴适应（全时长）", "Fit X (full duration)"));
        menu.addSeparator();
        QAction *tickAction = menu.addAction(m_showTickMarks ?
            lang("隐藏刻度", "Hide Tick Marks") :
            lang("显示刻度", "Show Tick Marks"));
        QAction *autoYAction = menu.addAction(m_yAxisAutoRange ?
            lang("禁用 Y轴自动范围", "Disable Auto Y") :
            lang("启用 Y轴自动范围", "Enable Auto Y"));
        QAction *chosen = menu.exec(mapToGlobal(pos));
        if (chosen == fitAllAction) {
            fitAll();
        } else if (chosen == fitXAction) {
            fitAllX();
        } else if (chosen == tickAction) {
            m_showTickMarks = !m_showTickMarks;
            updateTimeLabels();
            updateTimeLabelPositions();
        } else if (chosen == autoYAction) {
            setAutoYRange(!m_yAxisAutoRange);
            if (!m_timelineModel || m_timelineModel->snapshot().isEmpty())
                return;
            if (m_yAxisAutoRange)
                updateYAxisRange();
        }
    });
}

ChartPanel::~ChartPanel()
{
    for (auto *item : m_timeLabelItems) {
        if (item->scene()) item->scene()->removeItem(item);
        delete item;
    }
    m_timeLabelItems.clear();
    m_labelVideoTimes.clear();

    for (auto *item : m_tickMarkItems) {
        if (item->scene()) item->scene()->removeItem(item);
        delete item;
    }
    m_tickMarkItems.clear();

    if (m_startTimeLabel) {
        if (m_startTimeLabel->scene()) m_startTimeLabel->scene()->removeItem(m_startTimeLabel);
        delete m_startTimeLabel;
        m_startTimeLabel = nullptr;
    }
    if (m_endTimeLabel) {
        if (m_endTimeLabel->scene()) m_endTimeLabel->scene()->removeItem(m_endTimeLabel);
        delete m_endTimeLabel;
        m_endTimeLabel = nullptr;
    }

    for (auto *item : m_labelGraphicsItems) {
        if (item->scene()) item->scene()->removeItem(item);
        delete item;
    }
    m_labelGraphicsItems.clear();
}

void ChartPanel::setRegionModel(RegionModel *model)
{
    if (m_regionModel) {
        disconnect(m_regionModel, nullptr, this, nullptr);
    }
    m_regionModel = model;
    if (m_regionModel) {
        connect(m_regionModel, &RegionModel::regionsChanged,
                this, &ChartPanel::onRegionsChanged);
        rebuildSeries();
    }
}

void ChartPanel::setTimelineModel(TimelineModel *model)
{
    if (m_timelineModel) {
        disconnect(m_timelineModel, nullptr, this, nullptr);
    }
    m_timelineModel = model;
    if (m_timelineModel) {
        connect(m_timelineModel, &TimelineModel::dataReplaced,
                this, &ChartPanel::onDataReplaced);
        connect(m_timelineModel, &TimelineModel::dataCleared,
                this, &ChartPanel::onDataCleared);
    }
}

void ChartPanel::setCursorTime(qint64 timeMs)
{
    m_cursorTimeMs = timeMs;
    updateCursorPosition();
}

qint64 ChartPanel::cursorTime() const
{
    return m_cursorTimeMs;
}

void ChartPanel::setDuration(qint64 durationMs)
{
    m_durationMs = durationMs;
    if (m_durationMs > 0) {
        m_axisX->setRange(0, m_durationMs);
    }
    updateTimeLabels();
}

void ChartPanel::setTimeOffset(qint64 offsetMs)
{
    m_startTimeOfDayMs = offsetMs;
    updateTimeLabels();
}

void ChartPanel::setAutoYRange(bool enabled)
{
    m_yAxisAutoRange = enabled;
    if (enabled) {
        updateYAxisRange();
    } else {
        m_axisY->setRange(0, 255);
    }
}

void ChartPanel::onRegionsChanged()
{
    rebuildSeries();
}

void ChartPanel::onDataReplaced()
{
    if (!m_timelineModel)
        return;

    AnalysisSnapshot snapshot = m_timelineModel->snapshot();
    if (snapshot.isEmpty())
        return;

    if (m_durationMs > 0) {
        m_axisX->setRange(0, m_durationMs);
    } else {
        qreal xMax = qMax(qreal(snapshot.timestamps.last()), qreal(1000));
        m_axisX->setRange(0, xMax);
    }

    // Use direct synchronous update for series data.
    // The caller ensures no processEvents() or nested event loops
    // interfere with chart rendering.
    qint64 xMin = static_cast<qint64>(m_axisX->min());
    qint64 xMax = static_cast<qint64>(m_axisX->max());

    for (int i = 0; i < m_seriesList.size() && i < snapshot.regionCount(); ++i) {
        QLineSeries *series = m_seriesList[i];
        QVector<QPointF> pts = snapshot.pointsForViewport(i, xMin, xMax, 5000);
        if (pts.isEmpty()) {
            series->clear();
        } else {
            for (auto &pt : pts) {
                if (qIsNaN(pt.y()) || qIsInf(pt.y()))
                    pt.setY(0.0);
                else if (pt.y() < 0.0)
                    pt.setY(0.0);
                else if (pt.y() > 255.0)
                    pt.setY(255.0);
            }
            series->replace(pts);
        }
    }

    updateYAxisRange();
    updateTimeLabels();
    updateTimeLabelPositions();
    updateLabelItems();
    updateCursorPosition();
}

void ChartPanel::onDataCleared()
{
    for (auto *series : m_seriesList) {
        series->clear();
    }
    if (m_durationMs > 0) {
        m_axisX->setRange(0, m_durationMs);
    } else {
        m_axisX->setRange(0, 1000);
    }
    m_axisY->setRange(0, 255);
    updateTimeLabels();
    updateTimeLabelPositions();
    updateLabelItems();
}

void ChartPanel::updateYAxisRange()
{
    if (!m_yAxisAutoRange || m_seriesList.isEmpty())
        return;

    // Find data min/max across all visible series points in current X viewport
    qreal yMin = 255.0;
    qreal yMax = 0.0;
    bool hasData = false;

    for (const auto *series : m_seriesList) {
        const auto points = series->pointsVector();
        for (const auto &pt : points) {
            qreal y = pt.y();
            if (!qIsNaN(y) && !qIsInf(y)) {
                if (y < yMin) yMin = y;
                if (y > yMax) yMax = y;
                hasData = true;
            }
        }
    }

    if (hasData && yMax > yMin) {
        qreal margin = (yMax - yMin) * 0.05;
        if (margin < 0.5) margin = 0.5;
        qreal newMin = qMax(0.0, yMin - margin);
        qreal newMax = qMin(255.0, yMax + margin);
        // Avoid flat range
        if (newMax - newMin < 1.0) {
            qreal center = (newMin + newMax) / 2.0;
            newMin = qMax(0.0, center - 0.5);
            newMax = qMin(255.0, center + 0.5);
        }
        m_axisY->setRange(newMin, newMax);
    }
}

void ChartPanel::updateLabelItems()
{
    for (auto *item : m_labelGraphicsItems) {
        if (item->scene()) item->scene()->removeItem(item);
        delete item;
    }
    m_labelGraphicsItems.clear();
    m_labelHitAreas.clear();

    for (const auto &label : m_labels) {
        qreal x = mapTimeToX(label.timeMs);
        QRectF plotArea = m_chart->plotArea();
        if (x < plotArea.left() || x > plotArea.right())
            continue;

        auto *line = new QGraphicsLineItem(m_chart);
        QPen pen(label.color);
        pen.setWidth(2);
        line->setPen(pen);
        line->setLine(x, plotArea.top() + 10, x, plotArea.top() + 30);
        line->setZValue(USER_LABEL_Z_VALUE);
        m_labelGraphicsItems.append(line);

        auto *textItem = new QGraphicsSimpleTextItem(label.text, m_chart);
        textItem->setBrush(QBrush(label.color));
        textItem->setFont(fontSans(9));
        textItem->setZValue(USER_LABEL_Z_VALUE);
        textItem->setPos(x + 4, plotArea.top() + 2);
        m_labelGraphicsItems.append(textItem);

        QRectF hitRect(x - 4, plotArea.top(), textItem->boundingRect().width() + 12, 32);
        m_labelHitAreas.append({hitRect, label.timeMs});
    }
}

void ChartPanel::rebuildSeries()
{
    for (auto *series : m_seriesList) {
        m_chart->removeSeries(series);
        delete series;
    }
    m_seriesList.clear();

    if (!m_regionModel)
        return;

    int count = m_regionModel->regionCount();
    for (int i = 0; i < count; ++i) {
        auto *series = new QLineSeries();
        series->setName(QString("Region %1").arg(i + 1));
        QPen pen(RegionModel::regionColor(i));
        pen.setWidth(2);
        series->setPen(pen);

        m_chart->addSeries(series);
        series->attachAxis(m_axisX);
        series->attachAxis(m_axisY);
        m_seriesList.append(series);
    }

    if (m_timelineModel && !m_timelineModel->snapshot().isEmpty())
        onDataReplaced();
}

/// @brief 计算时间步长：目标6-8个标签可见
qint64 ChartPanel::computeTimeStep(qint64 durationMs)
{
    if (durationMs <= 0)
        return 60000;

    // Aim for roughly 6-8 labels across the duration
    const qint64 candidates[] = {
        1000,     // 1s
        2000,     // 2s
        5000,     // 5s
        10000,    // 10s
        15000,    // 15s
        30000,    // 30s
        60000,    // 1min
        120000,   // 2min
        300000,   // 5min
        600000,   // 10min
        900000,   // 15min
        1800000,  // 30min
        3600000,  // 1h
        7200000,  // 2h
        21600000  // 6h
    };

    qint64 target = durationMs / 6;
    for (qint64 step : candidates) {
        if (step >= target)
            return step;
    }
    return candidates[sizeof(candidates) / sizeof(candidates[0]) - 1];
}

/**
 * @brief 重建时间标签和刻度尺：主刻度+次刻度+起止标注
 */
void ChartPanel::updateTimeLabels()
{
    // Remove old custom labels
    for (auto *item : m_timeLabelItems) {
        if (item->scene()) item->scene()->removeItem(item);
        delete item;
    }
    m_timeLabelItems.clear();
    m_labelVideoTimes.clear();

    // Remove old tick marks
    for (auto *item : m_tickMarkItems) {
        if (item->scene()) item->scene()->removeItem(item);
        delete item;
    }
    m_tickMarkItems.clear();

    // Remove old start/end labels
    if (m_startTimeLabel) {
        if (m_startTimeLabel->scene()) m_startTimeLabel->scene()->removeItem(m_startTimeLabel);
        delete m_startTimeLabel;
        m_startTimeLabel = nullptr;
    }
    if (m_endTimeLabel) {
        if (m_endTimeLabel->scene()) m_endTimeLabel->scene()->removeItem(m_endTimeLabel);
        delete m_endTimeLabel;
        m_endTimeLabel = nullptr;
    }

    QRectF plotArea = m_chart->plotArea();
    if (plotArea.width() < 10 || !m_axisX)
        return;

    qreal visibleMin = m_axisX->min();
    qreal visibleMax = m_axisX->max();
    qint64 visibleDurationMs = static_cast<qint64>(visibleMax - visibleMin);

    if (visibleDurationMs <= 0)
        return;

    qint64 step = computeTimeStep(visibleDurationMs);
    qint64 offset = m_startTimeOfDayMs;
    qreal bottom = plotArea.bottom();

    // Align first label to step boundary
    qint64 visualStartWithOffset = static_cast<qint64>(visibleMin) + offset;
    qint64 firstLabel = (visualStartWithOffset / step) * step;
    if (firstLabel > visualStartWithOffset)
        firstLabel -= step;

    qint64 lastLabel = static_cast<qint64>(visibleMax) + offset;

    // --- Major tick labels (regular time labels) ---
    for (qint64 tReal = firstLabel; tReal <= lastLabel; tReal += step) {
        qint64 tVideo = tReal - offset;
        if (tVideo < static_cast<qint64>(visibleMin))
            continue;
        if (tVideo > static_cast<qint64>(visibleMax))
            break;

        QString text = formatTimeMs(tReal);
        auto *item = new QGraphicsSimpleTextItem(text, m_chart);
        item->setFont(fontSans(9));
        item->setBrush(QBrush(Qt::black));
        item->setZValue(LABEL_Z_VALUE);
        m_timeLabelItems.append(item);
        m_labelVideoTimes.append(tVideo);

        // Major tick mark
        if (m_showTickMarks) {
            auto *tick = new QGraphicsLineItem(m_chart);
            tick->setPen(QPen(QColor(120, 120, 120), 2));
            qreal x = mapTimeToX(tVideo);
            tick->setLine(x, bottom, x, bottom + 10);
            tick->setZValue(LABEL_Z_VALUE - 1);
            m_tickMarkItems.append(tick);
        }
    }

    // --- Minor tick marks (ruler-style sub-divisions) ---
    if (m_showTickMarks && step > 1000) {
        int minorDivisions = 4;
        qint64 minorStep = step / minorDivisions;
        if (minorStep >= 500) {
            // Draw minor ticks between each pair of major ticks
            for (qint64 tReal = firstLabel; tReal < lastLabel; tReal += step) {
                for (int m = 1; m < minorDivisions; ++m) {
                    qint64 tMinor = tReal + m * minorStep;
                    qint64 tMinorVideo = tMinor - offset;
                    if (tMinorVideo < static_cast<qint64>(visibleMin))
                        continue;
                    if (tMinorVideo > static_cast<qint64>(visibleMax))
                        break;

                    auto *tick = new QGraphicsLineItem(m_chart);
                    tick->setPen(QPen(QColor(180, 180, 180), 1));
                    qreal x = mapTimeToX(tMinorVideo);
                    tick->setLine(x, bottom, x, bottom + 5);
                    tick->setZValue(LABEL_Z_VALUE - 2);
                    m_tickMarkItems.append(tick);
                }
            }
        }
    }

    // --- Start time label at left edge (black, bold, larger) ---
    {
        qint64 startReal = static_cast<qint64>(visibleMin) + offset;
        QString text = formatTimeMs(startReal);
        auto *item = new QGraphicsSimpleTextItem(text, m_chart);
        item->setFont(fontMono(10, QFont::Bold));
        item->setBrush(QBrush(Qt::black));
        item->setZValue(LABEL_Z_VALUE + 2);
        m_startTimeLabel = item;

        if (m_showTickMarks) {
            auto *tick = new QGraphicsLineItem(m_chart);
            tick->setPen(QPen(QColor(80, 80, 80), 2));
            tick->setLine(plotArea.left(), bottom, plotArea.left(), bottom + 12);
            tick->setZValue(LABEL_Z_VALUE);
            m_tickMarkItems.append(tick);
        }
    }

    // --- End time label at right edge (black, bold, larger) ---
    {
        qint64 endReal = static_cast<qint64>(visibleMax) + offset;
        QString text = formatTimeMs(endReal);
        auto *item = new QGraphicsSimpleTextItem(text, m_chart);
        item->setFont(fontMono(10, QFont::Bold));
        item->setBrush(QBrush(Qt::black));
        item->setZValue(LABEL_Z_VALUE + 2);
        m_endTimeLabel = item;

        if (m_showTickMarks) {
            auto *tick = new QGraphicsLineItem(m_chart);
            tick->setPen(QPen(QColor(80, 80, 80), 2));
            tick->setLine(plotArea.right(), bottom, plotArea.right(), bottom + 12);
            tick->setZValue(LABEL_Z_VALUE);
            m_tickMarkItems.append(tick);
        }
    }

    updateTimeLabelPositions();
}

void ChartPanel::updateTimeLabelPositions()
{
    QRectF plotArea = m_chart->plotArea();
    qreal bottom = plotArea.bottom();

    // Collect start/end label rects for collision detection
    QRectF startRect, endRect;

    if (m_startTimeLabel) {
        QRectF textRect = m_startTimeLabel->boundingRect();
        m_startTimeLabel->setPos(plotArea.left() - textRect.width() / 2, bottom + 10);
        startRect = QRectF(plotArea.left() - textRect.width() / 2, bottom + 10,
                           textRect.width(), textRect.height());
    }

    if (m_endTimeLabel) {
        QRectF textRect = m_endTimeLabel->boundingRect();
        qreal x = qMin(plotArea.right() - textRect.width() / 2,
                       plotArea.right() - textRect.width());
        m_endTimeLabel->setPos(x, bottom + 10);
        endRect = QRectF(x, bottom + 10, textRect.width(), textRect.height());
    }

    // Position regular time labels, hiding any that collide with start/end labels
    for (int i = 0; i < m_timeLabelItems.size(); ++i) {
        qreal x = mapTimeToX(m_labelVideoTimes[i]);
        qreal y = bottom + 10;

        QRectF textRect = m_timeLabelItems[i]->boundingRect();
        QRectF labelRect(x - textRect.width() / 2, y, textRect.width(), textRect.height());

        // Check collision with start or end label
        bool collides = false;
        if (!startRect.isNull() && labelRect.intersects(startRect))
            collides = true;
        if (!endRect.isNull() && labelRect.intersects(endRect))
            collides = true;

        if (collides) {
            m_timeLabelItems[i]->setVisible(false);
        } else {
            m_timeLabelItems[i]->setPos(x - textRect.width() / 2, y);
            m_timeLabelItems[i]->setVisible(true);
        }
    }
}

void ChartPanel::resizeEvent(QResizeEvent *event)
{
    QChartView::resizeEvent(event);
    updateTimeLabels();
    updateTimeLabelPositions();
    updateLabelItems();
}

void ChartPanel::fitAll()
{
    if (m_durationMs > 0)
        m_axisX->setRange(0, m_durationMs);
    else if (m_timelineModel && !m_timelineModel->snapshot().isEmpty())
        m_axisX->setRange(0, m_timelineModel->snapshot().timestamps.last());
    else
        m_axisX->setRange(0, 1000);

    updateYAxisRange();
    updateTimeLabels();
    updateTimeLabelPositions();
    updateCursorPosition();
    updateLabelItems();
}

void ChartPanel::fitAllX()
{
    if (m_durationMs > 0)
        m_axisX->setRange(0, m_durationMs);
    else if (m_timelineModel && !m_timelineModel->snapshot().isEmpty())
        m_axisX->setRange(0, m_timelineModel->snapshot().timestamps.last());
    else
        m_axisX->setRange(0, 1000);

    updateTimeLabels();
    updateTimeLabelPositions();
    updateCursorPosition();
    updateLabelItems();
}

QString ChartPanel::formatTimeMs(qint64 ms)
{
    if (ms < 0) ms = 0;
    int totalSeconds = static_cast<int>(ms / 1000);
    int hours = totalSeconds / 3600;
    int minutes = (totalSeconds % 3600) / 60;
    int seconds = totalSeconds % 60;
    return QString("%1:%2:%3")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'));
}

/// @brief 更新光标线和时间标签位置
void ChartPanel::updateCursorPosition()
{
    if (!m_cursorLine)
        return;

    qreal x = mapTimeToX(m_cursorTimeMs);
    QRectF plotArea = m_chart->plotArea();

    // Allow a small epsilon to avoid floating-point edge issues
    if (x + 0.5 < plotArea.left() || x - 0.5 > plotArea.right() || !m_axisX) {
        m_cursorLine->setVisible(false);
        if (m_cursorTimeLabel) m_cursorTimeLabel->setVisible(false);
        if (m_cursorTimeBg) m_cursorTimeBg->setVisible(false);
        return;
    }

    m_cursorLine->setVisible(true);
    m_cursorLine->setLine(x, plotArea.top(), x, plotArea.bottom());

    // Show time label above cursor line
    if (m_cursorTimeLabel) {
        qint64 displayTime = m_cursorTimeMs + m_startTimeOfDayMs;
        m_cursorTimeLabel->setText(formatTimeMs(displayTime));
        QRectF textRect = m_cursorTimeLabel->boundingRect();
        qreal labelX = x - textRect.width() / 2;
        qreal labelY = plotArea.top() - textRect.height() - 8;

        // Clamp to plot area bounds
        labelX = qBound(plotArea.left(), labelX, plotArea.right() - textRect.width());
        labelY = qMax(labelY, plotArea.top() - textRect.height() - 12);

        m_cursorTimeLabel->setPos(labelX, labelY);
        m_cursorTimeLabel->setVisible(true);

        // Background rect
        if (m_cursorTimeBg) {
            QRectF bgRect(labelX - 3, labelY - 1, textRect.width() + 6, textRect.height() + 2);
            m_cursorTimeBg->setRect(bgRect);
            m_cursorTimeBg->setVisible(true);
        }
    }
}

qint64 ChartPanel::mapXToTime(qreal x) const
{
    QRectF plotArea = m_chart->plotArea();
    qreal ratio = (x - plotArea.left()) / plotArea.width();
    qreal min = m_axisX->min();
    qreal max = m_axisX->max();
    return static_cast<qint64>(min + ratio * (max - min));
}

qreal ChartPanel::mapTimeToX(qint64 timeMs) const
{
    QRectF plotArea = m_chart->plotArea();
    qreal min = m_axisX->min();
    qreal max = m_axisX->max();
    if (max <= min)
        return plotArea.left();
    qreal ratio = (static_cast<qreal>(timeMs) - min) / (max - min);
    qreal x = plotArea.left() + ratio * plotArea.width();
    // Clamp to avoid floating-point overshoot
    return qBound(plotArea.left(), x, plotArea.right());
}

qreal ChartPanel::clampX(qreal x) const
{
    QRectF plotArea = m_chart->plotArea();
    return qMax(plotArea.left(), qMin(plotArea.right(), x));
}

/// @brief 图表滚轮缩放：以光标位置为中心缩放X轴
void ChartPanel::wheelEvent(QWheelEvent *event)
{
    if (!m_axisX) {
        QChartView::wheelEvent(event);
        return;
    }

    QPointF chartPos = m_chart->mapFromScene(mapToScene(event->position().toPoint()));
    QRectF plotArea = m_chart->plotArea();
    if (!plotArea.contains(chartPos)) {
        QChartView::wheelEvent(event);
        return;
    }

    qreal delta = event->angleDelta().y() / 120.0;
    qreal xPos = mapXToTime(chartPos.x());

    qreal range = m_axisX->max() - m_axisX->min();
    qreal factor = (delta > 0) ? 0.8 : 1.25;  // zoom in / zoom out
    qreal newRange = qBound<qreal>(1000.0, range * factor, static_cast<qreal>(m_durationMs));

    qreal tLeft = xPos - (xPos - m_axisX->min()) * (newRange / range);
    qreal tRight = tLeft + newRange;

    if (tLeft < 0) { tLeft = 0; tRight = newRange; }
    if (tRight > m_durationMs) { tRight = m_durationMs; tLeft = m_durationMs - newRange; }

    m_axisX->setRange(tLeft, tRight);
    updateTimeLabels();
    updateTimeLabelPositions();
    updateCursorPosition();
    updateYAxisRange();
    updateLabelItems();
}

/**
 * @brief 在指定时间添加标签：弹出对话框选文字和颜色
 */
void ChartPanel::addLabelAtTime(qint64 timeMs)
{
    static const QColor presetColors[] = {
        QColor(220, 50, 50),   // red
        QColor(50, 160, 220),  // blue
        QColor(50, 200, 80),   // green
        QColor(230, 160, 30),  // orange
        QColor(160, 80, 200),  // purple
        QColor(200, 200, 50),  // yellow
    };
    static int colorIndex = 0;

    QDialog dlg(this->viewport());
    dlg.setWindowTitle(lang("添加标签", "Add Label"));
    dlg.setMinimumWidth(300);

    auto *layout = new QVBoxLayout(&dlg);

    layout->addWidget(new QLabel(lang("标签文字：", "Label text:")));
    auto *lineEdit = new QLineEdit(&dlg);
    layout->addWidget(lineEdit);

    layout->addWidget(new QLabel(lang("颜色：", "Color:")));
    auto *colorLayout = new QHBoxLayout();
    QColor chosenColor;
    for (const QColor &c : presetColors) {
        auto *btn = new QPushButton(&dlg);
        btn->setFixedSize(32, 32);
        btn->setStyleSheet(QString(
            "QPushButton { background-color: %1; border: 2px solid #555; border-radius: 4px; }"
            "QPushButton:hover { border: 2px solid white; }"
        ).arg(c.name()));
        connect(btn, &QPushButton::clicked, &dlg, [&dlg, &chosenColor, c]() {
            chosenColor = c;
            dlg.accept();
        });
        colorLayout->addWidget(btn);
    }
    layout->addLayout(colorLayout);

    auto *btnLayout = new QHBoxLayout();
    auto *okBtn = new QPushButton(lang("确定", "OK"), &dlg);
    auto *cancelBtn = new QPushButton(lang("取消", "Cancel"), &dlg);
    btnLayout->addStretch();
    btnLayout->addWidget(okBtn);
    btnLayout->addWidget(cancelBtn);
    layout->addLayout(btnLayout);

    connect(okBtn, &QPushButton::clicked, &dlg, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, &dlg, &QDialog::reject);
    connect(lineEdit, &QLineEdit::returnPressed, &dlg, &QDialog::accept);

    lineEdit->setFocus();

    if (dlg.exec() == QDialog::Accepted) {
        QString text = lineEdit->text().trimmed();
        if (!text.isEmpty()) {
            if (!chosenColor.isValid()) {
                chosenColor = presetColors[colorIndex % 6];
                colorIndex++;
            }
            m_labels.append(ChartLabel{timeMs, text, chosenColor});
            updateLabelItems();
        }
    }
}

/// @brief 双击图表添加标签
void ChartPanel::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QPointF chartPos = m_chart->mapFromScene(mapToScene(event->pos()));
        QRectF plotArea = m_chart->plotArea();
        if (plotArea.contains(chartPos)) {
            qint64 t = mapXToTime(clampX(chartPos.x()));
            addLabelAtTime(t);
            return;
        }
    }
    QChartView::mouseDoubleClickEvent(event);
}

void ChartPanel::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        QPointF chartPos = m_chart->mapFromScene(mapToScene(event->pos()));
        QRectF plotArea = m_chart->plotArea();

        qreal cursorX = mapTimeToX(m_cursorTimeMs);
        if (qAbs(chartPos.x() - cursorX) < 10 && plotArea.contains(chartPos)) {
            m_draggingCursor = true;
            setCursor(Qt::SizeHorCursor);
            return;
        }

        // Check label hit areas
        for (const auto &hit : m_labelHitAreas) {
            if (hit.first.contains(chartPos)) {
                m_cursorTimeMs = hit.second;
                updateCursorPosition();
                emit seekRequested(hit.second);
                return;
            }
        }

        if (plotArea.contains(chartPos)) {
            qreal x = clampX(chartPos.x());
            qint64 t = mapXToTime(x);
            m_cursorTimeMs = t;
            updateCursorPosition();
            emit seekRequested(t);
            return;
        }
    }

    // Middle-mouse or Shift+LMB for pan
    if (event->button() == Qt::MiddleButton ||
        (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ShiftModifier))) {
        QPointF chartPos = m_chart->mapFromScene(mapToScene(event->pos()));
        QRectF plotArea = m_chart->plotArea();
        if (plotArea.contains(chartPos)) {
            m_isPanning = true;
            m_panStartPos = event->pos();
            m_panStartMin = m_axisX->min();
            m_panStartMax = m_axisX->max();
            setCursor(Qt::ClosedHandCursor);
            return;
        }
    }

    QChartView::mousePressEvent(event);
}

void ChartPanel::mouseMoveEvent(QMouseEvent *event)
{
    if (m_isPanning) {
        QPointF chartPos = m_chart->mapFromScene(mapToScene(event->pos()));
        QPointF startChartPos = m_chart->mapFromScene(mapToScene(m_panStartPos));
        qreal dx = startChartPos.x() - chartPos.x();
        QRectF plotArea = m_chart->plotArea();
        qreal range = m_panStartMax - m_panStartMin;

        qreal shift = (dx / plotArea.width()) * range;
        qreal newMin = m_panStartMin + shift;
        qreal newMax = m_panStartMax + shift;

        // Clamp to video bounds
        if (newMin < 0) { newMin = 0; newMax = range; }
        if (newMax > m_durationMs) { newMax = m_durationMs; newMin = m_durationMs - range; }
        if (newMin < 0) { newMin = 0; }

        m_axisX->setRange(newMin, newMax);
        updateTimeLabels();
        updateTimeLabelPositions();
        updateCursorPosition();
        updateYAxisRange();
        updateLabelItems();
        return;
    }

    if (m_draggingCursor) {
        QPointF chartPos = m_chart->mapFromScene(mapToScene(event->pos()));
        qreal x = clampX(chartPos.x());
        qint64 t = mapXToTime(x);
        m_cursorTimeMs = t;
        updateCursorPosition();
        emit seekRequested(t);
        return;
    }

    QPointF chartPos = m_chart->mapFromScene(mapToScene(event->pos()));
    qreal cursorX = mapTimeToX(m_cursorTimeMs);
    QRectF plotArea = m_chart->plotArea();
    if (plotArea.contains(chartPos) && qAbs(chartPos.x() - cursorX) < 10) {
        setCursor(Qt::SizeHorCursor);
    } else {
        unsetCursor();
    }

    QChartView::mouseMoveEvent(event);
}

void ChartPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if ((event->button() == Qt::MiddleButton ||
         event->button() == Qt::LeftButton) && m_isPanning) {
        m_isPanning = false;
        unsetCursor();
        return;
    }
    if (event->button() == Qt::LeftButton && m_draggingCursor) {
        m_draggingCursor = false;
        unsetCursor();
    }
    QChartView::mouseReleaseEvent(event);
}
