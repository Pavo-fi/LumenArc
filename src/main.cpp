/**
 * @file main.cpp
 * @brief 程序入口，启动画面创建和初始化流程
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include <QApplication>
#include <QSplashScreen>
#include <QIcon>
#include <QDir>
#include <QFont>
#include <QScreen>
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

// Ivory white color constant
static const QColor IVORY(255, 255, 240);

static QPixmap createSplashPixmap(const QString &statusText, int progress = 30)
{
    const int W = 780;
    const int H = 450;
    QPixmap pixmap(W, H);
    pixmap.fill(Qt::black);

    QPainter p(&pixmap);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // --- Background: lightchaser.jpg (try embedded resource first, then filesystem)
    QStringList searchPaths = {
        ":/lightchaser.jpg",
        QCoreApplication::applicationDirPath() + "/lightchaser.jpg",
        QDir::currentPath() + "/lightchaser.jpg"
    };
    bool bgLoaded = false;
    for (const QString &path : searchPaths) {
        QImage bg(path);
        if (!bg.isNull()) {
            QImage scaled = bg.scaled(W, H, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            int sx = (scaled.width() - W) / 2;
            int sy = (scaled.height() - H) / 2;
            p.drawImage(0, 0, scaled, sx, sy, W, H);
            bgLoaded = true;
            break;
        }
    }
    if (!bgLoaded) {
        QLinearGradient bgGrad(0, 0, 0, H);
        bgGrad.setColorAt(0.0, QColor(20, 25, 45));
        bgGrad.setColorAt(1.0, QColor(10, 15, 30));
        p.fillRect(0, 0, W, H, bgGrad);
    }

    // --- Semi-transparent dark gradient overlay on the left side ---
    QLinearGradient overlayGrad(0, 0, W * 0.55, 0);
    overlayGrad.setColorAt(0.0, QColor(0, 0, 0, 180));
    overlayGrad.setColorAt(0.7, QColor(0, 0, 0, 120));
    overlayGrad.setColorAt(1.0, QColor(0, 0, 0, 0));
    p.fillRect(0, 0, W, H, overlayGrad);

    // --- Title: LUMEN ARC ---
    QFont titleFont = fontSans(32, QFont::Bold);
    p.setFont(titleFont);
    p.setPen(IVORY);
    p.drawText(QRect(60, 60, W - 120, 50), Qt::AlignLeft | Qt::AlignVCenter, "LUMEN ARC");

    // --- Subtitle: Chinese name ---
    QFont nameFont = fontSans(16, QFont::Bold);
    p.setFont(nameFont);
    p.setPen(IVORY);
    p.drawText(QRect(60, 120, W - 120, 35), Qt::AlignLeft | Qt::AlignVCenter,
               QString::fromUtf8("\xe8\xbf\xbd\xe5\x85\x89\xe8\x80\x85"));

    // --- Subtitle: System description ---
    QFont descFont = fontSans(11);
    p.setFont(descFont);
    p.setPen(QColor(255, 255, 240, 200));
    p.drawText(QRect(60, 160, W - 120, 28), Qt::AlignLeft | Qt::AlignVCenter,
               QString::fromUtf8("\xe7\x81\xab\xe7\x81\xbe\xe8\xb0\x83\xe6\x9f\xa5\xe8\xa7\x86\xe9\xa2\x91\xe5\x88\x86\xe6\x9e\x90\xe5\xb7\xa5\xe5\x85\xb7"));

    // --- Version tag ---
    QFont verFont = fontMono(10);
    p.setFont(verFont);
    p.setPen(QColor(255, 255, 240, 150));
    p.drawText(QRect(60, 216, W - 120, 22), Qt::AlignLeft | Qt::AlignVCenter, "v1.2.0");

    // --- Progress bar background ---
    int barX = 60;
    int barY = 348;
    int barW = 300;
    int barH = 4;
    p.setPen(Qt::NoPen);
    p.setBrush(QColor(255, 255, 255, 30));
    p.drawRoundedRect(barX, barY, barW, barH, 2, 2);

    // --- Progress bar fill (dynamic) --- 品牌金渐变
    int progressWidth = qBound(1, barW * qMin(100, qMax(0, progress)) / 100, barW);
    QLinearGradient barGrad(barX, barY, barX + progressWidth, barY);
    barGrad.setColorAt(0.0, QColor(217, 158, 20));   // AccentPress
    barGrad.setColorAt(1.0, QColor(240, 180, 41));   // Accent
    p.setBrush(barGrad);
    p.drawRoundedRect(barX, barY, progressWidth, barH, 2, 2);

    // --- Status text (below progress bar) ---
    QFont statusFont = fontSans(10);
    p.setFont(statusFont);
    p.setPen(QColor(255, 255, 240, 170));
    p.drawText(QRect(60, 365, barW, 28), Qt::AlignLeft | Qt::AlignVCenter, statusText);

    // --- Copyright notice (below subtitle, above version) ---
    QString copyrightText = QString::fromUtf8("\xe5\xb9\xbf\xe4\xb8\x9c\xe7\x9c\x81\xe7\x81\xab\xe8\xb0\x83\xe6\x8a\x80\xe6\x9c\xaf\xe4\xb8\xad\xe5\xbf\x83\xe5\x87\xba\xe5\x93\x81");
    QFont copyFont = fontSans(11);
    p.setFont(copyFont);
    p.setPen(QColor(255, 255, 240, 200));
    p.drawText(QRect(60, 188, W - 120, 28), Qt::AlignLeft | Qt::AlignVCenter, copyrightText);

    p.end();
    return pixmap;
}

int main(int argc, char *argv[])
{
    QApplication app(argc, argv);
    app.setApplicationName("LumenArc");
    app.setOrganizationName("LumenArc");

    // 全局深色主题（Design Tokens + Qt 默认控件全覆盖）
    Theme::apply(app);

    QString iconPath = QDir(app.applicationDirPath()).filePath("app.ico");
    app.setWindowIcon(QIcon(iconPath));

    // Custom splash screen
    QSplashScreen splash(createSplashPixmap("Loading...", 15));

    if (QScreen *screen = app.primaryScreen()) {
        QRect screenGeometry = screen->availableGeometry();
        splash.move((screenGeometry.width() - splash.width()) / 2,
                    (screenGeometry.height() - splash.height()) / 2);
    }
    splash.show();
    app.processEvents();

    splash.setPixmap(createSplashPixmap("Initializing video engine...", 35));
    app.processEvents();

    MainWindow window;

    splash.setPixmap(createSplashPixmap("Loading interface...", 85));
    app.processEvents();

    splash.setPixmap(createSplashPixmap("Ready.", 100));
    app.processEvents();

    splash.finish(&window);
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
