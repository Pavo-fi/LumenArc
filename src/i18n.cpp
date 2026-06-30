/**
 * @file i18n.cpp
 * @brief 国际化实现：语言检测/切换/持久化/跨平台字体
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
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

QImage applyBrightnessContrast(const QImage &src, int brightness, int contrast)
{
    if (src.isNull())
        return src;
    if (brightness == 0 && contrast == 0)
        return src.convertToFormat(QImage::Format_ARGB32);

    QImage result = src.convertToFormat(QImage::Format_ARGB32);
    double cf = (259.0 * (contrast + 255)) / (255.0 * (259 - contrast));

    for (int y = 0; y < result.height(); ++y) {
        QRgb *line = reinterpret_cast<QRgb*>(result.scanLine(y));
        for (int x = 0; x < result.width(); ++x) {
            QRgb px = line[x];
            int r = qBound(0, int(cf * (qRed(px) + brightness * 2 - 128) + 128), 255);
            int g = qBound(0, int(cf * (qGreen(px) + brightness * 2 - 128) + 128), 255);
            int b = qBound(0, int(cf * (qBlue(px) + brightness * 2 - 128) + 128), 255);
            line[x] = qRgba(r, g, b, qAlpha(px));
        }
    }
    return result;
}
