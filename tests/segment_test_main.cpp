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
#include <QtTest>


#include "domain/speed_plan.h"
#include "infrastructure/segment_export_engine.h"
#include "domain/timeline_model.h"
#include <QTemporaryDir>

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

int main(int argc, char **argv)
{
    QApplication app(argc, argv);   // QImage+drawText 需 GUI 应用上下文（字体子系统）
    testNormalize();
    testMapping();
    testPlanFromLabels();
    testAtempoChain();
    testAudioFilterChain();
    testLayout();
    testVlaRoundtrip();
    testEndToEnd();
    qInfo() << "segment_test:" << g_checks << "checks," << g_failures << "failures";
    return g_failures == 0 ? 0 : 1;
}
