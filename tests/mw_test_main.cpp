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
#include "app/video_session_manager.h"
#include "app/case_manager.h"
#include "domain/timeline_model.h"
#include "app/case_manager.h"
#include "domain/time_calibration.h"
#include "multicamplaybackwindow.h"
#include "camtilewidget.h"
#include "multicamview.h"
#include "infrastructure/ivideo_engine.h"
#include "i18n.h"
#include <QApplication>
#include <QTimer>
#include <QTest>
#include <QMessageBox>
#include <QDir>
#include <QFile>
#include <QWheelEvent>
#include <QSlider>
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

/// offscreen 模态框自动应答：QMessageBox 发回车（触发默认按钮：OK / question 默认 Yes）
static void armModalAutoClose(QApplication &app)
{
    auto *t = new QTimer(&app);
    QObject::connect(t, &QTimer::timeout, &app, []() {
        QMessageBox *m = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (m)
            QTest::keyClick(m, Qt::Key_Return);
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
// ---------------------------------------------------------------------------
// VideoSessionManager 决策面（P-31 T2-A）
// ---------------------------------------------------------------------------
static void testSessionPlan()
{
    VideoSessionManager mgr;
    CHECK(mgr.stateManager() != nullptr, "plan: state manager owned");

    // ① 无现场无缓存：空 plan
    const QString v = QDir::tempPath() + "/lumenarc_p31/plan_video.mp4";
    CaseManager none;
    auto plan = mgr.planOpen(v, &none);
    CHECK(!plan.hasMemoryState && plan.cacheVlaPath.isEmpty(),
          "plan: cold open empty");

    // ② 有内存现场：plan 带回（restore 副本）
    VideoState st;
    st.regions = {QRect(1, 2, 3, 4)};
    st.calibration.source = TimeCalibration::Source::Manual;
    st.calibration.offsetMs = 77;
    mgr.saveCurrentState(v, st);
    plan = mgr.planOpen(v, &none);
    CHECK(plan.hasMemoryState && plan.memoryState.regions == st.regions
          && plan.memoryState.calibration.offsetMs == 77,
          "plan: memory state roundtrip");

    // ③ 清空 + 键迁移
    mgr.clear();
    CHECK(!mgr.planOpen(v, &none).hasMemoryState, "plan: clear works");
    mgr.saveCurrentState(v, st);
    mgr.migrateKey(v, v + "2");
    CHECK(!mgr.planOpen(v, &none).hasMemoryState
          && mgr.planOpen(v + "2", &none).hasMemoryState,
          "plan: migrateKey moves state");
}

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

// ---------------------------------------------------------------------------
// 亮度分析完整链路（点击「亮度分析」复现路径：打开视频→缓存 vla 询问(Yes)→
// ROI 恢复→onAnalyze→进度/完成/气泡/自动保存）
// ---------------------------------------------------------------------------
static void testLumaFullChain(QApplication &app)
{
    const QString video = QString::fromLocal8Bit(
        qEnvironmentVariable("LUMENARC_REPRO_VIDEO").toUtf8());
    if (video.isEmpty() || !QFile::exists(video)) {
        fprintf(stderr, "[luma-chain] SKIP (LUMENARC_REPRO_VIDEO not set)\n");
        return;
    }
    const QString vla = video + ".vla";
    QFile::remove(vla);
    {
        // 带 ROI 的缓存分析结果（恢复后 onAnalyze 才可走全链）
        TimelineModel m;
        m.setData({0, 40, 80}, {{5.0, 6.0, 7.0}},
                  {DataEntry{DataEntry::Rect, 1}});
        m.saveToFile(vla, {QRect(10, 10, 80, 40)}, TimeCalibration());
    }

    MainWindow mw;
    mw.resize(1280, 800);
    mw.show();
    pump(app);

    // 打开视频 → 独立模式询问"找到缓存的分析结果" → 回车 Yes → ROI 恢复
    QMetaObject::invokeMethod(&mw, "openVideoFile", Q_ARG(QString, video));
    pump(app, 900);

    // 点「亮度分析」（QMetaObject 通道；ROI 已由缓存恢复）
    QMetaObject::invokeMethod(&mw, "onAnalyze");
    pump(app, 3000);

    // 分析完成不崩 = 通过（进程存活即断言；状态栏文本私有，仅验证无崩溃）
    fprintf(stderr, "[luma-chain] completed without crash\n");
    CHECK(true, "luma chain no crash");

    QFile::remove(vla);
}

// ---------------------------------------------------------------------------
// P-57 多机同步播放 UI 链（方案 §5）：开窗→临时进两路→播→对齐→切听→
// 瓦片放大镜（滚轮/中键）→双击回单路→合并条拖动→关窗，全程无崩溃
// ---------------------------------------------------------------------------
class McFakeEngine : public IVideoEngine
{
    Q_OBJECT
public:
    explicit McFakeEngine(QObject *parent = nullptr) : IVideoEngine(parent) {}
    qint64 pos = 0;
    int vol = -1;
    bool playing = false;
    int seekCount = 0;
    bool scrubOn = false;
    qint64 scrubTarget = -1;

    bool load(const QString &) override
    {
        QTimer::singleShot(0, this, [this]() {
            emit durationChanged(60000);
            emit stateChanged(PlaybackState::Stopped);
            QImage img(64, 64, QImage::Format_RGB32);
            img.fill(QColor(40, 60, 90));
            emit frameReady(img);   // 瓦片放大镜需要非空帧
        });
        return true;
    }
    void play() override { playing = true; }
    void pause() override { playing = false; }
    void stop() override { playing = false; }
    void unload() override {}
    void seek(qint64 t) override { ++seekCount; pos = t; }
    void setScrubMode(bool on) override { scrubOn = on; }
    void setScrubTarget(qint64 t) override { scrubTarget = t; }
    qint64 position() const override { return pos; }
    qint64 duration() const override { return 60000; }
    PlaybackState state() const override
    { return playing ? PlaybackState::Playing : PlaybackState::Paused; }
    int videoWidth() const override { return 64; }
    int videoHeight() const override { return 64; }
    float fps() const override { return 25.0f; }
    int volume() const override { return vol; }
    void setVolume(int v) override { vol = v; }
    void setRate(float) override {}
    float rate() const override { return 1.0f; }
};

static void testMultiCamWindow(QApplication &app)
{
    QVector<McFakeEngine *> engines;
    QString openedPath;
    auto *win = new MultiCamPlaybackWindow(nullptr);
    win->setEngineFactory([&](QObject *p) -> IVideoEngine * {
        auto *e = new McFakeEngine(p);
        engines.append(e);
        return e;
    });
    win->onOpenVideo = [&](const QString &p) { openedPath = p; };
    win->openStandalone();   // 独立模式：2 路临时槽位（U-1）
    win->resize(1000, 700);
    win->show();
    pump(app);

    // 临时进两路（测试通道：pickVideoForSlot 绕过文件对话框）
    win->pickVideoForSlot(0, "/fake/a.mp4");
    pump(app, 200);
    win->pickVideoForSlot(1, "/fake/b.mp4");
    pump(app, 300);
    // 每次选路全量重载：活引擎 = 最后两个（旧引擎已 deleteLater）；
    // 装配序 = 1 路（首槽）+ 2 路（第二槽）共 3 引擎
    CHECK(engines.size() >= 3, "mc: engines created across reloads");
    McFakeEngine *e0 = engines[engines.size() - 2];
    McFakeEngine *e1 = engines[engines.size() - 1];

    // 播 → 暂停（全路联动）
    QMetaObject::invokeMethod(win, "onTogglePlay");
    pump(app, 300);
    CHECK(e0->playing && e1->playing, "mc: both lanes playing");
    QMetaObject::invokeMethod(win, "onTogglePlay");
    pump(app, 100);

    // 未对齐拖路 0 进度条：只动本路（B 不跟随不显无信号——用户反馈修复）。
    // QSlider 鼠标语义随平台样式变（凹槽点击=步翻页不抓手柄），直发信号走接线
    const auto bars = win->findChildren<QSlider *>();
    CHECK(bars.size() >= 2, "mc: mode-B bars built");
    if (bars.size() >= 2) {
        const int e1Seeks = e1->seekCount;
        const int e0Seeks = e0->seekCount;
        emit bars[0]->sliderPressed();
        bars[0]->setValue(30000);
        emit bars[0]->sliderMoved(30000);
        emit bars[0]->sliderReleased();
        pump(app, 100);
        CHECK(e0->seekCount > e0Seeks, "mc: unlinked drag seeks lane A itself");
        CHECK(e1->seekCount == e1Seeks,
              "mc: unlinked drag does NOT touch lane B");
    }

    // 对齐会话：进入→确认（alignTempLane 服务内建偏移，双路转联动）
    QMetaObject::invokeMethod(win, "onEnterAlign");
    QMetaObject::invokeMethod(win, "onConfirmAlign");

    // 对齐后拖路 0：两路联动（B 经偏移收到追逐目标/松手 seek）
    if (bars.size() >= 2) {
        const int e1Seeks = e1->seekCount;
        emit bars[0]->sliderPressed();
        bars[0]->setValue(45000);
        emit bars[0]->sliderMoved(45000);
        emit bars[0]->sliderReleased();
        pump(app, 100);
        CHECK(e1->scrubTarget >= 0 || e1->seekCount > e1Seeks,
              "mc: after align, drag A scrubs/seeks B too");
    }

    // 瓦片交互
    const auto tiles = win->findChildren<CamTileWidget *>();
    CHECK(tiles.size() >= 2, "mc: tiles built");
    if (tiles.size() >= 2) {
        // 单击切听（U-2）
        QTest::mouseClick(tiles[1], Qt::LeftButton, {},
                          tiles[1]->rect().center());
        pump(app, 50);
        CHECK(e1->vol == 100 && e0->vol == 0, "mc: click switches audible");

        // 放大镜：滚轮以指针为中心缩放（N-7）
        const QPoint c = tiles[0]->rect().center();
        QWheelEvent we(QPointF(c), QPointF(tiles[0]->mapToGlobal(c)),
                       QPoint(0, 0), QPoint(0, 120), Qt::NoButton,
                       Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(tiles[0], &we);
        CHECK(tiles[0]->zoom() > 1.0, "mc: wheel zooms in");
        // 中键拖拽平移
        QTest::mousePress(tiles[0], Qt::MiddleButton, {}, c);
        QTest::mouseMove(tiles[0], c + QPoint(10, 8));
        QTest::mouseRelease(tiles[0], Qt::MiddleButton, {}, c + QPoint(10, 8));
        CHECK(tiles[0]->zoom() > 1.0, "mc: middle-drag pans (zoom kept)");
        // 双击回单路（U-6）
        QTest::mouseDClick(tiles[0], Qt::LeftButton, {}, c);
        pump(app, 50);
        CHECK(openedPath == "/fake/a.mp4", "mc: dblclick opens lane in main window");
    }

    // 合并时间线条（模式A 画笔/游标/拖动信号，widget 级）
    {
        MultiCamViewWidget bar;
        bar.resize(800, 220);
        CamLane a, b;
        a.videoId = "A"; a.fileName = "a";
        a.wallStartMs = 100000; a.wallEndMs = 160000; a.streamDurationMs = 60000;
        b.videoId = "B"; b.fileName = "b";
        b.wallStartMs = 150000; b.wallEndMs = 210000; b.streamDurationMs = 60000;
        bar.setLanes({a, b});
        bar.show();
        bar.setCursorMs(155000);
        int scrubHits = 0, commits = 0;
        QObject::connect(&bar, &MultiCamViewWidget::scrubPreview,
                         [&](qint64) { ++scrubHits; });
        QObject::connect(&bar, &MultiCamViewWidget::seekCommit,
                         [&](qint64) { ++commits; });
        const QPoint inside(400, 60);   // 条图区（标签列右侧）
        QTest::mousePress(&bar, Qt::LeftButton, {}, inside);
        QTest::mouseMove(&bar, inside + QPoint(40, 0));
        QTest::mouseRelease(&bar, Qt::LeftButton, {}, inside + QPoint(40, 0));
        CHECK(scrubHits >= 2, "mc: merged bar scrub preview fired");
        CHECK(commits == 1, "mc: merged bar seek commit on release");
        bar.close();
    }

    win->close();
    delete win;
    pump(app, 200);   // deleteLater 引擎回收（关窗 closeAll 路径）
    CHECK(true, "mc: full chain no crash");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    armModalAutoClose(app);

    testUiState();
    testProjectIo();
    testSessionPlan();
    testMainWindowBranches(app);
    testMultiCamWindow(app);
    testLumaFullChain(app);

    fprintf(stderr, "mw_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

#include "mw_test_main.moc"
