/**
 * @file spectrogrampanel_enhanced.h
 * @brief 语谱图增强版：QOpenGLWidget + GPU 渲染 + 对数频率轴 + 频率缩放
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.4
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#pragma once

#include <QOpenGLWidget>
#include <QOpenGLFunctions_3_3_Core>
#include <QOpenGLShaderProgram>
#include <QOpenGLTexture>
#include <QOpenGLBuffer>
#include <QVector>
#include <QVector2D>
#include "domain/analysis_snapshot.h"

QT_FORWARD_DECLARE_CLASS(QOpenGLVertexArrayObject)

class SpectrogramPanelEnhanced : public QOpenGLWidget, protected QOpenGLFunctions_3_3_Core
{
    Q_OBJECT

public:
    enum class ColorScale { Thermal, Inferno, Viridis };
    enum class FreqScale { Linear, Logarithmic };

    explicit SpectrogramPanelEnhanced(QWidget *parent = nullptr);
    ~SpectrogramPanelEnhanced();

    void setSpectrogramData(const AudioData &audio);
    void clear();
    void setColorScale(ColorScale scale);
    void setFreqScale(FreqScale scale);
    FreqScale freqScale() const { return m_freqScale; }
    void setNoiseFloor(qreal value);
    bool isDraggingCursor() const { return m_draggingCursor; }

    /// 离屏 CPU 光栅化渲染（2026-08-14 §14 快照全面化）：与 GPU 着色器同一
    /// 归一化 + 同一颜色 LUT + 同一视窗/频率轴设置，任意目标尺寸全幅输出，
    /// 不经 GL（QOpenGLWidget 的 grab 只能拿屏幕当前尺寸且尺寸错位）。
    /// 含橙色时间光标（与 paintGL 同款）。无语谱数据返回空图。
    QImage renderHeatmapImage(const QSize &targetSize);

public slots:
    void onXAxisRangeChanged(qreal xMin, qreal xMax);
    void setCursorTime(qint64 timeMs);
    void onChartPlotAreaChanged(QRectF plotArea);

signals:
    void colorScaleChanged(ColorScale scale);
    void freqScaleChanged(FreqScale scale);
    void seekRequested(qint64 timeMs);
    /// 光标拖拽松手（退出 scrub 模式 + 最终精确 seek，与 ChartPanel 同语义）
    void scrubEnded();
    void xAxisRangeChanged(qreal xMin, qreal xMax);

protected:
    void initializeGL() override;
    void resizeGL(int w, int h) override;
    void paintGL() override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    void initShaders();
    void uploadSpectrogramTexture();
    void uploadColorLUTTexture();
    void buildColorLUT();
    void drawAxes(QPainter &painter);
    void drawError(QPainter &painter, const QString &msg);

    double pixelToFreq(int y) const;
    int freqToPixel(double freq) const;
    int freqToBin(double freq) const;

    // Data
    AudioData m_audioData;
    QVector<QRgb> m_colorLUT;
    ColorScale m_colorScale = ColorScale::Thermal;
    FreqScale m_freqScale = FreqScale::Linear;

    // Viewport
    qreal m_viewXMin = 0;
    qreal m_viewXMax = 0;
    double m_viewYMin = 20.0;
    double m_viewYMax = 12000.0;
    qreal m_minValue = -5.5;
    qreal m_maxValue = 5.0;
    bool m_yRangeInitialized = false;

    // Cursor
    qint64 m_cursorTimeMs = -1;

    // Interaction state
    bool m_draggingY = false;
    int m_lastDragY = 0;
    bool m_draggingCursor = false;

    // OpenGL resources
    QOpenGLShaderProgram *m_program = nullptr;
    QOpenGLTexture *m_spectrogramTexture = nullptr;
    QOpenGLTexture *m_colorLUTTexture = nullptr;
    QOpenGLBuffer m_vertexBuffer;
    QOpenGLVertexArrayObject *m_vao = nullptr;

    // Cached texture data
    QVector<float> m_textureData;
    int m_textureWidth = 0;
    int m_textureHeight = 0;
    bool m_textureDirty = true;
    bool m_shaderOk = false;
    QString m_glError;

    // GL limits
    GLint m_maxTextureSize = 8192;

    // Margins for axes (dynamic, synced with chart plotArea)
    int m_leftMargin = 60;
    int m_rightMargin = 0;
    QRectF m_storedChartPlotArea;
    static constexpr int BOTTOM_MARGIN = 25;
    static constexpr int TOP_MARGIN = 5;
};
