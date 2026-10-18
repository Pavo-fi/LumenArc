/**
 * @file ui/chevron_button.cpp
 * @brief 折叠/展开按钮实现
 */
#include "ui/chevron_button.h"

#include "theme.h"

#include <QEasingCurve>
#include <QEnterEvent>
#include <QPainter>
#include <QVariantAnimation>

namespace ui {

ChevronButton::ChevronButton(QWidget *parent)
    : QAbstractButton(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setFixedSize(20, 20);
    connect(this, &QAbstractButton::clicked, this, [this]() {
        setExpanded(!m_expanded, true);
    });
}

void ChevronButton::setExpanded(bool on, bool animate)
{
    m_expanded = on;
    const int target = on ? 0 : -90;

    if (!animate) {
        m_angle = target;
        update();
        return;
    }

    if (m_anim)
        m_anim->stop();

    auto *anim = new QVariantAnimation(this);
    anim->setDuration(200);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->setStartValue(m_angle);
    anim->setEndValue(target);
    connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_angle = v.toInt();
        update();
    });
    m_anim = anim;
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ChevronButton::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    if (m_hover) {
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(Theme::BgHover));
        p.drawRoundedRect(rect().adjusted(0, 0, -1, -1), 5, 5);
    }

    p.translate(rect().center());
    p.rotate(m_angle);

    QPen pen(QColor(m_hover ? Theme::TextPrimary : Theme::TextSecond));
    pen.setWidthF(1.7);
    pen.setCapStyle(Qt::RoundCap);
    pen.setJoinStyle(Qt::RoundJoin);
    p.setPen(pen);

    const qreal s = 3.8;
    p.drawLine(QPointF(-s, -s * 0.55), QPointF(0.0, s * 0.55));
    p.drawLine(QPointF(0.0, s * 0.55), QPointF(s, -s * 0.55));
}

void ChevronButton::enterEvent(QEnterEvent *e)
{
    m_hover = true;
    update();
    QAbstractButton::enterEvent(e);
}

void ChevronButton::leaveEvent(QEvent *e)
{
    m_hover = false;
    update();
    QAbstractButton::leaveEvent(e);
}

} // namespace ui
