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
#include <QCryptographicHash>
#include <QThread>
#include <cstdio>
#include "domain/case_model.h"
#include "app/case_manager.h"

static int g_checks = 0, g_failures = 0;
#define CHECK(cond, msg) do { ++g_checks; if (!(cond)) { ++g_failures; \
    fprintf(stderr, "FAIL: %s:%d: %s\n", __FILE__, __LINE__, msg); } } while (0)

/// 最近案件注册表守卫：构造时快照并清空，析构时恢复（不污染开发机注册表）
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

    // ---- 7.5 框选记忆迁移（v1.3.0 M2 任务9：MainWindow 接线语义逐字复刻）----
    // savedTimestampRoi/saveTimestampRoi 双模式分流：入案→case.json 优先，
    // 注册表旧值只读复制一次入案（原值保留一版）；未入案→注册表照旧。
    {
        RecentGuard recentGuard;
        QTemporaryDir root;
        auto mkVideo = [&](const QString &name) {
            QFile f(root.filePath(name));
            f.open(QIODevice::WriteOnly);
            f.write("x", 1);
            f.close();
            return f.fileName();
        };
        const QString vidCase = mkVideo(QStringLiteral("迁移案内.mp4"));
        const QString vidInd  = mkVideo(QStringLiteral("迁移独立.mp4"));

        // MainWindow::readTimestampRoiRegistry 逐字复刻
        auto regRead = [](const QString &videoPath) {
            QSettings s("LumenArc", "LumenArc");
            const QByteArray key = "calibration/roi_"
                + QCryptographicHash::hash(videoPath.toUtf8(), QCryptographicHash::Md5).toHex();
            return s.value(QString::fromLatin1(key)).toRectF();
        };
        auto regWrite = [](const QString &videoPath, const QRectF &norm) {
            QSettings s("LumenArc", "LumenArc");
            const QByteArray key = "calibration/roi_"
                + QCryptographicHash::hash(videoPath.toUtf8(), QCryptographicHash::Md5).toHex();
            s.setValue(QString::fromLatin1(key), norm);
        };
        auto regClear = [](const QString &videoPath) {
            QSettings s("LumenArc", "LumenArc");
            const QByteArray key = "calibration/roi_"
                + QCryptographicHash::hash(videoPath.toUtf8(), QCryptographicHash::Md5).toHex();
            s.remove(QString::fromLatin1(key));
        };
        // MainWindow::savedTimestampRoi/saveTimestampRoi 逐字复刻
        CaseManager cm;
        auto savedRoi = [&](const QString &videoPath) -> QRectF {
            if (videoPath.isEmpty())
                return QRectF();
            if (cm.isCaseVideo(videoPath)) {
                QRectF roi = cm.timestampRoiFor(videoPath);
                if (roi.isValid())
                    return roi;
                roi = regRead(videoPath);
                if (roi.isValid())
                    cm.setTimestampRoi(videoPath, roi);
                return roi;
            }
            return regRead(videoPath);
        };
        auto saveRoi = [&](const QString &videoPath, const QRectF &norm) {
            if (videoPath.isEmpty() || !norm.isValid())
                return;
            if (cm.isCaseVideo(videoPath)) {
                cm.setTimestampRoi(videoPath, norm);
                return;
            }
            regWrite(videoPath, norm);
        };

        // 预清 + 建案 + 入案
        regClear(vidCase);
        regClear(vidInd);
        QString err;
        CaseMeta meta;
        meta.caseNo = QStringLiteral("20260813-迁移-a");
        meta.title = QStringLiteral("迁移");
        CHECK(cm.createCase(root.path(), meta, &err), "migrate: createCase");
        const QString idC = cm.addVideo(vidCase, &err);
        CHECK(!idC.isEmpty(), "migrate: addVideo");

        // ① 注册表旧值只读复制一次：案内空 → 返回注册表值 + 写入案 + 注册表保留
        const QRectF regRoi(0.11, 0.22, 0.33, 0.08);
        regWrite(vidCase, regRoi);
        const QRectF got1 = savedRoi(vidCase);
        CHECK(got1.isValid() && qAbs(got1.x() - 0.11) < 1e-9,
              "migrate: registry value returned");
        CHECK(cm.timestampRoiFor(vidCase).isValid()
              && qAbs(cm.timestampRoiFor(vidCase).x() - 0.11) < 1e-9,
              "migrate: copied into case.json");
        CHECK(regRead(vidCase).isValid()
              && qAbs(regRead(vidCase).x() - 0.11) < 1e-9,
              "migrate: registry original kept");

        // ② 案件内读写优先：案内值覆盖后不再读注册表
        const QRectF caseRoi(0.44, 0.55, 0.20, 0.06);
        saveRoi(vidCase, caseRoi);
        const QRectF got2 = savedRoi(vidCase);
        CHECK(qAbs(got2.x() - 0.44) < 1e-9, "migrate: case value wins");
        CHECK(qAbs(regRead(vidCase).x() - 0.11) < 1e-9,
              "migrate: save to case leaves registry untouched");

        // ③ 独立模式照旧：未入案视频只走注册表，案件不受影响
        const QRectF indRoi(0.66, 0.77, 0.10, 0.05);
        saveRoi(vidInd, indRoi);
        CHECK(regRead(vidInd).isValid() && qAbs(regRead(vidInd).x() - 0.66) < 1e-9,
              "migrate: independent writes registry");
        CHECK(savedRoi(vidInd).isValid() && qAbs(savedRoi(vidInd).x() - 0.66) < 1e-9,
              "migrate: independent reads registry");
        CHECK(!cm.isCaseVideo(vidInd) && !cm.timestampRoiFor(vidInd).isValid(),
              "migrate: independent leaves case untouched");

        // ④ 迁移后持久化：保存重开案内值仍在
        CHECK(cm.saveCase(&err), "migrate: saveCase");
        cm.closeCase();
        {
            CaseManager cm2;
            CHECK(cm2.openCase(QDir(root.path()).filePath("20260813-迁移-a-迁移"),
                               &err), "migrate: reopen");
            CHECK(cm2.timestampRoiFor(vidCase).isValid()
                  && qAbs(cm2.timestampRoiFor(vidCase).x() - 0.44) < 1e-9,
                  "migrate: persisted across reopen");
            cm2.closeCase();
        }
        // 清理注册表测试键
        regClear(vidCase);
        regClear(vidInd);
    }

    // ---- 8. 哈希队列 + 完整性校验 + manifest ----
    {
        RecentGuard recentGuard;
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
        const qint64 mtimeBefore = QFileInfo(va).lastModified().toMSecsSinceEpoch();
        {
            QFile f(va);
            f.open(QIODevice::WriteOnly);
            f.write(QByteArray(5000, 'z'));   // 同尺寸不同内容
            f.close();
        }
        // Windows 惰性 mtime 防御：轮询确认 mtime 变化（与生产代码同源
        // QFileInfo），确保快扫触发条件确定性成立（Defender/负载下曾抖动）
        t.restart();
        while (QFileInfo(va).lastModified().toMSecsSinceEpoch() == mtimeBefore
               && t.elapsed() < 5000)
            QThread::msleep(20);
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
        // Windows Defender 扫描锁防御：刚原子落盘的文件可能被短暂锁读，
        // 轮询至可打开且解析出 magic（不改变被验证内容）
        QJsonObject manifestObj;
        t.restart();
        while (t.elapsed() < 10000) {
            QFile mf(cm.caseDir() + "/manifest.json");
            if (mf.open(QIODevice::ReadOnly)) {
                const auto doc = QJsonDocument::fromJson(mf.readAll());
                if (doc.isObject() && !doc.object().isEmpty()) {
                    manifestObj = doc.object();
                    break;
                }
            }
            QThread::msleep(20);
        }
        {
            CHECK(manifestObj["magic"].toString() == "LumenArcManifest",
                  "manifest magic");
            const auto files = manifestObj["files"].toArray();
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

    // ---- 9. 前处理会话登记（v1.3.0 M2 任务8：addPreprocessSession）----
    {
        RecentGuard recentGuard;
        QTemporaryDir root;
        CaseManager cm;
        QString err;
        CaseMeta meta;
        meta.caseNo = QStringLiteral("20260813-前处理-a");
        meta.title = QStringLiteral("前处理");
        CHECK(cm.createCase(root.path(), meta, &err), "pps: createCase");

        // 会话目录结构：<案件>/preprocess/<ts>/{report.csv, merged.mp4,
        //   merged.mp4.lumencal.json, t01.mp4}
        const QString session = cm.caseDir()
            + QStringLiteral("/preprocess/20260813_143022");
        QDir().mkpath(session);
        auto mkFile = [](const QString &path, const QByteArray &content) {
            QFile f(path);
            f.open(QIODevice::WriteOnly);
            f.write(content);
            f.close();
        };
        mkFile(session + QStringLiteral("/report.csv"), "a,b\n1,2\n");
        mkFile(session + QStringLiteral("/merged.mp4"), QByteArray(4096, 'm'));
        mkFile(session + QStringLiteral("/t01.mp4"), QByteArray(2048, 't'));
        mkFile(session + QStringLiteral("/merged.mp4.lumencal.json"),
               "{\"segs\":[]}");

        // 登记：reportCsv 在证据子目录的场景（finalize 迁入 LumenArc_Evidence_）
        const QString evCsv = session
            + QStringLiteral("/LumenArc_Evidence_x/report.csv");
        QDir().mkpath(session + QStringLiteral("/LumenArc_Evidence_x"));
        mkFile(evCsv, "a,b\n3,4\n");
        CHECK(cm.addPreprocessSession(
                  session, evCsv,
                  {session + QStringLiteral("/merged.mp4"),
                   session + QStringLiteral("/t01.mp4"),
                   session + QStringLiteral("/missing.mp4")},   // 缺失产物跳过
                  {session + QStringLiteral("/merged.mp4.lumencal.json")},
                  &err), "pps: addPreprocessSession");
        CHECK(cm.meta().preprocessSessions.size() == 1, "pps: 1 session");
        const auto &p = cm.meta().preprocessSessions.first();
        CHECK(p.sessionDirRelPath == QStringLiteral("preprocess/20260813_143022"),
              "pps: sessionDirRelPath");
        CHECK(p.reportCsvRelPath.contains("LumenArc_Evidence_x/report.csv"),
              "pps: reportCsvRelPath (nested evidence dir)");
        CHECK(p.outputRefs.size() == 2, "pps: 2 outputs (missing skipped)");
        if (p.outputRefs.size() == 2) {
            CHECK(p.outputRefs[0].id == QStringLiteral("P001")
                  && p.outputRefs[1].id == QStringLiteral("P002"),
                  "pps: P### ids");
            CHECK(p.outputRefs[0].sizeBytes == 4096
                  && p.outputRefs[0].mtimeMs > 0
                  && p.outputRefs[0].sha256.isEmpty(),
                  "pps: output size/mtime registered, sha pending");
        }
        CHECK(p.sidecarRelPaths.size() == 1
              && p.sidecarRelPaths.first().contains(
                  "preprocess/20260813_143022/sidecars/merged.mp4.lumencal.json"),
              "pps: sidecar copied into sidecars/");
        CHECK(QFile::exists(session
                  + QStringLiteral("/sidecars/merged.mp4.lumencal.json")),
              "pps: sidecar copy exists");
        CHECK(QFile::exists(session
                  + QStringLiteral("/merged.mp4.lumencal.json")),
              "pps: original sidecar kept beside output");

        // 案件外会话目录拒绝
        CHECK(!cm.addPreprocessSession(root.path(), QString(), {}, {}, &err),
              "pps: outside-case session rejected");

        // 持久化
        CHECK(cm.saveCase(&err), "pps: saveCase");
        cm.closeCase();
        {
            CaseManager cm2;
            CHECK(cm2.openCase(QDir(root.path()).filePath(
                      "20260813-前处理-a-前处理"), &err), "pps: reopen");
            CHECK(cm2.meta().preprocessSessions.size() == 1
                  && cm2.meta().preprocessSessions.first().outputRefs.size() == 2,
                  "pps: session persisted across reopen");
            cm2.closeCase();
        }
    }

    // ---- 10. 重定位（v1.3.0 M2 任务10：relocateVideo）----
    {
        RecentGuard recentGuard;
        QTemporaryDir root;
        auto mkVideo = [&](const QString &name, const QByteArray &content) {
            QFile f(root.filePath(name));
            f.open(QIODevice::WriteOnly);
            f.write(content);
            f.close();
            return f.fileName();
        };
        const QString va = mkVideo("a.mp4", QByteArray(5000, 'a'));
        const QString vb = mkVideo("b.mp4", QByteArray(5000, 'b'));   // 同大小
        const QString vc = mkVideo("c.mp4", QByteArray(9000, 'c'));   // 不同大小

        CaseManager cm;
        QString err;
        CaseMeta meta;
        meta.caseNo = QStringLiteral("20260813-重定位-a");
        meta.title = QStringLiteral("重定位");
        CHECK(cm.createCase(root.path(), meta, &err), "reloc: createCase");
        int finishedCount = 0;
        QObject::connect(&cm, &CaseManager::hashQueueFinished,
                         [&]() { ++finishedCount; });
        const QString idA = cm.addVideo(va, &err);
        CHECK(!idA.isEmpty(), "reloc: addVideo");
        QElapsedTimer t;
        t.start();
        while (finishedCount == 0 && t.elapsed() < 10000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK(finishedCount == 1, "reloc: initial hash done");
        const QString shaA = cm.videoById(idA)->sha256;

        // ① 同大小新路径：直接接受，引用改变 + sha 作废重算
        bool mismatch = true;
        CHECK(cm.relocateVideo(idA, vb, &err, &mismatch, false),
              "reloc: same-size accepted");
        CHECK(!mismatch, "reloc: no mismatch on same size");
        CHECK(cm.videoById(idA)->originalPath.contains("b.mp4"),
              "reloc: reference updated (only)");
        CHECK(cm.videoById(idA)->sha256.isEmpty(), "reloc: sha invalidated");
        t.restart();
        while (finishedCount == 1 && t.elapsed() < 10000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 50);
        CHECK(finishedCount == 2, "reloc: rehash queued+done");
        CHECK(cm.videoById(idA)->sha256.size() == 64
              && cm.videoById(idA)->sha256 != shaA,
              "reloc: sha recomputed for new content");

        // ② 不同大小：默认拒绝（mismatch=true，引用不动）
        const QString pathBefore = cm.videoById(idA)->originalPath;
        CHECK(!cm.relocateVideo(idA, vc, &err, &mismatch, false),
              "reloc: size-mismatch rejected by default");
        CHECK(mismatch, "reloc: mismatch flag");
        CHECK(cm.videoById(idA)->originalPath == pathBefore,
              "reloc: reference untouched after reject");

        // ③ 不同大小 force（仍要采用）：接受 + extraFields 留档
        CHECK(cm.relocateVideo(idA, vc, &err, &mismatch, true),
              "reloc: force override accepted");
        CHECK(cm.videoById(idA)->originalPath.contains("c.mp4"),
              "reloc: override reference updated");
        CHECK(cm.meta().extraFields.contains(
                  QStringLiteral("relocateOverride/") + idA),
              "reloc: override archived in extraFields");

        // ④ 不存在文件：拒绝
        CHECK(!cm.relocateVideo(idA, root.filePath("nope.mp4"), &err,
                                nullptr, false),
              "reloc: missing file rejected");
        cm.closeCase();
    }

    // ---- 11. 案件属性更新 + 根目录设置（v1.3.0 M2 任务11）----
    {
        RecentGuard recentGuard;
        QTemporaryDir root;
        CaseManager cm;
        QString err;
        CaseMeta meta;
        meta.caseNo = QStringLiteral("20260813-属性-a");
        meta.title = QStringLiteral("原标题");
        meta.investigator = QStringLiteral("张三");
        meta.unit = QStringLiteral("某单位");
        CHECK(cm.createCase(root.path(), meta, &err), "props: createCase");

        // 可改字段更新
        CHECK(cm.updateCaseInfo(QStringLiteral("新标题"),
                                QStringLiteral("李四"),
                                QStringLiteral("新单位"),
                                QStringLiteral("xx路 1 号"),
                                QStringLiteral("备注文本"), &err),
              "props: updateCaseInfo");
        CHECK(cm.meta().title == QStringLiteral("新标题")
              && cm.meta().investigator == QStringLiteral("李四")
              && cm.meta().unit == QStringLiteral("新单位")
              && cm.meta().locationDetail == QStringLiteral("xx路 1 号")
              && cm.meta().description == QStringLiteral("备注文本"),
              "props: editable fields updated");
        CHECK(cm.meta().caseNo == QStringLiteral("20260813-属性-a"),
              "props: caseNo fixed");
        CHECK(cm.isDirty(), "props: dirty after update");

        // 名称空白拒绝
        CHECK(!cm.updateCaseInfo(QStringLiteral("  "), QString(), QString(),
                                 QString(), QString(), &err),
              "props: blank title rejected");

        // 持久化
        CHECK(cm.saveCase(&err), "props: saveCase");
        cm.closeCase();
        {
            CaseManager cm2;
            CHECK(cm2.openCase(QDir(root.path()).filePath(
                      "20260813-属性-a-原标题"), &err),
                  "props: reopen (dir name keeps original title)");
            CHECK(cm2.meta().title == QStringLiteral("新标题"),
                  "props: updated title persisted");
            cm2.closeCase();
        }

        // 根目录设置（QSettings 往返；恢复原有值避免污染真实配置）
        QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
        const QString savedRoot = s.value(QStringLiteral("case/rootDir"))
                                      .toString();
        CaseManager::setCaseRootDir(QStringLiteral("D:/__case_test_root__"));
        CHECK(CaseManager::caseRootDir()
                  == QStringLiteral("D:/__case_test_root__"),
              "rootdir: custom value");
        CaseManager::setCaseRootDir(QString());   // 恢复默认
        CHECK(CaseManager::caseRootDir() == CaseManager::defaultRootDir(),
              "rootdir: reset to default");
        if (!savedRoot.isEmpty())
            CaseManager::setCaseRootDir(savedRoot);
    }

    fprintf(stderr, "case_test: %d checks, %d failures\n", g_checks, g_failures);
    return g_failures ? 1 : 0;
}
