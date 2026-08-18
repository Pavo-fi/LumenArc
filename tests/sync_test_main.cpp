/**
 * @file sync_test_main.cpp
 * @brief P-57 多机同步播放测试：sync_model 纯函数 + MultiCamSyncService 假引擎
 *
 * 覆盖（方案 §5）：
 *  1. 墙钟↔流内映射（仿射/变速率/临时偏移/覆盖判定）
 *  2. 时间线模式判定（2 路含临时=分开；2-4 全校时=合并）
 *  3. 纠偏决策（阈值 + 持续增长迟滞）
 *  4. 服务级：加载收口/播放联动/缺口暂停复出/切听/纠偏实发/临时偏移重映射
 *     /游标追逐 beginScrub-scrubTo-endScrub 契约
 */
#include "app/multicam_sync_service.h"
#include "domain/sync_model.h"
#include "infrastructure/ivideo_engine.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <cstdio>
#include <limits>

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

// ---------------------------------------------------------------------------
// 假引擎（记录面，不进帧）
// ---------------------------------------------------------------------------
class FakeEngine : public IVideoEngine
{
    Q_OBJECT
public:
    explicit FakeEngine(QObject *parent = nullptr) : IVideoEngine(parent) {}

    QString path;
    qint64 dur = 60000;
    qint64 pos = 0;
    bool playing = false;
    bool failLoad = false;
    int vol = -1;
    float rate_ = 1.0f;
    int seekCount = 0;
    qint64 lastSeek = -1;
    bool scrubOn = false;
    qint64 scrubTarget = -1;
    int lowres = 0;
    int w = 320, h = 240;         // 可配尺寸（性能治理测试）
    float fps_ = 25.0f;
    QString hwName;               // 非空 = 硬解路
    qint64 gopMs = 0;             // 实测 GOP（纠偏阈值联动测试）
    int loadCount = 0;

    bool load(const QString &p) override
    {
        path = p;
        ++loadCount;
        if (failLoad)
            return false;
        QTimer::singleShot(0, this, [this]() {
            emit durationChanged(dur);
            emit stateChanged(PlaybackState::Stopped);
        });
        return true;
    }
    void play() override { playing = true; emit stateChanged(PlaybackState::Playing); }
    void pause() override { playing = false; emit stateChanged(PlaybackState::Paused); }
    void stop() override { playing = false; }
    void unload() override {}
    void seek(qint64 t) override { ++seekCount; lastSeek = t; pos = t; }
    qint64 position() const override { return pos; }
    qint64 duration() const override { return dur; }
    PlaybackState state() const override
    { return playing ? PlaybackState::Playing : PlaybackState::Paused; }
    int videoWidth() const override { return w; }
    int videoHeight() const override { return h; }
    float fps() const override { return fps_; }
    int volume() const override { return vol; }
    void setVolume(int v) override { vol = v; }
    void setRate(float r) override { rate_ = r; }
    float rate() const override { return rate_; }
    void setScrubMode(bool on) override { scrubOn = on; }
    void setScrubTarget(qint64 t) override { scrubTarget = t; }
    void setPreviewLowres(int level) override { lowres = level; }
    int previewLowres() const override { return lowres; }
    QString hardwareAdapterName() const override { return hwName; }
    qint64 learnedGopMs() const override { return gopMs; }
};

static void pump(int ms)
{
    QEventLoop loop;
    QTimer::singleShot(ms, &loop, &QEventLoop::quit);
    loop.exec();
}

/// 仿射校时：wall = offset + rate·stream
static TimeCalibration makeCal(qint64 offsetMs, double rate = 1.0,
                               bool rateApplied = false)
{
    TimeCalibration c;
    c.source = TimeCalibration::Source::Manual;
    c.dateKnown = true;
    c.offsetMs = offsetMs;
    c.rate = rate;
    c.rateApplied = rateApplied;
    return c;
}

static SyncLaneData makeLane(const QString &id, qint64 offsetMs,
                             qint64 durMs = 60000)
{
    SyncLaneData l;
    l.id = id;
    l.path = QStringLiteral("/fake/%1.mp4").arg(id);
    l.displayName = id;
    l.calibrated = true;
    l.cal = makeCal(offsetMs);
    l.durationMs = durMs;
    return l;
}

/// 服务装配：假引擎工厂 + 2 路加载并等收口
static void setupService(MultiCamSyncService &svc,
                         QVector<FakeEngine *> &engines,
                         const QVector<SyncLaneData> &lanes)
{
    engines.clear();
    svc.setEngineFactory([&engines](QObject *parent) -> IVideoEngine * {
        auto *e = new FakeEngine(parent);
        engines.append(e);
        return e;
    });
    bool finished = false;
    QObject::connect(&svc, &MultiCamSyncService::loadFinished,
                     [&finished]() { finished = true; });
    CHECK(svc.loadLanes(lanes), "loadLanes accepted");
    pump(50);   // 假引擎 singleShot(0) 回报 duration
    CHECK(finished, "loadFinished emitted");
}

// ---------------------------------------------------------------------------
// 1. 纯函数
// ---------------------------------------------------------------------------
static void testMapping()
{
    SyncLaneData a = makeLane("A", 100000);
    CHECK(syncWallOf(a, 5000) == 105000, "affine wallOf");
    CHECK(syncStreamOf(a, 105000) == 5000, "affine streamOf");
    CHECK(syncLaneWallStart(a) == 100000, "wallStart = offset");
    CHECK(syncLaneWallEnd(a) == 160000, "wallEnd = offset+duration");

    SyncLaneData f = makeLane("F", 1000);
    f.cal = makeCal(1000, 2.0, true);   // 流内 1s = 墙钟 2s
    CHECK(syncWallOf(f, 1000) == 3000, "rate2 wallOf");
    CHECK(syncStreamOf(f, 3000) == 1000, "rate2 streamOf");

    SyncLaneData t;
    t.id = "T1";
    t.temporary = true;
    t.tempOffsetMs = 500;
    t.durationMs = 60000;
    CHECK(syncWallOf(t, 100) == 600, "temp wallOf");
    CHECK(syncStreamOf(t, 600) == 100, "temp streamOf");

    CHECK(!syncLaneCovers(a, 99999), "covers: before start");
    CHECK(syncLaneCovers(a, 100000), "covers: start inclusive");
    CHECK(syncLaneCovers(a, 160000), "covers: end inclusive");
    CHECK(!syncLaneCovers(a, 160001), "covers: after end");
}

static void testModeDecision()
{
    using M = SyncTimelineMode;
    CHECK(decideSyncMode(2, 1) == M::Separate, "mode: 2 lanes 1 temp = separate");
    CHECK(decideSyncMode(2, 0) == M::Merged, "mode: 2 lanes calibrated = merged");
    CHECK(decideSyncMode(3, 0) == M::Merged, "mode: 3 lanes calibrated = merged");
    CHECK(decideSyncMode(4, 0) == M::Merged, "mode: 4 lanes calibrated = merged");
    CHECK(decideSyncMode(2, 2) == M::Separate, "mode: both temp = separate");
    CHECK(decideSyncMode(3, 1) == M::Separate, "mode: 3 lanes with temp = fallback");
}

static void testDriftDecision()
{
    const qint64 MIN = std::numeric_limits<qint64>::min();
    qint64 target = -1;
    CHECK(!decideDriftCorrection(1000, 1000, MIN, 120), "drift: zero err no seek");
    CHECK(decideDriftCorrection(1500, 1000, MIN, 120, &target) && target == 1000,
          "drift: first overshoot seeks");
    CHECK(!decideDriftCorrection(1500, 1000, 600, 120),
          "drift: converging (500<600) no seek");
    CHECK(decideDriftCorrection(1700, 1000, 500, 120),
          "drift: growing (700>500) seeks");
    CHECK(!decideDriftCorrection(1100, 1000, MIN, 120),
          "drift: inside threshold no seek");
}

// ---------------------------------------------------------------------------
// 2. 服务级（假引擎）
// ---------------------------------------------------------------------------
static void testServiceLoadAndPlay()
{
    MultiCamSyncService svc;
    QVector<FakeEngine *> eng;
    // 两路墙钟衔接：A [100s,160s)，B [150s,210s)
    setupService(svc, eng, {makeLane("A", 100000), makeLane("B", 150000)});
    CHECK(svc.state() == MultiCamSyncService::State::Ready, "state Ready");
    CHECK(svc.contentStartWallMs() == 100000, "content start");
    CHECK(svc.contentEndWallMs() == 210000, "content end");
    CHECK(eng.size() == 2 && eng[0]->vol == 100 && eng[1]->vol == 0,
          "default audible lane 0");

    svc.play();
    CHECK(svc.state() == MultiCamSyncService::State::Playing, "playing");
    // 起点 100s 仅 A 覆盖（B 150s 才起）→ B 驻停，不错位硬播（§3.1）
    CHECK(eng[0]->playing && !eng[1]->playing,
          "lane A plays; lane B parked until coverage");
    pump(250);
    svc.pause();
    CHECK(svc.state() == MultiCamSyncService::State::Paused, "paused");
    CHECK(!eng[0]->playing && !eng[1]->playing, "both paused");

    // 跳转：wall 155000 → A 流内 55000、B 流内 5000
    svc.seekWall(155000);
    CHECK(eng[0]->lastSeek == 55000, "seek maps lane A");
    CHECK(eng[1]->lastSeek == 5000, "seek maps lane B");
    // 续播：155s 两路均在覆盖内
    svc.play();
    CHECK(eng[0]->playing && eng[1]->playing, "both lanes playing in overlap");
    svc.pause();

    // 切听
    svc.setAudibleLane(1);
    CHECK(eng[0]->vol == 0 && eng[1]->vol == 100, "audible switch");

    // 追逐契约
    svc.beginScrub();
    CHECK(eng[0]->scrubOn && eng[1]->scrubOn, "scrub mode on");
    svc.scrubTo(152000);
    CHECK(eng[0]->scrubTarget == 52000, "scrub target A");
    CHECK(eng[1]->scrubTarget == 2000, "scrub target B");
    svc.endScrub();
    CHECK(!eng[0]->scrubOn && !eng[1]->scrubOn, "scrub off after commit");
    svc.closeAll();
}

static void testServiceGap()
{
    MultiCamSyncService svc;
    QVector<FakeEngine *> eng;
    // 两路墙钟不相交：A [100s,160s)，B [200s,260s) —— 中间 40s 缺口
    setupService(svc, eng, {makeLane("A", 100000), makeLane("B", 200000)});

    svc.play();   // 起点 100s：仅 A 在覆盖内
    CHECK(eng[0]->playing, "gap: lane A playing at start");
    CHECK(!eng[1]->playing, "gap: lane B parked (out of coverage)");

    svc.seekWall(200000);   // 跳进 B 的覆盖起点
    CHECK(!eng[0]->playing, "gap: lane A parked after jump");
    CHECK(eng[1]->playing, "gap: lane B resumed in coverage");
    CHECK(eng[1]->lastSeek == 0, "gap: lane B seek to its 0");

    svc.pause();
    svc.closeAll();
}

static void testServiceDriftCorrects()
{
    MultiCamSyncService svc;
    QVector<FakeEngine *> eng;
    setupService(svc, eng, {makeLane("A", 0), makeLane("B", 0)});
    svc.play();
    // 假引擎 A 位置冻结不动（模拟漂移源），主时钟前进 → 纠偏环应发 seek
    const int base = eng[0]->seekCount;
    pump(600);
    svc.pause();
    CHECK(eng[0]->seekCount > base, "drift: correction seeks issued");
    svc.closeAll();
}

static void testServiceTempOffset()
{
    MultiCamSyncService svc;
    QVector<FakeEngine *> eng;
    SyncLaneData ref = makeLane("R", 500000);           // 校时参考路
    SyncLaneData tmp;                                    // 临时路（未对齐）
    tmp.id = "T1";
    tmp.path = "/fake/t.mp4";
    tmp.displayName = "t";
    tmp.temporary = true;
    tmp.durationMs = 60000;
    setupService(svc, eng, {ref, tmp});

    // 对齐：参考路 510s 墙钟 = 临时路流内 2000ms → offset = 508000
    svc.setLaneOffsetMs(1, 508000);
    CHECK(syncWallOf(svc.lanes()[1], 2000) == 510000, "temp remap wallOf");
    svc.seekWall(510000);
    CHECK(eng[1]->lastSeek == 2000, "temp lane seek via new offset");
    svc.closeAll();
}

static void testServiceLoadFailure()
{
    MultiCamSyncService svc;
    QVector<FakeEngine *> engines;
    bool finished = false;
    int failCount = 0;
    svc.setEngineFactory([&](QObject *parent) -> IVideoEngine * {
        auto *e = new FakeEngine(parent);
        e->failLoad = (engines.size() == 1);   // 第二路失败
        engines.append(e);
        return e;
    });
    QObject::connect(&svc, &MultiCamSyncService::loadFinished,
                     [&]() { finished = true; });
    QObject::connect(&svc, &MultiCamSyncService::laneLoadFailed,
                     [&](int, const QString &) { ++failCount; });
    svc.loadLanes({makeLane("A", 0), makeLane("B", 0)});
    pump(50);
    CHECK(failCount == 1, "one lane failure reported");
    CHECK(finished, "loadFinished despite failure");
    CHECK(!svc.laneUsable(1) && svc.laneUsable(0), "laneUsable reflects failure");
    // 失败路不参与播放
    svc.play();
    CHECK(engines[0]->playing, "surviving lane plays");
    svc.closeAll();
}

// §4 性能治理：软解总吞吐超阈 → 最重路降 lowres 预览档并提示；硬解路不计入
static void testServicePerfGovernance()
{
    QVector<FakeEngine *> engines;
    QStringList notices;

    // 两路 4K25 软解 ≈ 207M×2 px/s ≫ 150M 阈 → 档②触发（两路等重取首路）；
    // 假引擎尺寸先于 durationChanged 就绪（与真引擎 openFile 内部序一致）
    auto *svc2 = new MultiCamSyncService;
    svc2->setEngineFactory([&](QObject *parent) -> IVideoEngine * {
        auto *e = new FakeEngine(parent);
        e->w = 3840; e->h = 2160; e->fps_ = 25.0f;   // 软解 4K25
        engines.append(e);
        return e;
    });
    QObject::connect(svc2, &MultiCamSyncService::performanceNotice,
                     [&](const QString &m) { notices.append(m); });
    svc2->loadLanes({makeLane("A", 0), makeLane("B", 1000)});
    pump(50);
    CHECK(engines.size() >= 2, "perf: engines created");
    CHECK(engines[0]->lowres == 1, "perf: heaviest lane dropped to lowres");
    CHECK(engines[1]->lowres == 0, "perf: other lane full-res");
    CHECK(engines[0]->loadCount == 2, "perf: lowres lane reloaded");
    CHECK(svc2->laneIsLowres(0) && !svc2->laneIsLowres(1), "perf: lowres flags");
    CHECK(!notices.isEmpty(), "perf: notice emitted (C2)");
    svc2->closeAll();
    delete svc2;

    // 硬解路不计入软解负载（档①）：一路硬解 4K + 一路软解 4K → 仅软解路触档
    engines.clear();
    notices.clear();
    auto *svc3 = new MultiCamSyncService;
    bool first = true;
    svc3->setEngineFactory([&](QObject *parent) -> IVideoEngine * {
        auto *e = new FakeEngine(parent);
        e->w = 3840; e->h = 2160; e->fps_ = 25.0f;
        if (first) { e->hwName = QStringLiteral("NVDEC"); first = false; }
        engines.append(e);
        return e;
    });
    QObject::connect(svc3, &MultiCamSyncService::performanceNotice,
                     [&](const QString &m) { notices.append(m); });
    svc3->loadLanes({makeLane("A", 0), makeLane("B", 1000)});
    pump(50);
    CHECK(engines[0]->lowres == 0, "perf: hw lane never downgraded");
    CHECK(engines[1]->lowres == 1, "perf: soft lane downgraded");
    svc3->closeAll();
    delete svc3;
}

// R-2：长 GOP 路纠偏阈值放宽（500ms），短 GOP 路 120ms 严阈值
static void testServiceGopThreshold()
{
    // 短 GOP：位置冻结 400ms 内必纠（阈值 120ms）
    {
        MultiCamSyncService svc;
        QVector<FakeEngine *> eng;
        setupService(svc, eng, {makeLane("A", 0), makeLane("B", 0)});
        svc.play();
        const int base = eng[0]->seekCount;
        pump(400);
        svc.pause();
        CHECK(eng[0]->seekCount > base, "gop: short-GOP lane corrected within 400ms");
        svc.closeAll();
    }
    // 长 GOP（10s）：同样冻结 400ms 不纠（阈值 500ms 放宽，防 seek 风暴）
    {
        MultiCamSyncService svc;
        QVector<FakeEngine *> eng;
        svc.setEngineFactory([&](QObject *parent) -> IVideoEngine * {
            auto *e = new FakeEngine(parent);
            e->gopMs = 10000;
            eng.append(e);
            return e;
        });
        bool finished = false;
        QObject::connect(&svc, &MultiCamSyncService::loadFinished,
                         [&]() { finished = true; });
        svc.loadLanes({makeLane("A", 0), makeLane("B", 0)});
        pump(50);
        CHECK(finished, "gop: long-GOP load finished");
        svc.play();
        const int base = eng[0]->seekCount;
        pump(400);
        svc.pause();
        CHECK(eng[0]->seekCount == base,
              "gop: long-GOP lane NOT corrected within 400ms (relaxed 500ms)");
        svc.closeAll();
    }
}

// R-1 对抗：运行期引擎暴毙（Ready 后回 Idle）→ 标死该路 + 上报，不拖垮全局
static void testServiceEngineDies()
{
    MultiCamSyncService svc;
    QVector<FakeEngine *> eng;
    int failCount = 0;
    setupService(svc, eng, {makeLane("A", 0), makeLane("B", 0)});
    QObject::connect(&svc, &MultiCamSyncService::laneLoadFailed,
                     [&](int, const QString &) { ++failCount; });
    svc.play();
    // 运行中 B 路暴毙
    emit eng[1]->stateChanged(PlaybackState::Idle);
    CHECK(failCount == 1, "death: reported once");
    CHECK(!svc.laneUsable(1), "death: lane marked unusable");
    // A 路不受影响继续播；seekWall 不再触死路
    CHECK(eng[0]->playing, "death: survivor still playing");
    const int seeks = eng[1]->seekCount;
    svc.seekWall(30000);
    CHECK(eng[1]->seekCount == seeks, "death: dead lane not sought");
    svc.closeAll();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testMapping();
    testModeDecision();
    testDriftDecision();
    testServiceLoadAndPlay();
    testServiceGap();
    testServiceDriftCorrects();
    testServiceTempOffset();
    testServiceLoadFailure();
    testServicePerfGovernance();
    testServiceGopThreshold();
    testServiceEngineDies();
    fprintf(stderr, "sync_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

#include "sync_test_main.moc"
