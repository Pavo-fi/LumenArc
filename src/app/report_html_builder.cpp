#include "report_html_builder.h"

#include "domain/report_fmt.h"
#include "domain/timeline_events.h"

#include <QBuffer>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace {

const QString kPlaceholder = QStringLiteral("/*__LA_DATA__*/");
const QString kTemplateRes = QStringLiteral(":/report_template.html");

/// 待处理图片：源路径 + 是否需要缩略图 + 输出文件名
struct Asset {
    QString srcPath;
    bool    isThumb = false;
    QString outName;   ///< 多文件模式下的文件名（不含 assets/ 前缀）
};

/// 图片引用分配器：单文件模式发 "#n" 令牌，多文件模式发 "assets/<名>"。
/// assets 为空指针 = 纯 JSON 模式（引用退回文件名，不产生 images 数组）。
QString refImage(QVector<Asset> *assets, bool single, QVector<QString> *usedNames,
                 const QString &path, bool thumb)
{
    if (path.isEmpty())
        return QString();
    const QFileInfo fi(path);
    if (assets == nullptr)
        return fi.fileName();

    QString name = fi.completeBaseName();
    if (thumb)
        name += QStringLiteral("__thumb.jpg");
    else
        name += QLatin1Char('.') + (fi.suffix().isEmpty() ? QStringLiteral("png")
                                                          : fi.suffix().toLower());
    int n = 1;
    const QString base = name;
    while (usedNames->contains(name)) {
        const QFileInfo b(base);
        name = b.completeBaseName() + QStringLiteral("_") + QString::number(++n)
               + QLatin1Char('.') + b.suffix();
    }
    usedNames->append(name);

    Asset a;
    a.srcPath = fi.absoluteFilePath();
    a.isThumb = thumb;
    a.outName = name;
    assets->append(a);

    if (single)
        return QLatin1Char('#') + QString::number(assets->size() - 1);
    return QStringLiteral("assets/") + name;
}

QString fmtRel(qint64 sec)
{
    if (sec < 0)
        sec = 0;
    if (sec < 60)
        return QStringLiteral("%1秒").arg(sec);
    if (sec < 3600)
        return QStringLiteral("%1分%2秒").arg(sec / 60).arg(sec % 60, 2, 10, QChar('0'));
    return QStringLiteral("%1小时%2分%3秒")
        .arg(sec / 3600).arg((sec % 3600) / 60).arg(sec % 60, 2, 10, QChar('0'));
}

QString timeText(qint64 ms)
{
    if (ms <= 0)
        return QString();
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("HH:mm:ss"));
}

/// 未校时案件：标签只有流内毫秒，按「时:分:秒」直接格式化
/// （不能走 fromMSecsSinceEpoch——那会被时区整体平移，显示成 08:26 这种假时刻）
QString clockText(qint64 ms)
{
    const qint64 s = qMax<qint64>(0, ms / 1000);
    return QStringLiteral("%1:%2:%3")
        .arg(s / 3600, 2, 10, QChar('0'))
        .arg((s % 3600) / 60, 2, 10, QChar('0'))
        .arg(s % 60, 2, 10, QChar('0'));
}

/// 事件/速览表用的时刻文案：校时案件 = 北京时间；未校时 = 流内位置
QString stampText(qint64 ms, bool absTime)
{
    return absTime ? timeText(ms) : clockText(ms);
}

QString dateText(qint64 ms)
{
    if (ms <= 0)
        return QString();
    return QDateTime::fromMSecsSinceEpoch(ms).toString(QStringLiteral("yyyy-MM-dd"));
}

QJsonObject statCard(const QString &value, const QString &key, bool hi)
{
    QJsonObject o;
    o[QStringLiteral("v")] = value;
    o[QStringLiteral("k")] = key;
    if (hi)
        o[QStringLiteral("hi")] = true;
    return o;
}

QJsonArray eventSummary(const QVector<TimelineEventItem> &events, qint64 totalSec,
                        int maxRows, bool absTime)
{
    QJsonArray arr;
    if (events.size() < 2)
        return arr;
    const TimelineEventItem &base = events.first();
    const int n = qMin<int>(events.size() - 1, qMax(1, maxRows));
    for (int i = 1; i <= n; ++i) {
        const TimelineEventItem &e = events.at(i);
        const qint64 sec = (e.wallMs - base.wallMs) / 1000;
        QJsonObject row;
        row[QStringLiteral("k")] = (base.title.isEmpty() ? QStringLiteral("基准")
                                                         : base.title)
                                   + QStringLiteral(" → ")
                                   + (e.title.isEmpty() ? QStringLiteral("（未命名）")
                                                        : e.title);
        row[QStringLiteral("aText")] = stampText(base.wallMs, absTime);
        row[QStringLiteral("bText")] = stampText(e.wallMs, absTime);
        row[QStringLiteral("relText")] = fmtRel(sec);
        row[QStringLiteral("sec")] = static_cast<double>(sec);
        row[QStringLiteral("pct")] = totalSec > 0
            ? qRound(sec * 1000.0 / totalSec) / 10.0 : 0.0;
        arr << row;
    }
    return arr;
}

/// 报告主体（章节数据）
QJsonObject buildBody(const ReportData &rd, const HtmlBuildOptions &opt,
                      const TimelineEvents &tl, QVector<Asset> *assets, bool single,
                      QVector<QString> *usedNames)
{
    QJsonObject root;

    // ---- meta ----
    QJsonObject meta;
    meta[QStringLiteral("docTitle")] = QStringLiteral("火灾视频分析报告");
    {
        QStringList head;
        if (!rd.caseNo.isEmpty())
            head << rd.caseNo;
        if (!rd.title.isEmpty())
            head << rd.title;
        meta[QStringLiteral("kicker")] = head.join(QStringLiteral(" · "));
    }
    meta[QStringLiteral("caseNo")] = rd.caseNo;
    meta[QStringLiteral("title")] = rd.title;
    meta[QStringLiteral("investigator")] = rd.investigator;
    meta[QStringLiteral("unit")] = rd.unit;
    meta[QStringLiteral("city")] = rd.city;
    meta[QStringLiteral("district")] = rd.district;
    meta[QStringLiteral("locationDetail")] = rd.locationDetail;
    meta[QStringLiteral("description")] = rd.description;
    meta[QStringLiteral("incidentTimeMs")] = static_cast<double>(rd.incidentTimeMs);
    meta[QStringLiteral("generatedAtMs")] = static_cast<double>(rd.generatedAtMs);
    meta[QStringLiteral("appVersion")] = rd.appVersion;
    QJsonObject extra;
    for (auto it = rd.extraFields.constBegin(); it != rd.extraFields.constEnd(); ++it)
        extra[it.key()] = it.value();
    meta[QStringLiteral("extraFields")] = extra;
    meta[QStringLiteral("singleFile")] = single;
    meta[QStringLiteral("footerNote")] = QStringLiteral(
        "本页由追光者 LumenArc 自动生成，仅供展示；截图时标与原始检材一致，"
        "证据以原始检材为准。");
    root[QStringLiteral("meta")] = meta;

    // ---- stats（4 张统计卡，时间轴为空时不摆节点卡）----
    QJsonArray stats;
    if (!rd.caseNo.isEmpty())
        stats << statCard(rd.caseNo, QStringLiteral("案件编号"), false);
    if (!tl.events.isEmpty()) {
        stats << statCard(QStringLiteral("%1 个").arg(tl.events.size()),
                          QStringLiteral("关键节点"), true);
        if (tl.totalSec > 0)
            stats << statCard(fmtRel(tl.totalSec), QStringLiteral("观测总时长"), false);
    }
    stats << statCard(QStringLiteral("%1 路").arg(rd.videos.size()),
                      QStringLiteral("检材视频"), false);
    root[QStringLiteral("stats")] = stats;

    // ---- timeline ----
    // 墙钟是否可信：未校时的案件里标签只有流内毫秒，直接当 epoch 会显示成 1970 年，
    // 所以低于 2000-01-01 一律按「流内时刻」呈现（不给假日期）
    const bool absTime = tl.t0Ms >= 946684800000LL;
    QJsonObject t;
    t[QStringLiteral("t0Ms")] = static_cast<double>(tl.t0Ms);
    t[QStringLiteral("totalSec")] = static_cast<double>(tl.totalSec);
    t[QStringLiteral("phaseLabel")] = QStringLiteral("阶段");
    t[QStringLiteral("absTime")] = absTime;
    t[QStringLiteral("timeBasisText")] = absTime
        ? QStringLiteral("北京时间")
        : QStringLiteral("录像流内时刻（本案未校时）");
    if (!tl.events.isEmpty()) {
        const QString d = absTime ? dateText(tl.t0Ms) : QString();
        t[QStringLiteral("timeRangeText")] =
            (d.isEmpty() ? QString() : d + QLatin1Char(' '))
            + stampText(tl.events.first().wallMs, absTime) + QStringLiteral(" — ")
            + stampText(tl.events.last().wallMs, absTime)
            + (absTime ? QString() : QStringLiteral("（流内）"));
    } else {
        t[QStringLiteral("timeRangeText")] = QString();
    }
    QJsonArray phases;
    for (const TimelinePhaseItem &p : tl.phases) {
        QJsonObject o;
        o[QStringLiteral("id")] = p.id;
        o[QStringLiteral("name")] = p.name;
        o[QStringLiteral("short")] = p.shortName;
        o[QStringLiteral("color")] = p.color;
        o[QStringLiteral("rangeText")] = stampText(p.firstMs, absTime)
            + QStringLiteral(" — ") + stampText(p.lastMs, absTime);
        o[QStringLiteral("n")] = p.n;
        phases << o;
    }
    t[QStringLiteral("phases")] = phases;

    QJsonArray events;
    for (int i = 0; i < tl.events.size(); ++i) {
        const TimelineEventItem &e = tl.events.at(i);
        const qint64 sec = (e.wallMs - tl.t0Ms) / 1000;
        QJsonObject o;
        o[QStringLiteral("seq")] = i + 1;
        o[QStringLiteral("wallMs")] = static_cast<double>(e.wallMs);
        o[QStringLiteral("timeText")] = stampText(e.wallMs, absTime);
        o[QStringLiteral("dateText")] = absTime ? dateText(e.wallMs) : QString();
        o[QStringLiteral("relText")] = fmtRel(sec);
        o[QStringLiteral("relSec")] = static_cast<double>(sec);
        o[QStringLiteral("pct")] = tl.totalSec > 0
            ? qRound(sec * 1000.0 / tl.totalSec) / 10.0 : 0.0;
        o[QStringLiteral("title")] = e.title;
        o[QStringLiteral("note")] = e.note;
        o[QStringLiteral("phase")] = tl.phases.isEmpty()
            ? QString() : tl.phases.at(tl.eventPhase.value(i, 0)).id;
        o[QStringLiteral("sourceLabel")] = e.sourceLabel;
        o[QStringLiteral("source")] = e.source;
        o[QStringLiteral("orig")] = e.origName;
        if (e.imagePath.isEmpty()) {
            o[QStringLiteral("thumb")] = QJsonValue::Null;
            o[QStringLiteral("full")] = QJsonValue::Null;
        } else {
            o[QStringLiteral("thumb")] =
                refImage(assets, single, usedNames, e.imagePath, true);
            o[QStringLiteral("full")] =
                refImage(assets, single, usedNames, e.imagePath, false);
        }
        QStringList meta;
        if (!e.sourceLabel.isEmpty())
            meta << e.sourceLabel;
        meta << (e.source == QStringLiteral("snapshot") ? QStringLiteral("关键帧快照")
                                                        : QStringLiteral("书签"));
        o[QStringLiteral("metaText")] = meta.join(QStringLiteral(" · "));
        events << o;
    }
    t[QStringLiteral("events")] = events;
    t[QStringLiteral("summary")] = eventSummary(tl.events, tl.totalSec, opt.summaryMaxRows,
                                                absTime);
    root[QStringLiteral("timeline")] = t;

    // ---- videos ----
    QJsonArray videos;
    for (const ReportVideoRow &v : rd.videos) {
        QJsonObject o;
        o[QStringLiteral("id")] = v.id;
        o[QStringLiteral("cameraLabel")] = v.cameraLabel;
        o[QStringLiteral("camNoText")] = v.camNoText;
        o[QStringLiteral("shootDir")] = v.shootDir;
        o[QStringLiteral("extractMethod")] = v.extractMethod;
        o[QStringLiteral("storageMedium")] = v.storageMedium;
        o[QStringLiteral("fileName")] = v.fileName;
        o[QStringLiteral("filePath")] = v.filePath;
        o[QStringLiteral("fileExists")] = v.fileExists;
        o[QStringLiteral("format")] = v.format;
        o[QStringLiteral("codec")] = v.codec;
        o[QStringLiteral("sizeBytes")] = static_cast<double>(v.sizeBytes);
        o[QStringLiteral("sizeText")] = reportfmt::fmtSizeMB(v.sizeBytes);
        o[QStringLiteral("width")] = v.width;
        o[QStringLiteral("height")] = v.height;
        o[QStringLiteral("fps")] = v.fps;
        o[QStringLiteral("durationMs")] = static_cast<double>(v.durationMs);
        o[QStringLiteral("durationText")] = reportfmt::fmtDuration(v.durationMs);
        o[QStringLiteral("hasCalib")] = v.hasCalib;
        o[QStringLiteral("calibWayText")] = v.calibWayText;
        o[QStringLiteral("conf")] = v.conf;
        o[QStringLiteral("wallStartMs")] = static_cast<double>(v.wallStartMs);
        o[QStringLiteral("wallEndMs")] = static_cast<double>(v.wallEndMs);
        o[QStringLiteral("wallStartText")] = reportfmt::fmtWall(v.wallStartMs);
        o[QStringLiteral("wallEndText")] = reportfmt::fmtWall(v.wallEndMs);
        o[QStringLiteral("osdSampleText")] = v.osdSampleText;
        o[QStringLiteral("formulaText")] = v.formulaText;
        o[QStringLiteral("timeDiffText")] = v.timeDiffText;
        o[QStringLiteral("baseRefText")] = v.baseRefText;
        o[QStringLiteral("resultText")] = v.resultText;
        o[QStringLiteral("anchorCount")] = v.anchorCount;
        o[QStringLiteral("md5")] = v.md5;
        o[QStringLiteral("sha256")] = v.sha256;
        o[QStringLiteral("chartImg")] = v.chartPng.isEmpty()
            ? QJsonValue(QJsonValue::Null)
            : QJsonValue(refImage(assets, single, usedNames, v.chartPng, false));
        QJsonArray shots;
        for (const QString &p : v.evidencePhotos)
            shots << refImage(assets, single, usedNames, p, false);
        o[QStringLiteral("evidencePhotos")] = shots;
        QJsonArray anchors;
        for (const eventcalib::EventAnchor &a : v.anchors)
            anchors << a.toJson();
        o[QStringLiteral("anchors")] = anchors;
        videos << o;
    }
    root[QStringLiteral("videos")] = videos;

    // ---- nodes / chains / concat ----
    QJsonArray nodes;
    for (const ReportNodeRow &n : rd.nodes) {
        QJsonObject o;
        o[QStringLiteral("wallMs")] = static_cast<double>(n.wallMs);
        o[QStringLiteral("timeText")] = timeText(n.wallMs);
        o[QStringLiteral("dateText")] = dateText(n.wallMs);
        o[QStringLiteral("wallText")] = reportfmt::fmtWall(n.wallMs);
        o[QStringLiteral("sourceLabel")] = n.sourceLabel;
        o[QStringLiteral("text")] = n.text;
        nodes << o;
    }
    root[QStringLiteral("nodes")] = nodes;

    QJsonArray chains;
    for (const ReportChain &c : rd.chains) {
        QJsonObject o;
        o[QStringLiteral("laneLabel")] = c.laneLabel;
        o[QStringLiteral("hopLines")] = QJsonArray::fromStringList(c.hopLines);
        o[QStringLiteral("totalToleranceText")] = c.totalToleranceText;
        o[QStringLiteral("eventHops")] = c.eventHops;
        chains << o;
    }
    root[QStringLiteral("chains")] = chains;

    QJsonArray concats;
    for (const ReportConcatRecord &r : rd.concatRecords) {
        QJsonObject o;
        o[QStringLiteral("sessionTs")] = r.sessionTs;
        o[QStringLiteral("productFile")] = r.productFile;
        o[QStringLiteral("productId")] = r.productId;
        o[QStringLiteral("evidenceDir")] = r.evidenceDir;
        o[QStringLiteral("logHighlights")] = QJsonArray::fromStringList(r.logHighlights);
        QJsonArray rows;
        for (const auto &row : r.sourceRows) {
            QJsonArray cells;
            for (const QString &c : row)
                cells << c;
            rows << cells;
        }
        o[QStringLiteral("sourceRows")] = rows;
        concats << o;
    }
    root[QStringLiteral("concatRecords")] = concats;

    root[QStringLiteral("limitationNotes")] =
        QJsonArray::fromStringList(rd.limitationNotes);
    root[QStringLiteral("exportClips")] =
        QJsonArray::fromStringList(rd.exportClips);
    root[QStringLiteral("sitemapImg")] = rd.sitemapPng.isEmpty()
        ? QJsonValue(QJsonValue::Null)
        : QJsonValue(refImage(assets, single, usedNames, rd.sitemapPng, false));

    QJsonArray snaps;
    for (const QString &p : rd.snapshotPaths) {
        const QFileInfo fi(p);
        const qint64 ms = snapshotWallMsFromName(fi.fileName());
        QJsonObject o;
        o[QStringLiteral("name")] = fi.fileName();
        o[QStringLiteral("path")] = p;
        o[QStringLiteral("timeText")] = timeText(ms);
        o[QStringLiteral("wallText")] = reportfmt::fmtWall(ms);
        snaps << o;
    }
    root[QStringLiteral("snapshotTable")] = snaps;

    return root;
}

/// 从 ReportData 装配时间轴事件（标签 + 案内快照）
TimelineEvents collectTimeline(const ReportData &rd, const HtmlBuildOptions &opt)
{
    QVector<TimelineSnapshotRef> snaps;
    for (const QString &p : rd.snapshotPaths) {
        TimelineSnapshotRef s;
        s.path = p;
        s.wallMs = snapshotWallMsFromName(QFileInfo(p).fileName());
        snaps << s;
    }
    return buildTimelineEvents(rd.nodes, snaps, opt.mergeWindowMs, opt.phaseGapMs,
                               opt.maxPhases, opt.phaseSeeds);
}

QString mimeOf(const QString &path)
{
    const QString s = QFileInfo(path).suffix().toLower();
    if (s == QLatin1String("jpg") || s == QLatin1String("jpeg"))
        return QStringLiteral("image/jpeg");
    if (s == QLatin1String("webp"))
        return QStringLiteral("image/webp");
    if (s == QLatin1String("bmp"))
        return QStringLiteral("image/bmp");
    return QStringLiteral("image/png");
}

/// 读图 → data URI。缩略图走缩放 + JPEG；整图直接内嵌原字节（不重编码，保真）。
QString dataUri(const Asset &a, const HtmlBuildOptions &opt)
{
    if (a.isThumb) {
        QImage img(a.srcPath);
        if (!img.isNull()) {
            if (img.width() > opt.thumbMaxEdge || img.height() > opt.thumbMaxEdge)
                img = img.scaled(opt.thumbMaxEdge, opt.thumbMaxEdge,
                                 Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QByteArray out;
            QBuffer buf(&out);
            buf.open(QIODevice::WriteOnly);
            if (img.save(&buf, "JPEG", opt.thumbQuality)) {
                buf.close();
                return QStringLiteral("data:image/jpeg;base64,")
                       + QString::fromLatin1(out.toBase64());
            }
        }
        // 读不了/存不了 → 落回原图内嵌，绝不出现破图占位
    }
    QFile f(a.srcPath);
    if (!f.open(QIODevice::ReadOnly))
        return QString();
    const QByteArray raw = f.readAll();
    f.close();
    return QStringLiteral("data:") + mimeOf(a.srcPath) + QStringLiteral(";base64,")
           + QString::fromLatin1(raw.toBase64());
}

} // namespace

QByteArray ReportHtmlBuilder::buildJson(const ReportData &rd, const HtmlBuildOptions &opt)
{
    const TimelineEvents tl = collectTimeline(rd, opt);
    QJsonObject root = buildBody(rd, opt, tl, nullptr, false, nullptr);
    root[QStringLiteral("images")] = QJsonValue(QJsonValue::Null);
    QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    // 内嵌 <script type="application/json"> 里不能出现字面 '<'
    json.replace('<', "\\u003c");
    return json;
}

QString ReportHtmlBuilder::build(const ReportData &rd, const QString &outPath,
                                 const HtmlBuildOptions &opt)
{
    const TimelineEvents tl = collectTimeline(rd, opt);

    QVector<Asset> assets;
    QVector<QString> usedNames;
    QJsonObject root = buildBody(rd, opt, tl, &assets, opt.singleFile, &usedNames);

    const QFileInfo outInfo(outPath);
    const QString dir = outInfo.absolutePath();
    if (!QDir().mkpath(dir))
        return QStringLiteral("无法创建输出目录：%1").arg(dir);

    if (opt.singleFile) {
        QJsonArray imgs;
        for (const Asset &a : assets) {
            const QString uri = dataUri(a, opt);
            if (uri.isEmpty())
                return QStringLiteral("图片读取失败：%1").arg(a.srcPath);
            imgs << uri;
        }
        root[QStringLiteral("images")] = imgs;
    } else {
        const QString adir = dir + QStringLiteral("/assets");
        if (!QDir().mkpath(adir))
            return QStringLiteral("无法创建资源目录：%1").arg(adir);
        for (const Asset &a : assets) {
            const QString dst = adir + QLatin1Char('/') + a.outName;
            QFile::remove(dst);
            if (a.isThumb) {
                QImage img(a.srcPath);
                if (!img.isNull()) {
                    if (img.width() > opt.thumbMaxEdge || img.height() > opt.thumbMaxEdge)
                        img = img.scaled(opt.thumbMaxEdge, opt.thumbMaxEdge,
                                         Qt::KeepAspectRatio, Qt::SmoothTransformation);
                    if (img.save(dst, "JPEG", opt.thumbQuality))
                        continue;
                }
                // 缩略图失败就整图落盘：浏览器按内容识别，不出现破图
            }
            if (!QFile::copy(a.srcPath, dst))
                return QStringLiteral("图片写出失败：%1").arg(dst);
        }
        root[QStringLiteral("images")] = QJsonValue(QJsonValue::Null);
    }

    QFile tpl(kTemplateRes);
    if (!tpl.open(QIODevice::ReadOnly))
        return QStringLiteral("模板资源缺失：%1").arg(kTemplateRes);
    QString html = QString::fromUtf8(tpl.readAll());
    tpl.close();
    if (!html.contains(kPlaceholder))
        return QStringLiteral("模板缺少占位符：%1").arg(kPlaceholder);

    QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Compact);
    json.replace('<', "\\u003c");   // 防 </script> 截断内嵌 JSON
    html.replace(kPlaceholder, QString::fromUtf8(json));

    QFile out(outPath);
    if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return QStringLiteral("无法写出文件：%1").arg(outPath);
    out.write(html.toUtf8());
    out.close();
    return QString();
}
