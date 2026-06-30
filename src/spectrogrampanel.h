/**
 * @file spectrogrampanel.h
 * @brief 频谱图面板：独立 QWidget + QImage 热力图渲染
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include <QWidget>
#include "domain/analysis_snapshot.h"

/**
 * @brief Standalone spectrogram display panel with QImage heatmap rendering.
 * Synchronizes X-axis with ChartPanel via onXAxisRangeChanged slot.
 */
class SpectrogramPanel : public QWidget
{
    Q_OBJECT

public:
    explicit SpectrogramPanel(QWidget *parent = nullptr);

    void setSpectrogramData(const AudioData &audio);
    void clear();

public slots:
    void onXAxisRangeChanged(qreal xMin, qreal xMax);
    void setCursorTime(qint64 timeMs);

protected:
    void paintEvent(QPaintEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;

private:
    QImage renderSpectrogram(int width, int height);
    QColor spectrogramColor(qreal value);
    static QString formatTimeShort(qint64 ms);
    static qint64 computeTimeStep(qint64 durationMs);
    static int lerp(int a, int b, qreal t) { return a + static_cast<int>((b - a) * t); }

    AudioData m_audioData;
    qreal m_viewXMin = 0;
    qreal m_viewXMax = 0;
    qreal m_minValue = -10.0;   // M6: Dynamic color range
    qreal m_maxValue = 5.0;
    QImage m_cachedImage;
    bool m_needsRedraw = true;
    qint64 m_cursorTimeMs = -1;  // Playback cursor position (-1 = hidden)
};
