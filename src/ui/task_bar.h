/**
 * @file ui/task_bar.h
 * @brief 前端 V2 骨架：任务带（所有长任务一个队列）
 *
 * OCR / 校时 / 秒级对齐 / 转码 / 渲染 / 导出统一进这里，
 * 取代「每个对话框各自一条进度条」。
 */
#pragma once

#include <QWidget>
#include <QList>
#include <QString>

class QLabel;
class QToolButton;
class QFrame;
class QProgressBar;
class QPushButton;
class QHBoxLayout;

namespace ui {

/// 统一任务带
class TaskBar : public QWidget
{
    Q_OBJECT

public:
    explicit TaskBar(QWidget *parent = nullptr);

    /// 在任务带**左侧**接入一组控件（降噪控件）
    void setLeadingWidgets(const QList<QWidget *> &widgets);
    /// 中部（播放控制）
    void setCenterWidgets(const QList<QWidget *> &widgets);
    /// 右侧（快照等，任务区之前）
    void setTrailingWidgets(const QList<QWidget *> &widgets);

    /// 登记一条任务，返回任务 id
    int  addTask(const QString &title);
    /// 更新进度（percent 0-100，eta 为「2:41 / 4:00」这类文本）
    void setTaskProgress(int id, int percent, const QString &eta = QString());
    /// 结束任务：ok=true 短暂提示后移除，false 保留并标红
    void finishTask(int id, bool ok, const QString &note = QString());
    void removeTask(int id);
    int  activeCount() const { return m_tasks.size(); }

signals:
    void cancelRequested(int id);

private:
    struct Entry {
        int     id = 0;
        QString title;
        int     percent = 0;
        QString eta;
    };

    void refresh();
    /// v2.5e：上拉菜单——同时跑多个任务时，点「共 N 项」向上弹出堆叠列表
    void togglePopup();
    void rebuildPopup();

    QList<Entry>   m_tasks;
    int            m_nextId = 1;
    QHBoxLayout   *m_leading = nullptr;   ///< 左侧接入区（降噪）
    QHBoxLayout   *m_center = nullptr;    ///< 中部（播放控制）
    QHBoxLayout   *m_trailing = nullptr;  ///< 右侧（快照等）
    QLabel        *m_title = nullptr;
    QProgressBar  *m_bar = nullptr;
    QLabel        *m_pct = nullptr;
    QLabel        *m_eta = nullptr;
    QPushButton   *m_cancel = nullptr;
    QToolButton   *m_more = nullptr;      ///< 「共 N 项 ▴」（>=2 个任务才显示）
    QFrame        *m_popup = nullptr;     ///< 上拉堆叠列表（Qt::Popup）
};

} // namespace ui
