/**
 * @file playbackadjustpanel.h
 * @brief 播放画面调节面板（亮度/对比度/伽马/色阶/反色 + 旋转档位）
 * @date 2026-08-14
 *
 * 设计要点：
 * - 仅作用于显示链路（VideoWidget 显示前 LUT 变换；放大镜/钉图同一张表），
 *   分析/ROI/语谱/证据数据全部仍走原始帧，证据链不变；
 * - 与截图叠加共用同一视觉公式（applyBrightnessContrast 的 cf/b*2），
 *   复合反色/色阶/伽马后按 256 级 LUT 预计算（displayadjust.h），
 *   4K 帧查表 ~5-10ms，远优于逐像素浮点；
 * - 参数为默认值时零开销（直接跳过变换）；
 * - 旋转 90° 步进：覆盖物（ROI/多边形/辅助线/框选/放大镜/截图叠加）
 *   随画面一起转，存储与分析始终为原视频坐标（双向映射）；
 * - 逐视频记忆由 MainWindow/VideoStateManager 负责（本组件不持状态）。
 */
#pragma once

#include <QDockWidget>
#include "displayadjust.h"

class QSlider;
class QLabel;
class QPushButton;
class QCheckBox;

class PlaybackAdjustPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit PlaybackAdjustPanel(QWidget *parent = nullptr);

    int brightness() const { return m_adjust.brightness; }
    int contrast() const { return m_adjust.contrast; }
    int rotation() const { return m_rotation; }
    DisplayAdjust adjust() const { return m_adjust; }

    /// 逐视频状态恢复（不触发信号，由 MainWindow 恢复后自行应用到引擎显示）
    void setValues(const DisplayAdjust &adj, int rotation = 0);

signals:
    /// 任意调节参数变化（亮度/对比度/伽马/黑点/白点/反色）
    void adjustChanged(const DisplayAdjust &adj);
    /// 旋转档位变化（Q1 方案 A：0/90/180/270 顺时针，90° 步进循环）
    void rotationChanged(int degrees);

private:
    QWidget *makeRow(const QString &label, QSlider *&slider, QLabel *&valLabel,
                     int minV, int maxV);
    void refreshLabels();
    void emitAdjust();

    DisplayAdjust m_adjust;
    int m_rotation = 0;   ///< 顺时针档位 0/90/180/270
    QSlider *m_brightnessSlider = nullptr;
    QSlider *m_contrastSlider = nullptr;
    QSlider *m_gammaSlider = nullptr;
    QSlider *m_blackPointSlider = nullptr;
    QSlider *m_whitePointSlider = nullptr;
    QCheckBox *m_invertBox = nullptr;
    QLabel *m_bValLabel = nullptr;
    QLabel *m_cValLabel = nullptr;
    QLabel *m_gValLabel = nullptr;
    QLabel *m_bpValLabel = nullptr;
    QLabel *m_wpValLabel = nullptr;
    QLabel *m_rotValLabel = nullptr;
    QPushButton *m_rotateBtn = nullptr;
    QPushButton *m_resetBtn = nullptr;
};
