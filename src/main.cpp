/**
 * @file main.cpp
 * @brief 程序入口：创建主窗口并进入事件循环（2026-08 移除启动画面：
 *        人工反馈「先于主界面的弹窗」即 QSplashScreen）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include <QApplication>
#include <QIcon>
#include <QDir>
#include <QFont>
#include <QPainter>
#include <QLinearGradient>
#include <QImage>
#include <QLibrary>
#ifdef Q_OS_WIN
#include <qt_windows.h>
#endif
#include "mainwindow.h"
#include "i18n.h"
#include "theme.h"

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("LumenArc");
    app.setOrganizationName("LumenArc");

    // 全局深色主题（Design Tokens + Qt 默认控件全覆盖）
    Theme::apply(app);

    QString iconPath = QDir(app.applicationDirPath()).filePath("app.ico");
    app.setWindowIcon(QIcon(iconPath));

    MainWindow window;   // 直接构造主窗口（无启动画面）

    window.showMaximized();


#ifdef Q_OS_WIN
    // Windows 深色系统标题栏（DWMWA_USE_IMMERSIVE_DARK_MODE）
    {
        typedef int (WINAPI *DwmSetAttrFn)(void *, unsigned long, const void *, unsigned long);
        if (auto fn = reinterpret_cast<DwmSetAttrFn>(
                QLibrary::resolve(QStringLiteral("dwmapi"), "DwmSetWindowAttribute"))) {
            int dark = 1;
            fn(reinterpret_cast<void *>(window.winId()), 20, &dark, sizeof(dark));
        }
    }
#endif

    return app.exec();
}
