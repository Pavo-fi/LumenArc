/**
 * @file snapshot_test_main.cpp
 * @brief v1.17.0 P-80 C2：快照合成纯模块回归闸
 *
 * 覆盖：
 *  1. FrameAnnotation 映射单元（0/90/180/270 + 缩放，手算期望）
 *  2. SnapshotComposer::timeCode（相对时间 / 北京时间格式+一致性）
 *  3. compose 像素断言（ROI 烧录色 / 辅助线 / 分段堆叠几何 / OSD 文本 / 标题条）
 *  4. 放大镜分屏（宽度合成 / 无独立放大镜段）
 *  5. 旋转元数据（PNG text "LumenArc:displayRotation"）
 */
#include <QApplication>
#include <QImage>
#include <QPainter>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QRegularExpression>
#include <cstdio>

#include "frame_annotation.h"
#include "app/snapshot_composer.h"
#include "domain/roi_model.h"
#include "domain/guide_line_model.h"
#include "domain/time_calibration.h"
#include "theme.h"

static int g_checks = 0, g_failures = 0;
#define CHECK(cond, msg)                                              \
    do {                                                              \
        ++g_checks;                                                   \
        if (!(cond)) {                                                \
            ++g_failures;                                             \
            fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__,   \
                    msg);                                             \
        }                                                             \
    } while (0)

static bool closeTo(const QColor &a, const QColor &b, int tol = 8)
{
    return qAbs(a.red() - b.red()) <= tol
        && qAbs(a.green() - b.green()) <= tol
        && qAbs(a.blue() - b.blue()) <= tol;
}

/// 窗口内是否存在近似目标色的像素（抗锯齿容忍；像素断言先例 = vla_load_test）
static bool windowHasColor(const QImage &img, int cx, int cy, int rad,
                           const QColor &target, int tol = 12)
{
    for (int dy = -rad; dy <= rad; ++dy)
        for (int dx = -rad; dx <= rad; ++dx) {
            const int x = cx + dx, y = cy + dy;
            if (x < 0 || y < 0 || x >= img.width() || y >= img.height())
                continue;
            if (closeTo(img.pixelColor(x, y), target, tol))
                return true;
        }
    return false;
}

static void testMapUnit()
{
    // 0° 恒等
    CHECK(FrameAnnotation::mapStoredPointToFrame(QPoint(5, 7), QSize(100, 100),
                                    QSize(100, 100), 0) == QPoint(5, 7),
          "map 0° identity");
    // 0° 等比缩放 2x
    CHECK(FrameAnnotation::mapStoredPointToFrame(QPoint(50, 50), QSize(200, 200),
                                    QSize(100, 100), 0) == QPoint(100, 100),
          "map 0° 2x scale");
    // 90°：存储 (x,y) @ (W,H) → 显示 (H-1-y, x)；显示尺寸 (H,W)
    CHECK(FrameAnnotation::mapStoredPointToFrame(QPoint(0, 0), QSize(50, 100),
                                    QSize(100, 50), 90) == QPoint(49, 0),
          "map 90° origin");
    CHECK(FrameAnnotation::mapStoredPointToFrame(QPoint(99, 49), QSize(50, 100),
                                    QSize(100, 50), 90) == QPoint(0, 99),
          "map 90° far corner");
    // 180°：(W-1-x, H-1-y)
    CHECK(FrameAnnotation::mapStoredPointToFrame(QPoint(10, 20), QSize(100, 50),
                                    QSize(100, 50), 180) == QPoint(89, 29),
          "map 180°");
    // 270°：(y, W-1-x)
    CHECK(FrameAnnotation::mapStoredPointToFrame(QPoint(10, 20), QSize(50, 100),
                                    QSize(100, 50), 270) == QPoint(20, 89),
          "map 270°");
    // 矩形 = 两点映射归一化（y 轴 2x 缩放：frame100x100 / disp100x50）
    // TL(10,20)→旋转(89,29)→缩放(89,58)；BR(39,29)→旋转(60,20)→缩放(60,40)
    const QRect r = FrameAnnotation::mapStoredRectToFrame(QRect(10, 20, 30, 10),
                                             QSize(100, 100), QSize(100, 50), 180);
    CHECK(r == QRect(QPoint(89, 58), QPoint(60, 40)).normalized(),
          "map rect 180° normalized");
    // 显示尺寸
    CHECK(FrameAnnotation::displaySizeForRotation(QSize(100, 50), 90) == QSize(50, 100),
          "displaySize 90° swap");
    CHECK(FrameAnnotation::displaySizeForRotation(QSize(100, 50), 0) == QSize(100, 50),
          "displaySize 0° identity");
}

static void testTimeCode()
{
    using SC = SnapshotComposer;
    // 相对时间：dateKnown=false，offset 0 → 流内即相对
    auto tc = SC::timeCode(TimeCalibration(), 3661000);   // 1h01m01s
    CHECK(tc.first == QStringLiteral("01:01:01"), "timecode relative text");
    CHECK(tc.second == QStringLiteral("t01-01-01"), "timecode relative tag");
    // 负偏移夹零
    auto tcNeg = SC::timeCode(TimeCalibration(), -500);
    CHECK(tcNeg.first == QStringLiteral("00:00:00"), "timecode negative clamps to 0");
    // 北京时间：格式 + 与 tag 一致性（避开机器时区差异：tag 由同一 dt 导出）
    TimeCalibration cal;
    cal.dateKnown = true;
    cal.offsetMs = 1787033483000LL;   // 任意 epoch
    auto tcb = SC::timeCode(cal, 61000);
    CHECK(tcb.first.length() == 19, "timecode beijing format len 19");
    CHECK(QRegularExpression(QStringLiteral("^\\d{4}-\\d{2}-\\d{2} \\d{2}:\\d{2}:\\d{2}$"))
              .match(tcb.first).hasMatch(),
          "timecode beijing format yyyy-MM-dd HH:mm:ss");
    CHECK(QRegularExpression(QStringLiteral("^\\d{8}_\\d{6}$"))
              .match(tcb.second).hasMatch(),
          "timecode beijing tag yyyyMMdd_HHmmss");
    // 一致性：tag = text 去分隔符（- 和 : 去掉，空格→_）
    QString derived = tcb.first;
    derived.replace('-', QString()).replace(':', QString());
    derived.replace(' ', '_');
    CHECK(tcb.second == derived, "timecode beijing tag consistent with text");
}

static void testComposePixels()
{
    RoiModel roi;
    roi.addRegion(QRect(100, 100, 80, 60));
    GuideLineModel gl;
    GuideLine g;
    g.start = QPoint(0, 240);
    g.end = QPoint(640, 240);
    g.color = QColor(0, 200, 255);
    gl.addLine(g);

    QImage frame(640, 480, QImage::Format_RGB32);
    frame.fill(QColor(0x40, 0x40, 0x40));
    QImage chart(640, 420, QImage::Format_RGB32);
    chart.fill(QColor(30, 60, 120));
    QImage spec(640, 300, QImage::Format_RGB32);
    spec.fill(QColor(20, 80, 40));

    SnapshotInputs in;
    in.frame = frame;
    in.videoSize = QSize(640, 480);
    in.regions = &roi;
    in.guideLines = &gl;
    in.chartImg = chart;
    in.specImg = spec;
    in.fileName = QStringLiteral("demo.mp4");
    in.labelText = QStringLiteral("火点出现");

    const QImage out = SnapshotComposer::compose(in);
    // 几何：视频480 + (2+34+420) + (2+34+300) = 1272
    CHECK(out.size() == QSize(640, 1272), "compose stacked geometry 640x1272");

    // ROI 顶边（vermillion 213,94,0；1px 线 + antialias 与底色混合 → 查色相主导像素）
    bool roiEdge = false;
    for (int y = 96; y <= 104 && !roiEdge; ++y)
        for (int x = 120; x <= 160; ++x) {
            const QColor c = out.pixelColor(x, y);
            if (c.red() > 140 && c.red() - c.blue() > 100 && c.blue() < 90)
                roiEdge = true;   // vermillion 色相（含 AA 混合）
        }
    CHECK(roiEdge, "ROI top edge burned in model color hue");
    // ROI 内部半透明填充（底色 64 + vermillion alpha20 混合）
    const QColor inside = out.pixelColor(140, 130);
    CHECK(inside.red() > 64 && inside.red() < 100, "ROI translucent fill over bg");
    // 辅助线（0,200,255 青色；1px 虚线 + AA → 查蓝色主导像素）
    bool glPixel = false;
    for (int y = 234; y <= 246 && !glPixel; ++y)
        for (int x = 100; x <= 500; ++x) {
            const QColor c = out.pixelColor(x, y);
            if (c.blue() > 120 && c.blue() - c.red() > 60)
                glPixel = true;
        }
    CHECK(glPixel, "guide line burned");

    // 分段堆叠：chart 段起点 y = 480+2+34 = 516
    CHECK(out.pixelColor(320, 700) == QColor(30, 60, 120), "chart section placed");
    // spec 段：480 + 456 + 2 + 34 = 972 起
    CHECK(out.pixelColor(320, 1100) == QColor(20, 80, 40), "spec section placed");
    // 分隔线 0x2A
    CHECK(out.pixelColor(320, 481) == QColor(0x2A, 0x2A, 0x2A), "divider row 0x2A");
    // 标题条 Accent 竖条（x=14..17，chart 段标题 y=482..515 中部）
    CHECK(closeTo(out.pixelColor(15, 482 + (34 - 16) / 2), QColor(Theme::Accent), 4),
          "title bar accent stripe");

    // OSD 文本：视频区底部带（y≈440..478）存在亮色文字像素（标签=金色 Accent）
    bool labelText = false;
    for (int x = 0; x < 640 && !labelText; x += 2)
        for (int y = 430; y < 480; y++)
            if (closeTo(out.pixelColor(x, y), QColor(Theme::Accent), 24))
                labelText = true;
    CHECK(labelText, "OSD gold label text present in bottom band");
    // 文件名白色文本
    bool fileText = false;
    for (int x = 0; x < 640 && !fileText; x += 2)
        for (int y = 430; y < 480; y++)
            if (out.pixelColor(x, y).lightness() > 220)
                fileText = true;
    CHECK(fileText, "OSD white file/time text present");

    // 无放大镜 → 无放大镜段（总高即 1272 已证）
    // 空帧 → null
    SnapshotInputs empty = in;
    empty.frame = QImage();
    CHECK(SnapshotComposer::compose(empty).isNull(), "compose null on empty frame");
}

static void testMagnifierSplit()
{
    QImage frame(640, 480, QImage::Format_RGB32);
    frame.fill(QColor(0x30, 0x30, 0x30));
    QImage mag(960, 480, QImage::Format_RGB32);   // 放大视图 2x 宽
    mag.fill(QColor(0x60, 0x50, 0x20));

    SnapshotInputs in;
    in.frame = frame;
    in.videoSize = QSize(640, 480);
    in.hasMagnifier = true;
    in.magnifierSourceRect = QRect(0, 0, 320, 240);
    in.magnifierZoom = 2.0;
    in.magnifiedImage = mag;

    const QImage out = SnapshotComposer::compose(in);
    // 分屏：640 + gap(4) + 960 = 1604；无分析段 → 高 = 480
    CHECK(out.size() == QSize(1604, 480), "magnifier split geometry 1604x480");
    // 左半 = 原帧底色（避开源区边缘的金色括号，取内部点）
    CHECK(closeTo(out.pixelColor(160, 120), QColor(0x30, 0x30, 0x30), 6),
          "split left = original frame");
    // 右半 = 放大图色
    CHECK(closeTo(out.pixelColor(640 + 4 + 480, 240), QColor(0x60, 0x50, 0x20), 12),
          "split right = magnified image");
    // 金色标识括号烧录在左半（源区 = 左半全幅 → 括号在边缘）
    CHECK(windowHasColor(out, 4, 4, 12, QColor(Theme::Accent), 30)
            || windowHasColor(out, 4, 476, 12, QColor(Theme::Accent), 30)
            || windowHasColor(out, 636, 4, 12, QColor(Theme::Accent), 30),
          "magnifier gold brackets burned on left half");
}

static void testRotationMeta()
{
    QTemporaryDir tmp;
    QImage frame(480, 640, QImage::Format_RGB32);   // 显示系（90° 后）
    frame.fill(Qt::black);

    SnapshotInputs in;
    in.frame = frame;
    in.videoSize = QSize(640, 480);   // 原生
    in.rotation = 90;
    const QImage out = SnapshotComposer::compose(in);
    CHECK(!out.isNull(), "rotated compose not null");
    const QString f1 = tmp.path() + "/r90.png";
    CHECK(out.save(f1, "PNG"), "rotated png saved");
    QImage back(f1);
    CHECK(back.text(QStringLiteral("LumenArc:displayRotation"))
              .toInt() == 90,
          "png text metadata rotation=90");

    // rotation 0 → 无元数据（旧产物字节级一致）
    SnapshotInputs in0 = in;
    in0.rotation = 0;
    in0.frame = QImage(480, 640, QImage::Format_RGB32);
    in0.frame.fill(Qt::black);
    const QString f0 = tmp.path() + "/r0.png";
    const QImage out0 = SnapshotComposer::compose(in0);
    CHECK(out0.save(f0, "PNG"), "non-rotated png saved");
    QImage back0(f0);
    CHECK(!back0.textKeys().contains(QStringLiteral("LumenArc:displayRotation")),
          "rotation 0 leaves no metadata");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testMapUnit();
    testTimeCode();
    testComposePixels();
    testMagnifierSplit();
    testRotationMeta();
    fprintf(stderr, "snapshot_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}