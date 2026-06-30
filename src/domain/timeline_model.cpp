/**
 * @file timeline_model.cpp
 * @brief 时间序列模型实现 + .vla v3 文件读写
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-05-31
 * @version 0.3
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

void TimelineModel::setData(QVector<qint64> timestamps, QVector<QVector<qreal>> values,
                             const AudioData &audio)
{
    QWriteLocker lock(&m_lock);
    m_snapshot.timestamps = std::move(timestamps);
    m_snapshot.values = std::move(values);
    m_snapshot.dataEntries.clear();
    m_snapshot.audio = audio;
    lock.unlock();
    emit dataReplaced();
}

void TimelineModel::setData(QVector<qint64> timestamps, QVector<QVector<qreal>> values,
                             QVector<DataEntry> dataEntries, const AudioData &audio)
{
    QWriteLocker lock(&m_lock);
    m_snapshot.timestamps = std::move(timestamps);
    m_snapshot.values = std::move(values);
    m_snapshot.dataEntries = std::move(dataEntries);
    m_snapshot.audio = audio;
    lock.unlock();
    emit dataReplaced();
}

void TimelineModel::clearData()
{
    QWriteLocker lock(&m_lock);
    m_snapshot.timestamps.clear();
    m_snapshot.values.clear();
    m_snapshot.dataEntries.clear();
    m_snapshot.audio = AudioData();
    lock.unlock();
    emit dataCleared();
}

void TimelineModel::clearLuminanceData()
{
    QWriteLocker lock(&m_lock);
    m_snapshot.timestamps.clear();
    m_snapshot.values.clear();
    m_snapshot.dataEntries.clear();
    // Preserve m_snapshot.audio
    lock.unlock();
    // Emit dataReplaced (not dataCleared) so ChartPanel can re-render with audio only
    emit dataReplaced();
}

void TimelineModel::removeRegionData(int index)
{
    QWriteLocker lock(&m_lock);
    if (index >= 0 && index < m_snapshot.values.size()) {
        m_snapshot.values.removeAt(index);
        if (index < m_snapshot.dataEntries.size())
            m_snapshot.dataEntries.removeAt(index);
    }
    if (m_snapshot.values.isEmpty()) {
        m_snapshot.timestamps.clear();
    }
    lock.unlock();
    emit dataReplaced();
}

void TimelineModel::removeRegionDataByRoiId(int roiId)
{
    QWriteLocker lock(&m_lock);
    int idx = -1;
    for (int i = 0; i < m_snapshot.dataEntries.size(); ++i) {
        if (m_snapshot.dataEntries[i].roiId == roiId) {
            idx = i;
            break;
        }
    }
    if (idx >= 0 && idx < m_snapshot.values.size()) {
        m_snapshot.values.removeAt(idx);
        if (idx < m_snapshot.dataEntries.size())
            m_snapshot.dataEntries.removeAt(idx);
    }
    if (m_snapshot.values.isEmpty()) {
        m_snapshot.timestamps.clear();
    }
    lock.unlock();
    emit dataReplaced();
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
                                const SnapshotFusionData &snapshotFusion,
                                const QVector<QPolygon> &polygons,
                                const QVector<GuideLine> &guideLines) const
{
    QReadLocker lock(&m_lock);

    if (m_snapshot.isEmpty() && !m_snapshot.hasAudio())
        return false;

    QJsonObject root;
    root["version"] = 6;
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

    // ROI polygons (v5)
    if (!polygons.isEmpty()) {
        QJsonArray polyArray;
        for (const QPolygon &poly : polygons) {
            QJsonObject pObj;
            QJsonArray pointsArray;
            for (const QPoint &pt : poly) {
                QJsonArray ptArr;
                ptArr.append(pt.x());
                ptArr.append(pt.y());
                pointsArray.append(ptArr);
            }
            pObj["points"] = pointsArray;
            polyArray.append(pObj);
        }
        root["polygons"] = polyArray;
    }

    // Guide lines (v5)
    if (!guideLines.isEmpty()) {
        QJsonArray glArray;
        for (const GuideLine &gl : guideLines) {
            QJsonObject glObj;
            glObj["x1"] = gl.start.x();
            glObj["y1"] = gl.start.y();
            glObj["x2"] = gl.end.x();
            glObj["y2"] = gl.end.y();
            glObj["color"] = gl.color.name(QColor::HexArgb);
            glArray.append(glObj);
        }
        root["guide_lines"] = glArray;
    }

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

    // Audio data (v4) - volume and metadata only, spectrogram stored separately in .vla.spec
    if (!m_snapshot.audio.isEmpty()) {
        QJsonObject audioObj;
        QJsonArray volumeArray;
        for (qreal v : m_snapshot.audio.volume)
            volumeArray.append(v);
        audioObj["volume"] = volumeArray;

        audioObj["sample_rate"] = m_snapshot.audio.sampleRate;
        audioObj["hop_length"] = m_snapshot.audio.hopLength;
        audioObj["n_fft"] = m_snapshot.audio.nFft;
        root["audio"] = audioObj;
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

    // v6: DataEntry metadata (ROI type + ID mapping)
    if (!m_snapshot.dataEntries.isEmpty()) {
        QJsonArray entriesArray;
        for (const DataEntry &entry : m_snapshot.dataEntries) {
            QJsonObject eObj;
            eObj["type"] = (entry.type == DataEntry::Rect) ? "rect" : "polygon";
            eObj["roi_id"] = entry.roiId;
            entriesArray.append(eObj);
        }
        root["data_entries"] = entriesArray;
    }

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
                                  SnapshotFusionData *snapshotFusion,
                                  QVector<QPolygon> *polygons,
                                  QVector<GuideLine> *guideLines)
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
            // v5: skip polygon entries in regions array (they have "type":"polygon")
            if (obj.contains("type") && obj["type"].toString() == "polygon")
                continue;
            regions->append(QRect(obj["x"].toInt(), obj["y"].toInt(),
                                   obj["w"].toInt(), obj["h"].toInt()));
        }
    }

    // v5+: ROI polygons
    if (version >= 5 && polygons && root.contains("polygons")) {
        polygons->clear();
        QJsonArray polyArray = root["polygons"].toArray();
        for (const auto &v : polyArray) {
            QJsonObject pObj = v.toObject();
            QJsonArray pointsArray = pObj["points"].toArray();
            QPolygon poly;
            for (const auto &ptVal : pointsArray) {
                QJsonArray ptArr = ptVal.toArray();
                if (ptArr.size() >= 2)
                    poly.append(QPoint(ptArr[0].toInt(), ptArr[1].toInt()));
            }
            if (poly.size() >= 3)
                polygons->append(poly);
        }
    }

    // v5+: guide lines
    if (version >= 5 && guideLines && root.contains("guide_lines")) {
        guideLines->clear();
        QJsonArray glArray = root["guide_lines"].toArray();
        for (const auto &v : glArray) {
            QJsonObject glObj = v.toObject();
            GuideLine gl;
            gl.start = QPoint(glObj["x1"].toInt(), glObj["y1"].toInt());
            gl.end = QPoint(glObj["x2"].toInt(), glObj["y2"].toInt());
            gl.color = QColor(glObj["color"].toString());
            guideLines->append(gl);
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

    // Parse audio data (v4+)
    AudioData audioData;
    if (version >= 4 && root.contains("audio")) {
        QJsonObject audioObj = root["audio"].toObject();

        QJsonArray volArray = audioObj["volume"].toArray();
        audioData.volume.reserve(volArray.size());
        for (const auto &v : volArray)
            audioData.volume.append(v.toDouble());

        // Try to load spectrogram from separate binary file (.vla.spec)
        QString specFilePath = filePath + ".spec";
        bool specLoaded = false;
        if (QFile::exists(specFilePath)) {
            specLoaded = loadSpecFromFile(specFilePath, audioData);
        }
        qDebug() << "[loadFromFile] specFilePath:" << specFilePath
                 << "exists:" << QFile::exists(specFilePath)
                 << "specLoaded:" << specLoaded
                 << "spectrogram.size:" << audioData.spectrogram.size()
                 << "volume.size:" << audioData.volume.size();

        // Fallback: load spectrogram from JSON (for backward compatibility)
        if (!specLoaded && audioObj.contains("spectrogram")) {
            QJsonArray specArray = audioObj["spectrogram"].toArray();
            audioData.spectrogram.reserve(specArray.size());
            for (const auto &binVal : specArray) {
                QJsonArray binArray = binVal.toArray();
                QVector<qreal> bin;
                bin.reserve(binArray.size());
                for (const auto &v : binArray)
                    bin.append(v.toDouble());
                audioData.spectrogram.append(std::move(bin));
            }
        }

        audioData.sampleRate = audioObj["sample_rate"].toDouble(16000);
        audioData.hopLength = audioObj["hop_length"].toInt(512);
        audioData.nFft = audioObj["n_fft"].toInt(1280);
        audioData.timeResolutionMs = 1000.0 * audioData.hopLength / audioData.sampleRate;
    }

    // Parse data entries (v6+)
    QVector<DataEntry> dataEntries;
    if (version >= 6 && root.contains("data_entries")) {
        QJsonArray entriesArray = root["data_entries"].toArray();
        dataEntries.reserve(entriesArray.size());
        for (const auto &v : entriesArray) {
            QJsonObject eObj = v.toObject();
            DataEntry entry;
            entry.type = (eObj["type"].toString() == "polygon") ? DataEntry::Polygon : DataEntry::Rect;
            entry.roiId = eObj["roi_id"].toInt(-1);
            dataEntries.append(entry);
        }
    } else if (version >= 2 && version < 6) {
        // Backward compatibility: generate dataEntries from regions/polygons order
        int nextId = 1;
        if (regions) {
            for (int i = 0; i < regions->size(); ++i) {
                DataEntry entry;
                entry.type = DataEntry::Rect;
                entry.roiId = nextId++;
                dataEntries.append(entry);
            }
        }
        if (polygons) {
            for (int i = 0; i < polygons->size(); ++i) {
                DataEntry entry;
                entry.type = DataEntry::Polygon;
                entry.roiId = nextId++;
                dataEntries.append(entry);
            }
        }
    }

    // Set data (emits dataReplaced)
    if (!dataEntries.isEmpty()) {
        setData(std::move(timestamps), std::move(values), std::move(dataEntries), audioData);
    } else {
        setData(std::move(timestamps), std::move(values), audioData);
    }
    return true;
}

bool TimelineModel::saveSpecToFile(const QString &filePath, const AudioData &audio)
{
    if (audio.spectrogram.isEmpty() || audio.spectrogram[0].isEmpty())
        return false;

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;

    quint32 nFrames = static_cast<quint32>(audio.spectrogram[0].size());
    quint32 nFreqBins = static_cast<quint32>(audio.spectrogram.size());
    float sampleRate = static_cast<float>(audio.sampleRate);
    quint32 hopLength = static_cast<quint32>(audio.hopLength);

    // Write header
    file.write(reinterpret_cast<const char *>(&nFrames), sizeof(quint32));
    file.write(reinterpret_cast<const char *>(&nFreqBins), sizeof(quint32));
    file.write(reinterpret_cast<const char *>(&sampleRate), sizeof(float));
    file.write(reinterpret_cast<const char *>(&hopLength), sizeof(quint32));

    // Write spectrogram data (row-major: [freq_bin][time_frame])
    for (const auto &bin : audio.spectrogram) {
        for (qreal v : bin) {
            float fv = static_cast<float>(v);
            file.write(reinterpret_cast<const char *>(&fv), sizeof(float));
        }
    }

    file.close();
    return true;
}

bool TimelineModel::loadSpecFromFile(const QString &filePath, AudioData &audio)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    // Read header
    quint32 nFrames, nFreqBins, hopLength;
    float sampleRate;

    if (file.read(reinterpret_cast<char *>(&nFrames), sizeof(quint32)) != sizeof(quint32))
        return false;
    if (file.read(reinterpret_cast<char *>(&nFreqBins), sizeof(quint32)) != sizeof(quint32))
        return false;
    if (file.read(reinterpret_cast<char *>(&sampleRate), sizeof(float)) != sizeof(float))
        return false;
    if (file.read(reinterpret_cast<char *>(&hopLength), sizeof(quint32)) != sizeof(quint32))
        return false;

    if (nFrames == 0 || nFreqBins == 0 || nFreqBins > 4096 || nFrames > 2000000)
        return false;

    // Read spectrogram data
    audio.spectrogram.resize(nFreqBins);
    for (quint32 f = 0; f < nFreqBins; ++f) {
        audio.spectrogram[f].resize(nFrames);
        for (quint32 t = 0; t < nFrames; ++t) {
            float fv;
            if (file.read(reinterpret_cast<char *>(&fv), sizeof(float)) != sizeof(float))
                return false;
            audio.spectrogram[f][t] = static_cast<qreal>(fv);
        }
    }

    audio.sampleRate = static_cast<qreal>(sampleRate);
    audio.hopLength = static_cast<int>(hopLength);
    audio.nFft = 0;  // Not stored in binary, will be set by caller if needed
    audio.timeResolutionMs = 1000.0 * audio.hopLength / audio.sampleRate;

    file.close();
    return true;
}
