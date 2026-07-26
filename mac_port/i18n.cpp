/**
 * @file i18n.cpp
 * @brief 国际化实现：语言检测/切换/持久化/跨平台字体
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "i18n.h"
#include <QSettings>
#include <QProcess>
#include <QCoreApplication>

AppLanguage g_language = LangChinese;

void loadLanguage() {
    QSettings s("LumenArc", "Language");
    g_language = s.value("language", 0).toInt() == 1 ? LangEnglish : LangChinese;
}

void saveLanguage(AppLanguage lang) {
    QSettings s("LumenArc", "Language");
    s.setValue("language", static_cast<int>(lang));
    g_language = lang;
}

void restartApp() {
#ifdef Q_OS_MACOS
    QProcess::startDetached("open", {"-a", "LumenArc"});
#else
    QProcess::startDetached(QCoreApplication::applicationFilePath());
#endif
    qApp->quit();
}

QFont fontSans(int size, QFont::Weight weight) {
#ifdef Q_OS_MACOS
    QFont f("PingFang SC", size, weight);
#else
    QFont f("Microsoft YaHei", size, weight);
#endif
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}

QFont fontMono(int size, QFont::Weight weight) {
#ifdef Q_OS_MACOS
    QFont f("Menlo", size, weight);
#else
    QFont f("Consolas", size, weight);
#endif
    f.setStyleStrategy(QFont::PreferAntialias);
    return f;
}
