/**
 * @file theme.h
 * @brief 全局设计令牌（Design Tokens）与样式表：统一背景/文字/强调色/控件样式
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-07-29
 * @version 1.1
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计原则：
 *  - UI 装饰色收敛为单一品牌金（取自 logo 光束），让数据的彩色成为画面主角
 *  - 背景采用深海军蓝灰（取自 logo 底色），避免纯灰的"工程感"
 *  - 交互反馈用背景深浅变化表达，弱化边框
 */
#pragma once

#include <QString>
#include <QColor>
#include <QVector>
#include <QApplication>
#include <QFont>

namespace Theme {

// ---- 背景层级（深海军蓝灰）----
inline const QString BgApp     = QStringLiteral("#16181D");  // 窗口底色
inline const QString BgPanel   = QStringLiteral("#1E2128");  // 面板/工具栏/标题栏
inline const QString BgCard    = QStringLiteral("#252932");  // 按钮/输入框/卡片
inline const QString BgHover   = QStringLiteral("#2E333D");  // hover 态
inline const QString BgPressed = QStringLiteral("#1A1D24");  // pressed 态
inline const QString Border    = QStringLiteral("#333947");  // 弱化边框

// ---- 文字 ----
inline const QString TextPrimary = QStringLiteral("#EDE8DF"); // 暖白主文字
inline const QString TextSecond  = QStringLiteral("#9AA0AB"); // 次级文字
inline const QString TextMuted   = QStringLiteral("#5C6270"); // 弱化/禁用文字

// ---- 品牌强调色（暖金，取自 logo 光束）----
inline const QString Accent       = QStringLiteral("#F0B429");
inline const QString AccentHover  = QStringLiteral("#FFC94D");
inline const QString AccentPress  = QStringLiteral("#D99E14");
inline const QString AccentOnDark = QStringLiteral("#16181D"); // 金底上的深色文字

// ---- 语义色（仅用于数据/状态，不做 UI 装饰）----
inline const QString Danger  = QStringLiteral("#E5484D");
inline const QString Info    = QStringLiteral("#3E9BD8");
inline const QString Success = QStringLiteral("#4CAF50");

// ---- 数据可视化色板（Okabe-Ito 色盲友好）----
inline const QVector<QColor> DataPalette = {
    QColor(86, 180, 233),   // sky blue
    QColor(230, 159, 0),    // orange
    QColor(0, 158, 115),    // bluish green
    QColor(213, 94, 0),     // vermillion
    QColor(204, 121, 167),  // reddish purple
    QColor(240, 228, 66),   // yellow
};

// ======================================================================
// 第二层：语义映射令牌（规范§1.2）
// 组件样式只允许引用本层语义名；换肤/浅色主题只改这里的映射表。
// 规范§1.3：语义色之外的新颜色 = 违规。
// ======================================================================
namespace Surface {
inline const QString Base    = BgApp;   // surface.base    L0 窗口底、视频黑边
inline const QString Raised  = BgPanel; // surface.raised  L1 面板/工具栏/标题栏
inline const QString Overlay = BgCard;  // surface.overlay L2 卡片/输入框/悬浮层
} // namespace Surface

namespace Interactive {
inline const QString Default = BgCard;    // interactive.default 常规按钮底
inline const QString Hover   = BgHover;   // interactive.hover
inline const QString Pressed = BgPressed; // interactive.pressed
} // namespace Interactive

namespace AccentTk {
inline const QString Solid   = Accent;       // accent.solid   主按钮底
inline const QString Outline = Accent;       // accent.outline 金描边
inline const QString Text    = Accent;       // accent.text    金色文字
inline const QString OnSolid = AccentOnDark; // text.on-accent 金底上的字
inline const QString Hover   = AccentHover;
inline const QString Pressed = AccentPress;
} // namespace AccentTk

namespace BorderTk {
inline const QString Default = Border; // border.default 唯一分隔手段
inline const QString Focus   = Accent; // border.focus   焦点环
} // namespace BorderTk

namespace Status {
inline const QString Error = Danger;   // status.error
inline const QString Info  = Theme::Info;   // status.info
inline const QString Ok    = Success;  // status.ok
} // namespace Status

namespace TextTk {
inline const QString Primary = TextPrimary;
inline const QString Second  = TextSecond;
inline const QString Muted   = TextMuted;   // 仅非关键信息（规范§12 对比度）
} // namespace TextTk

/// @brief 令牌 + 透明度派生（规范§1.3：半透明色一律从令牌派生，禁止新色值）
/// @param hex 形如 "#RRGGBB" 的令牌色；@param pct 不透明度 0-100
/// @return Qt 样式表可用的 "rgba(r,g,b,a)" 串
inline QString withAlpha(const QString &hex, int pct)
{
    const QColor c(hex);
    const int a = qBound(0, pct, 100) * 255 / 100;
    return QStringLiteral("rgba(%1,%2,%3,%4)")
        .arg(c.red()).arg(c.green()).arg(c.blue()).arg(a);
}

/// @brief 返回全局样式表（覆盖 Qt 默认控件：菜单/滚动条/进度条/滑杆/工具提示等）
inline QString globalStyleSheet()
{
    return QStringLiteral(
        // ---- 基础 ----
        "QMainWindow, QWidget { background-color: %1; color: %2; }"
        "QLabel { color: %2; background: transparent; }"
        "QToolTip { background-color: %15; color: %2;"  // 规范§2：L3=Overlay+1px Border（令牌派生半透明）
        "  border: 1px solid %16; border-radius: 8px; padding: 4px 8px; }"

        // ---- 菜单栏与菜单 ----
        "QMenuBar { background: %5; border-bottom: 1px solid %6; padding: 2px; }"
        "QMenuBar::item { padding: 4px 10px; border-radius: 4px; background: transparent; }"
        "QMenuBar::item:selected { background: %3; }"
        "QMenu { background: %13; border: 1px solid %4; border-radius: 8px; padding: 6px; }" // 规范§2：悬浮层 L3
        "QMenu::item { padding: 6px 28px 6px 14px; border-radius: 4px; color: %2; }"
        "QMenu::item:selected { background: %3; }"
        "QMenu::item:disabled { color: %7; }"
        "QMenu::separator { height: 1px; background: %4; margin: 4px 8px; }"

        // ---- 工具栏/状态栏/Dock ----
        "QToolBar { background: %5; border: none; spacing: 6px; padding: 4px 8px; }"
        "QToolBar::separator { width: 12px; background: transparent; }"
        "QStatusBar { background: %1; border-top: 1px solid %6; color: %8; }"
        "QStatusBar::item { border: none; }"
        "QDockWidget { color: %2; }"
        "QDockWidget::title { background: %5; padding: 4px; }"

        // ---- 按钮基础（无边框，背景变化表达交互）----
        // 规范§5：焦点环 2px Accent——基态预占 2px 透明边，聚焦零位移
        "QPushButton { background: %3; color: %2; border: 2px solid transparent; border-radius: 6px; padding: 2px 8px; }"
        "QPushButton:hover { background: %9; }"
        "QPushButton:pressed { background: %10; }"
        "QPushButton:disabled { background: %10; color: %7; }"
        "QPushButton:focus { border-color: %14; }"
        "QToolButton:focus { border: 2px solid %14; border-radius: 6px; }"

        // ---- 进度条 ----
        "QProgressBar { background: %3; border: none; border-radius: 6px; height: 12px;"
        "  color: %2; text-align: center; font-size: 10px; }"
        "QProgressBar::chunk { background: %11; border-radius: 6px; }"

        // ---- 滑杆 ----
        "QSlider { background: transparent; }"
        "QSlider::groove:horizontal { background: %4; height: 4px; border-radius: 2px; }"
        "QSlider::sub-page:horizontal { background: %11; border-radius: 2px; }"
        "QSlider::handle:horizontal { background: %11; width: 14px; height: 14px;"
        "  margin: -5px 0; border-radius: 7px; }"
        "QSlider::handle:horizontal:hover { background: %12; }"

        // ---- 滚动条 ----
        "QScrollBar:vertical { background: transparent; width: 10px; margin: 2px; }"
        "QScrollBar::handle:vertical { background: %4; border-radius: 4px; min-height: 30px; }"
        "QScrollBar::handle:vertical:hover { background: %8; }"
        "QScrollBar:horizontal { background: transparent; height: 10px; margin: 2px; }"
        "QScrollBar::handle:horizontal { background: %4; border-radius: 4px; min-width: 30px; }"
        "QScrollBar::handle:horizontal:hover { background: %8; }"
        "QScrollBar::add-line, QScrollBar::sub-line { width: 0; height: 0; }"
        "QScrollBar::add-page, QScrollBar::sub-page { background: transparent; }"

        // ---- 列表 ----
        // 规范§5：预占 2px 透明边，聚焦零位移；outline 交由焦点环接管
        "QListWidget { background: %1; color: %2; border: 2px solid transparent; outline: none; }"
        "QListWidget:focus { border-color: %14; }"
        "QListWidget::item { padding: 4px 8px; border-radius: 4px; }"
        "QListWidget::item:selected { background: %3; color: %2; }"
        "QListWidget::item:hover { background: %9; }"

        // ---- 输入框 ----
        "QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QComboBox {"
        "  background: %3; color: %2; border: 1px solid %4; border-radius: 6px; padding: 4px 8px; }"
        // 规范§5：焦点环 2px Accent（border 1→2px，padding 补偿 1px 零位移）
        "QLineEdit:focus, QTextEdit:focus, QPlainTextEdit:focus, QSpinBox:focus, QComboBox:focus {"
        "  border: 2px solid %14; padding: 3px 7px; }"

        // ---- 分隔器 ----
        "QSplitter::handle { background: transparent; }"
        "QSplitter::handle:hover { background: %17; }"

        // ---- 对话框（规范§2/§7.7：L3 = Overlay 底 + 1px Border）----
        "QDialog { background: %13; border: 1px solid %4; }"

        // ---- v1.12.9 面板观感统一（用户反馈④）：分组框/树表/表格头 ----
        "QGroupBox { background: transparent; border: 1px solid %4; border-radius: 8px;"
        "  margin-top: 14px; padding-top: 8px; }"
        "QGroupBox::title { subcontrol-origin: margin; subcontrol-position: top left;"
        "  left: 10px; top: 2px; padding: 0 6px; color: %8; }"
        "QTreeWidget, QTableWidget { background: %1; color: %2; border: 2px solid %4;"
        "  border-radius: 6px; outline: none; }"
        "QTreeWidget:focus, QTableWidget:focus { border-color: %14; }"  // 规范§5 焦点环（2px 预占零位移）
        "QTreeWidget::item, QTableWidget::item { padding: 2px 4px; }"
        "QTreeWidget::item:selected, QTableWidget::item:selected { background: %3; color: %2; }"
        "QTreeWidget::item:hover, QTableWidget::item:hover { background: %9; }"
        "QHeaderView::section { background: %5; color: %8; border: none;"
        "  border-bottom: 1px solid %4; padding: 4px 6px; }"
        "QTabWidget::pane { border: 1px solid %4; border-radius: 6px; top: -1px; }"
        "QTabBar::tab { background: %5; color: %8; padding: 4px 12px;"
        "  border: 2px solid transparent;"   // 规范§5：预占焦点环位
        "  border-top-left-radius: 6px; border-top-right-radius: 6px; }"
        "QTabBar::tab:focus { border-color: %14; }"
        "QTabBar::tab:selected { background: %3; color: %2; }"
        "QTabBar::tab:hover:!selected { background: %9; }"
        // 规范§5：滑杆手柄焦点环
        "QSlider::handle:horizontal:focus { border: 2px solid %14; }"
    ).arg(Surface::Base)       // %1
     .arg(TextTk::Primary)     // %2
     .arg(Interactive::Default)// %3
     .arg(BorderTk::Default)   // %4
     .arg(Surface::Raised)     // %5
     .arg(Surface::Overlay)    // %6  (menuBar/statusbar 分隔线用卡片色，比 Border 更柔)
     .arg(TextTk::Muted)       // %7
     .arg(TextTk::Second)      // %8
     .arg(Interactive::Hover)  // %9
     .arg(Interactive::Pressed)// %10
     .arg(AccentTk::Solid)     // %11
     .arg(AccentTk::Hover)     // %12
     .arg(Surface::Overlay)    // %13 (L3 悬浮层底：菜单/对话框)
     .arg(BorderTk::Focus)     // %14 (焦点环)
     .arg(withAlpha(Surface::Overlay, 92))  // %15 ToolTip 底（令牌派生）
     .arg(withAlpha(BorderTk::Default, 85)) // %16 ToolTip 边（令牌派生）
     .arg(withAlpha(AccentTk::Solid, 24));  // %17 分隔器悬停（令牌派生）
}

/// @brief 将全局主题应用到 QApplication（含 Fusion 基础风格，保证跨平台一致）
inline void apply(QApplication &app)
{
    app.setStyle(QStringLiteral("Fusion"));
    app.setStyleSheet(globalStyleSheet());
    // v1.12.9（用户反馈④）：全局默认字体统一——此前未设，未显式指定的
    // 控件回落 Qt 平台默认（Windows 为 MS Shell Dlg），与各界面元素的
    // Segoe UI/Microsoft YaHei 声明混搭显乱。setFamilies 逐级回退。
    QFont baseFont;
    baseFont.setFamilies({QStringLiteral("Microsoft YaHei UI"),
                          QStringLiteral("Microsoft YaHei"),
                          QStringLiteral("Segoe UI"),
                          QStringLiteral("PingFang SC"),
                          QStringLiteral("sans-serif")});
    baseFont.setPointSize(9);
    app.setFont(baseFont);
}

} // namespace Theme
