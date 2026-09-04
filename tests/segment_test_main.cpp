/// @file segment_test_main.cpp
/// @brief P-68 选段导出套件：speedplan 纯函数（分段变速映射/帧计数/规范化）+
///        SegmentExportEngine 静态纯函数（atempo 级联/音频滤镜链/布局）+
///        caltest 素材端到端（导出产物时长/帧数/音轨校验）。
/// 环境：QT_QPA_PLATFORM=offscreen；工作目录须为仓库根（caltest 相对路径，
/// 无素材时自动跳过 e2e——与 libav_analysis_test 同惯例）。

extern "C" {
#include <libavformat/avformat.h>
}

#include <QApplication>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QJsonDocument>
#include <QtTest>


#include "domain/speed_plan.h"
#include "infrastructure/segment_export_engine.h"
#include "infrastructure/compose_render.h"
#include "infrastructure/tool_paths.h"
#include "domain/timeline_model.h"
#include "domain/sync_model.h"
#include <QTemporaryDir>
#include <QProcess>
#include <QPainter>
#include <QRegularExpression>
#include <cmath>

using speedplan::SpeedPlan;

static int g_checks = 0, g_failures = 0;
#define CHECK(cond, msg) do { \
    ++g_checks; \
    if (!(cond)) { ++g_failures; \
        qWarning() << "FAIL:" << msg << "@" << __LINE__; } \
} while (0)

// ---------------------------------------------------------------------------
// speedplan 纯函数
// ---------------------------------------------------------------------------
static void testNormalize()
{
    SpeedPlan p;
    p.aMs = 1000; p.bMs = 10000;
    p.splits = {5000, 3000, -5, 10000, 3000};   // 乱序+越界+重复
    p.rates = {2.0, 3.3};                       // 段数不足+非法倍率
    CHECK(p.normalize(), "normalize 报告改动");
    CHECK(p.splits == QVector<qint64>({3000, 5000}), "边界裁剪排序去重");
    CHECK(p.rates.size() == 3, "rates 补齐到段数");
    CHECK(qAbs(p.rates[0] - 2.0) < 1e-9, "合法倍率保留");
    CHECK(qAbs(p.rates[1] - 4.0) < 1e-9 || qAbs(p.rates[1] - 2.0) < 1e-9,
          "非法倍率收编最近档");
    CHECK(!p.normalize(), "二次 normalize 幂等");
}

static void testMapping()
{
    // 0..10s，3s 处分段：段0 rate 2（0..3s 源 → 0..1.5s 输出），段1 rate 1
    SpeedPlan p;
    p.aMs = 0; p.bMs = 10000; p.splits = {3000}; p.rates = {2.0, 1.0};
    p.normalize();
    CHECK(qAbs(p.outputDurationMs() - (1500.0 + 7000.0)) < 1e-6,
          "输出总长 = Σ len/rate");
    CHECK(qAbs(p.sourceMsAtOutputMs(0.0) - 0.0) < 1e-6, "输出 0 → 源 A");
    CHECK(qAbs(p.sourceMsAtOutputMs(1500.0) - 3000.0) < 1e-6, "段边界对齐");
    CHECK(qAbs(p.sourceMsAtOutputMs(750.0) - 1500.0) < 1e-6, "快放段中点");
    CHECK(qAbs(p.sourceMsAtOutputMs(1500.0 + 3500.0) - 6500.0) < 1e-6,
          "常速段中点");
    CHECK(qAbs(p.sourceMsAtOutputMs(99999.0) - 10000.0) < 1e-6, "越界夹取 B");
    CHECK(qAbs(p.rateAtOutputMs(100.0) - 2.0) < 1e-9, "段0 倍率");
    CHECK(qAbs(p.rateAtOutputMs(2000.0) - 1.0) < 1e-9, "段1 倍率");

    // 慢放：0..4s @0.25x → 输出 16s
    SpeedPlan slow;
    slow.aMs = 0; slow.bMs = 4000; slow.rates = {0.25};
    slow.normalize();
    CHECK(qAbs(slow.outputDurationMs() - 16000.0) < 1e-6, "慢放输出 4 倍长");
    CHECK(qAbs(slow.sourceMsAtOutputMs(8000.0) - 2000.0) < 1e-6, "慢放中点");

    // 帧计数：25fps、8.5s 输出 → ceil(212.5)=213
    SpeedPlan f;
    f.aMs = 0; f.bMs = 8500; f.rates = {1.0};
    f.normalize();
    CHECK(f.outputFrameCount(25.0) == 213, "帧数 ceil 覆盖尾帧");
}

static void testPlanFromLabels()
{
    const QVector<qint64> labels = {500, 3000, 7000, 99999};
    SpeedPlan p = speedplan::planFromLabels(1000, 10000, labels, 4.0);
    CHECK(p.splits == QVector<qint64>({3000, 7000}), "选段内标签成边界");
    CHECK(p.rates.size() == 3 && qAbs(p.rates[0] - 4.0) < 1e-9,
          "默认倍率填充");
}

// ---------------------------------------------------------------------------
// SegmentExportEngine 静态纯函数
// ---------------------------------------------------------------------------
static void testAtempoChain()
{
    const auto c1 = SegmentExportEngine::atempoChain(1.0);
    CHECK(c1.size() == 1 && c1[0].startsWith("atempo=1"), "1x 保底合法");
    const auto c025 = SegmentExportEngine::atempoChain(0.25);
    CHECK(c025.size() == 2
              && c025[0].startsWith("atempo=0.5") && c025[1].startsWith("atempo=0.5"),
          "0.25 级联两级 0.5");
    const auto c8 = SegmentExportEngine::atempoChain(8.0);
    CHECK(c8.size() == 3, "8x 级联三级");
    for (const QString &c : c8)
        CHECK(c.startsWith("atempo=2.0"), "8x 每级 2.0");
    CHECK(SegmentExportEngine::atempoChain(2.0).size() == 1, "2.0 单级");
}

static void testAudioFilterChain()
{
    SpeedPlan p;
    p.aMs = 1000; p.bMs = 9000; p.splits = {4000}; p.rates = {4.0, 1.0};
    p.normalize();
    const QString fc = SegmentExportEngine::buildAudioFilterChain(p, "1:a");
    CHECK(fc.contains("[1:a]atrim=start=1.000:end=4.000"), "段0 atrim");
    CHECK(fc.contains("atempo=2.0,atempo=2.0"), "段0 4x atempo 级联");
    CHECK(fc.contains("[1:a]atrim=start=4.000:end=9.000"), "段1 atrim");
    CHECK(fc.contains("[a0][a1]concat=n=2:v=0:a=1[aout]"), "concat 汇总");
}

static void testLayout()
{
    QRect v, c, sp;
    SegmentExportEngine::layoutRects({1920, 1080}, true, true, &v, &c, &sp);
    CHECK(v.height() > 600 && v.height() < 700, "双面板时视频区高度");
    CHECK(!c.isEmpty() && !sp.isEmpty() && c.bottom() < sp.top(),
          "曲线在上语谱在下");
    SegmentExportEngine::layoutRects({1920, 1080}, false, false, &v, &c, &sp);
    CHECK(v.height() >= 1050 && c.isEmpty() && sp.isEmpty(),
          "无数据面板隐藏视频全幅");
    SegmentExportEngine::layoutRects({1920, 1080}, false, true, &v, &c, &sp);
    CHECK(c.isEmpty() && !sp.isEmpty(), "仅语谱");
}

// ---------------------------------------------------------------------------
// caltest 素材端到端（无素材自动跳过）
// ---------------------------------------------------------------------------
static qint64 probeDurationMs(const QString &path, bool *hasAudio)
{
    AVFormatContext *fmt = nullptr;
    if (avformat_open_input(&fmt, path.toUtf8().constData(), nullptr, nullptr) < 0)
        return -1;
    avformat_find_stream_info(fmt, nullptr);
    if (hasAudio) {
        *hasAudio = false;
        for (unsigned i = 0; i < fmt->nb_streams; ++i)
            if (fmt->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO)
                *hasAudio = true;
    }
    const qint64 dur = fmt->duration > 0 ? fmt->duration / 1000 : -1;
    avformat_close_input(&fmt);
    return dur;
}

static void testEndToEnd()
{
    const QString src = QStringLiteral("build_tmp/caltest/basic.mp4");
    if (!QFile::exists(src)) {
        qWarning() << "SKIP e2e: no caltest asset";
        return;
    }
    // basic.mp4 = 320x240 5fps 10帧（2s）。选段 400..1400ms（1s 源），
    // 900ms 分段：段0 2x（500ms→250ms）、段1 0.5x（500ms→1000ms）→
    // 输出 1.25s @5fps → 7 帧
    SpeedPlan p;
    p.aMs = 400; p.bMs = 1400; p.splits = {900}; p.rates = {2.0, 0.5};
    p.normalize();
    const qint64 expectFrames = p.outputFrameCount(5.0);
    CHECK(expectFrames == 7, "e2e 预期帧数 7");

    const QString out = QStringLiteral("build_tmp/caltest/seg_export_test.mp4");
    QFile::remove(out);
    SegmentExportEngine::Params pp;
    pp.sourcePath = src;
    pp.outputPath = out;
    pp.plan = p;
    pp.outFps = 5.0;
    pp.canvas = QSize(640, 480);
    pp.burnOsd = true;

    SegmentExportEngine eng;
    QSignalSpy spy(&eng, &SegmentExportEngine::finished);
    eng.start(pp);
    const bool got = spy.wait(60000);
    CHECK(got, "e2e finished 信号");
    if (!got)
        return;
    const QList<QVariant> args = spy.takeFirst();
    if (!args.at(0).toBool()) {
        CHECK(false, QStringLiteral("e2e 导出失败：%1").arg(args.at(1).toString()));
        return;
    }
    CHECK(QFile::exists(out), "e2e 产物存在");
    bool hasAudio = false;
    const qint64 dur = probeDurationMs(out, &hasAudio);
    CHECK(dur > 0, "e2e 产物可探测");
    // 输出 1.25s：容器时长含取整/音频对齐余量，±400ms 容差
    CHECK(qAbs(dur - 1250) <= 400,
          QStringLiteral("e2e 产物时长≈1250ms（实测 %1）").arg(dur));
    QFile::remove(out);
}

// ---------------------------------------------------------------------------
// .vla 回环：A/B 选段 + 分速方案写入读回（拍板 Q5）
// ---------------------------------------------------------------------------
static void testVlaRoundtrip()
{
    QTemporaryDir dir;
    if (!dir.isValid()) { CHECK(false, "临时目录"); return; }
    const QString path = dir.filePath("t.vla");
    TimelineModel model;
    AbRegionData ab;
    ab.a = 12345; ab.b = 67890; ab.loop = true;
    SpeedPlan sp;
    sp.aMs = ab.a; sp.bMs = ab.b; sp.splits = {30000, 50000}; sp.rates = {4.0, 1.0, 0.25};
    sp.normalize();
    QVector<QRect> regions = {QRect(1, 2, 30, 40)};
    TimeCalibration cal;   // 空校时（isValid=false → 不写 time_calibration 键）
    CHECK(model.saveToFile(path, regions, cal, QRect(), {}, QRect(),
                           SnapshotFusionData(), {}, {}, {}, {}, ab, sp),
          "saveToFile 成功");
    TimelineModel m2;
    QVector<QRect> r2;
    AbRegionData ab2;
    SpeedPlan sp2;
    CHECK(m2.loadFromFile(path, &r2, nullptr, nullptr, nullptr, nullptr,
                          nullptr, nullptr, nullptr, nullptr, nullptr,
                          &ab2, &sp2), "loadFromFile 成功");
    CHECK(ab2.a == ab.a && ab2.b == ab.b && ab2.loop == ab.loop, "AB 回环");
    CHECK(sp2.aMs == sp.aMs && sp2.bMs == sp.bMs, "方案区间回环");
    CHECK(sp2.splits == sp.splits, "边界回环");
    CHECK(sp2.rates == sp.rates, "倍率回环");
}

// ---------------------------------------------------------------------------
// 多机模式（P-68 第 10 条）：音频墙钟→流内映射 + 双路 e2e
// ---------------------------------------------------------------------------
static SyncLaneData makeTestLane(const QString &path, qint64 offsetMs,
                                 qint64 durMs)
{
    SyncLaneData l;
    l.path = path;
    l.displayName = QFileInfo(path).completeBaseName();
    l.calibrated = true;
    l.cal.source = TimeCalibration::Source::Manual;
    l.cal.dateKnown = true;
    l.cal.offsetMs = offsetMs;
    l.cal.rate = 1.0;
    l.cal.rateApplied = false;
    l.durationMs = durMs;
    return l;
}

static void testMultiCamAudioMapping()
{
    // 两路：lane0 offset=100000（wall = stream + 100s），lane1 offset=200000
    const SyncLaneData l0 = makeTestLane("a.mp4", 100000, 60000);
    const SyncLaneData l1 = makeTestLane("b.mp4", 200000, 60000);
    // 墙钟选段 105000..125000：lane0 流内 5000..25000，lane1 流内越界(-95000)→不覆盖
    CHECK(syncStreamOf(l0, 105000) == 5000, "lane0 墙钟→流内");
    CHECK(syncStreamOf(l0, 125000) == 25000, "lane0 末端");
    CHECK(!syncLaneCovers(l1, 110000), "lane1 不覆盖该墙钟");
    CHECK(syncLaneCovers(l0, 110000), "lane0 覆盖");
    // Ranges 版音频链：显式区间 + 倍率
    const QString fc = SegmentExportEngine::buildAudioFilterChainRanges(
        {2.0, 1.0}, {{5000, 15000}, {15000, 25000}}, "1:a");
    CHECK(fc.contains("atrim=start=5.000:end=15.000"), "映射段0 atrim");
    CHECK(fc.contains("atrim=start=15.000:end=25.000"), "映射段1 atrim");
    CHECK(fc.contains("concat=n=2"), "concat n=2");
}

static void testMultiCamEndToEnd()
{
    const QString src = QStringLiteral("build_tmp/caltest/basic.mp4");
    if (!QFile::exists(src)) {
        qWarning() << "SKIP multicam e2e: no caltest asset";
        return;
    }
    // 双路同一素材、offset 0（墙钟=流内）：选段 400..1400ms 分速 2x/0.5x
    SpeedPlan p;
    p.aMs = 400; p.bMs = 1400; p.splits = {900}; p.rates = {2.0, 0.5};
    p.normalize();
    SegmentExportEngine::Params pp;
    pp.outputPath = QStringLiteral("build_tmp/caltest/mc_export_test.mp4");
    pp.plan = p;
    pp.outFps = 5.0;
    pp.canvas = QSize(640, 480);
    pp.burnOsd = true;
    pp.lanes = {makeTestLane(src, 0, 2000), makeTestLane(src, 0, 2000)};
    pp.audioLane = -1;   // basic.mp4 无音轨
    QFile::remove(pp.outputPath);

    SegmentExportEngine eng;
    QSignalSpy spy(&eng, &SegmentExportEngine::finished);
    eng.start(pp);
    if (!spy.wait(60000)) { CHECK(false, "多机 e2e 超时"); return; }
    const QList<QVariant> args = spy.takeFirst();
    if (!args.at(0).toBool()) {
        CHECK(false, QStringLiteral("多机 e2e 失败：%1").arg(args.at(1).toString()));
        return;
    }
    bool hasAudio = true;
    const qint64 dur = probeDurationMs(pp.outputPath, &hasAudio);
    CHECK(dur > 0 && qAbs(dur - 1250) <= 400,
          QStringLiteral("多机产物时长≈1250ms（实测 %1）").arg(dur));
    CHECK(!hasAudio, "无音轨（audioLane=-1）");
    QFile::remove(pp.outputPath);
}


static void testComposeHelpers()
{
    // 多段输出帧数（纯函数）
    SegmentExportEngine::Params::ComposeSeg seg;
    seg.inMs = 1000; seg.outMs = 11000; seg.rate = 1.0;
    CHECK(SegmentExportEngine::composeSegOutFrames(seg, 15.0) == 150,
          "compose 10s@1x@15fps=150 帧");
    seg.rate = 2.0;
    CHECK(SegmentExportEngine::composeSegOutFrames(seg, 15.0) == 75,
          "compose 10s@2x@15fps=75 帧");
    seg.rate = 0.5;
    CHECK(SegmentExportEngine::composeSegOutFrames(seg, 15.0) == 300,
          "compose 10s@0.5x@15fps=300 帧");
    seg.outMs = seg.inMs;
    CHECK(SegmentExportEngine::composeSegOutFrames(seg, 15.0) == 0, "空区间=0");

    // 多源音频链：段0 有音轨（标签 1:a）、段1 无音轨（anullsrc 补静）、段2 有音轨 2x
    const QString fc = SegmentExportEngine::buildAudioFilterChainMulti(
        {1.0, 1.0, 2.0}, {{1000, 6000}, {0, 3000}, {2000, 6000}},
        {"1:a", QString(), "2:a"});
    CHECK(fc.contains("[1:a]atrim=start=1.000:end=6.000"), "multi 段0 atrim");
    CHECK(fc.contains("anullsrc"), "multi 段1 anullsrc 补静");
    CHECK(fc.contains("atrim=start=0:end=3.000"), "multi 段1 静音长度=输出域 3s");
    CHECK(fc.contains("atempo=2.0"), "multi 段2 2x atempo");
    CHECK(fc.contains("aresample=48000:out_chlayout=stereo:osf=s16"), "multi 全分支归一 48k 立体声");
    CHECK(fc.contains("concat=n=3"), "multi concat n=3");

    // 证据清单 JSON（纯函数）
    QVector<SegmentExportEngine::Params::ComposeSeg> segs(1);
    segs[0].sourcePath = QStringLiteral("/cases/x/a.mp4");
    segs[0].inMs = 1000; segs[0].outMs = 5000;
    const QJsonObject m = SegmentExportEngine::buildEvidenceManifest(
        QStringLiteral("1.16.2"), QStringLiteral("CASE001"),
        QStringLiteral("张三"), QStringLiteral("某单位"),
        segs, {QStringLiteral("ab12")}, QStringLiteral("out.mp4"),
        QStringLiteral("cd34"), 12345);
    CHECK(m.value("kind").toString() == "evidence_segment_export", "manifest kind");
    CHECK(m.value("tool_version").toString() == "1.16.2", "manifest 版本");
    CHECK(m.value("operator").toObject().value("name").toString() == "张三",
          "manifest 签署人");
    CHECK(m.value("segments").toArray().size() == 1, "manifest 段数");
    CHECK(m.value("segments").toArray().first().toObject()
              .value("in_ms").toDouble() == 1000.0, "manifest 入点");
    CHECK(m.value("output").toObject().value("sha256").toString() == "cd34",
          "manifest 产物哈希");
    CHECK(m.value("integrity_note").toString().contains("像素零改动"),
          "manifest 完整性声明");
}

// ---------------------------------------------------------------------------
// 合成导出 P1 e2e：多段合成（演示模式）+ 证据直拷
// ---------------------------------------------------------------------------
static void testComposeEndToEnd()
{
    const QString src = QStringLiteral("build_tmp/caltest/basic.mp4");
    if (!QFile::exists(src)) {
        qWarning() << "SKIP compose e2e: no caltest asset";
        return;
    }
    // basic.mp4 = 320x240 5fps 2s。两段：0..1000ms + 500..1500ms → 输出 2s @5fps = 10 帧
    SegmentExportEngine::Params pp;
    SegmentExportEngine::Params::ComposeSeg s0; s0.sourcePath = src; s0.inMs = 0;   s0.outMs = 1000;
    SegmentExportEngine::Params::ComposeSeg s1; s1.sourcePath = src; s1.inMs = 500; s1.outMs = 1500;
    pp.segments = {s0, s1};
    pp.outputPath = QStringLiteral("build_tmp/caltest/compose_e2e.mp4");
    pp.outFps = 5.0;
    pp.canvas = QSize(640, 480);
    pp.burnOsd = true;          // 流内时间回落（无校正）
    pp.demoWatermark = true;    // 强制角标
    pp.caseLabel = QStringLiteral("TEST-CASE");
    QFile::remove(pp.outputPath);

    SegmentExportEngine eng;
    QSignalSpy spy(&eng, &SegmentExportEngine::finished);
    eng.start(pp);
    const bool got = spy.wait(90000);
    CHECK(got, "compose e2e finished 信号");
    if (!got) return;
    const QList<QVariant> args = spy.takeFirst();
    if (!args.at(0).toBool()) {
        CHECK(false, QStringLiteral("compose e2e 导出失败：%1").arg(args.at(1).toString()));
        return;
    }
    bool hasAudio = false;
    const qint64 dur = probeDurationMs(pp.outputPath, &hasAudio);
    CHECK(dur > 0, "compose e2e 产物可探测");
    CHECK(qAbs(dur - 2000) <= 600,
          QStringLiteral("compose e2e 时长≈2000ms（实测 %1）").arg(dur));
    CHECK(!hasAudio, "compose e2e 无音轨（basic.mp4 源无音频）");
    QFile::remove(pp.outputPath);
}

static void testEvidenceEndToEnd()
{
    const QString src = QStringLiteral("build_tmp/caltest/basic.mp4");
    if (!QFile::exists(src)) {
        qWarning() << "SKIP evidence e2e: no caltest asset";
        return;
    }
    SegmentExportEngine::Params pp;
    SegmentExportEngine::Params::ComposeSeg s0; s0.sourcePath = src; s0.inMs = 0;   s0.outMs = 1000;
    SegmentExportEngine::Params::ComposeSeg s1; s1.sourcePath = src; s1.inMs = 500; s1.outMs = 1500;
    pp.segments = {s0, s1};
    pp.evidenceCopy = true;
    pp.caseLabel = QStringLiteral("TEST-CASE");
    pp.operatorName = QStringLiteral("测试员");
    pp.operatorOrg = QStringLiteral("测试单位");
    pp.outputPath = QStringLiteral("build_tmp/caltest/evidence_e2e.mp4");
    QFile::remove(pp.outputPath);
    QFile::remove(pp.outputPath + QStringLiteral(".forensic.json"));

    SegmentExportEngine eng;
    QSignalSpy spy(&eng, &SegmentExportEngine::finished);
    eng.start(pp);
    const bool got = spy.wait(90000);
    CHECK(got, "evidence e2e finished 信号");
    if (!got) return;
    const QList<QVariant> args = spy.takeFirst();
    if (!args.at(0).toBool()) {
        CHECK(false, QStringLiteral("evidence e2e 导出失败：%1").arg(args.at(1).toString()));
        return;
    }
    bool hasAudio = false;
    const qint64 dur = probeDurationMs(pp.outputPath, &hasAudio);
    CHECK(dur > 0, "evidence e2e 产物可探测");
    // 直拷按关键帧对齐：小测试资产仅首帧是关键帧 → 实际区间大幅外扩属预期，
    // 只校验产物存在且时长落在源全长范围内（像素零改动由 -c copy 保证+侧车清单核验）
    CHECK(dur >= 1500 && dur <= 4200,
          QStringLiteral("evidence e2e 时长在关键帧对齐合理域（实测 %1）").arg(dur));
    // 侧车清单
    QFile jf(pp.outputPath + QStringLiteral(".forensic.json"));
    CHECK(jf.open(QIODevice::ReadOnly), "侧车清单存在");
    const QJsonDocument jd = QJsonDocument::fromJson(jf.readAll());
    CHECK(jd.object().value("kind").toString() == "evidence_segment_export", "侧车 kind");
    CHECK(jd.object().value("segments").toArray().size() == 2, "侧车 2 段");
    CHECK(jd.object().value("output").toObject().value("sha256").toString().length() == 64,
          "侧车含产物 SHA-256");
    QFile::remove(pp.outputPath);
    QFile::remove(pp.outputPath + QStringLiteral(".forensic.json"));
}

static void testComposeLanesEndToEnd()
{
    const QString src = QStringLiteral("build_tmp/caltest/basic.mp4");
    if (!QFile::exists(src)) {
        qWarning() << "SKIP lanes e2e: no caltest asset";
        return;
    }
    // 双路同素材、临时路 tempOffset=0（墙钟=流内）：宫格段 0..2000ms 墙钟
    auto mkLane = [&](const QString &id) {
        SyncLaneData l;
        l.id = id;
        l.path = src;
        l.displayName = id;
        l.temporary = true;   // 墙钟=流内
        l.durationMs = 2000;
        return l;
    };
    SegmentExportEngine::Params pp;
    SegmentExportEngine::Params::ComposeSeg seg;
    seg.lanes = {mkLane(QStringLiteral("C01")), mkLane(QStringLiteral("C02"))};
    seg.audioLane = 0;
    seg.inMs = 0; seg.outMs = 2000;
    pp.segments = {seg};
    pp.outputPath = QStringLiteral("build_tmp/caltest/compose_lanes_e2e.mp4");
    pp.outFps = 5.0;
    pp.canvas = QSize(640, 480);
    pp.burnOsd = false;
    pp.demoWatermark = true;
    QFile::remove(pp.outputPath);

    SegmentExportEngine eng;
    QSignalSpy spy(&eng, &SegmentExportEngine::finished);
    eng.start(pp);
    const bool got = spy.wait(90000);
    CHECK(got, "lanes e2e finished 信号");
    if (!got) return;
    const QList<QVariant> args = spy.takeFirst();
    if (!args.at(0).toBool()) {
        CHECK(false, QStringLiteral("lanes e2e 导出失败：%1").arg(args.at(1).toString()));
        return;
    }
    bool hasAudio = false;
    const qint64 dur = probeDurationMs(pp.outputPath, &hasAudio);
    CHECK(qAbs(dur - 2000) <= 600,
          QStringLiteral("lanes e2e 时长≈2000ms（实测 %1）").arg(dur));
    CHECK(!hasAudio, "lanes e2e 源无音轨");
    QFile::remove(pp.outputPath);
}

static void testComposeLanesValidation()
{
    // 证据模式拒绝宫格段
    {
        SegmentExportEngine::Params pp;
        SegmentExportEngine::Params::ComposeSeg seg;
        SyncLaneData l; l.path = QStringLiteral("x.mp4"); l.temporary = true;
        seg.lanes = {l, l}; seg.inMs = 0; seg.outMs = 1000;
        pp.segments = {seg};
        pp.evidenceCopy = true;
        pp.outputPath = QStringLiteral("build_tmp/caltest/never.mp4");
        SegmentExportEngine eng;
        QSignalSpy spy(&eng, &SegmentExportEngine::finished);
        eng.start(pp);
        QCoreApplication::processEvents();
        CHECK(spy.count() >= 1 && !spy.first().at(0).toBool(),
              "证据模式拒绝宫格段");
    }
    // 未校时非临时路 → 拒绝
    {
        SegmentExportEngine::Params pp;
        SegmentExportEngine::Params::ComposeSeg seg;
        SyncLaneData l; l.path = QStringLiteral("x.mp4");   // calibrated=false temporary=false
        seg.lanes = {l, l}; seg.inMs = 0; seg.outMs = 1000;
        pp.segments = {seg};
        pp.outputPath = QStringLiteral("build_tmp/caltest/never.mp4");
        pp.outFps = 5.0; pp.canvas = QSize(320, 240);
        SegmentExportEngine eng;
        QSignalSpy spy(&eng, &SegmentExportEngine::finished);
        eng.start(pp);
        QCoreApplication::processEvents();
        CHECK(spy.count() >= 1 && !spy.first().at(0).toBool(),
              "未校时非临时路拒绝");
    }
    // 路数越界 → 拒绝
    {
        SegmentExportEngine::Params pp;
        SegmentExportEngine::Params::ComposeSeg seg;
        SyncLaneData l; l.path = QStringLiteral("x.mp4"); l.temporary = true;
        seg.lanes = {l};   // 仅 1 路
        seg.inMs = 0; seg.outMs = 1000;
        pp.segments = {seg};
        pp.outputPath = QStringLiteral("build_tmp/caltest/never.mp4");
        pp.outFps = 5.0; pp.canvas = QSize(320, 240);
        SegmentExportEngine eng;
        QSignalSpy spy(&eng, &SegmentExportEngine::finished);
        eng.start(pp);
        QCoreApplication::processEvents();
        CHECK(spy.count() >= 1 && !spy.first().at(0).toBool(), "单路宫格拒绝");
    }
}

static void msgToFile(QtMsgType, const QMessageLogContext &, const QString &msg)
{
    QFile f(QStringLiteral("build_tmp/segment_test_out.log"));
    if (f.open(QIODevice::Append | QIODevice::Text)) {
        f.write(msg.toUtf8());
        f.write("\n");
    }
}

static void testComposeOverlay()
{
    // 造 .vla（2 ROI + 亮度行 + 音量 + 标签）→ loadComposeOverlay 回环
    const QString vla = QStringLiteral("build_tmp/caltest/overlay_test.vla");
    TimelineModel model;
    AnalysisSnapshot snap;
    QVector<qint64> ts;
    for (int i = 0; i <= 200; ++i) ts << i * 10;   // 0..2000ms
    QVector<QVector<qreal>> rows(2);
    for (int i = 0; i <= 200; ++i) {
        rows[0] << qreal(50 + 40 * sin(i * 0.1));
        rows[1] << qreal(100 + 30 * cos(i * 0.07));
    }
    DataEntry e0; e0.type = DataEntry::Rect; e0.roiId = 0;
    DataEntry e1; e1.type = DataEntry::Rect; e1.roiId = 1;
    snap.setLuminance(ts, rows, {e0, e1});
    AudioData audio;
    audio.timeResolutionMs = 20.0;
    for (int i = 0; i <= 100; ++i) audio.volume << qreal(0.3 + 0.2 * sin(i * 0.2));
    snap.setAudio(audio);
    model.setSnapshot(snap);
    QVector<QRect> regions = {QRect(10, 10, 100, 60), QRect(50, 80, 80, 50)};
    QVector<ChartLabel> labels = {ChartLabel{500, QStringLiteral("起火"), Qt::red}};
    CHECK(model.saveToFile(vla, regions, TimeCalibration(), QRect(), labels),
          "造 overlay vla");

    ComposeOverlay ov = loadComposeOverlay(vla);
    CHECK(ov.loaded, "overlay 载入");
    CHECK(ov.rois.size() == 2 && ov.hasLum && ov.hasVol && ov.labels.size() == 1,
          "overlay 内容齐备");

    // 渲染冒烟：条带游标列有白像素、曲线区非全黑
    QImage img(800, 200, QImage::Format_RGBA8888);
    img.fill(Qt::black);
    {
        QPainter p(&img);
        drawChartStrip(p, QRect(0, 0, 800, 200), ov, 1000);
    }
    const QRect plot = QRect(0, 0, 800, 200).adjusted(48, 8, -10, 18);
    const int cx = plot.x() + int((1000 - (1000 - 20000)) / 30000.0 * plot.width());
    bool cursorWhite = false, curveInk = false;
    for (int y = plot.top(); y < plot.bottom(); ++y) {
        const QColor c = img.pixelColor(cx, y);
        if (c.red() > 200 && c.green() > 200 && c.blue() > 200) cursorWhite = true;
    }
    for (int x = plot.left(); x < plot.right() && !curveInk; x += 3)
        for (int y = plot.top(); y < plot.bottom(); ++y) {
            const QColor c = img.pixelColor(x, y);
            if (c.green() > 100 && c.red() < 120) { curveInk = true; break; }   // 音量绿
        }
    CHECK(cursorWhite, "条带游标白线");
    CHECK(curveInk, "条带音量曲线上墨");

    // ROI 叠加冒烟：R1 内部像素带色
    QImage frame(320, 240, QImage::Format_RGBA8888);
    frame.fill(Qt::black);
    {
        QPainter p(&frame);
        drawRoiOverlay(p, QRect(0, 0, 320, 240), QSize(320, 240), ov);
    }
    CHECK(frame.pixelColor(60, 40) != QColor(0, 0, 0), "ROI R1 上墨");
    CHECK(frame.pixelColor(300, 220) == QColor(0, 0, 0), "ROI 外不染色");
    QFile::remove(vla);
}

static void testComposeOverlayEndToEnd()
{
    const QString src = QStringLiteral("build_tmp/caltest/basic.mp4");
    if (!QFile::exists(src)) {
        qWarning() << "SKIP overlay e2e: no caltest asset";
        return;
    }
    // 造源配套 .vla（同 testComposeOverlay）
    const QString vla = QStringLiteral("build_tmp/caltest/overlay_e2e.vla");
    TimelineModel model;
    AnalysisSnapshot snap;
    QVector<qint64> ts;
    for (int i = 0; i <= 200; ++i) ts << i * 10;
    QVector<QVector<qreal>> rows(1);
    for (int i = 0; i <= 200; ++i) rows[0] << qreal(60 + 50 * sin(i * 0.08));
    DataEntry e0; e0.type = DataEntry::Rect; e0.roiId = 0;
    snap.setLuminance(ts, rows, {e0});
    model.setSnapshot(snap);
    QVector<QRect> regions = {QRect(20, 20, 120, 80)};
    CHECK(model.saveToFile(vla, regions, TimeCalibration(), QRect(), {}),
          "造 e2e vla");

    SegmentExportEngine::Params pp;
    SegmentExportEngine::Params::ComposeSeg seg;
    seg.sourcePath = src; seg.inMs = 0; seg.outMs = 2000;
    seg.burnRoi = true; seg.burnChart = true;
    pp.segments = {seg};
    pp.outputPath = QStringLiteral("build_tmp/caltest/compose_overlay_e2e.mp4");
    pp.outFps = 5.0;
    pp.canvas = QSize(640, 480);
    pp.demoWatermark = true;
    pp.vlaPathByPath.insert(src, vla);
    QFile::remove(pp.outputPath);

    SegmentExportEngine eng;
    QSignalSpy spy(&eng, &SegmentExportEngine::finished);
    eng.start(pp);
    const bool got = spy.wait(90000);
    CHECK(got, "overlay e2e finished");
    if (!got) return;
    if (!spy.first().at(0).toBool()) {
        CHECK(false, QStringLiteral("overlay e2e 失败：%1")
                         .arg(spy.first().at(1).toString()));
        return;
    }
    // 抽帧验证：底部条带非全黑（曲线上墨）
    const QString frame = QStringLiteral("build_tmp/caltest/overlay_e2e_f.png");
    QProcess proc;
    proc.start(ToolPaths::findFfmpegPath(),
               {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
                QStringLiteral("-ss"), QStringLiteral("1"),
                QStringLiteral("-i"), pp.outputPath,
                QStringLiteral("-frames:v"), QStringLiteral("1"), frame});
    proc.waitForFinished(30000);
    QImage shot(frame);
    CHECK(!shot.isNull(), "抽帧成功");
    if (!shot.isNull()) {
        // 底部 150px 条带区：存在非黑像素（曲线/标签/游标）
        bool stripInk = false;
        const int sy0 = shot.height() - 150;
        for (int y = sy0; y < shot.height() && !stripInk; y += 4)
            for (int x = 50; x < shot.width() - 10; x += 6)
                if (shot.pixelColor(x, y).lightness() > 40) {
                    stripInk = true; break;
                }
        CHECK(stripInk, "产物底部曲线滚动条上墨");
    }
    QFile::remove(frame);
    QFile::remove(pp.outputPath);
    QFile::remove(vla);
}

static void testComposeRealAssetEndToEnd()
{
    // 真实病灶素材（PTS 抖动族 LAMerged 91min/15fps/8kHz AAC mono）过合成管线：
    // 两段 5s+5s → 输出 10s；存在才跑（本机案件素材，CI 无则 SKIP）
    const QString src = QStringLiteral(
        "build/Release/cases/20260722-广州增城-a-20260722增城火灾/preprocess/"
        "20260902_162648/LAMerged_02-04-52_6m_03-39-11.mp4");
    if (!QFile::exists(src)) {
        qWarning() << "SKIP realasset e2e: no LAMerged asset";
        return;
    }
    SegmentExportEngine::Params pp;
    SegmentExportEngine::Params::ComposeSeg s0; s0.sourcePath = src; s0.inMs = 60000;  s0.outMs = 65000;
    SegmentExportEngine::Params::ComposeSeg s1; s1.sourcePath = src; s1.inMs = 120000; s1.outMs = 125000; s1.rate = 2.0;
    pp.segments = {s0, s1};
    pp.outputPath = QStringLiteral("build_tmp/caltest/compose_realasset_e2e.mp4");
    pp.outFps = 15.0;
    pp.canvas = QSize(1280, 720);
    pp.burnOsd = true;
    pp.demoWatermark = true;
    pp.caseLabel = QStringLiteral("增城回归");
    QFile::remove(pp.outputPath);

    SegmentExportEngine eng;
    QSignalSpy spy(&eng, &SegmentExportEngine::finished);
    eng.start(pp);
    const bool got = spy.wait(120000);
    CHECK(got, "realasset e2e finished");
    if (!got) return;
    if (!spy.first().at(0).toBool()) {
        CHECK(false, QStringLiteral("realasset e2e 失败：%1")
                         .arg(spy.first().at(1).toString()));
        return;
    }
    // 预期输出：5s@1x + 5s@2x=2.5s → 7.5s
    bool hasAudio = false;
    const qint64 dur = probeDurationMs(pp.outputPath, &hasAudio);
    CHECK(qAbs(dur - 7500) <= 900,
          QStringLiteral("realasset e2e 时长≈7500ms（实测 %1）").arg(dur));
    CHECK(hasAudio, "realasset e2e 有音轨（8kHz 源经归一化）");
    QFile::remove(pp.outputPath);
}

static void testAudioChainV2()
{
    using SEE = SegmentExportEngine;
    // 单片有源：与旧 multi 形态一致（直出 [a0]）
    const QString one = SEE::buildAudioFilterChainV2(
        {{SEE::AudioSegPart{QStringLiteral("1:a"), 1000, 3000, 1.0}}});
    CHECK(one.contains(QStringLiteral("[1:a]atrim=start=1.000:end=3.000")), "V2 单片有源 atrim");
    CHECK(one.contains(QStringLiteral("concat=n=1")), "V2 单段总拼");
    // 部分覆盖三片：静音头 + 有源中(2x) + 静音尾
    const QString part = SEE::buildAudioFilterChainV2({{
        SEE::AudioSegPart{QString(), 0, 1000, 2.0},
        SEE::AudioSegPart{QStringLiteral("2:a"), 60000, 62500, 2.0},
        SEE::AudioSegPart{QString(), 3500, 5000, 2.0}}});
    CHECK(part.contains(QStringLiteral("anullsrc=r=48000:cl=stereo,atrim=start=0:end=0.500")),
          "V2 静音头 1000ms/2x=0.5s");
    CHECK(part.contains(QStringLiteral("[2:a]atrim=start=60.000:end=62.500")), "V2 中片取流");
    CHECK(part.contains(QStringLiteral("atempo=2.0")), "V2 中片变速");
    CHECK(part.contains(QStringLiteral("atrim=start=0:end=0.750")),
          "V2 静音尾 1500ms/2x=0.75s");
    CHECK(part.contains(QStringLiteral("concat=n=3:v=0:a=1[as0]")), "V2 段内三拼");
    CHECK(part.contains(QStringLiteral("[as0]concat=n=1:v=0:a=1[aout]")), "V2 段间总拼");
}

/// 抽产物某窗口音频 RMS（dB）；静音 → -inf（返回 -120）
static double probeRmsDb(const QString &path, double startS, double durS)
{
    QProcess proc;
    proc.start(ToolPaths::findFfmpegPath(),
               {QStringLiteral("-v"), QStringLiteral("info"),
                QStringLiteral("-ss"), QString::number(startS),
                QStringLiteral("-t"), QString::number(durS),
                QStringLiteral("-i"), path,
                QStringLiteral("-af"), QStringLiteral("astats"),
                QStringLiteral("-f"), QStringLiteral("null"), QStringLiteral("-")});
    proc.waitForFinished(60000);
    const QString out = QString::fromLocal8Bit(proc.readAllStandardError());
    static const QRegularExpression re(QStringLiteral("RMS level dB:\\s*(-?[0-9.]+|-inf)"));
    const auto m = re.match(out);
    if (!m.hasMatch())
        return -120.0;
    const QString v = m.captured(1);
    return v == QStringLiteral("-inf") ? -120.0 : v.toDouble();
}

static void testComposeLanesPartialAudioEndToEnd()
{
    // 部分覆盖细分 e2e：主听路（LAMerged 8kHz 有声）只盖段前半，后半静音
    const QString la = QStringLiteral(
        "build/Release/cases/20260722-广州增城-a-20260722增城火灾/preprocess/"
        "20260902_162648/LAMerged_02-04-52_6m_03-39-11.mp4");
    const QString basic = QStringLiteral("build_tmp/caltest/basic.mp4");
    if (!QFile::exists(la) || !QFile::exists(basic)) {
        qWarning() << "SKIP partial-audio e2e: assets missing";
        return;
    }
    SyncLaneData l0;
    l0.id = QStringLiteral("A"); l0.path = la; l0.displayName = QStringLiteral("A");
    l0.temporary = true; l0.durationMs = 2500;   // 只盖 [0,2500)
    SyncLaneData l1;
    l1.id = QStringLiteral("B"); l1.path = basic; l1.displayName = QStringLiteral("B");
    l1.temporary = true; l1.durationMs = 2000;
    SegmentExportEngine::Params pp;
    SegmentExportEngine::Params::ComposeSeg seg;
    seg.lanes = {l0, l1};
    seg.audioLane = 0;
    seg.inMs = 0; seg.outMs = 4000;
    pp.segments = {seg};
    pp.outputPath = QStringLiteral("build_tmp/caltest/compose_partial_audio.mp4");
    pp.outFps = 5.0;
    pp.canvas = QSize(640, 480);
    pp.demoWatermark = true;
    QFile::remove(pp.outputPath);

    SegmentExportEngine eng;
    QSignalSpy spy(&eng, &SegmentExportEngine::finished);
    eng.start(pp);
    const bool got = spy.wait(120000);
    CHECK(got, "partial-audio e2e finished");
    if (!got) return;
    if (!spy.first().at(0).toBool()) {
        CHECK(false, QStringLiteral("partial-audio e2e 失败：%1")
                         .arg(spy.first().at(1).toString()));
        return;
    }
    bool hasAudio = false;
    const qint64 dur = probeDurationMs(pp.outputPath, &hasAudio);
    CHECK(qAbs(dur - 4000) <= 900,
          QStringLiteral("partial-audio 时长≈4000ms（实测 %1）").arg(dur));
    CHECK(hasAudio, "partial-audio 有音轨");
    // 前半（主听路覆盖）应有声，后半（盲区）应静音：RMS 差 ≥15dB
    const double rCovered = probeRmsDb(pp.outputPath, 0.8, 1.2);
    const double rSilent = probeRmsDb(pp.outputPath, 3.0, 0.9);
    CHECK(rCovered > -85.0,
          QStringLiteral("覆盖区非死寂（RMS %1 dB；监控源本身音量低）").arg(rCovered));
    CHECK(rCovered - rSilent >= 25.0,
          QStringLiteral("盲区显著静音（覆盖 %1 / 盲区 %2 dB）").arg(rCovered).arg(rSilent));
    QFile::remove(pp.outputPath);
}

static void testComposeAnnoEndToEnd()
{
    const QString src = QStringLiteral("build_tmp/caltest/basic.mp4");
    if (!QFile::exists(src)) {
        qWarning() << "SKIP anno e2e: no caltest asset";
        return;
    }
    using SEE = SegmentExportEngine;
    SEE::Params pp;
    SEE::Params::ComposeSeg seg;
    seg.sourcePath = src; seg.inMs = 0; seg.outMs = 2000;
    SEE::Params::ComposeAnno cap;   // 全程字幕
    cap.type = SEE::Params::ComposeAnno::Caption;
    cap.inMs = 0; cap.outMs = 2000; cap.text = QStringLiteral("FIRE POINT A");   // ASCII（offscreen 无 CJK 字体防豆腐块）
    SEE::Params::ComposeAnno arr;   // 0.6s 起红箭头
    arr.type = SEE::Params::ComposeAnno::Arrow;
    arr.inMs = 600; arr.outMs = 2000;
    arr.rect = QRectF(0.2, 0.2, 0.5, 0.5);
    arr.colorRgb = 0xff6060;
    SEE::Params::ComposeAnno spot;  // 1.0s 起聚光灯（中央 50%）
    spot.type = SEE::Params::ComposeAnno::Spotlight;
    spot.inMs = 1000; spot.outMs = 2000;
    spot.rect = QRectF(0.25, 0.25, 0.5, 0.5);
    seg.annos = {cap, arr, spot};
    pp.segments = {seg};
    pp.outputPath = QStringLiteral("build_tmp/caltest/compose_anno_e2e.mp4");
    pp.outFps = 5.0;
    pp.canvas = QSize(640, 480);
    pp.demoWatermark = false;
    QFile::remove(pp.outputPath);

    SEE eng;
    QSignalSpy spy(&eng, &SEE::finished);
    eng.start(pp);
    const bool got = spy.wait(90000);
    CHECK(got, "anno e2e finished");
    if (!got) return;
    if (!spy.first().at(0).toBool()) {
        CHECK(false, QStringLiteral("anno e2e 失败：%1").arg(spy.first().at(1).toString()));
        return;
    }
    auto grab = [&](double tS, const QString &out) {
        QProcess proc;
        proc.start(ToolPaths::findFfmpegPath(),
                   {QStringLiteral("-y"), QStringLiteral("-v"), QStringLiteral("error"),
                    QStringLiteral("-i"), pp.outputPath,
                    QStringLiteral("-ss"), QString::number(tS),
                    QStringLiteral("-frames:v"), QStringLiteral("1"), out});
        proc.waitForFinished(30000);
        return QImage(out);
    };
    const QString dir = QStringLiteral("build_tmp/caltest/");
    const QImage f03 = grab(0.3, dir + "anno_f03.png");   // 仅字幕
    const QImage f16 = grab(1.6, dir + "anno_f16.png");   // 字幕+箭头+聚光灯变暗峰值（dim 满）
    const QImage f18 = grab(1.8, dir + "anno_f18.png");   // 聚光灯接近放满
    CHECK(!f03.isNull() && !f16.isNull() && !f18.isNull(), "anno 抽帧成功");
    if (!f03.isNull()) {
        // 字幕：底部黑带区有亮色文字像素（全宽扫，防栅格采样错过 1px 笔画）
        int white = 0;
        for (int x = 0; x < 640; x += 2)
            for (int y = 405; y < 432; ++y)
                if (f03.pixelColor(x, y).lightness() > 150) ++white;
        CHECK(white > 50, QStringLiteral("字幕上墨（亮像素 %1）").arg(white));
    }
    if (!f16.isNull()) {
        // 红箭头像素
        int red = 0;
        for (int x = 0; x < 640; x += 2)
            for (int y = 0; y < 480; y += 2) {
                const QColor c = f16.pixelColor(x, y);
                if (c.red() > 170 && c.green() < 130 && c.blue() < 130) ++red;
            }
        CHECK(red > 40, QStringLiteral("箭头上墨（红像素 %1）").arg(red));
        // 聚光灯变暗峰值（1.6s 处 dim 满 145）：四角显著压暗
        if (!f03.isNull()) {
            auto cornerMean = [](const QImage &im) {
                double s = 0; int n = 0;
                for (int x = 4; x < 40; x += 4)
                    for (int y = 4; y < 40; y += 4) { s += im.pixelColor(x, y).lightness(); ++n; }
                return s / qMax(1, n);
            };
            CHECK(cornerMean(f16) < cornerMean(f03) - 20,
                  QStringLiteral("聚光灯半程压暗四角（%1 vs %2）")
                      .arg(cornerMean(f16)).arg(cornerMean(f03)));
        }
    }
    if (!qEnvironmentVariableIsSet("KEEP_ANNO_FRAMES"))
        for (const QString &f : {dir + "anno_f03.png", dir + "anno_f16.png", dir + "anno_f18.png"})
            QFile::remove(f);
    if (!qEnvironmentVariableIsSet("KEEP_ANNO_FRAMES"))
        QFile::remove(pp.outputPath);
}

int main(int argc, char **argv)
{
    QFile::remove(QStringLiteral("build_tmp/segment_test_out.log"));
    qInstallMessageHandler(msgToFile);
    QApplication app(argc, argv);   // QImage+drawText 需 GUI 应用上下文（字体子系统）
    testNormalize();
    testMapping();
    testPlanFromLabels();
    testAtempoChain();
    testAudioFilterChain();
    testLayout();
    testVlaRoundtrip();
    testMultiCamAudioMapping();
    testComposeHelpers();
    testComposeEndToEnd();
    testEvidenceEndToEnd();
    testComposeLanesEndToEnd();
    testComposeLanesValidation();
    testComposeOverlay();
    testComposeOverlayEndToEnd();
    testComposeRealAssetEndToEnd();
    testAudioChainV2();
    testComposeLanesPartialAudioEndToEnd();
    testComposeAnnoEndToEnd();
    testMultiCamEndToEnd();
    testEndToEnd();
    qInfo() << "segment_test:" << g_checks << "checks," << g_failures << "failures";
    return g_failures == 0 ? 0 : 1;
}
