/**
 * @file spectrogrampanel.cpp
 * @brief 频谱图面板实现：QImage 热力图 + X 轴同步
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "spectrogrampanel.h"
#include "i18n.h"

#include <QPainter>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QtMath>

// Label margins
static const int LEFT_MARGIN = 50;
static const int BOTTOM_MARGIN = 22;
static const int TOP_MARGIN = 2;

SpectrogramPanel::SpectrogramPanel(QWidget *parent)
    : QWidget(parent)
{
    setMinimumHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setMouseTracking(true);
}

void SpectrogramPanel::setSpectrogramData(const AudioData &audio)
{
    m_audioData = audio;
    // Normalize timeResolutionMs so renderSpectrogram/mouseMove never divide by 0.
    if (m_audioData.timeResolutionMs <= 0)
        m_audioData.timeResolutionMs = 32.0;

    if (!audio.spectrogram.isEmpty() && !audio.spectrogram[0].isEmpty()) {
        m_minValue = audio.spectrogram[0][0];
        m_maxValue = audio.spectrogram[0][0];
        for (const auto &bin : audio.spectrogram) {
            for (qreal v : bin) {
                if (v < m_minValue) m_minValue = v;
                if (v > m_maxValue) m_maxValue = v;
            }
        }
        if (qFuzzyCompare(m_minValue, m_maxValue)) {
            m_minValue -= 1.0;
            m_maxValue += 1.0;
        }

        // B4: If no viewport has been set yet (e.g. data arrives before any
        // rangeChanged signal, or --audio-only with durationMs=0), default the
        // view to the full audio extent so the heatmap isn't collapsed to 1 column.
        if (m_viewXMax <= m_viewXMin) {
            qreal res = audio.timeResolutionMs > 0 ? audio.timeResolutionMs : 32.0;
            m_viewXMin = 0;
            m_viewXMax = static_cast<qreal>(audio.spectrogram[0].size()) * res;
        }
    }

    m_needsRedraw = true;
    update();
}

void SpectrogramPanel::clear()
{
    m_audioData = AudioData();
    m_cachedImage = QImage();
    m_needsRedraw = true;
    update();
}

void SpectrogramPanel::onXAxisRangeChanged(qreal xMin, qreal xMax)
{
    // B4: qFuzzyCompare(1.0+x,...) has too-tight epsilon for large ms values
    // (it degenerates to exact equality). Use an absolute 0.5 ms tolerance.
    if (qAbs(m_viewXMin - xMin) < 0.5 && qAbs(m_viewXMax - xMax) < 0.5)
        return;
    m_viewXMin = xMin;
    m_viewXMax = xMax;
    m_needsRedraw = true;
    update();
}

void SpectrogramPanel::setCursorTime(qint64 timeMs)
{
    if (m_cursorTimeMs == timeMs)
        return;
    m_cursorTimeMs = timeMs;
    update();
}

void SpectrogramPanel::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event);
    QPainter painter(this);

    // Pure black background
    painter.fillRect(rect(), Qt::black);

    if (m_audioData.isEmpty()) {
        painter.setPen(QColor(100, 100, 100));
        painter.drawText(rect(), Qt::AlignCenter, lang("无频谱数据", "No spectrogram data"));
        return;
    }

    if (m_needsRedraw || m_cachedImage.size() != size()) {
        m_cachedImage = renderSpectrogram(width(), height());
        m_needsRedraw = false;
    }

    painter.drawImage(0, 0, m_cachedImage);

    // Draw playback cursor (cyan dashed line, synced with ChartPanel)
    if (m_cursorTimeMs >= 0 && m_viewXMax > m_viewXMin) {
        qreal xRatio = (m_cursorTimeMs - m_viewXMin) / (m_viewXMax - m_viewXMin);
        if (xRatio >= 0.0 && xRatio <= 1.0) {
            int heatW = width() - LEFT_MARGIN;
            int x = LEFT_MARGIN + static_cast<int>(xRatio * heatW);
            int yTop = TOP_MARGIN;
            int yBot = height() - BOTTOM_MARGIN;
            QPen cursorPen(QColor(0, 255, 255), 2, Qt::DashLine);
            painter.setPen(cursorPen);
            painter.drawLine(x, yTop, x, yBot);
        }
    }
}

void SpectrogramPanel::resizeEvent(QResizeEvent *event)
{
    Q_UNUSED(event);
    m_needsRedraw = true;
}

void SpectrogramPanel::mouseMoveEvent(QMouseEvent *event)
{
    if (m_audioData.isEmpty() || m_audioData.spectrogram.isEmpty() ||
        m_audioData.spectrogram[0].isEmpty()) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    int w = width() - LEFT_MARGIN;
    int h = height() - TOP_MARGIN - BOTTOM_MARGIN;
    if (w <= 0 || h <= 0) {
        QWidget::mouseMoveEvent(event);
        return;
    }

    QPoint pos = event->pos();
    int px = pos.x() - LEFT_MARGIN;
    int py = pos.y() - TOP_MARGIN;
    if (px < 0 || py < 0) {
        setToolTip("");
        QWidget::mouseMoveEvent(event);
        return;
    }

    qreal xRatio = static_cast<qreal>(px) / w;
    qint64 timeMs = static_cast<qint64>(m_viewXMin + xRatio * (m_viewXMax - m_viewXMin));

    qreal yRatio = static_cast<qreal>(py) / h;
    double nyquist = m_audioData.sampleRate / 2.0;
    double freq = nyquist * (1.0 - yRatio);

    int nFrames = m_audioData.spectrogram[0].size();
    int aIdx = static_cast<int>(timeMs / m_audioData.timeResolutionMs);
    aIdx = qBound(0, aIdx, nFrames - 1);

    int nFreqBins = m_audioData.spectrogram.size();
    int fIdx = nFreqBins - 1 - static_cast<int>(yRatio * nFreqBins);
    fIdx = qBound(0, fIdx, nFreqBins - 1);

    qreal value = m_audioData.spectrogram[fIdx][aIdx];

    QString tip = QString(lang("时间: %1  频率: %2 Hz  值: %3", "Time: %1  Freq: %2 Hz  Value: %3"))
        .arg(formatTimeShort(timeMs))
        .arg(freq, 0, 'f', 0)
        .arg(value, 0, 'f', 2);
    setToolTip(tip);

    QWidget::mouseMoveEvent(event);
}

QImage SpectrogramPanel::renderSpectrogram(int w, int h)
{
    if (m_audioData.spectrogram.isEmpty() || m_audioData.spectrogram[0].isEmpty() || w <= 0 || h <= 0)
        return QImage(w, h, QImage::Format_RGB32);

    // Pure black background
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(Qt::black);

    // Heatmap area (inside margins)
    int heatW = w - LEFT_MARGIN;
    int heatH = h - TOP_MARGIN - BOTTOM_MARGIN;
    if (heatW <= 0 || heatH <= 0)
        return img;

    // Determine audio index range for current viewport
    int nFrames = m_audioData.spectrogram[0].size();
    int aIdxMin = static_cast<int>(m_viewXMin / m_audioData.timeResolutionMs);
    int aIdxMax = static_cast<int>(m_viewXMax / m_audioData.timeResolutionMs);
    aIdxMin = qMax(0, qMin(aIdxMin, nFrames - 1));
    aIdxMax = qMax(0, qMin(aIdxMax, nFrames - 1));
    if (aIdxMin > aIdxMax)
        qSwap(aIdxMin, aIdxMax);

    int viewFrames = aIdxMax - aIdxMin + 1;
    if (viewFrames <= 0)
        viewFrames = 1;

    int nFreqBins = m_audioData.spectrogram.size();

    // Render heatmap into the image
    for (int x = 0; x < heatW; ++x) {
        int srcFrame = aIdxMin + (x * viewFrames) / heatW;
        if (srcFrame >= nFrames) srcFrame = nFrames - 1;

        for (int y = 0; y < heatH; ++y) {
            // Y: 0=top=high freq, heatH-1=bottom=low freq
            int freqBin = nFreqBins - 1 - (y * nFreqBins) / heatH;
            if (freqBin < 0) freqBin = 0;
            if (freqBin >= nFreqBins) freqBin = nFreqBins - 1;

            qreal value = m_audioData.spectrogram[freqBin][srcFrame];
            QColor c = spectrogramColor(value);
            img.setPixelColor(LEFT_MARGIN + x, TOP_MARGIN + y, c);
        }
    }

    // --- Draw axes labels ---
    QPainter painter(&img);
    painter.setRenderHint(QPainter::Antialiasing);

    // Cyan color for all labels
    QColor labelColor(0, 200, 200);
    QColor tickColor(0, 150, 150, 180);

    QFont font("Consolas", 9, QFont::Bold);
    painter.setFont(font);
    painter.setPen(labelColor);

    // --- Y axis: frequency labels ---
    double nyquist = m_audioData.sampleRate / 2.0;
    // Choose ~6 nice frequency divisions
    double freqStep = 500.0;  // default 500 Hz
    if (nyquist > 8000) freqStep = 2000.0;
    else if (nyquist > 4000) freqStep = 1000.0;
    else if (nyquist > 2000) freqStep = 500.0;
    else freqStep = 200.0;

    for (double freq = 0; freq <= nyquist; freq += freqStep) {
        if (freq <= 0) continue;
        int y = TOP_MARGIN + heatH - static_cast<int>((freq / nyquist) * heatH);
        if (y < TOP_MARGIN || y > TOP_MARGIN + heatH) continue;

        // Tick mark
        painter.setPen(tickColor);
        painter.drawLine(LEFT_MARGIN - 5, y, LEFT_MARGIN, y);

        // Label text
        painter.setPen(labelColor);
        QString label;
        if (freq >= 1000)
            label = QString("%1k").arg(freq / 1000.0, 0, 'f', 1);
        else
            label = QString::number(static_cast<int>(freq));
        QRect textRect(0, y - 8, LEFT_MARGIN - 8, 16);
        painter.drawText(textRect, Qt::AlignRight | Qt::AlignVCenter, label);
    }

    // --- Y axis line ---
    painter.setPen(tickColor);
    painter.drawLine(LEFT_MARGIN, TOP_MARGIN, LEFT_MARGIN, TOP_MARGIN + heatH);

    // --- X axis: time labels ---
    qint64 visibleDuration = static_cast<qint64>(m_viewXMax - m_viewXMin);
    if (visibleDuration <= 0) visibleDuration = 1000;

    // Compute nice time step (target ~6-8 labels)
    qint64 timeStep = computeTimeStep(visibleDuration);
    qint64 startTime = (static_cast<qint64>(m_viewXMin) / timeStep) * timeStep;

    for (qint64 t = startTime; t <= static_cast<qint64>(m_viewXMax); t += timeStep) {
        if (t < static_cast<qint64>(m_viewXMin)) continue;
        qreal xRatio = (t - m_viewXMin) / (m_viewXMax - m_viewXMin);
        int x = LEFT_MARGIN + static_cast<int>(xRatio * heatW);
        if (x < LEFT_MARGIN || x > LEFT_MARGIN + heatW) continue;

        // Tick mark
        painter.setPen(tickColor);
        painter.drawLine(x, TOP_MARGIN + heatH, x, TOP_MARGIN + heatH + 5);

        // Label text
        painter.setPen(labelColor);
        QRect textRect(x - 30, TOP_MARGIN + heatH + 5, 60, BOTTOM_MARGIN - 5);
        painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop, formatTimeShort(t));
    }

    // --- X axis line ---
    painter.setPen(tickColor);
    painter.drawLine(LEFT_MARGIN, TOP_MARGIN + heatH, LEFT_MARGIN + heatW, TOP_MARGIN + heatH);

    return img;
}

qint64 SpectrogramPanel::computeTimeStep(qint64 durationMs)
{
    if (durationMs <= 0)
        return 60000;

    const qint64 candidates[] = {
        1000, 2000, 5000, 10000, 15000, 30000,
        60000, 120000, 300000, 600000
    };

    qint64 target = durationMs / 6;
    for (qint64 step : candidates) {
        if (step >= target)
            return step;
    }
    return candidates[9];
}

QString SpectrogramPanel::formatTimeShort(qint64 ms)
{
    if (ms < 0) ms = 0;
    int totalSec = static_cast<int>(ms / 1000);
    int h = totalSec / 3600;
    int m = (totalSec % 3600) / 60;
    int s = totalSec % 60;
    if (h > 0)
        return QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
    return QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
}

QColor SpectrogramPanel::spectrogramColor(qreal value)
{
    qreal range = m_maxValue - m_minValue;
    if (range <= 0) range = 1.0;
    qreal t = qBound(0.0, (value - m_minValue) / range, 1.0);

    int r, g, b;

    if (t < 0.05) {
        // Near-black (silence)
        r = 5; g = 3; b = 2;
    } else if (t < 0.3) {
        // Dark brown → brown
        qreal s = (t - 0.05) / 0.25;
        r = lerp(15, 100, s);
        g = lerp(5, 40, s);
        b = lerp(2, 10, s);
    } else if (t < 0.55) {
        // Brown → amber/orange
        qreal s = (t - 0.3) / 0.25;
        r = lerp(100, 200, s);
        g = lerp(40, 100, s);
        b = lerp(10, 20, s);
    } else if (t < 0.75) {
        // Amber → bright orange
        qreal s = (t - 0.55) / 0.2;
        r = lerp(200, 240, s);
        g = lerp(100, 160, s);
        b = lerp(20, 40, s);
    } else if (t < 0.9) {
        // Bright orange → yellow
        qreal s = (t - 0.75) / 0.15;
        r = lerp(240, 255, s);
        g = lerp(160, 220, s);
        b = lerp(40, 80, s);
    } else {
        // Yellow → near-white
        qreal s = (t - 0.9) / 0.1;
        r = 255;
        g = lerp(220, 248, s);
        b = lerp(80, 200, s);
    }

    return QColor(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
}
