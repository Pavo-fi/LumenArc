/**
 * @file keyguardfilter.h
 * @brief 全局快捷键守卫（v1.17.0 P-78，Q5 收口）
 *
 * 安装到可聚焦控件（滑杆/列表/树/菜单等）上：KeyPress 先转发给处理器，
 * 处理器返回 true = 快捷键已消费（该控件不再收到此键），false = 放行。
 *
 * Q5 纪律：eventFilter 本体不写大 switch 键路由——18 键 switch 住在
 * MainWindow::handleGlobalShortcut（共享处理函数），本对象只做"守卫+转发"。
 * 禁止替代方案：不给可聚焦控件套 QShortcut（焦点语义漂移，v1.17.0 方案 Q4 拍板）。
 *
 * 纯 eventFilter 转发器：无信号/槽 → 无 Q_OBJECT → 头文件即完整实现。
 */
#pragma once

#include <QObject>
#include <QKeyEvent>
#include <functional>

class KeyGuardFilter : public QObject
{
public:
    using Handler = std::function<bool(QKeyEvent *)>;

    explicit KeyGuardFilter(QObject *parent = nullptr) : QObject(parent) {}

    void setHandler(Handler h) { m_handler = std::move(h); }

    /// eventFilter 入口：KeyPress 交给处理器；true = 快捷键已消费
    /// （阻断控件收键），false = 放行（继续走默认分发/上层 eventFilter）。
    /// 注意：必须 override eventFilter 本体——本对象是直接挂在
    /// installEventFilter 上的过滤器（MainWindow::eventFilter 不再被调用）。
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::KeyPress && m_handler)
            if (m_handler(static_cast<QKeyEvent *>(event)))
                return true;
        return false;
    }

    /// 显式转发入口（供 MainWindow::eventFilter 转发路径/单元测试用）
    bool filterKeyPress(QObject *watched, QKeyEvent *e)
    {
        Q_UNUSED(watched)
        return m_handler ? m_handler(e) : false;
    }

private:
    Handler m_handler;
};