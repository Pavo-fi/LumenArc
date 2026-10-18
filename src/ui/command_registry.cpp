/**
 * @file ui/command_registry.cpp
 * @brief 前端 V2 骨架：命令注册表实现
 */
#include "ui/command_registry.h"

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>

namespace ui {

CommandRegistry::CommandRegistry(QObject *parent)
    : QObject(parent)
{
}

QString CommandRegistry::cleanTitle(const QString &raw)
{
    QString t = raw;
    t.remove(QLatin1Char('&'));
    while (t.endsWith(QLatin1Char('.')) || t.endsWith(QChar(0x2026)))
        t.chop(1);
    return t.trimmed();
}

void CommandRegistry::collect(QMenu *menu, const QString &group)
{
    if (!menu)
        return;

    const QList<QAction *> actions = menu->actions();
    for (QAction *a : actions) {
        if (!a || a->isSeparator())
            continue;

        // 子菜单递归；分组仍沿用顶级菜单名
        if (a->menu()) {
            collect(a->menu(), group);
            continue;
        }

        Command c;
        c.title    = cleanTitle(a->text());
        c.group    = cleanTitle(group);
        c.action   = a;
        c.shortcut = a->shortcut().toString(QKeySequence::NativeText);
        c.enabled  = a->isEnabled();
        c.visible  = a->isVisible();
        c.id       = c.group + QStringLiteral(" / ") + c.title;
        if (c.title.isEmpty())
            continue;
        m_commands.append(c);
    }
}

void CommandRegistry::rebuild(QMenuBar *menuBar)
{
    m_commands.clear();
    if (!menuBar) {
        emit changed();
        return;
    }

    const QList<QAction *> tops = menuBar->actions();
    for (QAction *top : tops) {
        if (!top || !top->menu())
            continue;
        collect(top->menu(), top->text());
    }

    emit changed();
}

QList<Command> CommandRegistry::search(const QString &needle) const
{
    const QString n = needle.trimmed();
    if (n.isEmpty())
        return m_commands;

    QList<Command> out;
    for (const Command &c : m_commands) {
        if (c.title.contains(n, Qt::CaseInsensitive)
            || c.group.contains(n, Qt::CaseInsensitive)
            || c.shortcut.contains(n, Qt::CaseInsensitive)) {
            out.append(c);
        }
    }
    return out;
}

Command *CommandRegistry::find(const QString &id)
{
    for (Command &c : m_commands) {
        if (c.id == id)
            return &c;
    }
    return nullptr;
}

bool CommandRegistry::trigger(const QString &id)
{
    Command *c = find(id);
    if (!c || !c->action)
        return false;
    if (!c->action->isEnabled())
        return false;
    c->action->trigger();
    return true;
}

bool CommandRegistry::triggerByTitle(const QString &needle)
{
    if (needle.trimmed().isEmpty())
        return false;
    for (const Command &c : m_commands) {
        if (!c.action || !c.action->isEnabled())
            continue;
        if (c.title.contains(needle, Qt::CaseInsensitive)) {
            c.action->trigger();
            return true;
        }
    }
    return false;
}

} // namespace ui
