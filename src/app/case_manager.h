/**
 * @file case_manager.h
 * @brief 案件管理器（app 层服务）：案件生命周期 + 视频登记 + 路径分流 SSOT
 * @author Huang Jingyun, Liu xinghua, Huang Wenhua
 * @date 2026-08-13
 * @version 1.0
 *
 * Copyright 2026 Huang Jingyun/Liu xinghua/Huang Wenhua. All rights reserved.
 * Licensed under the Apache License, Version 2.0
 *
 * 设计来源：docs/DEVELOPMENT_PLAN_V1.3_CN.md §2/§3。
 * SSOT（R5/R6）：打开的案件由 CaseManager 持有，MainWindow/PreprocessWindow
 * 经接口读写，禁止各处自行解析 case.json。
 * 模式规则：视频是否"在案"决定其行为（per-video membership）——
 * vlaPathFor/evidenceDirFor 对未入案视频返回独立模式老路径（逐字节照旧）。
 */
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>
#include <QRectF>
#include <QVector>
#include <QThreadPool>
#include <atomic>
#include "domain/case_model.h"

/// 完整性报告条目（integrityReportReady 载荷）
struct CaseIntegrityItem {
    QString label;      ///< 显示名：V### 或案内相对路径
    QString path;       ///< 绝对路径
    int status = 0;     ///< 0=一致 1=已变更 2=缺失
};
Q_DECLARE_METATYPE(CaseIntegrityItem)
Q_DECLARE_METATYPE(QVector<CaseIntegrityItem>)

class CaseManager : public QObject
{
    Q_OBJECT

public:
    explicit CaseManager(QObject *parent = nullptr);
    ~CaseManager() override;

    // ---- 状态 ----
    bool isOpen() const { return m_open; }
    QString caseDir() const { return m_caseDir; }
    const CaseMeta &meta() const { return m_meta; }   ///< 只读；修改经 API
    bool isDirty() const { return m_dirty; }

    // ---- 生命周期 ----
    /// 新建案件：在 parentDir 下建 <编号>-<名称>/ 与子目录，写 case.json。
    /// meta.caseNo 须已由对话框预览生成（generateCaseNo）。
    bool createCase(const QString &parentDir, const CaseMeta &meta,
                    QString *error);
    /// 打开案件：读+迁移+.lock。lockConflict 出参=true 表示锁占用
    /// （其他实例或上次未正常关闭），调用方可 force=true 强制打开。
    bool openCase(const QString &dir, QString *error,
                  QStringList *warnings = nullptr,
                  bool *lockConflict = nullptr, bool force = false);
    bool saveCase(QString *error);
    /// 关闭案件（dirty 提示由调用方负责）。移除锁文件。
    void closeCase();

    // ---- 最近案件（QSettings，最多 10 条，新→旧）----
    QStringList recentCases() const;
    void removeRecent(const QString &dir);

    // ---- 视频登记 ----
    /// 添加视频：分配 V###（高水位不复用）、登记 size/mtime、置 dirty。
    /// 重复路径拒绝（error 说明已有编号）。成功返回新 id，失败返回空串。
    QString addVideo(const QString &path, QString *error);
    /// 移除视频；deleteData=true 时连同案内 .vla 与校时证据帧一并删除。
    bool removeVideo(const QString &id, bool deleteData, QString *error);

    const CaseVideoRef *videoById(const QString &id) const;
    const CaseVideoRef *videoByPath(const QString &path) const;
    bool isCaseVideo(const QString &path) const
    { return videoByPath(path) != nullptr; }

    // ---- 路径分流（双模式核心）----
    /// 入案视频 → <案件>/videos/V###.vla；未入案/无案件 → 视频路径+".vla"
    QString vlaPathFor(const QString &videoPath) const;
    /// 入案视频 → <案件>/evidence/calibration/<V###>；
    /// 未入案/无案件 → <视频目录>/LumenArc_Calibration（v1.2.x 老行为）
    QString evidenceDirFor(const QString &videoPath) const;

    // ---- 框选记忆（随案；独立模式仍由 MainWindow 走 QSettings）----
    QRectF timestampRoiFor(const QString &videoPath) const;
    void setTimestampRoi(const QString &videoPath, const QRectF &roi);

    // ---- 校时徽标缓存（写 .vla 时同步刷新；真数据以 .vla 为准）----
    void updateCalibrationBadge(const QString &videoPath, bool has,
                                const QString &summary);

    // ---- uiState ----
    void setLastVideoId(const QString &id);

    void setModified();   ///< 登记变更 → dirty

    // ---- 哈希队列（Q-9：闲时后台单线程低优先级，逐文件完成提示）----
    void queueMissingHashes();   ///< 未算/已变更的入队（开案自动调，手动也可）
    void queueAllHashes();       ///< 「统一计算哈希」全量重算
    void cancelHashes();         ///< 关案/退出前置取消
    bool hashQueueActive() const { return m_hashPending > 0; }

    // ---- 完整性校验（异步：快扫 size/mtime + 差异/全量重算比对）----
    void verifyIntegrity(bool fullRehash);

    // ---- manifest.json（机器维护：保存后队列回填 + 校验时重建）----
    void queueManifestRefresh();

signals:
    void caseOpened(const QString &dir);
    void caseClosed();
    void caseDirtyChanged(bool dirty);
    void caseSaved();
    void videoAdded(const QString &id);
    void videoRemoved(const QString &id);
    /// 哈希逐文件完成（Q-9 逐文件提示）：done/total 为队列进度
    void hashProgress(const QString &videoId, int done, int total);
    void hashQueueFinished();
    /// 完整性校验报告：status 0=一致 1=已变更 2=缺失
    void integrityReportReady(const QVector<CaseIntegrityItem> &items);

private slots:
    // 工作线程结果回投（QueuedConnection，UI 线程更新 meta）
    void onVideoHashed(const QString &id, const QString &sha256,
                       qint64 sizeBytes, qint64 mtimeMs);
    void onManifestRefreshed(int fileCount);
    void onVerifyDone(const QVector<CaseIntegrityItem> &items);

private:
    void pushRecent(const QString &dir);
    bool createLock(QString *error);
    void removeLock();
    static QString caseDirName(const CaseMeta &meta);
    void queueVideoHash(const QString &id);   ///< 单路入队（内部）

    QString m_caseDir;
    CaseMeta m_meta;
    bool m_open = false;
    bool m_dirty = false;

    // 哈希队列状态（QThreadPool×1，worker 经 QueuedConnection 回投）
    class HashTask;
    QThreadPool *m_hashPool = nullptr;
    std::atomic<bool> m_hashAbort{false};
    std::atomic<int> m_hashPending{0};
    int m_hashTotal = 0, m_hashDone = 0;
    bool m_verifyPending = false;
    friend class HashTask;
};
