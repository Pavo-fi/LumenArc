/**
 * @file build_stamp.h
 * @brief 构建时间戳（标题栏常驻）：v1.17.0 P-79 起独立成头文件
 *
 * 用途（2026-08-13 引入）：杜绝“用户在跑旧构建”无法辨识（多次出现修复已
 * 提交但用户测的是旧 exe 的扯皮）。__DATE__/__TIME__ 为包含它的编译单元的
 * 编译时刻；主窗口构造体已迁至 mainwindow_ui.cpp（P-79 多 TU 拆分），
 * 故标题戳 = mainwindow_ui.cpp 的编译时刻（任何主窗口代码改动重编即刷新）。
 */
#pragma once

#include <QString>

inline QString buildStamp()
{
    return QStringLiteral(" (build %1)")
        .arg(QStringLiteral(__DATE__) + QStringLiteral(" ")
             + QStringLiteral(__TIME__).left(5));
}