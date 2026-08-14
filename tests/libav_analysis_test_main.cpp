/**
 * @file libav_analysis_test_main.cpp
 * @brief libav 分析引擎单测（v1.5.0 P3 第二批）
 *
 * 1. rasterizePolygonSpans 扫描线光栅化（纯逻辑）
 * 2. scaleRectToFrame 缩放取整语义（对齐 analyze_video.py _build_roi_masks）
 * 3. 真视频亮度冒烟 + A/B 对拍 vs Python 快速路径（Q-14 验收线：
 *    逐点 |Δ| ≤ 1 且均值偏差 ≤ 0.5）
 *
 * 用法：lumenarc_libav_test [video.mp4]
 * 默认素材 build_tmp/caltest/basic.mp4（320x240 5fps 10 帧，全帧率无抽稀）。
 */
#include "infrastructure/libav_analysis_engine.h"
#include "domain/analysis_snapshot.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QDir>
#include <QTimer>
#include <cstdio>
#include <cmath>

static int g_checks = 0;
static int g_failures = 0;

#define CHECK(cond, msg)                                                    \
    do {                                                                    \
        ++g_checks;                                                         \
        if (!(cond)) {                                                      \
            ++g_failures;                                                   \
            fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg);  \
        }                                                                   \
    } while (0)

// ============================================================================
// 1. span 光栅化
// ============================================================================
static void testRasterizeRectPolygon()
{
    // 轴对齐矩形多边形 → 每行一个满宽 span
    QPolygon p;
    p << QPoint(10, 10) << QPoint(30, 10) << QPoint(30, 30) << QPoint(10, 30);
    const auto spans = LibavAnalysisEngine::rasterizePolygonSpans(p, 320, 240);
    CHECK(!spans.isEmpty(), "spans: rect poly produces spans");
    int rowCount = 0;
    bool allFull = true;
    for (const auto &s : spans) {
        if (s.y < 10 || s.y > 29)
            allFull = false;
        // cv2 闭合区间语义：每行覆盖 [10,30]（存储为半开 [10,31)）
        if (s.x1 != 10 || s.x2 != 31)
            allFull = false;
        ++rowCount;
    }
    CHECK(allFull && rowCount == 20, "spans: rect poly covers y=10..29 x=[10,31)");
}

static void testRasterizeTriangle()
{
    // 三角形 (0,0)-(8,0)-(8,8)：面积 32 像素（8*8/2）
    QPolygon p;
    p << QPoint(0, 0) << QPoint(8, 0) << QPoint(8, 8);
    const auto spans = LibavAnalysisEngine::rasterizePolygonSpans(p, 64, 64);
    qint64 area = 0;
    for (const auto &s : spans)
        area += s.x2 - s.x1;
    // 扫描线填充面积 = 45（cv2.fillPoly 实测值：round 取整 + 闭合区间，
    // 与几何面积 32 不同——OpenCV 扫描线含右端点像素）
    CHECK(std::llabs(area - 45) <= 1,
          qPrintable(QStringLiteral("spans: triangle area=%1").arg(area)));
}

static void testRasterizeDegenerate()
{
    QPolygon p;
    p << QPoint(0, 0) << QPoint(4, 4);   // 2 点
    CHECK(LibavAnalysisEngine::rasterizePolygonSpans(p, 64, 64).isEmpty(),
          "spans: 2-point poly empty");
    QPolygon p2;
    CHECK(LibavAnalysisEngine::rasterizePolygonSpans(p2, 64, 64).isEmpty(),
          "spans: empty poly empty");
    // 越界裁剪
    QPolygon p3;
    p3 << QPoint(-5, -5) << QPoint(70, -5) << QPoint(70, 70) << QPoint(-5, 70);
    const auto spans = LibavAnalysisEngine::rasterizePolygonSpans(p3, 64, 64);
    bool clipped = true;
    for (const auto &s : spans) {
        if (s.y < 0 || s.y >= 64 || s.x1 < 0 || s.x2 > 64)
            clipped = false;
    }
    CHECK(clipped && !spans.isEmpty(), "spans: out-of-bounds clipped");
}

// ============================================================================
// 2. 矩形缩放取整
// ============================================================================
static void testScaleRect()
{
    // 同尺寸：原样
    const QRect r0 = LibavAnalysisEngine::scaleRectToFrame(
        QRect(10, 10, 60, 40), 320, 240, 320, 240);
    CHECK(r0 == QRect(10, 10, 60, 40), "scale: identity");
    // 半尺寸：x=10*0.5=5, x+w=70*0.5=35（Python int(round) 语义）
    const QRect r1 = LibavAnalysisEngine::scaleRectToFrame(
        QRect(10, 10, 60, 40), 160, 120, 320, 240);
    CHECK(r1 == QRect(5, 5, 30, 20), "scale: half size");
    // 取整：x=3.5 → round=4
    const QRect r2 = LibavAnalysisEngine::scaleRectToFrame(
        QRect(3, 3, 5, 5), 640, 480, 320, 240);
    CHECK(r2 == QRect(6, 6, 10, 10), "scale: round-half-up");
    // clamp 到输出
    const QRect r3 = LibavAnalysisEngine::scaleRectToFrame(
        QRect(-5, -5, 400, 300), 160, 120, 320, 240);
    CHECK(r3.left() >= 0 && r3.top() >= 0
          && r3.right() < 160 && r3.bottom() < 120, "scale: clamped");
}

// ============================================================================
// 3. 真视频 A/B 对拍
// ============================================================================

/// 同步调 Python 快速路径，返回 {timestamps, luminances[][]}。
static bool runPythonLuminance(const QString &videoPath, const QString &roiJson,
                               const QString &pythonExe, const QString &scriptPath,
                               const QString &ffmpegExe,
                               QVector<qint64> *ts, QVector<QVector<qreal>> *lums)
{
    QProcess proc;
    proc.start(pythonExe, {scriptPath, videoPath, roiJson,
                           QStringLiteral("--ffmpeg-path"), ffmpegExe});
    if (!proc.waitForStarted(10000))
        return false;
    if (!proc.waitForFinished(120000))
        return false;
    if (proc.exitCode() != 0)
        return false;
    const QJsonDocument doc = QJsonDocument::fromJson(proc.readAllStandardOutput());
    if (!doc.isObject())
        return false;
    const QJsonObject obj = doc.object();
    const QJsonArray tsArr = obj["timestamps"].toArray();
    const QJsonArray lumArr = obj["luminances"].toArray();
    if (tsArr.isEmpty() || lumArr.isEmpty())
        return false;
    for (const auto &v : tsArr)
        ts->append(static_cast<qint64>(std::llround(v.toDouble())));
    for (const auto &reg : lumArr) {
        QVector<qreal> series;
        for (const auto &v : reg.toArray())
            series.append(v.toDouble());
        lums->append(series);
    }
    return true;
}

/// A/B 对拍：Python 点找 libav 最近时间戳点对比（抽稀时允许 ±1 步长内匹配）。
static void compareSeries(const QVector<qint64> &pyTs,
                          const QVector<qint64> &libTs,
                          const QVector<qreal> &pyLum,
                          const QVector<qreal> &libLum,
                          qint64 stepMs, const char *tag)
{
    if (pyLum.isEmpty() || libLum.isEmpty())
        return;
    double maxAbs = 0.0, sumAbs = 0.0;
    int n = 0, matched = 0;
    for (int i = 0; i < pyTs.size() && i < pyLum.size(); ++i) {
        // 找 libav 最近点
        int best = -1;
        qint64 bestDist = std::numeric_limits<qint64>::max();
        for (int j = 0; j < libTs.size(); ++j) {
            const qint64 d = std::llabs(libTs[j] - pyTs[i]);
            if (d < bestDist) {
                bestDist = d;
                best = j;
            }
        }
        if (best < 0)
            continue;
        // 容差：Python 抽稀步长的 0.6（半采样周期）
        if (bestDist > stepMs * 0.6 && stepMs > 0)
            continue;
        ++matched;
        const double diff = std::fabs(libLum[best] - pyLum[i]);
        maxAbs = std::max(maxAbs, diff);
        sumAbs += diff;
        ++n;
    }
    if (n == 0) {
        ++g_failures;
        ++g_checks;
        fprintf(stderr, "FAIL: %s: no matched points\n", tag);
        return;
    }
    const double meanAbs = sumAbs / n;
    ++g_checks;
    const bool ok = (maxAbs <= 1.0 && meanAbs <= 0.5);
    if (!ok)
        ++g_failures;
    fprintf(stderr, "[ab] %s: matched=%d/%d maxAbs=%.3f meanAbs=%.3f => %s\n",
            tag, matched, pyTs.size(), maxAbs, meanAbs,
            ok ? "PASS" : "FAIL <<<");
}

static void testRealVideoAB(const QString &videoPath)
{
    const QString appDir = QCoreApplication::applicationDirPath();
    const QString pythonExe = appDir + "/python/python.exe";
    const QString scriptPath = appDir + "/analyze_video.py";
    const QString ffmpegExe = appDir + "/ffmpeg/ffmpeg.exe";
    if (!QFile::exists(pythonExe) || !QFile::exists(scriptPath)
        || !QFile::exists(ffmpegExe) || !QFile::exists(videoPath)) {
        fprintf(stderr, "[ab] SKIP (deps missing: %s)\n",
                videoPath.toUtf8().constData());
        return;
    }

    // 同一 ROI 集：1 矩形 + 1 多边形（坐标 = 视频原始像素）
    const QString roiJson = QStringLiteral(
        "[{\"type\":\"rect\",\"roi_id\":1,\"x\":10,\"y\":10,\"w\":60,\"h\":40},"
        "{\"type\":\"polygon\",\"roi_id\":2,\"points\":[[20,20],[80,20],[80,70]]}]");

    // Python 通路
    QVector<qint64> pyTs;
    QVector<QVector<qreal>> pyLums;
    const bool pyOk = runPythonLuminance(videoPath, roiJson, pythonExe, scriptPath,
                                         ffmpegExe, &pyTs, &pyLums);
    ++g_checks;
    if (!pyOk) {
        ++g_failures;
        fprintf(stderr, "FAIL: python reference run failed\n");
        return;
    }
    fprintf(stderr, "[ab] python ref: %d points\n", pyTs.size());

    // libav 引擎（异步）
    LibavAnalysisEngine engine;
    AnalysisSnapshot snap;
    bool done = false, failed = false;
    QEventLoop loop;
    QObject::connect(&engine, &IAnalysisEngine::analysisFinished,
                     &loop, [&](const AnalysisSnapshot &s) { snap = s; done = true; loop.quit(); });
    QObject::connect(&engine, &IAnalysisEngine::analysisFailed,
                     &loop, [&](const QString &) { failed = true; loop.quit(); });

    QVector<QRect> rects{QRect(10, 10, 60, 40)};
    QVector<QPolygon> polys;
    polys << (QPolygon() << QPoint(20, 20) << QPoint(80, 20) << QPoint(80, 70));
    engine.startAnalysis(videoPath, rects, polys, {}, {1}, {2});

    QTimer::singleShot(60000, &loop, &QEventLoop::quit);
    loop.exec();

    ++g_checks;
    if (!done || failed) {
        ++g_failures;
        fprintf(stderr, "FAIL: libav engine run (done=%d failed=%d)\n", done, failed);
        return;
    }

    // 全帧率断言：点数 = Python 全帧率点数（本素材无抽稀）
    ++g_checks;
    if (snap.timestamps.size() != pyTs.size()) {
        ++g_failures;
        fprintf(stderr, "FAIL: point count libav=%d python=%d\n",
                snap.timestamps.size(), pyTs.size());
    } else {
        // 时间戳对齐（±1ms）
        bool tsOk = true;
        for (int i = 0; i < pyTs.size(); ++i) {
            if (std::llabs(snap.timestamps[i] - pyTs[i]) > 1) {
                tsOk = false;
                break;
            }
        }
        ++g_checks;
        if (!tsOk) {
            ++g_failures;
            fprintf(stderr, "FAIL: timestamps diverge\n");
        }
    }

    // 亮度逐点对比（验收线 |Δ|≤1 且均值偏差 ≤0.5）
    compareSeries(pyTs, snap.timestamps, pyLums[0], snap.values[0], 0, "rect");
    compareSeries(pyTs, snap.timestamps, pyLums[1], snap.values[1], 0, "poly");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testRasterizeRectPolygon();
    testRasterizeTriangle();
    testRasterizeDegenerate();
    testScaleRect();

    QString video = QStringLiteral("build_tmp/caltest/basic.mp4");
    if (argc > 1)
        video = QString::fromUtf8(argv[1]);
    testRealVideoAB(video);

    fprintf(stderr, "libav_test: %d checks, %d failures\n",
            g_checks, g_failures);
    return g_failures ? 1 : 0;
}
