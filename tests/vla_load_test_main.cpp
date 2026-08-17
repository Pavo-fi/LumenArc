/**
 * @file vla_load_test_main.cpp
 * @brief 离屏复现 .vla 加载→区域恢复→曲线重建全链路，验证各版本 vla 文件
 *
 * 模拟 MainWindow::openVideoFile 的 vla-cache 加载序列：
 *   clearData → loadFromFile(setData→dataReplaced 同步触发) → restoreRegions/restorePolygons
 *   → 事件循环跑 deferred rebuildSeries → 统计曲线数量与数据点
 */
#include <QApplication>
#include <QElapsedTimer>
#include <QtConcurrent>
#include <QThreadPool>
#include <QAtomicInt>
#include <QDebug>
#include <QDir>
#include <QtCharts/QLineSeries>
#include "chartpanel.h"
#include "spectrogrampanel_enhanced.h"
#include "theme.h"
#include "domain/roi_model.h"
#include "domain/roi_model.h"
#include "domain/timeline_model.h"

static int testFile(ChartPanel *chart, RoiModel *rm, RoiModel *pm,
                    TimelineModel *tm, QApplication &app,
                    const QString &path)
{
    fprintf(stderr, "[tf] start\n");
    // 模拟 openVideoFile：清空后加载 vla
    rm->clearRegions();
    pm->clearPolygons();
    tm->clearData();

    QVector<QRect> regions;
    QVector<QPolygon> polys;
    QVector<GuideLine> guideLines;
    QVector<int> rIds, pIds;
    TimeCalibration calibration;   // v8：校时（v≤7 自动迁移 time_offset）
    QRect magRect, pinnedRect;
    QVector<ChartLabel> labels;
    SnapshotFusionData fusion;

    QElapsedTimer tLoad;
    tLoad.start();
    bool ok = tm->loadFromFile(path, &regions, &calibration, &magRect, &labels,
                               &pinnedRect, &fusion, &polys, &guideLines, &rIds, &pIds);
    fprintf(stderr, "[tf] loadFromFile took %lld ms (%s)\n",
            (long long)tLoad.elapsed(), qPrintable(QFileInfo(path).fileName()));
    if (!ok) {
        fprintf(stderr, "  loadFromFile FAILED\n");
        return -1;
    }
    // v8 迁移校验：旧文件 time_offset → legacy 校时（dateKnown=false, rate=1.0）
    if (calibration.isValid()) {
        bool migOk = !calibration.dateKnown
                     && qAbs(calibration.rate - 1.0) < 1e-12
                     && calibration.source == TimeCalibration::Source::Manual;
        fprintf(stderr, "[tf] v<=7 time_offset migrated: offset=%lld %s\n",
                (long long)calibration.offsetMs, migOk ? "OK" : "MISMATCH <<<");
        if (!migOk) return -1;
    }
    fprintf(stderr, "[tf] loaded: regions=%d rIds=%lld polys=%d pIds=%lld entries=%lld ts=%lld values=%lld\n",
            regions.size(), (long long)rIds.size(), polys.size(), (long long)pIds.size(),
            (long long)tm->snapshot().dataEntries().size(),
            (long long)tm->snapshot().timestamps.size(),
            (long long)tm->snapshot().values().size());

    // 模拟 restoreAnalysisState + 多边形恢复
    if (rIds.size() == regions.size())
        rm->restoreRegions(regions, rIds);
    else {
        rm->clearRegions();
        for (const QRect &rc : regions) rm->addRegion(rc);
    }
    if (pIds.size() == polys.size())
        pm->restorePolygons(polys, pIds);
    else {
        pm->clearPolygons();
        for (const QPolygon &p : polys) pm->addPolygon(p);
    }

    // 让 QTimer::singleShot(0) 的 deferred rebuildSeries 执行
    app.processEvents();
    app.processEvents();

    int lumSeries = 0, nonEmpty = 0;
    const auto all = chart->chart()->series();
    for (auto *s : all) {
        auto *ls = qobject_cast<QLineSeries *>(s);
        if (!ls || ls->name() == QStringLiteral("音量")) continue;
        lumSeries++;
        if (ls->count() > 0) nonEmpty++;
    }
    int expected = regions.size() + polys.size();
    bool pass = (lumSeries == expected) && (expected == 0 || nonEmpty == expected);
    fprintf(stderr, "[tf] series=%d nonEmpty=%d expected=%d => %s\n",
            lumSeries, nonEmpty, expected, pass ? "PASS" : "FAIL <<<");

    // ---- VLA2 往返：新格式保存 → 清空 → 重载 → 数据与曲线比对 ----
    const QVector<qint64> oldTs = tm->snapshot().timestamps;
    const QVector<QVector<qreal>> oldVals = tm->snapshot().values();
    QString tmp = QDir::temp().filePath("vla2_roundtrip_test.vla");
    QFile::remove(tmp);
    TimeCalibration saveCal = TimeCalibration::fromLegacyOffset(12345);
    bool saved = tm->saveToFile(tmp, rm->regions(), saveCal, QRect(1, 2, 3, 4), {},
                                QRect(), SnapshotFusionData(), pm->polygons(), {},
                                rm->roiIds(), pm->polygonRoiIds());
    if (!saved) {
        fprintf(stderr, "[tf] VLA2 save FAILED <<<\n");
        return 0;
    }
    fprintf(stderr, "[tf] VLA2 size=%lld bytes (source JSON=%lld)\n",
            (long long)QFileInfo(tmp).size(), (long long)QFileInfo(path).size());
    bool rtOk = false;
    {
        QVector<QRect> r2;
        QVector<QPolygon> p2;
        QVector<GuideLine> g2;
        QVector<int> rIds2, pIds2;
        TimeCalibration cal2;
        QRect m2, pin2;
        QVector<ChartLabel> l2;
        SnapshotFusionData f2;
        if (tm->loadFromFile(tmp, &r2, &cal2, &m2, &l2, &pin2, &f2, &p2, &g2, &rIds2, &pIds2)) {
            const auto snap = tm->snapshot();
            bool dataOk = (snap.timestamps == oldTs) && (snap.values().size() == oldVals.size());
            if (dataOk) {
                for (int r = 0; r < oldVals.size() && dataOk; ++r) {
                    if (snap.values()[r].size() != oldVals[r].size()) { dataOk = false; break; }
                    for (int c = 0; c < oldVals[r].size(); ++c)
                        if (qAbs(snap.values()[r][c] - oldVals[r][c]) > 1e-3) { dataOk = false; break; }
                }
            }
            // roiId 保持：VLA2 必须原样带回
            bool idsOk = (rIds2 == rm->roiIds()) && (pIds2 == pm->polygonRoiIds());
            // v8 校时往返：legacy 偏移原样带回（Q-19：v8 不再写 time_offset）
            bool calOk = cal2.isValid() && cal2.offsetMs == 12345
                         && !cal2.dateKnown && qAbs(cal2.rate - 1.0) < 1e-12;
            rtOk = dataOk && idsOk && calOk;
            fprintf(stderr, "[tf] VLA2 roundtrip: data=%s roiIds=%s calibration=%s => %s\n",
                    dataOk ? "OK" : "MISMATCH", idsOk ? "OK" : "MISMATCH",
                    calOk ? "OK" : "MISMATCH", rtOk ? "PASS" : "FAIL <<<");
        } else {
            fprintf(stderr, "[tf] VLA2 reload FAILED <<<\n");
        }
    }
    // ---- VLA2 音频/频谱块往返（合成数据） ----
    {
        TimelineModel tm2;
        QVector<qint64> ts{0, 100, 200};
        QVector<QVector<qreal>> vals{{1.5, 2.5, 3.5}};
        QVector<DataEntry> entries{{DataEntry::Rect, 1}};
        AudioData audio;
        audio.volume = {0.1, 0.2, 0.3, 0.4};
        audio.spectrogram = {{1.1, 2.2}, {3.3, 4.4}, {5.5, 6.6}};
        audio.sampleRate = 24000;
        audio.hopLength = 512;
        audio.nFft = 1920;
        audio.timeResolutionMs = 1000.0 * 512 / 24000;
        tm2.setData(ts, vals, entries, audio);
        QString tmp2 = QDir::temp().filePath("vla2_audio_test.vla");
        QFile::remove(tmp2);
        bool okS = tm2.saveToFile(tmp2, {QRect(0, 0, 10, 10)}, TimeCalibration(), {}, {}, {}, {}, {}, {}, {1}, {});
        TimelineModel tm3;
        bool okL = okS && tm3.loadFromFile(tmp2, nullptr, nullptr, nullptr, nullptr,
                                           nullptr, nullptr, nullptr, nullptr, nullptr, nullptr);
        const auto a3 = tm3.snapshot().audioData();
        bool audioOk = okL && a3.volume.size() == 4
                       && a3.spectrogram.size() == 3 && a3.spectrogram[0].size() == 2
                       && qAbs(a3.spectrogram[2][1] - 6.6) < 5e-2   // uint16 量化容差
                       && qAbs(a3.volume[3] - 0.4) < 1e-4
                       && qAbs(a3.sampleRate - 24000) < 1
                       && a3.hopLength == 512;
        fprintf(stderr, "[tf] VLA2 audio/spec roundtrip: %s (file=%lld bytes)\n",
                audioOk ? "PASS" : "FAIL <<<", (long long)QFileInfo(tmp2).size());
        QFile::remove(tmp2);
        rtOk = rtOk && audioOk;
    }
    QFile::remove(tmp);
    return (pass && rtOk) ? 1 : 0;
}

// ===========================================================================
// v1.8.0 P1b：.vla v10 单测（迁移链 / 通道往返 / 未知通道 opaque 保全 / 上界拒绝）
// ===========================================================================
static const char kV10Magic[4] = {'V', 'L', 'A', '2'};

/// 解析 VLA2 文件为原始块列表（codec/rawLen/stored 原样保留）
struct V10Chunk {
    QByteArray tag; quint8 codec; quint32 rawLen, storedLen; QByteArray stored;
};
static bool v10Parse(const QByteArray &data, QVector<V10Chunk> *out)
{
    if (data.size() < 16 || memcmp(data.constData(), kV10Magic, 4) != 0)
        return false;
    QDataStream ds(data);
    ds.skipRawData(4);
    quint32 ver = 0, cnt = 0, flags = 0;
    ds >> ver >> cnt >> flags;
    if (ver != 2)
        return false;
    for (quint32 i = 0; i < cnt; ++i) {
        char tag[4]; quint8 codec; char res[3]; quint32 rawLen, storedLen;
        if (ds.readRawData(tag, 4) != 4) return false;
        ds >> codec;
        if (ds.readRawData(res, 3) != 3) return false;
        ds >> rawLen >> storedLen;
        QByteArray stored(int(storedLen), Qt::Uninitialized);
        if (storedLen && ds.readRawData(stored.data(), int(storedLen)) != int(storedLen))
            return false;
        out->append({QByteArray(tag, 4), codec, rawLen, storedLen, stored});
    }
    return true;
}

static QByteArray v10PackRaw(const QByteArray &tag, quint8 codec, quint32 rawLen,
                             const QByteArray &stored)
{
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.writeRawData(tag.constData(), 4);
    ds << codec;
    ds.writeRawData("\0\0\0", 3);
    ds << rawLen << quint32(stored.size());
    out.append(stored);
    return out;
}

static QByteArray v10Build(const QVector<V10Chunk> &chunks)
{
    QByteArray out;
    QDataStream ds(&out, QIODevice::WriteOnly);
    ds.writeRawData(kV10Magic, 4);
    ds << quint32(2) << quint32(chunks.size()) << quint32(0);
    for (const auto &c : chunks)
        out.append(v10PackRaw(c.tag, c.codec, c.rawLen, c.stored));
    return out;
}

static QByteArray v10ReadAll(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QByteArray();
    return f.readAll();
}

static QByteArray v10ChunkPayload(const V10Chunk &c)
{
    return (c.codec == 1) ? qUncompress(c.stored) : c.stored;
}

static void v10ReplaceMeta(QVector<V10Chunk> *chunks, const QJsonObject &meta)
{
    for (auto &c : *chunks) {
        if (c.tag == QByteArray("META", 4)) {
            const QByteArray payload = QJsonDocument(meta).toJson(QJsonDocument::Compact);
            const QByteArray z = qCompress(payload, 6);
            if (z.size() < payload.size()) {
                c.codec = 1; c.rawLen = payload.size();
                c.stored = z; c.storedLen = z.size();
            } else {
                c.codec = 0; c.rawLen = payload.size();
                c.stored = payload; c.storedLen = payload.size();
            }
            return;
        }
    }
}

static int g_v10Checks = 0, g_v10Failures = 0;
#define V10CHECK(cond, msg)                                                     \
    do {                                                                        \
        ++g_v10Checks;                                                          \
        if (!(cond)) {                                                          \
            ++g_v10Failures;                                                    \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg);       \
        }                                                                       \
    } while (0)

static int testV10RoundTripAndChain()
{
    QDir tmp(QDir::tempPath() + QStringLiteral("/lumenarc_vla_v10"));
    tmp.removeRecursively();
    QDir().mkpath(tmp.path());
    const QString path = tmp.path() + QStringLiteral("/v10_rt.vla");
    QFile::remove(path);

    // 造数据：亮度 2 行（rect id1 + polygon id2）+ 音频（volume+spec）
    TimelineModel m;
    AudioData audio;
    audio.sampleRate = 24000; audio.hopLength = 512; audio.nFft = 1280;
    audio.timeResolutionMs = 1000.0 * 512 / 24000;
    audio.volume = {0.1, 0.2, 0.3, 0.4};
    audio.spectrogram = {{1.0, 2.0}, {3.0, 4.0}, {5.5, 6.6}};
    QVector<DataEntry> entries = {{DataEntry::Rect, 1}, {DataEntry::Polygon, 2}};
    m.setData({0, 1000, 2000}, {{1, 2, 3}, {4, 5, 6}}, entries, audio);
    TimeCalibration cal;
    cal.source = TimeCalibration::Source::Manual;
    cal.offsetMs = 1784700002000LL;
    cal.dateKnown = true;
    const bool saved = m.saveToFile(path, {QRect(10, 10, 60, 40)},
                                    cal, QRect(1, 2, 30, 20),
                                    {ChartLabel{500, QStringLiteral("L1"), Qt::red}},
                                    QRect(0, 0, 5, 5), SnapshotFusionData(),
                                    {}, {GuideLine{QPoint(0, 0), QPoint(9, 9), Qt::blue}},
                                    {1}, {2});
    V10CHECK(saved, "v10: saveToFile ok");

    // 文件层面：META version=10 且带 channels 清单（F5：写入代码与格式注释同步）
    QByteArray raw = v10ReadAll(path);
    QVector<V10Chunk> chunks;
    V10CHECK(v10Parse(raw, &chunks), "v10: file parses");
    const V10Chunk *metaChunk = nullptr;
    for (const auto &c : chunks)
        if (c.tag == QByteArray("META", 4)) metaChunk = &c;
    V10CHECK(metaChunk, "v10: META chunk present");
    const QJsonObject meta = QJsonDocument::fromJson(v10ChunkPayload(*metaChunk)).object();
    V10CHECK(meta["version"].toInt() == 10, "v10: META version=10");
    const QJsonArray chArr = meta["channels"].toArray();
    V10CHECK(chArr.size() == 2, "v10: channels list has luminance+audio");
    bool sawLum = false, sawAud = false;
    for (const auto &cv : chArr) {
        const QString kind = cv.toObject()["kind"].toString();
        if (kind == QLatin1String("luminance")) sawLum = true;
        if (kind == QLatin1String("audio")) sawAud = true;
    }
    V10CHECK(sawLum && sawAud, "v10: both channel kinds declared");

    // 重载往返
    TimelineModel m2;
    QVector<QRect> regions; QVector<QPolygon> polys; QVector<GuideLine> gl;
    QVector<int> rIds, pIds; TimeCalibration cal2; QRect mag, pin;
    QVector<ChartLabel> labels; SnapshotFusionData fusion;
    V10CHECK(m2.loadFromFile(path, &regions, &cal2, &mag, &labels, &pin, &fusion,
                             &polys, &gl, &rIds, &pIds), "v10: reload ok");
    const AnalysisSnapshot snap = m2.snapshot();
    V10CHECK(snap.lumRows().size() == 2 && snap.lumRows()[0].size() == 3,
             "v10: luminance rows roundtrip");
    V10CHECK(snap.lumEntries().size() == 2
             && snap.lumEntries()[0].roiId == 1
             && snap.lumEntries()[1].type == DataEntry::Polygon,
             "v10: dataEntries roundtrip");
    V10CHECK(snap.audioData().volume.size() == 4
             && qAbs(snap.audioData().volume[3] - 0.4) < 1e-4,
             "v10: audio volume roundtrip");
    V10CHECK(snap.audioData().spectrogram.size() == 3, "v10: spec roundtrip");
    V10CHECK(cal2.dateKnown && cal2.offsetMs == cal.offsetMs, "v10: calibration roundtrip");
    V10CHECK(regions.size() == 1 && rIds == QVector<int>{1}, "v10: regions+roiIds roundtrip");
    V10CHECK(gl.size() == 1 && gl[0].end == QPoint(9, 9), "v10: guide lines roundtrip");

    // ---- v9 合成文件 -> v10 加载迁移 -> 回存 v10（F2 迁移链；v7/v8 真文件见 files 循环）----
    const QString v9Path = tmp.path() + QStringLiteral("/v9_legacy.vla");
    {
        QVector<V10Chunk> v9Chunks = chunks;
        QJsonObject v9Meta = meta;
        v9Meta["version"] = 9;
        v9Meta.remove(QStringLiteral("channels"));   // v9 无通道清单
        v10ReplaceMeta(&v9Chunks, v9Meta);
        QFile f(v9Path);
        f.open(QIODevice::WriteOnly);
        f.write(v10Build(v9Chunks));
        f.close();
    }
    TimelineModel m3;
    QVector<QRect> r3; QVector<QPolygon> p3; QVector<GuideLine> g3; QVector<int> ri3, pi3;
    TimeCalibration c3; QRect mg3, pn3; QVector<ChartLabel> lb3; SnapshotFusionData fs3;
    V10CHECK(m3.loadFromFile(v9Path, &r3, &c3, &mg3, &lb3, &pn3, &fs3, &p3, &g3, &ri3, &pi3),
             "v9->v10: legacy load (migration)");
    V10CHECK(m3.snapshot().lumRows().size() == 2
             && m3.snapshot().audioData().volume.size() == 4,
             "v9->v10: channels populated from legacy blocks");
    const QString v10Path2 = tmp.path() + QStringLiteral("/v9_resaved.vla");
    QFile::remove(v10Path2);
    V10CHECK(m3.saveToFile(v10Path2, r3, c3, mg3, lb3, pn3, fs3, p3, g3, ri3, pi3),
             "v9->v10: resave as v10");
    QByteArray raw2 = v10ReadAll(v10Path2);
    QVector<V10Chunk> chunks2;
    V10CHECK(v10Parse(raw2, &chunks2), "v9->v10: resaved file parses");
    for (const auto &c : chunks2) {
        if (c.tag == QByteArray("META", 4)) {
            const QJsonObject m2j = QJsonDocument::fromJson(v10ChunkPayload(c)).object();
            V10CHECK(m2j["version"].toInt() == 10, "v9->v10: resaved version=10");
        }
    }
    TimelineModel m4;
    V10CHECK(m4.loadFromFile(v10Path2, nullptr, nullptr, nullptr, nullptr, nullptr,
                             nullptr, nullptr, nullptr, nullptr, nullptr)
             && m4.snapshot().lumRows()[1][2] == 6.0
             && m4.snapshot().audioData().volume.size() == 4,
             "v9->v10: resaved roundtrip data intact");

    // ---- 上界拒绝（F4）：version=11 必须明确拒载 ----
    {
        const QString v11Path = tmp.path() + QStringLiteral("/v11_future.vla");
        QVector<V10Chunk> v11Chunks = chunks;
        QJsonObject v11Meta = meta;
        v11Meta["version"] = 11;
        v10ReplaceMeta(&v11Chunks, v11Meta);
        QFile f(v11Path);
        f.open(QIODevice::WriteOnly);
        f.write(v10Build(v11Chunks));
        f.close();
        TimelineModel m5;
        V10CHECK(!m5.loadFromFile(v11Path, nullptr, nullptr, nullptr, nullptr,
                                  nullptr, nullptr, nullptr, nullptr, nullptr, nullptr),
                 "F4: version 11 rejected");
    }

    // ---- peekCalibrationFromVla 对 v10 可用 ----
    const TimeCalibration pk = TimelineModel::peekCalibrationFromVla(path);
    V10CHECK(pk.dateKnown && pk.offsetMs == cal.offsetMs, "peek: v10 calibration readable");

    fprintf(stderr, "[v10] roundtrip+chain: %d checks, %d failures\n",
            g_v10Checks, g_v10Failures);
    return g_v10Failures;
}

static int testV10UnknownChannelPassthrough()
{
    QDir tmp(QDir::tempPath() + QStringLiteral("/lumenarc_vla_v10"));
    QDir().mkpath(tmp.path());
    const QString base = tmp.path() + QStringLiteral("/v10_rt.vla");
    if (!QFile::exists(base)) {
        fprintf(stderr, "[v10-opaque] SKIP (base fixture missing)\n");
        return 0;
    }

    // 在合法 v10 文件上附加未知块 CH01（模拟更新版本写入的新通道）
    const QString withUnknown = tmp.path() + QStringLiteral("/v10_unknown.vla");
    {
        QByteArray raw = v10ReadAll(base);
        QVector<V10Chunk> chunks;
        if (!v10Parse(raw, &chunks)) { fprintf(stderr, "[v10-opaque] parse FAIL\n"); return 1; }
        const QByteArray mystery = QByteArray("FUTURE-CHANNEL-PAYLOAD-0123456789");
        V10Chunk ch{QByteArray("CH01", 4), 0, quint32(mystery.size()),
                    quint32(mystery.size()), mystery};
        chunks.append(ch);
        QFile f(withUnknown);
        f.open(QIODevice::WriteOnly);
        f.write(v10Build(chunks));
        f.close();
    }

    // 载入：未知通道进 opaque，亮度/音频不受影响（Q2：不丢弃）
    TimelineModel m;
    V10CHECK(m.loadFromFile(withUnknown, nullptr, nullptr, nullptr, nullptr, nullptr,
                            nullptr, nullptr, nullptr, nullptr, nullptr),
             "opaque: load ok");
    const AnalysisSnapshot s1 = m.snapshot();
    const ChannelData *op = nullptr;
    const auto it = s1.channels.constFind(QStringLiteral("opaque:CH01"));
    if (it != s1.channels.constEnd() && it->kind == ChannelData::Kind::Opaque)
        op = &(*it);
    V10CHECK(op != nullptr, "opaque: unknown channel captured");
    if (op) {
        V10CHECK(op->opaquePayload == QByteArray("FUTURE-CHANNEL-PAYLOAD-0123456789"),
                 "opaque: payload preserved");
        V10CHECK(op->opaqueTag == QByteArray("CH01", 4), "opaque: tag preserved");
    }
    V10CHECK(s1.lumRows().size() == 2 && s1.audioData().volume.size() == 4,
             "opaque: known channels unaffected");

    // 回写：opaque 块字节保全（stored 原样带回）
    const QString resaved = tmp.path() + QStringLiteral("/v10_unknown_resaved.vla");
    QFile::remove(resaved);
    V10CHECK(m.saveToFile(resaved, {QRect(10, 10, 60, 40)}, TimeCalibration()),
             "opaque: resave ok");
    QByteArray raw = v10ReadAll(resaved);
    QVector<V10Chunk> chunks;
    V10CHECK(v10Parse(raw, &chunks), "opaque: resaved parses");
    const V10Chunk *found = nullptr;
    for (const auto &c : chunks)
        if (c.tag == QByteArray("CH01", 4)) found = &c;
    V10CHECK(found != nullptr, "opaque: chunk written back");
    if (found) {
        V10CHECK(v10ChunkPayload(*found) == QByteArray("FUTURE-CHANNEL-PAYLOAD-0123456789"),
                 "opaque: payload byte-identical after roundtrip");
    }

    // 再载入：数据仍完整
    TimelineModel m2;
    V10CHECK(m2.loadFromFile(resaved, nullptr, nullptr, nullptr, nullptr, nullptr,
                             nullptr, nullptr, nullptr, nullptr, nullptr)
             && m2.snapshot().lumRows().size() == 2
             && m2.snapshot().channels.contains(QStringLiteral("opaque:CH01")),
             "opaque: second roundtrip intact");

    fprintf(stderr, "[v10-opaque] passthrough: %d checks, %d failures\n",
            g_v10Checks, g_v10Failures);
    return g_v10Failures;
}


int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    QStringList files = {
        // v3 老文件（无 data_entries，无 roi_id）
        QStringLiteral("C:/Users/MJ/Desktop/$RCR79YF/测试/02-49-10_6m.mp4.vla"),
        // v6 文件（entries 用全局顺序 id：rect=1, polygon=2，无 roi_id 字段）
        QStringLiteral("C:/Users/MJ/Desktop/20260722广州增城/监控视频/000631_100.mp4.vla"),
        // v7 文件（roi_id 齐全，rect+polygon 混合）
        QStringLiteral("C:/Users/MJ/Desktop/20260722广州增城/监控视频/2026-07-22厨房监控/05/20260722-050202.mp4.vla"),
        // v7 纯矩形
        QStringLiteral("C:/Users/MJ/Desktop/20260722广州增城/监控视频/D17_20260722052140_20260722_17015190.mp4.vla"),
    };
    // 允许命令行追加自定义 vla 路径；--peek:<path> 只做轻量校时读取
    for (int i = 1; i < argc; ++i) {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg.startsWith(QStringLiteral("--peek:"))) {
            const TimeCalibration cal = TimelineModel::peekCalibrationFromVla(arg.mid(7));
            fprintf(stderr, "[peek] %s -> source=%d offset=%lld dateKnown=%d "
                            "isValid=%d isEffective=%d\n",
                    qPrintable(arg.mid(7)), int(cal.source),
                    static_cast<long long>(cal.offsetMs), int(cal.dateKnown),
                    int(cal.isValid()), int(cal.isEffective()));
            continue;
        }
        files << arg;
    }

    int v10Fail = 0;
    v10Fail += testV10RoundTripAndChain();
    v10Fail += testV10UnknownChannelPassthrough();

    RoiModel rm;
    RoiModel pm;
    TimelineModel tm;
    ChartPanel chart;
    chart.setRegionModel(&rm);
    chart.setPolygonModel(&pm);
    chart.setTimelineModel(&tm);
    chart.setDuration(600000);
    chart.resize(1200, 400);

    int fail = 0;
    // ---- 并发写 vla 回归（用户实测：隐藏曲线后保存关闭案件重开丢失——）
    // 自动保存（后台线程）与手动保存（前台）并发写同一文件互相覆盖，
    // QSaveFile+全局锁修复后：并发写必须都完整落盘、重开可加载
    {
        QDir tmpDir2(QDir::tempPath() + QStringLiteral("/lumenarc_vla_conc"));
        tmpDir2.removeRecursively();
        QDir().mkpath(tmpDir2.path());
        const QString vla2 = tmpDir2.path() + QStringLiteral("/conc.vla");
        TimelineModel tmC;
        RoiModel rmC;
        rmC.addRegion(QRect(0, 0, 30, 30));
        rmC.addRegion(QRect(40, 40, 50, 50));
        {
            QVector<qint64> ts{0, 5000, 10000};
            QVector<QVector<qreal>> vals{{5, 50, 95}, {200, 100, 20}};
            QVector<DataEntry> entries{{DataEntry::Rect, rmC.roiIdAt(0)},
                                       {DataEntry::Rect, rmC.roiIdAt(1)}};
            tmC.setData(ts, vals, entries);
        }
        const QVector<QRect> regsC = rmC.regions();
        const QVector<int> idsC = rmC.roiIds();
        const TimeCalibration calC;
        // 8 轮并发写（模拟自动保存 vs 手动保存竞争）
        QAtomicInt concFail{0};
        QtConcurrent::run([&]() {
            for (int i = 0; i < 8; ++i)
                if (!tmC.saveToFile(vla2, regsC, calC, QRect(), {}, QRect(),
                                    {}, {}, {}, idsC, {}))
                    concFail.ref();
        });
        for (int i = 0; i < 8; ++i)
            if (!tmC.saveToFile(vla2, regsC, calC, QRect(), {}, QRect(),
                                {}, {}, {}, idsC, {}))
                concFail.ref();
        QThreadPool::globalInstance()->waitForDone(30000);
        // 重开验证：必须可加载且数据完整（非半文件）
        TimelineModel tmR2;
        QVector<QRect> rr2;
        QVector<QPolygon> pp2;
        QVector<GuideLine> gg2;
        QVector<int> ri2, pi2;
        TimeCalibration calR2;
        QRect mr2, pr2;
        QVector<ChartLabel> ll2;
        SnapshotFusionData ff2;
        const bool cLoad = tmR2.loadFromFile(vla2, &rr2, &calR2, &mr2, &ll2,
                                             &pr2, &ff2, &pp2, &gg2, &ri2, &pi2);
        const auto snapR = tmR2.snapshot();
        const bool cOk = cLoad && !snapR.isEmpty()
                         && snapR.values().size() == 2
                         && snapR.values()[0].size() == 3;
        fprintf(stderr, "[conc] concurrent writes fail=%d reload ok=%d => %s\n",
                int(concFail.loadRelaxed()), cLoad, cOk ? "PASS" : "FAIL <<<");
        if (!cOk || concFail.loadRelaxed() != 0) ++fail;
        tmpDir2.removeRecursively();
    }

    // ---- 用户实测回归：两 ROI 亮度曲线 → 隐藏 → 保存 → 重开 → 曲线丢失 ----
    {
        QDir tmpDir(QDir::tempPath() + QStringLiteral("/lumenarc_vla_reopen"));
        tmpDir.removeRecursively();
        QDir().mkpath(tmpDir.path());
        const QString vla = tmpDir.path() + QStringLiteral("/reopen.vla");

        // 1) 两个 ROI + 分析数据（两曲线）
        RoiModel rmS;
        rmS.addRegion(QRect(10, 10, 60, 40));
        rmS.addRegion(QRect(80, 20, 90, 60));
        TimelineModel tmS;
        ChartPanel chartS;
        chartS.setRegionModel(&rmS);
        chartS.setTimelineModel(&tmS);
        chartS.resize(1200, 400);
        chartS.show();
        {
            QVector<qint64> ts{0, 10000, 20000, 30000, 40000};
            QVector<QVector<qreal>> vals{{10, 90, 200, 60, 150},
                                         {220, 180, 40, 120, 80}};
            QVector<DataEntry> entries{{DataEntry::Rect, rmS.roiIdAt(0)},
                                       {DataEntry::Rect, rmS.roiIdAt(1)}};
            tmS.setData(ts, vals, entries);
        }
        app.processEvents();
        int series0 = 0;
        for (auto *s : chartS.chart()->series()) {
            if (auto *ls = qobject_cast<QLineSeries *>(s); ls && ls->count() > 0)
                ++series0;
        }
        const bool twoOk = series0 == 2;
        fprintf(stderr, "[reopen] initial curves=%d => %s\n",
                series0, twoOk ? "PASS" : "FAIL <<<");
        if (!twoOk) ++fail;

        // 2) 隐藏第一条曲线（legend marker 语义）
        for (auto *s : chartS.chart()->series()) {
            if (auto *ls = qobject_cast<QLineSeries *>(s); ls && ls->count() > 0) {
                ls->setVisible(false);
                break;
            }
        }
        app.processEvents();

        // 3) 保存 .vla（模拟分析完成自动保存/手动保存）
        const QVector<QRect> regs = rmS.regions();
        const QVector<int> rIds = rmS.roiIds();
        const TimeCalibration calS;
        bool saved = tmS.saveToFile(vla, regs, calS, QRect(), {}, QRect(),
                                    {}, {}, {}, rIds, {});
        fprintf(stderr, "[reopen] save=%s\n", saved ? "OK" : "FAIL <<<");
        if (!saved) ++fail;

        // 4) 重开（新模型 + 加载 + 恢复）
        RoiModel rmR;
        RoiModel pmR;
        TimelineModel tmR;
        ChartPanel chartR;
        chartR.setRegionModel(&rmR);
        chartR.setPolygonModel(&pmR);
        chartR.setTimelineModel(&tmR);
        chartR.resize(1200, 400);
        chartR.show();
        QVector<QRect> r2;
        QVector<QPolygon> p2;
        QVector<GuideLine> g2;
        QVector<int> rIds2, pIds2;
        TimeCalibration cal2;
        QRect mag2, pin2;
        QVector<ChartLabel> l2;
        SnapshotFusionData f2;
        const bool loaded = tmR.loadFromFile(vla, &r2, &cal2, &mag2, &l2, &pin2,
                                             &f2, &p2, &g2, &rIds2, &pIds2);
        if (rIds2.size() == r2.size())
            rmR.restoreRegions(r2, rIds2);
        app.processEvents();
        int seriesR = 0, visibleR = 0, totalS = 0;
        for (auto *s : chartR.chart()->series()) {
            auto *ls = qobject_cast<QLineSeries *>(s);
            ++totalS;
            if (ls && ls->count() > 0) {
                ++seriesR;
                if (ls->isVisible())
                    ++visibleR;
            } else {
                fprintf(stderr, "[reopen]   series %s empty/other\n",
                        ls ? qPrintable(ls->name()) : "?");
            }
        }
        fprintf(stderr, "[reopen]   total series=%d roiCount=%d entries=%d\n",
                totalS, rmR.regionCount(),
                int(tmR.snapshot().dataEntries().size()));
        const bool reopenOk = loaded && seriesR == 2 && visibleR == 2;
        fprintf(stderr, "[reopen] loaded=%d curves=%d visible=%d => %s\n",
                loaded, seriesR, visibleR, reopenOk ? "PASS" : "FAIL <<<");
        if (!reopenOk) ++fail;
        tmpDir.removeRecursively();
    }

    for (const QString &f : files) {
        fprintf(stderr, "=== %s\n", qPrintable(f));
        if (!QFile::exists(f)) {
            fprintf(stderr, "  (not found, skip)\n");
            continue;
        }
        int r = testFile(&chart, &rm, &pm, &tm, app, f);
        if (r == 0) fail++;
    }

    // ---- 音量曲线“短一截”回归（2026-08-13 根因修复）----
    // 场景复刻：短视频 B(52min) ↔ 长视频 A(4h，仅音频分析无亮度数据)。
    // 切回 A 时 dataReplaced 先到（ChartPanel::m_durationMs 仍是 B 的残留值），
    // 引擎 durationChanged(A) 随后才到 —— 音量曲线必须在两步后都铺满全程。
    {
        TimelineModel tmA;
        ChartPanel chartA;
        RoiModel rmA;
        chartA.setRegionModel(&rmA);
        chartA.setTimelineModel(&tmA);
        chartA.resize(1200, 400);

        const qint64 durB = 52 * 60 * 1000;
        const qint64 durA = qint64(4) * 3600 * 1000;
        const qreal resMs = 32.0;
        AudioData audio;
        audio.timeResolutionMs = resMs;
        const int n = int(durA / resMs);
        audio.volume.resize(n);
        for (int i = 0; i < n; ++i)
            audio.volume[i] = 0.5;

        chartA.setDuration(durB);            // 残留时长（上一视频 B）
        tmA.setData({}, {}, {}, audio);      // 切回 A：dataReplaced（stale duration）
        app.processEvents();

        auto volLastX = [&]() -> qreal {
            for (auto *s : chartA.chart()->series()) {
                auto *ls = qobject_cast<QLineSeries *>(s);
                if (ls && ls->name() == QStringLiteral("音量") && ls->count() > 0)
                    return ls->at(ls->count() - 1).x();
            }
            return -1;
        };
        const qreal tol = (n / 8000 + 1) * resMs * 2;   // 下采样 stride 容差
        const qreal x1 = volLastX();
        bool ok1 = x1 >= durA - tol;
        fprintf(stderr, "[bugA] after setData(stale durB): lastX=%.0f/%lld => %s\n",
                x1, (long long)durA, ok1 ? "PASS" : "FAIL <<<");

        chartA.setDuration(durA);            // 引擎 durationChanged(A) 到达
        app.processEvents();
        const qreal x2 = volLastX();
        bool ok2 = x2 >= durA - tol;
        fprintf(stderr, "[bugA] after setDuration(durA):  lastX=%.0f/%lld => %s\n",
                x2, (long long)durA, ok2 ? "PASS" : "FAIL <<<");
        if (!ok1 || !ok2)
            ++fail;
    }

    // ---- §14v2 快照离屏渲染（dock 内 resize+grab 不可靠的替代路径）----
    // ChartPanel::renderToImage：CPU 矢量重绘到目标尺寸；屏幕 widget 尺寸不动。
    {
        TimelineModel tm;
        ChartPanel chart;
        RoiModel rm2;
        chart.setRegionModel(&rm2);
        chart.setTimelineModel(&tm);
        chart.resize(800, 300);
        chart.show();

        QVector<qint64> ts{0, 30000, 60000, 90000, 120000};
        QVector<QVector<qreal>> vals{{10, 80, 200, 60, 150}};
        QVector<DataEntry> entries{DataEntry{DataEntry::Rect, 7}};
        AudioData audio;
        audio.timeResolutionMs = 1000.0;
        audio.volume.resize(120);
        for (int i = 0; i < 120; ++i)
            audio.volume[i] = (i % 30) / 29.0;
        tm.setData(ts, vals, entries, audio);
        chart.setDuration(120000);
        app.processEvents();

        // QChart::resize 必须同步重排 plotArea（renderToImage 依赖此机制）
        const qreal paBefore = chart.chart()->plotArea().width();
        chart.chart()->resize(QSizeF(2560, 420));
        const qreal paWide = chart.chart()->plotArea().width();
        chart.chart()->resize(QSizeF(800, 300));
        const bool layoutSync = paWide > 1800 && paBefore < 900;
        fprintf(stderr, "[snaprender] plotArea sync relayout: %.0f -> %.0f => %s\n",
                paBefore, paWide, layoutSync ? "PASS" : "FAIL <<<");
        if (!layoutSync) ++fail;

        const QSize onScreen = chart.size();
        const QImage img = chart.renderToImage(QSize(2560, 420));
        const bool sizeOk = (img.size() == QSize(2560, 420));
        const bool restored = (chart.size() == onScreen);
        fprintf(stderr, "[snaprender] chart renderToImage size=%dx%d restored=%d => %s\n",
                img.width(), img.height(), restored ? 1 : 0,
                (sizeOk && restored) ? "PASS" : "FAIL <<<");
        if (!sizeOk || !restored) ++fail;

        // 全宽内容断言：曲线/文字应铺满 2560 宽（若布局没切换，右侧为空）
        const QColor bg(Theme::BgPanel);
        auto nonBg = [&](const QImage &im, int x0, int x1, int y0, int y1) {
            int n = 0;
            for (int y = y0; y < y1; y += 2)
                for (int x = x0; x < x1; x += 2) {
                    const QRgb px = im.pixel(x, y);
                    if (qAbs(qRed(px) - bg.red()) + qAbs(qGreen(px) - bg.green())
                        + qAbs(qBlue(px) - bg.blue()) > 24)
                        ++n;
                }
            return n;
        };
        const int left = nonBg(img, 0, 640, 40, 400);
        const int right = nonBg(img, 1920, 2560, 40, 400);
        const bool spread = (left > 5 && right > 5);
        fprintf(stderr, "[snaprender] chart content spread L=%d R=%d => %s\n",
                left, right, spread ? "PASS" : "FAIL <<<");
        if (!spread) ++fail;

        // X 轴刻度轨/基线必须在底部 1/4 内（旧 bug：固定坐标刻度项不听
        // plotAreaChanged，resize 后残留在旧 plotArea.bottom 处 → 中部浮线）
        int bestRow = -1, bestHits = 0;
        for (int y = 0; y < 420; ++y) {
            int hits = 0;
            for (int x = 80; x < 2500; x += 4) {
                const QRgb px = img.pixel(x, y);
                if (qAbs(qRed(px) - 58) < 26 && qAbs(qGreen(px) - 65) < 26
                    && qAbs(qBlue(px) - 82) < 26)
                    ++hits;   // 基线色 (58,65,82)，AA 混合背景后半值亦纳入
            }
            if (hits > bestHits) { bestHits = hits; bestRow = y; }
        }
        const bool baselineOk = (bestRow > 315 && bestHits > 30);
        fprintf(stderr, "[snaprender] chart baseline row=%d/420 hits=%d => %s\n",
                bestRow, bestHits, baselineOk ? "PASS" : "FAIL <<<");
        if (!baselineOk) ++fail;
    }

    // SpectrogramPanelEnhanced::renderHeatmapImage：纯 CPU 光栅化（不经 GL）。
    {
        SpectrogramPanelEnhanced spec;
        AudioData audio;
        audio.sampleRate = 8000;
        audio.timeResolutionMs = 32.0;
        const int bins = 64, frames = 200;
        audio.spectrogram.resize(bins);
        for (int b = 0; b < bins; ++b) {
            audio.spectrogram[b].resize(frames);
            for (int f = 0; f < frames; ++f)
                // 频率越高能量越大（顶行亮、底行黑），时间维恒定
                audio.spectrogram[b][f] = -5.0 + 10.0 * b / (bins - 1);
        }
        audio.specMin = -5.0;
        audio.specMax = 5.0;
        spec.setSpectrogramData(audio);
        spec.setCursorTime(qint64(200 * 32 / 2));   // 中点光标

        const QImage img = spec.renderHeatmapImage(QSize(1000, 120));
        const bool sizeOk = (img.size() == QSize(1000, 120));
        fprintf(stderr, "[snaprender] spec renderHeatmapImage size=%dx%d => %s\n",
                img.width(), img.height(), sizeOk ? "PASS" : "FAIL <<<");
        if (!sizeOk) ++fail;

        if (sizeOk) {
            // 布局：左侧频率轴条 axisW = qBound(56, 1000/40=25, 96) = 56，
            // 热力图在 [56, 1000)，光标列 cx = 56 + int(0.5*(944-1)) = 527
            const int axisW = 56;
            auto rowMean = [&](int y0, int y1) {
                double s = 0; int n = 0;
                for (int y = y0; y < y1; ++y)
                    for (int x = axisW + 2; x < 1000; x += 4) {
                        const QRgb px = img.pixel(x, y);
                        s += qRed(px) + qGreen(px) + qBlue(px); ++n;
                    }
                return s / n;
            };
            const double top = rowMean(0, 20);       // 高频 = 亮
            const double bottom = rowMean(100, 120); // 低频 = 暗（thermal LUT 黑端）
            const bool orient = top > bottom * 2.0 + 10;
            fprintf(stderr, "[snaprender] spec freq orientation top=%.1f bottom=%.1f => %s\n",
                    top, bottom, orient ? "PASS" : "FAIL <<<");
            if (!orient) ++fail;

            // 频率轴条：左条应有刻度/标签像素（非纯黑）
            int axisHits = 0;
            for (int y = 0; y < 120; ++y)
                for (int x = 0; x < axisW - 1; ++x) {
                    const QRgb px = img.pixel(x, y);
                    if (qRed(px) + qGreen(px) + qBlue(px) > 60)
                        ++axisHits;
                }
            const bool axisOk = axisHits >= 20;
            fprintf(stderr, "[snaprender] spec freq axis labels hits=%d => %s\n",
                    axisHits, axisOk ? "PASS" : "FAIL <<<");
            if (!axisOk) ++fail;

            // 时间光标：中点列（轴条右侧偏移）应有橙色虚线像素（#FF981C）
            int cursorHits = 0;
            for (int y = 0; y < 120; ++y) {
                const QRgb px = img.pixel(527, y);
                if (qRed(px) > 200 && qGreen(px) > 100 && qGreen(px) < 210
                    && qBlue(px) < 80)
                    ++cursorHits;
            }
            const bool cursorOk = cursorHits >= 10;
            fprintf(stderr, "[snaprender] spec cursor line hits=%d => %s\n",
                    cursorHits, cursorOk ? "PASS" : "FAIL <<<");
            if (!cursorOk) ++fail;
        }
    }

    qInfo() << (fail == 0 ? "ALL PASS" : "FAILURES:") << (fail == 0 ? "" : QString::number(fail));
    return (fail + v10Fail + g_v10Failures) == 0 ? 0 : 1;
}
