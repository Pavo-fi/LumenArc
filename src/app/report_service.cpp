#include "report_service.h"

#include "case_manager.h"
#include "videostatemanager.h"
#include "domain/report_fmt.h"
#include "infrastructure/tool_paths.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QRegularExpression>
#include <QVector>
#include <QPair>

/// v1.15.3：间接对时路「源监控较北京差值」——读产物 sidecar（.lumencal.json，
/// 源录像是 monitor 口径的分段墙钟），在首个锚点处算 参考北京墙钟 − 源墙钟。
/// 返回 false 表示无 sidecar/解析失败（调用方回退「≈0（依基准）」）。
static bool crosscamOsdDeltaMs(const QString &filePath,
                               qint64 refWallMs, qint64 targetStreamMs,
                               qint64 *outDeltaMs)
{
    *outDeltaMs = 0;
    QFile f(filePath + QStringLiteral(".lumencal.json"));
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QJsonObject root =
        QJsonDocument::fromJson(f.readAll()).object();
    f.close();
    const QJsonArray segs = root.value(QStringLiteral("segments")).toArray();
    if (segs.isEmpty())
        return false;
    for (const auto &sv : segs) {
        const QJsonObject s = sv.toObject();
        const double st = s.value(QStringLiteral("streamStartMs")).toDouble();
        const double en = s.value(QStringLiteral("streamEndMs")).toDouble();
        const double rate = s.value(QStringLiteral("rate")).toDouble();
        const double ws = s.value(QStringLiteral("wallStartMs")).toDouble();
        if (targetStreamMs >= st && targetStreamMs <= en) {
            *outDeltaMs = refWallMs -
                qRound64(ws + rate * (targetStreamMs - st));
            return true;   // positive = 源监控较北京慢
        }
    }
    return false;
}

/// report.csv 可信时长（ms 字符串 → 人读）
static QString fmtDurCsv(const QString &msStr)
{
    bool ok = false;
    const qint64 ms = msStr.toLongLong(&ok);
    if (!ok || ms <= 0)
        return msStr;
    const qint64 sec = ms / 1000;
    return QStringLiteral("%1 分 %2 秒").arg(sec / 60).arg(sec % 60);
}

/// ffprobe 取物理属性（同步，单文件超时 15s）
static void probeFile(const QString &path, ReportVideoRow &row)
{
    QProcess p;
    p.start(ToolPaths::findFfprobePath(),
            {QStringLiteral("-v"), QStringLiteral("error"),
             QStringLiteral("-print_format"), QStringLiteral("json"),
             QStringLiteral("-show_format"), QStringLiteral("-show_streams"),
             path});
    if (!p.waitForFinished(15000))
        return;
    const QJsonObject root = QJsonDocument::fromJson(p.readAllStandardOutput()).object();
    const QJsonObject fmt = root.value(QStringLiteral("format")).toObject();
    row.format = fmt.value(QStringLiteral("format_name")).toString();
    row.durationMs = qint64(fmt.value(QStringLiteral("duration")).toString().toDouble() * 1000.0);
    const QJsonArray streams = root.value(QStringLiteral("streams")).toArray();
    for (const auto &sv : streams) {
        const QJsonObject st = sv.toObject();
        if (st.value(QStringLiteral("codec_type")).toString() != QStringLiteral("video"))
            continue;
        row.codec = st.value(QStringLiteral("codec_name")).toString();
        row.width = st.value(QStringLiteral("width")).toInt();
        row.height = st.value(QStringLiteral("height")).toInt();
        const QString fr = st.value(QStringLiteral("avg_frame_rate")).toString();
        const int slash = fr.indexOf('/');
        if (slash > 0) {
            const double den = fr.mid(slash + 1).toDouble();
            row.fps = den > 0 ? fr.left(slash).toDouble() / den : 0.0;
        }
        break;
    }
}

/// MD5+SHA-256 单遍补算（knownSha 非空时只算 MD5）
static bool dualHash(const QString &path, const QString &knownSha,
                     QString *md5Out, QString *shaOut,
                     const std::function<bool(double)> &cb = {})
{
    QCryptographicHash md5(QCryptographicHash::Md5);
    QCryptographicHash sha(QCryptographicHash::Sha256);
    const bool needSha = knownSha.isEmpty();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return true;
    const qint64 total = f.size();
    qint64 done = 0;
    while (!f.atEnd()) {
        const QByteArray chunk = f.read(4 * 1024 * 1024);
        md5.addData(chunk);
        if (needSha)
            sha.addData(chunk);
        done += chunk.size();
        if (cb && !cb(total > 0 ? double(done) / total : 1.0))
            return false;   // 用户取消
    }
    *md5Out = QString::fromLatin1(md5.result().toHex());
    *shaOut = needSha ? QString::fromLatin1(sha.result().toHex())
                      : knownSha;
    return true;
}

ReportData ReportService::collect(CaseManager *cm, VideoStateManager *vsm,
                                  bool computeHashes)
{
    return collect(cm, vsm, computeHashes, {}, nullptr);
}

ReportData ReportService::collect(CaseManager *cm, VideoStateManager *vsm,
                                  bool computeHashes,
                                  const std::function<bool(const QString &, double)> &cb,
                                  bool *cancelled)
{
    if (cancelled) *cancelled = false;
    ReportData rd;
    if (!cm)
        return rd;
    const CaseMeta &meta = cm->meta();
    rd.caseNo = meta.caseNo;
    rd.title = meta.title;
    rd.investigator = meta.investigator;
    rd.unit = meta.unit;
    rd.incidentTimeMs = meta.incidentTimeMs;
    rd.city = meta.city;
    rd.district = meta.district;
    rd.locationDetail = meta.locationDetail;
    rd.description = meta.description;
    rd.extraFields = meta.extraFields;
    rd.generatedAtMs = QDateTime::currentMSecsSinceEpoch();
    rd.appVersion = QStringLiteral("V") + QString::fromLatin1(APP_VERSION);

    const QString caseDir = cm->caseDir();

    for (const CaseVideoRef *vp : CaseModel::allCaseRefs(meta)) {
        const CaseVideoRef &v = *vp;
        ReportVideoRow row;
        row.id = v.id;
        row.cameraLabel = v.cameraLabel.isEmpty() ? v.id : v.cameraLabel;
        row.shootDir = meta.extraFields.value(
            QStringLiteral("report/video/") + v.id + QStringLiteral("/direction"));
        row.extractMethod = meta.extraFields.value(
            QStringLiteral("report/video/") + v.id + QStringLiteral("/method"));
        row.storageMedium = meta.extraFields.value(
            QStringLiteral("report/video/") + v.id + QStringLiteral("/medium"));
        row.filePath = cm->effectivePathFor(v);
        row.fileName = QFileInfo(row.filePath).fileName();
        row.sizeBytes = v.sizeBytes;

        row.fileExists = QFile::exists(row.filePath);
        if (cb && !cb(QStringLiteral("探测 %1").arg(row.fileName), -1.0)) {
            if (cancelled) *cancelled = true;
            return rd;
        }
        if (row.fileExists)
            probeFile(row.filePath, row);
        if (computeHashes && row.fileExists) {
            const qint64 needBytes = v.sha256.isEmpty() ? row.sizeBytes * 1
                                                        : row.sizeBytes;
            Q_UNUSED(needBytes);
            const QString stage = QStringLiteral("哈希 %1").arg(row.fileName);
            const bool okc = dualHash(row.filePath, v.sha256, &row.md5, &row.sha256,
                [&](double f) { return cb ? cb(stage, f) : true; });
            if (!okc) {
                if (cancelled) *cancelled = true;
                return rd;
            }
        } else {
            row.sha256 = v.sha256;   // 已算过的沿用
        }

        // ---- 校时（.vla SSOT）----
        // v1.15.3：机位编号+名（C01 烟酒店东侧）供报告展示；参考路同口径
        auto camText = [&](const QString &fileId) -> QString {
            // 合并轨引用：锚点可能挂在「M_C02 烟酒店」（=M_+显示名）上
            if (fileId.startsWith(QStringLiteral("M_")))
                return fileId.mid(2);
            const QString gid = CaseModel::groupIdOf(meta, fileId);
            if (const CaseCameraGroup *g = gid.isEmpty()
                    ? nullptr : CaseModel::findGroup(meta, gid)) {
                if (!g->camNo.isEmpty())
                    return g->name.isEmpty()
                        ? g->camNo : g->camNo + QStringLiteral(" ") + g->name;
                if (!g->name.isEmpty())
                    return g->name;
            }
            return fileId;
        };
        VideoState st;
        if (vsm && vsm->restoreState(row.filePath, st) && st.calibration.isValid()) {
            const TimeCalibration &cal = st.calibration;
            row.hasCalib = true;
            row.calibWayText = reportfmt::calibWayText(cal.source);
            row.camNoText = camText(v.id);
            row.conf = cal.conf;
            row.anchors = cal.eventAnchors;
            row.anchorCount = cal.eventAnchors.size();
            const qint64 truth = cal.truthSet ? cal.truthOffsetMs : 0;
            row.wallStartMs = cal.wallMsOf(0) + truth;
            row.wallEndMs = cal.wallMsOf(row.durationMs > 0 ? row.durationMs : 0) + truth;
            row.formulaText = QStringLiteral("标准时间 = 录像时间 × %1 %2")
                .arg(cal.rateApplied ? QString::number(cal.rate, 'g', 9)
                                     : QStringLiteral("1.0"))
                .arg(cal.offsetMs != 0
                     ? QStringLiteral("+ 偏移 %1 秒").arg(cal.offsetMs / 1000.0, 0, 'f', 1)
                     : QString());
            // 取样点：显示时间 vs 校时后北京时间 → 时间差
            for (const auto &sp : cal.samples)
                if (sp.used && !sp.ocrSuspicious) {
                    row.osdSampleText = sp.rawText.isEmpty()
                        ? reportfmt::fmtWall(sp.wallMs) : sp.rawText;
                    const qint64 bj = cal.wallMsOf(sp.streamMs) + truth;
                    row.timeDiffText = reportfmt::fmtTimeDiff(bj - sp.wallMs);
                    break;
                }
            if (row.timeDiffText.isEmpty() && truth != 0)
                row.timeDiffText = reportfmt::fmtTimeDiff(truth);
            // v1.15.3 报告模板：时间基准 + 校准结果白话（讲清换算逻辑）
            // 直接对时（照片/手动）→ 基准=标准授时；时差=修正量；结果为换算公式
            auto stripDir = [](const QString &s) {   // 「慢 13 分 54 秒」→「13 分 54 秒」
                return s.startsWith(QStringLiteral("慢 "))
                           || s.startsWith(QStringLiteral("快 "))
                       ? s.mid(1).trimmed() : s;
            };
            if (cal.truthSet) {
                row.baseRefText = QStringLiteral("标准授时（北京时间）");
                const QString diff = reportfmt::fmtTimeDiff(truth);
                const QString corr = stripDir(diff);
                if (row.timeDiffText.isEmpty())
                    row.timeDiffText = diff;
                // v1.15.3：直接对时把「监控↔北京」同框读数对摆出来。OCR 原始
                // 识别可能被确认卡人工修正（实证：OCR 12:25:42 → 修正 12:25:47，
                // 偏移 834000 与 47 吻合）——报告显示修正后读数，原识别留档标注
                QString monRaw, bjRaw, corrMon;
                if (cal.truthSource == QStringLiteral("photo")) {
                    monRaw = reportfmt::flatTruthText(cal.truthMonitorText);
                    bjRaw = reportfmt::flatTruthText(cal.truthBeijingText);
                    QRegularExpression reHms(
                        QStringLiteral("(\\d{1,2}):(\\d{2}):(\\d{2})"));
                    const auto mk = reHms.match(cal.truthBeijingText);
                    if (mk.hasMatch()) {
                        const qint64 bjMs = (mk.captured(1).toInt() * 3600
                                             + mk.captured(2).toInt() * 60
                                             + mk.captured(3).toInt()) * 1000LL;
                        qint64 monMs = bjMs - truth;   // 监控 = 北京 − 偏移
                        if (monMs < 0)
                            monMs += 86400000LL;
                        const int s = int(monMs / 1000);
                        corrMon = QStringLiteral("%1:%2:%3")
                            .arg(s / 3600, 2, 10, QLatin1Char('0'))
                            .arg((s % 3600) / 60, 2, 10, QLatin1Char('0'))
                            .arg(s % 60, 2, 10, QLatin1Char('0'));
                        if (!corrMon.isEmpty())
                            row.osdSampleText = corrMon;   // 显示列用修正后读数
                    }
                }
                QString pair;
                if (!monRaw.isEmpty() && !bjRaw.isEmpty()) {
                    if (corrMon.isEmpty())
                        pair = QStringLiteral("，同框照片：监控显示「%1」↔ 北京时间「%2」")
                            .arg(monRaw, bjRaw);
                    else
                        pair = QStringLiteral(
                            "，同框照片：监控「%1」（OCR 留档）经确认卡人工修正为 %2 ↔ 北京时间「%3」")
                            .arg(monRaw, corrMon, bjRaw);
                }
                row.resultText = QStringLiteral(
                    "监控较北京时间%1%2；采用后：北京时间 = 监控显示时间 + %3")
                    .arg(diff, pair, corr);
            } else if (cal.source == TimeCalibration::Source::CrossCamEvent) {
                const QString ref = cal.eventAnchors.isEmpty()
                    ? QStringLiteral("—") : camText(cal.eventAnchors.first().refLaneId);
                row.baseRefText = ref;
                // 差值：锚点处源监控墙钟 vs 参考北京墙钟（sidecar 可取则取，
                // 否则用校时那一刻存盘的差值注记；再没有则诚实说「依基准」）
                QString deltaText;
                if (!cal.eventAnchors.isEmpty()) {
                    const auto &a0 = cal.eventAnchors.first();
                    qint64 dm;
                    if (crosscamOsdDeltaMs(row.filePath, a0.refWallMs,
                                           a0.targetStreamMs, &dm)) {
                        deltaText = reportfmt::fmtTimeDiff(dm);
                        if (qAbs(dm) < 500)
                            deltaText = QStringLiteral("一致（±0.5 秒内）");
                    }
                }
                if (deltaText.isEmpty() && !cal.calibNote.isEmpty())
                    deltaText = cal.calibNote;
                if (deltaText.isEmpty())
                    // v1.15.3 大白话：没可靠称出本体钟差，只证明时间轴已对齐
                    deltaText = QStringLiteral("未直接测定（仅锚点对齐，分析以墙钟为准）");
                row.timeDiffText = deltaText;
                row.resultText = QStringLiteral(
                    "以基准路「%1」为坐标：%2 个特征事件逐帧对齐；"
                    "源监控较北京时间 %3。%4")
                    .arg(ref).arg(row.anchorCount).arg(deltaText,
                    QStringLiteral("说明：锚点只证明两路画面内容同步、"
                                   "时间轴已对齐北京；本体烧录钟差未可靠测定。"));
            } else if (cal.source == TimeCalibration::Source::Inherited) {
                row.baseRefText = QStringLiteral("源校时");
                row.resultText = QStringLiteral("前处理产物，继承源校时时间轴");
            } else if (cal.dateKnown) {
                row.baseRefText = QStringLiteral("—");
                row.resultText = QStringLiteral(
                    "已建立监控时间轴（OCR 拟合，取样 %1 点），未与北京时间比对")
                    .arg(cal.samples.size());
            }
            // P-48：错读点 → 局限性
            int suspicious = 0;
            for (const auto &sp : cal.samples)
                if (sp.ocrSuspicious)
                    ++suspicious;
            if (suspicious > 0)
                rd.limitationNotes << QStringLiteral(
                    "%1 路存在 %2 处 OSD 疑似错读点（时间不可信），校时已自动剔除")
                    .arg(row.cameraLabel).arg(suspicious);
            // 标签 → 关键节点
            for (const ChartLabel &lb : st.labels) {
                ReportNodeRow n;
                n.wallMs = cal.wallMsOf(lb.timeMs) + truth;
                n.sourceLabel = row.cameraLabel;
                n.text = lb.text;
                rd.nodes << n;
            }
            // P-73 取证链
            if (!cal.eventAnchors.isEmpty()) {
                QHash<QString, QVector<eventcalib::EventAnchor>> byLane;
                QSet<QString> absoluteLaneIds;
                for (const CaseVideoRef *v2p : CaseModel::allCaseRefs(meta)) {
                    const CaseVideoRef &v2 = *v2p;
                    VideoState st2;
                    const QString p2 = cm->effectivePathFor(v2);
                    if (vsm->restoreState(p2, st2)) {
                        for (const auto &a : st2.calibration.eventAnchors)
                            byLane[v2.id] << a;
                        if (st2.calibration.isValid()
                            && st2.calibration.source != TimeCalibration::Source::CrossCamEvent)
                            absoluteLaneIds.insert(v2.id);
                    }
                }
                // v1.15.3：合并轨别名桥接——C03 的锚点挂在「M_C02 烟酒店」
                // （=M_+显示名）上，而 C02 的锚点在成员 P01/P02 自身 id 下：
                // 并入别名后接力链 C03→合并轨→源头→绝对锚 才能在报告完整展开
                for (const CaseCameraGroup &g2 : meta.cameraGroups) {
                    if (g2.memberIds.size() < 2 || g2.camNo.isEmpty())
                        continue;
                    const QString alias = QStringLiteral("M_")
                        + (g2.name.isEmpty()
                               ? g2.camNo
                               : g2.camNo + QStringLiteral(" ") + g2.name);
                    for (const QString &mid : g2.memberIds)
                        for (const auto &a : byLane.value(mid))
                            byLane[alias] << a;
                    for (const QString &mid : g2.memberIds)
                        if (absoluteLaneIds.contains(mid))
                            absoluteLaneIds.insert(alias);
                }
                const auto chain = eventcalib::expandChain(v.id, byLane, absoluteLaneIds);
                ReportChain rc;
                rc.laneLabel = row.camNoText.isEmpty()
                    ? row.cameraLabel : row.camNoText;
                qint64 cumTol = 0;
                for (const auto &hop : chain) {
                    cumTol += hop.anchor.toleranceMs;
                    if (!hop.absolute)
                        ++rc.eventHops;
                    const QString laneName = camText(hop.laneId);
                    rc.hopLines << (hop.absolute
                        ? QStringLiteral("%1：绝对校时锚（本路独立校时）")
                              .arg(laneName)
                        : QStringLiteral("%1 ← 参考「%2」（事件：%3，本跳容差 ±%4 ms）")
                              .arg(laneName, camText(hop.anchor.refLaneId),
                                   hop.anchor.eventName)
                              .arg(hop.anchor.toleranceMs));
                }
                rc.totalToleranceText = QStringLiteral("±%1 ms").arg(cumTol);
                rd.chains << rc;
            }
        } else {
            row.calibWayText = reportfmt::calibWayText(TimeCalibration::Source::None);
        }

        // 校准证据帧
        const QDir evDir(caseDir + QStringLiteral("/evidence/calibration/") + v.id);
        for (const auto &fi : evDir.entryInfoList({QStringLiteral("*.png"),
                                                   QStringLiteral("*.jpg")},
                                                  QDir::Files, QDir::Name))
            row.evidencePhotos << fi.absoluteFilePath();

        rd.videos << row;
    }

    // 节点按墙钟升序
    std::sort(rd.nodes.begin(), rd.nodes.end(),
              [](const ReportNodeRow &a, const ReportNodeRow &b) {
                  return a.wallMs < b.wallMs;
              });

    // ---- 前处理拼接记录（证据；拍板 2026-08-23）----
    const QDir ppRoot(caseDir + QStringLiteral("/preprocess"));
    for (const auto &sess : ppRoot.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                 QDir::Name)) {
        const QDir sdir(sess.absoluteFilePath());
        // 证据目录：LumenArc_Evidence_*
        QStringList evDirs = sdir.entryList({QStringLiteral("LumenArc_Evidence_*")},
                                            QDir::Dirs);
        if (evDirs.isEmpty())
            continue;
        const QString evDir = sess.absoluteFilePath() + '/' + evDirs.first();
        const QString csvPath = evDir + QStringLiteral("/report.csv");
        QFile csv(csvPath);
        if (!csv.open(QIODevice::ReadOnly | QIODevice::Text))
            continue;
        // 输出文件 → 记录（一会话可多产物，按输出分组）
        QHash<QString, int> recIdx;   // 输出文件名 → rd.concatRecords 下标
        QTextStream in(&csv);
        in.setEncoding(QStringConverter::Utf8);
        bool firstLine = true;
        while (!in.atEnd()) {
            const QString line = in.readLine();
            if (firstLine) { firstLine = false; continue; }   // 表头
            const QStringList f = line.split(',');
            if (f.size() < 23)
                continue;   // C1：列不齐整行跳过并留日志
            const QString outFile = QFileInfo(f[22]).fileName();
            if (outFile.isEmpty())
                continue;
            if (!recIdx.contains(outFile)) {
                ReportConcatRecord rec;
                rec.sessionTs = sess.fileName();
                rec.productFile = outFile;
                rec.evidenceDir = evDir;
                for (const CaseVideoRef *v3 : CaseModel::allCaseRefs(meta))
                    if (QFileInfo(cm->effectivePathFor(*v3)).fileName() == outFile
                        && cm->effectivePathFor(*v3).contains(sess.fileName())) {
                        rec.productId = v3->id;
                        break;
                    }
                rd.concatRecords << rec;
                recIdx[outFile] = rd.concatRecords.size() - 1;
            }
            ReportConcatRecord &rec = rd.concatRecords[recIdx[outFile]];
            rec.sourceRows << QVector<QString>{
                f[0], QFileInfo(f[1]).fileName(),
                fmtDurCsv(f[19]), f[21]};
        }
        // operations.log 关键行
        QFile log(evDir + QStringLiteral("/operations.log"));
        if (log.open(QIODevice::ReadOnly | QIODevice::Text)) {
            QTextStream lin(&log);
            lin.setEncoding(QStringConverter::Utf8);
            while (!lin.atEnd()) {
                const QString l = lin.readLine();
                if ((l.contains(QStringLiteral("素材统计"))
                     || l.contains(QStringLiteral("帧率不统一"))
                     || l.contains(QStringLiteral("重编码"))
                     || l.contains(QStringLiteral("拼接完成"))
                     || l.contains(QStringLiteral("失败")))
                    && rd.concatRecords.size() > 0) {
                    for (int idx : recIdx)
                        if (rd.concatRecords[idx].logHighlights.size() < 5
                            && !rd.concatRecords[idx].logHighlights.contains(l))
                            rd.concatRecords[idx].logHighlights << l.mid(12).trimmed();
                }
            }
        }
    }

    // 快照 / 导出片段 / 点位图
    const QDir snapDir(caseDir + QStringLiteral("/snapshots"));
    for (const auto &fi : snapDir.entryInfoList({QStringLiteral("*.png"),
                                                 QStringLiteral("*.jpg")},
                                                QDir::Files, QDir::Name))
        rd.snapshotPaths << fi.absoluteFilePath();
    const QDir expDir(caseDir + QStringLiteral("/exports"));
    for (const auto &fi : expDir.entryInfoList(QDir::Files, QDir::Name))
        rd.exportClips << fi.fileName();
    const QString sm = caseDir + QStringLiteral("/reports/assets/sitemap.png");
    if (QFile::exists(sm))
        rd.sitemapPng = sm;

    qInfo("report: collected %zu videos, %zu nodes, %zu chains, %zu snapshots",
          size_t(rd.videos.size()), size_t(rd.nodes.size()),
          size_t(rd.chains.size()), size_t(rd.snapshotPaths.size()));
    return rd;
}

#include "chartpanel.h"
#include "domain/roi_model.h"
#include "domain/timeline_model.h"

void ReportService::renderChartImages(CaseManager *cm, VideoStateManager *vsm,
                                      ReportData &rd)
{
    if (!cm || !vsm)
        return;
    const QString assetDir = cm->caseDir() + QStringLiteral("/reports/assets");
    QDir().mkpath(assetDir);
    for (ReportVideoRow &row : rd.videos) {
        VideoState st;
        if (!vsm->restoreState(row.filePath, st) || st.snapshot.isEmpty())
            continue;
        // 离屏图表（矢量重渲染，不经 grab——§14 定论）
        ChartPanel panel;
        RoiModel roi;
        TimelineModel tl;
        panel.setRegionModel(&roi);
        panel.setPolygonModel(&roi);
        panel.setTimelineModel(&tl);
        tl.setSnapshot(st.snapshot);
        panel.setCalibration(st.calibration);
        panel.setLabels(st.labels);
        const qint64 dur = row.durationMs > 0 ? row.durationMs
            : (st.snapshot.timestamps.isEmpty() ? 0 : st.snapshot.timestamps.last());
        if (dur > 0)
            panel.setDuration(dur);
        const QImage img = panel.renderToImage(QSize(1600, 420));
        if (img.isNull())
            continue;
        const QString out = assetDir + QStringLiteral("/chart_") + row.id
                            + QStringLiteral(".png");
        if (img.save(out)) {
            row.chartPng = out;
            qInfo() << "report: chart image" << row.id << img.size();
        }
    }
}
