/**
 * @file tool_paths.cpp
 * @brief 外部工具路径探测实现（P-25：自 python_analysis_engine 原样迁移）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-17
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "tool_paths.h"
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QProcess>
#include <QSettings>
#include <QStringList>
#include <algorithm>

QString ToolPaths::detectPythonPath()
{
    QString appDir = QCoreApplication::applicationDirPath();

#ifdef Q_OS_WIN
    // 0. Bundled Python (Windows)
    QString bundledPy = appDir + "/python/python.exe";
    if (QFile::exists(bundledPy))
        return QDir::toNativeSeparators(bundledPy);
#endif

#ifdef Q_OS_MACOS
    // 0. Bundled Python (inside .app bundle)
    QString bundledPy = appDir + "/python/bin/python3";
    if (QFile::exists(bundledPy))
        return bundledPy;
#endif

    // 1. Environment variable (cross-platform)
    QString env = qEnvironmentVariable("PYTHON_PATH");
    if (!env.isEmpty() && QFile::exists(env))
        return env;

#ifdef Q_OS_WIN
    // 2. Registry-based detection via QSettings
    QStringList registryKeys = {
        "HKEY_CURRENT_USER\\Software\\Python\\PythonCore",
        "HKEY_LOCAL_MACHINE\\SOFTWARE\\Python\\PythonCore"
    };
    for (const QString &regKey : registryKeys) {
        QSettings settings(regKey, QSettings::NativeFormat);
        QStringList versions = settings.childGroups();
        std::sort(versions.begin(), versions.end(), std::greater<QString>());
        for (const QString &ver : versions) {
            settings.beginGroup(ver);
            QString installPath = settings.value("InstallPath").toString();
            settings.endGroup();
            if (!installPath.isEmpty()) {
                QString pyPath = installPath + "/python.exe";
                if (QFile::exists(pyPath))
                    return QDir::toNativeSeparators(pyPath);
                pyPath = installPath + "/python3.exe";
                if (QFile::exists(pyPath))
                    return QDir::toNativeSeparators(pyPath);
            }
        }
    }

    // 3. Common Windows install paths
    QStringList winCandidates = {
        "C:/Python313/python.exe",
        "C:/Python312/python.exe",
        "C:/Python311/python.exe",
        "C:/Python310/python.exe",
        "C:/Program Files/Python313/python.exe",
        "C:/Program Files/Python312/python.exe",
        "C:/Program Files/Python311/python.exe",
        "C:/Program Files/Python310/python.exe",
        "python.exe"
    };
    for (const QString &c : winCandidates) {
        if (QFile::exists(c))
            return c;
    }

    // 4. Windows py.exe launcher
    QProcess probe;
    probe.start("py", {"-3", "-c", "import sys; print(sys.executable)"});
    if (probe.waitForFinished(3000) && probe.exitCode() == 0) {
        QString pyPath = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
        if (!pyPath.isEmpty() && QFile::exists(pyPath))
            return pyPath;
    }
#endif

#ifdef Q_OS_MACOS
    // 2. which python3
    QProcess probe;
    probe.start("which", {"python3"});
    if (probe.waitForFinished(3000) && probe.exitCode() == 0) {
        QString pyPath = QString::fromUtf8(probe.readAllStandardOutput()).trimmed();
        if (!pyPath.isEmpty() && QFile::exists(pyPath))
            return pyPath;
    }

    // 3. Common macOS install paths
    QStringList macCandidates = {
        "/opt/homebrew/bin/python3",
        "/usr/local/bin/python3",
        "/usr/bin/python3"
    };
    for (const QString &c : macCandidates) {
        if (QFile::exists(c))
            return c;
    }
#endif

    return QString();
}

QString ToolPaths::findFfmpegPath()
{
    QString appDir = QCoreApplication::applicationDirPath();
    // Bundled locations first (Windows .exe / macOS & Linux 无扩展名)
    const QStringList candidates = {
        appDir + "/ffmpeg/ffmpeg.exe",
        appDir + "/ffmpeg.exe",
        appDir + "/ffmpeg/ffmpeg",
        appDir + "/ffmpeg",
    };
    for (const QString &p : candidates)
        if (QFile::exists(p))
            return p;
    // System PATH
    return "ffmpeg";
}
