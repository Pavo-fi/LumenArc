// UI 操作链路复现 v2（offscreen）：MainWindow 接线块逐字复刻 + 真实
// VideoWidget（含 m_overlay→VideoWidget 信号转发）+ 真实 TimeSettingsDialog
// + 真实 CalibrationService/OCR。流程：GO → requestTimestampRoi → 框选模式
// → 鼠标拖拽 → 点「✓ 确认」→ VideoWidget 转发 → λ → setTimestampRoi
// → quickCheck → 路由三点 → 自动应用（calibrationApplied）
#include <QApplication>
#include <QPushButton>
#include <QSignalSpy>
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

// ---- MainWindow 等价物：savedTimestampRoi/saveTimestampRoi 逐字复刻 ----
static QRectF savedTimestampRoi(const QString &videoPath)
{
    if (videoPath.isEmpty())
        return QRectF();
    QSettings s("LumenArc", "LumenArc");
    const QByteArray key = "calibration/roi_"
        + QCryptographicHash::hash(videoPath.toUtf8(), QCryptographicHash::Md5).toHex();
    return s.value(QString::fromLatin1(key)).toRectF();
}
static void saveTimestampRoi(const QString &videoPath, const QRectF &norm)
{
    if (videoPath.isEmpty() || !norm.isValid())
        return;
    QSettings s("LumenArc", "LumenArc");
    const QByteArray key = "calibration/roi_"
        + QCryptographicHash::hash(videoPath.toUtf8(), QCryptographicHash::Md5).toHex();
    s.setValue(QString::fromLatin1(key), norm);
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    // 清掉历史 ROI（模拟首次使用；否则 GO 直接 startGo 跳过框选）
    const QString synth = QStringLiteral(
        "C:\\code\\LumenArc\\LumenArc_v1.0 remake\\build_tmp\\caltest\\synth.mp4");
    {
        QSettings s("LumenArc", "LumenArc");
        const QByteArray key = "calibration/roi_"
            + QCryptographicHash::hash(synth.toUtf8(), QCryptographicHash::Md5).toHex();
        s.remove(QString::fromLatin1(key));
    }

    CalibrationService service(nullptr);
    QWidget host;   // 模拟主窗口容器
    host.resize(1280, 720);
    VideoWidget *videoWidget = new VideoWidget(&host);
    videoWidget->setGeometry(host.rect());
    videoWidget->overlay()->setVideoSize(1280, 720);   // openVideoFile 的同步（95ced1f）
    host.show();

    QString m_currentVideoPath = synth;
    QPointer<TimeSettingsDialog> m_roiDialog;

    auto *dlg = new TimeSettingsDialog(
        synth, 15000, 30000, TimeCalibration(), QString(), &service, &host);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->show();

    // 恢复已保存 ROI（此处为空）——逐字复刻 onSetStartTime
    const QRectF savedRoi = savedTimestampRoi(m_currentVideoPath);
    if (savedRoi.isValid())
        dlg->setTimestampRoi(savedRoi);

    // ---- MainWindow onSetStartTime 的 connect 块（逐字复刻）----
    QObject::connect(dlg, &TimeSettingsDialog::requestTimestampRoi,
                     &host, [&]() {
                         QRectF saved = savedTimestampRoi(m_currentVideoPath);
                         videoWidget->beginTimestampRoiSelection(saved);
                         m_roiDialog = dlg;
                     });
    QObject::connect(videoWidget, &VideoWidget::timestampRoiConfirmed,
                     &host, [&](const QRectF &norm) {
                         if (m_roiDialog) {
                             saveTimestampRoi(m_currentVideoPath, norm);
                             m_roiDialog->setTimestampRoi(norm);
                         }
                         videoWidget->endTimestampRoiSelection();
                         m_roiDialog = nullptr;
                     });
    QObject::connect(dlg, &TimeSettingsDialog::cancelTimestampRoiRequest,
                     &host, [&]() {
                         videoWidget->endTimestampRoiSelection();
                         m_roiDialog = nullptr;
                     });
    QObject::connect(videoWidget, &VideoWidget::timestampRoiCancelled,
                     &host, [&]() {
                         if (m_roiDialog)
                             m_roiDialog->setTimestampRoi(QRectF());
                         videoWidget->endTimestampRoiSelection();
                         m_roiDialog = nullptr;
                     });

    // ---- 观测 ----
    bool quickStarted = false, routedThreePoint = false, appliedOk = false;
    QObject::connect(&service, &CalibrationService::progress,
                     [&](const QString &s) {
                         if (s.contains(QStringLiteral("quick"))) quickStarted = true;
                         fprintf(stderr, "  stage: %s\n", qPrintable(s));
                     });
    QObject::connect(&service, &CalibrationService::quickCheckReady,
                     [](const QString &, double rate, bool susp) {
                         fprintf(stderr, "  quickCheck: rate=%.3f susp=%d\n",
                                 rate, susp ? 1 : 0);
                     });
    QObject::connect(&service, &CalibrationService::threePointReady,
                     [&](const QString &, const TimeCalibration &) {
                         routedThreePoint = true;
                     });
    QObject::connect(&service, &CalibrationService::failed,
                     [](const QString &, const QString &e) {
                         fprintf(stderr, "  FAILED: %s\n", qPrintable(e));
                     });
    QObject::connect(dlg, &TimeSettingsDialog::calibrationApplied,
                     [&](const TimeCalibration &cal) {
                         appliedOk = cal.isValid();
                         fprintf(stderr, "  applied: valid=%d offset=%lld\n",
                                 cal.isValid() ? 1 : 0, (long long)cal.offsetMs);
                     });

    // ---- 用户操作：点 GO ----
    QMetaObject::invokeMethod(dlg, "onRunGo", Qt::DirectConnection);
    QTest::qWait(300);
    OverlayWidget *overlay = videoWidget->overlay();
    CHECK(overlay->isTimestampRoiMode(),
          "chain: overlay entered timestamp-roi mode after GO");
    fprintf(stderr, "  roi mode after GO: %d\n",
            overlay->isTimestampRoiMode() ? 1 : 0);

    // ---- 用户操作：拖拽框选（左上区域，synth OSD 位置）----
    QTest::mousePress(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
    QTest::mouseMove(overlay, QPoint(700, 130));
    QTest::mouseRelease(overlay, Qt::LeftButton, Qt::NoModifier, QPoint(700, 130));
    QTest::qWait(100);

    // ---- 用户操作：点「✓ 确认」----
    QPushButton *confirm = nullptr;
    for (auto *b : overlay->findChildren<QPushButton *>())
        if (b->text().contains(QStringLiteral("确认"))) { confirm = b; break; }
    CHECK(confirm && confirm->isVisible(), "chain: confirm button visible");
    if (!confirm) {
        fprintf(stderr, "ui_chain_v2: %d checks, %d failures (no confirm btn)\n",
                g_checks, g_failures);
        return 1;
    }
    QTest::mouseClick(confirm, Qt::LeftButton);
    QTest::qWait(300);
    CHECK(!overlay->isTimestampRoiMode(), "chain: roi mode exited after confirm");

    // ---- 等待链路抵达：quickCheck → 三点 → 自动应用 ----
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 240000 && !appliedOk) {
        QTest::qWait(500);
        if (t.elapsed() > 15000 && !quickStarted)
            break;   // 15s 未启动 quickcheck = 链路断
    }
    CHECK(quickStarted, "chain: quickCheck started after confirm (KEY LINK)");
    CHECK(routedThreePoint, "chain: routed to threePoint (normal file)");
    CHECK(appliedOk, "chain: auto-applied (calibrationApplied)");

    // ---- 持久化：QSettings 已存 ROI ----
    CHECK(savedTimestampRoi(synth).isValid(), "chain: ROI persisted to QSettings");

    fprintf(stderr, "ui_chain_v2: %d checks, %d failures\n", g_checks, g_failures);
    if (g_failures)
        return 1;

    // ================= 第二场景：「框选时间戳区域」按钮路径 =================
    // 用户不点 GO、直接点框选按钮 → 框选确认后必须自动继续（现场反馈：
    // 框选完了不抵达下一步——onRoiButton 此前不置 m_goStage）
    {
        QSettings s("LumenArc", "LumenArc");
        const QByteArray key = "calibration/roi_"
            + QCryptographicHash::hash(synth.toUtf8(), QCryptographicHash::Md5).toHex();
        s.remove(QString::fromLatin1(key));
    }
    {
        bool quick2 = false, applied2 = false;
        auto *dlg2 = new TimeSettingsDialog(
            synth, 15000, 30000, TimeCalibration(), QString(), &service, &host);
        dlg2->setAttribute(Qt::WA_DeleteOnClose);
        dlg2->show();
        QObject::connect(dlg2, &TimeSettingsDialog::requestTimestampRoi,
                         &host, [&]() {
                             videoWidget->beginTimestampRoiSelection(QRectF());
                             m_roiDialog = dlg2;
                         });
        QObject::connect(&service, &CalibrationService::progress,
                         [&](const QString &s) {
                             if (s.contains(QStringLiteral("quick"))) quick2 = true;
                         });
        QObject::connect(dlg2, &TimeSettingsDialog::calibrationApplied,
                         [&](const TimeCalibration &cal) {
                             applied2 = cal.isValid();
                         });
        // 点「框选时间戳区域」按钮（onRoiButton）
        QMetaObject::invokeMethod(dlg2, "onRoiButton", Qt::DirectConnection);
        QTest::qWait(300);
        OverlayWidget *ov = videoWidget->overlay();
        CHECK(ov->isTimestampRoiMode(), "roiBtn: overlay entered roi mode");
        QTest::mousePress(ov, Qt::LeftButton, Qt::NoModifier, QPoint(10, 10));
        QTest::mouseMove(ov, QPoint(700, 130));
        QTest::mouseRelease(ov, Qt::LeftButton, Qt::NoModifier, QPoint(700, 130));
        QPushButton *confirm2 = nullptr;
        for (auto *b : ov->findChildren<QPushButton *>())
            if (b->text().contains(QStringLiteral("确认"))) { confirm2 = b; break; }
        CHECK(confirm2 != nullptr, "roiBtn: confirm button present");
        if (confirm2)
            QTest::mouseClick(confirm2, Qt::LeftButton);
        QElapsedTimer t2;
        t2.start();
        while (t2.elapsed() < 240000 && !applied2) {
            QTest::qWait(500);
            if (t2.elapsed() > 15000 && !quick2)
                break;
        }
        CHECK(quick2, "roiBtn: quickCheck auto-started after confirm (KEY FIX)");
        CHECK(applied2, "roiBtn: auto-applied");
        dlg2->close();
    }

    fprintf(stderr, "ui_chain_v2 final: %d checks, %d failures\n",
            g_checks, g_failures);
    return g_failures ? 1 : 0;
}
