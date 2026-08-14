/**
 * @file spectrogrampanel_enhanced.cpp
 * @brief 语谱图增强版实现：QOpenGLWidget + GPU 渲染 + 对数频率轴 + 频率缩放
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.4
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "spectrogrampanel_enhanced.h"
#include "domain/tick_utils.h"
#include "i18n.h"

#include <QPainter>
#include <QResizeEvent>
#include <QMouseEvent>
#include <QWheelEvent>
#include <QtMath>
#include <QOpenGLVertexArrayObject>
#include <cmath>
#include <QSurfaceFormat>
#include <QDebug>

// Vertex shader (GLSL 3.30)
static const char *vertexShaderSource = R"(
#version 330 core
in vec4 position;
in vec2 texCoord;
out vec2 vTexCoord;
void main() {
    gl_Position = position;
    vTexCoord = texCoord;
}
)";

// Fragment shader (GLSL 3.30)
static const char *fragmentShaderSource = R"(
#version 330 core
uniform sampler2D spectrogramTexture;
uniform sampler2D colorLUTTexture;
uniform float xMin;
uniform float xMax;
uniform float yMin;
uniform float yMax;
uniform float nyquist;
uniform int useLogScale;

in vec2 vTexCoord;
out vec4 fragColor;

float logFreqMap(float t) {
    float logMin = log(yMin);
    float logMax = log(yMax);
    float freq = exp(logMin + t * (logMax - logMin));
    return freq / nyquist;
}

void main() {
    float x = xMin + vTexCoord.x * (xMax - xMin);
    float y;
    if (useLogScale == 1) {
        y = logFreqMap(vTexCoord.y);
    } else {
        y = yMin / nyquist + vTexCoord.y * (yMax - yMin) / nyquist;
    }
    x = clamp(x, 0.0, 1.0);
    y = clamp(y, 0.0, 1.0);
    float value = texture(spectrogramTexture, vec2(x, y)).r;
    fragColor = texture(colorLUTTexture, vec2(value, 0.5));
}
)";

// Standard audio frequency labels for logarithmic scale
static const double logFreqLabels[] = {
    20, 25, 31.5, 40, 50, 63, 80, 100, 125, 160, 200, 250, 315, 400, 500, 630, 800,
    1000, 1250, 1600, 2000, 2500, 3150, 4000, 5000, 6300, 8000, 10000, 12500, 16000, 20000
};

SpectrogramPanelEnhanced::SpectrogramPanelEnhanced(QWidget *parent)
    : QOpenGLWidget(parent)
{
    setMinimumHeight(100);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    setMouseTracking(true);

    QSurfaceFormat format;
    format.setVersion(3, 3);
    format.setProfile(QSurfaceFormat::CoreProfile);
    setFormat(format);
}

SpectrogramPanelEnhanced::~SpectrogramPanelEnhanced()
{
    makeCurrent();
    delete m_program;
    delete m_spectrogramTexture;
    delete m_colorLUTTexture;
    delete m_vao;
    m_vertexBuffer.destroy();
    doneCurrent();
}

void SpectrogramPanelEnhanced::initializeGL()
{
    if (!initializeOpenGLFunctions()) {
        m_glError = "Failed to initialize OpenGL 3.3 Core functions";
        qWarning() << m_glError;
        return;
    }

    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);

    glGetIntegerv(GL_MAX_TEXTURE_SIZE, &m_maxTextureSize);
    qDebug() << "GL_MAX_TEXTURE_SIZE:" << m_maxTextureSize;

    initShaders();
    if (!m_shaderOk)
        return;

    float vertices[] = {
        -1.0f, -1.0f,  0.0f, 0.0f,
         1.0f, -1.0f,  1.0f, 0.0f,
        -1.0f,  1.0f,  0.0f, 1.0f,
         1.0f,  1.0f,  1.0f, 1.0f,
    };

    m_vao = new QOpenGLVertexArrayObject(this);
    m_vao->create();
    m_vao->bind();

    m_vertexBuffer = QOpenGLBuffer(QOpenGLBuffer::VertexBuffer);
    m_vertexBuffer.create();
    m_vertexBuffer.bind();
    m_vertexBuffer.allocate(vertices, sizeof(vertices));

    m_program->bind();
    m_program->enableAttributeArray("position");
    m_program->setAttributeBuffer("position", GL_FLOAT, 0, 2, 4 * sizeof(float));
    m_program->enableAttributeArray("texCoord");
    m_program->setAttributeBuffer("texCoord", GL_FLOAT, 2 * sizeof(float), 2, 4 * sizeof(float));
    m_program->release();

    m_vao->release();

    buildColorLUT();
    uploadColorLUTTexture();
}

void SpectrogramPanelEnhanced::resizeGL(int w, int h)
{
    glViewport(0, 0, w, h);
}

void SpectrogramPanelEnhanced::paintGL()
{
    // Recompute margins from stored chart plot area (fixes alignment on resize)
    if (m_storedChartPlotArea.width() > 0 && m_storedChartPlotArea.height() > 0) {
        int newLeft = static_cast<int>(m_storedChartPlotArea.left());
        int newRight = width() - static_cast<int>(m_storedChartPlotArea.right());
        if (newLeft < 20) newLeft = 20;
        if (newLeft > width() / 2) newLeft = width() / 2;  // Safety: don't exceed half width
        if (newRight < 0) newRight = 0;
        if (newRight > width() / 2) newRight = width() / 2;
        m_leftMargin = newLeft;
        m_rightMargin = newRight;
    }

    glClear(GL_COLOR_BUFFER_BIT);

    if (!m_shaderOk || !m_program) {
        QPainter painter(this);
        drawError(painter, m_glError.isEmpty() ? "Shader initialization failed" : m_glError);
        return;
    }

    if (!m_audioData.hasSpectrogram()) {
        if (m_audioData.hasVolume()) {
            QPainter painter(this);
            drawError(painter, lang("频谱数据不可用", "Spectrogram data unavailable"));
        }
        return;
    }

    if (m_textureDirty || !m_spectrogramTexture) {
        uploadSpectrogramTexture();
        m_textureDirty = false;
    }

    if (!m_spectrogramTexture || !m_colorLUTTexture)
        return;

    m_program->bind();
    m_vao->bind();

    qreal totalDurationMs = m_audioData.safeTimeResolutionMs() * m_audioData.spectrogram[0].size();
    if (totalDurationMs > 0) {
        // 全屏四边形纹理坐标 [0,1] 覆盖整个 widget，但语谱图只渲染在带边距的子区域内。
        // 需要将 xMin/xMax 扩展，使 GPU 在子区域内采样的纹理坐标恰好对应正确的帧位置。
        qreal V = m_viewXMax - m_viewXMin;
        qreal H = width() - m_leftMargin - m_rightMargin;
        if (H > 0) {
            float xNormMin = static_cast<float>((m_viewXMin - m_leftMargin * V / H) / totalDurationMs);
            float xNormMax = static_cast<float>((m_viewXMax + m_rightMargin * V / H) / totalDurationMs);
            m_program->setUniformValue("xMin", xNormMin);
            m_program->setUniformValue("xMax", xNormMax);
        } else {
            m_program->setUniformValue("xMin", 0.0f);
            m_program->setUniformValue("xMax", 1.0f);
        }
    } else {
        m_program->setUniformValue("xMin", 0.0f);
        m_program->setUniformValue("xMax", 1.0f);
    }
    m_program->setUniformValue("yMin", static_cast<float>(m_viewYMin));
    m_program->setUniformValue("yMax", static_cast<float>(m_viewYMax));
    m_program->setUniformValue("nyquist", static_cast<float>(m_audioData.sampleRate / 2.0));
    m_program->setUniformValue("useLogScale", m_freqScale == FreqScale::Logarithmic ? 1 : 0);

    glActiveTexture(GL_TEXTURE0);
    m_spectrogramTexture->bind();
    m_program->setUniformValue("spectrogramTexture", 0);

    glActiveTexture(GL_TEXTURE1);
    m_colorLUTTexture->bind();
    m_program->setUniformValue("colorLUTTexture", 1);

    glEnable(GL_SCISSOR_TEST);
    const qreal dpr = devicePixelRatioF();
    glScissor(static_cast<GLint>(m_leftMargin * dpr),
              static_cast<GLint>(BOTTOM_MARGIN * dpr),
              static_cast<GLsizei>((width() - m_leftMargin - m_rightMargin) * dpr),
              static_cast<GLsizei>((height() - BOTTOM_MARGIN - TOP_MARGIN) * dpr));
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    glDisable(GL_SCISSOR_TEST);

    m_vao->release();
    m_program->release();

    // Reset GL state before QPainter
    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, 0);
    glUseProgram(0);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    drawAxes(painter);

    if (m_cursorTimeMs >= 0 && m_viewXMax > m_viewXMin) {
        int heatW = width() - m_leftMargin - m_rightMargin;
        int yTop = TOP_MARGIN;
        int yBot = height() - BOTTOM_MARGIN;
        qreal xRatio = (m_cursorTimeMs - m_viewXMin) / (m_viewXMax - m_viewXMin);
        if (xRatio >= 0.0 && xRatio <= 1.0) {
            int x = m_leftMargin + static_cast<int>(xRatio * heatW);
            QPen cursorPen(QColor(0xFF, 0x98, 0x1C), 2, Qt::DashLine);
            painter.setPen(cursorPen);
            painter.drawLine(x, yTop, x, yBot);
        }
    }
}

void SpectrogramPanelEnhanced::initShaders()
{
    m_program = new QOpenGLShaderProgram(this);

    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Vertex, vertexShaderSource)) {
        m_glError = "Vertex shader compile failed: " + m_program->log();
        qWarning() << m_glError;
        delete m_program;
        m_program = nullptr;
        return;
    }
    if (!m_program->addShaderFromSourceCode(QOpenGLShader::Fragment, fragmentShaderSource)) {
        m_glError = "Fragment shader compile failed: " + m_program->log();
        qWarning() << m_glError;
        delete m_program;
        m_program = nullptr;
        return;
    }
    if (!m_program->link()) {
        m_glError = "Shader link failed: " + m_program->log();
        qWarning() << m_glError;
        delete m_program;
        m_program = nullptr;
        return;
    }

    m_shaderOk = true;
}

void SpectrogramPanelEnhanced::uploadSpectrogramTexture()
{
    if (m_audioData.spectrogram.isEmpty() || m_audioData.spectrogram[0].isEmpty())
        return;

    int nFreqBins = m_audioData.spectrogram.size();
    int nFrames = m_audioData.spectrogram[0].size();

    // Downsample columns if exceeding GPU max texture size
    int texWidth = nFrames;
    int step = 1;
    if (nFrames > m_maxTextureSize) {
        step = qCeil(static_cast<double>(nFrames) / m_maxTextureSize);
        texWidth = (nFrames + step - 1) / step;
    }

    m_textureData.resize(nFreqBins * texWidth);
    qreal range = m_maxValue - m_minValue;
    if (range <= 0) range = 1.0;

    for (int y = 0; y < nFreqBins; ++y) {
        for (int x = 0; x < texWidth; ++x) {
            int srcX = qMin(x * step, nFrames - 1);
            qreal normalized = (m_audioData.spectrogram[y][srcX] - m_minValue) / range;
            m_textureData[y * texWidth + x] = static_cast<float>(qBound(0.0, normalized, 1.0));
        }
    }

    if (!m_spectrogramTexture) {
        m_spectrogramTexture = new QOpenGLTexture(QOpenGLTexture::Target2D);
        m_spectrogramTexture->create();
    }

    m_spectrogramTexture->bind();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_R32F, texWidth, nFreqBins, 0,
                 GL_RED, GL_FLOAT, m_textureData.constData());
    m_spectrogramTexture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
    m_spectrogramTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
    m_spectrogramTexture->release();

    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
        qWarning() << "glTexImage2D (spectrogram) error:" << err
                    << "size:" << texWidth << "x" << nFreqBins;

    m_textureWidth = texWidth;
    m_textureHeight = nFreqBins;
}

void SpectrogramPanelEnhanced::uploadColorLUTTexture()
{
    if (m_colorLUT.isEmpty())
        return;

    QVector<float> lutData(256 * 4);
    for (int i = 0; i < 256; ++i) {
        QRgb color = m_colorLUT[i];
        lutData[i * 4 + 0] = qRed(color) / 255.0f;
        lutData[i * 4 + 1] = qGreen(color) / 255.0f;
        lutData[i * 4 + 2] = qBlue(color) / 255.0f;
        lutData[i * 4 + 3] = 1.0f;
    }

    if (!m_colorLUTTexture) {
        m_colorLUTTexture = new QOpenGLTexture(QOpenGLTexture::Target2D);
        m_colorLUTTexture->create();
    }

    m_colorLUTTexture->bind();
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA32F, 256, 1, 0,
                 GL_RGBA, GL_FLOAT, lutData.constData());
    m_colorLUTTexture->setMinMagFilters(QOpenGLTexture::Linear, QOpenGLTexture::Linear);
    m_colorLUTTexture->setWrapMode(QOpenGLTexture::ClampToEdge);
    m_colorLUTTexture->release();

    GLenum err = glGetError();
    if (err != GL_NO_ERROR)
        qWarning() << "glTexImage2D (color LUT) error:" << err;
}

/// @brief 离屏 CPU 光栅化：与 GPU 着色器同一归一化/颜色 LUT/视窗/频率轴。
/// 目标尺寸任意（快照全宽输出），不经 GL——grabFramebuffer 只能拿屏幕当前
/// 尺寸且 dock 内 resize 后 FBO 错位（实测右半黑）。
QImage SpectrogramPanelEnhanced::renderHeatmapImage(const QSize &targetSize)
{
    if (targetSize.isEmpty() || !m_audioData.hasSpectrogram())
        return QImage();
    if (m_colorLUT.isEmpty())
        buildColorLUT();

    const int nFreqBins = m_audioData.spectrogram.size();
    const int nFrames = m_audioData.spectrogram[0].size();
    const qreal resMs = m_audioData.safeTimeResolutionMs();
    const qreal totalMs = resMs * nFrames;
    const double nyquist = m_audioData.sampleRate / 2.0;
    if (totalMs <= 0 || nyquist <= 0)
        return QImage();

    // X 视窗与屏上一致（与图表段同一条时间轴）；Y 视窗与频率轴设置同理
    qreal x0 = m_viewXMin, x1 = m_viewXMax;
    if (x1 <= x0) { x0 = 0; x1 = totalMs; }
    const double y0 = m_viewYMin, y1 = m_viewYMax;
    const bool logScale = (m_freqScale == FreqScale::Logarithmic);
    const double logMin = std::log(qMax(1.0, y0));
    const double logMax = std::log(qMax(1.0, y1));
    const qreal range = qFuzzyIsNull(m_maxValue - m_minValue)
        ? 1.0 : (m_maxValue - m_minValue);

    const int W = targetSize.width(), H = targetSize.height();
    QImage img(W, H, QImage::Format_ARGB32);
    for (int r = 0; r < H; ++r) {
        const double t = 1.0 - double(r) / qMax(1, H - 1);   // 顶行 = 最高频
        const double freqFrac = logScale
            ? std::exp(logMin + t * (logMax - logMin)) / nyquist
            : (y0 + t * (y1 - y0)) / nyquist;
        const int bin = qBound(0, int(freqFrac * (nFreqBins - 1) + 0.5),
                               nFreqBins - 1);
        const auto &bins = m_audioData.spectrogram[bin];
        QRgb *line = reinterpret_cast<QRgb *>(img.scanLine(r));
        for (int c = 0; c < W; ++c) {
            const qreal ms = x0 + (qreal(c) / qMax(1, W - 1)) * (x1 - x0);
            const int fr = qBound(0, int(ms / resMs + 0.5), nFrames - 1);
            const qreal norm = qBound(0.0, (bins[fr] - m_minValue) / range, 1.0);
            line[c] = m_colorLUT[qBound(0, int(norm * 255 + 0.5), 255)]
                      | 0xFF000000;
        }
    }

    // 时间光标（与 paintGL 同款橙色虚线）
    if (m_cursorTimeMs >= 0 && x1 > x0) {
        const qreal ratio = (m_cursorTimeMs - x0) / (x1 - x0);
        if (ratio >= 0.0 && ratio <= 1.0) {
            QPainter p(&img);
            p.setPen(QPen(QColor(0xFF, 0x98, 0x1C), 2, Qt::DashLine));
            const int cx = int(ratio * (W - 1));
            p.drawLine(cx, 0, cx, H - 1);
        }
    }
    return img;
}

void SpectrogramPanelEnhanced::buildColorLUT()
{
    m_colorLUT.resize(256);

    struct ColorPoint { qreal t; int r, g, b; };

    QVector<ColorPoint> thermalPoints = {
        {0.00, 0, 0, 0}, {0.10, 10, 5, 40}, {0.25, 60, 10, 100},
        {0.40, 140, 20, 50}, {0.55, 200, 50, 20}, {0.70, 240, 120, 10},
        {0.85, 255, 200, 40}, {1.00, 255, 255, 240}
    };
    QVector<ColorPoint> infernoPoints = {
        {0.00, 0, 0, 4}, {0.20, 40, 11, 84}, {0.40, 101, 21, 110},
        {0.60, 159, 42, 99}, {0.80, 217, 93, 42}, {0.95, 245, 175, 45},
        {1.00, 252, 255, 164}
    };
    QVector<ColorPoint> viridisPoints = {
        {0.00, 68, 1, 84}, {0.20, 59, 82, 139}, {0.40, 33, 145, 140},
        {0.60, 94, 201, 98}, {0.80, 253, 231, 37}, {1.00, 255, 255, 204}
    };

    const QVector<ColorPoint> *points;
    switch (m_colorScale) {
        case ColorScale::Inferno: points = &infernoPoints; break;
        case ColorScale::Viridis: points = &viridisPoints; break;
        default: points = &thermalPoints; break;
    }

    for (int i = 0; i < 256; ++i) {
        qreal t = i / 255.0;
        int idx = 0;
        for (int j = 0; j < points->size() - 1; ++j) {
            if (t >= points->at(j).t && t <= points->at(j + 1).t) { idx = j; break; }
        }
        const ColorPoint &p0 = points->at(idx);
        const ColorPoint &p1 = points->at(idx + 1);
        qreal localT = (p1.t > p0.t) ? (t - p0.t) / (p1.t - p0.t) : 0.0;
        int r = p0.r + static_cast<int>((p1.r - p0.r) * localT);
        int g = p0.g + static_cast<int>((p1.g - p0.g) * localT);
        int b = p0.b + static_cast<int>((p1.b - p0.b) * localT);
        m_colorLUT[i] = qRgb(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
    }
}

void SpectrogramPanelEnhanced::drawAxes(QPainter &painter)
{
    int heatW = width() - m_leftMargin - m_rightMargin;
    int heatH = height() - TOP_MARGIN - BOTTOM_MARGIN;
    if (heatW <= 0 || heatH <= 0)
        return;

    QColor labelColor(0xFF, 0x98, 0x1C);
    QColor tickColor(0xFF, 0x98, 0x1C, 180);
    QFont font("Consolas", 9, QFont::Bold);
    painter.setFont(font);

    double nyquist = m_audioData.sampleRate / 2.0;
    int minLabelSpacing = 25;
    int lastLabelY = -999;

    if (m_freqScale == FreqScale::Logarithmic) {
        for (double freq : logFreqLabels) {
            if (freq < m_viewYMin || freq > m_viewYMax)
                continue;
            double logMin = log(m_viewYMin);
            double logMax = log(m_viewYMax);
            double yRatio = (logMax > logMin) ? (log(freq) - logMin) / (logMax - logMin) : 0.5;
            int y = TOP_MARGIN + heatH - static_cast<int>(yRatio * heatH);
            if (y < TOP_MARGIN || y > TOP_MARGIN + heatH)
                continue;
            if (qAbs(y - lastLabelY) < minLabelSpacing)
                continue;
            lastLabelY = y;

            painter.setPen(tickColor);
            painter.drawLine(m_leftMargin - 5, y, m_leftMargin, y);
            painter.setPen(labelColor);
            QString label = (freq >= 1000)
                ? QString("%1k").arg(freq / 1000.0, 0, 'f', 1)
                : QString::number(static_cast<int>(freq));
            painter.drawText(QRect(0, y - 8, m_leftMargin - 8, 16),
                             Qt::AlignRight | Qt::AlignVCenter, label);
        }
    } else {
        double freqStep = (nyquist > 8000) ? 2000.0 : (nyquist > 4000) ? 1000.0 : (nyquist > 2000) ? 500.0 : 200.0;
        double pixelsPerFreq = (m_viewYMax > m_viewYMin) ? heatH / (m_viewYMax - m_viewYMin) : 1.0;
        while (freqStep * pixelsPerFreq < minLabelSpacing && freqStep < nyquist)
            freqStep *= 2;

        for (double freq = 0; freq <= nyquist; freq += freqStep) {
            if (freq < m_viewYMin || freq > m_viewYMax)
                continue;
            double yRatio = (m_viewYMax > m_viewYMin) ? (freq - m_viewYMin) / (m_viewYMax - m_viewYMin) : 0.5;
            int y = TOP_MARGIN + heatH - static_cast<int>(yRatio * heatH);
            if (y < TOP_MARGIN || y > TOP_MARGIN + heatH)
                continue;
            if (qAbs(y - lastLabelY) < minLabelSpacing)
                continue;
            lastLabelY = y;

            painter.setPen(tickColor);
            painter.drawLine(m_leftMargin - 5, y, m_leftMargin, y);
            painter.setPen(labelColor);
            QString label = (freq >= 1000)
                ? QString("%1k").arg(freq / 1000.0, 0, 'f', 1)
                : QString::number(static_cast<int>(freq));
            painter.drawText(QRect(0, y - 8, m_leftMargin - 8, 16),
                             Qt::AlignRight | Qt::AlignVCenter, label);
        }
    }

    painter.setPen(tickColor);
    painter.drawLine(m_leftMargin, TOP_MARGIN, m_leftMargin, TOP_MARGIN + heatH);

    // X-axis: time labels（像素密度自适应：长视频/小时级自动放大步长，
    // 避免标签糊成一团——现场反馈；算法见 domain/tick_utils.h 单测）
    const qint64 visibleDuration = static_cast<qint64>(m_viewXMax - m_viewXMin);
    const qint64 timeStep = computeXAxisStepMs(visibleDuration, heatW);

    qint64 startTime = (static_cast<qint64>(m_viewXMin) / timeStep) * timeStep;
    for (qint64 t = startTime; t <= static_cast<qint64>(m_viewXMax); t += timeStep) {
        if (t < static_cast<qint64>(m_viewXMin)) continue;
        qreal xRatio = (t - m_viewXMin) / (m_viewXMax - m_viewXMin);
        int x = m_leftMargin + static_cast<int>(xRatio * heatW);
        if (x < m_leftMargin || x > m_leftMargin + heatW) continue;

        painter.setPen(tickColor);
        painter.drawLine(x, TOP_MARGIN + heatH, x, TOP_MARGIN + heatH + 5);
        painter.setPen(labelColor);

        int totalSec = static_cast<int>(t / 1000);
        int h = totalSec / 3600;
        int m = (totalSec % 3600) / 60;
        int s = totalSec % 60;
        QString timeText = (h > 0)
            ? QString("%1:%2:%3").arg(h).arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'))
            : QString("%1:%2").arg(m, 2, 10, QChar('0')).arg(s, 2, 10, QChar('0'));
        painter.drawText(QRect(x - 30, TOP_MARGIN + heatH + 5, 60, BOTTOM_MARGIN - 5),
                         Qt::AlignHCenter | Qt::AlignTop, timeText);
    }

    painter.setPen(tickColor);
    painter.drawLine(m_leftMargin, TOP_MARGIN + heatH, m_leftMargin + heatW, TOP_MARGIN + heatH);
}

void SpectrogramPanelEnhanced::drawError(QPainter &painter, const QString &msg)
{
    painter.setPen(QColor(255, 100, 100));
    painter.setFont(QFont("Consolas", 10));
    painter.drawText(rect(), Qt::AlignCenter, msg);
}

double SpectrogramPanelEnhanced::pixelToFreq(int y) const
{
    int heatH = height() - TOP_MARGIN - BOTTOM_MARGIN;
    if (heatH <= 0 || y < TOP_MARGIN || y > TOP_MARGIN + heatH)
        return 0;
    double yRatio = static_cast<double>(TOP_MARGIN + heatH - y) / heatH;
    if (m_freqScale == FreqScale::Logarithmic) {
        double logMin = log(m_viewYMin);
        double logMax = log(m_viewYMax);
        return exp(logMin + yRatio * (logMax - logMin));
    } else {
        return m_viewYMin + yRatio * (m_viewYMax - m_viewYMin);
    }
}

int SpectrogramPanelEnhanced::freqToBin(double freq) const
{
    double nyquist = m_audioData.sampleRate / 2.0;
    int nFreqBins = m_audioData.spectrogram.isEmpty() ? 0 : m_audioData.spectrogram.size();
    if (nFreqBins == 0) return 0;
    double ratio = freq / nyquist;
    return qBound(0, static_cast<int>(ratio * (nFreqBins - 1)), nFreqBins - 1);
}

void SpectrogramPanelEnhanced::setSpectrogramData(const AudioData &audio)
{
    qDebug() << "[setSpectrogramData] spectrogram.size:" << audio.spectrogram.size()
             << "volume.size:" << audio.volume.size()
             << "sampleRate:" << audio.sampleRate
             << "hopLength:" << audio.hopLength;
    m_audioData = audio;
    if (m_audioData.timeResolutionMs <= 0)
        m_audioData.timeResolutionMs = 1000.0 * m_audioData.hopLength / m_audioData.sampleRate;

    if (!audio.spectrogram.isEmpty() && !audio.spectrogram[0].isEmpty()) {
        // Use max value from Python, but preserve user's noise floor threshold
        if (audio.specMin < audio.specMax) {
            m_maxValue = audio.specMax;
            // Keep m_minValue at user's threshold (set via setNoiseFloor)
            // Only update if the data's min is higher than the threshold
            if (audio.specMin > m_minValue)
                m_minValue = audio.specMin;
        } else {
            qreal dataMin = audio.spectrogram[0][0];
            qreal dataMax = audio.spectrogram[0][0];
            for (const auto &bin : audio.spectrogram) {
                for (qreal v : bin) {
                    if (v < dataMin) dataMin = v;
                    if (v > dataMax) dataMax = v;
                }
            }
            m_maxValue = dataMax;
            if (dataMin > m_minValue)
                m_minValue = dataMin;
        }
        if (qFuzzyCompare(m_minValue, m_maxValue)) {
            m_minValue -= 1.0;
            m_maxValue += 1.0;
        }

        // Only reset Y range on first load (B9)
        if (!m_yRangeInitialized) {
            double nyquist = m_audioData.sampleRate / 2.0;
            if (m_freqScale == FreqScale::Logarithmic) {
                m_viewYMin = 20.0;
                m_viewYMax = nyquist;
            } else {
                m_viewYMin = 0;
                m_viewYMax = qMin(12000.0, nyquist);
            }
            m_yRangeInitialized = true;
        }

        // Always set X range based on spectrogram's own duration
        qreal res = m_audioData.safeTimeResolutionMs();
        m_viewXMin = 0;
        m_viewXMax = static_cast<qreal>(audio.spectrogram[0].size()) * res;

        m_textureDirty = true;
    }

    update();
}

void SpectrogramPanelEnhanced::clear()
{
    m_audioData = AudioData();
    m_textureData.clear();
    m_textureDirty = true;
    m_yRangeInitialized = false;
    m_storedChartPlotArea = QRectF();  // Reset cached chart plot area
    update();
}

void SpectrogramPanelEnhanced::setColorScale(ColorScale scale)
{
    if (m_colorScale != scale) {
        m_colorScale = scale;
        buildColorLUT();
        makeCurrent();
        uploadColorLUTTexture();
        doneCurrent();
        update();
        emit colorScaleChanged(scale);
    }
}

void SpectrogramPanelEnhanced::setFreqScale(FreqScale scale)
{
    if (m_freqScale != scale) {
        m_freqScale = scale;
        update();
        emit freqScaleChanged(scale);
    }
}

void SpectrogramPanelEnhanced::setNoiseFloor(qreal value)
{
    if (!qFuzzyCompare(m_minValue, value)) {
        m_minValue = value;
        m_textureDirty = true;
        update();
    }
}

void SpectrogramPanelEnhanced::onXAxisRangeChanged(qreal xMin, qreal xMax)
{
    if (qAbs(m_viewXMin - xMin) < 0.5 && qAbs(m_viewXMax - xMax) < 0.5)
        return;
    m_viewXMin = xMin;
    m_viewXMax = xMax;
    update();
}

void SpectrogramPanelEnhanced::setCursorTime(qint64 timeMs)
{
    if (m_cursorTimeMs == timeMs)
        return;
    m_cursorTimeMs = timeMs;
    update();
}

void SpectrogramPanelEnhanced::onChartPlotAreaChanged(QRectF plotArea)
{
    m_storedChartPlotArea = plotArea;
    int newLeft = static_cast<int>(plotArea.left());
    int newRight = width() - static_cast<int>(plotArea.right());
    if (newLeft < 20) newLeft = 20;
    if (newRight < 0) newRight = 0;
    if (newLeft != m_leftMargin || newRight != m_rightMargin) {
        m_leftMargin = newLeft;
        m_rightMargin = newRight;
        update();
    }
}

void SpectrogramPanelEnhanced::wheelEvent(QWheelEvent *event)
{
    if (!m_audioData.hasSpectrogram()) {
        QOpenGLWidget::wheelEvent(event);
        return;
    }

    int delta = event->angleDelta().y();

    if (event->modifiers() & Qt::ControlModifier) {
        // Ctrl+wheel: frequency zoom (Y-axis)
        double factor = (delta > 0) ? 0.8 : 1.25;
        double mouseFreq = pixelToFreq(event->position().y());
        if (mouseFreq <= 0) mouseFreq = m_viewYMin;

        double newMin, newMax;
        if (m_freqScale == FreqScale::Logarithmic) {
            newMin = mouseFreq * pow(m_viewYMin / mouseFreq, factor);
            newMax = mouseFreq * pow(m_viewYMax / mouseFreq, factor);
        } else {
            newMin = mouseFreq + (m_viewYMin - mouseFreq) * factor;
            newMax = mouseFreq + (m_viewYMax - mouseFreq) * factor;
        }

        double nyquist = m_audioData.sampleRate / 2.0;
        double yMaxLimit = (m_freqScale == FreqScale::Logarithmic) ? nyquist : 12000.0;
        if (m_freqScale == FreqScale::Logarithmic) {
            newMin = qBound(20.0, newMin, nyquist / 2);
            newMax = qBound(20.0, newMax, yMaxLimit);
        } else {
            newMin = qBound(0.0, newMin, nyquist / 2);
            newMax = qBound(0.0, newMax, yMaxLimit);
        }

        if (newMax > newMin * 1.1) {
            m_viewYMin = newMin;
            m_viewYMax = newMax;
            update();
        }
        event->accept();
        return;
    }

    // Plain wheel: time axis zoom (X-axis), centered on mouse position
    if (m_viewXMax > m_viewXMin && m_audioData.hasSpectrogram()) {
        double mouseX = event->position().x();
        int plotLeft = m_leftMargin;
        int plotRight = width() - m_rightMargin;
        if (plotRight > plotLeft) {
            double t = (mouseX - plotLeft) / double(plotRight - plotLeft);
            t = qBound(0.0, t, 1.0);
            double mouseTime = m_viewXMin + t * (m_viewXMax - m_viewXMin);
            double factor = (delta > 0) ? 0.8 : 1.25;
            double oldRange = m_viewXMax - m_viewXMin;
            double specDur = double(m_audioData.durationMs());
            double newRange = qBound(100.0, oldRange * factor, specDur);
            double newMin = mouseTime - (mouseTime - m_viewXMin) * (newRange / oldRange);
            double newMax = newMin + newRange;
            if (newMin < 0) { newMin = 0; newMax = newRange; }
            if (newMax > specDur) { newMax = specDur; newMin = specDur - newRange; }
            if (newMin < 0) newMin = 0;
            m_viewXMin = newMin;
            m_viewXMax = newMax;
            emit xAxisRangeChanged(m_viewXMin, m_viewXMax);
            update();
        }
    }
    event->accept();
}

void SpectrogramPanelEnhanced::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && (event->modifiers() & Qt::ControlModifier)) {
        m_draggingY = true;
        m_lastDragY = event->position().y();
        event->accept();
        return;
    }

    // Cursor drag / click-to-seek (matching ChartPanel behavior)
    if (event->button() == Qt::LeftButton && !(event->modifiers() & Qt::ControlModifier)) {
        int heatW = width() - m_leftMargin - m_rightMargin;
        int mouseX = event->position().x() - m_leftMargin;
        int mouseY = event->position().y() - TOP_MARGIN;
        int heatH = height() - TOP_MARGIN - BOTTOM_MARGIN;

        if (mouseX >= 0 && mouseX < heatW && mouseY >= 0 && mouseY < heatH
            && m_viewXMax > m_viewXMin) {
            // Check proximity to cursor line (within 10px)
            if (m_cursorTimeMs >= 0) {
                qreal cursorRatio = (m_cursorTimeMs - m_viewXMin) / (m_viewXMax - m_viewXMin);
                int cursorX = m_leftMargin + static_cast<int>(cursorRatio * heatW);
                if (qAbs(event->position().x() - cursorX) < 10) {
                    // Near cursor: start drag only (don't seek yet)
                    m_draggingCursor = true;
                    setCursor(Qt::SizeHorCursor);
                    event->accept();
                    return;
                }
            }
            // Not near cursor: seek to click position, don't start drag
            qreal xRatio = static_cast<qreal>(mouseX) / heatW;
            qint64 clickTime = static_cast<qint64>(m_viewXMin + xRatio * (m_viewXMax - m_viewXMin));
            m_cursorTimeMs = clickTime;
            emit seekRequested(clickTime);
            update();
            event->accept();
            return;
        }
    }

    QOpenGLWidget::mousePressEvent(event);
}

void SpectrogramPanelEnhanced::mouseMoveEvent(QMouseEvent *event)
{
    // Cursor dragging
    if (m_draggingCursor) {
        int heatW = width() - m_leftMargin - m_rightMargin;
        int mouseX = event->position().x() - m_leftMargin;
        if (heatW > 0 && m_viewXMax > m_viewXMin) {
            qreal xRatio = qBound(0.0, static_cast<qreal>(mouseX) / heatW, 1.0);
            qint64 timeMs = static_cast<qint64>(m_viewXMin + xRatio * (m_viewXMax - m_viewXMin));
            m_cursorTimeMs = timeMs;
            emit seekRequested(timeMs);
            update();
        }
        event->accept();
        return;
    }

    if (m_draggingY) {
        int dy = event->position().y() - m_lastDragY;
        int heatH = height() - TOP_MARGIN - BOTTOM_MARGIN;
        if (heatH > 0) {
            double nyquist = m_audioData.sampleRate / 2.0;
            double yMaxLimit = (m_freqScale == FreqScale::Logarithmic) ? nyquist : 12000.0;

            if (m_freqScale == FreqScale::Logarithmic) {
                // Log-scale pan: multiplicative shift
                double logRange = log(m_viewYMax) - log(m_viewYMin);
                double shiftRatio = exp(-(static_cast<double>(dy) / heatH) * logRange);
                m_viewYMin *= shiftRatio;
                m_viewYMax *= shiftRatio;
                if (m_viewYMin < 20.0) {
                    m_viewYMax *= (20.0 / m_viewYMin);
                    m_viewYMin = 20.0;
                }
                if (m_viewYMax > yMaxLimit) {
                    m_viewYMin *= (yMaxLimit / m_viewYMax);
                    m_viewYMax = yMaxLimit;
                }
            } else {
                // Linear pan: additive shift
                double freqRange = m_viewYMax - m_viewYMin;
                double shift = (static_cast<double>(dy) / heatH) * freqRange;
                m_viewYMin += shift;
                m_viewYMax += shift;
                if (m_viewYMin < 0.0) {
                    m_viewYMax -= m_viewYMin;
                    m_viewYMin = 0.0;
                }
                if (m_viewYMax > yMaxLimit) {
                    m_viewYMin -= (m_viewYMax - yMaxLimit);
                    m_viewYMax = yMaxLimit;
                }
            }

            m_lastDragY = event->position().y();
            update();
        }
        event->accept();
        return;
    }

    // Tooltip
    if (m_audioData.hasSpectrogram()) {
        int mouseX = event->position().x() - m_leftMargin;
        int mouseY = event->position().y() - TOP_MARGIN;
        int heatW = width() - m_leftMargin - m_rightMargin;
        int heatH = height() - TOP_MARGIN - BOTTOM_MARGIN;

        if (mouseX >= 0 && mouseX < heatW && mouseY >= 0 && mouseY < heatH) {
            double xRatio = static_cast<double>(mouseX) / heatW;
            qint64 timeMs = static_cast<qint64>(m_viewXMin + xRatio * (m_viewXMax - m_viewXMin));
            double freq = pixelToFreq(event->position().y());

            int nFrames = m_audioData.spectrogram[0].size();
            qreal res = m_audioData.safeTimeResolutionMs();
            int aIdx = static_cast<int>(timeMs / res);
            aIdx = qBound(0, aIdx, nFrames - 1);

            int fIdx = freqToBin(freq);
            qreal value = m_audioData.spectrogram[fIdx][aIdx];

            int totalSec = static_cast<int>(timeMs / 1000);
            int h = totalSec / 3600;
            int m = (totalSec % 3600) / 60;
            int s = totalSec % 60;
            int ms = static_cast<int>(timeMs % 1000);
            QString timeText = QString("%1:%2:%3.%4")
                .arg(h).arg(m, 2, 10, QChar('0'))
                .arg(s, 2, 10, QChar('0')).arg(ms, 3, 10, QChar('0'));

            QString freqText = (freq >= 1000)
                ? QString("%1 kHz").arg(freq / 1000.0, 0, 'f', 2)
                : QString("%1 Hz").arg(freq, 0, 'f', 1);

            setToolTip(QString(lang("时间: %1\n频率: %2\n值: %3 dB", "Time: %1\nFreq: %2\nValue: %3 dB"))
                       .arg(timeText).arg(freqText).arg(value, 0, 'f', 2));
        }
    }

    // Hover detection: change cursor when near cursor line (matching ChartPanel)
    if (!m_draggingCursor && !m_draggingY && m_audioData.hasSpectrogram()) {
        int heatW = width() - m_leftMargin - m_rightMargin;
        int mouseX = event->position().x() - m_leftMargin;
        int mouseY = event->position().y() - TOP_MARGIN;
        int heatH = height() - TOP_MARGIN - BOTTOM_MARGIN;
        bool nearCursor = false;
        if (mouseX >= 0 && mouseX < heatW && mouseY >= 0 && mouseY < heatH
            && m_cursorTimeMs >= 0 && m_viewXMax > m_viewXMin) {
            qreal cursorRatio = (m_cursorTimeMs - m_viewXMin) / (m_viewXMax - m_viewXMin);
            int cursorX = m_leftMargin + static_cast<int>(cursorRatio * heatW);
            if (qAbs(event->position().x() - cursorX) < 10)
                nearCursor = true;
        }
        if (nearCursor)
            setCursor(Qt::SizeHorCursor);
        else
            unsetCursor();
    }

    QOpenGLWidget::mouseMoveEvent(event);
}

void SpectrogramPanelEnhanced::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_draggingY) {
        m_draggingY = false;
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton && m_draggingCursor) {
        m_draggingCursor = false;
        unsetCursor();
        emit scrubEnded();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseReleaseEvent(event);
}

void SpectrogramPanelEnhanced::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->position().x() < m_leftMargin) {
        double nyquist = m_audioData.sampleRate / 2.0;
        if (m_freqScale == FreqScale::Logarithmic) {
            m_viewYMin = 20.0;
            m_viewYMax = nyquist;
        } else {
            m_viewYMin = 0;
            m_viewYMax = qMin(12000.0, nyquist);
        }
        update();
        event->accept();
        return;
    }
    QOpenGLWidget::mouseDoubleClickEvent(event);
}
