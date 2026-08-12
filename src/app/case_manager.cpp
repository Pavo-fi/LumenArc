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
        if (lockConflict) *lockConflict = true;
        if (error)
            *error = QStringLiteral("案件已被其他实例打开，或上次未正常关闭");
        return false;
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
    if (error) *error = QStringLiteral("案件中没有视频 %1").arg(id);
    return false;
}

const CaseVideoRef *CaseManager::videoById(const QString &id) const
{
    return m_open ? CaseModel::findVideo(m_meta, id) : nullptr;
}

const CaseVideoRef *CaseManager::videoByPath(const QString &path) const
{
    if (!m_open)
        return nullptr;
    const QString norm = normPath(path);
    for (const auto &v : m_meta.videos)
        if (v.originalPath == norm)
            return &v;
    return nullptr;
}

// ---------------------------------------------------------------------------
// 路径分流
// ---------------------------------------------------------------------------
QString CaseManager::vlaPathFor(const QString &videoPath) const
{
    if (const auto *v = videoByPath(videoPath))
        return QDir(m_caseDir).filePath(v->vlaRelPath);
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
    if (auto *v = const_cast<CaseVideoRef *>(videoByPath(videoPath))) {
        if (v->hasCalibration != has || v->calibrationSummary != summary) {
            v->hasCalibration = has;
            v->calibrationSummary = summary;
            setModified();
        }
    }
}

void CaseManager::setLastVideoId(const QString &id)
{
    if (m_open && m_meta.lastVideoId != id) {
        m_meta.lastVideoId = id;
        setModified();
    }
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
            const QFileInfo fi(v.originalPath);
            if (!fi.exists()) {
                it.status = 2;   // 缺失
            } else {
                const bool metaChanged =
                    (fi.size() != v.sizeBytes)
                    || (fi.lastModified().toMSecsSinceEpoch() != v.mtimeMs);
                if (!fullRehash && !metaChanged && !v.sha256.isEmpty()) {
                    it.status = 0;   // 快扫一致
                } else {
                    QString sha;
                    if (m_mgr->m_hashAbort.load())
                        break;
                    if (hashFileSha256(v.originalPath, &sha, m_mgr->m_hashAbort)
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
// 哈希队列（Q-9）
// ---------------------------------------------------------------------------
void CaseManager::queueVideoHash(const QString &id)
{
    const auto *v = videoById(id);
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
    for (const auto &v : m_meta.videos) {
        const QFileInfo fi(v.originalPath);
        if (!fi.exists())
            continue;   // 缺失视频不入队（重定位后再算）
        const bool changed =
            (fi.size() != v.sizeBytes)
            || (fi.lastModified().toMSecsSinceEpoch() != v.mtimeMs);
        if (v.sha256.isEmpty() || changed)
            queueVideoHash(v.id);
    }
}

void CaseManager::queueAllHashes()
{
    if (!m_open)
        return;
    for (const auto &v : m_meta.videos)
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
