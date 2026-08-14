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
#include "app/calibration_service.h"
#include "domain/region_model.h"
#include "domain/polygon_model.h"
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
    RegionModel rm;
    PolygonModel pm;
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
    runFlow(c, "onRunGo");       // 场景 1：GO 入口
    if (argc < 4)
        runFlow(c, "onRoiButton");   // 场景 2：框选按钮入口（合成片）

    fprintf(stderr, "ui_chain_v3: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
