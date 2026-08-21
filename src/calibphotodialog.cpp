/**
 * @file calibphotodialog.cpp
 * @brief CalibPhotoDialog 实现（两框橡皮筋框选，原图像素坐标回传）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-21
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "calibphotodialog.h"

#include "i18n.h"

#include <QDialogButtonBox>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
/// 视图最大尺寸（图片等比缩放进此框）
constexpr int kViewW = 1080;
constexpr int kViewH = 720;
/// 框线颜色：框 1 监控主机时间（橙红）/ 框 2 北京时间（青蓝）
const QColor kBox1Color(230, 85, 60);
const QColor kBox2Color(0, 130, 200);
} // namespace

CalibPhotoDialog::CalibPhotoDialog(const QString &imagePath, QWidget *parent)
    : QDialog(parent)
    , m_imagePath(imagePath)
{
    m_pix.load(imagePath);
    setWindowTitle(lang("校时图片框选（北京时间对时）",
                        "Calibration Photo — box the two clocks"));
    auto *lay = new QVBoxLayout(this);

    m_hint = new QLabel(this);
    m_hint->setWordWrap(true);
    m_hint->setStyleSheet(QStringLiteral("font-weight:bold; padding:4px;"));
    lay->addWidget(m_hint);

    if (!m_pix.isNull()) {
        m_view = new QLabel(this);
        m_view->setMinimumSize(480, 320);
        m_view->setStyleSheet(QStringLiteral("background:#20242a;"));
        m_view->setAlignment(Qt::AlignCenter);
        const QSize fit = m_pix.size().scaled(kViewW, kViewH,
                                              Qt::KeepAspectRatio);
        m_view->setFixedSize(fit);
        m_view->installEventFilter(this);
        m_view->setMouseTracking(true);
        lay->addWidget(m_view, 0, Qt::AlignCenter);
    }

    auto *btnRow = new QHBoxLayout();
    m_resetBtn = new QPushButton(lang("重框", "Reset boxes"), this);
    m_okBtn = new QPushButton(lang("确认识别", "Recognize"), this);
    m_okBtn->setDefault(true);
    auto *cancelBtn = new QPushButton(lang("取消", "Cancel"), this);
    btnRow->addStretch(1);
    btnRow->addWidget(m_resetBtn);
    btnRow->addWidget(m_okBtn);
    btnRow->addWidget(cancelBtn);
    lay->addLayout(btnRow);

    connect(m_resetBtn, &QPushButton::clicked, this, [this]() {
        m_box1 = QRect();
        m_box2 = QRect();
        m_step = 1;
        m_dragging = QRect();
        refreshHint();
        if (m_view)
            m_view->update();
    });
    connect(m_okBtn, &QPushButton::clicked, this, [this]() {
        if (m_box1.isValid() && m_box2.isValid())
            accept();
    });
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    refreshHint();
}

QRect CalibPhotoDialog::contentRect() const
{
    if (!m_view || m_pix.isNull())
        return QRect();
    return QRect(0, 0, m_view->width(), m_view->height());
}

QRect CalibPhotoDialog::toImageRect(const QRect &viewRect) const
{
    if (m_pix.isNull() || m_view == nullptr)
        return QRect();
    const qreal sx = qreal(m_pix.width()) / contentRect().width();
    const qreal sy = qreal(m_pix.height()) / contentRect().height();
    const QRect r = viewRect.normalized();
    return QRect(qRound(r.left() * sx), qRound(r.top() * sy),
                 qRound(r.width() * sx), qRound(r.height() * sy))
        .normalized();
}

QRect CalibPhotoDialog::toViewRect(const QRect &imgRect) const
{
    if (m_pix.isNull() || m_view == nullptr)
        return QRect();
    const qreal sx = qreal(contentRect().width()) / m_pix.width();
    const qreal sy = qreal(contentRect().height()) / m_pix.height();
    const QRect r = imgRect.normalized();
    return QRect(qRound(r.left() * sx), qRound(r.top() * sy),
                 qRound(r.width() * sx), qRound(r.height() * sy));
}

void CalibPhotoDialog::refreshHint()
{
    if (m_step == 1) {
        m_hint->setText(lang(
            "第 1 步 / 共 2 步：框选【监控主机时间】——监控屏幕上的日期时间（须含秒），"
            "如「2026年07月22日 星期三 12:25:47」。在图上按住拖出方框。",
            "Step 1/2: box the RECORDER clock (on-screen date & time, seconds "
            "required), e.g. \"2026-07-22 12:25:47\". Drag on the image."));
        m_okBtn->setEnabled(false);
    } else {
        m_hint->setText(lang(
            "第 2 步 / 共 2 步：框选【北京时间】——手机/授时网页上的时间（须含秒），"
            "如「12:39:41」（框内同时含日期更准确）。",
            "Step 2/2: box the BEIJING time reference (phone/web clock, seconds "
            "required), e.g. \"12:39:41\" (include the date line if visible)."));
        m_okBtn->setEnabled(m_box2.isValid());
    }
}

bool CalibPhotoDialog::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_view) {
        if (event->type() == QEvent::MouseButtonPress) {
            onMousePress(static_cast<QMouseEvent *>(event)->pos());
            return true;
        }
        if (event->type() == QEvent::MouseMove) {
            onMouseMove(static_cast<QMouseEvent *>(event)->pos());
            return true;
        }
        if (event->type() == QEvent::MouseButtonRelease) {
            onMouseRelease(static_cast<QMouseEvent *>(event)->pos());
            return true;
        }
        if (event->type() == QEvent::Paint) {
            // 先让 QLabel 画底（背景），再画图片与框覆盖层
            QPainter p(m_view);
            p.drawPixmap(contentRect(), m_pix);
            const auto drawBox = [&p](const QRect &vr, const QColor &c,
                                      const QString &tag) {
                if (!vr.isValid())
                    return;
                p.setPen(QPen(c, 2, Qt::DashLine));
                p.drawRect(vr);
                p.fillRect(QRect(vr.topLeft(), QSize(86, 18)),
                           QColor(c.red(), c.green(), c.blue(), 180));
                p.setPen(Qt::white);
                p.drawText(QRect(vr.left() + 4, vr.top() + 1, 84, 16), tag);
            };
            drawBox(toViewRect(m_box1), kBox1Color, lang("监控主机", "DVR"));
            drawBox(toViewRect(m_box2), kBox2Color, lang("北京时间", "Beijing"));
            if (m_dragActive && m_dragging.isValid())
                drawBox(m_dragging,
                        m_step == 1 ? kBox1Color : kBox2Color,
                        m_step == 1 ? lang("监控主机", "DVR")
                                    : lang("北京时间", "Beijing"));
            return true;
        }
    }
    return QDialog::eventFilter(watched, event);
}

void CalibPhotoDialog::onMousePress(const QPoint &pos)
{
    if (m_pix.isNull())
        return;
    m_dragActive = true;
    m_dragStart = pos;
    m_dragging = QRect(pos, QSize(1, 1));
    m_view->update();
}

void CalibPhotoDialog::onMouseMove(const QPoint &pos)
{
    if (!m_dragActive)
        return;
    m_dragging = QRect(m_dragStart, pos).normalized() & contentRect();
    m_view->update();
}

void CalibPhotoDialog::onMouseRelease(const QPoint &pos)
{
    if (!m_dragActive)
        return;
    m_dragActive = false;
    m_dragging = QRect(m_dragStart, pos).normalized() & contentRect();
    // 小于 8px 视为误触，不收为框
    if (m_dragging.width() >= 8 && m_dragging.height() >= 8) {
        const QRect imgRect = toImageRect(m_dragging);
        if (m_step == 1) {
            m_box1 = imgRect;
            m_step = 2;
        } else {
            m_box2 = imgRect;
        }
    }
    m_dragging = QRect();
    refreshHint();
    m_view->update();
}
