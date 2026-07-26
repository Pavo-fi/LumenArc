/**
 * @file snapshotoverlay.h
 * @brief 截图叠加浮窗：缩略图/编辑器/亮度对比度透明度调节
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
#include <QSlider>
#include <QPushButton>
#include <QLabel>

/** @brief 截图叠加浮窗：缩略图预览/编辑器/亮度对比度透明度滑块/放置切换 */
class SnapshotOverlay : public QWidget
{
    Q_OBJECT

public:
    explicit SnapshotOverlay(QWidget *parent = nullptr);

    /// @brief 设置截图图像并刷新预览
    void setSnapshot(const QImage &img);
    /// @brief 清除截图及参数
    void clearSnapshot();
    /// @brief 批量设置亮度、对比度、透明度参数
    void setParameters(int brightness, int contrast, int opacity);
    bool hasSnapshot() const { return !m_snapshot.isNull(); }

    const QImage &snapshotImage() const { return m_snapshot; }
    int brightness() const { return m_brightnessSlider->value(); }
    int contrastValue() const { return m_contrastSlider->value(); }
    int opacityValue() const { return m_opacitySlider->value(); }

    /// @brief 返回叠加是否处于激活（放置）状态
    bool isOverlayActive() const { return m_overlayActive; }

signals:
    void snapshotChanged();
    void captureRequested();
    void clearRequested();
    void placeToggled(bool active);

protected:
    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void enterEvent(QEnterEvent *event) override;
    void leaveEvent(QEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    QImage applyAdjustments(const QImage &src) const;
    void layoutControls();

    QImage m_snapshot;
    QImage m_adjustedCache;   // cached adjusted image for preview
    bool m_dirty = true;      // flag to regenerate cache

    bool m_expanded = false;  // thumbnail vs editor
    bool m_overlayActive = false;
    QPoint m_dragOffset;      // for dragging

    // Thumbnail size
    static constexpr int THUMB_W = 120;
    static constexpr int THUMB_H = 80;

    // Editor size
    static constexpr int EDITOR_W = 300;
    static constexpr int EDITOR_H = 280;

    QSlider *m_brightnessSlider = nullptr;
    QSlider *m_contrastSlider = nullptr;
    QSlider *m_opacitySlider = nullptr;
    QLabel *m_bValLabel = nullptr;
    QLabel *m_cValLabel = nullptr;
    QLabel *m_oValLabel = nullptr;
    QPushButton *m_placeBtn = nullptr;
    QPushButton *m_clearBtn = nullptr;
    QPushButton *m_closeBtn = nullptr;
};
