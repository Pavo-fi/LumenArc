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

void TimelineModel::removeRegionDataByRoiId(int roiId, DataEntry::Type type)
{
    QWriteLocker lock(&m_lock);
    int idx = -1;
    for (int i = 0; i < m_snapshot.dataEntries.size(); ++i) {
        if (m_snapshot.dataEntries[i].roiId == roiId &&
            m_snapshot.dataEntries[i].type == type) {
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

// ---------------------------------------------------------------------------
// VLA2 二进制容器格式
//
// 布局（QDataStream 默认大端）：
//   [0-3]   magic "VLA2"
//   [4-7]   quint32 formatVersion = 2
//   [8-11]  quint32 chunkCount
//   [12-15] quint32 flags（保留，0）
//   随后 chunkCount 个块：
//     tag: 4 ASCII | codec: quint8 (0=raw,1=zlib) | reserved: 3 字节
//     rawLength: quint32（解压后长度）| storedLength: quint32 | payload
//
// 块定义：
//   META  QJsonDocument(compact)：version(8 语义)/analyzed_at/time_calibration/
//         regions[+roi_id]/polygons[+roi_id]/guide_lines/magnifier/labels/
//         pinned/snapshot_fusion(含 base64 PNG)/audio 参数/data_entries
//         （v8：time_calibration 仿射校时模型取代 time_offset；v≤7 迁移见 loadFromFile）
//   TMS   quint64 count + count×qint64 时间戳（毫秒）
//   LUM   quint32 roiCount + quint64 sampleCount + roiCount×sampleCount×float32
//   VOL   quint64 count + count×float32 音量
//   SPEC  quint32 freqBins + quint32 frames + double min + double max
//         + freqBins×frames×uint8（[min,max] 量化，恰为色映射显示精度；
//         量化误差 ~range/255；比 double 省 8 倍、比 float32 省 4 倍）
//
// 相比 JSON v7：单文件自包含（频谱内嵌，无 .spec 伴随文件）、体积减半以上、
// 读写无 JSON 大数组解析。旧 JSON vla 仍可读（魔数嗅探分流）。
// ---------------------------------------------------------------------------
static const char VLA2_MAGIC[4] = {'V', 'L', 'A', '2'};
static constexpr quint32 VLA2_VERSION = 2;
/// .vla 语义版本上限（META version 字段；v9 = time_calibration 含分段重建）
/// v8 = time_calibration 仿射校时；v≤7 迁移见 loadFromFile
static constexpr int kCurrentVlaVersion = 9;

static QByteArray vlaPackChunk(const char *tag4, const QByteArray &payload)
{
    QByteArray compressed = qCompress(payload, 6);
    const bool useZ = compressed.size() < payload.size();
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.writeRawData(tag4, 4);
    ds << quint8(useZ ? 1 : 0);
    ds.writeRawData("\0\0\0", 3);   // reserved
    ds << quint32(payload.size());
    ds << quint32(useZ ? compressed.size() : payload.size());
    out.append(useZ ? compressed : payload);
    return out;
}

static bool vlaUnpackAll(const QByteArray &data, QMap<QByteArray, QByteArray> *chunks)
{
    if (data.size() < 16 || memcmp(data.constData(), VLA2_MAGIC, 4) != 0)
        return false;
    QDataStream ds(data);
    ds.skipRawData(4);
    quint32 version = 0, count = 0, flags = 0;
    ds >> version >> count >> flags;
    Q_UNUSED(flags);
    if (version != VLA2_VERSION || count > 64)
        return false;
    for (quint32 i = 0; i < count; ++i) {
        char tag[4];
        quint8 codec = 0;
        char reserved[3];
        quint32 rawLen = 0, storedLen = 0;
        if (ds.readRawData(tag, 4) != 4)
            return false;
        ds >> codec;
        if (ds.readRawData(reserved, 3) != 3)
            return false;
        ds >> rawLen >> storedLen;
        if (rawLen > 512 * 1024 * 1024 || storedLen > 512 * 1024 * 1024)
            return false;
        QByteArray stored(int(storedLen), Qt::Uninitialized);
        if (storedLen > 0 && ds.readRawData(stored.data(), int(storedLen)) != int(storedLen))
            return false;
        QByteArray payload = (codec == 1) ? qUncompress(stored) : stored;
        if (payload.size() != int(rawLen))
            return false;
        chunks->insert(QByteArray(tag, 4), payload);
    }
    return true;
}

bool TimelineModel::saveToFile(const QString &filePath,
                                const QVector<QRect> &regions,
                                const TimeCalibration &calibration,
                                const QRect &magnifier,
                                const QVector<ChartLabel> &labels,
                                const QRect &pinned,
                                const SnapshotFusionData &snapshotFusion,
                                const QVector<QPolygon> &polygons,
                                const QVector<GuideLine> &guideLines,
                                const QVector<int> &regionRoiIds,
                                const QVector<int> &polygonRoiIds) const
{
    QReadLocker lock(&m_lock);

    if (m_snapshot.isEmpty() && !m_snapshot.hasAudio())
        return false;

    QJsonObject root;
    root["version"] = 9;
    root["analyzed_at"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    // v9：time_calibration 含分段重建（piecewise）；v8 及以下旧文件可读（迁移），
    // v9 不再写 time_offset（Q-19 严格升版）
    if (calibration.isValid())
        root["time_calibration"] = calibration.toJson();

    // ROI rectangles（v7: 附带 roi_id，恢复时保持与分析数据对齐）
    QJsonArray regionsArray;
    for (int i = 0; i < regions.size(); ++i) {
        const QRect &rc = regions[i];
        QJsonObject obj;
        obj["x"] = rc.x();
        obj["y"] = rc.y();
        obj["w"] = rc.width();
        obj["h"] = rc.height();
        if (i < regionRoiIds.size())
            obj["roi_id"] = regionRoiIds[i];
        regionsArray.append(obj);
    }
    root["regions"] = regionsArray;

    // ROI polygons (v5; v7 附带 roi_id)
    if (!polygons.isEmpty()) {
        QJsonArray polyArray;
        for (int i = 0; i < polygons.size(); ++i) {
            const QPolygon &poly = polygons[i];
            QJsonObject pObj;
            QJsonArray pointsArray;
            for (const QPoint &pt : poly) {
                QJsonArray ptArr;
                ptArr.append(pt.x());
                ptArr.append(pt.y());
                pointsArray.append(ptArr);
            }
            pObj["points"] = pointsArray;
            if (i < polygonRoiIds.size())
                pObj["roi_id"] = polygonRoiIds[i];
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

    // Audio metadata（音量/频谱在 VOL/SPEC 二进制块中）
    if (!m_snapshot.audio.isEmpty()) {
        QJsonObject audioObj;
        audioObj["sample_rate"] = m_snapshot.audio.sampleRate;
        audioObj["hop_length"] = m_snapshot.audio.hopLength;
        audioObj["n_fft"] = m_snapshot.audio.nFft;
        root["audio"] = audioObj;
    }

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

    // ---- 组装 VLA2 二进制块 ----
    QVector<QPair<QByteArray, QByteArray>> chunks;
    chunks.append({"META", QJsonDocument(root).toJson(QJsonDocument::Compact)});

    // TMS：时间戳（qint64 毫秒）
    QByteArray tms;
    {
        QDataStream ds(&tms, QIODevice::WriteOnly);
        ds << quint64(m_snapshot.timestamps.size());
        for (qint64 t : m_snapshot.timestamps)
            ds << t;
    }
    chunks.append({"TMS ", tms});

    // LUM：亮度矩阵（float32，按 ROI 行主序）
    QByteArray lum;
    {
        QDataStream ds(&lum, QIODevice::WriteOnly);
        ds << quint32(m_snapshot.values.size());
        ds << quint64(m_snapshot.timestamps.size());
        ds.setFloatingPointPrecision(QDataStream::SinglePrecision);
        for (const auto &row : m_snapshot.values)
            for (qreal v : row)
                ds << float(v);
    }
    chunks.append({"LUM ", lum});

    // VOL：音量（float32）
    if (!m_snapshot.audio.volume.isEmpty()) {
        QByteArray vol;
        QDataStream ds(&vol, QIODevice::WriteOnly);
        ds << quint64(m_snapshot.audio.volume.size());
        ds.setFloatingPointPrecision(QDataStream::SinglePrecision);
        for (qreal v : m_snapshot.audio.volume)
            ds << float(v);
        chunks.append({"VOL ", vol});
    }

    // SPEC：频谱矩阵（uint16 量化到 [min,max]，频率行主序 freq×frames）
    const auto &spec = m_snapshot.audio.spectrogram;
    if (!spec.isEmpty() && !spec[0].isEmpty()) {
        double sMin = std::numeric_limits<double>::max();
        double sMax = std::numeric_limits<double>::lowest();
        for (const auto &bin : spec)
            for (qreal v : bin) {
                sMin = qMin(sMin, double(v));
                sMax = qMax(sMax, double(v));
            }
        QByteArray sp;
        QDataStream ds(&sp, QIODevice::WriteOnly);
        ds << quint32(spec.size());
        ds << quint32(spec[0].size());
        ds << sMin << sMax;
        const double scale = (sMax > sMin) ? (255.0 / (sMax - sMin)) : 0.0;
        for (const auto &bin : spec)
            for (qreal v : bin)
                ds << quint8(scale > 0.0
                             ? qBound(0, int((v - sMin) * scale + 0.5), 255)
                             : 0);
        chunks.append({"SPEC", sp});
    }

    QByteArray fileData;
    {
        QDataStream ds(&fileData, QIODevice::WriteOnly);
        ds.writeRawData(VLA2_MAGIC, 4);
        ds << quint32(VLA2_VERSION) << quint32(chunks.size()) << quint32(0);
    }
    for (const auto &c : chunks)
        fileData.append(vlaPackChunk(c.first.constData(), c.second));

    lock.unlock();

    QFile file(filePath);
    if (!file.open(QIODevice::WriteOnly))
        return false;
    file.write(fileData);
    file.close();
    return file.error() == QFile::NoError;
}

bool TimelineModel::loadFromFile(const QString &filePath,
                                  QVector<QRect> *regions,
                                  TimeCalibration *calibration,
                                  QRect *magnifier,
                                  QVector<ChartLabel> *labels,
                                  QRect *pinned,
                                  SnapshotFusionData *snapshotFusion,
                                  QVector<QPolygon> *polygons,
                                  QVector<GuideLine> *guideLines,
                                  QVector<int> *regionRoiIds,
                                  QVector<int> *polygonRoiIds)
{
    QFile file(filePath);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QByteArray data = file.readAll();
    file.close();

    QJsonObject root;
    int version = 0;
    QVector<qint64> timestamps;
    QVector<QVector<qreal>> values;
    AudioData audioData;
    bool isVla2 = false;

    if (data.startsWith(QByteArray(VLA2_MAGIC, 4))) {
        // ===== VLA2 二进制容器 =====
        isVla2 = true;
        QMap<QByteArray, QByteArray> chunks;
        if (!vlaUnpackAll(data, &chunks))
            return false;
        QJsonParseError perr;
        QJsonDocument metaDoc = QJsonDocument::fromJson(chunks.value("META"), &perr);
        if (perr.error != QJsonParseError::NoError || !metaDoc.isObject())
            return false;
        root = metaDoc.object();
        version = root["version"].toInt();
        if (version < 1 || version > kCurrentVlaVersion)   // F4：超上限明确拒绝
            return false;

        // TMS：时间戳（qint64 毫秒）
        {
            QDataStream ds(chunks.value("TMS "));
            quint64 n = 0;
            ds >> n;
            if (n > 100000000)
                return false;
            timestamps.reserve(int(n));
            for (quint64 i = 0; i < n; ++i) {
                qint64 t = 0;
                ds >> t;
                timestamps.append(t);
            }
        }
        // LUM：亮度矩阵（float32）
        {
            QDataStream ds(chunks.value("LUM "));
            quint32 rows = 0;
            quint64 cols = 0;
            ds >> rows >> cols;
            if (rows > 1024 || cols > 100000000)
                return false;
            ds.setFloatingPointPrecision(QDataStream::SinglePrecision);
            values.reserve(int(rows));
            for (quint32 r = 0; r < rows; ++r) {
                QVector<qreal> row;
                row.reserve(int(cols));
                for (quint64 c = 0; c < cols; ++c) {
                    float f = 0;
                    ds >> f;
                    row.append(f);
                }
                values.append(std::move(row));
            }
        }
        // VOL：音量（float32）
        if (chunks.contains("VOL ")) {
            QDataStream ds(chunks.value("VOL "));
            quint64 n = 0;
            ds >> n;
            if (n > 100000000)
                return false;
            ds.setFloatingPointPrecision(QDataStream::SinglePrecision);
            audioData.volume.reserve(int(n));
            for (quint64 i = 0; i < n; ++i) {
                float f = 0;
                ds >> f;
                audioData.volume.append(f);
            }
        }
        // SPEC：频谱矩阵（uint16 量化，频率行主序）
        if (chunks.contains("SPEC")) {
            QDataStream ds(chunks.value("SPEC"));
            quint32 fb = 0, fr = 0;
            double sMin = 0, sMax = 0;
            ds >> fb >> fr >> sMin >> sMax;
            if (fb > 8192 || fr > 10000000)
                return false;
            const double inv = (sMax > sMin) ? ((sMax - sMin) / 255.0) : 0.0;
            audioData.spectrogram.resize(int(fb));
            for (quint32 f = 0; f < fb; ++f) {
                audioData.spectrogram[int(f)].resize(int(fr));
                for (quint32 t = 0; t < fr; ++t) {
                    quint8 q = 0;
                    ds >> q;
                    audioData.spectrogram[int(f)][int(t)] = sMin + q * inv;
                }
            }
        }
    } else {
        // ===== 旧 JSON 格式（v1~v7） =====
        QJsonParseError parseError;
        QJsonDocument doc = QJsonDocument::fromJson(data, &parseError);
        if (parseError.error != QJsonParseError::NoError)
            return false;

        if (!doc.isObject())
            return false;

        root = doc.object();
        version = root["version"].toInt();
        if (version < 1 || version > kCurrentVlaVersion)   // F4：超上限明确拒绝
            return false;
    }

    // v2+: ROI rectangles（v7 可能附带 roi_id）
    if (regionRoiIds)
        regionRoiIds->clear();
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
            if (regionRoiIds && obj.contains("roi_id"))
                regionRoiIds->append(obj["roi_id"].toInt());
        }
    }

    // v5+: ROI polygons（v7 可能附带 roi_id）
    if (polygonRoiIds)
        polygonRoiIds->clear();
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
            if (poly.size() >= 3) {
                polygons->append(poly);
                if (polygonRoiIds && pObj.contains("roi_id"))
                    polygonRoiIds->append(pObj["roi_id"].toInt());
            }
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

    // v8+: time calibration（v≤7 旧文件走 time_offset 迁移）
    if (calibration) {
        if (version >= 8 && root.contains("time_calibration")) {
            *calibration = TimeCalibration::fromJson(
                root["time_calibration"].toObject());
        } else if (version >= 2) {
            *calibration = TimeCalibration::fromLegacyOffset(
                static_cast<qint64>(root["time_offset"].toDouble()));
        }
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

    // Parse timestamps / luminances（旧 JSON 路径；VLA2 已从二进制块读取）
    if (!isVla2) {
        QJsonArray tsArray = root["timestamps"].toArray();
        timestamps.reserve(tsArray.size());
        for (const auto &v : tsArray)
            timestamps.append(static_cast<qint64>(v.toDouble()));

        QJsonArray lumArray = root["luminances"].toArray();
        values.reserve(lumArray.size());
        for (const auto &regionVal : lumArray) {
            QJsonArray regionArray = regionVal.toArray();
            QVector<qreal> region;
            region.reserve(regionArray.size());
            for (const auto &v : regionArray)
                region.append(v.toDouble());
            values.append(std::move(region));
        }
    }

    // Parse audio data (v4+；VLA2 的 VOL/SPEC 已从二进制块读取，此处仅取参数)
    if (version >= 4 && root.contains("audio")) {
        QJsonObject audioObj = root["audio"].toObject();

        if (!isVla2) {
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
    // v6 兼容：文件无 roi_id 字段时，从 dataEntries（v6 全局顺序 id）按类型顺序
    // 回填恢复 ID，使 ROI 模型恢复后与分析数据精确匹配；数量对不上则放弃，
    // 调用方回退顺序分配（与旧行为一致）
    if (regionRoiIds && regionRoiIds->isEmpty() && regions && !regions->isEmpty()
        && !dataEntries.isEmpty()) {
        for (const auto &e : dataEntries)
            if (e.type == DataEntry::Rect)
                regionRoiIds->append(e.roiId);
        if (regionRoiIds->size() != regions->size())
            regionRoiIds->clear();
    }
    if (polygonRoiIds && polygonRoiIds->isEmpty() && polygons && !polygons->isEmpty()
        && !dataEntries.isEmpty()) {
        for (const auto &e : dataEntries)
            if (e.type == DataEntry::Polygon)
                polygonRoiIds->append(e.roiId);
        if (polygonRoiIds->size() != polygons->size())
            polygonRoiIds->clear();
    }

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

TimeCalibration TimelineModel::peekCalibrationFromVla(const QString &filePath)
{
    TimeCalibration cal;
    QFile f(filePath);
    if (!f.open(QIODevice::ReadOnly))
        return cal;
    const QByteArray data = f.readAll();
    f.close();
    if (data.isEmpty())
        return cal;

    if (data.size() >= 16 && memcmp(data.constData(), VLA2_MAGIC, 4) == 0) {
        // VLA2：只解 META chunk（轻量，跳过谱图等重数据）
        QMap<QByteArray, QByteArray> chunks;
        if (!vlaUnpackAll(data, &chunks))
            return cal;
        const QJsonObject meta = QJsonDocument::fromJson(chunks.value("META")).object();
        if (meta.isEmpty())
            return cal;
        const int version = meta["version"].toInt();
        if (version >= 8 && meta.contains("time_calibration"))
            return TimeCalibration::fromJson(meta["time_calibration"].toObject());
        if (version >= 2)
            return TimeCalibration::fromLegacyOffset(
                static_cast<qint64>(meta["time_offset"].toDouble()));
        return cal;
    }

    // 旧 JSON 格式（v≤7）：只读顶层校时字段
    const QJsonDocument doc = QJsonDocument::fromJson(data);
    if (!doc.isObject())
        return cal;
    const QJsonObject root = doc.object();
    const int version = root["version"].toInt();
    if (version >= 8 && root.contains("time_calibration"))
        return TimeCalibration::fromJson(root["time_calibration"].toObject());
    if (version >= 2)
        return TimeCalibration::fromLegacyOffset(
            static_cast<qint64>(root["time_offset"].toDouble()));
    return cal;
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
