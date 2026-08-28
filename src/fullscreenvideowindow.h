#pragma once
/**
 * @file fullscreenvideowindow.h
 * @brief 副屏全屏展示窗（多屏主机：把视频原画面送到指定屏幕全屏）
 *
 * 拍板规格（2026-08-28）：
 * - 屏幕由「视图→副屏全屏显示」菜单手动选择（记忆上次选择）
 * - 纯画面：不带 ROI/放大镜/水印等任何叠加
 * - 画面调节与主视口共享同一组值（单真源，调一处两边同变）
 * - 本期仅主窗单路视频；多机窗口不支持
 * - ESC/双击退出；鼠标静止 3 秒自动隐藏光标
 * 纯显示链路：帧与主视口同源（QImage 隐式共享），源数据零影响。
 */
#include <QWidget>
#include <QImage>
#include <QByteArray>
#include <QElapsedTimer>
#include "displayadjust.h"

class QTimer;
class QScreen;

class FullscreenVideoWindow : public QWidget
{
    Q_OBJECT
public:
    explicit FullscreenVideoWindow(QWidget *parent = nullptr);

    void showOnScreen(QScreen *screen);   ///< 在指定屏全屏显示

public slots:
    void setFrame(const QImage &img);             ///< 引擎原帧（未调节）
    void setDisplayAdjust(const DisplayAdjust &adj);  ///< 共享调节值（立即重算）

protected:
    void paintEvent(QPaintEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;        ///< ESC 关闭
    void mouseDoubleClickEvent(QMouseEvent *) override;   ///< 双击关闭
    void mouseMoveEvent(QMouseEvent *) override;          ///< 唤醒光标

private:
    void rebuildShown();   ///< 原帧 + LUT → 显示帧

    QImage m_raw;          ///< 最近原帧
    QImage m_shown;        ///< 调节后显示帧
    QByteArray m_lut;      ///< 空 = 恒等直通
    QTimer *m_cursorTimer = nullptr;
    qint64 m_lastPaintMs = 0;   ///< 自适应降质依据（4K 平滑缩放慢则降 Fast）
};
