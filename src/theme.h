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

/// @brief 返回全局样式表（覆盖 Qt 默认控件：菜单/滚动条/进度条/滑杆/工具提示等）
inline QString globalStyleSheet()
{
    return QStringLiteral(
        // ---- 基础 ----
        "QMainWindow, QWidget { background-color: %1; color: %2; }"
        "QLabel { color: %2; background: transparent; }"
        "QToolTip { background-color: rgba(37, 41, 50, 0.92); color: %2;"
        "  border: 1px solid rgba(51, 57, 71, 0.85); border-radius: 6px; padding: 4px 8px; }"

        // ---- 菜单栏与菜单 ----
        "QMenuBar { background: %5; border-bottom: 1px solid %6; padding: 2px; }"
        "QMenuBar::item { padding: 4px 10px; border-radius: 4px; background: transparent; }"
        "QMenuBar::item:selected { background: %3; }"
        "QMenu { background: %5; border: 1px solid %4; border-radius: 8px; padding: 6px; }"
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
        "QPushButton { background: %3; color: %2; border: none; border-radius: 6px; padding: 4px 10px; }"
        "QPushButton:hover { background: %9; }"
        "QPushButton:pressed { background: %10; }"
        "QPushButton:disabled { background: %10; color: %7; }"

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
        "QListWidget { background: %1; color: %2; border: none; outline: none; }"
        "QListWidget::item { padding: 4px 8px; border-radius: 4px; }"
        "QListWidget::item:selected { background: %3; color: %2; }"
        "QListWidget::item:hover { background: %9; }"

        // ---- 输入框 ----
        "QLineEdit, QTextEdit, QPlainTextEdit, QSpinBox, QComboBox {"
        "  background: %3; color: %2; border: 1px solid %4; border-radius: 6px; padding: 4px 8px; }"
        "QLineEdit:focus, QTextEdit:focus, QSpinBox:focus, QComboBox:focus { border-color: %11; }"

        // ---- 分隔器 ----
        "QSplitter::handle { background: transparent; }"
        "QSplitter::handle:hover { background: rgba(240, 180, 41, 60); }"

        // ---- 对话框 ----
        "QDialog { background: %5; }"
    ).arg(BgApp)      // %1
     .arg(TextPrimary)// %2
     .arg(BgCard)     // %3
     .arg(Border)     // %4
     .arg(BgPanel)    // %5
     .arg(BgCard)     // %6  (menuBar/statusbar 分隔线用卡片色，比 Border 更柔)
     .arg(TextMuted)  // %7
     .arg(TextSecond) // %8
     .arg(BgHover)    // %9
     .arg(BgPressed)  // %10
     .arg(Accent)     // %11
     .arg(AccentHover); // %12
}

/// @brief 将全局主题应用到 QApplication（含 Fusion 基础风格，保证跨平台一致）
inline void apply(QApplication &app)
{
    app.setStyle(QStringLiteral("Fusion"));
    app.setStyleSheet(globalStyleSheet());
}

} // namespace Theme
