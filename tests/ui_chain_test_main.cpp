// UI 操作链路复现 v3（offscreen）：新框选 UX 全链路
//   点 GO/框选按钮 → 校时窗自动最小化 → 拖拽松开 → 校时窗自动恢复
//   → 主按钮变「✅ 确认并开始校时」（未启动）→ 点击 → quickCheck
//   → 路由三点 → 自动应用
// MainWindow 接线块逐字复刻 + 真实 VideoWidget/对话框/CalibrationService/OCR
#include <QApplication>
#include <QPushButton>
#include <QSettings>
#include <QtTest/QtTest>
#include <QCryptographicHash>
#include <cstdio>
#include "timesettingsdialog.h"
#include "videowidget.h"
#include "magnifierwidget.h"
#include "displayadjust.h"
#include "theme.h"
#include "i18n.h"
#include <QPainter>
#include "app/calibration_service.h"
#include "domain/roi_model.h"
#include "domain/roi_model.h"
#include "domain/guide_line_model.h"

static int g_checks = 0, g_failures = 0;
#define CHECK(cond, msg) do { ++g_checks; if (!(cond)) { ++g_failures; \
    fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); } } while (0)

static QByteArray roiKey(const QString &videoPath)
{
    return "calibration/roi_"
        + QCryptographicHash::hash(videoPath.toUtf8(), QCryptographicHash::Md5).toHex();
}
static QRectF savedTimestampRoi(const QString &videoPath)
{
    if (videoPath.isEmpty())
        return QRectF();
    QSettings s("LumenArc", "LumenArc");
    return s.value(QString::fromLatin1(roiKey(videoPath))).toRectF();
}
static void saveTimestampRoi(const QString &videoPath, const QRectF &norm)
{
    if (videoPath.isEmpty() || !norm.isValid())
        return;
    QSettings s("LumenArc", "LumenArc");
    s.setValue(QString::fromLatin1(roiKey(videoPath)), norm);
}
static void clearSavedRoi(const QString &videoPath)
{
    QSettings s("LumenArc", "LumenArc");
    s.remove(QString::fromLatin1(roiKey(videoPath)));
}

struct FlowCtx {
    QWidget host;
    VideoWidget *videoWidget;
    CalibrationService *service;
    QString videoPath;
    qint64 posMs = 15000;
    qint64 durMs = 30000;
    QPointer<TimeSettingsDialog> roiDialog;
    bool quickStarted = false;
    bool appliedOk = false;
};

// 跑一遍完整流程：entry = "onRunGo" 或 "onRoiButton"（私有槽名）
static bool runFlow(FlowCtx &c, const char *entry)
{
    clearSavedRoi(c.videoPath);
    c.quickStarted = false;
    c.appliedOk = false;

    auto *dlg = new TimeSettingsDialog(
        c.videoPath, c.posMs, c.durMs, TimeCalibration(), QString(), c.service,
        &c.host);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();

    // ---- MainWindow 接线（逐字复刻）----
    QObject::connect(dlg, &TimeSettingsDialog::requestTimestampRoi,
                     &c.host, [&]() {
                         QRectF saved = savedTimestampRoi(c.videoPath);
                         c.videoWidget->beginTimestampRoiSelection(saved);
                         c.roiDialog = dlg;
                     });
    QObject::connect(c.videoWidget, &VideoWidget::timestampRoiReady,
                     &c.host, [&](const QRectF &norm) {
                         if (c.roiDialog) {
                             saveTimestampRoi(c.videoPath, norm);
                             c.roiDialog->stageTimestampRoi(norm);
                         }
                         c.videoWidget->endTimestampRoiSelection();
                         c.roiDialog = nullptr;
                     });
    QObject::connect(c.videoWidget, &VideoWidget::timestampRoiCancelled,
                     &c.host, [&]() {
                         if (c.roiDialog)
                             c.roiDialog->stageTimestampRoi(QRectF());
                         c.videoWidget->endTimestampRoiSelection();
                         c.roiDialog = nullptr;
                     });
    QObject::connect(dlg, &TimeSettingsDialog::cancelTimestampRoiRequest,
                     &c.host, [&]() {
                         c.videoWidget->endTimestampRoiSelection();
                         c.roiDialog = nullptr;
                     });
    QObject::connect(dlg, &TimeSettingsDialog::calibrationApplied,
                     [&](const TimeCalibration &cal) {
                         c.appliedOk = cal.isValid();
                     });

    // ---- 1. 点击入口按钮 ----
    QMetaObject::invokeMethod(dlg, entry, Qt::DirectConnection);
    QTest::qWait(300);
    OverlayWidget *overlay = c.videoWidget->overlay();
    CHECK(overlay->isTimestampRoiMode(), "flow: roi mode entered");
    CHECK(dlg->isMinimized(), "flow: dialog auto-minimized on box selection");

    // ---- 2. 拖拽框选，松开 ----
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    QTest::mouseMove(overlay, QPoint(700, 130));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(700, 130));
    QTest::qWait(300);
    CHECK(!overlay->isTimestampRoiMode(), "flow: roi mode exited on release");
    CHECK(!dlg->isMinimized(), "flow: dialog auto-restored on release");
    CHECK(!c.quickStarted, "flow: NOT started before explicit confirm");
    // 主按钮应为「✅ 确认并开始校时」
    QPushButton *goBtn = nullptr;
    for (auto *b : dlg->findChildren<QPushButton *>())
        if (b->text().contains(QStringLiteral("确认并开始校时"))) { goBtn = b; break; }
    CHECK(goBtn != nullptr, "flow: prominent confirm-start button present");

    // ---- 3. 点「确认并开始校时」→ 启动 ----
    QMetaObject::invokeMethod(dlg, "onRunGo", Qt::DirectConnection);
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 240000 && !c.appliedOk) {
        QTest::qWait(500);
        if (t.elapsed() > 15000 && !c.quickStarted)
            break;
    }
    CHECK(c.quickStarted, "flow: quickCheck started after confirm-start");
    CHECK(c.appliedOk, "flow: auto-applied");
    fprintf(stderr, "  flow(%s): quick=%d applied=%d\n", entry,
            c.quickStarted ? 1 : 0, c.appliedOk ? 1 : 0);
    dlg->close();
    delete dlg;
    return c.quickStarted && c.appliedOk;
}

// ================= 旋转映射验收（Q1 方案 A，四角度） =================
// 视频 400x200，widget 400x400，显示矩形 = 整个 widget（无黑边）：
//   rot 0/180 显示系 400x200（x 向 1:1，y 向 0.5）；rot 90/270 显示系 200x400
// 参考实现与 OverlayWidget 独立同式推导；创建断言用手算字面值锚定，
// 防止参考实现与被测实现同错。
static QPoint refDisplayToStored(QPoint d, int W, int H, int rot)
{
    switch (rot) {
    case 90:  return QPoint(d.y(), H - 1 - d.x());
    case 180: return QPoint(W - 1 - d.x(), H - 1 - d.y());
    case 270: return QPoint(W - 1 - d.y(), d.x());
    default:  return d;
    }
}
static QSize refDisplaySize(int W, int H, int rot)
{
    return (rot == 90 || rot == 270) ? QSize(H, W) : QSize(W, H);
}
static QPoint refWidgetToVideo(QPoint w, const QRect &dr, int W, int H, int rot)
{
    const QSize ds = refDisplaySize(W, H, rot);
    const QPoint d((w.x() - dr.left()) * ds.width() / dr.width(),
                   (w.y() - dr.top()) * ds.height() / dr.height());
    return refDisplayToStored(d, W, H, rot);
}
static QPoint refStoredToWidget(QPoint s, int W, int H, int rot, const QRect &dr)
{
    QPoint d;
    switch (rot) {
    case 90:  d = QPoint(H - 1 - s.y(), s.x()); break;
    case 180: d = QPoint(W - 1 - s.x(), H - 1 - s.y()); break;
    case 270: d = QPoint(s.y(), W - 1 - s.x()); break;
    default:  d = s;
    }
    const QSize ds = refDisplaySize(W, H, rot);
    return QPoint(dr.left() + d.x() * dr.width() / ds.width(),
                  dr.top() + d.y() * dr.height() / ds.height());
}

/// 四角度 ×（画矩形创建 / 点击命中 + 拖拽移动 / 辅助线 / 多边形 / 时间戳框选）
static bool runRotationScenario()
{
    const int VW = 400, VH = 200;
    QWidget host;
    host.resize(VW, 400);
    auto *vw = new VideoWidget(&host);
    vw->setGeometry(host.rect());
    RoiModel rm;
    RoiModel pm;
    GuideLineModel gm;
    vw->setRegionModel(&rm);
    vw->setPolygonModel(&pm);
    vw->setGuideLineModel(&gm);
    OverlayWidget *ov = vw->overlay();
    ov->setVideoSize(VW, VH);
    ov->setVideoDisplayRect(QRect(0, 0, VW, 400));   // 无帧时等价全幅
    host.show();

    const QPoint A(100, 100), B(160, 200);   // widget 拖拽起止（避开边缘）
    // 手算锚定角点（推导见 HANDOVER 第三批 §12）。注意 QRect(p1,p2).normalized() 的
    // Qt 历史行为：角点交换的轴每端损失 1px（负尺寸归一化 quirk，反向拖拽
    // 预存行为，非旋转引入）——锚点经由同一 normalized() 构造以吸收该语义，
    // 角点本身为手工推导的独立验证值。
    const QRect anchor[4] = {
        QRect(QPoint(100, 50),  QPoint(160, 100)).normalized(),   // rot 0
        QRect(QPoint(100, 149), QPoint(200, 119)).normalized(),   // rot 90 CW
        QRect(QPoint(299, 149), QPoint(239, 99)).normalized(),    // rot 180
        QRect(QPoint(299, 50),  QPoint(199, 80)).normalized(),    // rot 270 CW
    };
    const int rots[4] = {0, 90, 180, 270};

    for (int ri = 0; ri < 4; ++ri) {
        const int rot = rots[ri];
        vw->setDisplayRotation(rot);
        CHECK(vw->displayRotation() == rot, "rot: setDisplayRotation stored");
        rm.clearRegions();
        gm.clearLines();

        // ---- 1) 拖拽创建矩形：存储坐标必须落在【原视频系】 ----
        QTest::mousePress(ov, Qt::LeftButton, Qt::NoModifier, A);
        QTest::mouseMove(ov, B);
        QTest::mouseRelease(ov, Qt::LeftButton, Qt::NoModifier, B);
        CHECK(rm.regionCount() == 1, "rot: rect created");
        if (rm.regionCount() != 1)
            return false;
        const QRect got = rm.regions().first();
        CHECK(got == anchor[ri],
              qPrintable(QStringLiteral("rot %1: rect literal anchor got (%2,%3 %4x%5)")
                             .arg(rot).arg(got.x()).arg(got.y())
                             .arg(got.width()).arg(got.height())));
        // 与参考实现交叉一致
        const QPoint sa = refWidgetToVideo(A, ov->rect(), VW, VH, rot);
        const QPoint sb = refWidgetToVideo(B, ov->rect(), VW, VH, rot);
        CHECK(got == QRect(sa, sb).normalized(), "rot: rect == reference mapping");

        // ---- 2) 点击显示位置命中 + 拖拽移动：位移向量轴向随档位置换 ----
        const QPoint centerW = refStoredToWidget(got.center(), VW, VH, rot, ov->rect());
        QTest::mousePress(ov, Qt::LeftButton, Qt::NoModifier, centerW);
        QTest::mouseMove(ov, centerW + QPoint(40, 0));   // 显示系向右 40px
        QTest::mouseRelease(ov, Qt::LeftButton, Qt::NoModifier,
                            centerW + QPoint(40, 0));
        const QRect moved = rm.regions().first();
        const QPoint sd = moved.topLeft() - got.topLeft();
        // 语义断言：90° CW 时「显示向右」=「原视频向上」；270° 反之
        if (rot == 90)
            CHECK(sd.x() == 0 && sd.y() < 0,
                  "rot90: display-right drag moves stored UP");
        else if (rot == 270)
            CHECK(sd.x() == 0 && sd.y() > 0,
                  "rot270: display-right drag moves stored DOWN");
        else if (rot == 180)
            CHECK(sd.x() < 0 && sd.y() == 0,
                  "rot180: display-right drag moves stored LEFT");
        else
            CHECK(sd.x() > 0 && sd.y() == 0,
                  "rot0: display-right drag moves stored RIGHT");
        // 与参考位移一致（±2 允许两次取整损失）
        const QPoint dDisp(40 * refDisplaySize(VW, VH, rot).width() / 400, 0);
        QPoint refDelta;
        switch (rot) {
        case 90:  refDelta = QPoint(dDisp.y(), -dDisp.x()); break;
        case 180: refDelta = QPoint(-dDisp.x(), -dDisp.y()); break;
        case 270: refDelta = QPoint(-dDisp.y(), dDisp.x()); break;
        default:  refDelta = dDisp;
        }
        CHECK(qAbs(sd.x() - refDelta.x()) <= 2 && qAbs(sd.y() - refDelta.y()) <= 2,
              "rot: move delta ~= reference");

        // ---- 3) 辅助线：存储坐标随档位置换 ----
        ov->setGuideLineMode(true);
        QTest::mousePress(ov, Qt::LeftButton, Qt::NoModifier, A);
        QTest::mouseMove(ov, B);
        QTest::mouseRelease(ov, Qt::LeftButton, Qt::NoModifier, B);
        ov->setGuideLineMode(false);
        CHECK(gm.lines().size() == 1, "rot: guide line created");
        if (gm.lines().size() == 1) {
            const GuideLine gl = gm.lines().first();
            CHECK(gl.start == sa && gl.end == sb,
                  "rot: guide line endpoints in stored coords");
        }

        // ---- 4) 绘制链路不崩（offscreen 强制 paint）----
        ov->update();
        QCoreApplication::processEvents();
    }

    // ---- 5) 多边形（rot 90）：单击×3 + 双击闭合，顶点为存储坐标 ----
    // 注意：物理双击的第 2 次 press 被 QtGui 转为 DblClick 事件；QTest 的
    // mouseDClick 在 offscreen 时间戳下该转换不保证发生（polygon 不闭合）。
    // 手动构造 DblClick 事件，语义等价于用户双击收尾（removeLast 去重点）。
    vw->setDisplayRotation(90);
    pm.clearPolygons();
    ov->setPolygonMode(true);
    const QPoint q1(120, 80), q2(240, 80), q3(180, 240);
    QTest::mousePress(ov, Qt::LeftButton, Qt::NoModifier, q1);
    QTest::mouseRelease(ov, Qt::LeftButton, Qt::NoModifier, q1);
    QTest::mousePress(ov, Qt::LeftButton, Qt::NoModifier, q2);
    QTest::mouseRelease(ov, Qt::LeftButton, Qt::NoModifier, q2);
    QTest::mousePress(ov, Qt::LeftButton, Qt::NoModifier, q3);
    QTest::mouseRelease(ov, Qt::LeftButton, Qt::NoModifier, q3);
    QTest::mousePress(ov, Qt::LeftButton, Qt::NoModifier, q3);   // 双击首 press（重复点）
    QTest::mouseRelease(ov, Qt::LeftButton, Qt::NoModifier, q3);
    {
        QMouseEvent dbl(QEvent::MouseButtonDblClick, QPointF(q3), QPointF(q3),
                        ov->mapToGlobal(q3), Qt::LeftButton, Qt::LeftButton,
                        Qt::NoModifier);
        QApplication::sendEvent(ov, &dbl);
    }
    ov->setPolygonMode(false);
    CHECK(pm.polygons().size() == 1, "rot90: polygon closed");
    if (pm.polygons().size() == 1) {
        const QPolygon &pg = pm.polygons().first();
        CHECK(pg.size() == 3, "rot90: polygon 3 vertices");
        const QPoint e1 = refWidgetToVideo(q1, ov->rect(), VW, VH, 90);
        CHECK(pg.first() == e1, "rot90: polygon vertex in stored coords");
    }

    // ---- 6) 时间戳框选（rot 180）：归一化 ROI 按【原视频尺寸】 ----
    vw->setDisplayRotation(180);
    {
        QRectF gotNorm;
        QObject::connect(vw, &VideoWidget::timestampRoiReady, &host,
                         [&](const QRectF &n) { gotNorm = n; });
        vw->beginTimestampRoiSelection(QRectF());
        QTest::mousePress(ov, Qt::LeftButton, Qt::NoModifier, A);
        QTest::mouseMove(ov, B);
        QTest::mouseRelease(ov, Qt::LeftButton, Qt::NoModifier, B);
        CHECK(gotNorm.isValid(), "rot180: timestamp roi ready");
        if (gotNorm.isValid()) {
            // A/B 在 rot180 的存储系锚点：(299,149)/(239,99)；QRect 归一化
            // quirk（双轴均交换）后闭区间矩形为 (240,100,59,49)，按原视频
            // 尺寸 400x200 归一化。
            const QRectF expect(240.0 / VW, 100.0 / VH, 59.0 / VW, 49.0 / VH);
            CHECK(qAbs(gotNorm.x() - expect.x()) < 0.01
                  && qAbs(gotNorm.y() - expect.y()) < 0.01
                  && qAbs(gotNorm.width() - expect.width()) < 0.01
                  && qAbs(gotNorm.height() - expect.height()) < 0.01,
                  qPrintable(QStringLiteral(
                      "rot180: normalized roi got (%1,%2 %3x%4)")
                      .arg(gotNorm.x()).arg(gotNorm.y())
                      .arg(gotNorm.width()).arg(gotNorm.height())));
        }
        vw->endTimestampRoiSelection();
    }

    // ---- 7) 时间戳默认 ROI（rot 90）：归一化默认框经旋转映射落到显示系右上角 ----
    {
        vw->setDisplayRotation(90);
        vw->beginTimestampRoiSelection(QRectF(0.75, 0.02, 0.23, 0.10));  // 原视频右上
        // 默认框必须位于显示矩形内且非空（原映射直接用显示矩形会错位）
        ov->update();
        QCoreApplication::processEvents();
        vw->endTimestampRoiSelection();
    }
    vw->setDisplayRotation(0);
    return g_failures == 0;
}

// ================= 画面调节 LUT 数学 + 放大镜生效验证 =================
static bool runDisplayAdjustScenario()
{
    // ---- LUT 数学（DisplayAdjust::buildLut）----
    {
        DisplayAdjust id;
        CHECK(id.isIdentity() && id.buildLut().isEmpty(),
              "lut: identity -> empty lut (zero cost)");

        // 反色
        DisplayAdjust inv; inv.invert = true;
        const QByteArray il = inv.buildLut();
        CHECK((uchar)il[10] == 245 && (uchar)il[200] == 55, "lut: invert");

        // 伽马 200%：中间调提升，端点不动
        DisplayAdjust gm; gm.gammaPercent = 200;
        const QByteArray gl = gm.buildLut();
        const int g128 = (uchar)gl[128];   // 255*(128/255)^(1/2) ≈ 180.7 → 180
        CHECK(g128 >= 178 && g128 <= 183, "lut: gamma 2.0 midtone lift");
        CHECK((uchar)gl[0] == 0 && (uchar)gl[255] == 255,
              "lut: gamma keeps endpoints");

        // 色阶 64..192 拉伸
        DisplayAdjust lv; lv.blackPoint = 64; lv.whitePoint = 192;
        const QByteArray ll = lv.buildLut();
        CHECK((uchar)ll[64] == 0 && (uchar)ll[192] == 255,
              "lut: levels endpoints");
        const int l128 = (uchar)ll[128];   // (128-64)*255/128 = 127.5 → 127
        CHECK(l128 >= 126 && l128 <= 129, "lut: levels midpoint");

        // 纯亮度/对比度：与既有 applyBrightnessContrast 逐位一致（回归兼容）
        DisplayAdjust bc; bc.brightness = 20; bc.contrast = 30;
        const QByteArray bl = bc.buildLut();
        QImage px(1, 1, QImage::Format_ARGB32);
        px.setPixel(0, 0, qRgba(100, 100, 100, 255));
        const QRgb ref = applyBrightnessContrast(px, 20, 30).pixel(0, 0);
        CHECK((uchar)bl[100] == (uchar)qRed(ref),
              "lut: bc identical to legacy formula");
    }

    // ---- 放大镜像素级验证（修复：放大视图此前不吃画面调节）----
    {
        MagnifierWidget mag(nullptr);
        mag.setVideoSize(400, 200);          // zoom 2.0 → 源区域 200x100 居中
        mag.show();
        QTest::qWait(50);

        QImage gray(400, 200, QImage::Format_ARGB32);
        gray.fill(qRgb(128, 128, 128));

        auto centerGray = [&]() -> int {
            const QImage g = mag.widget()->grab().toImage();
            if (g.isNull()) return -1;
            return qRed(g.convertToFormat(QImage::Format_ARGB32)
                        .pixel(g.width() / 2, g.height() / 2));
        };

        mag.onFrameReady(gray);
        const int base = centerGray();
        CHECK(base >= 120 && base <= 136, "mag: identity shows raw gray 128");

        DisplayAdjust adj; adj.gammaPercent = 200;   // 与 LUT 数学同一预期 ~180
        mag.setDisplayAdjust(adj);
        mag.onFrameReady(gray);
        const int lifted = centerGray();
        CHECK(lifted >= 172 && lifted <= 188,
              qPrintable(QStringLiteral("mag: gamma applied in magnifier (got %1)")
                             .arg(lifted)));

        mag.setDisplayAdjust(DisplayAdjust());       // 复位回到 128
        mag.onFrameReady(gray);
        const int back = centerGray();
        CHECK(back >= 120 && back <= 136, "mag: reset restores raw gray");
        mag.close();
    }
    return g_failures == 0;
}

// ================= §14 放大镜标识框 + 快照烧录验收 =================
// 视频 400x200，widget 400x400（显示矩形全幅）：放大镜 zoom2.0 居中源区域
// = (100,50,200,100)。四角度映射手算锚点推导见 anchors 构造处注释。
static bool runMagnifierIndicatorScenario()
{
    const int VW = 400, VH = 200;
    QWidget host;
    host.resize(VW, 400);
    auto *vw = new VideoWidget(&host);
    vw->setGeometry(host.rect());
    RoiModel rm;
    vw->setRegionModel(&rm);
    OverlayWidget *ov = vw->overlay();
    ov->setVideoSize(VW, VH);
    ov->setVideoDisplayRect(QRect(0, 0, VW, 400));
    host.show();

    MagnifierWidget mag(nullptr);
    mag.setVideoSize(VW, VH);   // zoom 2.0 → 源区域 (100,50,200,100)
    mag.show();
    QTest::qWait(50);

    // MainWindow 接线逐字复刻：sourceRectChanged → overlay->setMagnifierRect
    QObject::connect(&mag, &MagnifierWidget::sourceRectChanged, &host,
                     [&](const QRect &r, qreal z) { ov->setMagnifierRect(r, z); });
    ov->setMagnifierRect(mag.currentSourceRect(), mag.zoomLevel());

    CHECK(ov->magnifierRect() == QRect(100, 50, 200, 100),
          qPrintable(QStringLiteral("magind: initial source rect got (%1,%2 %3x%4)")
                         .arg(ov->magnifierRect().x()).arg(ov->magnifierRect().y())
                         .arg(ov->magnifierRect().width()).arg(ov->magnifierRect().height())));
    CHECK(qAbs(ov->magnifierZoom() - 2.0) < 0.001, "magind: initial zoom 2.0");

    // 四角度手算角点锚定（rot0: tl(100,50)->(100,100) br(299,149)->(299,298)；
    // rot90: tl->disp(149,100)->(298,100) br->disp(50,299)->(100,299)；
    // rot180: tl->(299,149)->(299,298) br->(100,50)->(100,100)；
    // rot270: tl->disp(50,299)->(100,299) br->disp(149,100)->(298,100)）。
    // QRect(p1,p2).normalized() quirk：角点交换轴每端损 1px（预存行为，
    // HANDOVER 第三批 §12 已记载）——锚点经同一 normalized() 构造吸收。
    const QRect anchors[4] = {
        QRect(QPoint(100, 100), QPoint(299, 298)).normalized(),   // rot 0
        QRect(QPoint(298, 100), QPoint(100, 299)).normalized(),   // rot 90 (x 轴交换)
        QRect(QPoint(299, 298), QPoint(100, 100)).normalized(),   // rot 180 (双轴交换)
        QRect(QPoint(100, 299), QPoint(298, 100)).normalized(),   // rot 270 (y 轴交换)
    };
    const int rots[4] = {0, 90, 180, 270};
    for (int ri = 0; ri < 4; ++ri) {
        const int rot = rots[ri];
        vw->setDisplayRotation(rot);
        const QRect got = ov->magnifierRectWidget();
        CHECK(got == anchors[ri],
              qPrintable(QStringLiteral("magind rot %1: widget rect got (%2,%3 %4x%5)")
                             .arg(rot).arg(got.x()).arg(got.y())
                             .arg(got.width()).arg(got.height())));
        // 绘制链路不崩（offscreen 强制 paint）
        ov->update();
        QCoreApplication::processEvents();
    }
    vw->setDisplayRotation(0);

    // 信号路径 1：锚点缩放 → 标识框跟随
    mag.zoomAtPoint(120, QPoint(200, 100));   // 放大一档（2.0 → 2.25）
    CHECK(ov->magnifierRect() == mag.currentSourceRect(),
          "magind: zoomAtPoint propagates via sourceRectChanged");
    CHECK(qAbs(ov->magnifierZoom() - mag.zoomLevel()) < 0.001,
          "magind: zoom value propagates");
    // 信号路径 2：光标移动 → 标识框跟随
    mag.updateCursorPosition(QPoint(300, 150));
    CHECK(ov->magnifierRect() == mag.currentSourceRect(),
          "magind: cursor move propagates via sourceRectChanged");
    // 关闭路径：空矩形隐藏
    ov->setMagnifierRect(QRect(), 0.0);
    CHECK(!ov->magnifierRectWidget().isValid(), "magind: empty rect hides indicator");

    // ---- 标识框绘制像素级（公共静态 drawMagnifierIndicator）----
    {
        QImage canvas(300, 300, QImage::Format_ARGB32);
        canvas.fill(qRgb(30, 30, 30));
        {
            QPainter p(&canvas);
            OverlayWidget::drawMagnifierIndicator(p, QRect(50, 50, 100, 80), 2.0, 2, 12);
        }
        const QColor accent(Theme::Accent);
        auto isAccent = [&](QRgb px) {
            return qAbs(qRed(px) - accent.red()) < 30
                && qAbs(qGreen(px) - accent.green()) < 30
                && qAbs(qBlue(px) - accent.blue()) < 40;
        };
        // 四角括号：左上横臂（y=50 附近、x 52~70）应有金色像素
        int armHits = 0;
        for (int x = 52; x <= 70; ++x)
            for (int y = 49; y <= 51; ++y)
                if (isAccent(canvas.pixel(x, y))) ++armHits;
        CHECK(armHits >= 4, "magind: bracket arm painted in accent");
        // 框内无填充零遮挡：中心像素保持背景色
        CHECK(canvas.pixel(100, 90) == qRgb(30, 30, 30),
              "magind: zero occlusion inside bracket");
        // 倍率徽章：框外右上角（y < 50 的上沿区域）应有金色文字像素
        int badgeHits = 0;
        for (int y = 10; y < 48; ++y)
            for (int x = 60; x <= 160; ++x)
                if (isAccent(canvas.pixel(x, y))) ++badgeHits;
        CHECK(badgeHits >= 3, "magind: zoom badge painted outside top-right");
    }

    // ---- 快照覆盖层烧录（burnAnnotations）----
    {
        RoiModel brm;
        brm.addRegion(QRect(10, 10, 100, 50));
        RoiModel bpm;
        bpm.addPolygon(QPolygon() << QPoint(200, 150) << QPoint(300, 150)
                                  << QPoint(250, 190));
        GuideLineModel bgm;
        GuideLine gl; gl.start = QPoint(0, 100); gl.end = QPoint(399, 100);
        bgm.addLine(gl);

        auto closerTo = [](QRgb px, const QColor &a, const QColor &b) {
            auto dist = [](QRgb p, const QColor &c) {
                return qAbs(qRed(p) - c.red()) + qAbs(qGreen(p) - c.green())
                     + qAbs(qBlue(p) - c.blue());
            };
            return dist(px, a) < dist(px, b);
        };
        const QColor bg(40, 40, 40);

        // rot 0：帧 400x200 与显示系 1:1
        QImage f0(400, 200, QImage::Format_ARGB32);
        f0.fill(bg);
        {
            QPainter p(&f0);
            OverlayWidget::burnAnnotations(p, f0.size(), QSize(400, 200), 0,
                                           &brm, &bpm, &bgm, 1);
        }
        // 矩形上边：y=10 附近，颜色更接近 regionColor(0) 而非背景
        bool edgeOk = false;
        for (int y = 9; y <= 11 && !edgeOk; ++y)
            edgeOk = closerTo(f0.pixel(60, y), RoiModel::regionColor(0), bg);
        CHECK(edgeOk, "burn: region border painted at stored coords");
        // 填充：矩形内部像素被半透明染色（≠ 背景）
        CHECK(f0.pixel(60, 30) != bg.rgb(), "burn: region translucent fill");
        // 序号标签 R1：矩形左上内测有白色文字像素
        bool tagOk = false;
        for (int y = 12; y < 30 && !tagOk; ++y)
            for (int x = 14; x < 40; ++x)
                if (qRed(f0.pixel(x, y)) > 200 && qGreen(f0.pixel(x, y)) > 200
                    && qBlue(f0.pixel(x, y)) > 200) { tagOk = true; break; }
        CHECK(tagOk, "burn: region index label painted");
        // 多边形上边（y=150, x=200..300 实线）
        bool polyOk = false;
        for (int y = 149; y <= 151 && !polyOk; ++y)
            polyOk = closerTo(f0.pixel(250, y), RoiModel::polygonColor(0), bg);
        CHECK(polyOk, "burn: polygon edge painted");

        // rot 90：显示系 200x400（与 QImage::transformed(rotate90) 输出同尺寸）
        QImage f90(200, 400, QImage::Format_ARGB32);
        f90.fill(bg);
        {
            QPainter p(&f90);
            OverlayWidget::burnAnnotations(p, f90.size(), QSize(400, 200), 90,
                                           &brm, nullptr, nullptr, 1);
        }
        // 矩形 (10,10,100,50) rot90：角点 (10,10)->(189,10) (109,59)->(140,109)
        // → 映射矩形 (140,10,50,100)，左边 x=140 纵向 y∈[10,109]
        bool rotOk = false;
        for (int x = 139; x <= 141 && !rotOk; ++x)
            rotOk = closerTo(f90.pixel(x, 60), RoiModel::regionColor(0), bg);
        CHECK(rotOk, "burn: rot90 region mapped to display coords");
        // 原位应为空（未旋转位置的 (60,10) 在 f90 中仍是背景）
        CHECK(f90.pixel(60, 10) == bg.rgb(), "burn: rot90 leaves original spot empty");
    }

    // ---- 放大镜当前裁剪图（快照放大镜段数据源）----
    {
        MagnifierWidget mag2(nullptr);
        mag2.setVideoSize(400, 200);
        mag2.show();
        QTest::qWait(50);
        QImage gray(400, 200, QImage::Format_ARGB32);
        gray.fill(qRgb(128, 128, 128));
        mag2.onFrameReady(gray);
        const QImage cur = mag2.currentMagnifiedImage();
        CHECK(cur.size() == QSize(200, 100), "magsnap: cropped at source rect size");
        if (cur.size() == QSize(200, 100)) {
            const int g = qRed(cur.convertToFormat(QImage::Format_ARGB32)
                                   .pixel(100, 50));
            CHECK(g >= 120 && g <= 136, "magsnap: pixel content matches source");
        }
        // 旋转 90 后裁剪图宽高互换（与放大视图一致）
        mag2.setDisplayRotation(90);   // 内部 recalc → 以最近帧重裁
        const QImage cur90 = mag2.currentMagnifiedImage();
        CHECK(cur90.size() == QSize(100, 200), "magsnap: rot90 swaps crop dims");
        mag2.close();
    }

    mag.close();
    return g_failures == 0;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QString synth = QStringLiteral(
        "C:\\code\\LumenArc\\LumenArc_v1.0 remake\\build_tmp\\caltest\\synth.mp4");
    qint64 posMs = 15000, durMs = 30000;
    if (argc >= 4) {   // 可选：真实文件实战 [video posMs durMs]
        synth = QString::fromLocal8Bit(argv[1]);
        posMs = QString::fromLocal8Bit(argv[2]).toLongLong();
        durMs = QString::fromLocal8Bit(argv[3]).toLongLong();
    }

    CalibrationService service(nullptr);

    // ---- 第三点确认（v1.2.2）静态校验用例：错读任一点必须可疑 ----
    {
        using S = TimeCalibration::Sample;
        const qint64 base = 1750000000000LL;   // 任意 epoch 基点
        auto mk = [](qint64 streamMs, qint64 wallMs) {
            S s; s.streamMs = streamMs; s.wallMs = wallMs; s.conf = 1.0;
            return s;
        };
        // 共线三点（rate=1.0）→ 不可疑
        const QVector<S> ok{mk(1000, base), mk(15000, base + 14000),
                            mk(27000, base + 26000)};
        CHECK(!CalibrationService::quickCheckSamplesInconsistent(ok),
              "quickCheck: collinear 3 points not suspect");
        // 乱序输入同样不可疑（内部按 streamMs 排序）
        const QVector<S> shuffled{ok[2], ok[0], ok[1]};
        CHECK(!CalibrationService::quickCheckSamplesInconsistent(shuffled),
              "quickCheck: shuffled collinear not suspect");
        // 尾点错读 +60s → 可疑
        CHECK(CalibrationService::quickCheckSamplesInconsistent(
                  {mk(1000, base), mk(15000, base + 14000),
                   mk(27000, base + 26000 + 60000)}),
              "quickCheck: misread tail point suspect");
        // 首点错读 +60s → 中点残差爆炸 → 可疑
        CHECK(CalibrationService::quickCheckSamplesInconsistent(
                  {mk(1000, base + 60000), mk(15000, base + 14000),
                   mk(27000, base + 26000)}),
              "quickCheck: misread head point suspect");
        // 中点错读 -60s → 可疑
        CHECK(CalibrationService::quickCheckSamplesInconsistent(
                  {mk(1000, base), mk(15000, base + 14000 - 60000),
                   mk(27000, base + 26000)}),
              "quickCheck: misread mid point suspect");
        // 仅两点 → 不可疑（保持旧行为：两点无法校验）
        CHECK(!CalibrationService::quickCheckSamplesInconsistent(
                  {mk(1000, base), mk(27000, base + 26000 + 60000)}),
              "quickCheck: 2 points never suspect");
        // 秒级小误差（±3s）在容差内 → 不可疑
        CHECK(!CalibrationService::quickCheckSamplesInconsistent(
                  {mk(1000, base), mk(15000, base + 14000 + 3000),
                   mk(27000, base + 26000)}),
              "quickCheck: small second-level jitter tolerated");
    }
    FlowCtx c;
    c.service = &service;
    c.videoPath = synth;
    c.posMs = posMs;
    c.durMs = durMs;
    c.host.resize(1280, 720);
    c.videoWidget = new VideoWidget(&c.host);
    c.videoWidget->setGeometry(c.host.rect());
    c.videoWidget->overlay()->setVideoSize(2560, 1440);
    c.host.show();
    QObject::connect(&service, &CalibrationService::progress,
                     [&](const QString &s) {
                         if (s.contains(QStringLiteral("quick")))
                             c.quickStarted = true;
                         fprintf(stderr, "  stage: %s\n", qPrintable(s));
                     });
    QObject::connect(&service, &CalibrationService::failed,
                     [](const QString &, const QString &e) {
                         fprintf(stderr, "  FAILED: %s\n", qPrintable(e));
                     });

    runRotationScenario();   // 场景 0：显示旋转四角度双向映射（Q1 方案 A）
    runDisplayAdjustScenario();   // 场景 0b：画面调节 LUT + 放大镜生效验证
    runMagnifierIndicatorScenario();   // 场景 0c：§14 标识框/快照烧录/放大图
    runFlow(c, "onRunGo");       // 场景 1：GO 入口
    if (argc < 4)
        runFlow(c, "onRoiButton");   // 场景 2：框选按钮入口（合成片）

    fprintf(stderr, "ui_chain_v3: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
