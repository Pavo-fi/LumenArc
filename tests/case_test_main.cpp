// case_model 单测（v1.3.0 M1）：case.json 往返/版本与 magic 规则/未知字段/
// 原子写/编号生成/视频编号分配。offscreen 无需显示，仅需 QCoreApplication。
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QTemporaryDir>
#include <QSettings>
#include <cstdio>
#include "domain/case_model.h"
#include "app/case_manager.h"

static int g_checks = 0, g_failures = 0;
#define CHECK(cond, msg) do { ++g_checks; if (!(cond)) { ++g_failures; \
    fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); } } while (0)

static CaseMeta makeSample()
{
    CaseMeta m;
    m.caseNo = QStringLiteral("20260813-广州天河-a");
    m.title = QStringLiteral("xx厂房火灾");
    m.investigator = QStringLiteral("黄景云");
    m.unit = QStringLiteral("广东省火调技术中心");
    m.incidentTimeMs = 1755014400000LL;   // 2026-08-13
    m.city = QStringLiteral("广州");
    m.district = QStringLiteral("天河");
    m.locationDetail = QStringLiteral("xx路 13 号");
    m.description = QStringLiteral("备注文本");
    m.createdMs = 1755000000000LL;
    m.modifiedMs = 1755001000000LL;
    m.lastVideoId = QStringLiteral("V002");
    m.nextVideoSeq = 3;
    CaseVideoRef v1;
    v1.id = QStringLiteral("V001");
    v1.originalPath = QStringLiteral("D:/监控/D17_xxx.mp4");
    v1.vlaRelPath = QStringLiteral("videos/V001.vla");
    v1.sizeBytes = 123456789;
    v1.mtimeMs = 1754900000000LL;
    v1.sha256 = QStringLiteral("abcd1234");
    v1.timestampRoi = QRectF(0.42, 0.02, 0.30, 0.06);
    v1.hasCalibration = true;
    v1.calibrationSummary = QStringLiteral("OCR 三点, rate=1.000");
    CaseVideoRef v2;
    v2.id = QStringLiteral("V002");
    v2.originalPath = QStringLiteral("D:/监控/02-39-10_6m.mp4");
    v2.vlaRelPath = QStringLiteral("videos/V002.vla");
    v2.bundledRelPath = QStringLiteral("sources/V002__02-39-10_6m.mp4");
    m.videos = {v1, v2};
    CasePreprocessRef p;
    p.sessionDirRelPath = QStringLiteral("preprocess/20260813_143022");
    p.reportCsvRelPath = QStringLiteral("preprocess/20260813_143022/report.csv");
    CaseVideoRef out;
    out.id = QStringLiteral("P001");
    out.originalPath = QStringLiteral("D:/out/merged.mp4");
    out.sizeBytes = 987654321;
    p.outputRefs = {out};
    p.sidecarRelPaths = {QStringLiteral("preprocess/20260813_143022/sidecars/merged.lumencal.json")};
    m.preprocessSessions = {p};
    m.reports = {QStringLiteral("reports/xx厂房火灾_分析报告.docx")};
    m.extraFields[QStringLiteral("custom1")] = QStringLiteral("v1");
    return m;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // ---- 1. 全字段往返 ----
    {
        QTemporaryDir dir;
        CHECK(dir.isValid(), "tmp dir valid");
        const CaseMeta in = makeSample();
        QString err;
        CHECK(CaseModel::save(dir.path(), in, &err), "save round-trip");
        if (!err.isEmpty()) fprintf(stderr, "  save err: %s\n", qPrintable(err));
        CaseMeta out;
        QStringList warnings;
        CHECK(CaseModel::load(dir.path(), out, &err, &warnings), "load round-trip");
        if (!err.isEmpty()) fprintf(stderr, "  load err: %s\n", qPrintable(err));
        CHECK(warnings.isEmpty(), "round-trip: no unknown-field warnings");
        CHECK(out.caseNo == in.caseNo, "caseNo");
        CHECK(out.title == in.title && out.investigator == in.investigator
              && out.unit == in.unit, "title/investigator/unit");
        CHECK(out.incidentTimeMs == in.incidentTimeMs, "incidentTimeMs");
        CHECK(out.city == in.city && out.district == in.district
              && out.locationDetail == in.locationDetail, "location fields");
        CHECK(out.description == in.description, "description");
        CHECK(out.createdMs == in.createdMs && out.modifiedMs == in.modifiedMs,
              "created/modified");
        CHECK(out.lastVideoId == in.lastVideoId, "lastVideoId");
        CHECK(out.nextVideoSeq == 3, "nextVideoSeq round-trip");
        CHECK(out.videos.size() == 2, "videos count");
        if (out.videos.size() == 2) {
            const auto &v = out.videos[0];
            CHECK(v.id == "V001" && v.sizeBytes == 123456789
                  && v.sha256 == "abcd1234", "video scalar fields");
            CHECK(v.timestampRoi.isValid()
                  && qAbs(v.timestampRoi.x() - 0.42) < 1e-9
                  && qAbs(v.timestampRoi.width() - 0.30) < 1e-9,
                  "timestampRoi round-trip");
            CHECK(v.hasCalibration && v.calibrationSummary.contains("rate"),
                  "calibration badge cache");
            CHECK(out.videos[1].bundledRelPath.contains("sources/V002"),
                  "bundledRelPath round-trip");
        }
        CHECK(out.preprocessSessions.size() == 1
              && out.preprocessSessions[0].outputRefs.size() == 1
              && out.preprocessSessions[0].sidecarRelPaths.size() == 1,
              "preprocess round-trip");
        CHECK(out.reports.size() == 1, "reports round-trip");
        CHECK(out.extraFields.value("custom1") == "v1", "extraFields round-trip");
        // 原子写：无临时文件残留
        CHECK(!QFile::exists(dir.filePath("case.json.tmp")),
              "atomic save: no tmp residue");
        CHECK(QFile::exists(dir.filePath("case.json")), "case.json exists");
    }

    // ---- 2. 版本/magic/损坏 拒绝规则（F1/F4）----
    {
        QTemporaryDir dir;
        QString err;
        CaseMeta out;
        // 高版本
        QFile f(dir.filePath("case.json"));
        f.open(QIODevice::WriteOnly);
        f.write(R"({"magic":"LumenArcCase","formatVersion":99})");
        f.close();
        CHECK(!CaseModel::load(dir.path(), out, &err), "future version rejected");
        CHECK(err.contains("更新版本"), "future version error mentions upgrade");
        // magic 不符
        f.open(QIODevice::WriteOnly);
        f.write(R"({"magic":"Other","formatVersion":1})");
        f.close();
        CHECK(!CaseModel::load(dir.path(), out, &err), "bad magic rejected");
        // 损坏 JSON
        f.open(QIODevice::WriteOnly);
        f.write("{not json");
        f.close();
        CHECK(!CaseModel::load(dir.path(), out, &err), "corrupt json rejected");
        // 缺版本号
        f.open(QIODevice::WriteOnly);
        f.write(R"({"magic":"LumenArcCase"})");
        f.close();
        CHECK(!CaseModel::load(dir.path(), out, &err), "missing version rejected");
    }

    // ---- 3. 未知字段忽略并警告（F3 只加不改）----
    {
        QTemporaryDir dir;
        QFile f(dir.filePath("case.json"));
        f.open(QIODevice::WriteOnly);
        f.write(R"({"magic":"LumenArcCase","formatVersion":1,"caseNo":"x",
                    "futureField":{"a":1},"videos":[]})");
        f.close();
        CaseMeta out;
        QStringList warnings;
        QString err;
        CHECK(CaseModel::load(dir.path(), out, &err, &warnings),
              "unknown field tolerated");
        CHECK(warnings.size() == 1 && warnings[0].contains("futureField"),
              "unknown field warned");
    }

    // ---- 4. 编号生成 ----
    {
        QTemporaryDir dir;
        const qint64 t = 1755014400000LL;   // 2025-08-13 00:00 +08:00
        const QString no1 = CaseModel::generateCaseNo(t, "广州", "天河", dir.path());
        CHECK(no1 == "20250813-广州天河-a", "first case no = ...-a");
        QDir().mkpath(dir.filePath(no1 + "-xx厂房火灾"));
        const QString no2 = CaseModel::generateCaseNo(t, "广州", "天河", dir.path());
        CHECK(no2 == "20250813-广州天河-b", "same day+district increments to -b");
        const QString no3 = CaseModel::generateCaseNo(t, "广州", "白云", dir.path());
        CHECK(no3 == "20250813-广州白云-a", "different district back to -a");
        QDir().mkpath(dir.filePath(no2 + "-另一案"));   // -b 也落地
        const QString no4 = CaseModel::generateCaseNo(t, " 广 州 ", " 天 河 ",
                                                      dir.path());
        CHECK(no4 == "20250813-广州天河-c", "whitespace cleaned, increments to -c");
    }

    // ---- 5. 视频编号分配（高水位，移除不复用）----
    {
        CaseMeta m;
        CHECK(CaseModel::allocateVideoId(m) == "V001", "first video V001");
        CHECK(CaseModel::allocateVideoId(m) == "V002", "second video V002");
        // 模拟 V002 移除后：高水位不回退
        m.videos.clear();
        CHECK(CaseModel::allocateVideoId(m) == "V003", "removed id not reused");
        // 旧文件无 nextVideoSeq 字段：load 取既有最大+1
        QTemporaryDir dir;
        QFile f(dir.filePath("case.json"));
        f.open(QIODevice::WriteOnly);
        f.write(R"({"magic":"LumenArcCase","formatVersion":1,
                    "videos":[{"id":"V001"},{"id":"V003"}]})");
        f.close();
        CaseMeta loaded;
        QString err;
        CHECK(CaseModel::load(dir.path(), loaded, &err), "load legacy case");
        CHECK(loaded.nextVideoSeq == 4, "legacy: nextVideoSeq = max+1");
        CHECK(CaseModel::allocateVideoId(loaded) == "V004",
              "legacy: allocates V004");
    }

    // ---- 6. findVideo ----
    {
        CaseMeta m = makeSample();
        CHECK(CaseModel::findVideo(m, "V002") != nullptr, "find V002");
        CHECK(CaseModel::findVideo(m, "V999") == nullptr, "V999 not found");
        const CaseMeta &cm = m;
        CHECK(CaseModel::findVideo(cm, "V001")->originalPath.contains("D17"),
              "const findVideo");
    }

    // ---- 7. CaseManager 生命周期 + 视频登记 ----
    {
        // 快照并清空最近案件（测试结束后恢复，不污染开发机注册表）
        CaseManager probe;
        const QStringList savedRecent = probe.recentCases();
        for (const auto &d : savedRecent)
            probe.removeRecent(d);

        QTemporaryDir root;
        // 三个临时“视频”
        auto mkVideo = [&](const QString &name, qint64 bytes) {
            QFile f(root.filePath(name));
            f.open(QIODevice::WriteOnly);
            f.write(QByteArray(int(bytes), 'x'));
            f.close();
            return f.fileName();
        };
        const QString vid1 = mkVideo(QStringLiteral("监控A.mp4"), 1000);
        const QString vid2 = mkVideo(QStringLiteral("监控B.mp4"), 2000);
        const QString vid3 = mkVideo(QStringLiteral("监控C.mp4"), 3000);

        CaseManager cm;
        // 未开案时的分流 = 独立模式老行为
        CHECK(cm.vlaPathFor(vid1) == vid1 + ".vla", "router: no case → side .vla");
        CHECK(cm.evidenceDirFor(vid1).contains("LumenArc_Calibration"),
              "router: no case → legacy evidence dir");
        CHECK(!cm.isCaseVideo(vid1), "no case → not case video");

        // 新建
        CaseMeta meta;
        meta.caseNo = CaseModel::generateCaseNo(1755014400000LL, "广州", "天河",
                                                root.path());
        meta.title = QStringLiteral("测试案件");
        meta.investigator = QStringLiteral("调查员");
        meta.unit = QStringLiteral("单位");
        meta.incidentTimeMs = 1755014400000LL;
        meta.city = QStringLiteral("广州");
        meta.district = QStringLiteral("天河");
        QString err;
        QTemporaryDir caseParent;
        CHECK(cm.createCase(caseParent.path(), meta, &err), "createCase");
        if (!err.isEmpty()) fprintf(stderr, "  create err: %s\n", qPrintable(err));
        const QString cdir = cm.caseDir();
        CHECK(QDir(cdir + "/videos").exists()
              && QDir(cdir + "/evidence").exists()
              && QDir(cdir + "/preprocess").exists()
              && QDir(cdir + "/reports").exists()
              && QDir(cdir + "/snapshots").exists(), "case subdirs created");
        CHECK(QFile::exists(cdir + "/case.json"), "case.json created");
        CHECK(QFile::exists(cdir + "/case.json.lock"), "lock created");
        CHECK(cm.recentCases().contains(cdir), "recent contains new case");
        CHECK(!cm.isDirty(), "fresh case not dirty");

        // 已开案时不能再建/再开
        {
            CaseManager other;
            QString err2;
            CHECK(!other.openCase(cdir, &err2), "second open rejected");
            bool conflict = false;
            CHECK(!other.openCase(cdir, &err2, nullptr, &conflict) && conflict,
                  "lock conflict reported");
            CHECK(other.openCase(cdir, &err2, nullptr, nullptr, true),
                  "force open works");
            other.closeCase();
            // force open 会重建锁；主 cm 稍后 closeCase 清自己那份
        }

        // 添加视频
        const QString id1 = cm.addVideo(vid1, &err);
        CHECK(id1 == "V001", "addVideo → V001");
        CHECK(cm.isDirty(), "addVideo sets dirty");
        const QString id2 = cm.addVideo(vid2, &err);
        CHECK(id2 == "V002", "addVideo → V002");
        QString dup = cm.addVideo(vid1, &err);
        CHECK(dup.isEmpty() && err.contains("V001"), "duplicate path rejected");
        CHECK(cm.addVideo(root.filePath("nonexist.mp4"), &err).isEmpty(),
              "missing file rejected");
        // 登记信息
        const CaseVideoRef *v1 = cm.videoById(id1);
        CHECK(v1 && v1->sizeBytes == 1000 && v1->mtimeMs > 0,
              "size/mtime registered");
        // 分流
        CHECK(cm.vlaPathFor(vid1).contains("videos/V001.vla"),
              "router: case video → case vla");
        CHECK(cm.evidenceDirFor(vid1).contains("evidence/calibration/V001"),
              "router: case video → case evidence");
        CHECK(cm.vlaPathFor(vid3) == vid3 + ".vla",
              "router: non-case video → side .vla");
        // 框选记忆
        cm.setTimestampRoi(vid1, QRectF(0.1, 0.2, 0.3, 0.1));
        CHECK(cm.timestampRoiFor(vid1).isValid()
              && qAbs(cm.timestampRoiFor(vid1).x() - 0.1) < 1e-9,
              "timestampRoi set/get");
        CHECK(!cm.timestampRoiFor(vid3).isValid(),
              "non-case video roi invalid");
        // 徽标
        cm.updateCalibrationBadge(vid1, true, "OCR 三点 rate=1.000");
        CHECK(cm.videoById(id1)->hasCalibration, "badge updated");

        // 保存 → dirty 清
        CHECK(cm.saveCase(&err), "saveCase");
        CHECK(!cm.isDirty(), "saved → clean");

        // 移除 V002（不删数据）→ 高水位不回退
        CHECK(cm.removeVideo(id2, false, &err), "removeVideo");
        const QString id3 = cm.addVideo(vid3, &err);
        CHECK(id3 == "V003", "after remove: V003 (no reuse)");
        cm.saveCase(&err);
        cm.closeCase();
        CHECK(!cm.isOpen(), "closed");
        CHECK(!QFile::exists(cdir + "/case.json.lock"), "lock removed on close");

        // 重开：高水位持久化 + 锁不冲突（已正常关闭）
        {
            CaseManager cm2;
            QStringList warnings;
            CHECK(cm2.openCase(cdir, &err, &warnings), "reopen");
            CHECK(cm2.meta().videos.size() == 2, "reopen: 2 videos");
            CHECK(cm2.meta().nextVideoSeq == 4, "reopen: high-water persisted");
            CHECK(cm2.timestampRoiFor(vid1).isValid(),
                  "reopen: roi persisted");
            CHECK(cm2.videoById(id1)->hasCalibration,
                  "reopen: badge persisted");
            // 移除并删数据
            QFile vlaFile(cdir + "/videos/V001.vla");
            vlaFile.open(QIODevice::WriteOnly);
            vlaFile.write("{}");
            vlaFile.close();
            QDir().mkpath(cdir + "/evidence/calibration/V001");
            QFile ev(cdir + "/evidence/calibration/V001/at_1.png");
            ev.open(QIODevice::WriteOnly);
            ev.write("x");
            ev.close();
            CHECK(cm2.removeVideo(id1, true, &err), "removeVideo deleteData");
            CHECK(!QFile::exists(cdir + "/videos/V001.vla"),
                  "deleteData: vla removed");
            CHECK(!QDir(cdir + "/evidence/calibration/V001").exists(),
                  "deleteData: evidence removed");
            cm2.closeCase();
        }

        // 恢复注册表（直接写 QSettings，绕过私有 pushRecent）
        {
            QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
            s.setValue(QStringLiteral("case/recent"), savedRecent);
        }
    }

    // ---- 8. 哈希队列 + 完整性校验 + manifest ----
    {
        QTemporaryDir root;
        auto mkVideo = [&](const QString &name, const QByteArray &content) {
            QFile f(root.filePath(name));
            f.open(QIODevice::WriteOnly);
            f.write(content);
            f.close();
            return f.fileName();
        };
        const QString va = mkVideo("a.mp4", QByteArray(5000, 'a'));
        const QString vb = mkVideo("b.mp4", QByteArray(7000, 'b'));

        CaseManager cm;
        CaseMeta meta;
        meta.caseNo = CaseModel::generateCaseNo(1755014400000LL, "广州", "天河",
                                                root.path());
        meta.title = "哈希测试";
        meta.investigator = "i";
        meta.unit = "u";
        meta.incidentTimeMs = 1755014400000LL;
        meta.city = "广州";
        meta.district = "天河";
        QString err;
        CHECK(cm.createCase(root.path(), meta, &err), "hash: createCase");
        int progressCount = 0, finishedCount = 0;
        QObject::connect(&cm, &CaseManager::hashProgress,
                         [&](const QString &, int, int) { ++progressCount; });
        QObject::connect(&cm, &CaseManager::hashQueueFinished,
                         [&]() { ++finishedCount; });
        const QString idA = cm.addVideo(va, &err);
        const QString idB = cm.addVideo(vb, &err);
        CHECK(!idA.isEmpty() && !idB.isEmpty(), "hash: videos added");
        // 入案即排队 → 等队列排空
        QElapsedTimer t;
        t.start();
        while (finishedCount == 0 && t.elapsed() < 10000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK(finishedCount == 1, "hash: queue finished");
        CHECK(progressCount >= 2, "hash: per-file progress >= 2");
        const QString shaA = cm.videoById(idA)->sha256;
        CHECK(shaA.size() == 64, "hash: sha256 registered");
        CHECK(cm.videoById(idB)->sha256.size() == 64, "hash: sha256 B");

        // 全一致校验（快扫）
        QVector<CaseIntegrityItem> report;
        QObject::connect(&cm, &CaseManager::integrityReportReady,
                         [&](const QVector<CaseIntegrityItem> &items) {
                             report = items;
                         });
        cm.verifyIntegrity(false);
        t.restart();
        while (report.isEmpty() && t.elapsed() < 10000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK(report.size() == 2, "verify: 2 items");
        CHECK(report[0].status == 0 && report[1].status == 0,
              "verify: all ok");

        // 篡改内容但保持大小（快扫抓不到，全量重算抓得到）
        report.clear();
        {
            QFile f(va);
            f.open(QIODevice::WriteOnly);
            f.write(QByteArray(5000, 'z'));   // 同尺寸不同内容
            f.close();
        }
        cm.verifyIntegrity(false);   // 快扫：size/mtime 变了 → 会重算比对
        t.restart();
        while (report.isEmpty() && t.elapsed() < 10000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK(report.size() == 2, "verify after tamper: 2 items");
        bool foundChanged = false;
        for (const auto &it : report)
            if (it.label == idA && it.status == 1)
                foundChanged = true;
        CHECK(foundChanged, "verify: tampered file flagged changed");
        // 重登记后 sha 应已更新为新内容的哈希
        CHECK(cm.videoById(idA)->sha256 != shaA,
              "verify: sha re-registered after change");

        // 删除文件 → 缺失
        report.clear();
        QFile::remove(vb);
        cm.verifyIntegrity(false);
        t.restart();
        while (report.isEmpty() && t.elapsed() < 10000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        bool foundMissing = false;
        for (const auto &it : report)
            if (it.label == idB && it.status == 2)
                foundMissing = true;
        CHECK(foundMissing, "verify: deleted file flagged missing");

        // manifest 刷新
        cm.queueManifestRefresh();
        t.restart();
        while (!QFile::exists(cm.caseDir() + "/manifest.json")
               && t.elapsed() < 10000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK(QFile::exists(cm.caseDir() + "/manifest.json"),
              "manifest written");
        {
            QFile mf(cm.caseDir() + "/manifest.json");
            mf.open(QIODevice::ReadOnly);
            const auto doc = QJsonDocument::fromJson(mf.readAll());
            CHECK(doc.object()["magic"].toString() == "LumenArcManifest",
                  "manifest magic");
            const auto files = doc.object()["files"].toArray();
            bool hasCaseJson = false, hasLock = false;
            for (const auto &fv : files) {
                const QString p = fv.toObject()["path"].toString();
                if (p == "case.json") hasCaseJson = true;
                if (p.endsWith(".lock")) hasLock = true;
            }
            CHECK(hasCaseJson, "manifest covers case.json");
            CHECK(!hasLock, "manifest excludes lock file");
        }
        cm.closeCase();
    }

    fprintf(stderr, "case_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
