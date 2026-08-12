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

    runFlow(c, "onRunGo");       // 场景 1：GO 入口
    if (argc < 4)
        runFlow(c, "onRoiButton");   // 场景 2：框选按钮入口（合成片）

    fprintf(stderr, "ui_chain_v3: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
