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

    m_more = new QLabel(this);
    m_more->setStyleSheet(QStringLiteral("color:%1;background:transparent;")
                              .arg(Theme::TextMuted));
    lay->addWidget(m_more);

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
        m_more->setText(lang(QStringLiteral("空闲"), QStringLiteral("Idle")));
        return;
    }

    const Entry &e = m_tasks.first();
    m_title->setText(e.title);
    m_bar->setValue(e.percent);
    m_pct->setText(QStringLiteral("%1%").arg(e.percent));
    m_eta->setText(e.eta);
    m_more->setText(m_tasks.size() > 1
                        ? lang(QStringLiteral("另有 %1 个任务").arg(m_tasks.size() - 1),
                               QStringLiteral("%1 more").arg(m_tasks.size() - 1))
                        : QString());
}

} // namespace ui
