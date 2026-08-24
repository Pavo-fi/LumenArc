/**
 * @file site_map.h
 * @brief P-74 监控点位图：数据模型 + 案内持久化（sitemap.json）
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-23
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 拍板（2026-08-23）：不画比例尺 / 扇形表拍摄方向且扇面可调 / 一案一张。
 * 坐标归一化（相对底图）——换图/换分辨率不失位；机位删除→孤儿标红不自动删。
 */
#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>
#include <QVector>
#include <QFile>
#include <QSaveFile>

struct SiteMapPoint {
    QString laneRef;        ///< 案内视频编号 V###/P###
    QString label;          ///< 冗余机位标签（改名后图上不丢字）
    double x = 0.5;         ///< 归一化坐标（相对底图，0~1）
    double y = 0.5;
    double headingDeg = 0;  ///< 朝向（0=右，屏幕顺时针为正）
    double spreadDeg = 60;  ///< 扇面张角（10~180）
    double radiusPct = 15;  ///< 扇面半径（相对底图短边 %）
    double labelScale = 1.0; ///< 标注字号倍率（0.5~3.0；v1.15.3 拍板：大小可调）
    bool   orphan = false;  ///< 载入时计算：机位已不在案（不入 JSON）

    QJsonObject toJson() const
    {
        QJsonObject o{
            {QStringLiteral("laneRef"), laneRef},
            {QStringLiteral("label"), label},
            {QStringLiteral("x"), x}, {QStringLiteral("y"), y},
            {QStringLiteral("headingDeg"), headingDeg},
            {QStringLiteral("spreadDeg"), spreadDeg},
            {QStringLiteral("radiusPct"), radiusPct},
        };
        if (labelScale != 1.0)   // F3 只加不改：默认不写字段
            o.insert(QStringLiteral("labelScale"), labelScale);
        return o;
    }
    static SiteMapPoint fromJson(const QJsonObject &o)
    {
        SiteMapPoint p;
        p.laneRef = o.value(QStringLiteral("laneRef")).toString();
        p.label = o.value(QStringLiteral("label")).toString();
        p.x = qBound(0.0, o.value(QStringLiteral("x")).toDouble(0.5), 1.0);
        p.y = qBound(0.0, o.value(QStringLiteral("y")).toDouble(0.5), 1.0);
        p.headingDeg = o.value(QStringLiteral("headingDeg")).toDouble(0.0);
        p.spreadDeg = qBound(10.0, o.value(QStringLiteral("spreadDeg")).toDouble(60.0), 180.0);
        p.radiusPct = qBound(3.0, o.value(QStringLiteral("radiusPct")).toDouble(15.0), 50.0);
        p.labelScale = qBound(0.5, o.value(QStringLiteral("labelScale")).toDouble(1.0), 3.0);
        return p;
    }
};

struct SiteMapData {
    int version = 1;
    QString baseImageRel;   ///< 相对案件根（reports/assets/sitemap_base.png）
    QVector<SiteMapPoint> points;

    QJsonObject toJson() const
    {
        QJsonArray arr;
        for (const auto &p : points)
            arr.append(p.toJson());
        return QJsonObject{
            {QStringLiteral("version"), version},
            {QStringLiteral("baseImage"), baseImageRel},
            {QStringLiteral("points"), arr},
        };
    }
    static SiteMapData fromJson(const QJsonObject &o)
    {
        SiteMapData d;
        d.version = o.value(QStringLiteral("version")).toInt(1);
        d.baseImageRel = o.value(QStringLiteral("baseImage")).toString();
        for (const auto &v : o.value(QStringLiteral("points")).toArray())
            d.points.append(SiteMapPoint::fromJson(v.toObject()));
        return d;
    }

    static QString filePathFor(const QString &caseDir)
    { return caseDir + QStringLiteral("/sitemap.json"); }

    bool save(const QString &caseDir) const
    {
        QSaveFile f(filePathFor(caseDir));   // 原子写（C1）
        if (!f.open(QIODevice::WriteOnly))
            return false;
        f.write(QJsonDocument(toJson()).toJson(QJsonDocument::Indented));
        return f.commit();
    }
    static SiteMapData load(const QString &caseDir)
    {
        QFile f(filePathFor(caseDir));
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return fromJson(QJsonDocument::fromJson(f.readAll()).object());
    }
};
