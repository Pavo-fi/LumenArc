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
#include "app/segment_switch_engine.h"
#include "app/cam_timeline.h"          // P-69：CamInventoryItem + buildMergedGroups（内联纯函数）
#include "domain/sync_model.h"
#include "infrastructure/ivideo_engine.h"

#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
#include <QHash>
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
    QHash<QString, qint64> durByPath;   // P-69：按文件路径定制时长（换段测试）

    bool load(const QString &p) override
    {
        path = p;
        ++loadCount;
        if (failLoad)
            return false;
        QTimer::singleShot(0, this, [this, p]() {
            emit durationChanged(durByPath.value(p, dur));
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

    // v1.12.7（用户实测反馈）：做过北京时间校对的路，多机墙钟轴 = 北京时间
    // （wallMsOf + truthOffsetMs）；反解/覆盖判定同轴一致；未设 truth 的路不变
    SyncLaneData bj = makeLane("BJ", 100000);
    bj.cal.truthSet = true;
    bj.cal.truthOffsetMs = 834000;   // 监控慢 13 分 54 秒
    CHECK(syncWallOf(bj, 5000) == 105000 + 834000, "truth wallOf = beijing");
    CHECK(syncStreamOf(bj, 105000 + 834000) == 5000, "truth streamOf inverse");
    CHECK(syncLaneWallStart(bj) == 100000 + 834000, "truth wallStart");
    CHECK(syncLaneWallEnd(bj) == 160000 + 834000, "truth wallEnd");
    CHECK(syncLaneCovers(bj, 100000 + 834000), "truth covers start");
    CHECK(!syncLaneCovers(bj, 100000), "truth: osd-axis point not covered");
    CHECK(syncWallOf(a, 5000) == 105000, "no-truth lane unchanged");
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

// 模式B 核心语义（用户反馈修复）：未对齐临时路独立播放——
// 不驻停/不随墙钟 seek/不显无信号；对齐后才进统一墙钟轴
static void testServiceUnlinkedTemp()
{
    MultiCamSyncService svc;
    QVector<FakeEngine *> eng;
    SyncLaneData ref = makeLane("R", 500000);           // 校时路：墙钟在真实 epoch
    SyncLaneData tmp;
    tmp.id = "T1";
    tmp.path = "/fake/t.mp4";
    tmp.displayName = "t";
    tmp.temporary = true;
    tmp.durationMs = 60000;                              // 未对齐：偏移 0（1970）
    setupService(svc, eng, {ref, tmp});

    // 内容区间只含已联动路（校时路）：临时路不进墙钟轴
    CHECK(svc.contentStartWallMs() == 500000, "unlinked: content start = ref only");
    CHECK(svc.contentEndWallMs() == 560000, "unlinked: content end = ref only");
    CHECK(svc.laneLinked(0) && !svc.laneLinked(1), "unlinked: linked flags");
    CHECK(svc.laneCoversNow(1), "unlinked: temp lane never shows gap");

    // 播放：两路都播（临时路自由播放，不被缺口驻停）
    svc.play();
    CHECK(eng[0]->playing && eng[1]->playing, "unlinked: both play independently");
    // 墙钟 seek 只动联动路：临时路纹丝不动
    const int tempSeeks = eng[1]->seekCount;
    svc.seekWall(510000);
    CHECK(eng[0]->lastSeek == 10000, "unlinked: ref lane seeks via wall");
    CHECK(eng[1]->seekCount == tempSeeks, "unlinked: temp lane untouched by wall seek");
    svc.pause();
    svc.closeAll();
}

// 对齐会话：alignTempLane 建立偏移并双路转联动（独立模式参考路锚定）
static void testServiceAlign()
{
    MultiCamSyncService svc;
    QVector<FakeEngine *> eng;
    SyncLaneData a, b;                                   // 独立模式：双临时路
    a.id = "T1"; a.path = "/fake/a.mp4"; a.displayName = "a";
    a.temporary = true; a.durationMs = 60000;
    b = a; b.id = "T2"; b.path = "/fake/b.mp4"; b.displayName = "b";
    setupService(svc, eng, {a, b});

    // 未对齐兑底：无联动路 → 内容区间全量（各路走流内轴）
    CHECK(svc.contentStartWallMs() == 0 && svc.contentEndWallMs() == 60000,
          "align: fallback range before alignment");

    // 用户操作：两路各拖到可辨认瞬间（ref 30000，temp 2000）→ 确认对齐
    eng[0]->seek(30000);
    eng[1]->seek(2000);
    svc.alignTempLane(1, 0);
    CHECK(svc.lanes()[1].tempOffsetMs == 28000, "align: offset = ref wall - temp pos");
    CHECK(svc.laneLinked(0) && svc.laneLinked(1), "align: both lanes linked");
    // 对齐后时钟锚到临时路当前画面（墙钟 30000）：两路同步 seek
    CHECK(eng[0]->lastSeek == 30000, "align: ref lane seeks to anchor wall");
    CHECK(eng[1]->lastSeek == 2000, "align: temp lane stays at its frame");
    // 此后墙钟 seek 双路联动
    svc.seekWall(45000);
    CHECK(eng[0]->lastSeek == 45000, "align: ref follows wall");
    CHECK(eng[1]->lastSeek == 17000, "align: temp follows via offset");
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

// ---------------------------------------------------------------------------
// P-73 同事件间接校时域层（event_calib.h 纯函数 + TimeCalibration 序列化）
// ---------------------------------------------------------------------------
static eventcalib::EventAnchor mkAnchor(const QString &ref, qint64 refStream,
                                        qint64 refWall, qint64 tgtStream,
                                        const QString &name, qint64 tol = 20)
{
    eventcalib::EventAnchor a;
    a.refLaneId = ref;
    a.refStreamMs = refStream;
    a.refWallMs = refWall;
    a.targetStreamMs = tgtStream;
    a.eventName = name;
    a.markedAtMs = 1000;
    a.toleranceMs = tol;
    return a;
}

static void testEventCalibFit()
{
    // 0 锚点：类型化错误（C1）
    {
        const auto r = eventcalib::fitAnchors({});
        CHECK(!r.ok && r.error.startsWith(QStringLiteral("EVENTCALIB_NO_ANCHOR")),
              "ecfit: empty anchors rejected with typed error");
    }
    // 单锚点：偏移型，rate=1
    {
        const auto r = eventcalib::fitAnchors(
            {mkAnchor(QStringLiteral("A"), 5000, 1000000, 3000,
                      QStringLiteral("ev1"))});
        CHECK(r.ok && !r.affine, "ecfit: single anchor → offset mode");
        CHECK(qAbs(r.offsetMs - (1000000.0 - 3000.0)) < 0.5,
              "ecfit: offset = refWall - targetStream");
        CHECK(r.residualsMs.size() == 1 && r.residualsMs[0] == 0.0,
              "ecfit: single residual zero");
    }
    // 双锚点：仿射拟合 rate（DVR 走快 0.1% 场景）
    {
        // 目标路流内 0s ↔ 墙钟 1000s；流内 1000s ↔ 墙钟 2001s（率 1.001）
        const auto r = eventcalib::fitAnchors({
            mkAnchor(QStringLiteral("A"), 100, 1000000, 0, QStringLiteral("e1")),
            mkAnchor(QStringLiteral("A"), 200, 2001000, 1000000,
                     QStringLiteral("e2")),
        });
        CHECK(r.ok && r.affine, "ecfit: two anchors → affine");
        CHECK(qAbs(r.rate - 1.001) < 1e-6,
              qPrintable(QStringLiteral("ecfit: rate got %1").arg(r.rate)));
        CHECK(qAbs(r.interceptMs - 1000000.0) < 1.0, "ecfit: intercept");
        for (double res : r.residualsMs)
            CHECK(res < 1.0, "ecfit: exact two-point residuals ~0");
    }
    // 三锚点带噪：残差如实报告；速率正常
    {
        const auto r = eventcalib::fitAnchors({
            mkAnchor(QStringLiteral("A"), 0, 1000000, 0, QStringLiteral("e1")),
            mkAnchor(QStringLiteral("A"), 0, 1500100, 500000, QStringLiteral("e2")),
            mkAnchor(QStringLiteral("A"), 0, 2000000, 1000000, QStringLiteral("e3")),
        });
        CHECK(r.ok && r.affine, "ecfit: noisy 3-anchor affine ok");
        CHECK(qAbs(r.rate - 1.0) < 1e-3, "ecfit: noisy rate ~1");
        CHECK(r.residualsMs.size() == 3, "ecfit: residuals per anchor");
        bool anyRes = false;
        for (double res : r.residualsMs)
            if (res > 1.0) anyRes = true;
        CHECK(anyRes, "ecfit: noise shows in residuals (honest)");
    }
    // 病态：锚点目标时刻全同 → 退化偏移型（不崩）
    {
        const auto r = eventcalib::fitAnchors({
            mkAnchor(QStringLiteral("A"), 0, 1000000, 5000, QStringLiteral("e1")),
            mkAnchor(QStringLiteral("A"), 0, 1000100, 5000, QStringLiteral("e2")),
        });
        CHECK(r.ok && !r.affine, "ecfit: degenerate → offset fallback");
    }
    // 病态：对错事件 → rate 异常拒收（C1 类型化）
    {
        const auto r = eventcalib::fitAnchors({
            mkAnchor(QStringLiteral("A"), 0, 1000000, 0, QStringLiteral("e1")),
            mkAnchor(QStringLiteral("A"), 0, 3000000, 1000000, QStringLiteral("e2")),
        });
        CHECK(!r.ok && r.error.startsWith(QStringLiteral("EVENTCALIB_BAD_RATE")),
              "ecfit: absurd rate rejected");
    }
    // 容差计算：较粗路半帧
    CHECK(eventcalib::frameToleranceMs(25.0, 20.0) == 25,
          "ecfit: tolerance = 500/minFps (20fps → 25ms)");
    CHECK(eventcalib::frameToleranceMs(0.0, 0.0) == 20,
          "ecfit: tolerance fallback 25fps");
}

static void testEventCalibCycleAndChain()
{
    using eventcalib::EventAnchor;
    QHash<QString, QVector<EventAnchor>> byLane;
    // B 参考 A；C 参考 B（多跳允许）
    byLane[QStringLiteral("B")] = {mkAnchor(QStringLiteral("A"), 0, 1000, 0,
                                            QStringLiteral("e"))};
    byLane[QStringLiteral("C")] = {mkAnchor(QStringLiteral("B"), 0, 2000, 0,
                                            QStringLiteral("e"))};
    // D 参考 C：不成环 ✓（A→B→C→D 合法多跳）
    CHECK(!eventcalib::wouldCreateCycle(QStringLiteral("D"), QStringLiteral("C"),
                                        byLane),
          "eccycle: multi-hop D←C allowed");
    // A 参考 C：成环 ✗（A 在 C 的上游）
    CHECK(eventcalib::wouldCreateCycle(QStringLiteral("A"), QStringLiteral("C"),
                                       byLane),
          "eccycle: A←C cycle rejected");
    // 自参考：成环
    CHECK(eventcalib::wouldCreateCycle(QStringLiteral("B"), QStringLiteral("B"),
                                       byLane),
          "eccycle: self-reference rejected");
    // B 参考 D（D 无锚点）：不成环
    CHECK(!eventcalib::wouldCreateCycle(QStringLiteral("B"), QStringLiteral("D"),
                                        byLane),
          "eccycle: B←D no cycle");

    // 链展开：C → B → A（A 绝对）
    QSet<QString> absIds{QStringLiteral("A")};
    const auto chain = eventcalib::expandChain(QStringLiteral("C"), byLane, absIds);
    CHECK(chain.size() == 3, "ecchain: C chain has 3 hops");
    if (chain.size() == 3) {
        CHECK(chain[0].laneId == QLatin1String("C") && !chain[0].absolute,
              "ecchain: head = C indirect");
        CHECK(chain[2].laneId == QLatin1String("A") && chain[2].absolute,
              "ecchain: tail = A absolute");
    }
    // 累积容差 = 间接跳容差之和（20+20；绝对跳不计）
    CHECK(eventcalib::cumulativeToleranceMs(chain) == 40,
          "ecchain: cumulative tolerance sums indirect hops");
}

static void testEventCalibJsonRoundTrip()
{
    TimeCalibration cal;
    cal.source = TimeCalibration::Source::CrossCamEvent;
    cal.offsetMs = 123456;
    cal.rate = 1.0003;
    cal.rateApplied = true;
    cal.dateKnown = true;
    cal.eventAnchors = {
        mkAnchor(QStringLiteral("V001"), 5000, 1700000000000ll, 3000,
                 QStringLiteral("黑衣男子推开东门"), 20),
        mkAnchor(QStringLiteral("V001"), 8000, 1700003000000ll, 6000,
                 QStringLiteral("白色轿车压减速带"), 25),
    };
    const auto back = TimeCalibration::fromJson(cal.toJson());
    CHECK(back.source == TimeCalibration::Source::CrossCamEvent,
          "ecjson: source round-trip");
    CHECK(back.eventAnchors.size() == 2, "ecjson: anchors round-trip");
    if (back.eventAnchors.size() == 2) {
        CHECK(back.eventAnchors[0].eventName == QStringLiteral("黑衣男子推开东门"),
              "ecjson: event name preserved (UTF-8)");
        CHECK(back.eventAnchors[1].toleranceMs == 25,
              "ecjson: tolerance preserved");
        CHECK(back.eventAnchors[1].refLaneId == QLatin1String("V001"),
              "ecjson: ref lane preserved");
    }
    // 成果校时换算可用（rate 生效）
    CHECK(qAbs(back.wallMsOf(1000000) - (123456 + 1.0003 * 1000000)) < 2.0,
          "ecjson: wallMsOf applies fitted rate");
    // 老读取端容忍：无 event_anchors 键的 JSON 正常读
    auto o = cal.toJson();
    o.remove(QStringLiteral("event_anchors"));
    const auto legacy = TimeCalibration::fromJson(o);
    CHECK(legacy.eventAnchors.isEmpty() && legacy.offsetMs == 123456,
          "ecjson: legacy json without anchors reads fine");
}

// ---------------------------------------------------------------------------
// P-69 编号合并轨：段感知映射（虚拟流内轴 / 先起步者赢 / 缺口钉最近段）
// ---------------------------------------------------------------------------
static SyncSegment makeSeg(const QString &path, qint64 wallStart, qint64 durMs)
{
    SyncSegment s;
    s.path = path;
    s.srcId = path;
    s.cal = makeCal(wallStart);   // rate=1：wallMsOf(s)=s+wallStart
    s.durationMs = durMs;
    return s;
}

static void testMergedMapping()
{
    // 两段有缺口：seg0 [100000,105000)，seg1 [105500,109500)
    SyncLaneData m;
    m.id = "M_D17";
    m.calibrated = true;
    m.segments = { makeSeg("/fake/a.mp4", 100000, 5000),
                   makeSeg("/fake/b.mp4", 105500, 4000) };
    m.durationMs = 9000;
    m.path = m.segments.first().path;
    CHECK(m.isMerged(), "merged: isMerged");
    CHECK(syncLaneWallStart(m) == 100000, "merged: wallStart = earliest seg");
    CHECK(syncLaneWallEnd(m) == 109500, "merged: wallEnd = latest seg end");
    CHECK(syncLaneCovers(m, 103000), "merged: covers seg0");
    CHECK(!syncLaneCovers(m, 105200), "merged: gap not covered");
    CHECK(syncLaneCovers(m, 107000), "merged: covers seg1");

    // 虚拟轴映射：cum = [0, 5000]
    CHECK(syncStreamOf(m, 102000) == 2000, "merged: streamOf in seg0");
    CHECK(syncStreamOf(m, 107000) == 6500, "merged: streamOf in seg1 (cum 5000 + 1500)");
    CHECK(syncWallOf(m, 6500) == 107000, "merged: wallOf round trip");
    // 缺口钉最近段（105200 → seg1 起点外推 → 钳 0 → cum[1]+0 = 5000）
    CHECK(syncStreamOf(m, 105200) == 5000, "merged: gap pins nearest seg start");
    const auto pr = syncMergedSegmentOf(m, 6500);
    CHECK(pr.first == 1 && pr.second == 1500, "merged: segmentOf decodes (seg,real)");

    // 先起步者赢：两帧重叠 [104000,105000)
    SyncLaneData ov;
    ov.id = "M_OV";
    ov.calibrated = true;
    ov.segments = { makeSeg("/fake/a.mp4", 100000, 10000),   // [100000,110000)
                    makeSeg("/fake/b.mp4", 104000, 4000) };  // [104000,108000)
    CHECK(syncSegmentAt(ov, 106000) == 0, "merged: earlier-start seg wins overlap");
    CHECK(syncSegmentAt(ov, 108500) == 0, "merged: only seg0 covers 108500");
    CHECK(syncSegmentAt(ov, 10000) == -1, "merged: nothing covers before start");
    // 重叠区取赢者：106000 在 seg0 → real 6000 → 虚拟 6000
    CHECK(syncStreamOf(ov, 106000) == 6000, "merged: overlap maps via winner");
}

// P-69：SegmentSwitchEngine 经服务装载——跨段 seek 换文件、位置回报虚拟轴
static void testMergedServiceSwitch()
{
    MultiCamSyncService svc;
    QVector<FakeEngine *> eng;
    SyncLaneData m;
    m.id = "M_D17";
    m.displayName = "D17";
    m.calibrated = true;
    m.segments = { makeSeg("/fake/a.mp4", 100000, 5000),
                   makeSeg("/fake/b.mp4", 105500, 4000) };
    m.durationMs = 9000;
    m.path = m.segments.first().path;
    // 定制工厂：段时长按路径回报（与段装配一致，验证引擎实测自修正不扰动）
    svc.setEngineFactory([&eng](QObject *parent) -> IVideoEngine * {
        auto *e = new FakeEngine(parent);
        e->durByPath = {{"/fake/a.mp4", 5000}, {"/fake/b.mp4", 4000}};
        eng.append(e);
        return e;
    });
    bool finished = false;
    QObject::connect(&svc, &MultiCamSyncService::loadFinished,
                     [&finished]() { finished = true; });
    CHECK(svc.loadLanes({m}), "merged svc: loadLanes accepted");
    pump(50);
    CHECK(finished, "merged svc: loadFinished emitted");

    CHECK(eng.size() == 1, "merged svc: one real engine (C5 resource bound)");
    auto *sw = qobject_cast<SegmentSwitchEngine *>(svc.engineAt(0));
    CHECK(sw != nullptr, "merged svc: lane engine is SegmentSwitchEngine");
    if (!sw)
        return;
    CHECK(eng[0]->path == QLatin1String("/fake/a.mp4"),
          "merged svc: loads segment 0 first");
    CHECK(sw->currentSegment() == 0, "merged svc: current segment 0");
    CHECK(svc.lanes()[0].durationMs == 9000, "merged svc: lane duration = sum");

    // 同段 seek：直接落点，不换文件
    svc.seekWall(102000);
    CHECK(eng[0]->lastSeek == 2000, "merged svc: seek in seg0 → real ms");
    const int loads = eng[0]->loadCount;

    // 跨段 seek：换文件到 seg1
    svc.seekWall(107000);
    pump(10);   // 假引擎 durationChanged 是 singleShot(0) 异步
    CHECK(eng[0]->loadCount == loads + 1, "merged svc: cross-seg seek reloads file");
    CHECK(eng[0]->path == QLatin1String("/fake/b.mp4"),
          "merged svc: switched to segment 1 file");
    CHECK(eng[0]->lastSeek == 1500, "merged svc: pending seek lands in seg1 real ms");
    CHECK(sw->currentSegment() == 1, "merged svc: current segment 1");

    // 位置回报虚拟轴（cum[1]=5000 + 真实 1500）
    CHECK(sw->position() == 6500, "merged svc: position on virtual axis");
    svc.closeAll();
}

// P-69：buildMergedGroups 分组规则（同归组键≥2、全校时、按起点升序；
// 归组键=cameraLabel 非空用之、缺省=自身 id——兼容「产物标签指向源机位 id」）
static void testMergedGrouping()
{
    QVector<CamInventoryItem> items;
    auto addItem = [&](const QString &id, const QString &label,
                       qint64 wallStart, bool calibrated) {
        CamInventoryItem it;
        it.id = id;
        it.displayName = label.isEmpty() ? id + ".mp4" : label;
        it.groupKey = label.isEmpty() ? id : label;   // 与 buildCamInventory 同源
        it.path = QStringLiteral("/fake/%1.mp4").arg(id);
        it.pathExists = true;
        it.calibrated = calibrated;
        if (calibrated) {
            it.lane.calibrated = true;
            it.lane.cal = makeCal(wallStart);
            it.lane.durationMs = 60000;
        }
        items.append(it);
    };
    addItem("V1", "D17", 300000, true);    // 乱序录入：验证分组内按起点排序
    addItem("V2", "D17", 100000, true);
    addItem("V3", "D17", 200000, false);   // 未校时 → 整组不成立（合并仅模式A）
    auto groups = buildMergedGroups(items);
    CHECK(groups.isEmpty(), "grouping: uncalibrated member disqualifies group");

    items[2].calibrated = true;
    items[2].lane.calibrated = true;
    items[2].lane.cal = makeCal(200000);
    items[2].lane.durationMs = 60000;
    groups = buildMergedGroups(items);
    CHECK(groups.size() == 1, "grouping: one D17 group");
    CHECK(groups[0].memberIdx.size() == 3, "grouping: three members");
    CHECK(groups[0].memberIdx == (QVector<int>{1, 2, 0}),
          "grouping: members sorted by wallStart asc");

    addItem("V4", "D15", 50000, true);     // 另一标签仅 1 个 → 不成组
    groups = buildMergedGroups(items);
    CHECK(groups.size() == 1, "grouping: single-member label no group");

    // 真机数据形态（天河案返修）：无标签产物以自身 id 为键，另一产物标签
    // 指向该 id → 同组（P002 无标签 + P005 标签="P002"）
    QVector<CamInventoryItem> prod;
    auto addProd = [&](const QString &id, const QString &label, qint64 ws) {
        CamInventoryItem it;
        it.id = id;
        it.groupKey = label.isEmpty() ? id : label;
        it.displayName = label.isEmpty() ? QStringLiteral("merged_concat.mp4") : label;
        it.path = "/fake/" + id;
        it.pathExists = true;
        it.calibrated = true;
        it.lane.calibrated = true;
        it.lane.cal = makeCal(ws);
        it.lane.durationMs = 60000;
        prod.append(it);
    };
    addProd("P002", QString(), 100000);   // 无标签 → 键=自身 id
    addProd("P005", "P002", 500000);      // 标签指向 P002 → 同组
    addProd("P003", QString(), 200000);   // 另一个无标签产物 → 键=P003，单成员
    addProd("P004", "P003", 300000);      // → 与 P003 同组
    groups = buildMergedGroups(prod);
    CHECK(groups.size() == 2, "grouping: label-to-id yields two product groups");
    CHECK(groups[0].label == QLatin1String("P002")
          && groups[0].memberIdx == (QVector<int>{0, 1}),
          "grouping: P002 group = {P002,P005} by wallStart");
    CHECK(groups[1].label == QLatin1String("P003")
          && groups[1].memberIdx == (QVector<int>{2, 3}),
          "grouping: P003 group = {P003,P004}");
    // 交叉污染检查：同名文件名（merged_concat.mp4）不再误入同组
    CHECK(groups[0].label != groups[1].label, "grouping: filename not the key");

    // 默认勾选决策（二轮返修）：组行优先，成员不勾（否则互斥把组行禁用变灰）
    {
        const auto d = pickerDefaultChecks(prod, groups);
        CHECK(d.groups.size() == 2, "defaults: both groups checked");
        CHECK(d.members.isEmpty(), "defaults: group members not auto-checked");
    }
    {
        // 组 + 非组成员混合：组计 1 路，余项补足，总 cap=4
        addProd("V9", "D15", 900000);      // 单成员标签不成组 → 作为普通项补足
        const auto d = pickerDefaultChecks(prod, groups);
        CHECK(d.groups.size() == 2, "defaults: groups first");
        CHECK(d.members == (QSet<int>{4}), "defaults: ungrouped calibrated fills");
        prod[2].calibrated = false;        // P003 变未校时 → 其组整组撤销
        const auto g2 = buildMergedGroups(prod);
        const auto d2 = pickerDefaultChecks(prod, g2);
        CHECK(g2.size() == 1, "defaults: uncal member dissolves P003 group");
        CHECK(d2.groups == (QSet<int>{0}), "defaults: only P002 group checked");
        CHECK(d2.members.contains(4), "defaults: ungrouped items fill up");
    }
}

// P-73 UX 重做：引导状态机 + 口语化钟差文案
static void testEventCalibGuidance()
{
    using namespace eventcalib;
    CHECK(guidanceStep(false, 0, false) == 0, "guide: no lanes -> pick");
    CHECK(guidanceStep(true, 0, false) == 1, "guide: lanes, no mark -> mark");
    CHECK(guidanceStep(true, 1, false) == 2, "guide: 1 mark -> can preview");
    CHECK(guidanceStep(true, 2, false) == 2, "guide: 2 marks still pre-preview");
    CHECK(guidanceStep(true, 1, true) == 3, "guide: previewed -> save");
    CHECK(guidanceStep(false, 3, true) == 0, "guide: lanes lost dominates");

    CHECK(plainClockDeltaText(134000) == QStringLiteral("目标的钟慢 2 分 14 秒"),
          "plain: 2m14s slow");
    CHECK(plainClockDeltaText(-45000) == QStringLiteral("目标的钟快 45.0 秒"),
          "plain: 45s fast");
    CHECK(plainClockDeltaText(3723000) == QStringLiteral("目标的钟慢 1 小时 2 分"),
          "plain: 1h2m slow");
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
    testServiceUnlinkedTemp();
    testServiceAlign();
    testServiceLoadFailure();
    testServicePerfGovernance();
    testServiceGopThreshold();
    testServiceEngineDies();
    testEventCalibFit();
    testEventCalibCycleAndChain();
    testEventCalibJsonRoundTrip();
    testMergedMapping();
    testMergedServiceSwitch();
    testMergedGrouping();
    testEventCalibGuidance();
    fprintf(stderr, "sync_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}

#include "sync_test_main.moc"
