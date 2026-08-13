// 案件系统端到端自检（v1.3.0 封版后全面自检）：单一连续流程覆盖
// 建案 → 中文路径视频入案 → 指纹队列 → 校时徽标/框选记忆/现场 → 关案重开恢复
// → 删源缺失 → 批量重定位（比对採用+异内容留档）→ 键迁移挂接（仿 MainWindow）
// → 全量校验 → 完整包导出换机零操作 → 包内篡改必报 → 轻量包重定位后可用。
// offscreen 仅需 QCoreApplication。
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QSettings>
#include <cstdio>
#include <functional>
#include "domain/case_model.h"
#include "app/case_manager.h"
#include "videostatemanager.h"

static int g_checks = 0, g_failures = 0;
#define CHECK(cond, msg) do { ++g_checks; if (!(cond)) { ++g_failures; \
    fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); } } while (0)

struct RecentGuard {
    QStringList saved;
    RecentGuard() {
        CaseManager probe;
        saved = probe.recentCases();
        for (const auto &d : saved)
            probe.removeRecent(d);
    }
    ~RecentGuard() {
        QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
        s.setValue(QStringLiteral("case/recent"), saved);
    }
};

static bool pumpUntil(std::function<bool()> cond, qint64 timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!cond() && t.elapsed() < timeoutMs)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
    return cond();
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    RecentGuard recentGuard;
    QTemporaryDir root;
    CHECK(root.isValid(), "e2e: tmp root");

    auto mkVideo = [&](const QString &sub, const QString &name,
                       const QByteArray &content) {
        const QString dir = root.filePath(sub);
        QDir().mkpath(dir);
        QFile f(QDir(dir).filePath(name));
        f.open(QIODevice::WriteOnly);
        f.write(content);
        f.close();
        return f.fileName();
    };

    // ---- ① 建案 + 中文路径视频入案 ----
    const QByteArray cA(8000, 'a'), cB(9000, 'b'), cC(10000, 'c');
    const QString vA = mkVideo(QStringLiteral("监控/东区"),
                               QStringLiteral("中文 录像 ①.mp4"), cA);
    const QString vB = mkVideo(QStringLiteral("监控"), QStringLiteral("cam2.mp4"), cB);
    const QString vC = mkVideo(QStringLiteral("监控"), QStringLiteral("cam3.mp4"), cC);

    CaseManager cm;
    QString err;
    CaseMeta meta;
    meta.caseNo = QStringLiteral("20260813-自检-a");
    meta.title = QStringLiteral("端到端自检");
    meta.investigator = QStringLiteral("黄景云");
    meta.unit = QStringLiteral("广东省火调技术中心");
    CHECK(cm.createCase(root.path(), meta, &err), "e2e: createCase");

    int hashDone = 0;
    QObject::connect(&cm, &CaseManager::hashQueueFinished, [&]() { ++hashDone; });
    const QString idA = cm.addVideo(vA, &err);
    const QString idB = cm.addVideo(vB, &err);
    const QString idC = cm.addVideo(vC, &err);
    CHECK(!idA.isEmpty() && !idB.isEmpty() && !idC.isEmpty(),
          "e2e: 3 videos added (incl. Chinese path)");
    CHECK(pumpUntil([&]() { return hashDone == 1; }, 15000),
          "e2e: initial fingerprints done");
    CHECK(cm.videoById(idA)->sha256.size() == 64
          && cm.videoById(idB)->sha256.size() == 64
          && cm.videoById(idC)->sha256.size() == 64,
          "e2e: all fingerprints registered");

    // ---- ② 校时徽标 / 框选记忆 / 现场，关案重开全恢复 ----
    cm.updateCalibrationBadge(vA, true, QStringLiteral("OCR 3点, rate=1.000"));
    cm.setTimestampRoi(vB, QRectF(0.4, 0.02, 0.3, 0.06));
    cm.setLastVideoId(idC);
    CHECK(cm.calibratedVideoCount() == 1, "e2e: badge count 1");
    CHECK(cm.saveCase(&err), "e2e: save");
    cm.closeCase();
    {
        QStringList warnings;
        CHECK(cm.openCase(QDir(root.path()).filePath(
                  QStringLiteral("20260813-自检-a-端到端自检")), &err, &warnings),
              "e2e: reopen");
        CHECK(cm.calibratedVideoCount() == 1, "e2e: badge persisted");
        CHECK(cm.videoById(idA)->calibrationSummary.contains("OCR"),
              "e2e: badge summary persisted");
        const QRectF roi = cm.timestampRoiFor(vB);
        CHECK(qAbs(roi.x() - 0.4) < 1e-9 && qAbs(roi.width() - 0.3) < 1e-9,
              "e2e: ROI persisted");
        CHECK(cm.meta().lastVideoId == idC, "e2e: lastVideoId persisted");
        // 指纹齐全：重开不触发补算
        cm.queueMissingHashes();
        CHECK(!cm.hashQueueActive(), "e2e: no rehash needed after reopen");
    }

    // ---- ③ 删源 → 批量重定位（含键迁移挂接，仿 MainWindow 接线） ----
    VideoStateManager vsm;
    QObject::connect(&cm, &CaseManager::videoRelocated, &vsm,
                     [&vsm](const QString &, const QString &o, const QString &n) {
                         vsm.migrateKey(o, n);
                     });
    vsm.saveState(vA, {}, {QRect(1, 1, 2, 2)}, {}, {}, {}, {}, {});
    // 候选目录：A 同内容副本 / B 异尺寸异内容 / C 同内容副本（⑦轻量包用）
    const QString candA = mkVideo(QStringLiteral("新位置"),
                                  QStringLiteral("中文 录像 ①.mp4"), cA);
    const QString candB = mkVideo(QStringLiteral("新位置"),
                                  QStringLiteral("cam2.mp4"),
                                  QByteArray(9500, 'B'));
    const QString candC = mkVideo(QStringLiteral("新位置"),
                                  QStringLiteral("cam3.mp4"), cC);
    QFile::remove(vA);
    QFile::remove(vB);
    {
        const auto pc = cm.exportPrecheck(root.filePath(QStringLiteral("out")),
                                          true);
        CHECK(pc.missingVideoIds.size() == 2, "e2e: precheck sees 2 missing");
    }
    const auto cands = cm.proposeRelocations(root.filePath(
        QStringLiteral("新位置")));
    CHECK(cands.size() == 2, "e2e: 2 relocation proposals");
    QString propA, propB;
    int lvlA = -1, lvlB = -1;
    for (const auto &c : cands) {
        if (c.videoId == idA) { propA = c.candidatePath; lvlA = c.matchLevel; }
        if (c.videoId == idB) { propB = c.candidatePath; lvlB = c.matchLevel; }
    }
    CHECK(lvlA == 2 && QDir::cleanPath(propA) == QDir::cleanPath(candA),
          "e2e: A name+size candidate");
    CHECK(lvlB == 1, "e2e: B name-only candidate (diff size→lvl1)");
    // A：指纹一致採用（knownSha 免重算）→ 内存状态键迁移生效
    QString shaA;
    CHECK(CaseManager::computeSha256(propA, &shaA), "e2e: hash cand A");
    CHECK(shaA == cm.videoById(idA)->sha256, "e2e: A fingerprint consistent");
    CHECK(cm.relocateVideo(idA, propA, &err, nullptr, false, shaA),
          "e2e: A adopted");
    CHECK(!vsm.hasState(vA) && vsm.hasState(propA),
          "e2e: VideoStateManager key migrated via videoRelocated");
    // B：异尺寸 → 大小守卫默认拒绝，显式仍要采用 → 覆写留档
    QString shaB;
    CHECK(CaseManager::computeSha256(propB, &shaB), "e2e: hash cand B");
    CHECK(shaB != cm.videoById(idB)->sha256, "e2e: B fingerprint differs");
    bool mismatch = false;
    CHECK(!cm.relocateVideo(idB, propB, &err, &mismatch, false, shaB),
          "e2e: B default rejected (size guard)");
    CHECK(mismatch, "e2e: B mismatch flagged");
    CHECK(cm.relocateVideo(idB, propB, &err, nullptr, true, shaB),
          "e2e: B force adopted");
    CHECK(cm.meta().extraFields.contains(
              QStringLiteral("relocateOverride/") + idB),
          "e2e: B override archived");

    // ---- ④ 全量校验：重定位后全部一致 ----
    QVector<CaseIntegrityItem> report;
    QObject::connect(&cm, &CaseManager::integrityReportReady,
                     [&](const QVector<CaseIntegrityItem> &items) {
                         report = items;
                     });
    cm.verifyIntegrity(true);
    CHECK(pumpUntil([&]() { return !report.isEmpty(); }, 20000),
          "e2e: verify report");
    {
        bool allOk = report.size() == 3;
        for (const auto &it : report)
            if (it.status != 0) allOk = false;
        CHECK(allOk, "e2e: all consistent after relocation");
    }
    CHECK(cm.saveCase(&err), "e2e: save after relocation");

    // ---- ⑤ 完整包导出 → 删全部源 → 换机零操作 ----
    const QString outRoot = root.filePath(QStringLiteral("out"));
    int expDone = 0;
    bool expOk = false;
    QObject::connect(&cm, &CaseManager::exportFinished,
                     [&](bool ok, const QString &m) {
                         ++expDone; expOk = ok;
                         if (!ok)
                             fprintf(stderr, "  export FAIL msg: %s\n",
                                     qPrintable(m));
                     });
    cm.exportCase(outRoot, true);
    CHECK(pumpUntil([&]() { return expDone == 1; }, 30000),
          "e2e: full export finished");
    CHECK(expOk, "e2e: full export ok");
    const QString pkg = outRoot + QStringLiteral("/20260813-自检-a-端到端自检");
    CHECK(QFile::exists(pkg + QStringLiteral("/sources/") + idA
                        + QStringLiteral("__中文 录像 ①.mp4")),
          "e2e: Chinese-name bundled copy exists");
    QFile::remove(vC);   // A/B 已删，C 现在也删 → 三源全缺失
    // 换机模拟：重定位后的“新位置”在接收端不可见（否则 effectivePathFor
    // 正确命中原路径，走不到包内副本兼底）
    const QString newLoc = root.filePath(QStringLiteral("新位置"));
    CHECK(QDir().rename(newLoc, newLoc + QStringLiteral("_away")),
          "e2e: simulate machine change");
    {
        CaseManager recv;
        CHECK(recv.openCase(pkg, &err), "e2e: package opens on receiver");
        const QString effA = recv.effectivePathFor(*recv.videoById(idA));
        CHECK(effA.contains(QStringLiteral("sources/")) && QFile::exists(effA),
              "e2e: bundled fallback works");
        CHECK(recv.isCaseVideo(effA), "e2e: bundled path hits membership");
        const QString effB = recv.effectivePathFor(*recv.videoById(idB));
        CHECK(recv.timestampRoiFor(effB).isValid(),
              "e2e: ROI resolvable via bundled path");
        QVector<CaseIntegrityItem> pkgReport;
        QObject::connect(&recv, &CaseManager::integrityReportReady,
                         [&](const QVector<CaseIntegrityItem> &items) {
                             pkgReport = items;
                         });
        recv.verifyIntegrity(true);
        CHECK(pumpUntil([&]() { return !pkgReport.isEmpty(); }, 20000),
              "e2e: package verify report");
        bool allOk = pkgReport.size() == 3;
        for (const auto &it : pkgReport)
            if (it.status != 0) allOk = false;
        CHECK(allOk, "e2e: package verify all consistent (zero-touch)");

        // ---- ⑥ 篡改包内文件 → 校验必报 ----
        const QString victim = pkg + QStringLiteral("/sources/") + idC
                               + QStringLiteral("__cam3.mp4");
        {
            QFile f(victim);
            f.open(QIODevice::ReadWrite);
            f.seek(0);
            f.write("X", 1);
            f.close();
        }
        pkgReport.clear();
        recv.verifyIntegrity(true);
        CHECK(pumpUntil([&]() { return !pkgReport.isEmpty(); }, 20000),
              "e2e: tamper verify report");
        bool caught = false;
        for (const auto &it : pkgReport)
            if (it.status == 1) caught = true;
        CHECK(caught, "e2e: tampered copy MUST be flagged changed");
        recv.closeCase();
    }

    // ---- ⑦ 轻量包：无副本 → 接收端重定位后可用 ----
    int expDone2 = 0;
    bool expOk2 = false;
    QObject::connect(&cm, &CaseManager::exportFinished,
                     [&](bool ok, const QString &m) {
                         ++expDone2; expOk2 = ok;
                         if (!ok)
                             fprintf(stderr, "  light export FAIL msg: %s\n",
                                     qPrintable(m));
                     });
    cm.exportCase(outRoot + QStringLiteral("2"), false);
    CHECK(pumpUntil([&]() { return expDone2 == 1; }, 30000),
          "e2e: light export finished");
    CHECK(expOk2, "e2e: light export ok");
    // 源找回（轻量包接收端场景：源视频经重定位回本机）
    CHECK(QDir().rename(newLoc + QStringLiteral("_away"), newLoc),
          "e2e: restore sources folder");
    {
        const QString pkg2 = outRoot + QStringLiteral("2/20260813-自检-a-端到端自检");
        CaseManager recv;
        CHECK(recv.openCase(pkg2, &err), "e2e: light package opens");
        CHECK(recv.videoById(idA)->bundledRelPath.isEmpty(),
              "e2e: light has no bundled copy");
        // 接收端：C 源已备在新位置 → 重定位（轻量包重定位后可用）
        QString shaC;
        CHECK(CaseManager::computeSha256(candC, &shaC), "e2e: hash cand C");
        CHECK(recv.relocateVideo(idC, candC, &err, nullptr, false, shaC),
              "e2e: light receiver relocates C");
        QVector<CaseIntegrityItem> lr;
        QObject::connect(&recv, &CaseManager::integrityReportReady,
                         [&](const QVector<CaseIntegrityItem> &items) {
                             lr = items;
                         });
        recv.verifyIntegrity(true);
        CHECK(pumpUntil([&]() { return !lr.isEmpty(); }, 20000),
              "e2e: light verify report");
        bool allOk = lr.size() == 3;
        for (const auto &it : lr)
            if (it.status != 0) allOk = false;
        CHECK(allOk, "e2e: light package consistent after relocation");
        recv.closeCase();
    }
    cm.closeCase();

    fprintf(stderr, "case_e2e: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
