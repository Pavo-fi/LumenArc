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
#include "domain/time_calibration.h"

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

    /// 校时下发（与 ChartPanel 同口径）：本轨道 X 轴刻度从「流内 00:00:00 起」
    /// 改为校时后的墙钟——两轨同宽并排，时间轴必须一致（用户：秒级对得上）。
    /// 传空模型（未校时）→ 与旧行为逐字一致。
    void setCalibration(const TimeCalibration &cal);
    const TimeCalibration &calibration() const { return m_calibration; }

    /// 流内 ms → 显示域 ms（与 ChartPanel::displayMsOf 同一口径，不另立一套）
    qint64 displayMsOf(qint64 streamMs) const;
    /// 显示域 ms → 刻度文本（与 ChartPanel::formatDisplayTime 同一口径）
    QString formatDisplayTime(qint64 displayMs) const;
    /// 测试钩子（自检）：按当前可视范围刷新跨天标志后，返回该流内位置
    /// 会画出的 X 刻度文本（不依赖 GL 绘制上下文）。
    QString axisTickTextForTest(qint64 streamMs);

    /// 离屏 CPU 光栅化渲染（2026-08-14 §14 快照全面化）：与 GPU 着色器同一
    /// 归一化 + 同一颜色 LUT + 同一视窗/频率轴设置，任意目标尺寸全幅输出，
    /// 不经 GL（QOpenGLWidget 的 grab 只能拿屏幕当前尺寸且尺寸错位）。
    /// 含橙色时间光标（与 paintGL 同款）。无语谱数据返回空图。
    /// @param leftInset  左侧轴条宽度（像素）。<0 = 按自身规则（W/40）自适应。
    /// @param rightInset 右侧留白（像素）。<0 = 0。
    /// v3.7：快照里要让语谱热力图与图表绘图区**严格同宽同起点**，
    ///       调用方把图表的绘图区左右内缩量传进来（用户：长度对不上逼死强迫症）。
    QImage renderHeatmapImage(const QSize &targetSize, int leftInset = -1,
                              int rightInset = -1);

    /// 热力图水平区间（纯函数，自检直验）：给定画布宽与左右内缩，
    /// 返回 [起始x, 结束x]（含端点）。保证与图表绘图区同口径。
    static QPair<int, int> heatSpan(int width, int leftInset, int rightInset);

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
    /// 按当前可视范围刷新「是否跨天」（dateKnown 路径决定刻度要不要带日期）
    void updateSpanCrossDay();

    // Data
    AudioData m_audioData;
    QVector<QRgb> m_colorLUT;
    ColorScale m_colorScale = ColorScale::Thermal;
    FreqScale m_freqScale = FreqScale::Linear;
    /// 校时模型（只读派生：X 轴刻度文本；SSOT 在 MainWindow/VideoState）
    TimeCalibration m_calibration;
    bool m_spanCrossDay = false;   ///< 可视范围跨天（drawAxes 前刷新，同 ChartPanel）

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
    // v1.16.1 滚轮/拖拽归一化：中键拖拽平移时间轴（与曲线图同语义）
    bool m_panningX = false;
    QPointF m_panStartPos;
    double m_panStartXMin = 0.0;
    double m_panStartXMax = 0.0;

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
    // v2.5m：顶端刻度标签画在 QRect(0, y-8, .., 16) 里，y = TOP_MARGIN。
    // TOP_MARGIN=5 时矩形上沿在 -3 → 顶部被裁掉 3px（用户：标签被遮挡）。
    // 取 13 = 标签半高 8 + 间隙 5：标签完整可见，且距面板顶端正好 5px。
    // v2.5o：用户要看效果 —— 在 13 的基础上再压缩 10px（= 23）
    static constexpr int TOP_MARGIN = 33;
};
