/**
 * @file ui/inspector_panel.h
 * @brief 前端 V2 骨架：检视器（当前上下文，随选中对象变化）
 *
 * 原型的核心结构之一：每个工作区都有一块「上下文」。
 * 本面板只**展示**（由 MainWindow 推送事实）+ 发起「工具/显示」请求，
 * 不自己持有任何领域状态（见 docs/FRONTEND_V2_IA_CN.md §5 两条铁律）。
 */
#pragma once

#include <QWidget>
#include <QList>
#include <QString>

class QScrollArea;

class QLabel;
class QPushButton;
class QVBoxLayout;
class QGridLayout;

namespace ui {

/// 检视器：素材 / 时间基准 / 游标 / ROI / 工具 / 显示
class InspectorPanel : public QWidget
{
    Q_OBJECT

public:
    explicit InspectorPanel(QWidget *parent = nullptr);

    // ---- 事实推送（MainWindow →）----
    void setSource(const QString &name, int w, int h, double fps, qint64 durationMs);
    void setCalibration(const QString &state, bool ok);
    void setCursor(qint64 ms);
    void setRoi(int rects, int polys);
    void setMagnifier(bool on);

    /// v2.0：把主窗口「分析与标注」整行的真控件收进「工具」区。
    /// 传进来的 widget 由本面板接管布局（reparent），启用态/勾选态与原来完全一致。
    void addToolGroup(const QString &label, const QList<QWidget *> &widgets,
                      bool primary = false);
    /// 「截图叠加」栏：截取/编辑/放置（栏标题本身不可点）
    void setCaptureWidgets(const QList<QWidget *> &widgets);
    /// 「显示」栏：快照/画面调节（自动排在放大镜之前）
    void setDisplayWidgets(const QList<QWidget *> &widgets);

signals:
    void magnifierToggled(bool on);

private:
    QLabel *addSection(QVBoxLayout *lay, const QString &title, int topPad = 12);
    QLabel *addRow(QVBoxLayout *lay, const QString &key);

    QLabel *m_name = nullptr;
    QLabel *m_size = nullptr;
    QLabel *m_fps = nullptr;
    QLabel *m_dur = nullptr;
    QLabel *m_cal = nullptr;
    QLabel *m_cursor = nullptr;
    QLabel *m_roi = nullptr;
    QVBoxLayout *m_toolLay = nullptr;      ///< 「工具」区容器（依次追加分组）
    QVBoxLayout *m_captureLay = nullptr;   ///< 「截图叠加」栏
    QVBoxLayout *m_displayLay = nullptr;   ///< 「显示」栏
    QPushButton *m_magBtn = nullptr;

    QGridLayout *makeChipGrid(const QList<QWidget *> &widgets, bool primary);
    /// 行渲染：键（小灰）+ 值（大亮）内联在一行富文本里 —— 值自然换行，永远不被裁
    void setRowText(QLabel *l, const QString &value, const QString &color);
};

} // namespace ui
