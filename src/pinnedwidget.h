/**
 * @file pinnedwidget.h
 * @brief 固定时间戳浮动窗口，实时显示视频指定区域画面
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include <QWidget>
#include <QImage>
#include <QRect>

/**
 * @brief Floating always-on-top widget showing a pinned video region (e.g. timestamp).
 */
class PinnedWidget : public QWidget
{
    Q_OBJECT

public:
    explicit PinnedWidget(QWidget *parent = nullptr);

    /// 源视频原生分辨率（视频坐标换算基准）。由调用方在视频加载/切换时同步；
    /// 不设置时退化：不换算（按传入矩形原样裁剪）。
    void setVideoSize(int width, int height);
    /// 显示旋转（Q1 方案 A）：钉图内容随主画面一起转；
    /// videoRect 仍为【原视频系】，旋转在裁剪后显示前应用。
    void setDisplayRotation(int degrees) { m_displayRotation = degrees; }
    int displayRotation() const { return m_displayRotation; }
    /// 画面调节 LUT（空表 = 恒等；旋转之后应用，与主画面同一张表）
    void setDisplayLut(const QByteArray &lut) { m_displayLut = lut; }
    void setPinnedImage(const QImage &fullFrame, const QRect &videoRect);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_pinnedImage;
    QSize m_videoSize;   // 源视频原生分辨率（空 = 未知）
    int m_displayRotation = 0;
    QByteArray m_displayLut;
};
