/**
 * @file ui/task_bar.cpp
 * @brief 前端 V2 骨架：任务带实现
 */
#include "ui/task_bar.h"

#include "i18n.h"
#include "theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QProgressBar>
#include <QPushButton>
#include <QToolButton>
#include <QFrame>
#include <QVBoxLayout>
#include <QHBoxLayout>

namespace ui {

TaskBar::TaskBar(QWidget *parent)
    : QWidget(parent)
{
    setObjectName(QStringLiteral("taskBar"));
    setFixedHeight(34);
    setStyleSheet(QStringLiteral("QWidget#taskBar{background:%1;border-top:1px solid %2;}")
                      .arg(Theme::BgPanel, Theme::Border));

    auto *lay = new QHBoxLayout(this);
    lay->setContentsMargins(10, 0, 10, 0);
    lay->setSpacing(8);

    // 左侧接入区（降噪）/ 中部（播放）/ 右侧（快照）——三区对称，中部真正居中
    m_leading = new QHBoxLayout();
    m_leading->setSpacing(6);
    lay->addLayout(m_leading);
    lay->addStretch(1);

    m_center = new QHBoxLayout();
    m_center->setSpacing(6);
    lay->addLayout(m_center);
    lay->addStretch(1);

    m_trailing = new QHBoxLayout();
    m_trailing->setSpacing(6);
    lay->addLayout(m_trailing);

    m_title = new QLabel(this);
    m_title->setStyleSheet(QStringLiteral("color:%1;background:transparent;")
                               .arg(Theme::TextSecond));
    lay->addWidget(m_title);

    m_bar = new QProgressBar(this);
    m_bar->setRange(0, 100);
    m_bar->setFixedWidth(180);
    m_bar->setTextVisible(false);
    m_bar->setFixedHeight(6);
    lay->addWidget(m_bar);

    m_pct = new QLabel(this);
    m_pct->setStyleSheet(QStringLiteral("color:%1;background:transparent;")
                             .arg(Theme::TextSecond));
    lay->addWidget(m_pct);

    m_eta = new QLabel(this);
    m_eta->setStyleSheet(QStringLiteral("color:%1;background:transparent;")
                             .arg(Theme::TextMuted));
    lay->addWidget(m_eta);

    m_cancel = new QPushButton(lang(QStringLiteral("取消"), QStringLiteral("Cancel")), this);
    m_cancel->setFixedHeight(22);
    m_cancel->setFocusPolicy(Qt::NoFocus);
    m_cancel->setCursor(Qt::PointingHandCursor);
    lay->addWidget(m_cancel);

    lay->addStretch(1);

    // v2.5e：任务 >=2 时出现「共 N 项 ▴」——点它向上弹出堆叠列表
    m_more = new QToolButton(this);
    m_more->setText(QString());
    m_more->setFocusPolicy(Qt::NoFocus);
    m_more->setCursor(Qt::PointingHandCursor);
    m_more->setStyleSheet(QStringLiteral(
        "QToolButton{background:transparent;color:%1;border:1px solid transparent;"
        "border-radius:5px;padding:2px 8px;font-size:11px;}"
        "QToolButton:hover{color:%2;border-color:%3;}")
        .arg(Theme::TextMuted, Theme::Accent, Theme::Border));
    m_more->hide();
    lay->addWidget(m_more);
    connect(m_more, &QToolButton::clicked, this, &TaskBar::togglePopup);

    connect(m_cancel, &QPushButton::clicked, this, [this]() {
        if (!m_tasks.isEmpty())
            emit cancelRequested(m_tasks.first().id);
    });

    refresh();
}

void TaskBar::setLeadingWidgets(const QList<QWidget *> &widgets)
{
    if (!m_leading)
        return;
    for (QWidget *w : widgets) {
        if (!w)
            continue;
        w->setParent(this);
        m_leading->addWidget(w);
        w->show();
    }
}

void TaskBar::setCenterWidgets(const QList<QWidget *> &widgets)
{
    if (!m_center)
        return;
    for (QWidget *w : widgets) {
        if (!w)
            continue;
        w->setParent(this);
        m_center->addWidget(w);
        w->show();
    }
}

void TaskBar::setTrailingWidgets(const QList<QWidget *> &widgets)
{
    if (!m_trailing)
        return;
    for (QWidget *w : widgets) {
        if (!w)
            continue;
        w->setParent(this);
        m_trailing->addWidget(w);
        w->show();
    }
}

int TaskBar::addTask(const QString &title)
{
    Entry e;
    e.id = m_nextId++;
    e.title = title;
    m_tasks.append(e);
    refresh();
    return e.id;
}

void TaskBar::setTaskProgress(int id, int percent, const QString &eta)
{
    for (Entry &e : m_tasks) {
        if (e.id != id)
            continue;
        e.percent = qBound(0, percent, 100);
        if (!eta.isEmpty())
            e.eta = eta;
        break;
    }
    refresh();
}

void TaskBar::finishTask(int id, bool ok, const QString &note)
{
    Q_UNUSED(note);
    if (!ok) {
        for (Entry &e : m_tasks) {
            if (e.id == id) {
                e.percent = 100;
                e.eta = lang(QStringLiteral("失败"), QStringLiteral("Failed"));
                break;
            }
        }
        refresh();
        return;
    }
    removeTask(id);
}

void TaskBar::removeTask(int id)
{
    for (int i = 0; i < m_tasks.size(); ++i) {
        if (m_tasks[i].id == id) {
            m_tasks.removeAt(i);
            break;
        }
    }
    refresh();
}

void TaskBar::refresh()
{
    const bool busy = !m_tasks.isEmpty();
    m_title->setVisible(busy);
    m_bar->setVisible(busy);
    m_pct->setVisible(busy);
    m_eta->setVisible(busy);
    m_cancel->setVisible(busy);

    if (!busy) {
        m_more->hide();
        if (m_popup)
            m_popup->hide();
        return;
    }

    const Entry &e = m_tasks.first();
    m_title->setText(e.title);
    m_bar->setValue(e.percent);
    m_pct->setText(QStringLiteral("%1%").arg(e.percent));
    m_eta->setText(e.eta);
    const int extra = m_tasks.size() - 1;
    m_more->setVisible(extra >= 1);
    m_more->setText(extra >= 1
                        ? lang(QStringLiteral("共 %1 项 ▴").arg(m_tasks.size()),
                               QStringLiteral("%1 tasks ▴").arg(m_tasks.size()))
                        : QString());
    if (m_popup && m_popup->isVisible())
        rebuildPopup();      // 弹着的时候实时跟着刷新
}

// ---------------------------------------------------------------------------
// v2.5e：上拉菜单堆叠显示
// 同时跑校时 / 音频 / 微变 / ROI 量化时，任务带只显示第一条；其余进这里，
// 点「共 N 项 ▴」在任务带上方向上弹出，每条一行：标题 + 进度 + 备注 + 取消
// ---------------------------------------------------------------------------
void TaskBar::togglePopup()
{
    if (m_popup && m_popup->isVisible()) {
        m_popup->hide();
        return;
    }
    if (!m_popup) {
        m_popup = new QFrame(this, Qt::Popup);
        m_popup->setObjectName(QStringLiteral("taskPopup"));
        m_popup->setStyleSheet(QStringLiteral(
            "QFrame#taskPopup{background:%1;border:1px solid %2;border-radius:8px;}"
            "QFrame#taskPopup QLabel{background:transparent;border:0;}")
            .arg(Theme::BgCard, Theme::Border));
        auto *v = new QVBoxLayout(m_popup);
        v->setContentsMargins(10, 8, 10, 8);
        v->setSpacing(6);
    }
    rebuildPopup();
    const QSize hint = m_popup->sizeHint();
    m_popup->resize(qMax(340, hint.width()), hint.height());
    const QPoint anchor = m_more->mapToGlobal(QPoint(m_more->width(), 0));
    m_popup->move(anchor.x() - m_popup->width(),
                  anchor.y() - m_popup->height() - 4);   // 向上弹
    m_popup->show();
}

void TaskBar::rebuildPopup()
{
    if (!m_popup || !m_popup->layout())
        return;
    QLayout *v = m_popup->layout();
    // 清空旧行
    while (QLayoutItem *it = v->takeAt(0)) {
        if (QWidget *w = it->widget())
            w->deleteLater();
        delete it;
    }
    for (const Entry &e : m_tasks) {
        auto *row = new QWidget(m_popup);
        auto *hl = new QHBoxLayout(row);
        hl->setContentsMargins(0, 0, 0, 0);
        hl->setSpacing(8);

        auto *title = new QLabel(e.title, row);
        title->setMinimumWidth(120);
        title->setStyleSheet(QStringLiteral("color:%1;font-size:11px;")
                                 .arg(Theme::TextPrimary));
        hl->addWidget(title, 1);

        auto *bar = new QProgressBar(row);
        bar->setRange(0, 100);
        bar->setValue(e.percent);
        bar->setTextVisible(false);
        bar->setFixedSize(90, 6);
        bar->setStyleSheet(QStringLiteral(
            "QProgressBar{background:%1;border:0;border-radius:3px;}"
            "QProgressBar::chunk{background:%2;border-radius:3px;}")
            .arg(Theme::BgPanel, Theme::Accent));
        hl->addWidget(bar, 0);

        auto *pct = new QLabel(QStringLiteral("%1%").arg(e.percent), row);
        pct->setStyleSheet(QStringLiteral("color:%1;font-size:11px;")
                               .arg(Theme::TextSecond));
        pct->setMinimumWidth(38);
        hl->addWidget(pct, 0);

        if (!e.eta.isEmpty()) {
            auto *eta = new QLabel(e.eta, row);
            eta->setStyleSheet(QStringLiteral("color:%1;font-size:11px;")
                                   .arg(Theme::TextMuted));
            hl->addWidget(eta, 0);
        }

        auto *btn = new QToolButton(row);
        btn->setText(QString(QChar(0x2715)));    // ✕
        btn->setFocusPolicy(Qt::NoFocus);
        btn->setCursor(Qt::PointingHandCursor);
        btn->setToolTip(lang(QStringLiteral("取消这个任务"),
                             QStringLiteral("Cancel this task")));
        btn->setStyleSheet(QStringLiteral(
            "QToolButton{background:transparent;color:%1;border:0;padding:0 4px;}"
            "QToolButton:hover{color:%2;}")
            .arg(Theme::TextMuted, Theme::Accent));
        const int id = e.id;
        connect(btn, &QToolButton::clicked, this, [this, id]() {
            emit cancelRequested(id);
        });
        hl->addWidget(btn, 0);

        v->addWidget(row);
    }
}

} // namespace ui
