/**
 * @file vla_load_test_main.cpp
 * @brief 离屏复现 .vla 加载→区域恢复→曲线重建全链路，验证各版本 vla 文件
 *
 * 模拟 MainWindow::openVideoFile 的 vla-cache 加载序列：
 *   clearData → loadFromFile(setData→dataReplaced 同步触发) → restoreRegions/restorePolygons
 *   → 事件循环跑 deferred rebuildSeries → 统计曲线数量与数据点
 */
#include <QApplication>
#include <QDebug>
#include <QDir>
#include <QtCharts/QLineSeries>
#include "chartpanel.h"
#include "domain/region_model.h"
#include "domain/polygon_model.h"
#include "domain/timeline_model.h"

static int testFile(ChartPanel *chart, RegionModel *rm, PolygonModel *pm,
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

    bool ok = tm->loadFromFile(path, &regions, &calibration, &magRect, &labels,
                               &pinnedRect, &fusion, &polys, &guideLines, &rIds, &pIds);
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
            (long long)tm->snapshot().dataEntries.size(),
            (long long)tm->snapshot().timestamps.size(),
            (long long)tm->snapshot().values.size());

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
    const QVector<QVector<qreal>> oldVals = tm->snapshot().values;
    QString tmp = QDir::temp().filePath("vla2_roundtrip_test.vla");
    QFile::remove(tmp);
    TimeCalibration saveCal = TimeCalibration::fromLegacyOffset(12345);
    bool saved = tm->saveToFile(tmp, rm->regions(), saveCal, QRect(1, 2, 3, 4), {},
                                QRect(), SnapshotFusionData(), pm->polygons(), {},
                                rm->roiIds(), pm->roiIds());
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
            bool dataOk = (snap.timestamps == oldTs) && (snap.values.size() == oldVals.size());
            if (dataOk) {
                for (int r = 0; r < oldVals.size() && dataOk; ++r) {
                    if (snap.values[r].size() != oldVals[r].size()) { dataOk = false; break; }
                    for (int c = 0; c < oldVals[r].size(); ++c)
                        if (qAbs(snap.values[r][c] - oldVals[r][c]) > 1e-3) { dataOk = false; break; }
                }
            }
            // roiId 保持：VLA2 必须原样带回
            bool idsOk = (rIds2 == rm->roiIds()) && (pIds2 == pm->roiIds());
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
        const auto a3 = tm3.snapshot().audio;
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
    // 允许命令行追加自定义 vla 路径
    for (int i = 1; i < argc; ++i)
        files << QString::fromLocal8Bit(argv[i]);

    RegionModel rm;
    PolygonModel pm;
    TimelineModel tm;
    ChartPanel chart;
    chart.setRegionModel(&rm);
    chart.setPolygonModel(&pm);
    chart.setTimelineModel(&tm);
    chart.setDuration(600000);
    chart.resize(1200, 400);

    int fail = 0;
    for (const QString &f : files) {
        fprintf(stderr, "=== %s\n", qPrintable(f));
        if (!QFile::exists(f)) {
            fprintf(stderr, "  (not found, skip)\n");
            continue;
        }
        int r = testFile(&chart, &rm, &pm, &tm, app, f);
        if (r == 0) fail++;
    }
    qInfo() << (fail == 0 ? "ALL PASS" : "FAILURES:") << (fail == 0 ? "" : QString::number(fail));
    return fail == 0 ? 0 : 1;
}
