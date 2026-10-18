/**
 * @file ui/workflow_bar.cpp
 * @brief 前端 V2 骨架：工作流条（全自绘）
 */
#include "ui/workflow_bar.h"

#include "i18n.h"
#include "theme.h"

#include <QEasingCurve>
#include <QFontMetrics>
#include <QHelpEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QToolTip>
#include <QVariantAnimation>

namespace ui {

namespace {

const Stage kOrder[] = { Stage::Ingest, Stage::Prepare, Stage::Align,
                         Stage::Analyze, Stage::Produce, Stage::Deliver };
const int kStageCount = int(sizeof(kOrder) / sizeof(kOrder[0]));

QString stageEn(Stage s)
{
    switch (s) {
    case Stage::Ingest:  return QStringLiteral("Ingest");
    case Stage::Prepare: return QStringLiteral("Prepare");
    case Stage::Align:   return QStringLiteral("Align");
    case Stage::Analyze: return QStringLiteral("Analyze");
    case Stage::Produce: return QStringLiteral("Produce");
    case Stage::Deliver: return QStringLiteral("Deliver");
    default:             return QString();
    }
}

QColor gateColor(GateState g)
{
    switch (g) {
    case GateState::Ready:          return QColor(Theme::Success);
    case GateState::NeedsAttention: return QColor(Theme::Accent);
    default:                        return QColor(Theme::TextMuted);
    }
}

} // namespace

WorkflowBar::WorkflowBar(AppState *state, QWidget *parent)
    : QWidget(parent)
    , m_state(state)
{
    setFixedHeight(44);
    setMouseTracking(true);
    setCursor(Qt::ArrowCursor);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);

    if (m_state)
        connect(m_state, &AppState::changed, this, [this]() {
            relayout();
            movePill(true);
            update();
        });

    relayout();
    movePill(false);
}

void WorkflowBar::relayout()
{
    const QFontMetrics fm(font());
    m_rects.clear();

    int x = 12 + fm.horizontalAdvance(QStringLiteral("LumenArc")) + 20;   // 品牌区
    for (int i = 0; i < kStageCount; ++i) {
        const QString label = QStringLiteral("%1  %2").arg(i + 1).arg(stageName(kOrder[i]));
        const int w = fm.horizontalAdvance(label) + 30;                   // 留出闸门点位置
        m_rects.append(QRect(x, 6, w, height() - 13));
        x += w + 3;
    }

    const QString pt = lang(QStringLiteral("Ctrl+K  命令 / 跳转"),
                            QStringLiteral("Ctrl+K  Commands"));
    const int pw = fm.horizontalAdvance(pt) + 22;
    m_paletteRect = QRect(width() - 12 - pw, 7, pw, height() - 15);
}

void WorkflowBar::movePill(bool animate)
{
    if (!m_state)
        return;
    const int idx = int(m_state->stage());
    if (idx < 0 || idx >= m_rects.size())
        return;

    const QRect target = m_rects[idx].adjusted(-2, -1, 2, 1);

    if (!animate || !m_pill.isValid()) {
        m_pill = target;
        update();
        return;
    }

    if (m_anim)
        m_anim->stop();

    auto *anim = new QVariantAnimation(this);
    anim->setDuration(240);
    anim->setEasingCurve(QEasingCurve::OutCubic);
    anim->setStartValue(m_pill);
    anim->setEndValue(target);
    connect(anim, &QVariantAnimation::valueChanged, this, [this](const QVariant &v) {
        m_pill = v.toRect();
        update();
    });
    m_anim = anim;
    anim->start(QAbstractAnimation::DeleteWhenStopped);
}

int WorkflowBar::hitStage(const QPoint &p) const
{
    for (int i = 0; i < m_rects.size(); ++i)
        if (m_rects[i].contains(p))
            return i;
    return -1;
}

void WorkflowBar::paintEvent(QPaintEvent *e)
{
    Q_UNUSED(e);

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing, true);

    // 底色 + 下边线
    p.fillRect(rect(), QColor(Theme::BgPanel));
    p.setPen(QColor(Theme::Border));
    p.drawLine(0, height() - 1, width(), height() - 1);

    // 品牌
    {
        QFont f = font();
        f.setBold(true);
        p.setFont(f);
        p.setPen(QColor(Theme::TextPrimary));
        p.drawText(QRect(12, 0, 140, height()),
                   Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("LumenArc"));
        p.setFont(font());
    }

    // 当前阶段滑块（画在内容之下）
    if (m_pill.isValid()) {
        QPen pen{QColor(Theme::Accent)};   // 花括号：避免 most vexing parse
        pen.setWidth(2);
        p.setPen(pen);
        p.setBrush(QColor(Theme::BgCard));
        p.drawRoundedRect(m_pill, 6, 6);
    }

    // 六个阶段
    for (int i = 0; i < m_rects.size(); ++i) {
        const QRect r = m_rects[i];
        const bool current = (int(m_state ? m_state->stage() : Stage::Ingest) == i);

        if (m_hover == i && !current) {
            p.setPen(Qt::NoPen);
            p.setBrush(QColor(Theme::BgHover));
            p.drawRoundedRect(r, 5, 5);
        }

        // 闸门点
        const GateState g = m_state ? m_state->gate(kOrder[i]) : GateState::NotStarted;
        p.setPen(Qt::NoPen);
        p.setBrush(gateColor(g));
        p.drawEllipse(QPoint(r.left() + 11, r.center().y()), 3, 3);

        // 文本
        p.setPen(QColor(current ? Theme::TextPrimary : Theme::TextSecond));
        p.drawText(r.adjusted(20, 0, -8, 0), Qt::AlignVCenter | Qt::AlignLeft,
                   QStringLiteral("%1  %2").arg(i + 1).arg(stageName(kOrder[i])));
    }

    // 命令面板入口
    {
        p.setPen(QPen(QColor(Theme::Border)));
        p.setBrush(QColor(Theme::BgCard));
        p.drawRoundedRect(m_paletteRect, 5, 5);
        p.setPen(QColor(Theme::TextSecond));
        p.drawText(m_paletteRect, Qt::AlignCenter,
                   lang(QStringLiteral("Ctrl+K  命令 / 跳转"), QStringLiteral("Ctrl+K  Commands")));
    }
}

void WorkflowBar::mousePressEvent(QMouseEvent *e)
{
    if (e->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(e);
        return;
    }
    const int i = hitStage(e->pos());
    if (i >= 0) {
        emit stageClicked(kOrder[i]);
        return;
    }
    if (m_paletteRect.contains(e->pos())) {
        emit commandPaletteRequested();
        return;
    }
    QWidget::mousePressEvent(e);
}

void WorkflowBar::mouseMoveEvent(QMouseEvent *e)
{
    const int i = hitStage(e->pos());
    if (i != m_hover) {
        m_hover = i;
        update();
    }
    QWidget::mouseMoveEvent(e);
}

void WorkflowBar::leaveEvent(QEvent *e)
{
    if (m_hover != -1) {
        m_hover = -1;
        update();
    }
    QWidget::leaveEvent(e);
}

void WorkflowBar::resizeEvent(QResizeEvent *e)
{
    QWidget::resizeEvent(e);
    relayout();
    movePill(false);
}

bool WorkflowBar::event(QEvent *e)
{
    if (e->type() == QEvent::ToolTip) {
        auto *he = static_cast<QHelpEvent *>(e);
        const int i = hitStage(he->pos());
        if (i >= 0 && m_state) {
            const GateState g = m_state->gate(kOrder[i]);
            const QString tip = (g == GateState::Ready)
                                    ? lang(QStringLiteral("已完成"), QStringLiteral("Done"))
                                    : m_state->gateHint(kOrder[i]);
            QToolTip::showText(he->globalPos(),
                               QStringLiteral("%1 — %2/%3\n%4")
                                   .arg(stageName(kOrder[i]), stageName(kOrder[i]), stageEn(kOrder[i]), tip));
        } else {
            QToolTip::hideText();
        }
        return true;
    }
    return QWidget::event(e);
}

} // namespace ui
