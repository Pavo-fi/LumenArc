/**
 * @file calibphotodialog.h
 * @brief 北京时间对时的校时图片两框框选对话框（v1.12.5，2026-08-21 拍板）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-21
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 取证惯例：对监控屏幕拍照时同框拍入标准时间参照物（手机授时网页等）。
 * 本对话框引导用户在图片上依次框两个框：
 *   框 1 = 监控主机时间（屏幕 OSD 的日期时间，须含秒）
 *   框 2 = 北京时间（参照物上的日期时间，须含秒）
 * 确认后由 CalibrationService::runCalibPhoto 做 OCR 识别。
 */
#ifndef CALIBPHOTODIALOG_H
#define CALIBPHOTODIALOG_H

#include <QDialog>
#include <QPixmap>
#include <QRect>

class QLabel;
class QPushButton;

/// 可缩放图片视图（v1.12.5 对时确认卡）：滚轮以光标为锚缩放、左键拖动平移、
/// 双击复位适应窗口；叠加绘制两个框（原图像素坐标，随缩放平移）
class ZoomPhotoView : public QWidget
{
public:
    ZoomPhotoView(const QPixmap &pix, const QRect &box1, const QRect &box2,
                  QWidget *parent = nullptr);
    QSize sizeHint() const override { return QSize(720, 540); }

protected:
    void paintEvent(QPaintEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;

private:
    void resetFit();
    qreal fitScale() const;

    QPixmap m_pix;
    QRect m_box1, m_box2;      // 原图像素
    qreal m_scale = 1.0;
    QPointF m_offset;          // 图片左上角在控件内的位置（px）
    bool m_userZoomed = false; // 用户碰过缩放/平移后不再自动 resetFit
    QPoint m_dragLast;
    bool m_dragging = false;
};

/// 对时偏差确认卡（v1.12.5 拍板：校时图片集成上卡 + 可放大校对）：
/// 左侧可缩放图片（两框叠加），右侧识别结果（两个时间+原文+偏差表述）
/// + 人工核对提示 + 使用/取消。exec()==Accepted = 用户确认采用。
class TruthPhotoConfirmDialog : public QDialog
{
    Q_OBJECT
public:
    TruthPhotoConfirmDialog(const QString &imagePath,
                            const QRect &monitorBox, const QRect &beijingBox,
                            const QString &monitorTimeText,
                            const QString &monitorRawText,
                            const QString &beijingTimeText,
                            const QString &beijingRawText,
                            const QString &offsetVerboseText,
                            const QString &crossDayNote,
                            QWidget *parent = nullptr);
};

class CalibPhotoDialog : public QDialog
{
    Q_OBJECT
public:
    /// imagePath 加载失败 → isValidImage()=false（调用方给错误提示）
    explicit CalibPhotoDialog(const QString &imagePath, QWidget *parent = nullptr);

    bool isValidImage() const { return !m_pix.isNull(); }
    /// 两个框（原图像素坐标）；两框均有效 = 可确认识别
    QRect monitorBox() const { return m_box1; }
    QRect beijingBox() const { return m_box2; }
    QString imagePath() const { return m_imagePath; }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void onMousePress(const QPoint &pos);
    void onMouseMove(const QPoint &pos);
    void onMouseRelease(const QPoint &pos);
    void refreshHint();

    /// 视图坐标 ↔ 原图像素（等比缩放 + 居中留白）
    QRect toImageRect(const QRect &viewRect) const;
    QRect toViewRect(const QRect &imgRect) const;
    QRect contentRect() const;   // 图片在视图标签内的绘制区

    QString m_imagePath;
    QPixmap m_pix;
    QLabel *m_view = nullptr;        // 图片绘制区（自绘覆盖层）
    QLabel *m_hint = nullptr;        // 步骤指引
    QPushButton *m_okBtn = nullptr;
    QPushButton *m_resetBtn = nullptr;

    int m_step = 1;                  // 1=框监控主机时间 2=框北京时间
    QRect m_box1, m_box2;            // 原图像素坐标
    QPoint m_dragStart;
    QRect m_dragging;                // 视图坐标（拖动中）
    bool m_dragActive = false;
};

#endif // CALIBPHOTODIALOG_H
