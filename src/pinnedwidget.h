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

    void setPinnedImage(const QImage &fullFrame, const QRect &videoRect);
    void clear();

protected:
    void paintEvent(QPaintEvent *event) override;

private:
    QImage m_pinnedImage;
};
