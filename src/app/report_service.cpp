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
static void dualHash(const QString &path, const QString &knownSha,
                     QString *md5Out, QString *shaOut)
{
    QCryptographicHash md5(QCryptographicHash::Md5);
    QCryptographicHash sha(QCryptographicHash::Sha256);
    const bool needSha = knownSha.isEmpty();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return;
    while (!f.atEnd()) {
        const QByteArray chunk = f.read(4 * 1024 * 1024);
        md5.addData(chunk);
        if (needSha)
            sha.addData(chunk);
    }
    *md5Out = QString::fromLatin1(md5.result().toHex());
    *shaOut = needSha ? QString::fromLatin1(sha.result().toHex())
                      : knownSha;
}

ReportData ReportService::collect(CaseManager *cm, VideoStateManager *vsm)
{
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

    for (const CaseVideoRef &v : meta.videos) {
        ReportVideoRow row;
        row.id = v.id;
        row.cameraLabel = v.cameraLabel.isEmpty() ? v.id : v.cameraLabel;
        row.filePath = cm->effectivePathFor(v);
        row.fileName = QFileInfo(row.filePath).fileName();
        row.sizeBytes = v.sizeBytes;

        probeFile(row.filePath, row);
        dualHash(row.filePath, v.sha256, &row.md5, &row.sha256);

        // ---- 校时（.vla SSOT）----
        VideoState st;
        if (vsm && vsm->restoreState(row.filePath, st) && st.calibration.isValid()) {
            const TimeCalibration &cal = st.calibration;
            row.hasCalib = true;
            row.calibWayText = reportfmt::calibWayText(cal.source);
            row.conf = cal.conf;
            row.anchors = cal.eventAnchors;
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
                for (const CaseVideoRef &v2 : meta.videos) {
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
                const auto chain = eventcalib::expandChain(v.id, byLane, absoluteLaneIds);
                ReportChain rc;
                rc.laneLabel = row.cameraLabel;
                qint64 cumTol = 0;
                for (const auto &hop : chain) {
                    cumTol += hop.anchor.toleranceMs;
                    QString laneName = hop.laneId;
                    for (const CaseVideoRef &v3 : meta.videos)
                        if (v3.id == hop.laneId && !v3.cameraLabel.isEmpty()) {
                            laneName = v3.cameraLabel;
                            break;
                        }
                    rc.hopLines << (hop.absolute
                        ? QStringLiteral("%1：绝对校时锚（本路独立校时）")
                              .arg(laneName)
                        : QStringLiteral("%1 ← 参考「%2」（事件：%3，本跳容差 ±%4 ms）")
                              .arg(laneName, hop.anchor.refLaneId,
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
