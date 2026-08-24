/**
 * @file case_model.cpp
 * @brief case.json 读写迁移 + 案件编号/视频编号规则实现
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "case_model.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>

namespace {

/// 根层已知字段（F3：其余忽略并警告）
const QSet<QString> &knownRootKeys()
{
    static const QSet<QString> keys = {
        QStringLiteral("magic"), QStringLiteral("formatVersion"),
        QStringLiteral("caseNo"), QStringLiteral("title"),
        QStringLiteral("investigator"), QStringLiteral("unit"),
        QStringLiteral("incidentTimeMs"), QStringLiteral("city"),
        QStringLiteral("district"), QStringLiteral("locationDetail"),
        QStringLiteral("description"), QStringLiteral("createdMs"),
        QStringLiteral("modifiedMs"), QStringLiteral("lastVideoId"),
        QStringLiteral("nextVideoSeq"),
        QStringLiteral("videos"), QStringLiteral("preprocessSessions"),
        QStringLiteral("cameraGroups"), QStringLiteral("nextGroupSeq"),
        QStringLiteral("reports"), QStringLiteral("extraFields"),
    };
    return keys;
}

QJsonArray roiToJson(const QRectF &r)
{
    QJsonArray a;
    if (r.isValid()) {
        a.append(r.x()); a.append(r.y());
        a.append(r.width()); a.append(r.height());
    }
    return a;
}

QRectF roiFromJson(const QJsonArray &a)
{
    if (a.size() != 4)
        return QRectF();
    const QRectF r(a[0].toDouble(), a[1].toDouble(),
                   a[2].toDouble(), a[3].toDouble());
    return r.isValid() ? r : QRectF();
}

} // namespace

// ---------------------------------------------------------------------------
// CaseVideoRef
// ---------------------------------------------------------------------------
QJsonObject CaseVideoRef::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("id")] = id;
    o[QStringLiteral("originalPath")] = originalPath;
    o[QStringLiteral("vlaRelPath")] = vlaRelPath;
    o[QStringLiteral("sizeBytes")] = sizeBytes;
    o[QStringLiteral("mtimeMs")] = mtimeMs;
    if (!sha256.isEmpty())
        o[QStringLiteral("sha256")] = sha256;
    if (timestampRoi.isValid())
        o[QStringLiteral("timestampRoi")] = roiToJson(timestampRoi);
    if (hasCalibration)
        o[QStringLiteral("hasCalibration")] = hasCalibration;
    if (!calibrationSummary.isEmpty())
        o[QStringLiteral("calibrationSummary")] = calibrationSummary;
    if (!bundledRelPath.isEmpty())
        o[QStringLiteral("bundledRelPath")] = bundledRelPath;
    if (!cameraLabel.isEmpty())
        o[QStringLiteral("cameraLabel")] = cameraLabel;
    return o;
}

CaseVideoRef CaseVideoRef::fromJson(const QJsonObject &o)
{
    CaseVideoRef v;
    v.id = o[QStringLiteral("id")].toString();
    v.originalPath = o[QStringLiteral("originalPath")].toString();
    v.vlaRelPath = o[QStringLiteral("vlaRelPath")].toString();
    v.sizeBytes = o[QStringLiteral("sizeBytes")].toInteger();
    v.mtimeMs = o[QStringLiteral("mtimeMs")].toInteger();
    v.sha256 = o[QStringLiteral("sha256")].toString();
    v.timestampRoi = roiFromJson(o[QStringLiteral("timestampRoi")].toArray());
    v.hasCalibration = o[QStringLiteral("hasCalibration")].toBool(false);
    v.calibrationSummary = o[QStringLiteral("calibrationSummary")].toString();
    v.bundledRelPath = o[QStringLiteral("bundledRelPath")].toString();
    v.cameraLabel = o[QStringLiteral("cameraLabel")].toString();
    return v;
}

// ---------------------------------------------------------------------------
// CasePreprocessRef
// ---------------------------------------------------------------------------
QJsonObject CasePreprocessRef::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("sessionDirRelPath")] = sessionDirRelPath;
    o[QStringLiteral("reportCsvRelPath")] = reportCsvRelPath;
    QJsonArray outs;
    for (const auto &r : outputRefs)
        outs.append(r.toJson());
    o[QStringLiteral("outputRefs")] = outs;
    QJsonArray sc;
    for (const auto &s : sidecarRelPaths)
        sc.append(s);
    o[QStringLiteral("sidecarRelPaths")] = sc;
    return o;
}

CasePreprocessRef CasePreprocessRef::fromJson(const QJsonObject &o)
{
    CasePreprocessRef p;
    p.sessionDirRelPath = o[QStringLiteral("sessionDirRelPath")].toString();
    p.reportCsvRelPath = o[QStringLiteral("reportCsvRelPath")].toString();
    for (const auto &v : o[QStringLiteral("outputRefs")].toArray())
        p.outputRefs.append(CaseVideoRef::fromJson(v.toObject()));
    for (const auto &v : o[QStringLiteral("sidecarRelPaths")].toArray())
        p.sidecarRelPaths.append(v.toString());
    return p;
}

// ---------------------------------------------------------------------------
// CaseMeta
// ---------------------------------------------------------------------------
QJsonObject CaseCameraGroup::toJson() const
{
    return QJsonObject{
        {QStringLiteral("groupId"), groupId},
        {QStringLiteral("name"), name},
        {QStringLiteral("memberIds"), QJsonArray::fromStringList(memberIds)},
        {QStringLiteral("createdMs"), createdMs},
    };
}

CaseCameraGroup CaseCameraGroup::fromJson(const QJsonObject &o)
{
    CaseCameraGroup g;
    g.groupId = o.value(QStringLiteral("groupId")).toString();
    g.name = o.value(QStringLiteral("name")).toString();
    for (const auto &v : o.value(QStringLiteral("memberIds")).toArray())
        g.memberIds << v.toString();
    g.createdMs = qint64(o.value(QStringLiteral("createdMs")).toDouble());
    return g;
}

QJsonObject CaseMeta::toJson() const
{
    QJsonObject o;
    o[QStringLiteral("magic")] = QString::fromLatin1(CaseModel::kMagic);
    o[QStringLiteral("formatVersion")] = CaseModel::kFormatVersion;
    o[QStringLiteral("caseNo")] = caseNo;
    o[QStringLiteral("title")] = title;
    o[QStringLiteral("investigator")] = investigator;
    o[QStringLiteral("unit")] = unit;
    o[QStringLiteral("incidentTimeMs")] = incidentTimeMs;
    o[QStringLiteral("city")] = city;
    o[QStringLiteral("district")] = district;
    if (!locationDetail.isEmpty())
        o[QStringLiteral("locationDetail")] = locationDetail;
    if (!description.isEmpty())
        o[QStringLiteral("description")] = description;
    o[QStringLiteral("createdMs")] = createdMs;
    o[QStringLiteral("modifiedMs")] = modifiedMs;
    if (!lastVideoId.isEmpty())
        o[QStringLiteral("lastVideoId")] = lastVideoId;
    o[QStringLiteral("nextVideoSeq")] = nextVideoSeq;
    QJsonArray vs;
    for (const auto &v : videos)
        vs.append(v.toJson());
    o[QStringLiteral("videos")] = vs;
    QJsonArray ps;
    for (const auto &p : preprocessSessions)
        ps.append(p.toJson());
    o[QStringLiteral("preprocessSessions")] = ps;
    QJsonArray gs;
    for (const auto &g : cameraGroups)
        gs.append(g.toJson());
    if (!gs.isEmpty())
        o[QStringLiteral("cameraGroups")] = gs;
    o[QStringLiteral("nextGroupSeq")] = nextGroupSeq;
    QJsonArray rs;
    for (const auto &r : reports)
        rs.append(r);
    o[QStringLiteral("reports")] = rs;
    if (!extraFields.isEmpty()) {
        QJsonObject ex;
        for (auto it = extraFields.constBegin(); it != extraFields.constEnd(); ++it)
            ex[it.key()] = it.value();
        o[QStringLiteral("extraFields")] = ex;
    }
    return o;
}

// ---------------------------------------------------------------------------
// CaseModel namespace
// ---------------------------------------------------------------------------
namespace CaseModel {

const char kMagic[] = "LumenArcCase";
const char kFileName[] = "case.json";
const char kLockName[] = "case.json.lock";

bool load(const QString &caseDir, CaseMeta &out, QString *error,
          QStringList *warnings)
{
    const QString path = QDir(caseDir).filePath(QString::fromLatin1(kFileName));
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (error) *error = QStringLiteral("无法打开案件文件：%1").arg(path);
        return false;
    }
    QJsonParseError perr;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &perr);
    if (perr.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error) *error = QStringLiteral("案件文件损坏（JSON 解析失败）：%1").arg(path);
        return false;
    }
    const QJsonObject root = doc.object();
    if (root[QStringLiteral("magic")].toString() != QLatin1String(kMagic)) {
        if (error) *error = QStringLiteral("不是 LumenArc 案件文件（magic 不符）：%1").arg(path);
        return false;
    }
    const int ver = root[QStringLiteral("formatVersion")].toInt(-1);
    if (ver < 1) {
        if (error) *error = QStringLiteral("案件文件缺少版本号：%1").arg(path);
        return false;
    }
    if (ver > kFormatVersion) {
        if (error)
            *error = QStringLiteral("案件由更新版本创建（格式 v%2，当前支持到 v%1），"
                                    "请升级软件后打开")
                         .arg(kFormatVersion).arg(ver);
        return false;
    }
    // F3：未知根字段忽略并收集警告（只加不改，向前兼容）
    if (warnings) {
        for (auto it = root.constBegin(); it != root.constEnd(); ++it)
            if (!knownRootKeys().contains(it.key()))
                warnings->append(QStringLiteral("忽略未知字段：%1").arg(it.key()));
    }

    CaseMeta m;
    m.formatVersion = ver;
    m.caseNo = root[QStringLiteral("caseNo")].toString();
    m.title = root[QStringLiteral("title")].toString();
    m.investigator = root[QStringLiteral("investigator")].toString();
    m.unit = root[QStringLiteral("unit")].toString();
    m.incidentTimeMs = root[QStringLiteral("incidentTimeMs")].toInteger();
    m.city = root[QStringLiteral("city")].toString();
    m.district = root[QStringLiteral("district")].toString();
    m.locationDetail = root[QStringLiteral("locationDetail")].toString();
    m.description = root[QStringLiteral("description")].toString();
    m.createdMs = root[QStringLiteral("createdMs")].toInteger();
    m.modifiedMs = root[QStringLiteral("modifiedMs")].toInteger();
    m.lastVideoId = root[QStringLiteral("lastVideoId")].toString();
    for (const auto &v : root[QStringLiteral("videos")].toArray())
        m.videos.append(CaseVideoRef::fromJson(v.toObject()));
    // 高水位：缺省（旧文件）时取既有最大编号+1，保证不复用
    m.nextVideoSeq = root[QStringLiteral("nextVideoSeq")].toInt(0);
    {
        int maxN = 0;
        static const QRegularExpression idRe(QStringLiteral("^V(\\d+)$"));
        for (const auto &v : m.videos) {
            const auto match = idRe.match(v.id);
            if (match.hasMatch())
                maxN = qMax(maxN, match.captured(1).toInt());
        }
        if (m.nextVideoSeq <= maxN)
            m.nextVideoSeq = maxN + 1;
        if (m.nextVideoSeq < 1)
            m.nextVideoSeq = 1;
    }
    for (const auto &v : root[QStringLiteral("preprocessSessions")].toArray())
        m.preprocessSessions.append(CasePreprocessRef::fromJson(v.toObject()));
    for (const auto &v : root[QStringLiteral("cameraGroups")].toArray())
        m.cameraGroups.append(CaseCameraGroup::fromJson(v.toObject()));
    m.nextGroupSeq = root[QStringLiteral("nextGroupSeq")].toInt(0);
    {
        int maxN = 0;
        static const QRegularExpression idRe(QStringLiteral("^G(\d+)$"));
        for (const auto &g : m.cameraGroups) {
            const auto match = idRe.match(g.groupId);
            if (match.hasMatch())
                maxN = qMax(maxN, match.captured(1).toInt());
        }
        if (m.nextGroupSeq <= maxN)
            m.nextGroupSeq = maxN + 1;
        if (m.nextGroupSeq < 1)
            m.nextGroupSeq = 1;
    }
    for (const auto &v : root[QStringLiteral("reports")].toArray())
        m.reports.append(v.toString());
    const QJsonObject ex = root[QStringLiteral("extraFields")].toObject();
    for (auto it = ex.constBegin(); it != ex.constEnd(); ++it)
        m.extraFields[it.key()] = it.value().toString();
    out = m;
    return true;
}

bool save(const QString &caseDir, const CaseMeta &meta, QString *error)
{
    const QString path = QDir(caseDir).filePath(QString::fromLatin1(kFileName));
    QSaveFile f(path);   // 临时文件 + rename：崩溃不留半文件
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("无法写入案件文件：%1").arg(path);
        return false;
    }
    f.write(QJsonDocument(meta.toJson()).toJson(QJsonDocument::Indented));
    if (!f.commit()) {
        if (error) *error = QStringLiteral("案件文件保存失败：%1").arg(path);
        return false;
    }
    return true;
}

namespace {

QString cleanSegment(const QString &s)
{
    QString r = s;
    r.remove(QRegularExpression(QStringLiteral("\\s+")));
    // 目录名禁用字符保底清理（编号会进目录名）
    r.remove(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")));
    return r;
}

/// 序号 → 字母后缀：0..25 = a..z；26.. = aa, ab…（同日同区县超 26 件的保底）
QString letterSuffix(int idx)
{
    if (idx < 26)
        return QString(QChar(u'a' + idx));
    return QString(QChar(u'a' + (idx / 26 - 1))) + QChar(u'a' + (idx % 26));
}

} // namespace

QString generateCaseNo(qint64 incidentTimeMs, const QString &city,
                       const QString &district, const QString &caseRootDir)
{
    const QString date = QDateTime::fromMSecsSinceEpoch(incidentTimeMs)
                             .toString(QStringLiteral("yyyyMMdd"));
    const QString prefix = date + u'-' + cleanSegment(city)
                           + cleanSegment(district) + u'-';
    // 扫描根目录下同前缀目录，收集已占用字母（目录名 = 编号[-标题]，
    // 取前缀之后、首个 '-' 之前的字母段）
    QSet<QString> used;
    const QDir root(caseRootDir);
    for (const QFileInfo &fi :
         root.entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QString name = fi.fileName();
        if (!name.startsWith(prefix))
            continue;
        QString rest = name.mid(prefix.length());
        const int dash = rest.indexOf(u'-');
        if (dash >= 0)
            rest = rest.left(dash);
        if (!rest.isEmpty())
            used.insert(rest);
    }
    for (int i = 0;; ++i) {
        const QString letter = letterSuffix(i);
        if (!used.contains(letter))
            return prefix + letter;
    }
}

QString allocateVideoId(CaseMeta &meta)
{
    if (meta.nextVideoSeq < 1)
        meta.nextVideoSeq = 1;
    // v1.15.3 用户拍板：两位数字够用了（实际案件机位 << 100），超 99 自动扩位
    return QStringLiteral("V%1").arg(meta.nextVideoSeq++, 2, 10, QChar(u'0'));
}

const CaseVideoRef *findVideo(const CaseMeta &meta, const QString &id)
{
    for (const auto &v : meta.videos)
        if (v.id == id)
            return &v;
    return nullptr;
}

CaseVideoRef *findVideo(CaseMeta &meta, const QString &id)
{
    for (auto &v : meta.videos)
        if (v.id == id)
            return &v;
    return nullptr;
}

const CaseVideoRef *findRef(const CaseMeta &meta, const QString &id)
{
    if (const auto *v = findVideo(meta, id))
        return v;
    for (const auto &s : meta.preprocessSessions)
        for (const auto &o : s.outputRefs)
            if (o.id == id)
                return &o;
    return nullptr;
}

CaseVideoRef *findRef(CaseMeta &meta, const QString &id)
{
    if (auto *v = findVideo(meta, id))
        return v;
    for (auto &s : meta.preprocessSessions)
        for (auto &o : s.outputRefs)
            if (o.id == id)
                return &o;
    return nullptr;
}

} // namespace CaseModel

// ---------------------------------------------------------------------------
// 机位组辅助（2026-08-24 拍板：组为组织轴心）
// ---------------------------------------------------------------------------
namespace CaseModel {

const CaseCameraGroup *findGroup(const CaseMeta &meta, const QString &groupId)
{
    for (const auto &g : meta.cameraGroups)
        if (g.groupId == groupId)
            return &g;
    return nullptr;
}

CaseCameraGroup *findGroup(CaseMeta &meta, const QString &groupId)
{
    return const_cast<CaseCameraGroup *>(
        findGroup(const_cast<const CaseMeta &>(meta), groupId));
}

QString groupIdOf(const CaseMeta &meta, const QString &refId)
{
    for (const auto &g : meta.cameraGroups)
        if (g.memberIds.contains(refId))
            return g.groupId;
    return QString();
}

QString groupDisplayName(const CaseCameraGroup &g)
{
    if (!g.name.isEmpty())
        return g.name;
    if (!g.memberIds.isEmpty())
        return g.memberIds.first();
    return g.groupId;
}

bool migrateCameraGroups(CaseMeta &meta)
{
    bool changed = false;
    QSet<QString> grouped;
    for (const auto &g : meta.cameraGroups)
        for (const QString &id : g.memberIds)
            grouped.insert(id);

    // 未归组引用按 cameraLabel 聚类
    QHash<QString, QStringList> byLabel;   // label → ids（保序）
    QStringList labelOrder;
    QStringList unlabeled;
    for (const CaseVideoRef *v : allCaseRefs(meta)) {
        if (grouped.contains(v->id))
            continue;
        if (!v->cameraLabel.isEmpty()) {
            if (!byLabel.contains(v->cameraLabel))
                labelOrder << v->cameraLabel;
            byLabel[v->cameraLabel] << v->id;
        } else {
            unlabeled << v->id;
        }
    }
    auto mkGroup = [&](const QString &name, const QStringList &ids) {
        CaseCameraGroup g;
        g.groupId = QStringLiteral("G%1").arg(meta.nextGroupSeq++, 3, 10,
                                              QLatin1Char('0'));
        g.name = name;
        g.memberIds = ids;
        g.createdMs = QDateTime::currentMSecsSinceEpoch();
        meta.cameraGroups.append(g);
        changed = true;
    };
    // 同标签并组；标签与既有组名相同 → 并入既有组而非新建
    for (const QString &label : labelOrder) {
        CaseCameraGroup *existing = nullptr;
        for (auto &g : meta.cameraGroups)
            if (!g.name.isEmpty() && g.name == label) {
                existing = &g;
                break;
            }
        if (existing) {
            for (const QString &id : byLabel[label])
                existing->memberIds << id;
            changed = true;
        } else {
            mkGroup(label, byLabel[label]);
        }
    }
    for (const QString &id : unlabeled)
        mkGroup(QString(), {id});
    return changed;
}

} // namespace CaseModel
