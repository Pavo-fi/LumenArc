/**
 * @file v17_test_main.cpp
 * @brief v1.7.0 单测：编码器探测/参数面 + 重叠剪切计划 + 自动命名
 */
#include "infrastructure/encoder_probe.h"
#include "infrastructure/transcode_engine.h"
#include "infrastructure/python_analysis_engine.h"
#include "domain/overlap_cut.h"
#include "domain/preprocess_task.h"
#include <QCoreApplication>
#include <QDateTime>
#include <cstdio>

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
// 1. 编码器探测与参数映射
// ============================================================================
static void testEncoderProbe()
{
    const QStringList encs = encoder_probe::availableEncoders();
    ++g_checks;
    if (encs.isEmpty()) {
        ++g_failures;
        fprintf(stderr, "FAIL: no encoders\n");
        return;
    }
    fprintf(stderr, "[enc] available: %s\n", encs.join(", ").toUtf8().constData());
    fprintf(stderr, "[enc] ffmpeg=%s nvenc=%d qsv=%d\n",
            PythonAnalysisEngine::findFfmpegPath().toUtf8().constData(),
            int(encoder_probe::probeEncoderWorks(QStringLiteral("h264_nvenc"))),
            int(encoder_probe::probeEncoderWorks(QStringLiteral("h264_qsv"))));
    // 探测链首选项必须是可用编码器之一
    const QString best = encoder_probe::selectBestEncoder();
    ++g_checks;
    if (!encs.contains(best)) {
        ++g_failures;
        fprintf(stderr, "FAIL: best encoder %s not in available list\n",
                best.toUtf8().constData());
    }
    // 选择链优先：NVENC 可用时必须选中（列表顺序是 -encoders 字母序，
    // 不代表优先级；优先级由 selectBestEncoder 的偏好链决定）
    ++g_checks;
    if (encs.contains(QStringLiteral("h264_nvenc"))
        && best != QStringLiteral("h264_nvenc")) {
        ++g_failures;
        fprintf(stderr, "FAIL: nvenc available but not selected\n");
    }
    // 参数映射
    const auto aNv = encoder_probe::encoderArgsFor(QStringLiteral("h264_nvenc"));
    CHECK(aNv.encoder == "h264_nvenc" && aNv.qualityFlag == "-cq"
          && aNv.qualityValue == 19 && aNv.presetValue == "p4",
          "args: nvenc mapping");
    const auto aQs = encoder_probe::encoderArgsFor(QStringLiteral("h264_qsv"));
    CHECK(aQs.encoder == "h264_qsv" && aQs.qualityFlag == "-global_quality"
          && aQs.presetValue == "veryfast", "args: qsv mapping");
    const auto aX = encoder_probe::encoderArgsFor(QStringLiteral("libx264"));
    CHECK(aX.encoder == "libx264" && aX.qualityFlag == "-crf"
          && aX.qualityValue == 18, "args: libx264 mapping");
    const auto aAuto = encoder_probe::encoderArgsFor(QString());
    CHECK(aAuto.encoder == "libx264", "args: empty → libx264");
}

// ============================================================================
// 2. buildArgs 参数面（三分支 + 剪切）
// ============================================================================
static void testBuildArgs()
{
    TranscodeRequest req;
    req.input = QStringLiteral("in.avi");
    req.output = QStringLiteral("out.mp4");
    req.durationMs = 60000;
    req.crf = 20;

    // libx264 分支（显式指定；Auto 会选 NVENC）：-c:v libx264 -crf 20
    req.encoder = QStringLiteral("libx264");
    const QStringList x = TranscodeEngine::buildArgs(req, QStringLiteral("tmp.mp4"));
    const int cvi = x.indexOf(QStringLiteral("-c:v"));
    CHECK(cvi >= 0 && x[cvi + 1] == "libx264", "args: libx264 c:v");
    const int cri = x.indexOf(QStringLiteral("-crf"));
    CHECK(cri >= 0 && x[cri + 1] == "20", "args: custom crf kept");

    // nvenc 分支
    req.encoder = QStringLiteral("h264_nvenc");
    const QStringList n = TranscodeEngine::buildArgs(req, QStringLiteral("tmp.mp4"));
    const int nvi = n.indexOf(QStringLiteral("-c:v"));
    CHECK(nvi >= 0 && n[nvi + 1] == "h264_nvenc", "args: nvenc c:v");
    CHECK(n.contains(QStringLiteral("-cq")), "args: nvenc -cq");
    CHECK(n.contains(QStringLiteral("p4")), "args: nvenc preset p4");

    // 剪切：-ss 输入侧 + -t
    req.encoder.clear();
    req.trimStartMs = 15000;
    req.trimEndMs = 45000;
    const QStringList t = TranscodeEngine::buildArgs(req, QStringLiteral("tmp.mp4"));
    const int ssi = t.indexOf(QStringLiteral("-ss"));
    CHECK(ssi >= 0 && t[ssi + 1] == "15.000", "args: -ss 15s");
    const int ti = t.indexOf(QStringLiteral("-t"));
    CHECK(ti >= 0 && t[ti + 1] == "30.000", "args: -t 30s");
    const int ii = t.indexOf(QStringLiteral("-i"));
    CHECK(ii >= 0 && ii > ssi, "args: -ss before -i (input-side seek)");

    // 分辨率统一：vf 含 scale
    req.trimStartMs = 0;
    req.trimEndMs = 0;
    req.outWidth = 1280;
    req.outHeight = 720;
    const QStringList s = TranscodeEngine::buildArgs(req, QStringLiteral("tmp.mp4"));
    const int vi = s.indexOf(QStringLiteral("-vf"));
    CHECK(vi >= 0 && s[vi + 1].contains("scale=1280:720")
          && s[vi + 1].contains("setpts"),
          "args: vf scale+setpts");
}

// ============================================================================
// 3. 重叠剪切计划
// ============================================================================
static void testCutPlans()
{
    // 无重叠
    {
        QVector<WallSegment> segs = {
            {QStringLiteral("a"), 0, 100000, 100000},
            {QStringLiteral("b"), 100000, 200000, 100000},
        };
        const auto plans = planOverlapCuts(segs);
        CHECK(plans.size() == 2 && !plans[1].trimmed && plans[1].keepStartMs == 0,
              "cut: no overlap untouched");
    }
    // 部分重叠：b 开头被剪（Q-17 保 a 完整）
    {
        QVector<WallSegment> segs = {
            {QStringLiteral("a"), 0, 100000, 100000},
            {QStringLiteral("b"), 80000, 180000, 100000},
        };
        const auto plans = planOverlapCuts(segs);
        CHECK(plans[0].keepStartMs == 0 && !plans[0].trimmed,
              "cut: a untouched");
        CHECK(plans[1].trimmed && plans[1].keepStartMs == 20000
              && !plans[1].dropped,
              "cut: b keep from 20s (100s-80s)");
    }
    // 完全包含：b 在 a 内 → b 丢弃
    {
        QVector<WallSegment> segs = {
            {QStringLiteral("a"), 0, 200000, 200000},
            {QStringLiteral("b"), 50000, 150000, 100000},
        };
        const auto plans = planOverlapCuts(segs);
        CHECK(plans[1].dropped && plans[1].trimmed,
              "cut: contained segment dropped");
    }
    // 三重重叠链：a[0,100] b[80,180] c[170,280]
    // b 保留 [20,100]（流内）；c 与 b（墙钟止 180）重叠 → c 保留 [10, 尾]
    {
        QVector<WallSegment> segs = {
            {QStringLiteral("a"), 0, 100000, 100000},
            {QStringLiteral("b"), 80000, 180000, 100000},
            {QStringLiteral("c"), 170000, 280000, 110000},
        };
        const auto plans = planOverlapCuts(segs);
        CHECK(plans[1].trimmed && plans[1].keepStartMs == 20000,
              "cut3: b keep 20s");
        CHECK(plans[2].trimmed && plans[2].keepStartMs == 10000,
              "cut3: c keep 10s (180s-170s)");
    }
    // 边界接触：we == ws 不剪
    {
        QVector<WallSegment> segs = {
            {QStringLiteral("a"), 0, 100000, 100000},
            {QStringLiteral("b"), 100000, 200000, 100000},
        };
        const auto plans = planOverlapCuts(segs);
        CHECK(!plans[1].trimmed, "cut: touching boundary not trimmed");
    }
}

// ============================================================================
// 4. 自动命名
// ============================================================================
static void testNaming()
{
    // 正常：通道 + 墙钟起止（QDateTime 本地时区语义——墙钟时间即本地时间）
    const qint64 ws = 1700000000000LL, we = 1700003600000LL;
    const QString n1 = autoOutputName(QStringLiteral("CAM01"),
                                      QStringLiteral("fallback"), ws, we);
    const QString expect = QStringLiteral("CAM01_%1_%2-%3.mp4")
        .arg(QDateTime::fromMSecsSinceEpoch(ws).toString(QStringLiteral("yyyyMMdd")))
        .arg(QDateTime::fromMSecsSinceEpoch(ws).toString(QStringLiteral("HHmmss")))
        .arg(QDateTime::fromMSecsSinceEpoch(we).toString(QStringLiteral("HHmmss")));
    CHECK(n1 == expect, qPrintable(QStringLiteral("name: %1").arg(n1)));
    // 通道缺失 → 组名（fallbackName）+ 墙钟仍命名（Q4：通道缺失用组名）
    const QString n2 = autoOutputName(QString(), QStringLiteral("src"), ws, we);
    const QString expect2 = QStringLiteral("src_%1_%2-%3.mp4")
        .arg(QDateTime::fromMSecsSinceEpoch(ws).toString(QStringLiteral("yyyyMMdd")))
        .arg(QDateTime::fromMSecsSinceEpoch(ws).toString(QStringLiteral("HHmmss")))
        .arg(QDateTime::fromMSecsSinceEpoch(we).toString(QStringLiteral("HHmmss")));
    CHECK(n2 == expect2, qPrintable(QStringLiteral("name: group fallback %1").arg(n2)));
    // 无墙钟 → 回退名
    const QString n3 = autoOutputName(QStringLiteral("CAM01"),
                                      QStringLiteral("src"), 0, 0);
    CHECK(n3 == "CAM01.mp4", "name: no wallclock → fallback");
    // 跨天（起止日期不同）
    const qint64 ws2 = 1699990000000LL, we2 = 1700007000000LL;
    const QString n4 = autoOutputName(QStringLiteral("CAM01"),
                                      QStringLiteral("src"), ws2, we2);
    const QString expect4 = QStringLiteral("CAM01_%1_%2-%3.mp4")
        .arg(QDateTime::fromMSecsSinceEpoch(ws2).toString(QStringLiteral("yyyyMMdd")))
        .arg(QDateTime::fromMSecsSinceEpoch(ws2).toString(QStringLiteral("HHmmss")))
        .arg(QDateTime::fromMSecsSinceEpoch(we2).toString(QStringLiteral("HHmmss")));
    CHECK(n4 == expect4, qPrintable(QStringLiteral("name: cross-day %1").arg(n4)));
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testEncoderProbe();
    testBuildArgs();
    testCutPlans();
    testNaming();
    fprintf(stderr, "v17_test: %d checks, %d failures\n",
            g_checks, g_failures);
    return g_failures ? 1 : 0;
}
