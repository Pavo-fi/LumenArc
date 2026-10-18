/**
 * @file ui/command_registry.h
 * @brief 前端 V2 骨架：命令注册表（命令的唯一登记处）
 *
 * 菜单栏是当前的事实来源：遍历 QMenuBar 收集全部 QAction，
 * 菜单里加一项，注册表、命令面板、快捷键表**自动**多一项，不需两处登记。
 * 后续接入工作流条 / 右键菜单 / 快捷键速查表时共用同一份数据。
 */
#pragma once

#include <QObject>
#include <QList>
#include <QString>

class QMenuBar;
class QMenu;
class QAction;
class QKeySequence;

namespace ui {

/// 一条用户可执行命令
struct Command
{
    QString  id;          ///< 稳定标识（分组 / 标题）
    QString  title;       ///< 显示名（含助记符，显示前可清洗）
    QString  group;       ///< 分组（顶级菜单名）
    QString  shortcut;    ///< 快捷键文本（NativeText）
    QAction *action = nullptr;   ///< 背后真实动作（可为空 = 只读展示）
    bool     enabled = true;
    bool     visible = true;
};

/**
 * @brief 命令注册表：从菜单栏实时收集 QAction。
 */
class CommandRegistry : public QObject
{
    Q_OBJECT

public:
    explicit CommandRegistry(QObject *parent = nullptr);

    /// 从菜单栏重建（递归展开子菜单；分组沿用顶级菜单名）
    void rebuild(QMenuBar *menuBar);

    int count() const { return m_commands.size(); }
    const QList<Command> &commands() const { return m_commands; }

    /// 模糊过滤（标题 / 分组 / 快捷键）
    QList<Command> search(const QString &needle) const;

    Command *find(const QString &id);
    /// 触发一条命令；返回是否真的触发
    bool trigger(const QString &id);
    /// 按标题片段触发（新工作面按钮解耦用；例：“打开视频”）
    bool triggerByTitle(const QString &needle);

    /// 去掉 & 助记符与尾部 "..."，用于在面板里显示
    static QString cleanTitle(const QString &raw);

signals:
    void changed();

private:
    void collect(QMenu *menu, const QString &group);

    QList<Command> m_commands;
};

} // namespace ui
