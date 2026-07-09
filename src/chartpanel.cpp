/**
 * @file chartpanel.cpp
 * @brief 折线图面板实现：时间标签/光标/缩放平移/刻度尺/标签管理
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "chartpanel.h"
#include "domain/region_model.h"
#include "domain/polygon_model.h"
#include "domain/timeline_model.h"
#include "i18n.h"

#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLegendMarker>
#include <QtCharts/QAbstractSeries>
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
    setBackgroundBrush(QBrush(QColor(40, 40, 40)));
    setFocusPolicy(Qt::NoFocus);  // 防止图表窃取键盘焦点导致快捷键失效

    m_chart = new QChart();
    m_chart->setMargins(QMargins(2, 35, 2, 2));
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);
    m_chart->legend()->setLabelColor(QColor(0xF5, 0xF0, 0xE8));
    m_chart->setBackgroundBrush(QBrush(QColor(40, 40, 40)));

    connect(m_chart, &QChart::plotAreaChanged, this, &ChartPanel::updateTimeLabelPositions);
    connect(m_chart, &QChart::plotAreaChanged, this, &ChartPanel::updateLabelItems);
    connect(m_chart, &QChart::plotAreaChanged, this, &ChartPanel::updateABMarkers);
    connect(m_chart, &QChart::plotAreaChanged, this, [this](const QRectF &area) {
        emit plotAreaUpdated(area);
    });

    m_axisX = new QValueAxis();
    m_axisX->setLabelFormat("%.0f");
    m_axisX->setLabelsVisible(false);
    m_axisX->setTitleBrush(QBrush(QColor(0xF5, 0xF0, 0xE8)));
    m_axisX->setLabelsColor(QColor(0xF5, 0xF0, 0xE8));
    m_axisX->setGridLineVisible(false);

    m_axisY = new QValueAxis();
    m_axisY->setTitleText(lang("亮度 (Y均值)", "Brightness (Y avg)"));
    m_axisY->setRange(0, 255);
    m_axisY->setTitleBrush(QBrush(QColor(0xF5, 0xF0, 0xE8)));
    m_axisY->setLabelsColor(QColor(0xF5, 0xF0, 0xE8));
    m_axisY->setGridLineVisible(false);

    m_chart->addAxis(m_axisX, Qt::AlignBottom);
    m_chart->addAxis(m_axisY, Qt::AlignLeft);

    // v0.3: Forward X-axis range changes as a signal (for spectrogram sync)
    connect(m_axisX, &QValueAxis::rangeChanged, this, [this](qreal min, qreal max) {
        emit xAxisRangeChanged(min, max);
        updateCursorPosition();
        updateTimeLabels();
        updateTimeLabelPositions();
        updateLabelItems();
    });

    // Update A/B markers when axis range changes (zoom/pan/fit)
    connect(m_axisX, &QValueAxis::rangeChanged, this, [this](qreal, qreal) {
        updateABMarkers();
    });

    setChart(m_chart);

    // Legend click to toggle series visibility (connect after series are added in rebuildSeries)

    m_cursorLine = new QGraphicsLineItem(m_chart);
    m_cursorLine->setZValue(CURSOR_Z_VALUE);
    QPen cursorPen(QColor(0xFF, 0x98, 0x1C)); // orange
    cursorPen.setWidth(2);
    cursorPen.setStyle(Qt::DashLine);
    m_cursorLine->setPen(cursorPen);
    m_cursorLine->setVisible(false);

    // Time label above cursor line
    m_cursorTimeBg = new QGraphicsRectItem(m_chart);
    m_cursorTimeBg->setZValue(CURSOR_Z_VALUE);
    m_cursorTimeBg->setBrush(QBrush(QColor(60, 60, 60, 220)));
    m_cursorTimeBg->setPen(Qt::NoPen);
    m_cursorTimeBg->setVisible(false);

    m_cursorTimeLabel = new QGraphicsSimpleTextItem(m_chart);
    m_cursorTimeLabel->setZValue(CURSOR_Z_VALUE + 1);
    m_cursorTimeLabel->setFont(fontMono(9));
    m_cursorTimeLabel->setBrush(QBrush(QColor(0xF5, 0xF0, 0xE8)));
    m_cursorTimeLabel->setVisible(false);

    m_cursorDataLabel = new QGraphicsSimpleTextItem(m_chart);
    m_cursorDataLabel->setZValue(CURSOR_Z_VALUE + 1);
    m_cursorDataLabel->setFont(fontMono(9));
    m_cursorDataLabel->setBrush(QBrush(QColor(0xF5, 0xF0, 0xE8)));
    m_cursorDataLabel->setVisible(false);

    // A/B region markers
    QPen penA(QColor(0xF4, 0x43, 0x36)); // red
    penA.setWidth(2);
    m_lineA = new QGraphicsLineItem(m_chart);
    m_lineA->setPen(penA);
    m_lineA->setZValue(CURSOR_Z_VALUE - 1);
    m_lineA->setVisible(false);

    QPen penB(QColor(0x21, 0x96, 0xF3)); // blue
    penB.setWidth(2);
    m_lineB = new QGraphicsLineItem(m_chart);
    m_lineB->setPen(penB);
    m_lineB->setZValue(CURSOR_Z_VALUE - 1);
    m_lineB->setVisible(false);

    m_labelAText = new QGraphicsSimpleTextItem("A", m_chart);
    m_labelAText->setFont(fontMono(10, QFont::Bold));
    m_labelAText->setBrush(QBrush(QColor(0xF4, 0x43, 0x36)));
    m_labelAText->setZValue(CURSOR_Z_VALUE);
    m_labelAText->setVisible(false);

    m_labelBText = new QGraphicsSimpleTextItem("B", m_chart);
    m_labelBText->setFont(fontMono(10, QFont::Bold));
    m_labelBText->setBrush(QBrush(QColor(0x21, 0x96, 0xF3)));
    m_labelBText->setZValue(CURSOR_Z_VALUE);
    m_labelBText->setVisible(false);

    m_abHighlight = new QGraphicsRectItem(m_chart);
    m_abHighlight->setBrush(QBrush(QColor(33, 150, 243, 25)));
    m_abHighlight->setPen(Qt::NoPen);
    m_abHighlight->setZValue(0);
    m_abHighlight->setVisible(false);

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
        menu.addSeparator();
        // Chart guide lines
        QAction *addHGuideAction = menu.addAction(lang("添加水平辅助线", "Add Horizontal Guide"));
        QAction *addVGuideAction = menu.addAction(lang("添加垂直辅助线", "Add Vertical Guide"));
        int nearGuideIdx = hitTestChartGuideLine(pos);
        QAction *delGuideAction = menu.addAction(lang("删除此处辅助线", "Delete Guide Here"));
        delGuideAction->setEnabled(nearGuideIdx >= 0);
        menu.addSeparator();
        qint64 cursorTime = m_cursorTimeMs;
        QAction *addLabelAction = menu.addAction(lang("添加标签", "Add Label"));
        // Find nearest label to cursor for undo
        int nearestIdx = -1;
        qint64 nearestDist = LLONG_MAX;
        for (int i = 0; i < m_labels.size(); ++i) {
            qint64 dist = qAbs(m_labels[i].timeMs - cursorTime);
            if (dist < nearestDist) {
                nearestDist = dist;
                nearestIdx = i;
            }
        }
        QAction *undoLabelAction = menu.addAction(lang("删除光标处标签", "Delete Label at Cursor"));
        undoLabelAction->setEnabled(nearestIdx >= 0 && nearestDist < 5000);  // within 5s
        QAction *setAAction = menu.addAction(lang("设置 A 点", "Set Point A"));
        QAction *setBAction = menu.addAction(lang("设置 B 点", "Set Point B"));
        QAction *clearABAction = menu.addAction(lang("清除 A/B 区域", "Clear A/B Region"));
        clearABAction->setEnabled(isABRegionSet());
        menu.addSeparator();
        QAction *loopAction = menu.addAction(m_abLoop ?
            lang("关闭循环", "Disable Loop") :
            lang("开启循环", "Enable Loop"));
        loopAction->setEnabled(isABRegionSet());
        QAction *zoomABAction = menu.addAction(lang("缩放到 A-B 区域", "Zoom to A-B Region"));
        zoomABAction->setEnabled(isABRegionSet());
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
        } else if (chosen == addLabelAction) {
            addLabelAtTime(cursorTime);
        } else if (chosen == undoLabelAction) {
            if (nearestIdx >= 0) {
                m_labels.removeAt(nearestIdx);
                updateLabelItems();
            }
        } else if (chosen == setAAction) {
            setPointA(cursorTime);
        } else if (chosen == setBAction) {
            setPointB(cursorTime);
        } else if (chosen == clearABAction) {
            clearAB();
        } else if (chosen == loopAction) {
            setABLoop(!m_abLoop);
        } else if (chosen == zoomABAction) {
            zoomToABRegion();
        } else if (chosen == addHGuideAction) {
            bool ok;
            qreal val = QInputDialog::getDouble(this,
                lang("添加水平辅助线", "Add Horizontal Guide"),
                lang("Y 值 (亮度)", "Y value (brightness)"),
                m_axisY ? (m_axisY->min() + m_axisY->max()) / 2 : 128,
                m_axisY ? m_axisY->min() : 0,
                m_axisY ? m_axisY->max() : 255,
                1, &ok);
            if (ok)
                addHorizontalGuideLine(val);
        } else if (chosen == addVGuideAction) {
            addVerticalGuideLine(static_cast<qreal>(cursorTime));
        } else if (chosen == delGuideAction) {
            if (nearGuideIdx >= 0)
                removeChartGuideLine(nearGuideIdx);
        }
    });
}

ChartPanel::~ChartPanel()
{
    clearChartGuideLines();

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

void ChartPanel::setPolygonModel(PolygonModel *model)
{
    if (m_polygonModel) {
        disconnect(m_polygonModel, nullptr, this, nullptr);
    }
    m_polygonModel = model;
    if (m_polygonModel) {
        connect(m_polygonModel, &PolygonModel::polygonsChanged,
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

QRectF ChartPanel::plotArea() const
{
    return m_chart->plotArea();
}

void ChartPanel::setDuration(qint64 durationMs)
{
    m_durationMs = durationMs;
    if (m_durationMs > 0) {
        // Bug fix: If analysis data already exists, refresh the chart to ensure
        // the X range covers both the new duration and the data extent.
        // This handles the case where onDurationChanged fires after onDataReplaced.
        if (m_timelineModel && !m_timelineModel->snapshot().isEmpty()) {
            onDataReplaced();
        } else {
            m_axisX->setRange(0, m_durationMs);
        }
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
    // Defer rebuildSeries to after the current signal chain completes.
    // This prevents rebuildSeries from running with unstable data state
    // (e.g., after removeRegionData shifts values[] but before all signals settle).
    QTimer::singleShot(0, this, [this]() {
        if (m_regionModel)
            rebuildSeries();
    });
}

void ChartPanel::onDataReplaced()
{
    if (!m_timelineModel)
        return;

    AnalysisSnapshot snapshot = m_timelineModel->snapshot();
    qDebug() << "[onDataReplaced] isEmpty:" << snapshot.isEmpty()
             << "hasAudio:" << snapshot.hasAudio()
             << "timestamps:" << snapshot.timestamps.size()
             << "volume:" << snapshot.audio.volume.size()
             << "spectrogram:" << snapshot.audio.spectrogram.size()
             << "durationMs:" << m_durationMs;
    // Need either timestamps (luminance) or audio data to proceed
    if (snapshot.isEmpty() && !snapshot.hasAudio())
        return;

    // If no luminance series exist, only continue when audio is present
    if (m_seriesList.isEmpty()) {
        if (!snapshot.hasAudio())
            return;
        // Create volume series lazily for audio-only display
        if (!m_volumeSeries) {
            m_volumeSeries = new QLineSeries();
            m_volumeSeries->setName(lang("音量", "Volume"));
            QPen volumePen(QColor(76, 175, 80, 180));
            volumePen.setWidth(1);
            m_volumeSeries->setPen(volumePen);
            if (!m_axisYVolume) {
                m_axisYVolume = new QValueAxis();
                m_axisYVolume->setRange(-80, 0);
                m_axisYVolume->setTitleText(lang("音量 (dB)", "Volume (dB)"));
                m_axisYVolume->setLabelFormat("%.0f");
                m_axisYVolume->setLabelsVisible(true);
                m_axisYVolume->setTitleBrush(QBrush(QColor(0xF5, 0xF0, 0xE8)));
                m_axisYVolume->setLabelsColor(QColor(0xF5, 0xF0, 0xE8));
                m_axisYVolume->setGridLineVisible(false);
                m_chart->addAxis(m_axisYVolume, Qt::AlignRight);
            }
            m_chart->addSeries(m_volumeSeries);
            m_volumeSeries->attachAxis(m_axisX);
            m_volumeSeries->attachAxis(m_axisYVolume);
        }
    }

    // Always rebuild when dataEntries exist to ensure series names/colors
    // match the latest data. The m_rebuilding flag prevents recursion.
    if (!m_rebuilding && !snapshot.dataEntries.isEmpty()) {
        rebuildSeries();
        return;
    }

    if (m_durationMs > 0) {
        // Bug fix: Ensure X range covers both video duration AND data extent.
        // VLC may report a slightly different duration than the actual frame
        // timestamps from analysis. Use the larger of the two to prevent the
        // brightness curve from not reaching the right edge of the chart.
        qreal dataMax = snapshot.isEmpty() ? 0 : qreal(snapshot.timestamps.last());
        qreal xMax = qMax(qreal(m_durationMs), dataMax);
        if (xMax < 1000) xMax = 1000;
        m_axisX->setRange(0, xMax);
    } else if (!snapshot.isEmpty()) {
        qreal xMax = qMax(qreal(snapshot.timestamps.last()), qreal(1000));
        m_axisX->setRange(0, xMax);
    } else if (snapshot.hasAudio()) {
        // Audio-only: set X-axis from audio duration
        qint64 audioDur = snapshot.audio.durationMs();
        m_axisX->setRange(0, qMax(audioDur, qint64(1000)));
    }

    // Use direct synchronous update for series data.
    // The caller ensures no processEvents() or nested event loops
    // interfere with chart rendering.
    qint64 xMin = static_cast<qint64>(m_axisX->min());
    qint64 xMax = static_cast<qint64>(m_axisX->max());

    for (int i = 0; i < m_seriesList.size(); ++i) {
        QLineSeries *series = m_seriesList[i];
        // Use dataIndex from SeriesMapping to access correct data entry
        // (series order may differ from values[] order after ROI deletion/addition)
        int dataIdx = (i < m_seriesMapping.size()) ? m_seriesMapping[i].dataIndex : -1;
        if (dataIdx < 0 || dataIdx >= snapshot.regionCount() || snapshot.isEmpty()) {
            series->clear();
            continue;
        }
        QVector<QPointF> pts = snapshot.pointsForViewport(dataIdx, xMin, xMax, 5000);
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

    // v0.3: Update volume series
    if (m_volumeSeries && snapshot.hasAudio()) {
        QVector<QPointF> volPts = snapshot.audio.volumePointsForViewport(
            xMin, xMax);
        // 转换为 dB 刻度: dB = 20 * log10(linear)
        qreal minDb = 0;
        for (auto &pt : volPts) {
            qreal linear = pt.y();
            qreal db = (linear > 0.0001) ? 20.0 * std::log10(linear) : -80.0;
            db = qBound(-80.0, db, 0.0);
            pt.setY(db);
            if (db < minDb) minDb = db;
        }
        m_volumeSeries->replace(volPts);
        // 动态设置音量 Y 轴范围：顶部 0，底部为实际最低点 + 5% margin
        if (!volPts.isEmpty()) {
            qreal rangeBottom = qMax(minDb * 1.05, -80.0);
            if (rangeBottom > -1.0) rangeBottom = -1.0;  // 至少 1 dB 范围
            m_axisYVolume->setRange(rangeBottom, 0);
        }
    } else if (m_volumeSeries) {
        m_volumeSeries->clear();
    }

    // Toggle Volume legend/axis visibility based on audio presence
    if (m_volumeSeries) {
        bool hasAudio = snapshot.hasAudio();
        m_volumeSeries->setVisible(hasAudio);
        if (m_axisYVolume)
            m_axisYVolume->setVisible(hasAudio);
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
    // v0.3: Clear volume series
    if (m_volumeSeries) {
        m_volumeSeries->clear();
        m_volumeSeries->setVisible(false);
    }
    if (m_axisYVolume)
        m_axisYVolume->setVisible(false);

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
    m_rebuilding = true;

    for (auto *series : m_seriesList) {
        m_chart->removeSeries(series);
        delete series;
    }
    m_seriesList.clear();

    // v0.3: Remove old volume series
    if (m_volumeSeries) {
        m_chart->removeSeries(m_volumeSeries);
        delete m_volumeSeries;
        m_volumeSeries = nullptr;
    }

    if (!m_regionModel)
        return;

    // Always use ROI model count for series count (dataEntries may be stale after deletion)
    int rectCount = m_regionModel->regionCount();
    int polyCount = m_polygonModel ? m_polygonModel->polygonCount() : 0;
    int totalCount = rectCount + polyCount;

    AnalysisSnapshot snap;
    bool hasDataEntries = false;
    if (m_timelineModel) {
        snap = m_timelineModel->snapshot();
        hasDataEntries = !snap.dataEntries.isEmpty();
    }

    // Build merged entries: ROI model as skeleton, dataEntries matched by roiId
    // Each entry records the index into snapshot.values[] for correct data mapping
    m_seriesMapping.clear();
    for (int i = 0; i < rectCount; ++i) {
        int roiId = m_regionModel->roiIdAt(i);
        int dataIdx = -1;
        if (hasDataEntries) {
            for (int j = 0; j < snap.dataEntries.size(); ++j) {
                if (snap.dataEntries[j].type == DataEntry::Rect && snap.dataEntries[j].roiId == roiId) {
                    dataIdx = j;
                    break;
                }
            }
        }
        m_seriesMapping.append({DataEntry::Rect, roiId, dataIdx});
    }
    for (int i = 0; i < polyCount; ++i) {
        int roiId = m_polygonModel->roiIdAt(i);
        int dataIdx = -1;
        if (hasDataEntries) {
            for (int j = 0; j < snap.dataEntries.size(); ++j) {
                if (snap.dataEntries[j].type == DataEntry::Polygon && snap.dataEntries[j].roiId == roiId) {
                    dataIdx = j;
                    break;
                }
            }
        }
        m_seriesMapping.append({DataEntry::Polygon, roiId, dataIdx});
    }

    int rectCounter = 0, polyCounter = 0;
    for (int i = 0; i < totalCount; ++i) {
        auto *series = new QLineSeries();
        if (m_seriesMapping[i].type == DataEntry::Rect) {
            series->setName(QString(lang("区域 %1", "Region %1")).arg(rectCounter + 1));
            QPen pen(RegionModel::regionColor(rectCounter));
            pen.setWidth(1);
            series->setPen(pen);
            rectCounter++;
        } else {
            series->setName(QString(lang("多边形 %1", "Polygon %1")).arg(polyCounter + 1));
            QPen pen(PolygonModel::polygonColor(polyCounter));
            pen.setWidth(1);
            series->setPen(pen);
            polyCounter++;
        }

        m_chart->addSeries(series);
        series->attachAxis(m_axisX);
        series->attachAxis(m_axisY);
        m_seriesList.append(series);
    }

    // v0.3: Create volume series
    m_volumeSeries = new QLineSeries();
    m_volumeSeries->setName(lang("音量", "Volume"));
    QPen volumePen(QColor(76, 175, 80, 180));  // Green semi-transparent
    volumePen.setWidth(1);
    m_volumeSeries->setPen(volumePen);

    if (!m_axisYVolume) {
        m_axisYVolume = new QValueAxis();
        m_axisYVolume->setRange(-80, 0);
        m_axisYVolume->setTitleText(lang("音量 (dB)", "Volume (dB)"));
        m_axisYVolume->setLabelFormat("%.0f");
        m_axisYVolume->setLabelsVisible(true);
        m_axisYVolume->setTitleBrush(QBrush(QColor(0xF5, 0xF0, 0xE8)));
        m_axisYVolume->setLabelsColor(QColor(0xF5, 0xF0, 0xE8));
        m_axisYVolume->setGridLineVisible(false);
        m_chart->addAxis(m_axisYVolume, Qt::AlignRight);
    }

    m_chart->addSeries(m_volumeSeries);
    m_volumeSeries->attachAxis(m_axisX);
    m_volumeSeries->attachAxis(m_axisYVolume);

    // Hide Volume legend/axis if no audio data yet
    bool hasAudio = m_timelineModel ? m_timelineModel->snapshot().hasAudio() : false;
    m_volumeSeries->setVisible(hasAudio);
    if (m_axisYVolume)
        m_axisYVolume->setVisible(hasAudio);

    // Connect legend markers to toggle series visibility on click
    // Legend text always visible; only icon color block toggles transparent/solid
    for (QLegendMarker *marker : m_chart->legend()->markers()) {
        QLineSeries *ls = qobject_cast<QLineSeries*>(marker->series());
        QColor color = ls ? ls->pen().color() : QColor(0xF5, 0xF0, 0xE8);
        connect(marker, &QLegendMarker::clicked, this, [marker, color]() {
            bool vis = !marker->series()->isVisible();
            marker->series()->setVisible(vis);
            marker->setVisible(true);  // Force marker always visible
            marker->setBrush(vis ? QBrush(color) : Qt::transparent);
            marker->setLabelBrush(QBrush(QColor(0xF5, 0xF0, 0xE8)));
        });
    }

    if (m_timelineModel) {
        auto snap = m_timelineModel->snapshot();
        if (!snap.isEmpty() || snap.hasAudio())
            onDataReplaced();
    }

    m_rebuilding = false;
}

/// @brief 计算时间步长：目标6-8个标签可见
qint64 ChartPanel::computeTimeStep(qint64 durationMs, int plotWidthPx)
{
    if (durationMs <= 0)
        return 60000;

    // Target: one major tick every ~100px
    const int targetPxPerTick = 100;
    int numTicks = qMax(2, plotWidthPx / targetPxPerTick);
    qint64 rawStep = durationMs / numTicks;

    // Snap to nearest "nice" step from candidates
    const qint64 candidates[] = {
        500,      // 0.5s
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

    for (qint64 step : candidates) {
        if (step >= rawStep)
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

    qint64 step = computeTimeStep(visibleDurationMs, static_cast<int>(plotArea.width()));
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

        QString text = formatTimeHMS(tReal);
        auto *item = new QGraphicsSimpleTextItem(text, m_chart);
        item->setFont(fontSans(9));
        item->setBrush(QBrush(QColor(0xF5, 0xF0, 0xE8)));
        item->setZValue(LABEL_Z_VALUE);
        m_timeLabelItems.append(item);
        m_labelVideoTimes.append(tVideo);

        // Major tick mark
        if (m_showTickMarks) {
            auto *tick = new QGraphicsLineItem(m_chart);
            tick->setPen(QPen(QColor(180, 180, 180), 2));
            qreal x = mapTimeToX(tVideo);
            tick->setLine(x, bottom, x, bottom + 10);
            tick->setZValue(LABEL_Z_VALUE - 1);
            m_tickMarkItems.append(tick);
        }
    }

    // --- Minor tick marks (ruler-style sub-divisions) ---
    if (m_showTickMarks) {
        int minorDivisions = 4;
        qint64 minorStep = step / minorDivisions;
        if (minorStep >= 1) {
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
                    tick->setPen(QPen(QColor(140, 140, 140), 1));
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
        QString text = formatTimeHMS(startReal);
        auto *item = new QGraphicsSimpleTextItem(text, m_chart);
        item->setFont(fontMono(10, QFont::Bold));
        item->setBrush(QBrush(QColor(0xF5, 0xF0, 0xE8)));
        item->setZValue(LABEL_Z_VALUE + 2);
        m_startTimeLabel = item;

        if (m_showTickMarks) {
            auto *tick = new QGraphicsLineItem(m_chart);
            tick->setPen(QPen(QColor(160, 160, 160), 2));
            tick->setLine(plotArea.left(), bottom, plotArea.left(), bottom + 12);
            tick->setZValue(LABEL_Z_VALUE);
            m_tickMarkItems.append(tick);
        }
    }

    // --- End time label at right edge (black, bold, larger) ---
    {
        qint64 endReal = static_cast<qint64>(visibleMax) + offset;
        QString text = formatTimeHMS(endReal);
        auto *item = new QGraphicsSimpleTextItem(text, m_chart);
        item->setFont(fontMono(10, QFont::Bold));
        item->setBrush(QBrush(QColor(0xF5, 0xF0, 0xE8)));
        item->setZValue(LABEL_Z_VALUE + 2);
        m_endTimeLabel = item;

        if (m_showTickMarks) {
            auto *tick = new QGraphicsLineItem(m_chart);
            tick->setPen(QPen(QColor(160, 160, 160), 2));
            tick->setLine(plotArea.right(), bottom, plotArea.right(), bottom + 12);
            tick->setZValue(LABEL_Z_VALUE);
            m_tickMarkItems.append(tick);
        }
    }

    drawChartGuideLines();
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

    // Position regular time labels, hiding any that collide with start/end labels or adjacent labels
    QRectF lastVisibleRect;
    for (int i = 0; i < m_timeLabelItems.size(); ++i) {
        qreal x = mapTimeToX(m_labelVideoTimes[i]);
        qreal y = bottom + 10;

        QRectF textRect = m_timeLabelItems[i]->boundingRect();
        QRectF labelRect(x - textRect.width() / 2, y, textRect.width(), textRect.height());

        // Check collision with start, end, and previous visible label
        bool collides = false;
        if (!startRect.isNull() && labelRect.intersects(startRect))
            collides = true;
        if (!endRect.isNull() && labelRect.intersects(endRect))
            collides = true;
        if (!lastVisibleRect.isNull() && labelRect.intersects(lastVisibleRect))
            collides = true;

        if (collides) {
            m_timeLabelItems[i]->setVisible(false);
        } else {
            m_timeLabelItems[i]->setPos(x - textRect.width() / 2, y);
            m_timeLabelItems[i]->setVisible(true);
            lastVisibleRect = labelRect;
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
    if (m_durationMs > 0) {
        qreal dataMax = (m_timelineModel && !m_timelineModel->snapshot().isEmpty())
            ? qreal(m_timelineModel->snapshot().timestamps.last()) : 0;
        m_axisX->setRange(0, qMax(qreal(m_durationMs), dataMax));
    } else if (m_timelineModel && !m_timelineModel->snapshot().isEmpty())
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
    if (m_durationMs > 0) {
        qreal dataMax = (m_timelineModel && !m_timelineModel->snapshot().isEmpty())
            ? qreal(m_timelineModel->snapshot().timestamps.last()) : 0;
        m_axisX->setRange(0, qMax(qreal(m_durationMs), dataMax));
    } else if (m_timelineModel && !m_timelineModel->snapshot().isEmpty())
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
    int millis = static_cast<int>(ms % 1000);
    return QString("%1:%2:%3.%4")
        .arg(hours, 2, 10, QChar('0'))
        .arg(minutes, 2, 10, QChar('0'))
        .arg(seconds, 2, 10, QChar('0'))
        .arg(millis, 3, 10, QChar('0'));
}

QString ChartPanel::formatTimeHMS(qint64 ms)
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
        if (m_cursorDataLabel) m_cursorDataLabel->setVisible(false);
        if (m_cursorTimeBg) m_cursorTimeBg->setVisible(false);
        return;
    }

    m_cursorLine->setVisible(true);
    m_cursorLine->setLine(x, plotArea.top(), x, plotArea.bottom());

    // Show two-line info label above cursor line
    if (m_cursorTimeLabel) {
        // Line 1: time
        qint64 displayTime = m_cursorTimeMs + m_startTimeOfDayMs;
        m_cursorTimeLabel->setText(formatTimeMs(displayTime));
        m_cursorTimeLabel->setVisible(true);

        // Line 2: region luminance + volume
        bool hasData = false;
        if (m_timelineModel) {
            AnalysisSnapshot snap = m_timelineModel->snapshot();
            if (!snap.isEmpty()) {
                int idx = snap.indexAtTime(m_cursorTimeMs);
                if (idx >= 0 && idx < snap.pointCount()) {
                    QStringList parts;
                    int rectLabel = 0, polyLabel = 0;
                    for (int i = 0; i < snap.regionCount(); ++i) {
                        if (i >= snap.values.size() || snap.values[i].isEmpty() || idx >= snap.values[i].size())
                            continue;
                        qreal val = snap.values[i][idx];
                        // Use DataEntry for R/P labeling if available
                        if (i < snap.dataEntries.size()) {
                            if (snap.dataEntries[i].type == DataEntry::Rect)
                                parts << QString("R%1:%2").arg(++rectLabel).arg(static_cast<int>(val));
                            else
                                parts << QString("P%1:%2").arg(++polyLabel).arg(static_cast<int>(val));
                        } else {
                            int rectCount = m_regionModel ? m_regionModel->regionCount() : 0;
                            if (i < rectCount)
                                parts << QString("R%1:%2").arg(++rectLabel).arg(static_cast<int>(val));
                            else
                                parts << QString("P%1:%2").arg(++polyLabel).arg(static_cast<int>(val));
                        }
                    }
                    // Append volume in dB
                    if (snap.hasAudio()) {
                        int volIdx = static_cast<int>(m_cursorTimeMs / snap.audio.safeTimeResolutionMs());
                        if (volIdx >= 0 && volIdx < snap.audio.volume.size()) {
                            qreal linear = snap.audio.volume[volIdx];
                            qreal db = (linear > 0.0001) ? 20.0 * std::log10(linear) : -80.0;
                            parts << QString("%1dB").arg(static_cast<int>(qBound(-80.0, db, 0.0)));
                        }
                    }
                    if (!parts.isEmpty()) {
                        m_cursorDataLabel->setText(parts.join("  "));
                        m_cursorDataLabel->setVisible(true);
                        hasData = true;
                    }
                }
            }
        }
        if (!hasData)
            m_cursorDataLabel->setVisible(false);

        QRectF timeRect = m_cursorTimeLabel->boundingRect();
        QRectF dataRect = hasData ? m_cursorDataLabel->boundingRect() : QRectF();
        qreal maxW = hasData ? qMax(timeRect.width(), dataRect.width()) : timeRect.width();
        qreal totalH = hasData ? (timeRect.height() + dataRect.height()) : timeRect.height();

        // Left-aligned at cursor x, above plot area
        qreal labelX = x;
        qreal labelY = plotArea.top() - totalH - 10;

        // Clamp to plot area bounds
        labelX = qBound(plotArea.left(), labelX, plotArea.right() - maxW);
        labelY = qMax(labelY, plotArea.top() - totalH - 14);

        m_cursorTimeLabel->setPos(labelX, labelY);
        if (hasData)
            m_cursorDataLabel->setPos(labelX, labelY + timeRect.height());

        // Background rect covering both lines
        if (m_cursorTimeBg) {
            QRectF bgRect(labelX - 3, labelY - 1, maxW + 6, totalH + 2);
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

        // Guide line drag start
        int guideIdx = hitTestChartGuideLine(event->pos());
        if (guideIdx >= 0) {
            m_draggingGuideLine = guideIdx;
            m_dragGuideLineStartValue = m_chartGuideLines[guideIdx].value;
            return;
        }

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

    // Guide line drag
    if (m_draggingGuideLine >= 0) {
        auto &gl = m_chartGuideLines[m_draggingGuideLine];
        if (gl.orientation == ChartGuideLine::Horizontal) {
            if (m_axisY) {
                qreal normalized = (plotArea.bottom() - chartPos.y()) / plotArea.height();
                gl.value = m_axisY->min() + normalized * (m_axisY->max() - m_axisY->min());
            }
        } else {
            gl.value = static_cast<qreal>(mapXToTime(clampX(chartPos.x())));
        }
        drawChartGuideLines();
        return;
    }

    // Guide line hover detection
    int oldHover = m_hoveredGuideLine;
    m_hoveredGuideLine = hitTestChartGuideLine(event->pos());
    if (m_hoveredGuideLine != oldHover) {
        drawChartGuideLines();
        if (m_hoveredGuideLine >= 0) {
            setCursor(m_chartGuideLines[m_hoveredGuideLine].orientation == ChartGuideLine::Horizontal
                      ? Qt::SizeVerCursor : Qt::SizeHorCursor);
        }
    }

    if (m_hoveredGuideLine < 0) {
        if (plotArea.contains(chartPos) && qAbs(chartPos.x() - cursorX) < 10) {
            setCursor(Qt::SizeHorCursor);
        } else {
            unsetCursor();
        }
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
    if (event->button() == Qt::LeftButton && m_draggingGuideLine >= 0) {
        m_draggingGuideLine = -1;
        unsetCursor();
        return;
    }
    if (event->button() == Qt::LeftButton && m_draggingCursor) {
        m_draggingCursor = false;
        unsetCursor();
    }
    QChartView::mouseReleaseEvent(event);
}

// =============================================================================
// A/B Region Playback
// =============================================================================

void ChartPanel::setPointA(qint64 timeMs)
{
    m_abPointA = timeMs;
    if (m_abPointB >= 0 && m_abPointA > m_abPointB)
        qSwap(m_abPointA, m_abPointB);
    if (isABRegionSet() && !m_abLoop) {
        m_abLoop = true;
    }
    updateABMarkers();
    emit abRegionChanged();
    if (isABRegionSet())
        zoomToABRegion();
}

void ChartPanel::setPointB(qint64 timeMs)
{
    m_abPointB = timeMs;
    if (m_abPointA >= 0 && m_abPointA > m_abPointB)
        qSwap(m_abPointA, m_abPointB);
    if (isABRegionSet() && !m_abLoop) {
        m_abLoop = true;
    }
    updateABMarkers();
    emit abRegionChanged();
    if (isABRegionSet())
        zoomToABRegion();
}

void ChartPanel::clearAB()
{
    m_abPointA = -1;
    m_abPointB = -1;
    m_abLoop = false;
    updateABMarkers();
    emit abRegionChanged();
    fitAllX();
}

void ChartPanel::setABLoop(bool loop)
{
    m_abLoop = loop;
    updateABMarkers();
    emit abRegionChanged();
}

void ChartPanel::zoomToABRegion()
{
    if (!isABRegionSet())
        return;
    qint64 a = qMin(m_abPointA, m_abPointB);
    qint64 b = qMax(m_abPointA, m_abPointB);
    qint64 margin = qMax<qint64>((b - a) / 20, 100);
    m_axisX->setRange(a - margin, b + margin);
    updateTimeLabels();
    updateTimeLabelPositions();
    updateCursorPosition();
    updateYAxisRange();
    updateLabelItems();
}

void ChartPanel::updateABMarkers()
{
    QRectF pa = m_chart->plotArea();
    bool hasA = m_abPointA >= 0;
    bool hasB = m_abPointB >= 0;

    m_lineA->setVisible(hasA);
    m_labelAText->setVisible(hasA);
    if (hasA) {
        qreal x = mapTimeToX(m_abPointA);
        m_lineA->setLine(x, pa.top(), x, pa.bottom());
        m_labelAText->setPos(x + 3, pa.top() + 2);
    }

    m_lineB->setVisible(hasB);
    m_labelBText->setVisible(hasB);
    if (hasB) {
        qreal x = mapTimeToX(m_abPointB);
        m_lineB->setLine(x, pa.top(), x, pa.bottom());
        m_labelBText->setPos(x + 3, pa.top() + 2);
    }

    bool hasRegion = isABRegionSet();
    m_abHighlight->setVisible(hasRegion);
    if (hasRegion) {
        qreal xA = mapTimeToX(m_abPointA);
        qreal xB = mapTimeToX(m_abPointB);
        m_abHighlight->setRect(qMin(xA, xB), pa.top(), qAbs(xB - xA), pa.height());
    }
}

// =============================================================================
// Chart Guide Lines
// =============================================================================

void ChartPanel::addHorizontalGuideLine(qreal yValue, const QColor &color)
{
    ChartGuideLine gl;
    gl.orientation = ChartGuideLine::Horizontal;
    gl.value = yValue;
    gl.color = color;
    gl.lineItem = new QGraphicsLineItem(m_chart);
    gl.lineItem->setPen(QPen(color, 1, Qt::DashLine));
    gl.lineItem->setZValue(LABEL_Z_VALUE - 3);
    gl.labelItem = new QGraphicsSimpleTextItem(QString::number(yValue, 'f', 1), m_chart);
    gl.labelItem->setFont(fontSans(8));
    gl.labelItem->setBrush(QBrush(color));
    gl.labelItem->setZValue(LABEL_Z_VALUE - 2);
    m_chartGuideLines.append(gl);
    drawChartGuideLines();
}

void ChartPanel::addVerticalGuideLine(qreal xTimeMs, const QColor &color)
{
    ChartGuideLine gl;
    gl.orientation = ChartGuideLine::Vertical;
    gl.value = xTimeMs;
    gl.color = color;
    gl.lineItem = new QGraphicsLineItem(m_chart);
    gl.lineItem->setPen(QPen(color, 1, Qt::DashLine));
    gl.lineItem->setZValue(LABEL_Z_VALUE - 3);
    gl.labelItem = new QGraphicsSimpleTextItem(formatTimeMs(xTimeMs + m_startTimeOfDayMs), m_chart);
    gl.labelItem->setFont(fontSans(8));
    gl.labelItem->setBrush(QBrush(color));
    gl.labelItem->setZValue(LABEL_Z_VALUE - 2);
    m_chartGuideLines.append(gl);
    drawChartGuideLines();
}

void ChartPanel::removeChartGuideLine(int index)
{
    if (index < 0 || index >= m_chartGuideLines.size())
        return;
    auto &gl = m_chartGuideLines[index];
    if (gl.lineItem) {
        if (gl.lineItem->scene()) gl.lineItem->scene()->removeItem(gl.lineItem);
        delete gl.lineItem;
    }
    if (gl.labelItem) {
        if (gl.labelItem->scene()) gl.labelItem->scene()->removeItem(gl.labelItem);
        delete gl.labelItem;
    }
    m_chartGuideLines.removeAt(index);
}

void ChartPanel::clearChartGuideLines()
{
    for (int i = m_chartGuideLines.size() - 1; i >= 0; --i)
        removeChartGuideLine(i);
}

void ChartPanel::drawChartGuideLines()
{
    QRectF pa = m_chart->plotArea();
    if (pa.width() < 10)
        return;

    for (int i = 0; i < m_chartGuideLines.size(); ++i) {
        auto &gl = m_chartGuideLines[i];
        if (gl.orientation == ChartGuideLine::Horizontal) {
            qreal y = gl.value;
            // Map Y value to widget coordinates
            if (m_axisY) {
                qreal yMin = m_axisY->min();
                qreal yMax = m_axisY->max();
                if (yMax > yMin) {
                    qreal normalized = (y - yMin) / (yMax - yMin);
                    qreal widgetY = pa.bottom() - normalized * pa.height();
                    gl.lineItem->setLine(pa.left(), widgetY, pa.right(), widgetY);
                    gl.labelItem->setPos(pa.right() + 3, widgetY - 8);
                }
            }
        } else {
            qreal x = mapTimeToX(static_cast<qint64>(gl.value));
            gl.lineItem->setLine(x, pa.top(), x, pa.bottom());
            gl.labelItem->setPos(x + 3, pa.top() + 2);
        }
        bool hovered = (i == m_hoveredGuideLine);
        QPen pen(gl.color, hovered ? 2 : 1, Qt::DashLine);
        gl.lineItem->setPen(pen);
    }
}

int ChartPanel::hitTestChartGuideLine(const QPoint &pos) const
{
    const int hitRadius = 5;
    for (int i = 0; i < m_chartGuideLines.size(); ++i) {
        const auto &gl = m_chartGuideLines[i];
        if (!gl.lineItem || !gl.lineItem->isVisible())
            continue;
        QLineF line = gl.lineItem->line();
        if (gl.orientation == ChartGuideLine::Horizontal) {
            if (qAbs(pos.y() - line.y1()) <= hitRadius &&
                pos.x() >= line.x1() && pos.x() <= line.x2())
                return i;
        } else {
            if (qAbs(pos.x() - line.x1()) <= hitRadius &&
                pos.y() >= line.y1() && pos.y() <= line.y2())
                return i;
        }
    }
    return -1;
}
