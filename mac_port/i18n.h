/**
 * @file i18n.h
 * @brief 国际化支持，提供中英文切换和语言持久化
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */

#pragma once

#include <QString>
#include <QFont>
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
