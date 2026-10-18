/**
 * @file ui/chevron_button.h
 * @brief 折叠/展开按钮：自绘 chevron，展开↔收起带旋转动画
 *
 * 替代原来的「▼/▲」文字按钮（字形随系统字体变、旋转无过渡、观感廉价）。
 */
#pragma once

#include <QAbstractButton>
#include <QPointer>

class QVariantAnimation;

namespace ui {

/// 折叠/展开按钮
class ChevronButton : public QAbstractButton
{
    Q_OBJECT

public:
    explicit ChevronButton(QWidget *parent = nullptr);

    /// 展开=true（箭头朝下 0°）；收起=false（箭头朝右 -90°）
    void setExpanded(bool on, bool animate = true);
    bool isExpanded() const { return m_expanded; }

    QSize sizeHint() const override { return QSize(20, 20); }

protected:
    void paintEvent(QPaintEvent *e) override;
    void enterEvent(QEnterEvent *e) override;
    void leaveEvent(QEvent *e) override;

private:
    bool m_expanded = true;
    int  m_angle = 0;      ///< 0 = 朝下（展开），-90 = 朝右（收起）
    bool m_hover = false;
    QPointer<QVariantAnimation> m_anim;
};

} // namespace ui
