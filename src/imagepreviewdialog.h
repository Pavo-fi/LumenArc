/**
 * @file imagepreviewdialog.h
 * @brief 通用图片预览对话框（v1.16.0）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-26
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 用户拍板（2026-08-26）：快照/校时照片/证据帧等图片要能在软件内直接预览，
 * 组件做通用。交互与校时照片对话框同款：滚轮缩放（光标锚点）/拖动平移/
 * 双击复位；另提供「适应窗口」「1:1」「在资源管理器中显示」。
 * 非模态 + WA_DeleteOnClose（导出弹窗卡死教训：不持有嵌套事件循环）。
 */
#pragma once

#include <QDialog>
#include <QImage>
#include <QPoint>
#include <QPointF>

class QLabel;

class ImagePreviewDialog : public QDialog
{
    Q_OBJECT
public:
    explicit ImagePreviewDialog(const QString &path, QWidget *parent = nullptr);

    /// 通用入口：新建并非模态显示（调用方无需管理生命周期）
    static void preview(const QString &path, QWidget *parent = nullptr);

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;

private:
    double fitScale() const;          ///< 适应窗口所需缩放
    void   fitToWindow();             ///< 复位 = 适应窗口居中
    void   setScaleAt(double newScale, const QPointF &anchorViewport);
    void   updateStatus();

    QString  m_path;
    QImage   m_img;
    double   m_scale = 1.0;           ///< 视口像素 / 图像像素
    QPointF  m_offset;                ///< 图像左上角在视口中的位置
    QPoint   m_dragStart;
    QPointF  m_offsetStart;
    bool     m_dragging = false;
    QLabel  *m_status = nullptr;
};
