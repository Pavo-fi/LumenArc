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
#include <QWheelEvent>
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

// ---------------------------------------------------------------------------
// ZoomPhotoView（对时确认卡的可缩放图片视图）
// ---------------------------------------------------------------------------
ZoomPhotoView::ZoomPhotoView(const QPixmap &pix, const QRect &box1,
                             const QRect &box2, QWidget *parent)
    : QWidget(parent), m_pix(pix), m_box1(box1), m_box2(box2)
{
    setMinimumSize(400, 300);
    setMouseTracking(false);
    setCursor(Qt::OpenHandCursor);
}

qreal ZoomPhotoView::fitScale() const
{
    if (m_pix.isNull() || width() < 10 || height() < 10)
        return 1.0;
    return qMin(qreal(width()) / m_pix.width(),
                qreal(height()) / m_pix.height());
}

void ZoomPhotoView::resetFit()
{
    m_scale = fitScale();
    m_offset = QPointF((width() - m_pix.width() * m_scale) / 2.0,
                       (height() - m_pix.height() * m_scale) / 2.0);
    update();
}

void ZoomPhotoView::resizeEvent(QResizeEvent *event)
{
    QWidget::resizeEvent(event);
    if (!m_userZoomed)
        resetFit();
}

void ZoomPhotoView::wheelEvent(QWheelEvent *event)
{
    if (m_pix.isNull())
        return;
    const qreal oldScale = m_scale;
    const qreal factor = event->angleDelta().y() > 0 ? 1.25 : 0.8;
    m_scale = qBound(fitScale() * 0.5, m_scale * factor, fitScale() * 30.0);
    // 以光标为锚：光标下的图像点缩放前后不动
    const QPointF pos = event->position();
    const QPointF imgPt = (pos - m_offset) / oldScale;
    m_offset = pos - imgPt * m_scale;
    m_userZoomed = true;
    update();
    event->accept();
}

void ZoomPhotoView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_dragging = true;
        m_dragLast = event->pos();
        setCursor(Qt::ClosedHandCursor);
        event->accept();
    }
}

void ZoomPhotoView::mouseMoveEvent(QMouseEvent *event)
{
    if (m_dragging) {
        m_offset += event->pos() - m_dragLast;
        m_dragLast = event->pos();
        m_userZoomed = true;
        update();
        event->accept();
    }
}

void ZoomPhotoView::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        m_dragging = false;
        setCursor(Qt::OpenHandCursor);
        event->accept();
    }
}

void ZoomPhotoView::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        m_userZoomed = false;
        resetFit();
        setCursor(Qt::OpenHandCursor);
        m_dragging = false;
        event->accept();
    }
}

void ZoomPhotoView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.fillRect(rect(), QColor(0x20, 0x24, 0x2a));
    if (m_pix.isNull()) {
        p.setPen(Qt::white);
        p.drawText(rect(), Qt::AlignCenter,
                   lang("图片无法读取", "Image unreadable"));
        return;
    }
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.translate(m_offset);
    p.scale(m_scale, m_scale);
    p.drawPixmap(0, 0, m_pix);
    // 两框叠加（线宽随缩放反比，视觉恒定 2px）
    const qreal penW = 2.0 / m_scale;
    const auto drawBox = [&p, penW](const QRect &r, const QColor &c,
                                    const QString &tag, qreal scale) {
        if (!r.isValid())
            return;
        p.setPen(QPen(c, penW, Qt::DashLine));
        p.drawRect(r);
        QFont f = p.font();
        f.setPointSizeF(12.0 / scale);
        f.setBold(true);
        p.setFont(f);
        const QPointF tagPos(r.left(), r.top() - 4.0 / scale);
        p.setPen(c);
        p.drawText(tagPos, tag);
    };
    drawBox(m_box1, QColor(230, 85, 60), lang("监控主机", "DVR"), m_scale);
    drawBox(m_box2, QColor(0, 130, 200), lang("北京时间", "Beijing"), m_scale);
}

// ---------------------------------------------------------------------------
// TruthPhotoConfirmDialog：图片（左，可缩放）+ 识别结果（右）+ 确认/取消
// ---------------------------------------------------------------------------
TruthPhotoConfirmDialog::TruthPhotoConfirmDialog(
    const QString &imagePath, const QRect &monitorBox, const QRect &beijingBox,
    const QString &monitorTimeText, const QString &monitorRawText,
    const QString &beijingTimeText, const QString &beijingRawText,
    const QString &offsetVerboseText, const QString &crossDayNote,
    QWidget *parent)
    : QDialog(parent)
{
    setWindowTitle(lang("确认对时偏差", "Confirm offset"));
    auto *lay = new QHBoxLayout(this);

    QPixmap pix;
    pix.load(imagePath);
    auto *view = new ZoomPhotoView(pix, monitorBox, beijingBox, this);
    lay->addWidget(view, 1);

    auto *right = new QVBoxLayout();
    auto *title = new QLabel(lang("请核对原文读数（滚轮放大图片、拖动平移、双击复位）",
                                  "Verify readings (wheel=zoom, drag=pan, "
                                  "double-click=fit)"), this);
    title->setWordWrap(true);
    title->setStyleSheet(QStringLiteral("color:#c90;font-weight:bold;"));
    right->addWidget(title);

    auto mkRow = [this](const QString &k, const QString &v, bool mono) {
        auto *row = new QVBoxLayout();
        auto *kl = new QLabel(k, this);
        kl->setStyleSheet(QStringLiteral("color:#888;"));
        auto *vl = new QLabel(v, this);
        vl->setTextInteractionFlags(Qt::TextSelectableByMouse);
        vl->setWordWrap(true);
        if (mono)
            vl->setStyleSheet(QStringLiteral("font-family:Consolas,monospace;"));
        row->addWidget(kl);
        row->addWidget(vl);
        return row;
    };
    right->addLayout(mkRow(lang("监控主机时间：", "Recorder clock:"),
                           monitorTimeText, false));
    right->addLayout(mkRow(lang("框 1 原文：", "Box 1 raw:"),
                           monitorRawText, true));
    right->addSpacing(6);
    right->addLayout(mkRow(lang("北京时间：", "Beijing time:"),
                           beijingTimeText, false));
    right->addLayout(mkRow(lang("框 2 原文：", "Box 2 raw:"),
                           beijingRawText, true));
    right->addSpacing(10);

    auto *offLabel = new QLabel(lang("偏差：监控主机时间比北京时间 %1 %2",
                                     "Offset: recorder is %1 %2")
                                    .arg(offsetVerboseText, crossDayNote),
                                this);
    offLabel->setWordWrap(true);
    offLabel->setStyleSheet(QStringLiteral("font-weight:bold; font-size:14px;"));
    right->addWidget(offLabel);

    auto *warn = new QLabel(lang("OCR 可能误读个别数字（如秒位）。请放大图片核对\n"
                                 "两个框内读数与上方解析一致后再确认。",
                                 "OCR may misread digits (e.g. seconds). Zoom in "
                                 "and verify both boxes before confirming."),
                            this);
    warn->setWordWrap(true);
    warn->setStyleSheet(QStringLiteral("color:#c90;"));
    right->addWidget(warn);
    right->addStretch(1);

    auto *useBtn = new QPushButton(lang("✅ 使用此偏差", "✅ Use this offset"),
                                   this);
    useBtn->setDefault(true);
    useBtn->setMinimumHeight(32);
    auto *cancelBtn = new QPushButton(lang("取消", "Cancel"), this);
    auto *btnRow = new QHBoxLayout();
    btnRow->addWidget(useBtn, 1);
    btnRow->addWidget(cancelBtn);
    right->addLayout(btnRow);
    auto *rightW = new QWidget(this);
    rightW->setLayout(right);
    rightW->setFixedWidth(360);
    lay->addWidget(rightW);

    connect(useBtn, &QPushButton::clicked, this, &QDialog::accept);
    connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);

    resize(1120, 640);
}
