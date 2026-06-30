/**
 * @file i18n.h
 * @brief 国际化支持，提供中英文切换和语言持久化
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include <QString>
#include <QFont>
#include <QImage>
#include <QCoreApplication>

enum AppLanguage { LangChinese, LangEnglish };
extern AppLanguage g_language;

inline QString lang(const QString &zh, const QString &en) {
    return g_language == LangChinese ? zh : en;
}

/// 跨平台无衬线字体（中文/英文自适应）
QFont fontSans(int size, QFont::Weight weight = QFont::Normal);

/// 跨平台等宽字体
QFont fontMono(int size, QFont::Weight weight = QFont::Normal);

void loadLanguage();
void saveLanguage(AppLanguage lang);
void restartApp();

/// @brief 对图像应用亮度/对比度调整（统一公式）
/// @param src 源图像（ARGB32 格式）
/// @param brightness 亮度偏移（-50~50，0=无变化）
/// @param contrast 对比度（-50~50，0=无变化）
/// @return 调整后的图像副本
QImage applyBrightnessContrast(const QImage &src, int brightness, int contrast);
