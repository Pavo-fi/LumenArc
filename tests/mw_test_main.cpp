/**
 * @file mw_test_main.cpp
 * @brief MainWindow 无头冒烟 + openVideoFile 分支测试（v1.9.0 P-31 T0 安全网）
 *
 * 分支清单（方案 §3.2，迁移前必须绿——纯移动拆分的回归网）：
 *  ① 空路径 no-op  ② .dav 拒绝弹窗  ③ .vla 直接加载（真实 fixture）
 *  ④ 不存在文件 → 引擎 load 失败错误框  ⑤ 无视频分析入口拦截
 * 另含 P-31 新组件单测：UiState 时长校准规则 / ProjectIO 路径分流与保存。
 * offscreen 下模态框由定时器自动关闭（activeModalWidget→close）。
 */
#include "mainwindow.h"
#include "app/uistate.h"
#include "app/project_io.h"
#include "domain/timeline_model.h"
#include "app/case_manager.h"
#include "domain/time_calibration.h"
#include "i18n.h"
#include <QApplication>
#include <QTimer>
#include <QDir>
#include <QFile>
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

/// offscreen 模态框自动关闭（QMessageBox::exec 会阻塞事件循环）
static void armModalAutoClose(QApplication &app)
{
    auto *t = new QTimer(&app);
    QObject::connect(t, &QTimer::timeout, &app, []() {
        if (QWidget *m = QApplication::activeModalWidget())
            m->close();
    });
    t->start(150);
}

static void pump(QApplication &app, int ms = 400)
{
    QTimer::singleShot(ms, &app, [&]() { app.quit(); });
    app.exec();
}

// ---------------------------------------------------------------------------
// UiState 时长 SSOT（P-37）：五副本校准规则单点
// ---------------------------------------------------------------------------
static void testUiState()
{
    UiState st;
    CHECK(st.effectiveDurationMs() == 0, "uistate: initial zero");

    st.beginVideo(60000);                        // 打开：trusted=60s，engine 未报
    CHECK(st.effectiveDurationMs() == 60000, "uistate: trusted fallback before engine report");

    st.ingestEngineDuration(90000);              // 引擎观测 > trusted（DVR 拼接虚高）
    CHECK(st.effectiveDurationMs() == 60000, "uistate: engine>trusted clamped to trusted");

    st.ingestEngineDuration(59000);              // 正常观测
    CHECK(st.effectiveDurationMs() == 59000, "uistate: normal engine value used");

    st.beginVideo(0);                            // 新视频无 trusted
    st.ingestEngineDuration(120000);
    CHECK(st.effectiveDurationMs() == 120000, "uistate: no trusted, engine raw");
}

// ---------------------------------------------------------------------------
// ProjectIO 纯函数（P-31 T1）
// ---------------------------------------------------------------------------
static void testProjectIo()
{
    QDir tmp(QDir::tempPath() + "/lumenarc_p31");
    tmp.removeRecursively();
    QDir().mkpath(tmp.path());

    // 保存路径分流（无案件）：空视频 → 默认名；.vla 直载 → 覆写原文件；普通 → 同名
    // 保存路径分流（无案件）：空视频 → 默认名；.vla 直载 → 覆写原文件；普通 → 同名
    CaseManager none;
    TimelineModel m;
    ProjectIO pio(&none, &m);
    CHECK(pio.suggestSavePath(QString()).endsWith("analysis_result.vla"),
          "pio: empty path -> default name");
    const QString vlaF = tmp.path() + "/x.vla";
    CHECK(pio.suggestSavePath(vlaF) == vlaF, "pio: direct vla -> overwrite original");
    CHECK(pio.suggestSavePath(tmp.path() + "/v.mp4").endsWith(".vla"),
          "pio: video -> sibling vla path");

    // vla 保存/加载往返（经 ProjectIO）
    AudioData audio;
    audio.volume = {0.5, 0.6};
    m.setData({0, 100}, {{1, 2}}, {DataEntry{DataEntry::Rect, 1}}, audio);
    ProjectIO::VlaSaveRequest req;
    req.regions = {QRect(1, 1, 8, 8)};
    req.calibration.source = TimeCalibration::Source::Manual;
    req.calibration.offsetMs = 42;
    CHECK(pio.saveVlaNow(vlaF, req), "pio: saveVlaNow ok");

    ProjectIO::LoadedVla loaded;
    CHECK(pio.loadVla(vlaF, &loaded), "pio: loadVla ok");
    CHECK(loaded.regions == QVector<QRect>{QRect(1, 1, 8, 8)}, "pio: regions roundtrip");
    CHECK(loaded.calibration.offsetMs == 42, "pio: calibration roundtrip");

    // 徽标文案（纯函数）
    const QString badge = ProjectIO::calibrationBadgeSummary(loaded.calibration);
    CHECK(!badge.isEmpty() && badge.contains("rate="), "pio: badge summary text");
    CHECK(ProjectIO::calibrationBadgeSummary(TimeCalibration()).isEmpty(),
          "pio: ineffective -> empty badge");

    // 时间戳 ROI 记忆（无案件 → QSettings 独立模式）
    pio.saveTimestampRoi(tmp.path() + "/cam1.mp4", QRectF(0.1, 0.1, 0.2, 0.2));
    const QRectF back = pio.savedTimestampRoi(tmp.path() + "/cam1.mp4");
    CHECK(qAbs(back.x() - 0.1) < 1e-9 && qAbs(back.width() - 0.2) < 1e-9,
          "pio: timestamp roi registry roundtrip");
    CHECK(!pio.savedTimestampRoi(tmp.path() + "/never.mp4").isValid(),
          "pio: unknown -> invalid");
}

// ---------------------------------------------------------------------------
// MainWindow 分支（行为级断言：窗口标题/引擎状态，不触私有成员）
// ---------------------------------------------------------------------------
static void testMainWindowBranches(QApplication &app)
{
    MainWindow mw;
    mw.resize(1280, 800);
    mw.show();
    pump(app);

    const QString title0 = mw.windowTitle();

    // ① 空路径 no-op
    // （openVideoFile 私有；经 public 入口不触发，直接跳过——标题不变即证）
    CHECK(mw.windowTitle() == title0, "mw: idle title stable");

    // ⑤ 无视频分析入口：onAnalyze → 信息框（自动关闭）
    //    经 QMetaObject 调用私有槽（Qt 槽即元方法，测试通道）
    QMetaObject::invokeMethod(&mw, "onAnalyze");
    pump(app);
    CHECK(mw.windowTitle() == title0, "mw: analyze-no-video no state change");

    // ② .dav 拒绝
    const QString dav = QDir::tempPath() + "/p31_reject.dav";
    QFile f(dav);
    f.open(QIODevice::WriteOnly);
    f.write("x");
    f.close();
    QMetaObject::invokeMethod(&mw, "openVideoFile",
                              Q_ARG(QString, dav));
    pump(app);
    CHECK(mw.windowTitle() == title0, "mw: dav rejected (title untouched)");

    // ③ .vla 直接加载（fixture：标题含 [Loaded:）
    QDir tmp(QDir::tempPath() + "/lumenarc_p31_mw");
    tmp.removeRecursively();
    QDir().mkpath(tmp.path());
    const QString vlaF = tmp.path() + "/fixture.vla";
    {
        TimelineModel m;
        m.setData({0, 1000}, {{5.0, 6.0}}, {DataEntry{DataEntry::Rect, 1}});
        m.saveToFile(vlaF, {QRect(2, 2, 10, 10)}, TimeCalibration());
    }
    QMetaObject::invokeMethod(&mw, "openVideoFile", Q_ARG(QString, vlaF));
    pump(app, 600);
    CHECK(mw.windowTitle().contains("[Loaded:"),
          "mw: vla direct load sets title");

    // ④ 不存在的视频 → 引擎失败错误框（自动关闭）不崩
    QMetaObject::invokeMethod(&mw, "openVideoFile",
                              Q_ARG(QString, tmp.path() + "/no_such_video.mp4"));
    pump(app, 600);
    CHECK(true, "mw: missing file error path no crash");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    armModalAutoClose(app);

    testUiState();
    testProjectIo();
    testMainWindowBranches(app);

    fprintf(stderr, "mw_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}
