/**
 * @file playbackadjustpanel.h
 * @brief 播放画面调节面板（亮度/对比度，旋转档位待 Q1 拍板后启用）
 * @date 2026-08-14
 *
 * 设计要点：
 * - 仅作用于显示链路（VideoWidget::onFrameReady 显示前 LUT 变换），
 *   分析/ROI/语谱/证据数据全部仍走原始帧，证据链不变；
 * - 与截图叠加共用同一视觉公式（applyBrightnessContrast 的 cf/b*2），
 *   但按 256 级 LUT 预计算，4K 帧查表 ~5-10ms，远优于逐像素浮点；
 * - 参数为默认值时零开销（直接跳过变换）；
 * - 逐视频记忆由 MainWindow/VideoStateManager 负责（本组件不持状态）。
 */
#pragma once

#include <QDockWidget>

class QSlider;
class QLabel;
class QPushButton;

class PlaybackAdjustPanel : public QDockWidget
{
    Q_OBJECT

public:
    explicit PlaybackAdjustPanel(QWidget *parent = nullptr);

    int brightness() const { return m_brightness; }
    int contrast() const { return m_contrast; }

    /// 逐视频状态恢复（不触发信号，由 MainWindow 恢复后自行应用到引擎显示）
    void setValues(int brightness, int contrast);

signals:
    void adjustChanged(int brightness, int contrast);

private:
    QWidget *makeRow(const QString &label, QSlider *&slider, QLabel *&valLabel,
                     int minV, int maxV);

    int m_brightness = 0;
    int m_contrast = 0;
    QSlider *m_brightnessSlider = nullptr;
    QSlider *m_contrastSlider = nullptr;
    QLabel *m_bValLabel = nullptr;
    QLabel *m_cValLabel = nullptr;
    QPushButton *m_resetBtn = nullptr;
};
