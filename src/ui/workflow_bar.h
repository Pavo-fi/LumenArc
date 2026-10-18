/**
 * @file ui/workflow_bar.h
 * @brief 前端 V2 骨架：工作流条（六阶段 + 闸门状态点 + 滑块动画）
 *
 * 全自绘（不挂子控件）：阶段滑块、闸门点、悬停高亮统一在 paintEvent 里画，
 * 才能做到「点分色、块滑过去」这种原型级观感。
 */
#pragma once

#include <QWidget>
#include <QPointer>
#include <QList>
#include <QRect>

#include "ui/app_state.h"

class QVariantAnimation;

namespace ui {

class AppState;

/// 工作流条
class WorkflowBar : public QWidget
{
    Q_OBJECT

public:
    explicit WorkflowBar(AppState *state, QWidget *parent = nullptr);

signals:
    void stageClicked(ui::Stage s);
    void commandPaletteRequested();

protected:
    void paintEvent(QPaintEvent *e) override;
    void mousePressEvent(QMouseEvent *e) override;
    void mouseMoveEvent(QMouseEvent *e) override;
    void leaveEvent(QEvent *e) override;
    void resizeEvent(QResizeEvent *e) override;
    bool event(QEvent *e) override;

private:
    void relayout();
    void movePill(bool animate);
    int  hitStage(const QPoint &p) const;

    AppState *m_state = nullptr;
    QList<QRect> m_rects;       ///< 六个阶段的可点矩形
    QRect m_paletteRect;        ///< 右侧命令面板矩形
    QRect m_pill;               ///< 当前阶段滑块（动画中）
    int   m_hover = -1;
    QPointer<QVariantAnimation> m_anim;
};

} // namespace ui
