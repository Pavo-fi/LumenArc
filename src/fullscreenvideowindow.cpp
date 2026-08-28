/**
 * @file fullscreenvideowindow.cpp
 * @brief 副屏全屏展示窗实现：黑底 letterbox 等比缩放 + 调节 LUT + 光标自动隐藏
 */
#include "fullscreenvideowindow.h"

#include <QPainter>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QTimer>

FullscreenVideoWindow::FullscreenVideoWindow(QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setAttribute(Qt::WA_OpaquePaintEvent);
    setMouseTracking(true);   // 光标隐藏计时需要 move 事件

    m_cursorTimer = new QTimer(this);
    m_cursorTimer->setSingleShot(true);
    m_cursorTimer->setInterval(3000);
    connect(m_cursorTimer, &QTimer::timeout, this, [this]() {
        setCursor(Qt::BlankCursor);
    });
}

void FullscreenVideoWindow::showOnScreen(QScreen *screen)
{
    if (screen) {
        setScreen(screen);
        move(screen->geometry().topLeft());
    }
    showFullScreen();
    m_cursorTimer->start();
}

void FullscreenVideoWindow::setFrame(const QImage &img)
{
    m_raw = img;               // QImage 隐式共享：浅拷贝
    rebuildShown();
    update();                  // Qt 自动合并 update → 天然丢帧背压
}

void FullscreenVideoWindow::setDisplayAdjust(const DisplayAdjust &adj)
{
    m_lut = adj.buildLut();    // 与主视口同一张 LUT（共享调节值）
    rebuildShown();            // 暂停态拖滑杆同样实时预览
    update();
}

void FullscreenVideoWindow::rebuildShown()
{
    if (m_raw.isNull()) {
        m_shown = QImage();
        return;
    }
    m_shown = applyDisplayLut(m_raw, m_lut);   // 空表 = 恒等浅拷贝
}

void FullscreenVideoWindow::paintEvent(QPaintEvent *)
{
    QElapsedTimer t;
    t.start();
    QPainter p(this);
    p.fillRect(rect(), Qt::black);
    if (!m_shown.isNull()) {
        const QSize scaled = m_shown.size();
        QSize target = scaled;
        target.scale(size(), Qt::KeepAspectRatio);
        const QRect dst(QPoint((width() - target.width()) / 2,
                               (height() - target.height()) / 2),
                        target);
        // 自适应降质：4K 以上目标且上一帧绘制超 30ms → 快速缩放保帧率
        const bool heavy = qint64(target.width()) * target.height() > 8000000
                           && m_lastPaintMs > 30;
        p.setRenderHint(QPainter::SmoothPixmapTransform, !heavy);
        p.drawImage(dst, m_shown);
    }
    m_lastPaintMs = t.elapsed();
}

void FullscreenVideoWindow::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Escape) {
        close();
        return;
    }
    QWidget::keyPressEvent(event);
}

void FullscreenVideoWindow::mouseDoubleClickEvent(QMouseEvent *)
{
    close();
}

void FullscreenVideoWindow::mouseMoveEvent(QMouseEvent *event)
{
    unsetCursor();
    m_cursorTimer->start();
    QWidget::mouseMoveEvent(event);
}
