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
