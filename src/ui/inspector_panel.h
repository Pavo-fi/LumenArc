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

class QLabel;
class QPushButton;
class QVBoxLayout;

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
    void setTool(int tool);          ///< 0 选择 / 1 矩形 / 2 多边形 / 3 辅助线
    void setMagnifier(bool on);
    void setOverlay(bool on);

signals:
    void toolRequested(int tool);
    void magnifierToggled(bool on);
    void overlayToggled(bool on);

private:
    QLabel *addSection(QVBoxLayout *lay, const QString &title);
    QLabel *addRow(QVBoxLayout *lay, const QString &key);

    QLabel *m_name = nullptr;
    QLabel *m_size = nullptr;
    QLabel *m_fps = nullptr;
    QLabel *m_dur = nullptr;
    QLabel *m_cal = nullptr;
    QLabel *m_cursor = nullptr;
    QLabel *m_roi = nullptr;
    QList<QPushButton *> m_toolBtns;
    QPushButton *m_magBtn = nullptr;
    QPushButton *m_ovlBtn = nullptr;
};

} // namespace ui
