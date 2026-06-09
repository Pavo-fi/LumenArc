/**
 * @file timeline_model.cpp
 * @brief 时间序列模型实现 + .vla v3 文件读写
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.2
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "timeline_model.h"
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QFile>
#include <QDateTime>
#include <QBuffer>

TimelineModel::TimelineModel(QObject *parent)
    : QObject(parent)
{
}

void TimelineModel::setData(QVector<qint64> timestamps, QVector<QVector<qreal>> values)
{
    QWriteLocker lock(&m_lock);
    m_snapshot.timestamps = std::move(timestamps);
    m_snapshot.values = std::move(values);
    lock.unlock();
    emit dataReplaced();
}

void TimelineModel::clearData()
{
    QWriteLocker lock(&m_lock);
    m_snapshot.timestamps.clear();
    m_snapshot.values.clear();
    lock.unlock();
    emit dataCleared();
}

AnalysisSnapshot TimelineModel::snapshot() const
{
    QReadLocker lock(&m_lock);
    return m_snapshot;
}

bool TimelineModel::saveToFile(const QString &filePath,
                                const QVector<QRect> &regions,
                                qint64 timeOffsetMs,
                                const QRect &magnifier,
                                const QVector<ChartLabel> &labels,
                                const QRect &pinned,
                                const SnapshotFusionData &snapshotFusion) const
{
    QReadLocker lock(&m_lock);

    if (m_snapshot.isEmpty())
        return false;

    QJsonObject root;
    root["version"] = 3;
    root["analyzed_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    root["time_offset"] = static_cast<double>(timeOffsetMs);

    // ROI rectangles
    QJsonArray regionsArray;
    for (const QRect &rc : regions) {
        QJsonObject obj;
        obj["x"] = rc.x();
        obj["y"] = rc.y();
        obj["w"] = rc.width();
        obj["h"] = rc.height();
        regionsArray.append(obj);
    }
    root["regions"] = regionsArray;

    // Magnifier (v3)
    if (!magnifier.isEmpty()) {
        QJsonObject magObj;
        magObj["x"] = magnifier.x();
        magObj["y"] = magnifier.y();
        magObj["w"] = magnifier.width();
        magObj["h"] = magnifier.height();
        root["magnifier"] = magObj;
    }

    // Chart labels (v3)
    if (!labels.isEmpty()) {
        QJsonArray labelsArray;
        for (const auto &label : labels) {
            QJsonObject lObj;
            lObj["time_ms"] = static_cast<double>(label.timeMs);
            lObj["text"] = label.text;
            lObj["color"] = label.color.name(QColor::HexArgb);
            labelsArray.append(lObj);
        }
        root["labels"] = labelsArray;
    }

    // Pinned (v3)
    if (!pinned.isEmpty()) {
        QJsonObject pinObj;
        pinObj["x"] = pinned.x();
        pinObj["y"] = pinned.y();
        pinObj["w"] = pinned.width();
        pinObj["h"] = pinned.height();
        root["pinned"] = pinObj;
    }

    // Snapshot fusion (v3)
    if (snapshotFusion.isValid()) {
        QJsonObject snapObj;
        snapObj["brightness"] = snapshotFusion.brightness;
        snapObj["contrast"] = snapshotFusion.contrast;
        snapObj["opacity"] = snapshotFusion.opacity;
        if (!snapshotFusion.imageData.isNull()) {
            QByteArray ba;
            QBuffer buf(&ba);
            buf.open(QIODevice::WriteOnly);
            snapshotFusion.imageData.save(&buf, "PNG");
            snapObj["image"] = QString::fromLatin1(ba.toBase64());
        }
        root["snapshot_fusion"] = snapObj;
    }

    // Timestamps as JSON array
    QJsonArray tsArray;
    for (qint64 ts : m_snapshot.timestamps)
        tsArray.append(static_cast<double>(ts));
    root["timestamps"] = tsArray;

    // Luminances as 2D JSON array
    QJsonArray lumArray;
    for (const auto &region : m_snapshot.values) {
        QJsonArray regionArray;
        for (qreal v : region)
            regionArray.append(v);
        lumArray.append(regionArray);
    }
    root["luminances"] = lumArray;
    root["point_count"] = m_snapshot.pointCount();
    root["region_count"] = m_snapshot.regionCount();

    lock.unlock();

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text))
        return false;

    QJsonDocument doc(root);
    file.write(doc.toJson(QJsonDocument::Compact));
    file.close();
    return true;
}

bool TimelineModel::loadFromFile(const QString &filePath,
                                  QVector<QRect> *regions,
                                  qint64 *timeOffsetMs,
                                  QRect *magnifier,
                                  QVector<ChartLabel> *labels,
                                  QRect *pinned,
                                  SnapshotFusionData *snapshotFusion)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;

    QByteArray data = file.readAll();
    file.close();

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
    if (parseError.error != QJsonParseError::NoError)
        return false;

    if (!doc.isObject())
        return false;

    QJsonObject root = doc.object();

    int version = root["version"].toInt();
    if (version < 1)
        return false;

    // v2+: ROI rectangles
    if (version >= 2 && regions) {
        regions->clear();
        QJsonArray regionsArray = root["regions"].toArray();
        for (const auto &v : regionsArray) {
            QJsonObject obj = v.toObject();
            regions->append(QRect(obj["x"].toInt(), obj["y"].toInt(),
                                   obj["w"].toInt(), obj["h"].toInt()));
        }
    }

    // v2+: time offset
    if (version >= 2 && timeOffsetMs) {
        *timeOffsetMs = static_cast<qint64>(root["time_offset"].toDouble());
    }

    // v3+: magnifier
    if (version >= 3 && magnifier && root.contains("magnifier")) {
        QJsonObject magObj = root["magnifier"].toObject();
        *magnifier = QRect(magObj["x"].toInt(), magObj["y"].toInt(),
                           magObj["w"].toInt(), magObj["h"].toInt());
    }

    // v3+: chart labels
    if (version >= 3 && labels && root.contains("labels")) {
        labels->clear();
        QJsonArray labelsArray = root["labels"].toArray();
        for (const auto &v : labelsArray) {
            QJsonObject lObj = v.toObject();
            ChartLabel label;
            label.timeMs = static_cast<qint64>(lObj["time_ms"].toDouble());
            label.text = lObj["text"].toString();
            label.color = QColor(lObj["color"].toString());
            labels->append(label);
        }
    }

    // v3+: pinned
    if (version >= 3 && pinned && root.contains("pinned")) {
        QJsonObject pinObj = root["pinned"].toObject();
        *pinned = QRect(pinObj["x"].toInt(), pinObj["y"].toInt(),
                        pinObj["w"].toInt(), pinObj["h"].toInt());
    }

    // v3+: snapshot fusion
    if (version >= 3 && snapshotFusion && root.contains("snapshot_fusion")) {
        QJsonObject snapObj = root["snapshot_fusion"].toObject();
        snapshotFusion->brightness = snapObj["brightness"].toInt();
        snapshotFusion->contrast = snapObj["contrast"].toInt();
        snapshotFusion->opacity = snapObj["opacity"].toInt();
        if (snapObj.contains("image")) {
            QByteArray ba = QByteArray::fromBase64(snapObj["image"].toString().toLatin1());
            QBuffer buf(&ba);
            buf.open(QIODevice::ReadOnly);
            snapshotFusion->imageData.load(&buf, "PNG");
        }
    }

    // Parse timestamps
    QJsonArray tsArray = root["timestamps"].toArray();
    QVector<qint64> timestamps;
    timestamps.reserve(tsArray.size());
    for (const auto &v : tsArray)
        timestamps.append(static_cast<qint64>(v.toDouble()));

    // Parse luminances
    QJsonArray lumArray = root["luminances"].toArray();
    QVector<QVector<qreal>> values;
    values.reserve(lumArray.size());
    for (const auto &regionVal : lumArray) {
        QJsonArray regionArray = regionVal.toArray();
        QVector<qreal> region;
        region.reserve(regionArray.size());
        for (const auto &v : regionArray)
            region.append(v.toDouble());
        values.append(std::move(region));
    }

    // Set data (emits dataReplaced)
    setData(std::move(timestamps), std::move(values));
    return true;
}
