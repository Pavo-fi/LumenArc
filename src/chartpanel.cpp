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
#include "domain/roi_model.h"
#include "domain/roi_model.h"
#include "domain/timeline_model.h"
#include "domain/time_calibration.h"
#include "i18n.h"
#include "theme.h"

#include <QtCharts/QChart>
#include <QtCharts/QLineSeries>
#include <QtCharts/QValueAxis>
#include <QtCharts/QLegendMarker>
#include <QtCharts/QAbstractSeries>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QTimer>
#include <QDebug>
#include <QDateTime>
#include <QMenu>
#include <QAction>
#include <QColorDialog>
#include <QDialog>
#include <QVBoxLayout>
#include <QGraphicsTextItem>
#include <QTextDocument>
#include <QFontMetricsF>
#include <QToolTip>
#include <QScreen>
#include <QGuiApplication>
#include <QCoreApplication>
#include <QPainter>
#include <algorithm>
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
    setMouseTracking(true);   // 标签标记点悬停悬浮窗依赖 hover 事件
    setBackgroundBrush(QBrush(QColor(Theme::BgPanel)));
    setFocusPolicy(Qt::NoFocus);  // 防止图表窃取键盘焦点导致快捷键失效

    m_chart = new QChart();
    m_chart->setMargins(QMargins(2, 35, 2, 2));
    m_chart->legend()->setVisible(true);
    m_chart->legend()->setAlignment(Qt::AlignBottom);
    m_chart->legend()->setLabelColor(QColor(0xF5, 0xF0, 0xE8));
    m_chart->setBackgroundBrush(QBrush(QColor(Theme::BgPanel)));

    connect(m_chart, &QChart::plotAreaChanged, this, &ChartPanel::updateTimeLabelPositions);
    connect(m_chart, &QChart::plotAreaChanged, this, &ChartPanel::updateLabelItems);
    connect(m_chart, &QChart::plotAreaChanged, this, &ChartPanel::updateABMarkers);
    connect(m_chart, &QChart::plotAreaChanged, this, [this](const QRectF &area) {
        emit plotAreaUpdated(area);
    });

    m_axisX = new QValueAxis();
    m_axisX->setLabelFormat("%.0f");
    m_axisX->setLabelsVisible(false);
    // 隐藏 Qt 自绘轴线/刻度（与自定义刻度线叠加会产生“额外刻度线”）；
    // 刻度样式统一由 updateTimeLabels 自绘控制
    m_axisX->setLineVisible(false);
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
    QPen cursorPen{QColor(Theme::Accent)}; // 品牌金
    cursorPen.setWidth(2);
    cursorPen.setStyle(Qt::DashLine);
    m_cursorLine->setPen(cursorPen);
    m_cursorLine->setVisible(false);

    // Time label above cursor line
    m_cursorTimeBg = new QGraphicsRectItem(m_chart);
    m_cursorTimeBg->setZValue(CURSOR_Z_VALUE);
    m_cursorTimeBg->setBrush(QBrush(QColor(37, 41, 50, 230)));
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

    // A/B region markers（Okabe-Ito 色板）
    QPen penA(QColor(213, 94, 0)); // vermillion
    penA.setWidth(2);
    m_lineA = new QGraphicsLineItem(m_chart);
    m_lineA->setPen(penA);
    m_lineA->setZValue(CURSOR_Z_VALUE - 1);
    m_lineA->setVisible(false);

    QPen penB(QColor(86, 180, 233)); // sky blue
    penB.setWidth(2);
    m_lineB = new QGraphicsLineItem(m_chart);
    m_lineB->setPen(penB);
    m_lineB->setZValue(CURSOR_Z_VALUE - 1);
    m_lineB->setVisible(false);

    m_labelAText = new QGraphicsSimpleTextItem("A", m_chart);
    m_labelAText->setFont(fontMono(10, QFont::Bold));
    m_labelAText->setBrush(QBrush(QColor(213, 94, 0)));
    m_labelAText->setZValue(CURSOR_Z_VALUE);
    m_labelAText->setVisible(false);

    m_labelBText = new QGraphicsSimpleTextItem("B", m_chart);
    m_labelBText->setFont(fontMono(10, QFont::Bold));
    m_labelBText->setBrush(QBrush(QColor(86, 180, 233)));
    m_labelBText->setZValue(CURSOR_Z_VALUE);
    m_labelBText->setVisible(false);

    m_abHighlight = new QGraphicsRectItem(m_chart);
    m_abHighlight->setBrush(QBrush(QColor(86, 180, 233, 25)));
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
        QAction *labelTextAction = menu.addAction(lang("显示标签文字", "Show Label Text"));
        labelTextAction->setCheckable(true);
        // 勾选状态 = 当前实际可见性（自动模式按折线数判定）
        labelTextAction->setChecked((m_labelsTextMode == 1)
            || (m_labelsTextMode == 0 && visibleSeriesCount() < 2));
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
        } else if (chosen == labelTextAction) {
            // 用户手动切换：固定显示/隐藏（自动模式由菜单再次点击退出）
            m_labelsTextMode = labelTextAction->isChecked() ? 1 : 2;
            updateLabelItems();
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
            // 不弹框：直接用鼠标 Y 位置换算亮度值添加水平辅助线
            qreal yVal = m_axisY ? (m_axisY->min() + m_axisY->max()) / 2 : 128;
            if (m_axisY) {
                QRectF pa = m_chart->plotArea();
                QPointF chartPos = m_chart->mapFromScene(mapToScene(pos));
                if (pa.height() > 1) {
                    qreal norm = (pa.bottom() - chartPos.y()) / pa.height();
                    norm = qBound(0.0, norm, 1.0);
                    yVal = m_axisY->min() + norm * (m_axisY->max() - m_axisY->min());
                }
            }
            addHorizontalGuideLine(yVal);
        } else if (chosen == addVGuideAction) {
            // 不弹框：直接用鼠标 X 位置换算时间添加垂直辅助线（原用播放光标时间）
            QPointF chartPos = m_chart->mapFromScene(mapToScene(pos));
            addVerticalGuideLine(static_cast<qreal>(mapXToTime(clampX(chartPos.x()))));
        } else if (chosen == delGuideAction) {
            if (nearGuideIdx >= 0)
                removeChartGuideLine(nearGuideIdx);
        }
    });
}

ChartPanel::~ChartPanel()
{
    clearChartGuideLines();
    delete m_labelTip;   // 独立顶层悬浮窗（无 parent），析构时回收

    for (auto *item : m_timeLabelItems) {
        if (item->scene()) item->scene()->removeItem(item);
        delete item;
    }
    m_timeLabelItems.clear();
    m_labelVideoTimes.clear();
    m_labelIsGap.clear();

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

void ChartPanel::setRegionModel(RoiModel *model)
{
    if (m_regionModel) {
        disconnect(m_regionModel, nullptr, this, nullptr);
    }
    m_regionModel = model;
    if (m_regionModel) {
        connect(m_regionModel, &RoiModel::regionsChanged,
                this, &ChartPanel::onRegionsChanged);
        rebuildSeries();
    }
}

void ChartPanel::setPolygonModel(RoiModel *model)
{
    if (m_polygonModel) {
        disconnect(m_polygonModel, nullptr, this, nullptr);
    }
    m_polygonModel = model;
    if (m_polygonModel) {
        connect(m_polygonModel, &RoiModel::polygonsChanged,
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
        // 2026-08-13 修复（音量曲线“短一截”根因）：仅音频快照（无亮度时间戳）
        // 也必须重填——否则切换视频瞬间按残留时长填充的音量曲线在正确时长
        // 到达后永不补齐，曲线停在旧视频时长处。
        const bool hasData = m_timelineModel
            && (!m_timelineModel->snapshot().isEmpty()
                || m_timelineModel->snapshot().hasAudio());
        if (hasData) {
            onDataReplaced();
        } else {
            m_axisX->setRange(0, m_durationMs);
        }
    }
    updateTimeLabels();
}

void ChartPanel::setCalibration(const TimeCalibration &cal)
{
    m_calibration = cal;
    updateTimeLabels();
}

qint64 ChartPanel::displayMsOf(qint64 streamMs) const
{
    if (!m_calibration.dateKnown)
        return streamMs + m_calibration.offsetMs;   // 旧路径：日内偏移（v7 行为）
    return m_calibration.beijingMsOf(streamMs);     // 新路径：北京时间
}

qint64 ChartPanel::streamMsFromDisplay(qint64 displayMs) const
{
    if (!m_calibration.dateKnown)
        return displayMs - m_calibration.offsetMs;
    return m_calibration.streamMsOf(displayMs - m_calibration.truthOffsetMs);
}

QString ChartPanel::formatDisplayTime(qint64 displayMs) const
{
    if (!m_calibration.dateKnown)
        return formatTimeHMS(displayMs);
    const QDateTime dt = QDateTime::fromMSecsSinceEpoch(displayMs);
    return m_spanCrossDay ? dt.toString(QStringLiteral("MM-dd HH:mm"))
                          : dt.toString(QStringLiteral("HH:mm:ss"));
}

QString ChartPanel::formatDisplayTimeFull(qint64 displayMs) const
{
    if (!m_calibration.dateKnown)
        return formatTimeMs(displayMs);
    return QDateTime::fromMSecsSinceEpoch(displayMs)
        .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss.zzz"));
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

QImage ChartPanel::renderToImage(const QSize &targetSize)
{
    if (targetSize.isEmpty() || !m_chart)
        return QImage();
    QGraphicsScene *sc = m_chart->scene();
    if (!sc)
        return QImage();

    setUpdatesEnabled(false);
    const QSizeF oldSize = m_chart->size();
    // 重新布局到目标尺寸：plotAreaChanged 同步驱动时间标签/图表标签/AB 标记
    // 重排（同线程 DirectConnection）；但【刻度线/底部基线是固定坐标项、不听
    // 该信号】（plotAreaChanged 只搬文字）——必须 updateTimeLabels() 重建，
    // 否则快照里 X 轴刻度轨残留在旧 plotArea 位置（中部浮线，用户实测）。
    m_chart->resize(QSizeF(targetSize));
    updateTimeLabels();      // 重建刻度/基线（内部连带 updateTimeLabelPositions + drawChartGuideLines）
    updateCursorPosition();  // 光标项不听 plotAreaChanged，手动刷新
    sc->update();   // 强制场景立即 polish/布局（离屏无事件循环兜底）
    QCoreApplication::sendPostedEvents(m_chart, QEvent::Polish);

    QImage img(targetSize, QImage::Format_ARGB32);
    img.fill(QColor(Theme::BgPanel));
    {
        QPainter p(&img);
        p.setRenderHint(QPainter::Antialiasing);
        sc->render(&p, QRectF(0, 0, targetSize.width(), targetSize.height()),
                   m_chart->geometry());
        p.end();
    }

    m_chart->resize(oldSize);   // 恢复原布局（屏幕 widget 尺寸不变）
    updateTimeLabels();         // 刻度项同样重建回原位
    updateCursorPosition();
    setUpdatesEnabled(true);
    update();
    return img;
}

void ChartPanel::onRegionsChanged()
{
    // Defer rebuildSeries to after the current signal chain completes.
    // This prevents rebuildSeries from running with unstable data state
    // (e.g., after removeRegionData shifts values[] but before all signals settle).
    QTimer::singleShot(0, this, [this]() {
        if (m_regionModel && m_polygonModel)
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
             << "volume:" << snapshot.audioData().volume.size()
             << "spectrogram:" << snapshot.audioData().spectrogram.size()
             << "durationMs:" << m_durationMs;
    // Need either timestamps (luminance) or audio data to proceed
    if (snapshot.isEmpty() && !snapshot.hasAudio()) {
        // 2026-08 修复：切换视频后旧音量曲线残留 + X 轴换成新时长 →
        // 曲线“短一截”。空快照必须清掉音量曲线，而不是直接 return 残留
        if (m_volumeSeries) {
            m_volumeSeries->clear();
            m_volumeSeries->setVisible(false);
        }
        if (m_axisYVolume)
            m_axisYVolume->setVisible(false);
        return;
    }

    // If no luminance series exist, only continue when audio is present
    if (m_seriesList.isEmpty()) {
        if (!snapshot.hasAudio())
            return;
        // Create volume series lazily for audio-only display
        if (!m_volumeSeries) {
            m_volumeSeries = new QLineSeries();
            m_volumeSeries->setName(lang("音量", "Volume"));
            // alpha=179 = 70% 不透明度（255×0.70，2026-08-18 用户拍板；原 180≈70.6%）
            QPen volumePen(QColor(0, 158, 115, 179));
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
    if (!m_rebuilding && !snapshot.lumEntries().isEmpty()) {
        rebuildSeries();
        // 不 return：rebuildSeries 重建的空音量曲线需要继续填充，
        // 否则有亮度数据的视频切换后音量曲线为空（2026-08 修复）
    }

    if (m_durationMs > 0) {
        // Bug fix: Ensure X range covers both video duration AND data extent.
        // VLC may report a slightly different duration than the actual frame
        // timestamps from analysis. Use the larger of the two to prevent the
        // brightness curve from not reaching the right edge of the chart.
        qreal dataMax = snapshot.isEmpty() ? 0 : qreal(snapshot.timestamps.last());
        // 2026-08-13：音量曲线按音频自身时间轴铺满。切换视频瞬间
        // m_durationMs 可能仍是上一个视频的残留值（偏小），
        // 不计入音频全长会把音量曲线截断在残留时长处（“短一截”）。
        if (snapshot.hasAudio())
            dataMax = qMax(dataMax, qreal(snapshot.audioData().durationMs()));
        qreal xMax = qMax(qreal(m_durationMs), dataMax);
        if (xMax < 1000) xMax = 1000;
        m_axisX->setRange(0, xMax);
    } else if (!snapshot.isEmpty()) {
        qreal xMax = qMax(qreal(snapshot.timestamps.last()), qreal(1000));
        m_axisX->setRange(0, xMax);
    } else if (snapshot.hasAudio()) {
        // Audio-only: set X-axis from audio duration
        qint64 audioDur = snapshot.audioData().durationMs();
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
        QVector<QPointF> volPts = snapshot.audioData().volumePointsForViewport(
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

/// 标签多行避让布局的行走位槽（文件级：MSVC 对局部类型作模板实参解析不稳）
struct Slot { int labelIdx; qreal x; qreal fullW; };
typedef QVector<Slot> SlotRow;

/// 标签标记点：悬停 250ms 后经 ChartPanel 显示自控悬浮窗（点右上方，
/// 标签色文字 + 时间行）；离开立即隐藏。悬浮窗由 ChartPanel 持有，
/// 显示/隐藏/时长完全自控（QToolTip 平台行为不可控，已弃用）。
class LabelDotItem : public QGraphicsEllipseItem
{
public:
    LabelDotItem(qreal x, qreal y, qreal size, const QColor &color,
                 const QString &text, const QString &timeStr,
                 QGraphicsItem *parent, QWidget *host)
        : QGraphicsEllipseItem(x, y, size, size, parent)
        , m_color(color), m_text(text), m_timeStr(timeStr), m_host(host)
    {
        setBrush(color);
        setPen(QPen(QColor(22, 24, 29), 1.5));
        setAcceptHoverEvents(true);
    }

protected:
    void hoverEnterEvent(QGraphicsSceneHoverEvent *event) override
    {
        if (auto *panel = static_cast<ChartPanel *>(m_host))
            panel->scheduleLabelTip(m_color, m_text, m_timeStr, anchorGlobal());
        QGraphicsEllipseItem::hoverEnterEvent(event);
    }
    void hoverLeaveEvent(QGraphicsSceneHoverEvent *event) override
    {
        if (auto *panel = static_cast<ChartPanel *>(m_host))
            panel->hideLabelTip();
        QGraphicsEllipseItem::hoverLeaveEvent(event);
    }

private:
    /// 点右上角的全局坐标（悬浮窗锚点）
    QPoint anchorGlobal() const
    {
        const QPointF scenePos = mapToScene(rect().center());
        auto *view = static_cast<QGraphicsView *>(m_host);
        return m_host->mapToGlobal(view->mapFromScene(scenePos)) + QPoint(8, -6);
    }

    QColor m_color;
    QString m_text;
    QString m_timeStr;
    QWidget *m_host = nullptr;   // ChartPanel（QGraphicsView），场景→全局坐标用
};

void ChartPanel::updateLabelItems()
{
    for (auto *item : m_labelGraphicsItems) {
        if (item->scene()) item->scene()->removeItem(item);
        delete item;
    }
    m_labelGraphicsItems.clear();
    m_labelHitAreas.clear();

    if (m_labels.isEmpty())
        return;

    const QRectF plotArea = m_chart->plotArea();
    // 标签文字可见性：0=自动（折线 ≥2 时只显示彩色标记点，避免画面杂乱），
    // 1=强制显示，2=强制隐藏（右键菜单切换）。隐藏时标记点 tooltip 显示全文。
    const int seriesCount = visibleSeriesCount();
    const bool textVisible = (m_labelsTextMode == 1)
        || (m_labelsTextMode == 0 && seriesCount < 2);
    if (!textVisible) {
        // 标记点模式：每个标签 = 彩色圆点 + 悬停悬浮窗（右上方，标签色文字 + 时间）
        for (const ChartLabel &label : m_labels) {
            const qreal x = mapTimeToX(label.timeMs);
            if (x < plotArea.left() || x > plotArea.right())
                continue;
            const QString timeStr = formatDisplayTime(displayMsOf(label.timeMs));
            auto *dot = new LabelDotItem(x - 4, plotArea.top() + 0.5, 8, label.color,
                                         label.text, timeStr, m_chart, this);
            dot->setZValue(USER_LABEL_Z_VALUE);
            m_labelGraphicsItems.append(dot);
            m_labelHitAreas.append({QRectF(x - 8, plotArea.top() - 2, 16, 16), label.timeMs});
        }
        return;
    }

    // 多行避让布局（两遍法）：
    //  第一遍 按完整文字宽度分行走位（行内间距 >= GAP，行数有界 MAX_ROWS）；
    //  第二遍 仅在同行内按“下一个同行标签间距”截断——文字尽量完整显示，
    //         密集/超长时省略号截断，全文经 tooltip 查看。
    constexpr int MAX_ROWS = 3;
    constexpr qreal GAP = 10.0;        // 同行标签最小间距
    constexpr qreal MIN_TEXT_W = 16.0; // 截断后最小宽度
    const QFont f = fontSans(9);
    const QFontMetricsF fm(f);
    const qreal ROW_H = fm.height() + 3.0;   // 每行高度（随字体度量，防行间贴叠）
    const qreal top = plotArea.top();

    // 按时间点排序后依次放置（m_labels 本身保持添加顺序不变）
    QVector<int> order;
    order.reserve(m_labels.size());
    for (int i = 0; i < m_labels.size(); ++i)
        order.append(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return mapTimeToX(m_labels[a].timeMs) < mapTimeToX(m_labels[b].timeMs);
    });

    // 第一遍：分行走位（用完整宽度，保守换行）
    QVector<SlotRow> rows;
    rows.resize(MAX_ROWS);
    qreal rowEnd[MAX_ROWS] = {};
    for (int r = 0; r < MAX_ROWS; ++r)
        rowEnd[r] = plotArea.left();
    for (int i : order) {
        const ChartLabel &label = m_labels[i];
        const qreal x = mapTimeToX(label.timeMs);
        if (x < plotArea.left() || x > plotArea.right())
            continue;
        const qreal fullW = fm.horizontalAdvance(label.text);
        int row = 0;
        while (row < MAX_ROWS - 1 && x < rowEnd[row] + GAP)
            ++row;
        rows[row].append({ i, x, fullW });
        rowEnd[row] = qMax(rowEnd[row], x + fullW + GAP);
    }

    // 第二遍：同行内按实际间距截断，生成图形项
    for (int row = 0; row < MAX_ROWS; ++row) {
        const SlotRow &rowSlots = rows[row];
        for (int j = 0; j < rowSlots.size(); ++j) {
            const ChartLabel &label = m_labels[rowSlots[j].labelIdx];
            const qreal x = rowSlots[j].x;
            // 同行内下一个标签（或右边界）决定可用宽度
            const qreal nextX = (j + 1 < rowSlots.size())
                ? rowSlots[j + 1].x : plotArea.right();
            const qreal rawAvail = qMin(nextX - x - GAP, plotArea.right() - x - 6.0);

            // 时间点竖线：从图表顶边延伸到该标签行文字中线
            const qreal rowY = top + 2.0 + row * ROW_H;
            auto *line = new QGraphicsLineItem(m_chart);
            QPen pen(label.color);
            pen.setWidth(2);
            line->setPen(pen);
            line->setLine(x, top + 4, x, rowY + fm.height() / 2.0);
            line->setZValue(USER_LABEL_Z_VALUE);
            m_labelGraphicsItems.append(line);

            if (rowSlots[j].fullW > rawAvail && rawAvail < MIN_TEXT_W) {
                // 空间不足（密集标签）：降级为圆点标记，悬停悬浮窗显示全文
                const QString timeStr = formatDisplayTime(displayMsOf(label.timeMs));
                auto *dot = new LabelDotItem(x - 4, rowY + fm.height() / 2.0 - 4, 8,
                                             label.color, label.text, timeStr, m_chart, this);
                dot->setZValue(USER_LABEL_Z_VALUE);
                m_labelGraphicsItems.append(dot);
                m_labelHitAreas.append({QRectF(x - 6, top + row * ROW_H, 12, ROW_H + 8),
                                        label.timeMs});
                continue;
            }

            // 截断：超长文字加省略号，全文经 tooltip 查看
            const qreal availW = qMax(MIN_TEXT_W, rawAvail);
            QString shown = label.text;
            if (rowSlots[j].fullW > availW) {
                while (!shown.isEmpty()
                       && fm.horizontalAdvance(shown + QStringLiteral("…")) > availW)
                    shown.chop(1);
                shown += QStringLiteral("…");
            }
            const qreal textW = fm.horizontalAdvance(shown);

            // 文字（QGraphicsTextItem 支持 tooltip 显示全文）
            auto *textItem = new QGraphicsTextItem(shown, m_chart);
            textItem->setDefaultTextColor(label.color);
            textItem->setFont(f);
            textItem->setTextInteractionFlags(Qt::NoTextInteraction);
            textItem->document()->setDocumentMargin(0);
            if (shown != label.text)
                textItem->setToolTip(label.text);   // 截断时悬停显示完整文字
            textItem->setZValue(USER_LABEL_Z_VALUE);
            textItem->setPos(x + 4, rowY);
            m_labelGraphicsItems.append(textItem);

            m_labelHitAreas.append({QRectF(x - 4, top + row * ROW_H,
                                           textW + 12, ROW_H + 8),
                                    label.timeMs});
        }
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

    if (!m_regionModel && !m_polygonModel)
        return;

    // Always use ROI model count for series count (dataEntries may be stale after deletion)
    int rectCount = m_regionModel ? m_regionModel->regionCount() : 0;
    int polyCount = m_polygonModel ? m_polygonModel->polygonCount() : 0;
    int totalCount = rectCount + polyCount;

    AnalysisSnapshot snap;
    bool hasDataEntries = false;
    if (m_timelineModel) {
        snap = m_timelineModel->snapshot();
        hasDataEntries = !snap.lumEntries().isEmpty();
    }

    // Build merged entries: ROI model as skeleton, dataEntries matched by roiId
    // Each entry records the index into snapshot.lumRows()[] for correct data mapping
    m_seriesMapping.clear();
    for (int i = 0; i < rectCount; ++i) {
        int roiId = m_regionModel->roiIdAt(i);
        int dataIdx = -1;
        if (hasDataEntries) {
            for (int j = 0; j < snap.lumEntries().size(); ++j) {
                if (snap.lumEntries()[j].type == DataEntry::Rect && snap.lumEntries()[j].roiId == roiId) {
                    dataIdx = j;
                    break;
                }
            }
        }
        m_seriesMapping.append({DataEntry::Rect, roiId, dataIdx});
    }
    for (int i = 0; i < polyCount; ++i) {
        int roiId = m_polygonModel->polygonRoiIdAt(i);
        int dataIdx = -1;
        if (hasDataEntries) {
            for (int j = 0; j < snap.lumEntries().size(); ++j) {
                if (snap.lumEntries()[j].type == DataEntry::Polygon && snap.lumEntries()[j].roiId == roiId) {
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
            QPen pen(RoiModel::regionColor(rectCounter));
            pen.setWidth(1);
            series->setPen(pen);
            rectCounter++;
        } else {
            series->setName(QString(lang("多边形 %1", "Polygon %1")).arg(polyCounter + 1));
            QPen pen(RoiModel::polygonColor(polyCounter));
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
    // alpha=179 = 70% 不透明度（255×0.70，2026-08-18 用户拍板；原 180≈70.6%）
    QPen volumePen(QColor(0, 158, 115, 179));  // Okabe-Ito bluish green
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
    m_labelIsGap.clear();

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
    qreal bottom = plotArea.bottom();

    // 跨天检测（dateKnown 路径的刻度格式选择）
    if (m_calibration.dateKnown) {
        m_spanCrossDay =
            QDateTime::fromMSecsSinceEpoch(displayMsOf(static_cast<qint64>(visibleMin))).date()
            != QDateTime::fromMSecsSinceEpoch(displayMsOf(static_cast<qint64>(visibleMax))).date();
    } else {
        m_spanCrossDay = false;
    }

    // Align first label to step boundary（显示域对齐，逆运算回流内；仿射下两侧均均匀）
    const qint64 dispStart = displayMsOf(static_cast<qint64>(visibleMin));
    const qint64 dispEnd = displayMsOf(static_cast<qint64>(visibleMax));
    qint64 firstLabel = (dispStart / step) * step;
    if (firstLabel > dispStart)
        firstLabel -= step;

    // --- Major tick labels (regular time labels) ---
    // v1.12.3（越秀案实测实锤「时间轴不显示校时结果」）：分段模式（拼接产物
    // 含时间缺口）改流内域等距刻度——墙钟域等距步进在缺口内反解出幻影流内
    // 位置（旧 streamMsOf 沿前段外推），且大缺口后循环提前 break，真实后段
    // 完全无标签。流内域刻度每个位置的墙钟由 wallMsOf 逐段锚定（恒真）；
    // 缺口以红色虚线 + 「缺 H:MM」小字标记上轴（C1 不静默）。
    const bool pwMode = m_calibration.dateKnown && m_calibration.piecewiseMode();
    if (pwMode) {
        const qint64 vMin = static_cast<qint64>(visibleMin);
        const qint64 vMax = static_cast<qint64>(visibleMax);
        qint64 t0 = (vMin / step) * step;
        if (t0 < vMin)
            t0 += step;
        for (qint64 tVideo = t0; tVideo <= vMax; tVideo += step) {
            const QString text = formatDisplayTime(displayMsOf(tVideo));
            auto *item = new QGraphicsSimpleTextItem(text, m_chart);
            item->setFont(fontMono(9, QFont::Bold));
            item->setBrush(QBrush(QColor(0xF5, 0xF0, 0xE8)));
            item->setZValue(LABEL_Z_VALUE);
            m_timeLabelItems.append(item);
            m_labelVideoTimes.append(tVideo);
            m_labelIsGap.append(false);

            if (m_showTickMarks) {
                auto *tick = new QGraphicsLineItem(m_chart);
                tick->setPen(QPen(QColor(154, 160, 171), 2));
                qreal x = mapTimeToX(tVideo);
                tick->setLine(x, bottom, x, bottom + 10);
                tick->setZValue(LABEL_Z_VALUE - 1);
                m_tickMarkItems.append(tick);
            }
        }
        // 缺口标记（红色虚线竖线 + 红色「缺 H:MM」；清理/碰撞/平移随既有流程）
        const QVector<PiecewiseGap> pwGaps = m_calibration.piecewise.gaps();
        for (const auto &g : pwGaps) {
            if (g.streamPosMs <= vMin || g.streamPosMs >= vMax)
                continue;
            const qreal x = mapTimeToX(g.streamPosMs);
            if (m_showTickMarks) {
                auto *gl = new QGraphicsLineItem(m_chart);
                gl->setPen(QPen(QColor(0xE0, 0x54, 0x54), 1, Qt::DashLine));
                gl->setLine(x, plotArea.top(), x, bottom);
                gl->setZValue(LABEL_Z_VALUE - 3);
                m_tickMarkItems.append(gl);
            }
            auto *gt = new QGraphicsSimpleTextItem(
                lang("缺 %1", "GAP %1").arg(fmtGapDuration(g.gapWallMs)),
                m_chart);
            gt->setFont(fontMono(8, QFont::Bold));
            gt->setBrush(QBrush(QColor(0xE0, 0x54, 0x54)));
            gt->setZValue(LABEL_Z_VALUE + 1);
            m_timeLabelItems.append(gt);
            m_labelVideoTimes.append(g.streamPosMs);
            m_labelIsGap.append(true);
        }
    } else
    for (qint64 tReal = firstLabel; tReal <= dispEnd; tReal += step) {
        qint64 tVideo = streamMsFromDisplay(tReal);
        if (tVideo < static_cast<qint64>(visibleMin))
            continue;
        if (tVideo > static_cast<qint64>(visibleMax))
            break;

        QString text = formatDisplayTime(tReal);
        auto *item = new QGraphicsSimpleTextItem(text, m_chart);
        item->setFont(fontMono(9, QFont::Bold));
        item->setBrush(QBrush(QColor(0xF5, 0xF0, 0xE8)));
        item->setZValue(LABEL_Z_VALUE);
        m_timeLabelItems.append(item);
        m_labelVideoTimes.append(tVideo);
        m_labelIsGap.append(false);

        // Major tick mark（亮灰，2px）
        if (m_showTickMarks) {
            auto *tick = new QGraphicsLineItem(m_chart);
            tick->setPen(QPen(QColor(154, 160, 171), 2));
            qreal x = mapTimeToX(tVideo);
            tick->setLine(x, bottom, x, bottom + 10);
            tick->setZValue(LABEL_Z_VALUE - 1);
            m_tickMarkItems.append(tick);
        }
    }

    // --- 底部基线（贯穿整个绘图区，弱化分隔感） ---
    if (m_showTickMarks) {
        auto *baseline = new QGraphicsLineItem(m_chart);
        baseline->setPen(QPen(QColor(58, 65, 82), 1));
        baseline->setLine(plotArea.left(), bottom, plotArea.right(), bottom);
        baseline->setZValue(LABEL_Z_VALUE - 4);
        m_tickMarkItems.append(baseline);
    }

    // --- Minor tick marks（ruler-style sub-divisions，暗灰 1px） ---
    if (m_showTickMarks) {
        int minorDivisions = 4;
        qint64 minorStep = step / minorDivisions;
        if (minorStep >= 1) {
            if (pwMode) {
                // 分段模式：次级刻度同样走流内域（防缺口幻影，同主刻度）
                const qint64 vMin = static_cast<qint64>(visibleMin);
                const qint64 vMax = static_cast<qint64>(visibleMax);
                qint64 m0 = (vMin / minorStep) * minorStep;
                if (m0 < vMin)
                    m0 += minorStep;
                for (qint64 tMinorVideo = m0; tMinorVideo <= vMax;
                     tMinorVideo += minorStep) {
                    auto *tick = new QGraphicsLineItem(m_chart);
                    tick->setPen(QPen(QColor(74, 80, 96), 1));
                    qreal x = mapTimeToX(tMinorVideo);
                    tick->setLine(x, bottom, x, bottom + 5);
                    tick->setZValue(LABEL_Z_VALUE - 2);
                    m_tickMarkItems.append(tick);
                }
            } else
            for (qint64 tReal = firstLabel; tReal < dispEnd; tReal += step) {
                for (int m = 1; m < minorDivisions; ++m) {
                    qint64 tMinor = tReal + m * minorStep;
                    qint64 tMinorVideo = streamMsFromDisplay(tMinor);
                    if (tMinorVideo < static_cast<qint64>(visibleMin))
                        continue;
                    if (tMinorVideo > static_cast<qint64>(visibleMax))
                        break;

                    auto *tick = new QGraphicsLineItem(m_chart);
                    tick->setPen(QPen(QColor(74, 80, 96), 1));
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
        const qint64 startReal = displayMsOf(static_cast<qint64>(visibleMin));
        QString text = formatDisplayTime(startReal);
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
        const qint64 endReal = displayMsOf(static_cast<qint64>(visibleMax));
        QString text = formatDisplayTime(endReal);
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
    // v1.12.4（用户实测：缺口红字与时间轴刻度重叠）：两遍法 + 优先级——
    // 缺口标记追加在刻度项之后，旧按数组序的 lastVisibleRect 假定 x 递增，
    // 靠左的缺口与左侧刻度永不判碰 → 红字直接压刻度。改为：
    //   第一遍 非缺口刻度按 x 升序走位（刻度之间互避，与起止标签避让），
    //          刻度永远优先保留；
    //   第二遍 缺口红字按 x 升序，与所有已放刻度/起止标签/已放缺口判碰，
    //          重叠即隐藏文字（红色虚线保留，缺口指示不丢），放大到不重叠
    //          自动恢复显示。
    QVector<int> order;
    order.reserve(m_timeLabelItems.size());
    for (int i = 0; i < m_timeLabelItems.size(); ++i)
        order.append(i);
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        return m_labelVideoTimes[a] < m_labelVideoTimes[b];
    });

    QVector<QRectF> placed;   // 已放置（可见）标签矩形
    if (!startRect.isNull())
        placed.append(startRect);
    if (!endRect.isNull())
        placed.append(endRect);

    // 第一遍：非缺口刻度
    for (const int i : order) {
        if (i < m_labelIsGap.size() && m_labelIsGap[i])
            continue;
        const qreal x = mapTimeToX(m_labelVideoTimes[i]);
        const qreal y = bottom + 10;
        const QRectF textRect = m_timeLabelItems[i]->boundingRect();
        const QRectF labelRect(x - textRect.width() / 2, y,
                               textRect.width(), textRect.height());
        bool collides = false;
        for (const QRectF &r : placed)
            if (labelRect.intersects(r)) { collides = true; break; }
        if (collides) {
            m_timeLabelItems[i]->setVisible(false);
        } else {
            m_timeLabelItems[i]->setPos(x - textRect.width() / 2, y);
            m_timeLabelItems[i]->setVisible(true);
            placed.append(labelRect);
        }
    }
    // 第二遍：缺口红字（碰撞即隐藏，刻度不被挤掉）
    for (const int i : order) {
        if (i >= m_labelIsGap.size() || !m_labelIsGap[i])
            continue;
        const qreal x = mapTimeToX(m_labelVideoTimes[i]);
        const qreal y = bottom + 10;
        const QRectF textRect = m_timeLabelItems[i]->boundingRect();
        const QRectF labelRect(x - textRect.width() / 2, y,
                               textRect.width(), textRect.height());
        bool collides = false;
        for (const QRectF &r : placed)
            if (labelRect.intersects(r)) { collides = true; break; }
        if (collides) {
            m_timeLabelItems[i]->setVisible(false);
        } else {
            m_timeLabelItems[i]->setPos(x - textRect.width() / 2, y);
            m_timeLabelItems[i]->setVisible(true);
            placed.append(labelRect);
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

QString ChartPanel::fmtGapDuration(qint64 gapMs)
{
    // 缺口时长紧凑格式（时间轴缺口小字）：<90s→"42s"；<90min→"31m"；否则 "10.3h"
    if (gapMs < 0)
        gapMs = 0;
    const double s = gapMs / 1000.0;
    if (s < 90.0)
        return QStringLiteral("%1s").arg(qRound(s));
    const double m = s / 60.0;
    if (m < 90.0)
        return QStringLiteral("%1m").arg(qRound(m));
    return QStringLiteral("%1h").arg(m / 60.0, 0, 'f', 1);
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
        const qint64 displayTime = displayMsOf(m_cursorTimeMs);
        m_cursorTimeLabel->setText(formatDisplayTimeFull(displayTime));
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
                        if (i >= snap.lumRows().size() || snap.lumRows()[i].isEmpty() || idx >= snap.lumRows()[i].size())
                            continue;
                        qreal val = snap.lumRows()[i][idx];
                        // Use DataEntry for R/P labeling if available
                        if (i < snap.lumEntries().size()) {
                            if (snap.lumEntries()[i].type == DataEntry::Rect)
                                parts << QString("R%1:%2").arg(++rectLabel).arg(QString::number(val, 'f', 2));
                            else
                                parts << QString("P%1:%2").arg(++polyLabel).arg(QString::number(val, 'f', 2));
                        } else {
                            int rectCount = m_regionModel ? m_regionModel->regionCount() : 0;
                            if (i < rectCount)
                                parts << QString("R%1:%2").arg(++rectLabel).arg(QString::number(val, 'f', 2));
                            else
                                parts << QString("P%1:%2").arg(++polyLabel).arg(QString::number(val, 'f', 2));
                        }
                    }
                    // Append volume in dB
                    if (snap.hasAudio()) {
                        int volIdx = static_cast<int>(m_cursorTimeMs / snap.audioData().safeTimeResolutionMs());
                        if (volIdx >= 0 && volIdx < snap.audioData().volume.size()) {
                            qreal linear = snap.audioData().volume[volIdx];
                            qreal db = (linear > 0.0001) ? 20.0 * std::log10(linear) : -80.0;
                            parts << QString("%1dB").arg(QString::number(qBound(-80.0, db, 0.0), 'f', 1));
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

/// 标签标记点悬浮窗：250ms 延迟后显示（划过不弹，停留才出）
void ChartPanel::scheduleLabelTip(const QColor &color, const QString &text,
                                  const QString &timeStr, const QPoint &anchorGlobal)
{
    m_labelTipColor = color;
    m_labelTipText = text;
    m_labelTipTime = timeStr;
    m_labelTipAnchor = anchorGlobal;
    if (!m_labelTipTimer) {
        m_labelTipTimer = new QTimer(this);
        m_labelTipTimer->setSingleShot(true);
        m_labelTipTimer->setInterval(250);
        connect(m_labelTipTimer, &QTimer::timeout, this, &ChartPanel::showLabelTipNow);
    }
    m_labelTipTimer->start();
}

/// 立即隐藏悬浮窗（并取消未触发的延迟显示）
void ChartPanel::hideLabelTip()
{
    if (m_labelTipTimer)
        m_labelTipTimer->stop();
    if (m_labelTip)
        m_labelTip->hide();
}

void ChartPanel::showLabelTipNow()
{
    if (!m_labelTip) {
        // 自控悬浮窗：鼠标穿透、不抢焦点、半透明深色圆角（样式与主题一致）
        m_labelTip = new QLabel(nullptr, Qt::ToolTip | Qt::FramelessWindowHint);
        m_labelTip->setAttribute(Qt::WA_TransparentForMouseEvents);
        m_labelTip->setAttribute(Qt::WA_ShowWithoutActivating);
        m_labelTip->setStyleSheet(QStringLiteral(
            "QLabel { background-color: rgba(30, 33, 40, 0.92); color: #EDE8DF;"
            "  border: 1px solid rgba(51, 57, 71, 0.85); border-radius: 6px;"
            "  padding: 4px 8px; }"));
    }
    m_labelTip->setText(QStringLiteral(
        "<div style='color:%1; font-size:12px;'>%2</div>"
        "<div style='color:#9AA0AB; font-size:11px; margin-top:2px;'>%3</div>")
        .arg(m_labelTipColor.name(), m_labelTipText.toHtmlEscaped(),
             m_labelTipTime.toHtmlEscaped()));
    m_labelTip->adjustSize();

    // 整体对齐点右上方：悬浮窗底部 = 点右上角锚点；屏幕边缘自动避让
    const QSize tipSize = m_labelTip->size();
    QPoint tipPos(m_labelTipAnchor.x(), m_labelTipAnchor.y() - tipSize.height());
    if (QScreen *screen = QGuiApplication::screenAt(m_labelTipAnchor)) {
        const QRect avail = screen->availableGeometry();
        if (tipPos.x() + tipSize.width() > avail.right())
            tipPos.setX(avail.right() - tipSize.width());
        if (tipPos.y() < avail.top())
            tipPos.setY(m_labelTipAnchor.y() + 6);
    }
    m_labelTip->move(tipPos);
    m_labelTip->show();
}

/// 当前可见折线数（图例条目数：含无数据骨架 ROI——空 series 也占图例位；
/// 音量曲线有数据时 +1）。标签文字自动隐藏判定用：≥2 时只显示标记点
int ChartPanel::visibleSeriesCount() const
{
    int n = m_seriesList.size();
    if (m_volumeSeries && m_volumeSeries->count() > 0)
        ++n;
    return n;
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
        // Okabe-Ito 色盲友好调色板
        QColor(86, 180, 233),   // sky blue
        QColor(230, 159, 0),    // orange
        QColor(0, 158, 115),    // bluish green
        QColor(213, 94, 0),     // vermillion
        QColor(204, 121, 167),  // reddish purple
        QColor(240, 228, 66),   // yellow
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
            "QPushButton { background-color: %1; border: 2px solid #333947; border-radius: 4px; }"
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
            // 拖拽匀速化：初始化速度估计器与量化网格锚点
            m_dragWallClock.start();
            m_dragLastWallMs = -1;
            m_dragLastRawMs = mapXToTime(clampX(chartPos.x()));
            m_dragVelocity = 0.0;
            m_dragAnchorMs = m_dragLastRawMs;
            m_dragLastEmittedMs = -1;
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
        qint64 t = quantizeDragTarget(mapXToTime(x));
        m_cursorTimeMs = t;
        updateCursorPosition();
        if (t != m_dragLastEmittedMs) {
            m_dragLastEmittedMs = t;
            emit seekRequested(t);
        }
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
        emit scrubEnded();
    }
    QChartView::mouseReleaseEvent(event);
}

// =============================================================================
// 拖拽匀速化：速度自适应帧网格量化
// =============================================================================

qint64 ChartPanel::quantizeDragTarget(qint64 t)
{
    if (m_frameMs <= 0)
        return t;   // 帧时长未知：退化为原始直通

    // 目标速度 EMA（时间轴 ms / 墙钟 ms，带符号）。0.5/0.5 权重：
    // 加减速响应快，起步飞奔不会因 EMA 冷启动而滞留 n=1
    const qint64 now = m_dragWallClock.elapsed();
    if (m_dragLastWallMs >= 0) {
        const qint64 dtWall = now - m_dragLastWallMs;
        if (dtWall > 0) {
            const double inst = double(t - m_dragLastRawMs) / double(dtWall);
            m_dragVelocity = 0.5 * m_dragVelocity + 0.5 * inst;
        }
    }
    m_dragLastWallMs = now;
    m_dragLastRawMs = t;

    // 速度自适应步长：显示帧以固定墙钟节拍均匀推进所需的帧数。
    // 慢拖 v·25ms < 半帧 → n=1 逐帧均匀；快拖等距跳帧，步长随速度缩放。
    const double v = qAbs(m_dragVelocity);
    const qint64 n = qBound<qint64>(
        1LL, qint64(v * DRAG_DISPLAY_CADENCE_MS / m_frameMs + 0.5), 256LL);
    const qint64 quantum = n * m_frameMs;

    // 步长切换时把网格锚点平移到最近发出点，保持步进相位连续——
    // 避免 n 变化瞬间目标序列出现额外跳变
    if (quantum != m_dragQuantum && m_dragLastEmittedMs >= 0)
        m_dragAnchorMs = m_dragLastEmittedMs;
    m_dragQuantum = quantum;

    // 以拖拽起点为锚的网格量化：保证一次拖拽内步进严格均匀
    const qint64 tq = m_dragAnchorMs
        + qRound64(double(t - m_dragAnchorMs) / double(quantum)) * quantum;
    return qBound<qint64>(0LL, tq, m_durationMs);
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
    // 防御：构造期 setChart 可能提前触发 rangeChanged，此时 A/B 图形项尚未创建
    if (!m_lineA || !m_lineB || !m_labelAText || !m_labelBText || !m_abHighlight || !m_chart)
        return;
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
    // 左标签 = 亮度值（文本随 draw 更新，拖动时联动）
    gl.labelItem = new QGraphicsSimpleTextItem(m_chart);
    gl.labelItem->setFont(fontSans(8));
    gl.labelItem->setBrush(QBrush(color));
    gl.labelItem->setZValue(LABEL_Z_VALUE - 2);
    // 右标签 = 响度值（右 Y 轴同一像素高度的等效值）
    gl.labelItemRight = new QGraphicsSimpleTextItem(m_chart);
    gl.labelItemRight->setFont(fontSans(8));
    gl.labelItemRight->setBrush(QBrush(color));
    gl.labelItemRight->setZValue(LABEL_Z_VALUE - 2);
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
    gl.labelItem = new QGraphicsSimpleTextItem(m_chart);   // 文本随 draw 更新，拖动时联动
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
    if (gl.labelItemRight) {
        if (gl.labelItemRight->scene()) gl.labelItemRight->scene()->removeItem(gl.labelItemRight);
        delete gl.labelItemRight;
    }
    m_chartGuideLines.removeAt(index);
}

void ChartPanel::clearChartGuideLines()
{
    for (int i = m_chartGuideLines.size() - 1; i >= 0; --i)
        removeChartGuideLine(i);
}

QVector<ChartGuideData> ChartPanel::chartGuideLinesData() const
{
    QVector<ChartGuideData> out;
    out.reserve(m_chartGuideLines.size());
    for (const auto &gl : m_chartGuideLines)
        out.append({ gl.orientation == ChartGuideLine::Horizontal, gl.value, gl.color });
    return out;
}

void ChartPanel::setChartGuideLinesData(const QVector<ChartGuideData> &lines)
{
    clearChartGuideLines();
    for (const auto &d : lines) {
        if (d.horizontal)
            addHorizontalGuideLine(d.value, d.color);
        else
            addVerticalGuideLine(d.value, d.color);
    }
}

void ChartPanel::drawChartGuideLines()
{
    QRectF pa = m_chart->plotArea();
    if (pa.width() < 10)
        return;

    // Clean up old delta labels
    for (auto *item : m_chartGuideLineDeltaLabels) {
        if (item && item->scene()) item->scene()->removeItem(item);
        delete item;
    }
    m_chartGuideLineDeltaLabels.clear();

    // Collect horizontal guide Y values
    QVector<QPair<int, qreal>> horizontalGuides;
    for (int i = 0; i < m_chartGuideLines.size(); ++i) {
        if (m_chartGuideLines[i].orientation == ChartGuideLine::Horizontal)
            horizontalGuides.append(qMakePair(i, m_chartGuideLines[i].value));
    }
    std::sort(horizontalGuides.begin(), horizontalGuides.end(),
              [](const QPair<int, qreal> &a, const QPair<int, qreal> &b) { return a.second < b.second; });

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
                    // 左标签 = 亮度（左 Y 轴值），文本每次 draw 刷新 → 拖动联动；
                    // 标签置线段上方，避免与虚线重叠
                    if (gl.labelItem) {
                        gl.labelItem->setText(QString::number(y, 'f', 1));
                        gl.labelItem->setPos(pa.left() + 3,
                                             widgetY - gl.labelItem->boundingRect().height() - 1);
                    }
                    // 右标签 = 响度（右 Y 轴同一像素高度的等效值）；无音量轴时隐藏。
                    // 绘制在图表内侧右对齐（不遮右 Y 轴刻度），同样置线段上方
                    if (gl.labelItemRight) {
                        if (m_axisYVolume && m_axisYVolume->max() > m_axisYVolume->min()) {
                            qreal vol = m_axisYVolume->min()
                                        + normalized * (m_axisYVolume->max() - m_axisYVolume->min());
                            gl.labelItemRight->setText(QString::number(vol, 'f', 2));
                            gl.labelItemRight->setPos(pa.right() - gl.labelItemRight->boundingRect().width() - 3,
                                                      widgetY - gl.labelItemRight->boundingRect().height() - 1);
                            gl.labelItemRight->setVisible(true);
                        } else {
                            gl.labelItemRight->setVisible(false);
                        }
                    }
                }
            }
        } else {
            qreal x = mapTimeToX(static_cast<qint64>(gl.value));
            gl.lineItem->setLine(x, pa.top(), x, pa.bottom());
            if (gl.labelItem) {   // 时间文本每次 draw 刷新 → 拖动联动
                gl.labelItem->setText(formatDisplayTimeFull(displayMsOf(static_cast<qint64>(gl.value))));
                gl.labelItem->setPos(x + 3, pa.top() + 2);
            }
        }
        bool hovered = (i == m_hoveredGuideLine);
        QPen pen(gl.color, hovered ? 2 : 1, Qt::DashLine);
        gl.lineItem->setPen(pen);
    }

    // Draw delta labels between adjacent horizontal guide lines
    if (m_axisY && horizontalGuides.size() >= 2) {
        qreal yMin = m_axisY->min();
        qreal yMax = m_axisY->max();
        if (yMax > yMin) {
            QColor deltaColor(0xFF, 0x98, 0x1C); // orange
            const bool hasVol = m_axisYVolume && m_axisYVolume->max() > m_axisYVolume->min();
            for (int i = 1; i < horizontalGuides.size(); ++i) {
                qreal yLow = horizontalGuides[i - 1].second;
                qreal yHigh = horizontalGuides[i].second;
                qreal delta = yHigh - yLow;
                qreal yMid = (yLow + yHigh) / 2.0;
                qreal normalized = (yMid - yMin) / (yMax - yMin);
                qreal widgetY = pa.bottom() - normalized * pa.height();

                // 左侧：亮度差值 △（图表内侧左对齐）
                auto *deltaLabel = new QGraphicsSimpleTextItem(
                    QString("△%1").arg(QString::number(delta, 'f', 1)), m_chart);
                deltaLabel->setFont(fontSans(8));
                deltaLabel->setBrush(QBrush(deltaColor));
                deltaLabel->setZValue(LABEL_Z_VALUE - 2);
                deltaLabel->setPos(pa.left() + 4, widgetY - deltaLabel->boundingRect().height() / 2);
                deltaLabel->setVisible(true);
                m_chartGuideLineDeltaLabels.append(deltaLabel);

                // 右侧：响度差值 ▲（图表内侧右对齐，dB 差取绝对值）
                if (hasVol) {
                    qreal vMin = m_axisYVolume->min();
                    qreal vMax = m_axisYVolume->max();
                    qreal normLow = (yLow - yMin) / (yMax - yMin);
                    qreal normHigh = (yHigh - yMin) / (yMax - yMin);
                    qreal volDelta = qAbs((normHigh - normLow) * (vMax - vMin));
                    auto *volDeltaLabel = new QGraphicsSimpleTextItem(
                        QString("▲%1").arg(QString::number(volDelta, 'f', 2)), m_chart);
                    volDeltaLabel->setFont(fontSans(8));
                    volDeltaLabel->setBrush(QBrush(deltaColor));
                    volDeltaLabel->setZValue(LABEL_Z_VALUE - 2);
                    volDeltaLabel->setPos(pa.right() - volDeltaLabel->boundingRect().width() - 3,
                                          widgetY - volDeltaLabel->boundingRect().height() / 2);
                    volDeltaLabel->setVisible(true);
                    m_chartGuideLineDeltaLabels.append(volDeltaLabel);
                }
            }
        }
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

/// P-31 T5（R3 收口）：外部轴范围设置入口
void ChartPanel::setXAxisRange(qreal min, qreal max)
{
    if (auto *ax = axisX())
        ax->setRange(min, max);
}
