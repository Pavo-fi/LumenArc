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
#include "displayadjust.h"          // 放大镜显示链 LUT（导出右半同表）
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

// ---------------------------------------------------------------------------
// 放大镜同框导出（v1.18.x 所见即所得）：右半 = 放大镜显示链（裁剪→旋转→LUT）
// ---------------------------------------------------------------------------
/// 抽产物某时刻帧（PNG → QImage；失败返回空图）
static QImage grabFrameAt(const QString &path, double atSec)
{
    QTemporaryDir dir;
    if (!dir.isValid())
        return QImage();
    const QString png = dir.path() + QStringLiteral("/f.png");
    QProcess p;
    p.start(ToolPaths::findFfmpegPath(),
            {QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-ss"), QString::number(atSec, 'f', 3),
             QStringLiteral("-i"), path,
             QStringLiteral("-frames:v"), QStringLiteral("1"),
             QStringLiteral("-y"), png});
    if (!p.waitForFinished(60000) || !QFile::exists(png))
        return QImage();
    return QImage(png);
}

/// 区域平均亮度（0..255；空区域返回 -1）
static double regionMeanLuma(const QImage &img, const QRect &r)
{
    const QRect rc = r.intersected(img.rect());
    if (rc.isEmpty())
        return -1.0;
    double sum = 0.0;
    qint64 n = 0;
    for (int y = rc.top(); y <= rc.bottom(); ++y)
        for (int x = rc.left(); x <= rc.right(); ++x) {
            const QRgb c = img.pixel(x, y);
            sum += 0.299 * qRed(c) + 0.587 * qGreen(c) + 0.114 * qBlue(c);
            ++n;
        }
    return n ? sum / double(n) : -1.0;
}

/// 同尺寸两区域 PSNR（dB；完全相同 → 99）
static double regionPsnr(const QImage &a, const QRect &ra,
                         const QImage &b, const QRect &rb)
{
    const int w = qMin(ra.width(), rb.width());
    const int h = qMin(ra.height(), rb.height());
    if (w <= 0 || h <= 0)
        return -1.0;
    double mse = 0.0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const QRgb ca = a.pixel(ra.x() + x, ra.y() + y);
            const QRgb cb = b.pixel(rb.x() + x, rb.y() + y);
            const double dr = qRed(ca) - qRed(cb);
            const double dg = qGreen(ca) - qGreen(cb);
            const double db = qBlue(ca) - qBlue(cb);
            mse += (dr * dr + dg * dg + db * db) / 3.0;
        }
    mse /= double(w * h);
    if (mse <= 1e-9)
        return 99.0;
    return 10.0 * std::log10(255.0 * 255.0 / mse);
}

/// 场景 1：单视频段 + 放大镜取景 + 非恒等画面调节 → 右半走显示链、左半原始像素；
/// 场景 2：未开放大镜（对照）不受影响；场景 3：旋转 90 确实进管道。
static void testComposeMagnifierSplit()
{
    const QString src = QStringLiteral("build_tmp/caltest/basic.mp4");
    if (!QFile::exists(src)) {
        qWarning() << "SKIP magnifier split e2e: no caltest asset";
        return;
    }
    // 源 320x240：取景 120x90（与整幅同 4:3 → 左右内容矩形几何一致，便于逐区比对）
    const QRect srcRect(100, 75, 120, 90);
    DisplayAdjust adj;
    adj.brightness = 25;                    // 非恒等 → 右半必须可量化地更亮
    const QByteArray lut = adj.buildLut();
    CHECK(lut.size() == 256, "magnifier: 非恒等调节产出 256 级 LUT");

    auto runExport = [&](const QString &out, bool mag, int rotation) -> bool {
        SegmentExportEngine::Params pp;
        SegmentExportEngine::Params::ComposeSeg s;
        s.sourcePath = src; s.inMs = 0; s.outMs = 2000;
        pp.segments = {s};
        pp.outputPath = out;
        pp.outFps = 5.0;
        pp.canvas = QSize(640, 480);
        pp.burnOsd = false;      // 保持画面区纯净（OSD 会压在内容上）
        if (mag) {
            pp.magnifierPip = true;
            pp.magnifierSrcRect = srcRect;
            pp.magnifierRotation = rotation;
            pp.magnifierZoom = 2.0;
            pp.magnifierLut = lut;
            pp.magnifierSourcePath = src;
        }
        QFile::remove(out);
        SegmentExportEngine eng;
        QSignalSpy spy(&eng, &SegmentExportEngine::finished);
        eng.start(pp);
        if (!spy.wait(90000) || spy.first().at(0).toBool() == false)
            return false;
        return QFile::exists(out);
    };

    const QString magOut = QStringLiteral("build_tmp/caltest/mag_split.mp4");
    const QString plainOut = QStringLiteral("build_tmp/caltest/mag_split_plain.mp4");
    const QString rotOut = QStringLiteral("build_tmp/caltest/mag_split_rot90.mp4");
    CHECK(runExport(magOut, true, 0), "magnifier: 同框导出完成");
    CHECK(runExport(plainOut, false, 0), "magnifier: 对照导出完成（未开放大镜）");
    CHECK(runExport(rotOut, true, 90), "magnifier: 旋转 90 同框导出完成");

    const QImage fMag = grabFrameAt(magOut, 0.2);
    const QImage fPlain = grabFrameAt(plainOut, 0.2);
    const QImage fRot = grabFrameAt(rotOut, 0.2);
    CHECK(!fMag.isNull() && !fPlain.isNull() && !fRot.isNull(), "magnifier: 抽帧成功");
    if (fMag.isNull() || fPlain.isNull() || fRot.isNull())
        return;

    // 版式与引擎同一公式（canvas 无面板 → videoRect 居中带 pad）
    QRect vr, cr, sr;
    SegmentExportEngine::layoutRects(QSize(640, 480), false, false, &vr, &cr, &sr);
    const int halfW = vr.width() / 2 - 3;
    const QRect leftPane(vr.x(), vr.y(), halfW, vr.height());
    const QRect rightPane(vr.x() + halfW + 6, vr.y(), vr.width() - halfW - 6, vr.height());
    CHECK(leftPane.width() > 0 && rightPane.width() > 0, "magnifier: 左右半矩形有效");
    // 内容区：两半都是 4:3 等比居中（不是面板全高——避开上下黑边对均值的影响）
    const int contentH = leftPane.width() * 240 / 320;
    const int contentY = leftPane.y() + (leftPane.height() - contentH) / 2;
    const QRect leftContent(leftPane.x(), contentY, leftPane.width(), contentH);
    const QRect rightContent(rightPane.x(), contentY, rightPane.width(), contentH);

    const double lMag = regionMeanLuma(fMag, leftContent);
    const double rMag = regionMeanLuma(fMag, rightContent);
    CHECK(rMag > lMag + 15.0,
          QStringLiteral("magnifier: 右半走放大镜显示链 LUT（右 %1 vs 左 %2）")
              .arg(rMag).arg(lMag));

    // 源帧（同刻度）作为两半期望的参照
    const QImage srcFrame = grabFrameAt(src, 0.2);
    CHECK(!srcFrame.isNull(), "magnifier: 源帧抽帧成功");

    // 对照（未开放大镜）：右半不得被 LUT 提亮
    CHECK(regionMeanLuma(fPlain, rightContent) < rMag - 15.0,
          QStringLiteral("magnifier: 未开放大镜不烧 LUT（对照右半 %1 vs 同框右半 %2）")
              .arg(regionMeanLuma(fPlain, rightContent)).arg(rMag));

    if (!srcFrame.isNull()) {
        // 左半 == 源帧原始像素（未过 LUT，取证口径不变；仅编码损耗）
        const QImage expLeft = srcFrame.scaled(leftPane.size(), Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
        const QRect expLeftRect(leftPane.x() + (leftPane.width() - expLeft.width()) / 2,
                                leftPane.y() + (leftPane.height() - expLeft.height()) / 2,
                                expLeft.width(), expLeft.height());
        const double psnrLeft = regionPsnr(fMag, expLeftRect, expLeft, expLeft.rect());
        CHECK(psnrLeft > 20.0,
              QStringLiteral("magnifier: 左半 == 原始像素（PSNR %1 dB）").arg(psnrLeft));

        // 对照产物 = 满幅原图（旧口径逐位不变，无分屏）
        const QImage expFull = srcFrame.scaled(vr.size(), Qt::KeepAspectRatio,
                                               Qt::SmoothTransformation);
        const QRect expFullRect(vr.x() + (vr.width() - expFull.width()) / 2,
                                vr.y() + (vr.height() - expFull.height()) / 2,
                                expFull.width(), expFull.height());
        const double psnrFull = regionPsnr(fPlain, expFullRect, expFull, expFull.rect());
        CHECK(psnrFull > 20.0,
              QStringLiteral("magnifier: 未开放大镜 = 满幅原图（PSNR %1 dB）").arg(psnrFull));

        // 右半内容 == 放大镜显示链（源帧裁剪 → 旋转 → LUT → 等比），容编码损耗
        QImage expect = applyDisplayLut(srcFrame.copy(srcRect), lut);
        const QImage expectPane = expect.scaled(rightPane.size(), Qt::KeepAspectRatio,
                                                Qt::SmoothTransformation);
        const QRect expRect(rightPane.x() + (rightPane.width() - expectPane.width()) / 2,
                            rightPane.y() + (rightPane.height() - expectPane.height()) / 2,
                            expectPane.width(), expectPane.height());
        const double psnrRight = regionPsnr(fMag, expRect, expectPane, expectPane.rect());
        CHECK(psnrRight > 20.0,
              QStringLiteral("magnifier: 右半 == 裁剪+显示链（PSNR %1 dB）").arg(psnrRight));
        // 错参照（未过 LUT 的裁剪）必须明显更差——防“右半其实照抄左半”假通过
        const QImage wrong = srcFrame.copy(srcRect).scaled(rightPane.size(),
                                                           Qt::KeepAspectRatio,
                                                           Qt::SmoothTransformation);
        const double psnrWrong = regionPsnr(fMag, expRect, wrong, wrong.rect());
        CHECK(psnrWrong < psnrRight - 2.0,
              QStringLiteral("magnifier: 错参照（无 LUT）明显更差（%1 vs %2 dB）")
                  .arg(psnrWrong).arg(psnrRight));
        // 旋转 90：同一参照下必须变差（旋转确实进了管道）
        const double psnrRot = regionPsnr(fRot, expRect, expectPane, expectPane.rect());
        CHECK(psnrRot < psnrRight - 3.0,
              QStringLiteral("magnifier: 旋转 90 改变右半（%1 vs %2 dB）")
                  .arg(psnrRot).arg(psnrRight));
    }
    for (const QString &f : {magOut, plainOut, rotOut})
        QFile::remove(f);
}

// ---------------------------------------------------------------------------
// 回归（2026-09-24 真机 4 路宫格“进度 0% 假死”）：源路径过期 + ffmpeg stderr 管道
// ---------------------------------------------------------------------------
/// 场景 A：时间线里的机位源文件已被移走（案内路径过期）→ 必须【快速类型化报错】。
/// 真机事实：过期路径被当 -i 交给 ffmpeg，ffmpeg 的报错与开场信息写进无人读的
/// stderr 管道（4096B）→ 阻塞在 stderr 写、永不读 stdin（ReadTransferCount=0 /
/// WriteTransferCount=2366）→ 进度恒 0%，取消也无效。
static void testComposeStaleSourceFailsFast()
{
    const QString src = QStringLiteral("build_tmp/caltest/basic.mp4");
    if (!QFile::exists(src)) {
        qWarning() << "SKIP stale-source: no caltest asset";
        return;
    }
    const QString ghostDir = QStringLiteral("build_tmp/caltest/relocated_away_dir");
    const QString ghost1 = ghostDir + QStringLiteral("/ghost_C11.mp4");
    const QString ghost2 = ghostDir + QStringLiteral("/ghost_C06.mp4");
    QFile::remove(ghost1);
    QFile::remove(ghost2);
    QDir(ghostDir).removeRecursively();   // 确保不存在（复刻“文件已搬走”）

    auto mkLane = [&](const QString &path, const QString &name) {
        SyncLaneData l;
        l.id = name;
        l.path = path;
        l.displayName = name;
        l.temporary = true;
        l.durationMs = 2000;
        return l;
    };
    SegmentExportEngine::Params::ComposeSeg seg;
    seg.lanes = {mkLane(src, QStringLiteral("C01")), mkLane(ghost1, QStringLiteral("C11")),
                 mkLane(src, QStringLiteral("C02")), mkLane(ghost2, QStringLiteral("C06"))};
    seg.audioLane = 0;
    seg.inMs = 0;
    seg.outMs = 1000;
    SegmentExportEngine::Params pp;
    pp.segments = {seg};
    pp.outputPath = QStringLiteral("build_tmp/caltest/stale_source_out.mp4");
    pp.outFps = 5.0;
    pp.canvas = QSize(640, 480);
    pp.burnOsd = false;
    QFile::remove(pp.outputPath);

    SegmentExportEngine eng;
    QSignalSpy spy(&eng, &SegmentExportEngine::finished);
    QElapsedTimer t;
    t.start();
    eng.start(pp);
    const bool got = spy.wait(20000);
    CHECK(got, "stale-source: 20s 内必须给出结论（不得 0% 假死）");
    if (!got)
        return;
    const QList<QVariant> a = spy.takeFirst();
    CHECK(a.at(0).toBool() == false, "stale-source: 必须失败而非成功");
    const QString msg = a.at(1).toString();
    CHECK(msg.contains(QStringLiteral("源文件不存在")),
          QStringLiteral("stale-source: 类型化错误文案（实测：%1）").arg(msg));
    CHECK(msg.contains(QStringLiteral("ghost_C11"))
              && msg.contains(QStringLiteral("C11")),
          "stale-source: 报错点名到机位名与文件名（可操作）");
    CHECK(!QFile::exists(pp.outputPath), "stale-source: 不落半成品");
    CHECK(t.elapsed() < 20000, "stale-source: 快速失败（不启动 ffmpeg 空耗）");
}

/// 场景 B：4 路带音轨源（开场 stderr 远超过管道缓冲）必须正常出片。
/// 断言依赖：合成源做成 1 视频+10 音频流 → 单路开场信息 ~1.1KB，4 路 ≈ 11.5KB » 4096B。
static void testComposeManyInputsStderrNoDeadlock()
{
    const QString dir = QStringLiteral("build_tmp/caltest/many_inputs_lr_regression");
    QDir().mkpath(dir);
    const QString base = dir + QStringLiteral("/many_stream_src.mp4");
    if (!QFile::exists(base)) {
        QStringList a;
        a << QStringLiteral("-v") << QStringLiteral("error") << QStringLiteral("-y")
          << QStringLiteral("-f") << QStringLiteral("lavfi") << QStringLiteral("-i")
          << QStringLiteral("testsrc2=size=320x240:rate=5:duration=2")
          << QStringLiteral("-f") << QStringLiteral("lavfi") << QStringLiteral("-i")
          << QStringLiteral("sine=frequency=440:duration=2")
          << QStringLiteral("-map") << QStringLiteral("0:v");
        for (int i = 0; i < 10; ++i)
            a << QStringLiteral("-map") << QStringLiteral("1:a");
        a << QStringLiteral("-c:v") << QStringLiteral("libx264")
          << QStringLiteral("-preset") << QStringLiteral("ultrafast")
          << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
          << QStringLiteral("-c:a") << QStringLiteral("aac")
          << QStringLiteral("-shortest") << base;
        QProcess sp;
        sp.start(ToolPaths::findFfmpegPath(), a);
        if (!sp.waitForFinished(120000) || !QFile::exists(base)) {
            qWarning() << "SKIP many-inputs: synth fail";
            return;
        }
    }
    QVector<SegmentExportEngine::Params::ComposeSeg> segs;
    for (int i = 0; i < 4; ++i) {
        const QString dst = QStringLiteral("%1/lumenarc_multi_source_input_video_number_%2_%3.mp4")
                                .arg(dir).arg(i).arg(QString(40, QLatin1Char('p')));
        if (!QFile::exists(dst) && !QFile::copy(base, dst)) {
            qWarning() << "SKIP many-inputs: copy fail";
            return;
        }
        SegmentExportEngine::Params::ComposeSeg s;
        s.sourcePath = dst;
        s.inMs = 0;
        s.outMs = 1000;
        segs << s;
    }
    SegmentExportEngine::Params pp;
    pp.segments = segs;
    pp.outputPath = QStringLiteral("build_tmp/caltest/many_inputs_out.mp4");
    pp.outFps = 5.0;
    pp.canvas = QSize(640, 480);
    pp.burnOsd = false;
    QFile::remove(pp.outputPath);
    QFile::remove(pp.outputPath + QStringLiteral(".ffmpeg.log"));

    SegmentExportEngine eng;
    QSignalSpy spy(&eng, &SegmentExportEngine::finished);
    eng.start(pp);
    const bool got = spy.wait(90000);
    CHECK(got, "many-inputs: 4 源导出 90s 内完成（stderr 管道不得写满卡死）");
    if (!got)
        return;
    const QList<QVariant> a = spy.takeFirst();
    CHECK(a.at(0).toBool(),
          QStringLiteral("many-inputs: 导出成功（实测：%1）").arg(a.at(1).toString()));
    bool hasAudio = false;
    const qint64 dur = probeDurationMs(pp.outputPath, &hasAudio);
    CHECK(qAbs(dur - 4000) <= 700,
          QStringLiteral("many-inputs: 时长≈4000ms（实测 %1）").arg(dur));
    CHECK(!QFile::exists(pp.outputPath + QStringLiteral(".ffmpeg.log")),
          "many-inputs: 成功后不留 stderr 旁车日志");
    QFile::remove(pp.outputPath);
}

/// 场景 C：源存在但不是媒体（ffmpeg 必失败）→ 必须报错 + 保留旁车日志（不静默、不挂住）
/// 回归（2026-09-21 首轮诊断 / 2026-09-24 一并修）：多路宫格导出不得“画面定格”。
/// 根因：SeqDecoder 拿【容器绝对 PTS】与【流内 0 基 target】比大小——DVR/NVR 流
/// start_time 几万秒（实测 hiv00835=37533s）→ 首帧就判“已覆盖” → 每路只解 1 帧。
/// 修法同 run()/runCompose 单源段：seek 与帧 PTS 都减容器起点。
static double regionMeanAbsDiff(const QImage &a, const QRect &ra,
                                const QImage &b, const QRect &rb)
{
    const int w = qMin(ra.width(), rb.width());
    const int h = qMin(ra.height(), rb.height());
    if (w <= 0 || h <= 0)
        return -1.0;
    double sum = 0.0;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const QRgb ca = a.pixel(ra.x() + x, ra.y() + y);
            const QRgb cb = b.pixel(rb.x() + x, rb.y() + y);
            sum += (qAbs(qRed(ca) - qRed(cb)) + qAbs(qGreen(ca) - qGreen(cb))
                    + qAbs(qBlue(ca) - qBlue(cb))) / 3.0;
        }
    return sum / double(w * h);
}

static void testComposeLanesOffsetPtsNotFrozen()
{
    // 造 2s@5fps 素材并人为把输出时间戳推到 90000s（复刻 DVR start_time 异常）
    const QString dir = QStringLiteral("build_tmp/caltest/offset_pts_regression");
    QDir().mkpath(dir);
    struct Case { const char *name; qint64 offsetSec; };
    const Case cases[2] = {{"offset_pts_src", 90000}, {"normal_pts_src", 0}};
    const QRect sample = QRect(40, 30, 80, 60);   // 瓦片内部样区（避开覆盖条/OSD）
    for (const Case &c : cases) {
        const QString src = QStringLiteral("%1/%2.mp4").arg(dir, QLatin1String(c.name));
        if (!QFile::exists(src)) {
            QStringList a;
            a << QStringLiteral("-v") << QStringLiteral("error") << QStringLiteral("-y")
              << QStringLiteral("-f") << QStringLiteral("lavfi") << QStringLiteral("-i")
              << QStringLiteral("testsrc2=size=320x240:rate=5:duration=2")
              << QStringLiteral("-c:v") << QStringLiteral("libx264")
              << QStringLiteral("-preset") << QStringLiteral("ultrafast")
              << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p");
            if (c.offsetSec > 0)
                a << QStringLiteral("-output_ts_offset") << QString::number(c.offsetSec);
            a << src;
            QProcess sp;
            sp.start(ToolPaths::findFfmpegPath(), a);
            if (!sp.waitForFinished(60000) || !QFile::exists(src)) {
                qWarning() << "SKIP offset-pts: synth fail" << c.name;
                return;
            }
        }
        // 双路宫格段（同一素材两路）1s 轴长
        auto mkLane = [&](const QString &id) {
            SyncLaneData l;
            l.id = id;
            l.path = src;
            l.displayName = id;
            l.temporary = true;
            l.durationMs = 2000;
            return l;
        };
        SegmentExportEngine::Params::ComposeSeg seg;
        seg.lanes = {mkLane(QStringLiteral("L1")), mkLane(QStringLiteral("L2"))};
        seg.audioLane = -1;
        seg.inMs = 0;
        seg.outMs = 1000;
        SegmentExportEngine::Params pp;
        pp.segments = {seg};
        pp.outputPath = QStringLiteral("build_tmp/caltest/offset_pts_%1_out.mp4")
                            .arg(QLatin1String(c.name));
        pp.outFps = 5.0;
        pp.canvas = QSize(640, 480);
        pp.burnOsd = false;
        QFile::remove(pp.outputPath);
        SegmentExportEngine eng;
        QSignalSpy spy(&eng, &SegmentExportEngine::finished);
        eng.start(pp);
        if (!spy.wait(60000) || spy.first().at(0).toBool() == false) {
            CHECK(false, QStringLiteral("offset-pts(%1): 宫格导出失败")
                             .arg(QLatin1String(c.name)));
            continue;
        }
        // 瓦片内部：t=0.2s 与 t=0.8s 必须不同（定格仅差覆盖条/OSD，均不在样区内）
        const QImage f1 = grabFrameAt(pp.outputPath, 0.2);
        const QImage f2 = grabFrameAt(pp.outputPath, 0.8);
        CHECK(!f1.isNull() && !f2.isNull(),
              QStringLiteral("offset-pts(%1): 抽帧成功").arg(QLatin1String(c.name)));
        if (!f1.isNull() && !f2.isNull()) {
            QRect vr, cr, sr;
            SegmentExportEngine::layoutRects(QSize(640, 480), false, false, &vr, &cr, &sr);
            const int cellW = vr.width() / 2;
            const QRect tile1(vr.x() + 2, vr.y() + 2, cellW - 4, vr.height() - 4);
            const QRect inner = sample.translated(tile1.x() + cellW / 4, tile1.y() + tile1.height() / 3);
            const double d = regionMeanAbsDiff(f1, inner, f2, inner);
            CHECK(d > 6.0,
                  QStringLiteral("offset-pts(%1): 瓦片画面必须随时间变化（实测差值 %2）")
                      .arg(QLatin1String(c.name)).arg(d, 0, 'f', 2));
        }
        QFile::remove(pp.outputPath);
    }
}

/// 版面拍板（2026-09-24 真机反馈）：①「时间轴示意」（覆盖条）必须画在画面**最下方**；
/// ②删掉右上「分析演示材料 · 非原始证据」红字演示角标。
/// 用纯灰素材（color=gray）做断言：叠加层与源色彩差异容易判定。
static void testComposeLanesStripAtBottomLayout()
{
    const QString dir = QStringLiteral("build_tmp/caltest/strip_bottom_regression");
    QDir().mkpath(dir);
    const QString src = dir + QStringLiteral("/gray_src.mp4");
    if (!QFile::exists(src)) {
        QProcess sp;
        sp.start(ToolPaths::findFfmpegPath(),
                 {QStringLiteral("-v"), QStringLiteral("error"), QStringLiteral("-y"),
                  QStringLiteral("-f"), QStringLiteral("lavfi"), QStringLiteral("-i"),
                  QStringLiteral("color=c=gray:s=320x240:rate=5:duration=2"),
                  QStringLiteral("-c:v"), QStringLiteral("libx264"),
                  QStringLiteral("-preset"), QStringLiteral("ultrafast"),
                  QStringLiteral("-pix_fmt"), QStringLiteral("yuv420p"), src});
        if (!sp.waitForFinished(60000) || !QFile::exists(src)) {
            qWarning() << "SKIP strip-bottom: synth fail";
            return;
        }
    }
    auto mkLane = [&](const QString &id) {
        SyncLaneData l;
        l.id = id;
        l.path = src;
        l.displayName = id;
        l.temporary = true;
        l.durationMs = 2000;
        return l;
    };
    SegmentExportEngine::Params::ComposeSeg seg;
    seg.lanes = {mkLane(QStringLiteral("L1")), mkLane(QStringLiteral("L2"))};
    seg.audioLane = -1;
    seg.inMs = 0;
    seg.outMs = 1000;
    SegmentExportEngine::Params pp;
    pp.segments = {seg};
    pp.outputPath = QStringLiteral("build_tmp/caltest/strip_bottom_out.mp4");
    pp.outFps = 5.0;
    pp.canvas = QSize(640, 480);
    pp.burnOsd = false;
    QFile::remove(pp.outputPath);
    SegmentExportEngine eng;
    QSignalSpy spy(&eng, &SegmentExportEngine::finished);
    eng.start(pp);
    if (!spy.wait(60000) || spy.first().at(0).toBool() == false) {
        CHECK(false, "strip-bottom: 宫格导出失败");
        return;
    }
    // k=2 → wall=400ms → 游标在条带 40% 处
    const QImage f = grabFrameAt(pp.outputPath, 0.4);
    CHECK(!f.isNull(), "strip-bottom: 抽帧成功");
    if (!f.isNull()) {
        QRect vr, cr, sr;
        SegmentExportEngine::layoutRects(QSize(640, 480), false, false, &vr, &cr, &sr);
        const int n = 2;
        const int stripH = 8 + n * 6 + 9;
        const QRect gridRect(vr.x(), vr.y(), vr.width(), vr.height() - stripH);
        const QRect stripRect(vr.x() + 6, gridRect.bottom() + 4, vr.width() - 12, stripH - 6);
        // ① 最下方：白游标竖线（贯穿条带高度）
        const int cursorX = stripRect.x() + 6
            + int(0.4 * (stripRect.width() - 12));
        int bright = 0;
        for (int y = stripRect.y() + 2; y <= stripRect.bottom() - 2; ++y) {
            const QRgb c = f.pixel(qBound(0, cursorX, f.width() - 1), qBound(0, y, f.height() - 1));
            if (qRed(c) > 235 && qGreen(c) > 235 && qBlue(c) > 235)
                ++bright;
        }
        CHECK(bright >= stripRect.height() - 8,
              QStringLiteral("strip-bottom: 覆盖条在最下方（白游标列亮像素 %1/%2）")
                  .arg(bright).arg(stripRect.height() - 4));
        // ② 瓦片内部（不是黑边）仍是源灰、且未被条带压住
        // —— 两路宫格：瓦片内容等比居中（上下黑边），取内容矩形中心
        const int cellW = gridRect.width() / 2;
        const int cellH = gridRect.height();
        const QRect tile1(gridRect.x() + 2, gridRect.y() + 2, cellW - 4, cellH - 4);
        const int contentH = tile1.width() * 240 / 320;
        const QRect content1(tile1.x(), tile1.y() + (tile1.height() - contentH) / 2,
                             tile1.width(), contentH);
        const QColor top = f.pixelColor(content1.center().x(), content1.center().y());
        CHECK(qAbs(top.red() - 128) < 45 && qAbs(top.green() - 128) < 45
                  && qAbs(top.blue() - 128) < 45,
              QStringLiteral("strip-bottom: 瓦片内容无条带叠层（实测 RGB %1,%2,%3）")
                  .arg(top.red()).arg(top.green()).arg(top.blue()));
        // ③ 右上角无红字演示角标（源为灰，红色像素=角标特征）
        int reddish = 0;
        const QRect topRight(vr.right() - 500, vr.y(), 500, 40);
        for (int y = topRight.top(); y <= topRight.bottom(); ++y)
            for (int x = topRight.left(); x <= topRight.right(); ++x) {
                const QRgb c = f.pixel(qBound(0, x, f.width() - 1), qBound(0, y, f.height() - 1));
                // v1.18.x：判据必须真判「红」——旧式 qGreen<130&&qBlue<130 会把
                // 顶部逐路彩条里的橄榄色（实测 176,119,0 的 DataPalette 机位色）
                // 误计成红字角标（角标已删，此断言本是防它复活）。
                if (qRed(c) > 170 && qGreen(c) < 90 && qBlue(c) < 90)
                    ++reddish;
            }
        CHECK(reddish == 0,
              QStringLiteral("strip-bottom: 右上无演示红字角标（红色像素 %1）").arg(reddish));
    }
    QFile::remove(pp.outputPath);
}

static void testEvidenceFfmpegFailureKeepsLog()
{
    const QString junk = QStringLiteral("build_tmp/caltest/not_a_media_file.mp4");
    {
        QFile f(junk);
        if (!f.open(QIODevice::WriteOnly)) {
            qWarning() << "SKIP ffmpeg-fail: cannot write junk";
            return;
        }
        f.write("this is not a media file\n");
    }
    SegmentExportEngine::Params pp;
    SegmentExportEngine::Params::ComposeSeg s;
    s.sourcePath = junk;
    s.inMs = 0;
    s.outMs = 1000;
    pp.segments = {s};
    pp.evidenceCopy = true;
    pp.outputPath = QStringLiteral("build_tmp/caltest/ffmpeg_fail_out.mp4");
    QFile::remove(pp.outputPath);
    QFile::remove(pp.outputPath + QStringLiteral(".ffmpeg.log"));
    SegmentExportEngine eng;
    QSignalSpy spy(&eng, &SegmentExportEngine::finished);
    eng.start(pp);
    const bool got = spy.wait(60000);
    CHECK(got, "ffmpeg-fail: 必须给出结论（不得挂住）");
    if (!got)
        return;
    const QList<QVariant> a = spy.takeFirst();
    CHECK(a.at(0).toBool() == false, "ffmpeg-fail: 必须失败而非成功");
    CHECK(a.at(1).toString().contains(QStringLiteral("直拷失败")),
          QStringLiteral("ffmpeg-fail: 类型化错误（实测：%1）").arg(a.at(1).toString()));
    CHECK(QFile::exists(pp.outputPath + QStringLiteral(".ffmpeg.log")),
          "ffmpeg-fail: 失败保留旁车日志供排查");
    QFile::remove(pp.outputPath + QStringLiteral(".ffmpeg.log"));
    QFile::remove(junk);
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

/// P2.13（2026-09-28 用户拍板「每个视频自由选窗位」）：宫格窗位映射像素级验证。
/// 两路纯色源（红/蓝）→ 默认顺序：左格红、右格蓝；seg.laneCell={1,0}：两格互换。
static void testComposeLaneCellMapping()
{
    const QString dir = QStringLiteral("build_tmp/caltest/lanecell");
    QDir().mkpath(dir);
    const QString red = dir + QStringLiteral("/red.mp4");
    const QString blue = dir + QStringLiteral("/blue.mp4");
    auto mkSolid = [&](const QString &path, const QString &color) {
        if (QFile::exists(path))
            return true;
        QStringList a;
        a << QStringLiteral("-v") << QStringLiteral("error")
          << QStringLiteral("-y") << QStringLiteral("-f") << QStringLiteral("lavfi")
          << QStringLiteral("-i")
          << QStringLiteral("color=c=%1:size=320x240:rate=5:duration=2").arg(color)
          << QStringLiteral("-c:v") << QStringLiteral("libx264")
          << QStringLiteral("-preset") << QStringLiteral("ultrafast")
          << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p") << path;
        QProcess sp;
        sp.start(ToolPaths::findFfmpegPath(), a);
        return sp.waitForFinished(120000) && QFile::exists(path);
    };
    if (!mkSolid(red, QStringLiteral("red"))
        || !mkSolid(blue, QStringLiteral("blue"))) {
        qWarning() << "SKIP lane-cell: synth fail";
        return;
    }
    auto mkLane = [](const QString &id, const QString &p) {
        SyncLaneData l;
        l.id = id;
        l.path = p;
        l.displayName = id;
        l.temporary = true;      // 墙钟=流内
        l.durationMs = 2000;
        return l;
    };
    auto runCase = [&](const QVector<int> &cell, const QString &out) {
        SegmentExportEngine::Params pp;
        SegmentExportEngine::Params::ComposeSeg seg;
        seg.lanes = {mkLane(QStringLiteral("C01"), red),
                     mkLane(QStringLiteral("C02"), blue)};
        seg.laneCell = cell;
        seg.audioLane = -1;
        seg.inMs = 0;
        seg.outMs = 2000;
        pp.segments = {seg};
        pp.outputPath = out;
        pp.outFps = 5.0;
        pp.canvas = QSize(640, 480);
        pp.burnOsd = false;
        QFile::remove(out);
        SegmentExportEngine eng;
        QSignalSpy spy(&eng, &SegmentExportEngine::finished);
        eng.start(pp);
        if (!spy.wait(90000))
            return QImage();
        if (!spy.takeFirst().at(0).toBool())
            return QImage();
        return grabFrameAt(out, 0.2);
    };
    const QImage a = runCase({}, QStringLiteral("build_tmp/caltest/lanecell_base.mp4"));
    const QImage b = runCase({1, 0},
                             QStringLiteral("build_tmp/caltest/lanecell_swap.mp4"));
    if (a.isNull() || b.isNull()) {
        CHECK(false, "lane-cell: 导出/抽帧失败");
        return;
    }
    auto px = [](const QImage &img, double fx, double fy) {
        return img.pixelColor(int(img.width() * fx), int(img.height() * fy));
    };
    // 宫格上收下条带：取上半部左右两格中心（避开条带与 OSD）
    const QColor aL = px(a, 0.25, 0.35), aR = px(a, 0.75, 0.35);
    const QColor bL = px(b, 0.25, 0.35), bR = px(b, 0.75, 0.35);
    CHECK(aL.red() > 150 && aL.blue() < 100,
          qPrintable(QStringLiteral("lane-cell: 默认左格=红（实测 %1,%2,%3）")
                         .arg(aL.red()).arg(aL.green()).arg(aL.blue())));
    CHECK(aR.blue() > 150 && aR.red() < 100,
          qPrintable(QStringLiteral("lane-cell: 默认右格=蓝（实测 %1,%2,%3）")
                         .arg(aR.red()).arg(aR.green()).arg(aR.blue())));
    CHECK(bL.blue() > 150 && bL.red() < 100,
          qPrintable(QStringLiteral("lane-cell: laneCell={1,0} 左格应变蓝（实测 %1,%2,%3）")
                         .arg(bL.red()).arg(bL.green()).arg(bL.blue())));
    CHECK(bR.red() > 150 && bR.blue() < 100,
          qPrintable(QStringLiteral("lane-cell: laneCell={1,0} 右格应变红（实测 %1,%2,%3）")
                         .arg(bR.red()).arg(bR.green()).arg(bR.blue())));
    QFile::remove(QStringLiteral("build_tmp/caltest/lanecell_base.mp4"));
    QFile::remove(QStringLiteral("build_tmp/caltest/lanecell_swap.mp4"));
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
        drawChartStrip(p, QRect(0, 0, 800, 200), ov, 1000, 0, 2000);
    }
    const QRect plot = QRect(0, 0, 800, 200).adjusted(48, 8, -10, 18);
    const int cx = plot.x() + int(1000.0 / 2000.0 * plot.width());   // 全量区间中点
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
    CHECK(part.contains(QStringLiteral("[as0]concat=n=1:v=0:a=1,apad[aout]")),
          "V2 段间总拼（§86 起尾接 apad 补静）");
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
    testComposeMagnifierSplit();
    testComposeStaleSourceFailsFast();
    testComposeManyInputsStderrNoDeadlock();
    testComposeLanesOffsetPtsNotFrozen();
    testComposeLanesStripAtBottomLayout();
    testEvidenceFfmpegFailureKeepsLog();
    testEvidenceEndToEnd();
    testComposeLanesEndToEnd();
    testComposeLaneCellMapping();
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
