/**
 * @file case_manager.cpp
 * @brief CaseManager 实现：生命周期/锁/视频登记/路径分流/框选记忆/徽标
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 */
#include "case_manager.h"

#include <windows.h>   // OpenProcess（锁残留检测，2026-08）

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStorageInfo>
#include <QThread>

namespace {
const char kSettingsRecent[] = "case/recent";
constexpr int kRecentMax = 10;
const char kManifestName[] = "manifest.json";

QString normPath(const QString &p)
{
    return QDir::cleanPath(QDir(p).absolutePath());
}

/// SHA-256 流式计算（1MB 分块，abort 可中断；失败/中止返回 false）
bool hashFileSha256(const QString &path, QString *outHex,
                    const std::atomic<bool> &abort)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    QCryptographicHash h(QCryptographicHash::Sha256);
    while (!f.atEnd()) {
        if (abort.load())
            return false;
        const QByteArray chunk = f.read(1 << 20);
        if (chunk.isEmpty())
            return false;
        h.addData(chunk);
    }
    *outHex = QString::fromLatin1(h.result().toHex());
    return true;
}
} // namespace

CaseManager::CaseManager(QObject *parent)
    : QObject(parent)
{
    qRegisterMetaType<CaseIntegrityItem>("CaseIntegrityItem");
    qRegisterMetaType<QVector<CaseIntegrityItem>>("QVector<CaseIntegrityItem>");
}

CaseManager::~CaseManager()
{
    cancelExport();
    cancelHashes();
}

// ---------------------------------------------------------------------------
// 生命周期
// ---------------------------------------------------------------------------
QString CaseManager::caseDirName(const CaseMeta &meta)
{
    // 目录名 = 编号[-标题]；标题清理目录禁用字符
    QString t = meta.title;
    t.remove(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")));
    t = t.trimmed();
    return t.isEmpty() ? meta.caseNo
                       : meta.caseNo + QStringLiteral("-") + t;
}

bool CaseManager::createCase(const QString &parentDir, const CaseMeta &meta,
                             QString *error)
{
    if (m_open) {
        if (error) *error = QStringLiteral("已有打开的案件");
        return false;
    }
    if (meta.caseNo.isEmpty() || meta.title.isEmpty()) {
        if (error) *error = QStringLiteral("案件编号/名称为空");
        return false;
    }
    const QString dir = QDir(parentDir).filePath(caseDirName(meta));
    if (QDir(dir).exists()) {
        if (error) *error = QStringLiteral("案件目录已存在：%1").arg(dir);
        return false;
    }
    QDir d;
    if (!d.mkpath(dir)) {
        if (error) *error = QStringLiteral("无法创建案件目录：%1").arg(dir);
        return false;
    }
    for (const char *sub : {"videos", "evidence", "preprocess", "reports",
                            "snapshots"})
        d.mkpath(dir + u'/' + QString::fromLatin1(sub));

    m_caseDir = dir;
    m_meta = meta;
    m_meta.createdMs = m_meta.modifiedMs = QDateTime::currentMSecsSinceEpoch();
    m_meta.formatVersion = CaseModel::kFormatVersion;
    if (!CaseModel::save(dir, m_meta, error)) {
        d.rmdir(dir);   // 空目录回滚
        m_caseDir.clear();
        m_meta = CaseMeta();
        return false;
    }
    if (!createLock(error)) {
        m_caseDir.clear();
        m_meta = CaseMeta();
        return false;
    }
    m_open = true;
    m_dirty = false;
    m_hashAbort.store(false);
    pushRecent(dir);
    emit caseOpened(dir);
    return true;
}

bool CaseManager::openCase(const QString &dir, QString *error,
                           QStringList *warnings, bool *lockConflict,
                           bool force)
{
    if (lockConflict) *lockConflict = false;
    if (m_open) {
        if (error) *error = QStringLiteral("已有打开的案件");
        return false;
    }
    const QString lockPath = QDir(dir).filePath(
        QString::fromLatin1(CaseModel::kLockName));
    if (QFile::exists(lockPath) && !force) {
        // 残留锁自动清理（2026-08 人工反馈：每次打开都弹「强制打开」）。
        // 锁内含 pid：持有者进程已不存在 → 上次未正常关闭的残留，
        // 自动接管不打扰；进程仍在 → 真双开冲突才交由 UI 决策。
        if (isLockStale(lockPath)) {
            QFile::remove(lockPath);
        } else {
            if (lockConflict) *lockConflict = true;
            if (error)
                *error = QStringLiteral("案件已被其他实例打开");
            return false;
        }
    }
    CaseMeta meta;
    if (!CaseModel::load(dir, meta, error, warnings))
        return false;
    if (!createLock(error))
        return false;
    m_caseDir = normPath(dir);
    m_meta = meta;
    m_open = true;
    m_dirty = false;
    // v1.7.1 迁移：历史数据跨会话 P### 重复 ID 重排为全局唯一
    // （用户实测：给后一个会话产物改编号，错误地落到首个产物）
    {
        int seq = 1;
        bool renamed = false;
        for (auto &s : m_meta.preprocessSessions) {
            for (auto &o : s.outputRefs) {
                const QString want = QStringLiteral("P%1")
                    .arg(seq++, 3, 10, QLatin1Char('0'));
                if (o.id != want) {
                    o.id = want;
                    renamed = true;
                }
            }
        }
        if (renamed)
            m_dirty = true;   // 迁移后随下次保存落盘
    }
    m_hashAbort.store(false);   // 复位取消标志（上次关案可能置位）
    pushRecent(m_caseDir);
    emit caseOpened(m_caseDir);
    queueMissingHashes();       // Q-9/M3-Q1：开案自动补算缺失/已变更
    return true;
}

bool CaseManager::saveCase(QString *error)
{
    if (!m_open) {
        if (error) *error = QStringLiteral("没有打开的案件");
        return false;
    }
    m_meta.modifiedMs = QDateTime::currentMSecsSinceEpoch();
    if (!CaseModel::save(m_caseDir, m_meta, error))
        return false;
    if (m_dirty) {
        m_dirty = false;
        emit caseDirtyChanged(false);
    }
    emit caseSaved();
    return true;
}

void CaseManager::closeCase()
{
    if (!m_open)
        return;
    cancelHashes();
    removeLock();
    const bool wasDirty = m_dirty;
    m_open = false;
    m_dirty = false;
    m_caseDir.clear();
    m_meta = CaseMeta();
    if (wasDirty)
        emit caseDirtyChanged(false);
    emit caseClosed();
}

// ---------------------------------------------------------------------------
// 锁（防双实例写冲突；残留 = 上次未正常关闭，由 UI 提示强制打开）
// ---------------------------------------------------------------------------
bool CaseManager::createLock(QString *error)
{
    QFile f(QDir(m_caseDir).filePath(QString::fromLatin1(CaseModel::kLockName)));
    if (!f.open(QIODevice::WriteOnly)) {
        if (error) *error = QStringLiteral("无法创建案件锁文件");
        return false;
    }
    f.write(QStringLiteral("pid=%1 time=%2")
                .arg(QCoreApplication::applicationPid())
                .arg(QDateTime::currentMSecsSinceEpoch())
                .toUtf8());
    return true;
}

bool CaseManager::isLockStale(const QString &lockPath)
{
    // 锁内容 pid=<PID>：进程已不存在 → 残留锁。
    // 解析失败/无 pid 也视为残留（旧格式或半写锁）。
    QFile f(lockPath);
    if (!f.open(QIODevice::ReadOnly))
        return false;
    const QByteArray data = f.readAll();
    qint64 pid = -1;
    const int p = data.indexOf("pid=");
    if (p >= 0) {
        const QByteArray rest = data.mid(p + 4);
        const int sp = rest.indexOf(' ');
        pid = (sp > 0 ? rest.left(sp) : rest).toLongLong();
    }
    if (pid <= 0)
        return true;
    HANDLE h = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE,
                           DWORD(pid));
    if (!h)
        return true;   // 进程不存在或无权访问 → 残留
    CloseHandle(h);
    return false;
}

void CaseManager::removeLock()
{
    QFile::remove(QDir(m_caseDir).filePath(
        QString::fromLatin1(CaseModel::kLockName)));
}

// ---------------------------------------------------------------------------
// 最近案件
// ---------------------------------------------------------------------------
QStringList CaseManager::recentCases() const
{
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    return s.value(QString::fromLatin1(kSettingsRecent)).toStringList();
}

QString CaseManager::defaultRootDir()
{
    return QCoreApplication::applicationDirPath() + QStringLiteral("/cases");
}

QString CaseManager::caseRootDir()
{
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    const QString v = s.value(QStringLiteral("case/rootDir")).toString();
    return v.isEmpty() ? defaultRootDir() : v;
}

void CaseManager::setCaseRootDir(const QString &dir)
{
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    if (dir.isEmpty() || dir == defaultRootDir())
        s.remove(QStringLiteral("case/rootDir"));
    else
        s.setValue(QStringLiteral("case/rootDir"), dir);
}

bool CaseManager::updateCaseInfo(const QString &title,
                                 const QString &investigator,
                                 const QString &unit,
                                 const QString &locationDetail,
                                 const QString &description, QString *error)
{
    if (!m_open) {
        if (error) *error = QStringLiteral("没有打开的案件");
        return false;
    }
    if (title.trimmed().isEmpty()) {
        if (error) *error = QStringLiteral("案件名称为必填项");
        return false;
    }
    // 注意：title 变更不改案件目录名（目录名=编号+创建时标题，
    // 编号/目录创建后固定；重命名目录会破坏根目录编号扫描与外部引用）
    m_meta.title = title.trimmed();
    m_meta.investigator = investigator.trimmed();
    m_meta.unit = unit.trimmed();
    m_meta.locationDetail = locationDetail.trimmed();
    m_meta.description = description;
    m_meta.modifiedMs = QDateTime::currentMSecsSinceEpoch();
    setModified();
    return true;
}

void CaseManager::pushRecent(const QString &dir)
{
    QStringList list = recentCases();
    list.removeAll(dir);
    list.prepend(dir);
    while (list.size() > kRecentMax)
        list.removeLast();
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    s.setValue(QString::fromLatin1(kSettingsRecent), list);
}

void CaseManager::removeRecent(const QString &dir)
{
    QStringList list = recentCases();
    list.removeAll(dir);
    QSettings s(QStringLiteral("LumenArc"), QStringLiteral("LumenArc"));
    s.setValue(QString::fromLatin1(kSettingsRecent), list);
}

// ---------------------------------------------------------------------------
// 视频登记
// ---------------------------------------------------------------------------
bool CaseManager::setReportExtra(const QString &key, const QString &value,
                                 QString *error)
{
    if (m_meta.caseNo.isEmpty()) {
        if (error) *error = QStringLiteral("无打开的案件");
        return false;
    }
    m_meta.extraFields[QStringLiteral("report/") + key] = value;
    m_meta.modifiedMs = QDateTime::currentMSecsSinceEpoch();
    return saveCase(error);
}

QString CaseManager::addVideo(const QString &path, QString *error)
{
    if (!m_open) {
        if (error) *error = QStringLiteral("没有打开的案件");
        return QString();
    }
    const QString norm = normPath(path);
    if (const auto *existing = videoByPath(norm)) {
        if (error)
            *error = QStringLiteral("该视频已在案件中（%1）").arg(existing->id);
        return QString();
    }
    const QFileInfo fi(norm);
    if (!fi.exists() || !fi.isFile()) {
        if (error) *error = QStringLiteral("文件不存在：%1").arg(norm);
        return QString();
    }
    CaseVideoRef v;
    v.id = CaseModel::allocateVideoId(m_meta);
    v.originalPath = norm;
    v.vlaRelPath = QStringLiteral("videos/%1.vla").arg(v.id);
    v.sizeBytes = fi.size();
    v.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
    m_meta.videos.append(v);
    setModified();
    emit videoAdded(v.id);
    queueVideoHash(v.id);   // Q-9：入案即排队算指纹
    return v.id;
}

bool CaseManager::removeVideo(const QString &id, bool deleteData,
                              QString *error)
{
    for (int i = 0; i < m_meta.videos.size(); ++i) {
        if (m_meta.videos[i].id != id)
            continue;
        if (deleteData) {
            const QDir d(m_caseDir);
            QFile::remove(d.filePath(m_meta.videos[i].vlaRelPath));
            QDir(d.filePath(QStringLiteral("evidence/calibration/") + id))
                .removeRecursively();
        }
        m_meta.videos.remove(i);
        if (m_meta.lastVideoId == id)
            m_meta.lastVideoId.clear();
        setModified();
        emit videoRemoved(id);
        return true;
    }
    if (error) *error = QStringLiteral("案件中没有该文件 %1").arg(id);
    return false;
}

bool CaseManager::removePreprocessSession(int sessIdx, bool deleteFiles,
                                          QString *error)
{
    if (!m_open) {
        if (error) *error = QStringLiteral("没有打开的案件");
        return false;
    }
    if (sessIdx < 0 || sessIdx >= m_meta.preprocessSessions.size()) {
        if (error) *error = QStringLiteral("会话索引越界");
        return false;
    }
    const auto &p = m_meta.preprocessSessions[sessIdx];
    if (deleteFiles) {
        const QDir caseDir(m_caseDir);
        QDir(caseDir.absoluteFilePath(p.sessionDirRelPath))
            .removeRecursively();   // 会话目录（含输出/sidecar）
    }
    m_meta.preprocessSessions.remove(sessIdx);
    setModified();
    return true;
}

bool CaseManager::removePreprocessOutput(int sessIdx, int outIdx,
                                         bool deleteFile, QString *error)
{
    if (!m_open) {
        if (error) *error = QStringLiteral("没有打开的案件");
        return false;
    }
    if (sessIdx < 0 || sessIdx >= m_meta.preprocessSessions.size()
        || outIdx < 0
        || outIdx >= m_meta.preprocessSessions[sessIdx].outputRefs.size()) {
        if (error) *error = QStringLiteral("输出索引越界");
        return false;
    }
    auto &p = m_meta.preprocessSessions[sessIdx];
    if (deleteFile)
        QFile::remove(p.outputRefs[outIdx].originalPath);
    p.outputRefs.remove(outIdx);
    setModified();
    return true;
}

bool CaseManager::removeReport(int idx, bool deleteFile, QString *error)
{
    if (!m_open) {
        if (error) *error = QStringLiteral("没有打开的案件");
        return false;
    }
    if (idx < 0 || idx >= m_meta.reports.size()) {
        if (error) *error = QStringLiteral("报告索引越界");
        return false;
    }
    if (deleteFile)
        QFile::remove(QDir(m_caseDir).absoluteFilePath(m_meta.reports[idx]));
    m_meta.reports.remove(idx);
    setModified();
    return true;
}

bool CaseManager::removeSidecar(int sessIdx, const QString &absPath,
                                bool deleteFile, QString *error)
{
    if (!m_open) {
        if (error) *error = QStringLiteral("没有打开的案件");
        return false;
    }
    if (sessIdx < 0 || sessIdx >= m_meta.preprocessSessions.size()) {
        if (error) *error = QStringLiteral("会话索引越界");
        return false;
    }
    auto &p = m_meta.preprocessSessions[sessIdx];
    const QString rel = QDir(m_caseDir).relativeFilePath(
        QDir::cleanPath(absPath));
    for (int i = 0; i < p.sidecarRelPaths.size(); ++i) {
        if (p.sidecarRelPaths[i] != rel)
            continue;
        if (deleteFile)
            QFile::remove(absPath);
        p.sidecarRelPaths.remove(i);
        setModified();
        return true;
    }
    if (error) *error = QStringLiteral("会话中未找到该 sidecar");
    return false;
}

int CaseManager::pruneMissingFiles(QString *error)
{
    // 外部删除自动清理登记（2026-08 人工反馈：资源管理器删文件后列表应
    // 清除对应条目）。只移除「文件确实不存在」的引用，不删任何现存文件；
    // 完整包接收端（原路径缺失但包内副本在场）不受影响。
    if (!m_open) {
        if (error) *error = QStringLiteral("没有打开的案件");
        return 0;
    }
    int removed = 0;
    for (int i = m_meta.videos.size() - 1; i >= 0; --i) {
        const auto &v = m_meta.videos[i];
        if (!QFileInfo::exists(v.originalPath)
            && !QFileInfo::exists(effectivePathFor(v))) {
            m_meta.videos.remove(i);
            if (m_meta.lastVideoId == v.id)
                m_meta.lastVideoId.clear();
            ++removed;
        }
    }
    const QDir caseDir(m_caseDir);
    for (int i = m_meta.preprocessSessions.size() - 1; i >= 0; --i) {
        auto &p = m_meta.preprocessSessions[i];
        const QString sdir = caseDir.absoluteFilePath(p.sessionDirRelPath);
        if (!QDir(sdir).exists()) {
            m_meta.preprocessSessions.remove(i);
            ++removed;
            continue;
        }
        for (int j = p.outputRefs.size() - 1; j >= 0; --j) {
            if (!QFileInfo::exists(p.outputRefs[j].originalPath)) {
                p.outputRefs.remove(j);
                ++removed;
            }
        }
    }
    for (int i = m_meta.reports.size() - 1; i >= 0; --i) {
        const QString rp = caseDir.absoluteFilePath(m_meta.reports[i]);
        if (!QFileInfo::exists(rp)) {
            m_meta.reports.remove(i);
            ++removed;
        }
    }
    if (removed > 0)
        setModified();
    return removed;
}

bool CaseManager::relocateVideo(const QString &id, const QString &newPath,
                                QString *error, bool *sizeMismatch,
                                bool force, const QString &knownSha)
{
    if (sizeMismatch) *sizeMismatch = false;
    auto *v = const_cast<CaseVideoRef *>(videoById(id));
    if (!v) {
        if (error) *error = QStringLiteral("案件中没有该文件 %1").arg(id);
        return false;
    }
    const QString norm = normPath(newPath);
    const QFileInfo fi(norm);
    if (!fi.exists() || !fi.isFile()) {
        if (error) *error = QStringLiteral("文件不存在：%1").arg(norm);
        return false;
    }
    // 同名同大小软匹配之外的大小不一致：默认拒绝（拍板§8-8 简化版）
    if (!force && v->sizeBytes > 0 && fi.size() != v->sizeBytes) {
        if (sizeMismatch) *sizeMismatch = true;
        if (error)
            *error = QStringLiteral(
                "大小不一致（登记 %1 字节，新文件 %2 字节），默认拒绝")
                .arg(v->sizeBytes).arg(fi.size());
        return false;
    }
    // 「仍要采用」留档（拍板§8-8）：extraFields 记录覆写轨迹
    if (v->sizeBytes > 0 && fi.size() != v->sizeBytes) {
        m_meta.extraFields[QStringLiteral("relocateOverride/") + id] =
            QStringLiteral("%1: %2 -> %3 (%4 -> %5 bytes)")
                .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                     v->originalPath, norm)
                .arg(v->sizeBytes)
                .arg(fi.size());
    }
    // M3 任务13：同尺寸但指纹不一致的「仍要采用」同样留档（指纹强制比对
    // 的覆写轨迹；旧登记指纹与新候选指纹一并记录）
    if (force && !knownSha.isEmpty() && !v->sha256.isEmpty()
        && v->sha256 != knownSha) {
        m_meta.extraFields[QStringLiteral("relocateShaOverride/") + id] =
            QStringLiteral("%1: %2 -> %3 (sha %4... -> %5...)")
                .arg(QDateTime::currentDateTime().toString(Qt::ISODate),
                     v->originalPath, norm,
                     v->sha256.left(8), knownSha.left(8));
    }
    const QString oldPath = v->originalPath;
    v->originalPath = norm;
    v->sizeBytes = fi.size();
    v->mtimeMs = fi.lastModified().toMSecsSinceEpoch();
    // 已知指纹（批量比对场景）直接登记免二次哈希；否则作废入队重算
    v->sha256 = knownSha;
    setModified();
    emit videoInfoChanged(id);
    emit videoRelocated(id, oldPath, norm);   // M3：状态键迁移用
    if (knownSha.isEmpty())
        queueVideoHash(id);
    return true;
}

// ---------------------------------------------------------------------------
// 批量重新定位（v1.3.0 M3 任务13）
// ---------------------------------------------------------------------------
QVector<CaseManager::CaseRelocateCandidate>
CaseManager::proposeRelocations(const QString &dir) const
{
    QVector<CaseRelocateCandidate> out;
    if (!m_open || dir.isEmpty())
        return out;
    // 收集目录下全部视频文件（递归）
    static const char *kExts[] = {
        "mp4", "avi", "mkv", "mov", "wmv", "flv", "webm",
        "ts", "m2ts", "dav", "mpg", "mpeg", "3gp"};
    QStringList files;
    QDirIterator it(dir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString f = it.next();
        const QString suf = QFileInfo(f).suffix().toLower();
        for (const char *e : kExts)
            if (suf == QLatin1String(e)) {
                files << f;
                break;
            }
    }
    for (const auto &v : m_meta.videos) {
        // 仅有效路径缺失的视频需要重定位（完整包副本兼底场景跳过）
        if (QFile::exists(effectivePathFor(v)))
            continue;
        CaseRelocateCandidate c;
        c.videoId = v.id;
        c.originalPath = v.originalPath;
        const QString wantName = QFileInfo(v.originalPath).fileName();
        for (const QString &f : files) {
            const QFileInfo fi(f);
            if (fi.fileName() != wantName)
                continue;
            // 名+大小一致为最佳（level 2），仅文件名一致次之（level 1）
            if (v.sizeBytes > 0 && fi.size() == v.sizeBytes) {
                if (c.matchLevel < 2) {
                    c.matchLevel = 2;
                    c.candidatePath = f;
                }
            } else if (c.matchLevel < 1) {
                c.matchLevel = 1;
                c.candidatePath = f;
            }
        }
        out.append(c);
    }
    return out;
}

bool CaseManager::computeSha256(const QString &path, QString *outHex,
                                std::atomic<bool> *abort)
{
    static std::atomic<bool> dummyAbort{false};
    return hashFileSha256(path, outHex, abort ? *abort : dummyAbort);
}

const CaseVideoRef *CaseManager::videoById(const QString &id) const
{
    // v1.7.1：findRef 覆盖 preprocessSessions[].outputRefs（P### 与 V###
    // 同待遇：哈希回填/徽标/编号）；仅 videos[] 语义处仍用 findVideo
    return m_open ? CaseModel::findRef(m_meta, id) : nullptr;
}

const CaseVideoRef *CaseManager::videoByPath(const QString &path) const
{
    if (!m_open)
        return nullptr;
    // v1.7.1：视频（V###）与前处理产物（P###）同待遇——本函数是
    // vlaPathFor / evidenceDirFor / timestampRoi / isCaseVideo /
    // updateCalibrationBadge 的统一分流中枢，通用查找两表
    const QString norm = normPath(path);
    for (const auto &v : m_meta.videos)
        if (v.originalPath == norm)
            return &v;
    for (const auto &s : m_meta.preprocessSessions)
        for (const auto &o : s.outputRefs)
            if (o.originalPath == norm)
                return &o;
    // 完整包接收端：播放包内 sources/ 副本时按 bundledRelPath 命中
    const QDir caseDir(m_caseDir);
    for (const auto &v : m_meta.videos) {
        if (!v.bundledRelPath.isEmpty()
            && normPath(caseDir.absoluteFilePath(v.bundledRelPath)) == norm)
            return &v;
    }
    return nullptr;
}

QString CaseManager::effectivePathFor(const CaseVideoRef &v) const
{
    if (QFile::exists(v.originalPath))
        return v.originalPath;
    if (!v.bundledRelPath.isEmpty()) {
        const QString bundled = QDir(m_caseDir).absoluteFilePath(v.bundledRelPath);
        if (QFile::exists(bundled))
            return bundled;
    }
    return v.originalPath;   // 缺失：原路径返回，调用方按 exists 判
}

int CaseManager::calibratedVideoCount() const
{
    if (!m_open)
        return 0;
    int n = 0;
    for (const auto &v : m_meta.videos)
        if (v.hasCalibration)
            ++n;
    for (const auto &s : m_meta.preprocessSessions)
        for (const auto &o : s.outputRefs)
            if (o.hasCalibration)
                ++n;
    return n;
}

// ---------------------------------------------------------------------------
// 路径分流
// ---------------------------------------------------------------------------
QString CaseManager::vlaPathFor(const QString &videoPath) const
{
    if (const auto *v = videoByPath(videoPath)) {
        if (!v->vlaRelPath.isEmpty())
            return QDir(m_caseDir).filePath(v->vlaRelPath);
        // 旧数据产物（无 vlaRelPath）：回退源旁 .vla（产物在案内 → 随案）
        return videoPath + QStringLiteral(".vla");
    }
    return videoPath + QStringLiteral(".vla");
}

QString CaseManager::evidenceDirFor(const QString &videoPath) const
{
    if (const auto *v = videoByPath(videoPath))
        return QDir(m_caseDir).filePath(
            QStringLiteral("evidence/calibration/") + v->id);
    return QFileInfo(videoPath).absoluteDir().absoluteFilePath(
        QStringLiteral("LumenArc_Calibration"));
}

// ---------------------------------------------------------------------------
// 框选记忆 / 校时徽标 / uiState
// ---------------------------------------------------------------------------
QRectF CaseManager::timestampRoiFor(const QString &videoPath) const
{
    if (const auto *v = videoByPath(videoPath))
        return v->timestampRoi;
    return QRectF();
}

void CaseManager::setTimestampRoi(const QString &videoPath, const QRectF &roi)
{
    if (auto *v = const_cast<CaseVideoRef *>(videoByPath(videoPath))) {
        if (v->timestampRoi != roi) {
            v->timestampRoi = roi;
            setModified();
        }
    }
}

void CaseManager::updateCalibrationBadge(const QString &videoPath, bool has,
                                         const QString &summary)
{
    // v1.7.1：视频（videos[]）与前处理产物（outputRefs P###）同待遇——
    // 用户实测：产物校时成功后无 ⏰ 徽标（videoByPath 只查 videos）
    CaseVideoRef *v = nullptr;
    const QString norm = normPath(videoPath);
    for (auto &vv : m_meta.videos)
        if (vv.originalPath == norm) { v = &vv; break; }
    if (!v) {
        for (auto &s : m_meta.preprocessSessions)
            for (auto &o : s.outputRefs)
                if (o.originalPath == norm) { v = &o; break; }
    }
    if (!v) {
        // 包内副本兜底（同 videoByPath 语义）
        for (auto &vv : m_meta.videos) {
            if (!vv.bundledRelPath.isEmpty()
                && QDir(m_caseDir).absoluteFilePath(vv.bundledRelPath) == norm) {
                v = &vv;
                break;
            }
        }
    }
    if (v && (v->hasCalibration != has || v->calibrationSummary != summary)) {
        v->hasCalibration = has;
        v->calibrationSummary = summary;
        setModified();
    }
}

void CaseManager::setLastVideoId(const QString &id)
{
    if (m_open && m_meta.lastVideoId != id) {
        m_meta.lastVideoId = id;
        setModified();
    }
}

// ---------------------------------------------------------------------------
// 前处理会话登记（M2 任务8）
// ---------------------------------------------------------------------------
bool CaseManager::addPreprocessSession(const QString &sessionDirAbs,
                                       const QString &reportCsvAbs,
                                       const QStringList &outputPaths,
                                       const QStringList &sidecarAbsPaths,
                                       const QMap<QString, QString> &outputChannels,
                                       QString *error)
{
    if (!m_open) {
        if (error) *error = QStringLiteral("没有打开的案件");
        return false;
    }
    const QDir caseDir(m_caseDir);
    const QString normSession = normPath(sessionDirAbs);
    const QString rel = caseDir.relativeFilePath(normSession);
    if (rel.startsWith(QStringLiteral("..")) || QDir::isAbsolutePath(rel)) {
        if (error)
            *error = QStringLiteral("会话目录不在案件内：%1").arg(normSession);
        return false;
    }

    CasePreprocessRef p;
    p.sessionDirRelPath = rel;
    if (!reportCsvAbs.isEmpty()) {
        const QString relCsv = caseDir.relativeFilePath(normPath(reportCsvAbs));
        if (!relCsv.startsWith(QStringLiteral(".."))
            && !QDir::isAbsolutePath(relCsv))
            p.reportCsvRelPath = relCsv;
    }
    // 输出引用制登记（P### 全局唯一编号——用户实测：会话内从 1 起导致
    // 跨会话 P001 碰撞，改编号/指纹/重定位全错位到首个产物）
    int outSeq = 1;
    for (const auto &s : m_meta.preprocessSessions)
        for (const auto &o : s.outputRefs)
            if (o.id.startsWith(QLatin1Char('P')))
                outSeq = qMax(outSeq, o.id.mid(1).toInt() + 1);
    for (const QString &o : outputPaths) {
        const QFileInfo fi(normPath(o));
        if (!fi.exists() || !fi.isFile())
            continue;   // 产物缺失不入册（部分失败场景 C2：日志已在协调器留痕）
        CaseVideoRef out;
        out.id = QStringLiteral("P%1").arg(outSeq++, 3, 10, QLatin1Char('0'));
        out.originalPath = fi.absoluteFilePath();
        out.sizeBytes = fi.size();
        out.mtimeMs = fi.lastModified().toMSecsSinceEpoch();
        // v1.7.1：摄像头编号自动继承前处理通道名（自定义可后改）
        out.cameraLabel = outputChannels.value(normPath(o));
        // v1.7.1：产物 vla 落会话目录内（随案移交；与视频 videos/V###.vla 同语义）
        const QString vlaRel = caseDir.relativeFilePath(normPath(o) + QStringLiteral(".vla"));
        if (!vlaRel.startsWith(QStringLiteral("..")) && !QDir::isAbsolutePath(vlaRel))
            out.vlaRelPath = vlaRel;
        p.outputRefs.append(out);
    }
    // sidecar 复制归类 sessionDir/sidecars/（原件保留在输出旁，拍板§8-11）
    if (!sidecarAbsPaths.isEmpty()) {
        const QString sidecarsDir = normSession + QStringLiteral("/sidecars");
        for (const QString &s : sidecarAbsPaths) {
            const QFileInfo fi(normPath(s));
            if (!fi.exists() || !fi.isFile())
                continue;
            QDir().mkpath(sidecarsDir);
            const QString dst = sidecarsDir + QLatin1Char('/') + fi.fileName();
            if (QFile::exists(dst))
                QFile::remove(dst);   // 同会话重跑：以最新为准
            if (QFile::copy(fi.absoluteFilePath(), dst))
                p.sidecarRelPaths.append(caseDir.relativeFilePath(dst));
        }
    }
    m_meta.preprocessSessions.append(p);
    setModified();
    // v1.7.1：产物指纹与视频同待遇——登记即入闲时哈希队列
    for (const auto &o : p.outputRefs)
        queueVideoHash(o.id);
    return true;
}

bool CaseManager::setCameraLabel(const QString &id, const QString &label,
                                 QString *error)
{
    if (!m_open) {
        if (error) *error = QStringLiteral("没有打开的案件");
        return false;
    }
    if (auto *v = CaseModel::findRef(m_meta, id)) {
        v->cameraLabel = label;
        setModified();
        return true;
    }
    if (error) *error = QStringLiteral("未找到登记条目：%1").arg(id);
    return false;
}

void CaseManager::setModified()
{
    if (!m_dirty) {
        m_dirty = true;
        emit caseDirtyChanged(true);
    }
}

// ---------------------------------------------------------------------------
// HashTask：哈希队列工作单元（QThreadPool×1，OS 线程置最低优先级）
// 三类：VideoHash（单路视频）/ Manifest（案内小文件清单回填）/ Verify（校验）
// ---------------------------------------------------------------------------
class CaseManager::HashTask : public QRunnable
{
public:
    enum Kind { VideoHash, Manifest, Verify };
    HashTask(CaseManager *mgr, Kind kind)
        : m_mgr(mgr), m_kind(kind)
    {
        setAutoDelete(true);
    }
    // VideoHash 载荷
    QString id, path;
    // Verify 载荷
    bool fullRehash = false;

    void run() override
    {
        QThread::currentThread()->setPriority(QThread::LowestPriority);
        switch (m_kind) {
        case VideoHash: runVideoHash(); break;
        case Manifest:  runManifest();  break;
        case Verify:    runVerify();    break;
        }
    }

private:
    void runVideoHash()
    {
        QString sha;
        if (!m_mgr->m_hashAbort.load())
            hashFileSha256(path, &sha, m_mgr->m_hashAbort);
        // 失败/中止 → sha 空：onVideoHashed 不计完成，队列计数照走
        QMetaObject::invokeMethod(m_mgr, "onVideoHashed", Qt::QueuedConnection,
                                  Q_ARG(QString, id), Q_ARG(QString, sha),
                                  Q_ARG(qint64, -1), Q_ARG(qint64, -1));
    }

    // 案内小文件清单（排除 manifest 自身/锁文件/sources 包内副本）
    void runManifest()
    {
        const QString root = m_mgr->m_caseDir;
        QJsonArray files;
        qint64 count = 0;
        QDir rootDir(root);
        auto it = QDirIterator(root, QDir::Files,
                               QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (m_mgr->m_hashAbort.load())
                break;
            const QString abs = it.next();
            const QString rel = rootDir.relativeFilePath(abs);
            if (rel == QLatin1String(kManifestName)
                || rel == QLatin1String(CaseModel::kLockName)
                || rel.startsWith(QStringLiteral("sources/")))
                continue;
            QString sha;
            if (!hashFileSha256(abs, &sha, m_mgr->m_hashAbort))
                continue;
            QJsonObject e;
            e[QStringLiteral("path")] = rel;
            e[QStringLiteral("sizeBytes")] = QFileInfo(abs).size();
            e[QStringLiteral("sha256")] = sha;
            files.append(e);
            ++count;
        }
        if (!m_mgr->m_hashAbort.load()) {
            QJsonObject rootObj;
            rootObj[QStringLiteral("magic")] = QStringLiteral("LumenArcManifest");
            rootObj[QStringLiteral("formatVersion")] = 1;
            rootObj[QStringLiteral("generatedMs")] =
                QDateTime::currentMSecsSinceEpoch();
            rootObj[QStringLiteral("files")] = files;
            QSaveFile f(root + u'/' + QString::fromLatin1(kManifestName));
            if (f.open(QIODevice::WriteOnly)) {
                f.write(QJsonDocument(rootObj).toJson(QJsonDocument::Indented));
                f.commit();
            }
        }
        QMetaObject::invokeMethod(m_mgr, "onManifestRefreshed",
                                  Qt::QueuedConnection, Q_ARG(int, int(count)));
    }

    void runVerify()
    {
        QVector<CaseIntegrityItem> items;
        const auto videos = m_mgr->m_meta.videos;   // 快照（值拷贝）
        for (const auto &v : videos) {
            CaseIntegrityItem it;
            it.label = v.id;
            it.path = v.originalPath;
            // 完整包接收端：原路径缺失时以包内副本为校验对象（内容一致）
            const QString eff = m_mgr->effectivePathFor(v);
            const QFileInfo fi(eff);
            if (!fi.exists()) {
                it.status = 2;   // 缺失
            } else {
                const bool isBundled = (eff != v.originalPath);
                // 包内副本 mtime 与登记值天然不同：不参与快扫变更判定；
                // size 不一致（副本损坏/调包）仍触发重算
                const bool metaChanged =
                    (fi.size() != v.sizeBytes)
                    || (!isBundled
                        && fi.lastModified().toMSecsSinceEpoch() != v.mtimeMs);
                if (!fullRehash && !metaChanged && !v.sha256.isEmpty()) {
                    it.status = 0;   // 快扫一致
                } else {
                    QString sha;
                    if (m_mgr->m_hashAbort.load())
                        break;
                    if (hashFileSha256(eff, &sha, m_mgr->m_hashAbort)
                        && !v.sha256.isEmpty()) {
                        it.status = (sha == v.sha256) ? 0 : 1;
                    } else if (sha.isEmpty()) {
                        it.status = 1;
                    } else {
                        it.status = 0;   // 此前未算：首算即登记值
                    }
                    QMetaObject::invokeMethod(
                        m_mgr, "onVideoHashed", Qt::QueuedConnection,
                        Q_ARG(QString, v.id), Q_ARG(QString, sha),
                        Q_ARG(qint64, fi.size()),
                        Q_ARG(qint64, fi.lastModified().toMSecsSinceEpoch()));
                }
            }
            items.append(it);
        }
        // manifest 覆盖不到的登记外文件：仅提示存在，不算不一致（略）
        QMetaObject::invokeMethod(m_mgr, "onVerifyDone", Qt::QueuedConnection,
                                  Q_ARG(QVector<CaseIntegrityItem>, items));
    }

    CaseManager *m_mgr;
    Kind m_kind;
};

// ---------------------------------------------------------------------------
// ExportTask：移交包导出工作单元（1MB 分块可复制可取消，半成品清理）
// ---------------------------------------------------------------------------
class CaseManager::ExportTask : public QRunnable
{
public:
    ExportTask(CaseManager *mgr, const QString &targetParentDir, bool full)
        : m_mgr(mgr)
        , m_parent(targetParentDir)
        , m_full(full)
        , m_caseDir(mgr->m_caseDir)
        , m_meta(mgr->m_meta)   // 快照：导出期间源案件继续编辑不影响本包
    {
        setAutoDelete(true);
    }

    void run() override
    {
        QThread::currentThread()->setPriority(QThread::LowestPriority);
        QString msg;
        const bool ok = doExport(&msg);
        QMetaObject::invokeMethod(m_mgr, "onExportFinished",
                                  Qt::QueuedConnection,
                                  Q_ARG(bool, ok), Q_ARG(QString, msg));
    }

private:
    void progress(int pct, const QString &stage)
    {
        if (pct != m_lastPct) {
            m_lastPct = pct;
            QMetaObject::invokeMethod(m_mgr, "onExportProgress",
                                      Qt::QueuedConnection,
                                      Q_ARG(int, pct), Q_ARG(QString, stage));
        }
    }

    bool copyFileChunked(const QString &src, const QString &dst,
                         const QString &stage)
    {
        QFile in(src), out(dst);
        if (!in.open(QIODevice::ReadOnly) || !out.open(QIODevice::WriteOnly))
            return false;
        while (!in.atEnd()) {
            if (m_mgr->m_exportAbort.load())
                return false;
            const QByteArray chunk = in.read(1 << 20);
            if (chunk.isEmpty())
                break;
            if (out.write(chunk) != chunk.size())
                return false;
            m_done += chunk.size();
            progress(m_total > 0 ? int(m_done * 100 / m_total) : 99, stage);
        }
        return true;
    }

    /// 清单重建（包内 manifest.json；与 M1 runManifest 同排除语义）
    bool writeManifest(const QString &root, QString *err)
    {
        QJsonArray files;
        const QDir rootDir(root);
        QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            if (m_mgr->m_exportAbort.load())
                return false;
            const QString abs = it.next();
            const QString rel = rootDir.relativeFilePath(abs);
            if (rel == QLatin1String(kManifestName)
                || rel == QLatin1String(CaseModel::kLockName)
                || rel.startsWith(QStringLiteral("sources/")))
                continue;
            QString sha;
            if (!hashFileSha256(abs, &sha, m_mgr->m_exportAbort))
                continue;
            QJsonObject e;
            e[QStringLiteral("path")] = rel;
            e[QStringLiteral("sizeBytes")] = QFileInfo(abs).size();
            e[QStringLiteral("sha256")] = sha;
            files.append(e);
        }
        QJsonObject rootObj;
        rootObj[QStringLiteral("magic")] = QStringLiteral("LumenArcManifest");
        rootObj[QStringLiteral("formatVersion")] = 1;
        rootObj[QStringLiteral("generatedMs")] =
            QDateTime::currentMSecsSinceEpoch();
        rootObj[QStringLiteral("files")] = files;
        QSaveFile f(root + QLatin1Char('/') + QString::fromLatin1(kManifestName));
        if (!f.open(QIODevice::WriteOnly)) {
            if (err) *err = QStringLiteral("manifest 写入失败");
            return false;
        }
        f.write(QJsonDocument(rootObj).toJson(QJsonDocument::Indented));
        return f.commit();
    }

    QString buildReadme(const QString &pkgType,
                        const QStringList &skipBundled) const
    {
        const CaseMeta &m = m_meta;
        QStringList lines;
        lines << QStringLiteral("追光者 Lumen Arc 案件移交包")
              << QStringLiteral("======================================")
              << QStringLiteral("案件编号：%1").arg(m.caseNo)
              << QStringLiteral("案件名称：%1").arg(m.title)
              << QStringLiteral("调查员：%1　单位：%2").arg(m.investigator, m.unit)
              << QStringLiteral("案发时间：%1")
                     .arg(QDateTime::fromMSecsSinceEpoch(m.incidentTimeMs)
                              .toString(QStringLiteral("yyyy-MM-dd")))
              << QStringLiteral("案发地点：%1 %2 %3")
                     .arg(m.city, m.district, m.locationDetail).trimmed()
              << QStringLiteral("导出时间：%1")
                     .arg(QDateTime::currentDateTime()
                              .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss")))
              << QStringLiteral("导出类型：%1").arg(pkgType)
              << QString()
              << QStringLiteral("内容清单：")
              << QStringLiteral("- case.json：案件登记（视频/分析/前处理/校时索引）")
              << QStringLiteral("- manifest.json：案内文件完整性清单（SHA-256）")
              << QStringLiteral("- videos/V###.vla：逐路分析结果")
              << QStringLiteral("- evidence/calibration/V###/：校时证据帧")
              << QStringLiteral("- preprocess/：前处理会话产物与报告")
              << (m_full ? QStringLiteral("- sources/V###__原名：源视频副本（完整包）")
                         : QStringLiteral("（轻量包不含源视频副本）"))
              << QString()
              << QStringLiteral("视频指纹（SHA-256）：");
        for (const auto &v : m.videos) {
            lines << QStringLiteral("%1  %2  %3")
                         .arg(v.id, v.sha256.isEmpty()
                                  ? QStringLiteral("（未算）") : v.sha256,
                              QFileInfo(v.originalPath).fileName());
        }
        lines << QString()
              << QStringLiteral("使用说明：")
              << QStringLiteral("1. 用 Lumen Arc（v1.3.0+）「案件 → 打开案件」选择本目录，"
                                "即可查看全部分析成果。")
              << (m_full
                      ? QStringLiteral("2. 完整包：视频已随包携带（sources/），零操作可播放核对。")
                      : QStringLiteral("2. 轻量包：首次打开后请按提示将各路视频重新定位到本机文件；"
                                       "定位后强制指纹比对，与上表一致方可采用。"))
              << QStringLiteral("3. 「案件 → 完整性校验」可随时复核案内文件未被篡改。")
              << QStringLiteral("4. 本包由 Lumen Arc v1.3.0 导出；案件数据只读引用，"
                                "源视频与 .vla 分析数据分离管理。");
        if (!skipBundled.isEmpty()) {
            lines << QString()
                  << QStringLiteral("注意：以下视频源文件导出时缺失，未随包携带副本：%1")
                         .arg(skipBundled.join(QStringLiteral("、")));
        }
        return lines.join(QLatin1Char('\n')) + QLatin1Char('\n');
    }

    bool doExport(QString *msg)
    {
        const QString pkgDir = m_parent + QLatin1Char('/')
            + CaseManager::caseDirName(m_meta);
        if (QDir(pkgDir).exists()) {
            *msg = QStringLiteral("目标目录已存在：%1").arg(pkgDir);
            return false;
        }
        // pkgDir 必须先建：轻量包且案内无文件时③④循环均空转，
        // ⑤ CaseModel::save(QSaveFile) 在不存在目录中必失败（v1.3.0 封版后
        // e2e 自检抓到的边缘 bug；writeManifest 同理随此修复）
        QDir().mkpath(pkgDir);
        // ① 收集案内文件（排除锁/manifest/sources）
        QStringList caseFiles;
        const QDir root(m_caseDir);
        QDirIterator it(m_caseDir, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            const QString abs = it.next();
            const QString rel = root.relativeFilePath(abs);
            if (rel == QLatin1String(kManifestName)
                || rel == QLatin1String(CaseModel::kLockName)
                || rel == QStringLiteral("case.json")   // 包内 case.json 后写（含 bundledRelPath）
                || rel.startsWith(QStringLiteral("sources/")))
                continue;
            caseFiles.append(abs);
            m_total += QFileInfo(abs).size();
        }
        // ② 完整包：源视频副本清单（有效路径：原路径缺失时以包内副本为源，
        //    接收端再导出完整包不丢副本；副本名仍按原名登记，不复发前缀）
        QVector<QPair<QString, QString>> sources;   // src → bundledRelPath
        QStringList skipBundled;
        if (m_full) {
            for (const auto &v : m_meta.videos) {
                const QString eff = m_mgr->effectivePathFor(v);
                const QFileInfo fi(eff);
                if (!fi.exists()) {
                    skipBundled << v.id;   // 缺失源：不携带，README 注明
                    continue;
                }
                const QString rel = QStringLiteral("sources/%1__%2")
                                        .arg(v.id, QFileInfo(v.originalPath).fileName());
                sources.append({eff, rel});
                m_total += fi.size();
            }
        }
        // ③ 复制
        progress(0, QStringLiteral("copying"));
        auto fail = [&](const QString &why) {
            QDir(pkgDir).removeRecursively();   // 半成品清理
            *msg = why;
            return false;
        };
        for (const QString &abs : caseFiles) {
            if (m_mgr->m_exportAbort.load())
                return fail(QStringLiteral("已取消（半成品已清理）"));
            const QString rel = root.relativeFilePath(abs);
            const QString dst = pkgDir + QLatin1Char('/') + rel;
            QDir().mkpath(QFileInfo(dst).absolutePath());
            if (!copyFileChunked(abs, dst, rel))
                return fail(m_mgr->m_exportAbort.load()
                                ? QStringLiteral("已取消（半成品已清理）")
                                : QStringLiteral("复制失败：%1").arg(rel));
        }
        // ④ sources 副本
        for (const auto &pr : sources) {
            if (m_mgr->m_exportAbort.load())
                return fail(QStringLiteral("已取消（半成品已清理）"));
            const QString dst = pkgDir + QLatin1Char('/') + pr.second;
            QDir().mkpath(QFileInfo(dst).absolutePath());
            if (!copyFileChunked(pr.first, dst, pr.second))
                return fail(m_mgr->m_exportAbort.load()
                                ? QStringLiteral("已取消（半成品已清理）")
                                : QStringLiteral("视频副本失败：%1").arg(pr.second));
        }
        // ⑤ 包内 case.json（bundledRelPath 写入包内副本；源案件不动）
        CaseMeta pkg = m_meta;
        for (auto &v : pkg.videos) {
            for (const auto &pr : sources) {
                if (pr.second.startsWith(QStringLiteral("sources/") + v.id
                                         + QStringLiteral("__")))
                    v.bundledRelPath = pr.second;
            }
        }
        {
            QString serr;
            if (!CaseModel::save(pkgDir, pkg, &serr))
                return fail(QStringLiteral("包内 case.json 写入失败：%1").arg(serr));
        }
        // ⑥ 包内 manifest
        {
            QString merr;
            progress(97, QStringLiteral("manifest"));
            if (!writeManifest(pkgDir, &merr))
                return fail(m_mgr->m_exportAbort.load()
                                ? QStringLiteral("已取消（半成品已清理）")
                                : merr);
        }
        // ⑦ README.txt
        {
            QFile f(pkgDir + QStringLiteral("/README.txt"));
            if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
                return fail(QStringLiteral("README 写入失败"));
            f.write(buildReadme(m_full ? QStringLiteral("完整包（含视频副本）")
                                       : QStringLiteral("轻量包（不含视频副本）"),
                                skipBundled).toUtf8());
        }
        // ⑧ 导后快校（完整包拍板§8-9：副本 sha 与登记值逐路比对）
        QStringList verifyNotes;
        if (m_full) {
            progress(98, QStringLiteral("verify"));
            for (const auto &pr : sources) {
                if (m_mgr->m_exportAbort.load())
                    return fail(QStringLiteral("已取消（半成品已清理）"));
                const QString vid = pr.second.mid(8, 4);   // sources/V###__
                const auto *v = CaseModel::findVideo(m_meta, vid);
                if (!v || v->sha256.isEmpty()) {
                    verifyNotes << QStringLiteral("%1 未算指纹，快校跳过").arg(vid);
                    continue;
                }
                QString sha;
                if (!hashFileSha256(pkgDir + QLatin1Char('/') + pr.second,
                                    &sha, m_mgr->m_exportAbort)
                    || sha != v->sha256)
                    return fail(QStringLiteral(
                        "导后快校失败：%1 副本指纹与登记不一致（包不可靠，已清理）")
                        .arg(vid));
            }
        }
        progress(100, QStringLiteral("done"));
        *msg = pkgDir;
        if (!verifyNotes.isEmpty())
            *msg += QStringLiteral("\n") + verifyNotes.join(QStringLiteral("\n"));
        return true;
    }

    CaseManager *m_mgr;
    QString m_parent;
    bool m_full;
    QString m_caseDir;
    CaseMeta m_meta;
    qint64 m_total = 0;
    qint64 m_done = 0;
    int m_lastPct = -1;
};

// ---------------------------------------------------------------------------
// 哈希队列（Q-9）
// ---------------------------------------------------------------------------
void CaseManager::queueVideoHash(const QString &id)
{
    const auto *v = CaseModel::findRef(m_meta, id);
    if (!v)
        return;
    if (!m_hashPool) {
        m_hashPool = new QThreadPool(this);
        m_hashPool->setMaxThreadCount(1);   // 单线程低 IO 抢占
    }
    auto *task = new HashTask(this, HashTask::VideoHash);
    task->id = id;
    task->path = v->originalPath;
    ++m_hashTotal;
    ++m_hashPending;
    m_hashPool->start(task);
}

void CaseManager::queueMissingHashes()
{
    if (!m_open)
        return;
    // v1.7.1：前处理产物（P###）与视频同待遇——一起入哈希队列
    QVector<CaseVideoRef> refs = m_meta.videos;
    for (const auto &s : m_meta.preprocessSessions)
        for (const auto &o : s.outputRefs)
            refs.append(o);
    for (const auto &v : refs) {
        // 完整包接收端：原路径缺失时以包内副本为准（内容一致，sha 语义不变）
        const QString eff = effectivePathFor(v);
        const QFileInfo fi(eff);
        if (!fi.exists())
            continue;   // 缺失视频不入队（重定位后再算）
        const bool changed =
            (fi.size() != v.sizeBytes)
            || (fi.lastModified().toMSecsSinceEpoch() != v.mtimeMs);
        // 包内副本 mtime 与登记值天然不同：sha 一致即视为一致，不触发重算
        if (v.sha256.isEmpty() || (changed && eff == v.originalPath))
            queueVideoHash(v.id);
    }
}

void CaseManager::queueAllHashes()
{
    if (!m_open)
        return;
    QVector<CaseVideoRef> refs = m_meta.videos;
    for (const auto &s : m_meta.preprocessSessions)
        for (const auto &o : s.outputRefs)
            refs.append(o);
    for (const auto &v : refs)
        if (QFileInfo::exists(v.originalPath))
            queueVideoHash(v.id);
}

void CaseManager::cancelHashes()
{
    m_hashAbort.store(true);
    if (m_hashPool)
        m_hashPool->waitForDone(2000);
}

void CaseManager::queueManifestRefresh()
{
    if (!m_open || m_hashAbort.load())
        return;
    if (!m_hashPool) {
        m_hashPool = new QThreadPool(this);
        m_hashPool->setMaxThreadCount(1);
    }
    m_hashPool->start(new HashTask(this, HashTask::Manifest));
}

void CaseManager::verifyIntegrity(bool fullRehash)
{
    if (!m_open || m_verifyPending)
        return;
    m_verifyPending = true;
    if (!m_hashPool) {
        m_hashPool = new QThreadPool(this);
        m_hashPool->setMaxThreadCount(1);
    }
    auto *task = new HashTask(this, HashTask::Verify);
    task->fullRehash = fullRehash;
    m_hashPool->start(task);
}

// ---------------------------------------------------------------------------
// 移交包导出（v1.3.0 M3 任务12）
// ---------------------------------------------------------------------------
CaseManager::CaseExportPrecheck CaseManager::exportPrecheck(
    const QString &targetDir, bool fullPackage) const
{
    CaseExportPrecheck pc;
    if (!m_open)
        return pc;
    // 案内体量（排除锁/manifest 自身/sources 包内副本——与 M1 清单同语义）
    const QDir root(m_caseDir);
    QDirIterator it(m_caseDir, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString abs = it.next();
        const QString rel = root.relativeFilePath(abs);
        if (rel == QLatin1String(kManifestName)
            || rel == QLatin1String(CaseModel::kLockName)
            || rel.startsWith(QStringLiteral("sources/")))
            continue;
        pc.caseBytes += QFileInfo(abs).size();
    }
    for (const auto &v : m_meta.videos) {
        if (v.sha256.isEmpty())
            ++pc.missingHashCount;
        // 有效路径（与哈希队列/完整性校验同语义）：原路径缺失但包内副本
        // 在场时不算缺失——完整包接收端自检不误报（v1.3.0 后对齐）
        const QFileInfo fi(effectivePathFor(v));
        if (!fi.exists()) {
            pc.missingVideoIds << v.id;
            continue;
        }
        if (fullPackage)
            pc.sourcesBytes += fi.size();
    }
    const QStorageInfo storage(targetDir);
    if (storage.isValid() && storage.bytesAvailable() >= 0) {
        pc.availableBytes = storage.bytesAvailable();
        const qint64 need = pc.caseBytes
            + (fullPackage ? pc.sourcesBytes : 0);
        pc.insufficientSpace = (pc.availableBytes < need * 11 / 10);  // 10% 余量
    }
    return pc;
}

void CaseManager::exportCase(const QString &targetParentDir, bool fullPackage)
{
    if (!m_open || m_exportActive.load())
        return;
    m_exportAbort.store(false);
    m_exportActive.store(true);
    if (!m_hashPool) {
        m_hashPool = new QThreadPool(this);
        m_hashPool->setMaxThreadCount(1);
    }
    // 复用单线程池：导出与哈希/校验串行（IO 纪律，不抢播放）
    m_hashPool->start(new ExportTask(this, targetParentDir, fullPackage));
}

void CaseManager::cancelExport()
{
    m_exportAbort.store(true);
}

void CaseManager::onExportProgress(int percent, const QString &stage)
{
    emit exportProgress(percent, stage);
}

void CaseManager::onExportFinished(bool ok, const QString &message)
{
    m_exportActive.store(false);
    emit exportFinished(ok, message);
}

// ---------------------------------------------------------------------------
// 工作线程结果回投（UI 线程）
// ---------------------------------------------------------------------------
void CaseManager::onVideoHashed(const QString &id, const QString &sha256,
                                qint64 sizeBytes, qint64 mtimeMs)
{
    if (m_hashTotal > 0) {
        if (m_hashPending > 0)
            --m_hashPending;
        ++m_hashDone;
        emit hashProgress(id, m_hashDone, m_hashTotal);
        if (m_hashPending == 0) {
            m_hashTotal = m_hashDone = 0;
            emit hashQueueFinished();
        }
    }
    if (!m_open || sha256.isEmpty())
        return;   // 中止/失败不登记
    if (auto *v = const_cast<CaseVideoRef *>(videoById(id))) {
        v->sha256 = sha256;
        // Verify 任务带回实测 size/mtime（变更后重登记）；队列任务保持 -1 不动
        if (sizeBytes >= 0)
            v->sizeBytes = sizeBytes;
        if (mtimeMs >= 0)
            v->mtimeMs = mtimeMs;
        setModified();
    }
}

void CaseManager::onManifestRefreshed(int fileCount)
{
    Q_UNUSED(fileCount);
}

void CaseManager::onVerifyDone(const QVector<CaseIntegrityItem> &items)
{
    m_verifyPending = false;
    emit integrityReportReady(items);
}
