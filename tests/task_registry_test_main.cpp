/**
 * @file task_registry_test_main.cpp
 * @brief P1a 任务注册表 + AnalysisTaskService 状态机单测（v1.8.0 P-30）
 *
 * 覆盖：
 *  1. TaskRegistry 注册/查找/覆盖/未知 id；
 *  2. AnalysisTaskService 状态机全迁移路径（Idle→Running→Finished/Failed/Cancelled）；
 *  3. 对抗：Running 中重复 start 拒绝；cancel 后迟到的 finished/failed 被忽略
 *     （竞态 gating，替代旧版按错误文案判断取消——C1 收口）；
 *  4. 前置条件失败路径（无 ROI → precondition 错误，不触碰引擎）；
 *  5. 合并策略：亮度完成保留既有 audio；audio-only 完成保留既有亮度
 *     （producedChannels 逐通道覆盖，未产出通道保持）。
 */
#include "domain/task_registry.h"
#include "domain/timeline_model.h"
#include "app/analysis_task_service.h"
#include "infrastructure/ianalysis_engine.h"
#include <QCoreApplication>
#include <QEventLoop>
#include <QTimer>
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

// ---------------------------------------------------------------------------
// 测试替身引擎：手工驱动信号（状态机测试不依赖真实解码）
// ---------------------------------------------------------------------------
class FakeEngine : public IAnalysisEngine
{
    Q_OBJECT
public:
    using IAnalysisEngine::IAnalysisEngine;

    void startAnalysis(const QString &, const QVector<QRect> &,
                       const QVector<QPolygon> &, const QStringList &,
                       const QVector<int> &, const QVector<int> &) override
    {
        startCalls++;
    }
    void startAudioAnalysis(const QString &) override { audioCalls++; }
    void cancelAnalysis() override { cancels++; }
    bool isRunning() const override { return running; }

    int startCalls = 0;
    int audioCalls = 0;
    int cancels = 0;
    bool running = false;
};

// ---------------------------------------------------------------------------
// 1. TaskRegistry
// ---------------------------------------------------------------------------
static void testRegistryBasics()
{
    auto &reg = TaskRegistry::instance();
    const int before = reg.tasks().size();

    AnalysisTaskDesc d1;
    d1.taskId = "test-a";
    d1.displayNameZh = "任务A";
    d1.displayNameEn = "Task A";
    d1.preconditionError = [] { return QString(); };
    d1.producedChannels = {AnalysisChannels::luminance()};
    reg.registerTask(d1);

    AnalysisTaskDesc d2;
    d2.taskId = "test-b";
    d2.displayNameZh = "任务B";
    d2.displayNameEn = "Task B";
    d2.preconditionError = [] { return QStringLiteral("缺前置"); };
    d2.producedChannels = {AnalysisChannels::audio()};
    reg.registerTask(d2);

    CHECK(reg.tasks().size() == before + 2, "registry: two tasks added");
    const AnalysisTaskDesc *a = reg.find("test-a");
    const AnalysisTaskDesc *b = reg.find("test-b");
    CHECK(a && a->displayNameEn == "Task A", "registry: find by id");
    CHECK(a->producedChannels == QStringList{AnalysisChannels::luminance()},
          "registry: producedChannels stored");
    CHECK(reg.find("no-such") == nullptr, "registry: unknown id -> nullptr");

    // 覆盖注册（同 id）
    AnalysisTaskDesc d1v2;
    d1v2.taskId = "test-a";
    d1v2.displayNameZh = "任务A2";
    d1v2.displayNameEn = "Task A2";
    d1v2.preconditionError = [] { return QString(); };
    d1v2.producedChannels = {AnalysisChannels::luminance(), AnalysisChannels::audio()};
    reg.registerTask(d1v2);
    CHECK(reg.tasks().size() == before + 2, "registry: re-register overwrites, no dup");
    CHECK(reg.find("test-a")->displayNameEn == "Task A2", "registry: overwrite visible");
}

// ---------------------------------------------------------------------------
// 2-4. AnalysisTaskService 状态机
// ---------------------------------------------------------------------------
struct ServiceSpy
{
    int started = 0, finished = 0, failed = 0, cancelled = 0, progress = 0;
    QString lastTaskId, lastErrorCode;
    AnalysisSnapshot lastSnapshot;

    void connect(AnalysisTaskService *svc)
    {
        QObject::connect(svc, &AnalysisTaskService::taskStarted,
                         [&](const QString &id) { started++; lastTaskId = id; });
        QObject::connect(svc, &AnalysisTaskService::taskFinished,
                         [&](const QString &id, const AnalysisSnapshot &s) {
                             finished++; lastTaskId = id; lastSnapshot = s; });
        QObject::connect(svc, &AnalysisTaskService::taskFailed,
                         [&](const QString &id, const QString &code, const QString &) {
                             failed++; lastTaskId = id; lastErrorCode = code; });
        QObject::connect(svc, &AnalysisTaskService::taskCancelled,
                         [&](const QString &id) { cancelled++; lastTaskId = id; });
        QObject::connect(svc, &AnalysisTaskService::taskProgress,
                         [&](const QString &, qreal, const QString &) { progress++; });
    }
};

static void testServiceLifecycle()
{
    TaskRegistry::instance().registerTask(
        {AnalysisChannels::luminance(), "亮度", "Luminance",
         [] { return QString(); }, {AnalysisChannels::luminance()}});
    TaskRegistry::instance().registerTask(
        {AnalysisChannels::audio(), "音频", "Audio",
         [] { return QString(); }, {AnalysisChannels::audio()}});

    FakeEngine engine;
    TimelineModel model;
    AnalysisTaskService svc(&engine, &model);
    ServiceSpy spy;
    spy.connect(&svc);

    // --- 2a. 正常完成：亮度任务，保留既有 audio ---
    AudioData existingAudio;
    existingAudio.volume = {0.1f, 0.2f, 0.3f};
    model.setData({100, 200, 300}, {{1.0, 2.0, 3.0}}, existingAudio);

    AnalysisSnapshot lumResult;
    lumResult.setLuminance({100, 200, 300}, {{9.0, 8.0, 7.0}},
                           {DataEntry{DataEntry::Rect, 1}});

    CHECK(svc.start(AnalysisChannels::luminance(), "video.mp4", {}, {}, {}, {}),
          "svc: start luminance ok");
    CHECK(engine.startCalls == 1, "svc: engine startAnalysis invoked");
    CHECK(spy.started == 1, "svc: taskStarted emitted");
    CHECK(svc.isRunning(), "svc: running state");

    // --- 3a. Running 中重复 start 拒绝 ---
    CHECK(!svc.start(AnalysisChannels::audio(), "video.mp4", {}, {}, {}, {}),
          "svc: double start rejected");
    CHECK(engine.audioCalls == 0, "svc: rejected start did not touch engine");
    // 拒绝以 taskFailed(busy) 上报（行为冻结：旧版 QMessageBox::information 同源）
    CHECK(spy.failed == 1 && spy.lastErrorCode == "busy", "svc: busy rejection -> taskFailed(busy)");

    emit engine.progressUpdated(1, 10, 10.0);
    CHECK(spy.progress == 1, "svc: progress forwarded");

    emit engine.analysisFinished(lumResult);
    CHECK(spy.finished == 1, "svc: taskFinished emitted");
    CHECK(!svc.isRunning(), "svc: back to idle");

    // 合并策略：亮度结果 + 既有 audio 保留
    const AnalysisSnapshot merged = model.snapshot();
    CHECK(!merged.audioData().volume.isEmpty()
              && qAbs(merged.audioData().volume[0] - 0.1f) < 1e-6,
          "merge: existing audio preserved on luminance finish");
    CHECK(qAbs(merged.lumRows()[0][0] - 9.0) < 1e-6, "merge: new luminance applied");

    // --- 2b. audio-only 完成：保留既有亮度 ---
    AnalysisSnapshot audioResult;
    AudioData newAudio;
    newAudio.volume = {0.9f};
    audioResult.setAudio(newAudio);
    CHECK(svc.start(AnalysisChannels::audio(), "video.mp4", {}, {}, {}, {}),
          "svc: start audio ok");
    CHECK(engine.audioCalls == 1, "svc: engine startAudioAnalysis invoked");
    emit engine.analysisFinished(audioResult);
    CHECK(spy.finished == 2, "svc: second finish");
    const AnalysisSnapshot merged2 = model.snapshot();
    CHECK(!merged2.lumRows().isEmpty()
              && qAbs(merged2.lumRows()[0][0] - 9.0) < 1e-6,
          "merge: existing luminance preserved on audio finish");
    CHECK(qAbs(merged2.audioData().volume[0] - 0.9f) < 1e-6,
          "merge: new audio applied");
}

static void testServiceFailureAndCancel()
{
    FakeEngine engine;
    TimelineModel model;
    AnalysisTaskService svc(&engine, &model);
    ServiceSpy spy;
    spy.connect(&svc);

    // --- 2c. 失败路径（错误码透传，非取消） ---
    CHECK(svc.start(AnalysisChannels::luminance(), "video.mp4", {}, {}, {}, {}),
          "svc: start ok");
    emit engine.analysisFailed(QStringLiteral("boom"));
    CHECK(spy.failed == 1 && spy.lastErrorCode == "engine_error",
          "svc: engine failure -> taskFailed(engine_error)");
    CHECK(!svc.isRunning(), "svc: idle after failure");

    // --- 3b. cancel 后迟到 finished 被忽略（竞态 gating） ---
    CHECK(svc.start(AnalysisChannels::luminance(), "video.mp4", {}, {}, {}, {}),
          "svc: start ok (2)");
    svc.cancel();
    CHECK(engine.cancels == 1, "svc: cancel forwarded to engine");
    AnalysisSnapshot late;
    late.setLuminance({1}, {{42.0}}, {});
    emit engine.analysisFinished(late);
    CHECK(spy.finished == 0, "svc: late finished after cancel ignored");
    CHECK(spy.cancelled == 1, "svc: taskCancelled emitted");

    // 引擎随后发 failed（cancel 的常规表现）→ 归并为 cancelled，不再重复发
    emit engine.analysisFailed(QStringLiteral("Analysis cancelled by user."));
    // failed 计数仍为 1（仅首段真实失败；取消后的引擎 failed 被归并不重复计）
    CHECK(spy.cancelled == 1 && spy.failed == 1,
          "svc: post-cancel engine failure folded into cancelled (C1)");

    // 状态已回 Idle，可再次启动
    CHECK(svc.start(AnalysisChannels::luminance(), "video.mp4", {}, {}, {}, {}),
          "svc: restart after cancel ok");
}

static void testServicePrecondition()
{
    FakeEngine engine;
    TimelineModel model;
    AnalysisTaskService svc(&engine, &model);
    ServiceSpy spy;
    spy.connect(&svc);

    TaskRegistry::instance().registerTask(
        {"needs-roi", "需ROI", "NeedsRoi",
         [] { return QStringLiteral("请先绘制 ROI"); }, {"luminance"}});

    // --- 4. 前置条件失败：不触碰引擎 ---
    CHECK(!svc.start("needs-roi", "video.mp4", {}, {}, {}, {}),
          "svc: precondition failure rejects start");
    CHECK(engine.startCalls == 0, "svc: engine untouched on precondition failure");
    CHECK(spy.failed == 1 && spy.lastErrorCode == "precondition",
          "svc: precondition -> taskFailed(precondition)");
    CHECK(!svc.isRunning(), "svc: idle after precondition failure");

    // --- 未知任务 id ---
    CHECK(!svc.start("no-such-task", "video.mp4", {}, {}, {}, {}),
          "svc: unknown task id rejected");
    CHECK(spy.failed == 2 && spy.lastErrorCode == "unknown_task",
          "svc: unknown task -> taskFailed(unknown_task)");
}

static void testServiceNoVideoNoStart()
{
    FakeEngine engine;
    TimelineModel model;
    AnalysisTaskService svc(&engine, &model);
    ServiceSpy spy;
    spy.connect(&svc);

    // --- 空视频路径（旧 onAnalyze 首检行为等价迁移） ---
    CHECK(!svc.start(AnalysisChannels::luminance(), QString(), {}, {}, {}, {}),
          "svc: empty video path rejected");
    CHECK(engine.startCalls == 0, "svc: engine untouched on empty path");
    CHECK(spy.failed == 1 && spy.lastErrorCode == "no_video",
          "svc: empty path -> taskFailed(no_video)");
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testRegistryBasics();
    testServiceLifecycle();
    testServiceFailureAndCancel();
    testServicePrecondition();
    testServiceNoVideoNoStart();
    fprintf(stderr, "task_registry_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures == 0 ? 0 : 1;
}

#include "task_registry_test_main.moc"
